// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/game/arcade.h"

#include "audio/stream.h"
#include "common/fs.h"
#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/formats/audio.h"
#include "engines/stharbinger/formats/resources.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace reluva::stharbinger {

namespace {

struct SequenceInfo {
    int type;
    const char *name;
    int x;
    int y;
    int w;
    int h;
    int startFrame;
    int endFrame;
    bool torpedoes;
    bool shieldHud;
    bool overlay;
    const char *deathVideo;
};

// Per type (DEEP.EXE tables 0x24E8F, 0x24E2F, 0x24E4F and the code of 0x25351).
const SequenceInfo kSequences[] = {
    {0x02, "LAND", 120, 12, 400, 300, 200, 1500, true, true, true, "DEATHMIX"},
    {0x03, "SPAC", 120, 12, 400, 300, 200, 1501, true, true, true, "DEATHMIX"},
    {0x04, "HOLO", 120, 12, 400, 300, 200, 2200, true, true, true, "DEATHMIX"},
    {0x05, "ADR2", 80, 62, 480, 250, 100, 1350, false, false, false, "ADR2DEAD"},
    {0x06, "PLAN", 80, 62, 480, 250, 100, 1225, true, true, true, "DEATH480"},
    {0x19, "CANN", 80, 62, 480, 250, 100, 919, true, true, true, "DEATH480"},
    {0x1A, "CITA", 120, 12, 400, 300, 100, 361, true, true, true, "DEATHMIX"},
    {0x1B, "DEFI", 80, 106, 480, 250, 100, 579, true, false, true, "DEFDEAD"},
    {0x1E, "DEF2", 80, 106, 480, 250, 100, 596, true, false, true, "DEFDEAD"},
    {0x1F, "CAN2", 80, 62, 480, 250, 100, 481, true, true, true, "DEATH480"},
};

const SequenceInfo *findSequence(int type) {
    for (const SequenceInfo &s : kSequences)
        if (s.type == type)
            return &s;
    return nullptr;
}

const int kMaxSprites = 150;
const int kWinCountdown = 12;
const int kDoorHealth = 20;
const uint8_t kPhaserColor = 18;
// Torpedo flight: how far along (sixteenths) and the gap of each pair, per stage.
const int kTorpedoStep[4] = {0, 6, 11, 15};
const int kTorpedoHalfGap[4] = {29, 17, 9, 5};
// Screen shake per intensity (5 steps, read from the end; 0x54E9C / 0x54ED8).
const int kShakeX[6][5] = {{0, 2, -2, 2, -2}, {0, 0, 0, 0, 0},   {0, 4, -4, 4, -4},
                           {0, 0, 0, 0, 0},   {0, 6, -6, 6, -6}, {0, 0, 0, 0, 0}};
const int kShakeY[6][5] = {{0, 0, 0, 0, 0},   {0, 2, -2, 2, -2}, {0, 0, 0, 0, 0},
                           {0, -4, 4, 4, -4}, {0, 0, 0, 0, 0},   {0, 6, -6, 6, -6}};
const int kDoorGridX[9] = {1, 2, 3, 1, 2, 3, 1, 2, 3};
const int kDoorGridY[9] = {1, 1, 1, 2, 2, 2, 3, 3, 3};

// Decodes raw game audio (format detected as for scene sounds) to 16-bit samples.
std::vector<int16_t> decodeRaw(std::vector<uint8_t> data) {
    std::vector<int16_t> out;
    auto stream = makeRawStream(std::move(data));
    int16_t buf[4096];
    int n;
    while ((n = stream->read(buf, 4096)) > 0)
        out.insert(out.end(), buf, buf + n);
    return out;
}

} // namespace

// The music: segments before "tag" loop; at the tag frame the tag plays once.
class Arcade::MusicStream : public audio::Stream {
public:
    MusicStream(std::vector<std::vector<int16_t>> segments, std::vector<int> order, int tag)
        : _segments(std::move(segments)), _order(std::move(order)), _tag(tag) {}

