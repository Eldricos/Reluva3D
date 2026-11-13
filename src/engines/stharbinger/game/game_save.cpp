// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Saved games, in DEEP.EXE's format (Save_Game 0x1EABB, Load_Saved_Game
// 0x1F91F), so the original's saves load here and the other way round. Indexes
// follow the original: scenes in DS9.LCC order, variables by DS9.LVM, dialogue
// slots by DS9.LDM, animations, hotspots and timers by the scene's lists, and
// script positions as directive indexes.

#include "engines/stharbinger/game/game.h"

#include "common/fs.h"
#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace reluva::stharbinger {

namespace {

const int kSlots = 300; // animation, hotspot, timer and dialogue slots
const int kNumGlobals = 3000;
const uint16_t kNone16 = 0xFFFF;
const uint32_t kNone32 = 0xFFFF;
const char *const kSettingsFile = "dlas.ds9";

// Reads the little-endian fields of a save, failing softly at the end.
struct SaveReader {
    const std::vector<uint8_t> &d;
    size_t pos = 0;
    bool bad = false;

    explicit SaveReader(const std::vector<uint8_t> &data) : d(data) {}
    bool need(size_t n) {
        if (pos + n > d.size())
            bad = true;
        return !bad;
    }
    uint8_t u8() { return need(1) ? d[pos++] : 0; }
    uint16_t u16() {
        if (!need(2))
            return 0;
        uint16_t v = uint16_t(d[pos] | (d[pos + 1] << 8));
        pos += 2;
        return v;
    }
    uint32_t u32() {
        uint32_t lo = u16();
        return lo | (uint32_t(u16()) << 16);
    }
    std::string cstr() {
        std::string s;
        while (need(1) && d[pos])
            s += char(d[pos++]);
        if (need(1))
            pos++;
        return s;
    }
    const uint8_t *bytes(size_t n) {
        if (!need(n))
            return nullptr;
        const uint8_t *p = d.data() + pos;
        pos += n;
        return p;
    }
};

int findName(const std::vector<std::string> &names, const std::string &name) {
    for (size_t i = 0; i < names.size(); i++)
        if (text::equalsIgnoreCase(names[i], name))
            return int(i);
    return -1;
}

} // namespace

// As in the game's folder: settings in dlas.ds9, saved games in savegame/
// (original saves can be copied there), in the game's own folder of
// Reluva's (GameTarget::savePath).
static std::string settingsDirectory(Platform *system) {
    return system->getSavePath();
}

