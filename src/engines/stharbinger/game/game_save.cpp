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
const char* const kSettingsFile = "dlas.ds9";

// Reads the little-endian fields of a save, failing softly at the end.
struct SaveReader {
    const std::vector<uint8_t>& d;
    size_t pos = 0;
    bool bad = false;

    explicit SaveReader(const std::vector<uint8_t>& data) : d(data) {}
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
    const uint8_t* bytes(size_t n) {
        if (!need(n))
            return nullptr;
        const uint8_t* p = d.data() + pos;
        pos += n;
        return p;
    }
};

int findName(const std::vector<std::string>& names, const std::string& name) {
    for (size_t i = 0; i < names.size(); i++)
        if (text::equalsIgnoreCase(names[i], name))
            return int(i);
    return -1;
}

} // namespace

// As in the game's folder: settings in dlas.ds9, saved games in savegame/
// (original saves can be copied there), in the game's own folder of
// Reluva's (GameTarget::savePath).
static std::string settingsDirectory(Platform* system) { return system->getSavePath(); }

std::string Game::getSaveDirectory() const {
    std::string base = settingsDirectory(system_);
    if (base.empty())
        return base;
    const std::filesystem::path dir = std::filesystem::path(base) / "savegame";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

uint32_t Game::getResumePc() const {
    // Idle on a RETURN, or the directive the script stopped at.
    return thread_.active ? thread_.pc : idlePc_;
}

// ---- Settings (dlas.ds9: difficulty, autosave, last save shown) --------------

void Game::loadSettings() {
    std::vector<uint8_t> d;
    std::string dir = settingsDirectory(system_);
    if (dir.empty() || !readFile((std::filesystem::path(dir) / kSettingsFile), d) || d.size() < 12)
        return;
    SaveReader r(d);
    uint32_t difficulty = r.u32();
    arcadeDifficulty_ = difficulty <= 2 ? int(difficulty) : 1;
    autosave_ = r.u32() != 0;
    lastSave_ = int(r.u32());
}

void Game::saveSettings() {
    std::string dir = settingsDirectory(system_);
    if (dir.empty())
        return;
    MemoryWriteStream w;
    w.writeUint32LE(uint32_t(arcadeDifficulty_));
    w.writeUint32LE(autosave_ ? 1 : 0);
    w.writeUint32LE(uint32_t(lastSave_));
    writeFile((std::filesystem::path(dir) / kSettingsFile), w.data());
}

// ---- Saving --------------------------------------------------------------------

bool Game::saveGame(const std::string& fileName, const std::string& description, uint32_t resumePc) {
    std::string dir = getSaveDirectory();
    if (dir.empty() || !scene_)
        return false;
    const auto& linkScenes = resources_.getLinkScenes();
    const auto& linkVars = resources_.getLinkVariables();
    const auto& linkDialogues = resources_.getLinkDialogues();
    int linkScene = findName(linkScenes, scene_->name);
    if (linkScene < 0) {
        log::warning("Cannot save: scene %s is not in DS9.LCC", scene_->name.c_str());
        return false;
    }
    auto labelTarget = [&](int label) -> uint32_t {
        auto it = labels_.find(uint32_t(label));
        return label < 0 || it == labels_.end() ? kNone32 : it->second;
    };

    MemoryWriteStream w;
    w.writeUint16LE(uint16_t(disc_));
    w.writeUint32LE(tricorderIcon_ ? 1 : 0);
    w.writeUint32LE(uint32_t(mapLocation_));
    w.writeUint32LE(tickPeriod_);
    w.writeUint32LE(tickPhase_);
    w.write("ds9", 4);
    w.write(description.c_str(), uint32_t(description.size() + 1));
    // The picture without cursor or menu, and the palette (6-bit).
    if (menuGameScreen_.w != 640 || menuGameScreen_.h != 480)
        draw::create(menuGameScreen_, 640, 480);
    w.write(menuGameScreen_.pixels.data(), 640 * 480);
    for (int i = 0; i < 768; i++)
        w.writeByte(uint8_t(menuGamePalette_.data()[i] >> 2));
    w.writeUint32LE(uint32_t(paletteBackground_));
    w.writeUint16LE(uint16_t(linkScene));
    w.writeUint32LE(uint32_t(thread_.callStack.size()));
    for (uint32_t pc : thread_.callStack)
        w.writeUint32LE(pc);

    // Dialogue slots, by DS9.LDM index.
    std::vector<int> slotDialogue(kSlots, -1);
    for (const auto& d : dialogues_) {
        int slot = findName(linkDialogues, getDialogueName(d.first));
        if (slot >= 0 && slot < kSlots)
            slotDialogue[slot] = d.first;
    }
    for (int slot = 0; slot < kSlots; slot++) {
        if (slotDialogue[slot] < 0) {
            w.writeByte(0xFF);
            continue;
        }
        const DialogueState& st = dialogues_[slotDialogue[slot]];
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
        for (const auto* flags : {&st.npcFlags, &st.choiceFlags}) {
            for (int g = 0; g < kMaxGroups; g++) {
                if (g >= int(flags->size()) || (*flags)[g].empty()) {
                    w.writeByte(0xFF);
                    continue;
                }
                w.writeByte(uint8_t((*flags)[g].size()));
                for (const auto& f : (*flags)[g]) {
                    w.writeByte(f.on ? 1 : 0);
                    w.writeByte(f.said ? 1 : 0);
                }
            }
        }
        for (const auto* table : {st.doneX, st.emptyXGoto, st.emptyXReply}) {
            for (int g = 0; g < kMaxGroups; g++) {
                const DialogueState::Redirect& r = table[g];
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
            for (const auto& h : st.hooks[g]) {
                w.writeByte(h.state == 2 ? 0xFF : uint8_t(h.state));
                w.writeByte(uint8_t(h.group));
            }
        }
    }

    // Global variables by DS9.LVM index.
    std::vector<uint16_t> globals(kNumGlobals, 0);
    for (const auto& v : variables_) {
        int i = findName(linkVars, v.first);
        if (i >= 0 && i < kNumGlobals)
            globals[i] = v.second;
    }
    for (uint16_t g : globals)
        w.writeUint16LE(g);
    w.writeUint16LE(uint16_t(backgroundIndex_ < 0 ? kNone16 : backgroundIndex_));

    // Animations: a 0x34-byte record as in memory, then frame positions.
    for (int i = 0; i < kSlots; i++) {
        if (i >= int(anims_.size()) || !anims_[i].loaded) {
            w.writeByte(0xFF);
            continue;
        }
        const Anim& a = anims_[i];
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
        if (i >= int(hotspots_.size()) || !hotspots_[i].defined) {
            w.writeByte(0xFF);
            continue;
        }
        const Hotspot& h = hotspots_[i];
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
        w.writeUint32LE(i < int(ambientSounds_.size()) ? uint32_t(ambientSounds_[i]) : kNone32);
    for (int i = 0; i < kSlots; i++) {
        if (i >= int(timers_.size()) || !timers_[i].defined) {
            w.writeByte(0xFF);
            continue;
        }
        const Timer& t = timers_[i];
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
    log::debug(1, "Saved %s (%s) at %s, directive %u", fileName.c_str(), description.c_str(), scene_->name.c_str(),
               resumePc);
    return true;
}

// ---- Loading -------------------------------------------------------------------

bool Game::readSaveInfo(const std::string& path, SaveInfo& info) {
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
    const uint8_t* shot = r.bytes(640 * 480);
    const uint8_t* pal = r.bytes(768);
    if (r.bad)
        return false;
    info.screenshot.assign(shot, shot + 640 * 480);
    for (int i = 0; i < 768; i++)
        info.palette[i] = uint8_t(((pal[i] & 0x3F) << 2) | ((pal[i] & 0x3F) >> 4));
    return true;
}

bool Game::loadGame(const std::string& fileName) {
    std::vector<uint8_t> d;
    std::string dir = getSaveDirectory();
    if (dir.empty() || !readFile((std::filesystem::path(dir) / fileName), d)) {
        log::warning("Cannot read saved game %s", fileName.c_str());
        return false;
    }
    const auto& linkScenes = resources_.getLinkScenes();
    const auto& linkVars = resources_.getLinkVariables();
    SaveReader r(d);
    int disc = r.u16();
    bool tricorder = r.u32() != 0;
    int mapLocation = int(r.u32());
    uint32_t tickPeriod = r.u32();
    uint32_t tickPhase = r.u32();
    r.cstr();
    r.cstr();
    r.bytes(640 * 480);
    const uint8_t* pal = r.bytes(768);
    int paletteBackground = int(r.u32());
    int linkScene = r.u16();
    if (r.bad || linkScene >= int(linkScenes.size())) {
        log::warning("%s is not a saved game of this game", fileName.c_str());
        return false;
    }
    int sceneIndex = vm_->findScene(linkScenes[linkScene]);
    if (sceneIndex < 0) {
        log::warning("%s: unknown scene %s", fileName.c_str(), linkScenes[linkScene].c_str());
        return false;
    }

    // Leave whatever runs now and set the scene up as on entry, without its script.
    if (conv_.active) {
        conv_.ops.clear();
        conv_.result = -1;
        finishConversation();
    }
    video_ = VideoPlayback();
    arcade_.reset();
    if (scene_)
        leaveScene();
    if (audio::Mixer* mixer = system_->getMixer())
        mixer->stopAll();
    sceneIndex_ = sceneIndex;
    scene_ = &scenes_[sceneIndex];
    buildLabels();
    hotspots_.assign(scene_->lists[kListHotspots].size(), Hotspot());
    timers_.assign(scene_->lists[kListTimers].size(), Timer());
    setupAnims();
    thread_ = Thread();
    pendingScene_ = -1;

    uint32_t depth = r.u32();
    for (uint32_t i = 0; i < depth && i < 24 && !r.bad; i++)
        thread_.callStack.push_back(r.u32()); // (the original misreads these)

    // Dialogues: reloaded by name, then their state.
    dialogues_.clear();
    for (int slot = 0; slot < kSlots && !r.bad; slot++) {
        if (r.u8() == 0xFF)
            continue;
        std::string name = r.cstr();
        DialogueState scratch;
        DialogueState* st = getDialogueState(findDialogue(name));
        if (!st)
            st = &scratch; // unknown: read and drop
        st->cur = r.u16();
        st->replyMode = r.u8() != 0;
        st->rudeGroup = r.u8();
        st->rude = r.u8() != 0;
        st->backGroup = r.u8();
        st->exit = r.u8() != 0;
        for (uint8_t* table : {st->done, st->emptyGoto, st->emptyReply})
            if (const uint8_t* p = r.bytes(kMaxGroups))
                std::memcpy(table, p, kMaxGroups);
        for (auto* flags : {&st->npcFlags, &st->choiceFlags}) {
            for (int g = 0; g < kMaxGroups; g++) {
                uint8_t n = r.u8();
                if (n == 0xFF)
                    continue;
                for (int i = 0; i < n; i++) {
                    bool on = r.u8() != 0, said = r.u8() != 0;
                    if (g < int(flags->size()) && i < int((*flags)[g].size()))
                        (*flags)[g][i] = {on, said};
                }
            }
        }
        for (DialogueState::Redirect* table : {st->doneX, st->emptyXGoto, st->emptyXReply}) {
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
            uint8_t group = r.u8(), index = r.u8();
            if (group != 0xFF && group < st->current.size())
                st->current[group] = std::min<int>(index, int(st->data.npcLines[group].size()) - 1);
        }
        r.bytes(kMaxGroups);
        for (int g = 0; g < kMaxGroups; g++) {
            st->hooks[g].clear();
            uint8_t n = r.u8();
            for (int i = 0; i < n; i++) {
                uint8_t state = r.u8(), group = r.u8();
                st->hooks[g].push_back({group, state == 0xFF ? 2 : state});
            }
        }
    }

    variables_.clear();
    for (int i = 0; i < kNumGlobals && !r.bad; i++) {
        uint16_t v = r.u16();
        if (v && i < int(linkVars.size()))
            variables_[fs::toLower(linkVars[i])] = v;
    }
    int background = r.u16();

    auto labelAt = [&](uint32_t pc) {
        for (const auto& l : labels_)
            if (l.second == pc)
                return int(l.first);
        return -1;
    };
    for (int i = 0; i < kSlots && !r.bad; i++) {
        uint8_t kind = r.u8();
        if (kind == 0xFF)
            continue;
        const uint8_t* rec = r.bytes(0x34);
        uint32_t frame = r.u32(), drawn = r.u32(), endPc = r.u32();
        r.u32();
        uint8_t nodes = r.u8();
        for (int k = 0; k < nodes; k++)
            r.bytes(8);
        if (!rec || i >= int(anims_.size()))
            continue;
        if (!anims_[i].loaded)
            loadAnim(i, kind == 1);
        Anim& a = anims_[i];
        a.x = int16_t(rec[0] | (rec[1] << 8));
        a.y = int16_t(rec[2] | (rec[3] << 8));
        a.rate = rec[4] | (rec[5] << 8);
        a.countdown = rec[6] | (rec[7] << 8);
        a.flags = rec[8];
        a.priority = rec[9];
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
        if (i < int(hotspots_.size()))
            hotspots_[i] = h;
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
        if (i < int(timers_.size()))
            timers_[i] = t;
    }
    if (r.bad)
        log::warning("%s is truncated; loaded what was there", fileName.c_str());

    // The picture, the palette and the sounds.
    disc_ = disc;
    tricorderIcon_ = tricorder;
    tickPeriod_ = tickPeriod;
    tickPhase_ = tickPhase;
    if (tickPeriod_ != 0xFFFF && tickSound_.empty())
        if (auto s = resources_.openFile("SOUND/TICK.RAW"))
            tickSound_ = s->readAll();
    mapLocation_ = disc == 1 ? 0xFF : mapLocation;
    mapEnabled_ = true;
    dead_ = false;
    if (background != kNone16)
        setBackground(background, false);
    if (pal) {
        for (int i = 0; i < 256; i++)
            palette_.setColor(i, uint8_t((pal[i * 3] & 0x3F) << 2 | (pal[i * 3] & 0x3F) >> 4),
                              uint8_t((pal[i * 3 + 1] & 0x3F) << 2 | (pal[i * 3 + 1] & 0x3F) >> 4),
                              uint8_t((pal[i * 3 + 2] & 0x3F) << 2 | (pal[i * 3 + 2] & 0x3F) >> 4));
        fadeLevel_ = 256;
        applyPalette();
    }
    paletteBackground_ = paletteBackground;
    for (int sound : ambients) {
        if (sound != int(kNone32) && sound < int(scene_->lists[kListSounds].size())) {
            Argument ambient;
            ambient.type = kArgSymbol;
            ambient.value = 0;
            playSound(sound, &ambient);
        }
    }

    // The script carries on where it was: idle on a RETURN, or at the
    // directive (an autosave restarts its action sequence).
    thread_.pc = resume;
    uint8_t cmd = resume < scene_->script.size() ? scene_->script[resume].command : 0;
    if (cmd == kCmdReturn || cmd == kCmdReturnVal) {
        idlePc_ = resume;
        thread_.active = false;
        thread_.pc = resume + 1;
    } else {
        thread_.active = resume < scene_->script.size();
    }
    status_.clear();
    updateCaption();
    log::debug(1, "Loaded %s: scene %s, directive %u", fileName.c_str(), scene_->name.c_str(), resume);
    return true;
}

void Game::startNewGame() {
    // Everything zero, then OPENING (the first scene in DS9.LCC).
    variables_.clear();
    dialogues_.clear();
    arcadeGroups_.clear();
    lastArcadeDamage_ = 0;
    tricorderIcon_ = false;
    tickPeriod_ = 0xFFFF;
    tickPhase_ = 0;
    mapLocation_ = 0xFF;
    dead_ = false;
    paletteBackground_ = 299;
    palette_ = Palette();
    fadeLevel_ = 256;
    applyPalette();
    draw::create(background_, kScreenWidth, kScreenHeight);
    workBufferStale_ = true;
    draw::fill(screen_, 0);
    if (scene_)
        leaveScene();
    int start = vm_->findScene("OPENING");
    pendingScene_ = start < 0 ? 0 : start;
    switchScenes();
}

void Game::autosave(int type) {
    // Before some action sequences, as savegame\<name>save.ds9 (option on by default).
    static const struct {
        int type;
        const char* file;
        const char* title;
    } kAutosaves[] = {
        {0x04, "holosave.ds9", "Holosuite Simulation: Citadel Run"},
        {0x05, "adr2save.ds9", "Drones attack Deep Space Nine"},
        {0x06, "plansave.ds9", "Kira and Bannik descend upon the Citadel"},
        {0x19, "cannsave.ds9", "Harbinger menaces Deep Space Nine"},
        {0x1B, "defisave.ds9", "Nemesis assails Deep Space Nine"},
    };
    if (!autosave_)
        return;
    for (const auto& a : kAutosaves) {
        if (a.type != type)
            continue;
        draw::create(menuGameScreen_, kScreenWidth, kScreenHeight);
        draw::blit(menuGameScreen_, screen_, 0, 0);
        menuGamePalette_ = palette_;
        saveGame(a.file, a.title, actionSeqPc_);
    }
}

} // namespace reluva::stharbinger