    void playTag() {
        std::lock_guard<std::mutex> lock(_mutex);
        _index = _tag;
        _pos = 0;
        _tagPlaying = true;
        _ended = _tag < 0;
    }
    void stop() {
        std::lock_guard<std::mutex> lock(_mutex);
        _ended = true;
    }

    int read(int16_t *buffer, int numSamples) override {
        std::lock_guard<std::mutex> lock(_mutex);
        int n = 0;
        while (n < numSamples && !_ended) {
            if (_index < 0 || _index >= int(_order.size())) {
                _ended = true;
                break;
            }
            const std::vector<int16_t> &seg = _segments[_order[_index]];
            if (_pos >= seg.size()) {
                _pos = 0;
                if (_tagPlaying) {
                    _ended = true;
                    break;
                }
                // The next segment; wrap at the end or at the tag.
                _index++;
                if (_index >= int(_order.size()) || _index == _tag)
                    _index = 0;
                if (_segments[_order[_index]].empty()) {
                    _ended = true;
                    break;
                }
                continue;
            }
            buffer[n++] = seg[_pos++];
        }
        return n;
    }
    bool stereo() const override { return false; }
    int rate() const override { return kSampleRate; }
    bool ended() const override { return _ended; }

private:
    std::mutex _mutex;
    std::vector<std::vector<int16_t>> _segments;
    std::vector<int> _order;
    int _tag;
    int _index = 0;
    size_t _pos = 0;
    bool _tagPlaying = false;
    bool _ended = false;
};

Arcade::Arcade(Resources &resources, audio::Mixer *mixer, int type, int difficulty)
    : _resources(resources), _mixer(mixer), _type(type), _difficulty(difficulty) {
    const SequenceInfo *info = findSequence(type);
    if (info) {
        _name = info->name;
        _rect = Rect(info->x, info->y, info->x + info->w, info->y + info->h);
        _startFrame = info->startFrame;
        _endFrame = info->endFrame;
        _torpedoes = info->torpedoes;
        _shieldHud = info->shieldHud;
        _overlay = info->overlay;
    }
}

Arcade::~Arcade() {
    if (_mixer) {
        _mixer->stop(_musicHandle);
        _mixer->stop(_videoAudioHandle);
    }
}

const char *Arcade::getDeathVideo() const {
    const SequenceInfo *info = findSequence(_type);
    return info ? info->deathVideo : "DEATHMIX";
}

int Arcade::random(int n) {
    _randSeed = _randSeed * 1103515245u + 12345u;
    int r = int((_randSeed >> 16) & 0x7FFF);
    return n > 0 ? r % n : 0;
}

// ---- Loading ------------------------------------------------------------------------------

bool Arcade::loadSound(const std::string &name, std::vector<uint8_t> &data) const {
    auto s = _resources.openFile("SOUND/" + text::toUpper(name) + ".RAW");
    if (!s)
        return false;
    data = s->readAll();
    return !data.empty();
}

void Arcade::playSound(const std::vector<uint8_t> &data, audio::SoundType type) {
    if (_mixer && !data.empty())
        _mixer->play(makeRawStream(data), type);
}