std::string Game::getSaveDirectory() const {
    std::string base = settingsDirectory(_system);
    if (base.empty())
        return base;
    const std::filesystem::path dir = std::filesystem::path(base) / "savegame";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

uint32_t Game::getResumePc() const {
    // Idle on a RETURN, or the directive the script stopped at.
    return _thread.active ? _thread.pc : _idlePc;
}

// ---- Settings (dlas.ds9: difficulty, autosave, last save shown) --------------

void Game::loadSettings() {
    std::vector<uint8_t> d;
    std::string dir = settingsDirectory(_system);
    if (dir.empty() || !readFile((std::filesystem::path(dir) / kSettingsFile), d) || d.size() < 12)
        return;
    SaveReader r(d);
    uint32_t difficulty = r.u32();
    _arcadeDifficulty = difficulty <= 2 ? int(difficulty) : 1;
    _autosave = r.u32() != 0;
    _lastSave = int(r.u32());
}

void Game::saveSettings() {
    std::string dir = settingsDirectory(_system);
    if (dir.empty())
        return;
    MemoryWriteStream w;
    w.writeUint32LE(uint32_t(_arcadeDifficulty));
    w.writeUint32LE(_autosave ? 1 : 0);
    w.writeUint32LE(uint32_t(_lastSave));
    writeFile((std::filesystem::path(dir) / kSettingsFile), w.data());
}

// ---- Saving --------------------------------------------------------------------

bool Game::saveGame(const std::string &fileName, const std::string &description, uint32_t resumePc) {
    std::string dir = getSaveDirectory();
    if (dir.empty() || !_scene)
        return false;
    const auto &linkScenes = _resources.getLinkScenes();
    const auto &linkVars = _resources.getLinkVariables();
    const auto &linkDialogues = _resources.getLinkDialogues();
    int linkScene = findName(linkScenes, _scene->name);
    if (linkScene < 0) {
        log::warning("Cannot save: scene %s is not in DS9.LCC", _scene->name.c_str());
        return false;
    }
    auto labelTarget = [&](int label) -> uint32_t {
        auto it = _labels.find(uint32_t(label));
        return label < 0 || it == _labels.end() ? kNone32 : it->second;
    };

    MemoryWriteStream w;
    w.writeUint16LE(uint16_t(_disc));
    w.writeUint32LE(_tricorderIcon ? 1 : 0);
    w.writeUint32LE(uint32_t(_mapLocation));
    w.writeUint32LE(_tickPeriod);
    w.writeUint32LE(_tickPhase);
    w.write("ds9", 4);
    w.write(description.c_str(), uint32_t(description.size() + 1));
    // The picture without cursor or menu, and the palette (6-bit).
    if (_menuGameScreen.w != 640 || _menuGameScreen.h != 480)
        draw::create(_menuGameScreen, 640, 480);
    w.write(_menuGameScreen.pixels.data(), 640 * 480);
    for (int i = 0; i < 768; i++)
        w.writeByte(uint8_t(_menuGamePalette.data()[i] >> 2));
    w.writeUint32LE(uint32_t(_paletteBackground));
    w.writeUint16LE(uint16_t(linkScene));
    w.writeUint32LE(uint32_t(_thread.callStack.size()));
    for (uint32_t pc : _thread.callStack)
        w.writeUint32LE(pc);

    // Dialogue slots, by DS9.LDM index.
    std::vector<int> slotDialogue(kSlots, -1);
    for (const auto &d : _dialogues) {
        int slot = findName(linkDialogues, getDialogueName(d.first));
        if (slot >= 0 && slot < kSlots)
            slotDialogue[slot] = d.first;
    }
    for (int slot = 0; slot < kSlots; slot++) {
        if (slotDialogue[slot] < 0) {
            w.writeByte(0xFF);
            continue;
        }
        const DialogueState &st = _dialogues[slotDialogue[slot]];
        w.writeByte(0);
        w.write(linkDialogues[slot].c_str(), uint32_t(linkDialogues[slot].size() + 1));
        w.writeUint16LE(uint16_t(st.cur));
        w.writeByte(st.replyMode ? 1 : 0);
        w.writeByte(st.rudeGroup);
        w.writeByte(st.rude ? 1 : 0);
        w.writeByte(st.backGroup);
        w.writeByte(st.exit ? 1 : 0);
        w.write(st.done, kMaxGroups);
        w.write(st.emptyGoto, kMaxGroups);
        w.write(st.emptyReply, kMaxGroups);
        for (const auto *flags : {&st.npcFlags, &st.choiceFlags}) {
            for (int g = 0; g < kMaxGroups; g++) {
                if (g >= int(flags->size()) || (*flags)[g].empty()) {
                    w.writeByte(0xFF);
                    continue;
                }
                w.writeByte(uint8_t((*flags)[g].size()));
                for (const auto &f : (*flags)[g]) {
                    w.writeByte(f.on ? 1 : 0);
                    w.writeByte(f.said ? 1 : 0);
                }
            }
        }
        for (const auto *table : {st.doneX, st.emptyXGoto, st.emptyXReply}) {
            for (int g = 0; g < kMaxGroups; g++) {
                const DialogueState::Redirect &r = table[g];
                if (r.target < 0) {
                    w.writeByte(0xFF);
                    continue;
                }
                w.writeByte(uint8_t(r.target));
                w.writeByte(uint8_t(r.except.group));
                w.writeByte(uint8_t(r.except.item));
            }
        }
        for (int g = 0; g < kMaxGroups; g++) {
            bool present = g < int(st.data.npcLines.size()) && !st.data.npcLines[g].empty() && g < 49;
            w.writeByte(present ? uint8_t(g) : 0xFF);
            w.writeByte(present ? uint8_t(st.current[g]) : 0);
        }
        for (int g = 0; g < kMaxGroups; g++)
            w.writeByte(g < int(st.current.size()) ? uint8_t(st.current[g]) : 0);
        for (int g = 0; g < kMaxGroups; g++) {
            w.writeByte(uint8_t(st.hooks[g].size()));
            for (const auto &h : st.hooks[g]) {
                w.writeByte(h.state == 2 ? 0xFF : uint8_t(h.state));
                w.writeByte(uint8_t(h.group));
            }
        }
    }

    // Global variables by DS9.LVM index.
    std::vector<uint16_t> globals(kNumGlobals, 0);
    for (const auto &v : _variables) {
        int i = findName(linkVars, v.first);
        if (i >= 0 && i < kNumGlobals)
            globals[i] = v.second;
    }
    for (uint16_t g : globals)
        w.writeUint16LE(g);
    w.writeUint16LE(uint16_t(_backgroundIndex < 0 ? kNone16 : _backgroundIndex));

    // Animations: a 0x34-byte record as in memory, then frame positions.
    for (int i = 0; i < kSlots; i++) {
        if (i >= int(_anims.size()) || !_anims[i].loaded) {
            w.writeByte(0xFF);
            continue;
        }
        const Anim &a = _anims[i];
        w.writeByte(a.lazy ? 1 : 0);
        uint8_t rec[0x34] = {};
        auto put16 = [&](int off, int v) {
            rec[off] = uint8_t(v);
            rec[off + 1] = uint8_t(v >> 8);
        };
        put16(0x00, a.x);
        put16(0x02, a.y);
        put16(0x04, a.rate);
        put16(0x06, a.countdown);
        rec[0x08] = a.flags;
        rec[0x09] = uint8_t(a.priority);
        put16(0x14, a.rle.frameCount());
        rec[0x26] = a.defined ? 0 : 0xFF;
        rec[0x27] = a.suspended ? 1 : 0;
        w.write(rec, sizeof(rec));
        w.writeUint32LE(a.frame < 0 ? kNone32 : uint32_t(a.frame));
        w.writeUint32LE(a.drawnFrame >= 0 ? uint32_t(a.drawnFrame) : kNone32);
        w.writeUint32LE(labelTarget(a.endLabel));
        w.writeUint32LE(kNone32); // ANIMARRV: never used
        w.writeByte(0);           // ANIMFRAM: never used
    }

    for (int i = 0; i < kSlots; i++) {
        if (i >= int(_hotspots.size()) || !_hotspots[i].defined) {
            w.writeByte(0xFF);
            continue;
        }
        const Hotspot &h = _hotspots[i];
        w.writeByte(0);
        w.writeUint16LE(uint16_t(h.x));
        w.writeUint16LE(uint16_t(h.y));
        w.writeUint16LE(uint16_t(h.w));
        w.writeUint16LE(uint16_t(h.h));
        w.writeByte(h.enabled ? 1 : 0);
        w.writeUint16LE(uint16_t(h.label));
        w.writeByte(uint8_t(h.cursor));
    }
    w.writeUint32LE(resumePc);
    for (int i = 0; i < 2; i++)
        w.writeUint32LE(i < int(_ambientSounds.size()) ? uint32_t(_ambientSounds[i]) : kNone32);
    for (int i = 0; i < kSlots; i++) {
        if (i >= int(_timers.size()) || !_timers[i].defined) {
            w.writeByte(0xFF);
            continue;
        }
        const Timer &t = _timers[i];
        w.writeByte(0);
        w.writeUint16LE(t.reload);
        w.writeUint16LE(t.count);
        w.writeUint16LE(uint16_t(t.label));
        w.writeByte(t.active ? 1 : 0);
        w.writeByte(uint8_t(t.kind));
    }

    std::string path = (std::filesystem::path(dir) / fileName).string();
    if (!writeFile(path, w.data())) {
        log::warning("Cannot write %s", path.c_str());
        return false;
    }
    log::debug(1, "Saved %s (%s) at %s, directive %u", fileName.c_str(), description.c_str(), _scene->name.c_str(),
               resumePc);
    return true;
}

// ---- Loading -------------------------------------------------------------------

bool Game::readSaveInfo(const std::string &path, SaveInfo &info) {
    std::vector<uint8_t> d;
    if (!readFile(path, d))
        return false;
    SaveReader r(d);
    r.u16();
    r.u32();
    r.u32();
    r.u32();
    r.u32();
    r.cstr();
    info.description = r.cstr();
    const uint8_t *shot = r.bytes(640 * 480);
    const uint8_t *pal = r.bytes(768);
    if (r.bad)
        return false;
    info.screenshot.assign(shot, shot + 640 * 480);
    for (int i = 0; i < 768; i++)
        info.palette[i] = uint8_t(((pal[i] & 0x3F) << 2) | ((pal[i] & 0x3F) >> 4));
    return true;
}

bool Game::loadGame(const std::string &fileName) {
    std::vector<uint8_t> d;
    std::string dir = getSaveDirectory();
    if (dir.empty() || !readFile((std::filesystem::path(dir) / fileName), d)) {
        log::warning("Cannot read saved game %s", fileName.c_str());
        return false;
    }
    const auto &linkScenes = _resources.getLinkScenes();
    const auto &linkVars = _resources.getLinkVariables();
    SaveReader r(d);
    int disc = r.u16();
    bool tricorder = r.u32() != 0;
    int mapLocation = int(r.u32());
    uint32_t tickPeriod = r.u32();
    uint32_t tickPhase = r.u32();
    r.cstr();
    r.cstr();
    r.bytes(640 * 480);
    const uint8_t *pal = r.bytes(768);
    int paletteBackground = int(r.u32());
    int linkScene = r.u16();
    if (r.bad || linkScene >= int(linkScenes.size())) {
        log::warning("%s is not a saved game of this game", fileName.c_str());
        return false;
    }
    int sceneIndex = _vm->findScene(linkScenes[linkScene]);
    if (sceneIndex < 0) {
        log::warning("%s: unknown scene %s", fileName.c_str(), linkScenes[linkScene].c_str());
        return false;
    }

    // Leave whatever runs now and set the scene up as on entry, without its script.
    if (_conv.active) {
        _conv.ops.clear();
        _conv.result = -1;
        finishConversation();
    }
    _video = VideoPlayback();
    _arcade.reset();
    if (_scene)
        leaveScene();
    if (audio::Mixer *mixer = _system->getMixer())
        mixer->stopAll();
    _sceneIndex = sceneIndex;
    _scene = &_scenes[sceneIndex];
    buildLabels();
    _hotspots.assign(_scene->lists[kListHotspots].size(), Hotspot());
    _timers.assign(_scene->lists[kListTimers].size(), Timer());
    setupAnims();
    _thread = Thread();
    _pendingScene = -1;

    uint32_t depth = r.u32();
    for (uint32_t i = 0; i < depth && i < 24 && !r.bad; i++)
        _thread.callStack.push_back(r.u32()); // (the original misreads these)

    // Dialogues: reloaded by name, then their state.
    _dialogues.clear();
    for (int slot = 0; slot < kSlots && !r.bad; slot++) {
        if (r.u8() == 0xFF)
            continue;
        std::string name = r.cstr();
        DialogueState scratch;
        DialogueState *st = getDialogueState(findDialogue(name));
        if (!st)
            st = &scratch; // unknown: read and drop
        st->cur = r.u16();
        st->replyMode = r.u8() != 0;
        st->rudeGroup = r.u8();
        st->rude = r.u8() != 0;
        st->backGroup = r.u8();
        st->exit = r.u8() != 0;
        for (uint8_t *table : {st->done, st->emptyGoto, st->emptyReply})
            if (const uint8_t *p = r.bytes(kMaxGroups))
                std::memcpy(table, p, kMaxGroups);
        for (auto *flags : {&st->npcFlags, &st->choiceFlags}) {
            for (int g = 0; g < kMaxGroups; g++) {
                uint8_t n = r.u8();
                if (n == 0xFF)
                    continue;
                for (int i = 0; i < n; i++) {
                    bool on = r.u8() != 0;
                    bool said = r.u8() != 0;
                    if (g < int(flags->size()) && i < int((*flags)[g].size()))
                        (*flags)[g][i] = {on, said};
                }
            }
        }
        for (DialogueState::Redirect *table : {st->doneX, st->emptyXGoto, st->emptyXReply}) {
            for (int g = 0; g < kMaxGroups; g++) {
                uint8_t target = r.u8();
                if (target == 0xFF) {
                    table[g] = DialogueState::Redirect();
                    continue;
                }
                table[g].target = target;
                table[g].except.npc = false;
                table[g].except.group = r.u8();
                table[g].except.item = r.u8();
            }
        }
        for (int g = 0; g < kMaxGroups; g++) {
            uint8_t group = r.u8();
            uint8_t index = r.u8();
            if (group != 0xFF && group < st->current.size())
                st->current[group] = std::min<int>(index, int(st->data.npcLines[group].size()) - 1);
        }
        r.bytes(kMaxGroups);
        for (int g = 0; g < kMaxGroups; g++) {
            st->hooks[g].clear();
            uint8_t n = r.u8();
            for (int i = 0; i < n; i++) {
                uint8_t state = r.u8();
                uint8_t group = r.u8();
                st->hooks[g].push_back({group, state == 0xFF ? 2 : state});
            }
        }
    }

    _variables.clear();
    for (int i = 0; i < kNumGlobals && !r.bad; i++) {
        uint16_t v = r.u16();
        if (v && i < int(linkVars.size()))
            _variables[fs::toLower(linkVars[i])] = v;
    }
    int background = r.u16();

    auto labelAt = [&](uint32_t pc) {
        for (const auto &l : _labels)
            if (l.second == pc)
                return int(l.first);
        return -1;
    };
    for (int i = 0; i < kSlots && !r.bad; i++) {
        uint8_t kind = r.u8();
        if (kind == 0xFF)
            continue;
        const uint8_t *rec = r.bytes(0x34);
        uint32_t frame = r.u32();
        uint32_t drawn = r.u32();
        uint32_t endPc = r.u32();
        r.u32();
        uint8_t nodes = r.u8();
        for (int k = 0; k < nodes; k++)
            r.bytes(8);
        if (!rec || i >= int(_anims.size()))
            continue;
        if (!_anims[i].loaded)
            loadAnim(i, kind == 1);
        Anim &a = _anims[i];
        a.x = int16_t(rec[0] | (rec[1] << 8));
        a.y = int16_t(rec[2] | (rec[3] << 8));
        a.rate = rec[4] | (rec[5] << 8);
        a.countdown = rec[6] | (rec[7] << 8);
        a.flags = rec[8];
        a.priority = std::min<int>(rec[9], 4); // the renderer draws levels 0..4 only
        a.defined = rec[0x26] == 0;
        a.suspended = false;
        a.frame = frame == kNone32 ? -1 : int(frame);
        a.drawnFrame = drawn == kNone32 ? -1 : int(drawn);
        a.drawnX = a.x;
        a.drawnY = a.y;
        a.endLabel = endPc == kNone32 ? -1 : labelAt(endPc);
    }
    for (int i = 0; i < kSlots && !r.bad; i++) {
        if (r.u8() == 0xFF)
            continue;
        Hotspot h;
        h.defined = true;
        h.x = r.u16();
        h.y = r.u16();
        h.w = r.u16();
        h.h = r.u16();
        h.enabled = r.u8() != 0;
        h.label = r.u16();
        h.cursor = r.u8();
        if (i < int(_hotspots.size()))
            _hotspots[i] = h;
    }
    uint32_t resume = r.u32();
    int ambients[2] = {int(r.u32()), int(r.u32())};
    for (int i = 0; i < kSlots && !r.bad; i++) {
        if (r.u8() == 0xFF)
            continue;
        Timer t;
        t.defined = true;
        t.reload = r.u16();
        t.count = r.u16();
        t.label = r.u16();
        t.active = r.u8() != 0;
        t.kind = r.u8();
        if (i < int(_timers.size()))
            _timers[i] = t;
    }
    if (r.bad)
        log::warning("%s is truncated; loaded what was there", fileName.c_str());

    // The picture, the palette and the sounds.
    _disc = disc;
    _tricorderIcon = tricorder;
    _tickPeriod = tickPeriod;
    _tickPhase = tickPhase;
    _beams.clear();
    _beamLife = 0;
    if (_tickPeriod != 0xFFFF && _tickSound.empty())
        if (auto s = _resources.openFile("SOUND/TICK.RAW"))
            _tickSound = s->readAll();
    _mapLocation = disc == 1 ? 0xFF : mapLocation;
    _mapEnabled = true;
    _dead = false;
    if (background != kNone16)
        setBackground(background, false);
    if (pal) {
        for (int i = 0; i < 256; i++)
            _palette.setColor(i, uint8_t((pal[i * 3] & 0x3F) << 2 | (pal[i * 3] & 0x3F) >> 4),
                              uint8_t((pal[i * 3 + 1] & 0x3F) << 2 | (pal[i * 3 + 1] & 0x3F) >> 4),
                              uint8_t((pal[i * 3 + 2] & 0x3F) << 2 | (pal[i * 3 + 2] & 0x3F) >> 4));
        _fadeLevel = 256;
        applyPalette();
    }
    _paletteBackground = paletteBackground;
    for (int sound : ambients) {
        if (sound != int(kNone32) && sound < int(_scene->lists[kListSounds].size())) {
            Argument ambient;
            ambient.type = kArgSymbol;
            ambient.value = 0;
            playSound(sound, &ambient);
        }
    }

    // The script carries on where it was: idle on a RETURN, or at the
    // directive (an autosave restarts its action sequence).
    _thread.pc = resume;
    uint8_t cmd = resume < _scene->script.size() ? _scene->script[resume].command : 0;
    if (cmd == kCmdReturn || cmd == kCmdReturnVal) {
        _idlePc = resume;
        _thread.active = false;
        _thread.pc = resume + 1;
    } else {
        _thread.active = resume < _scene->script.size();
    }
    _status.clear();
    updateCaption();
    log::debug(1, "Loaded %s: scene %s, directive %u", fileName.c_str(), _scene->name.c_str(), resume);
    return true;
}

void Game::startNewGame() {
    // Everything zero, then OPENING (the first scene in DS9.LCC).
    _variables.clear();
    _dialogues.clear();
    _arcadeGroups.clear();
    _lastArcadeDamage = 0;
    _tricorderIcon = false;
    _tickPeriod = 0xFFFF;
    _tickPhase = 0;
    _beams.clear();
    _beamLife = 0;
    _mapLocation = 0xFF;
    _dead = false;
    _paletteBackground = 299;
    _palette = Palette();
    _fadeLevel = 256;
    applyPalette();
    draw::create(_background, kScreenWidth, kScreenHeight);
    _workBufferStale = true;
    draw::fill(_screen, 0);
    if (_scene)
        leaveScene();
    int start = _vm->findScene("OPENING");
    _pendingScene = start < 0 ? 0 : start;
    switchScenes();
}

void Game::autosave(int type) {
    // Before some action sequences, as savegame\<name>save.ds9 (option on by default).
    static const struct {
        int type;
        const char *file;
        const char *title;
    } kAutosaves[] = {
        {0x04, "holosave.ds9", "Holosuite Simulation: Citadel Run"},
        {0x05, "adr2save.ds9", "Drones attack Deep Space Nine"},
        {0x06, "plansave.ds9", "Kira and Bannik descend upon the Citadel"},
        {0x19, "cannsave.ds9", "Harbinger menaces Deep Space Nine"},
        {0x1B, "defisave.ds9", "Nemesis assails Deep Space Nine"},
    };
    if (!_autosave)
        return;
    for (const auto &a : kAutosaves) {
        if (a.type != type)
            continue;
        draw::create(_menuGameScreen, kScreenWidth, kScreenHeight);
        draw::blit(_menuGameScreen, _screen, 0, 0);
        _menuGamePalette = _palette;
        saveGame(a.file, a.title, _actionSeqPc);
    }
}

} // namespace reluva::stharbinger