void Arcade::loadData() {
    std::vector<std::string> files;
    for (const std::string &name : _resources.getSearchSet().list())
        files.push_back(fs::normalizeDosPath(name));
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    auto endsWith = [](const std::string &s, const std::string &suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    auto inDir = [&](const std::string &f, const std::string &dir) {
        return f.compare(0, dir.size(), dir) == 0 && f.find('/', dir.size()) == std::string::npos;
    };

    // Explosions, sorted by name: EXPLLG01..05, then EXPLSM01..05.
    for (const std::string &f : files) {
        if (inDir(f, "EXPLODE/") && f.compare(8, 4, "EXPL") == 0 && endsWith(f, ".RLE")) {
            RleAnimation a;
            uint8_t header[8];
            if (auto s = _resources.openFile(f))
                if (a.loadWithHeader(*s, header))
                    _explosions.push_back(std::move(a));
        }
    }

    static const char *const kTorpedoNames[6] = {"T29L", "T17L", "T09L", "T29R", "T17R", "T09R"};
    uint8_t header[8];
    for (int i = 0; i < 6; i++)
        if (auto s = _resources.openFile(std::string("TORPEDOS/") + kTorpedoNames[i] + ".RLE"))
            _torpedoAnims[i].loadWithHeader(*s, header);
    if (auto s = _resources.openFile("TORPEDOS/T5ALL.RLE"))
        _torpedoEnd.loadWithHeader(*s, header);

    // Targets: every RLE file of the sequence's sprite folder, in name order.
    std::string dir = "SPRITES/" + _name + "/";
    for (const std::string &f : files) {
        if (!inDir(f, dir) || !endsWith(f, ".RLE") || int(_targets.size()) >= kMaxSprites)
            continue;
        Target t;
        auto s = _resources.openFile(f);
        if (!s || !t.rle.loadWithHeader(*s, header))
            continue;
        // The 8 header bytes: u16 start frame, hit points, damage per shot, fire
        // period, gun (FF never fires, 2 alternates muzzles), u16 group id.
        t.start = header[0] | (header[1] << 8);
        t.hp = header[2];
        t.damage = header[3];
        t.period = header[4];
        t.gun = header[5];
        t.id = header[6] | (header[7] << 8);
        t.draw = &t.rle;
        _targets.push_back(std::move(t));
    }
    for (Target &t : _targets)
        t.draw = &t.rle; // the vector has moved

    if (auto s = _resources.openFile("PANEL/RUNSH.RLE"))
        if (_shieldHud)
            _shield.loadWithHeader(*s, header);
    if (_overlay)
        if (auto s = _resources.openFile("ART/" + _name + "RUN.RLE"))
            _windshield.loadWithHeader(*s, header);
    if (_type == 0x06) {
        if (auto s = _resources.openFile("SPRITES/PDOOR.RLE")) {
            _door.loadWithHeader(*s, header);
            _doorStart = header[0] | (header[1] << 8);
        }
    }

    for (int i = 0; i < 5; i++)
        loadSound(text::format("BANG%02d", i + 1), _bangs[i]);
    if (!loadSound("QUANT", _quant))
        loadSound("QUANTUM", _quant);
    loadSound("PHASOR", _phasor);
    loadSound("SHAKE", _shake);
    loadMusic();
    loadVoice();
    log::debug(1, "Arcade %s: %u targets, %u explosions, %u voice cues", _name.c_str(), unsigned(_targets.size()),
               unsigned(_explosions.size()), unsigned(_voice.size()));
}

void Arcade::loadMusic() {
    // SOUND/<name>.MUS: a number, then segment suffixes (NAME + suffix + .RAW);
    // the "tag" segment ends the music.
    auto s = _resources.openFile("SOUND/" + _name + ".MUS");
    if (!s)
        return;
    std::vector<uint8_t> data = s->readAll();
    std::vector<std::string> lines = text::split(std::string(data.begin(), data.end()), '\n');
    if (lines.empty())
        return;
    _musicValue = std::atoi(lines[0].c_str());
    std::map<std::string, int> loaded;
    for (size_t i = 1; i < lines.size() && _musicOrder.size() < 20; i++) {
        std::string suffix;
        for (char c : lines[i]) {
            if (c < '0')
                break;
            suffix += c;
        }
        if (suffix.empty())
            break;
        suffix = text::toUpper(suffix);
        auto it = loaded.find(suffix);
        if (it == loaded.end()) {
            std::vector<uint8_t> raw;
            loadSound(_name + suffix, raw);
            _musicSegments.push_back(decodeRaw(std::move(raw)));
            it = loaded.emplace(suffix, int(_musicSegments.size()) - 1).first;
        }
        if (suffix == "TAG")
            _musicTag = int(_musicOrder.size());
        _musicOrder.push_back(it->second);
    }
}

void Arcade::loadVoice() {
    // VOICE/<name>.VCE: pairs of lines: a sound name, then a video frame or
    // d<n> (plays when damage first reaches 95 - 5 * (n / 5)).
    auto s = _resources.openFile("VOICE/" + _name + ".VCE");
    if (!s)
        return;
    std::vector<uint8_t> data = s->readAll();
    std::vector<std::string> lines;
    for (std::string l : text::split(std::string(data.begin(), data.end()), '\n')) {
        l = text::trim(l);
        if (!l.empty())
            lines.push_back(l);
    }
    for (size_t i = 0; i + 1 < lines.size() && _voice.size() < 25; i += 2) {
        VoiceCue cue;
        std::string name;
        for (char c : lines[i]) {
            if (c < '0')
                break;
            name += c;
        }
        loadSound(name, cue.sound);
        const std::string &trigger = lines[i + 1];
        if (trigger[0] == 'd' || trigger[0] == 'D')
            cue.damageBand = std::atoi(trigger.c_str() + 1) / 5;
        else
            cue.frame = std::atoi(trigger.c_str());
        _voice.push_back(std::move(cue));
    }
}

bool Arcade::start(const Surface &screen, int initialDamage, const std::map<int, ArcadeGroupState> &groups) {
    auto stream = _resources.openFile("MOVIES/" + _name + ".SMK");
    _video = std::make_unique<smacker::Decoder>();
    if (!stream || !_video->load(std::move(stream))) {
        log::warning("Action sequence video MOVIES/%s.SMK not found", _name.c_str());
        _result = kArcadeMissing;
        _done = true;
        return false;
    }
    // The cockpit picture stays around the video.
    draw::create(_work, 640, 480);
    draw::blit(_work, screen, 0, 0);
    draw::create(_compose, 640, 480);
    draw::blit(_compose, screen, 0, 0);
    loadData();
    _damage = initialDamage;
    // The Defiant's second sequence continues the first one's groups.
    if (_type == 0x1E) {
        for (Target &t : _targets) {
            auto it = groups.find(t.id);
            if (t.id >= 100 && it != groups.end()) {
                t.state = it->second.state;
                t.hits = it->second.hits;
            }
        }
    }
    if (_mixer && _video->hasAudio() && _type == 0x1A) {
        auto audio = std::make_shared<audio::QueuingStream>(_video->audioRate(), _video->audioStereo());
        _videoAudio = audio;
        _videoAudioHandle = _mixer->play(audio, audio::SoundType::kMusic);
    }
    if (_mixer && !_musicOrder.empty()) {
        auto music = std::make_shared<MusicStream>(_musicSegments, _musicOrder, _musicTag);
        _music = music;
        _musicHandle = _mixer->play(music, audio::SoundType::kMusic, mixerVolume(kVolumeArcadeMusic));
    }
    return true;
}

void Arcade::saveGroups(std::map<int, ArcadeGroupState> &out) const {
    for (const Target &t : _targets) {
        if (t.id < 100)
            continue;
        ArcadeGroupState &g = out[t.id];
        if (t.groupGone || t.state == 2)
            g.state = 2;
        g.hits = std::max(g.hits, t.hits);
    }
}

bool Arcade::takePaletteChanged() {
    bool c = _paletteChanged;
    _paletteChanged = false;
    return c;
}

// ---- The frame loop -------------------------------------------------------------------------

bool Arcade::update(uint32_t millis, Point aim, bool leftPressed, bool rightPressed) {
    _aim = aim;
    uint64_t elapsed = uint64_t(millis) * 1000;
    bool stepped = false;
    while (!_done && uint64_t(_frame + 1) * _video->frameMicros() <= elapsed) {
        stepFrame(leftPressed, rightPressed);
        leftPressed = rightPressed = false;
        stepped = true;
    }
    return stepped;
}

void Arcade::stepFrame(bool leftPressed, bool rightPressed) {
    if (!_video->decodeNextFrame()) {
        finishFrame();
        _done = true;
        return;
    }
    _frame++;
    if (_video->paletteChanged()) {
        std::memcpy(_palette, _video->palette(), 768);
        _paletteChanged = true;
    }
    if (_videoAudio)
        _videoAudio->queue(_video->takeAudio());
    else
        _video->takeAudio();
    draw::blit(_work, _video->surface(), _rect.left, _rect.top);

    // The video into the picture, shifted by the shake (the uncovered strip keeps
    // the last frame).
    for (int y = _rect.top; y < _rect.bottom; y++) {
        int dy = y + _shakeY;
        if (dy < _rect.top || dy >= _rect.bottom)
            continue;
        for (int x = _rect.left; x < _rect.right; x++) {
            int dx = x + _shakeX;
            if (dx >= _rect.left && dx < _rect.right)
                *draw::at(_compose, dx, dy) = *draw::at(_work, x, y);
        }
    }

    // Shields recharge.
    if (_damage && _frame % (_difficulty == 0 ? 10 : 13) == 0)
        _damage--;

    _torpedoFiredThisFrame = false;
    if (_type == 0x06)
        updateDoor(leftPressed);
    updateTargets();
    if (rightPressed && _torpedoes && _rect.contains(_aim.x, _aim.y))
        fireTorpedo();
    updateTorpedoes();
    if (leftPressed && !_torpedoFiredThisFrame && _rect.contains(_aim.x, _aim.y))
        firePhaser();
    drawHud();
    updateShake();
    finishFrame();
}

void Arcade::finishFrame() {
    // After each frame: the music's tag, voice cues, death and victory.
    if (_frame == _startFrame) {
        uint32_t elapsedMs = uint32_t(uint64_t(_startFrame) * _video->frameMicros() / 1000);
        _tagFrame = _endFrame - int(int64_t(elapsedMs) * _musicValue / 10000) + _endFrame / 40;
    }
    if (_frame == _tagFrame && _music)
        _music->playTag();

    for (VoiceCue &cue : _voice) {
        if (cue.frame >= 0 && cue.frame == _frame)
            playSound(cue.sound, audio::SoundType::kSpeech);
        if (cue.damageBand >= 0 && cue.damageBand < 20 && _voiceFlags[cue.damageBand] == 1) {
            _voiceFlags[cue.damageBand] = 2;
            playSound(cue.sound, audio::SoundType::kSpeech);
        }
    }

    if (_damage >= 100) {
        _result = kArcadeKilled;
    } else if (_damage / 5 != _prevDamage / 5 && _damage > _prevDamage) {
        int band = 19 - _damage / 5;
        if (band >= 0 && band < 20 && _voiceFlags[band] == 0)
            _voiceFlags[band] = 1;
    }
    _prevDamage = _damage;
    if (_winCountdown && --_winCountdown == 1)
        _result = kArcadeWon;

    if (_result != 0 || _frame + 1 >= _video->frameCount() || _done) {
        if (_type == 0x06 && _result == 0)
            _result = _doorDestroyed ? kArcadeWon : kArcadeDoorIntact;
        _done = true;
        if (_music)
            _music->stop();
        if (_videoAudio)
            _videoAudio->finish();
        log::debug(1, "Arcade %s ended at frame %d: result %u, damage %d", _name.c_str(), _frame, _result, _damage);
    }
}

// ---- Targets --------------------------------------------------------------------------------

void Arcade::updateTargets() {
    _targetUnderSight = -1;
    for (int i = 0; i < int(_targets.size()); i++) {
        Target &t = _targets[i];
        if (t.state == 2 || t.start > _frame)
            continue;
        const RleAnimation::Frame &path = t.rle.getFrame(t.pathFrame);
        const RleAnimation::Frame &draw = t.draw->getFrame(t.drawFrame);
        int x = path.x + _rect.left + _shakeX;
        int y = path.y + _rect.top + _shakeY;
        if (t.state == 1) {
            // An explosion is centred on the target's path.
            x -= (draw.w - path.w) / 2;
            y -= (draw.h - path.h) / 2;
        }
        if (t.state == 0 && _aim.x >= x && _aim.x < x + draw.w && _aim.y >= y && _aim.y < y + draw.h)
            _targetUnderSight = i;
        t.draw->drawFrameAt(t.drawFrame, _compose, x, y, &_rect);
        enemyFire(t, x, y);

        // One sprite frame per video frame; the target is gone when either list ends.
        t.drawFrame++;
        t.pathFrame++;
        if (t.drawFrame >= t.draw->frameCount() || t.pathFrame >= t.rle.frameCount())
            t.state = 2;
    }
}

void Arcade::enemyFire(Target &t, int x, int y) {
    int delay = t.id >= 100 ? t.id - 100 : t.id;
    int period = std::max(1, t.period + 1 - _difficulty);
    if (t.state != 0 || _doorDestroyed || _winCountdown || t.start + delay >= _frame || t.gun == 0xFF ||
        (_frame - t.start) % period != 0)
        return;
    const RleAnimation::Frame &f = t.draw->getFrame(t.drawFrame);
    int mx = f.muzzleX[t.muzzle & 1];
    int my = f.muzzleY[t.muzzle & 1];
    if (mx == 0 && my == 0)
        return;
    int sx = x + mx;
    int sy = y + my;
    if (!_rect.contains(sx, sy))
        return;
    if (_shakeCount == 0)
        playSound(_shake);
    // Enemy shots hit at once.
    _damage += t.damage;
    int cx = std::clamp(sx, _rect.left + _rect.width() / 8, _rect.right - _rect.width() / 8);
    drawEnemyBeam(sx, sy, cx, _rect.bottom - 6, t.damage > 10 ? 7 : 1);
    _shakeCount = 4;
    _shakeIntensity = (std::min(t.damage, 3) - 1) * 2;
    if (t.gun == 2)
        t.muzzle ^= 1;
}

void Arcade::hitTarget(int index, int amount, bool torpedo) {
    Target &t = _targets[index];
    if (torpedo && t.id < 200) {
        destroyTarget(index); // a torpedo destroys anything but a boss
        return;
    }
    t.hits += amount;
    if (t.hits >= t.hp) {
        destroyTarget(index);
        return;
    }
    // Groups share their damage.
    if (t.id >= 100)
        for (int i = 0; i < int(_targets.size()); i++)
            if (i != index && _targets[i].id == t.id)
                _targets[i].hits += amount;
}

void Arcade::destroyTarget(int index) {
    Target &t = _targets[index];
    t.state = 1;
    int half = int(_explosions.size()) / 2;
    if (half > 0) {
        // A small explosion if it covers the target by its fifth frame, else a large one.
        int r = half + random(half);
        const RleAnimation::Frame &f = t.draw->getFrame(t.drawFrame < t.draw->frameCount() ? t.drawFrame : 0);
        const RleAnimation &small = _explosions[r];
        int smallArea = small.frameCount() > 4 ? small.getFrame(4).w * small.getFrame(4).h : 0;
        t.draw = smallArea >= f.w * f.h ? &small : &_explosions[r - half];
        t.drawFrame = 0;
        playSound(_bangs[std::min(r - half, 4)]);
    }
    if (t.id > 100) {
        // The rest of the group vanishes.
        for (int i = 0; i < int(_targets.size()); i++) {
            if (i != index && _targets[i].id == t.id) {
                _targets[i].state = 2;
                _targets[i].groupGone = true;
            }
        }
        t.groupGone = true;
    }
    if (t.id >= 200)
        _winCountdown = kWinCountdown;
}

// ---- The player's weapons ---------------------------------------------------------------------

void Arcade::firePhaser() {
    int launchY = _rect.top + _rect.height() * 3 / 4;
    drawPhaserBeam(_rect.left, launchY, _aim.x, _aim.y, kPhaserColor);
    drawPhaserBeam(_rect.right - 1, launchY, _aim.x, _aim.y, kPhaserColor);
    playSound(_phasor);
    if (_targetUnderSight >= 0 && _targets[_targetUnderSight].state == 0)
        hitTarget(_targetUnderSight, 1, false);
}

int Arcade::torpedoDirection(int dx, int dy) {
    if (std::abs(dx) < 58 && std::abs(dy) < 58)
        return 9;
    long s = dx == 0 ? 0xFFFFFF : -(long(dy) * 128) / std::abs(dx);
    static const long kCuts[8] = {-54, -17, 17, 54, 101, 175, 350, 1000};
    for (int i = 0; i < 8; i++)
        if (s < kCuts[i])
            return i;
    return 8;
}

void Arcade::fireTorpedo() {
    Torpedo *slot = nullptr;
    for (Torpedo &t : _torps)
        if (t.state < 0) {
            slot = &t;
            break;
        }
    if (!slot)
        return;
    _torpedoFiredThisFrame = true;
    playSound(_quant);
    slot->state = 0;
    slot->aimX = _aim.x;
    slot->aimY = _aim.y;
    int launchY = _rect.top + _rect.height() * 3 / 4;
    int dy = _aim.y - launchY;
    for (int side = 0; side < 2; side++) {
        int dx = side == 0 ? _aim.x - _rect.left : _aim.x - (_rect.right - 1);
        int dir = torpedoDirection(dx, dy);
        for (int k = 0; k < 4; k++) {
            const RleAnimation *anim = k < 3 ? &_torpedoAnims[side * 3 + k] : &_torpedoEnd;
            int frame = k < 3 ? dir : 0;
            if (frame >= anim->frameCount())
                frame = 0;
            int ox = (dx * kTorpedoStep[k] + 8) / 16;
            int oy = (dy * kTorpedoStep[k] + 8) / 16;
            int x = side == 0 ? ox + _rect.left : ox + _rect.right - (anim->frameCount() ? anim->getFrame(0).w : 0);
            for (int pair = 0; pair < 2; pair++) {
                Torpedo::Sprite &s = slot->sprites[k][side * 2 + pair];
                s.anim = anim;
                s.frame = frame;
                s.x = x;
                s.y = oy + (pair == 0 ? -kTorpedoHalfGap[k] : kTorpedoHalfGap[k]) + launchY;
            }
        }
    }
}

void Arcade::updateTorpedoes() {
    for (Torpedo &t : _torps) {
        if (t.state < 0)
            continue;
        if (t.state < 4) {
            for (const Torpedo::Sprite &s : t.sprites[t.state])
                if (s.anim && s.anim->frameCount())
                    s.anim->drawFrameAt(s.frame, _compose, s.x + _shakeX, s.y + _shakeY, &_rect);
            t.state++;
            continue;
        }
        // Hits on the fifth frame, where the player aimed.
        torpedoImpact(t.aimX, t.aimY);
        t.state = -1;
    }
}

void Arcade::torpedoImpact(int x, int y) {
    if (_type == 0x06 && !_doorDestroyed && _door.frameCount()) {
        const RleAnimation::Frame &d = _door.getFrame(_doorFrame);
        Rect r(d.x + _rect.left, d.y + _rect.top, d.x + _rect.left + d.w, d.y + _rect.top + d.h);
        if (r.contains(x, y))
            _doorDamage += 6;
    }
    for (int i = 0; i < int(_targets.size()); i++) {
        Target &t = _targets[i];
        if (t.state != 0 || t.start >= _frame)
            continue;
        const RleAnimation::Frame &f = t.draw->getFrame(std::min(t.drawFrame, t.draw->frameCount() - 1));
        int tx = f.x + _rect.left;
        int ty = f.y + _rect.top;
        if (x >= tx && x <= tx + f.w && y >= ty && y <= ty + f.h) {
            hitTarget(i, 2, true);
            return;
        }
    }
}

// ---- The plan door (type 6) -------------------------------------------------------------------

void Arcade::updateDoor(bool leftPressed) {
    if (!_door.frameCount())
        return;
    if (_frame > _doorStart && _doorFrame + 1 < _door.frameCount())
        _doorFrame++;
    const RleAnimation::Frame &d = _door.getFrame(_doorFrame);
    Rect r(d.x + _rect.left, d.y + _rect.top, d.x + _rect.left + d.w, d.y + _rect.top + d.h);
    if (!_doorDestroyed) {
        if (leftPressed && r.contains(_aim.x, _aim.y))
            _doorDamage += 2;
        if (_doorDamage >= kDoorHealth) {
            // Nine large explosions on a 3x3 grid, a few frames apart.
            _doorDestroyed = true;
            _doorOverlayDelay = 2;
            int half = int(_explosions.size()) / 2;
            for (int i = 0; i < 9 && half > 0; i++) {
                DoorExplosion e;
                int index = random(half);
                e.anim = &_explosions[index];
                e.bang = std::min(index / 2, 4);
                e.delay = random(5) + 1;
                e.x = r.left + kDoorGridX[i] * r.width() / 4;
                e.y = r.top + kDoorGridY[i] * r.height() / 4;
                _doorExplosions.push_back(e);
            }
        }
    } else {
        // The blown-open door follows the camera.
        if (_doorOverlayDelay > 0)
            _doorOverlayDelay--;
        else
            _door.drawFrameAt(_doorFrame, _compose, r.left, r.top, &_rect);
    }
    for (DoorExplosion &e : _doorExplosions) {
        if (e.delay == 1)
            playSound(_bangs[e.bang]);
        if (e.delay > 0) {
            e.delay--;
            continue;
        }
        if (e.frame < e.anim->frameCount()) {
            const RleAnimation::Frame &f = e.anim->getFrame(e.frame);
            e.anim->drawFrameAt(e.frame, _compose, e.x - f.w / 2, e.y - f.h / 2, &_rect);
            e.frame++;
        }
    }
}

// ---- HUD, shake and beams ----------------------------------------------------------------------

void Arcade::drawHud() {
    // The shield indicator on the cockpit panel (blinking every tenth frame).
    if (_shieldHud && _shield.frameCount()) {
        int n = _shield.frameCount();
        int index = std::max(std::min(_damage * n / 100, n), 1) - 1;
        if (_frame % 10 == 9)
            index = 0;
        _shield.drawFrame(index, _compose);
    }
    // The windshield frame over the video.
    for (int i = 0; i < _windshield.frameCount(); i++) {
        const RleAnimation::Frame &f = _windshield.getFrame(i);
        _windshield.drawFrameAt(i, _compose, f.x, f.y, &_rect);
    }
}

void Arcade::updateShake() {
    if (_shakeCount > 0) {
        int row = std::clamp(_shakeIntensity, 0, 5);
        _shakeX = kShakeX[row][_shakeCount];
        _shakeY = kShakeY[row][_shakeCount];
        _shakeCount--;
        // Colour 12 flashes yellow and orange (0x27760).
        static const uint8_t kFlash[2][3] = {{58, 33, 5}, {63, 63, 0}};
        const uint8_t *c = kFlash[_shakeCount & 1];
        for (int k = 0; k < 3; k++)
            _palette[12 * 3 + k] = uint8_t((c[k] << 2) | (c[k] >> 4));
        _paletteChanged = true;
    } else {
        _shakeX = _shakeY = 0;
    }
}

void Arcade::drawPhaserBeam(int x0, int y0, int x1, int y1, uint8_t color) {
    stharbinger::drawPhaserBeam(_compose, x0, y0, x1, y1, color);
}

void Arcade::drawEnemyBeam(int x0, int y0, int x1, int y1, uint8_t color) {
    stharbinger::drawEnemyBeam(_compose, x0, y0, x1, y1, color, 20, _rect.bottom, [this](int n) { return random(n); });
}

} // namespace reluva::stharbinger
