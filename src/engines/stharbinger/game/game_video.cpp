// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Videos (ACTIONSEQ, the intro) and the mouse cursors, after DEEP.EXE
// (video table 0x24E8F, plain playback 0x26104, cursors 0x1925F).

#include "engines/stharbinger/game/game.h"

#include "audio/stream.h"
#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

#include <algorithm>
#include <cstring>

namespace reluva::stharbinger {

namespace {

enum VideoDir { kDirIntro, kDirMovies, kDirTrans };

enum VideoKind {
    kPlain,     // a cut-scene
    kPlainKeep, // the same, over the current picture
    kArcade,    // an action sequence: a video with sprites and a HUD on top
    kRejected
};

struct VideoEntry {
    const char *name;
    VideoDir dir;
    VideoKind kind;
    int x;
    int y;
};

// ACTIONSEQ types 0x00..0x1F (DEEP.EXE 0x24E8F names, 0x24F0F directories).
const VideoEntry kVideos[32] = {
    {"NEWLOGMX", kDirIntro, kPlain, 0, 0},      {"DS9MIX", kDirMovies, kPlain, 120, 90},
    {"LAND", kDirMovies, kArcade, 120, 12},     {"SPAC", kDirMovies, kArcade, 120, 12},
    {"HOLO", kDirMovies, kArcade, 120, 12},     {"ADR2", kDirMovies, kArcade, 80, 62},
    {"PLAN", kDirMovies, kArcade, 80, 62},      {"PREPLAN", kDirTrans, kPlain, 108, 40},
    {"CITADEL", kDirTrans, kPlain, 108, 40},    {"SCYWORM", kDirTrans, kPlain, 108, 40},
    {"SQUAD", kDirTrans, kPlainKeep, 120, 12},  {"CANWORM", kDirTrans, kPlain, 108, 40},
    {"CANBLOW", kDirTrans, kPlain, 108, 40},    {"INSPACE", kDirTrans, kPlain, 108, 40},
    {"CANYON", kDirTrans, kPlain, 108, 40},     {"DEFDEAD", kDirTrans, kRejected, 0, 0},
    {"ATTXENO", kDirTrans, kPlain, 108, 40},    {"HORNET", kDirTrans, kPlain, 108, 40},
    {"SCYSTAT", kDirTrans, kPlainKeep, 80, 60}, {"CITD", kDirTrans, kPlainKeep, 80, 62},
    {"DRONRUN", kDirTrans, kPlain, 108, 40},    {"DRONLOG", kDirTrans, kPlainKeep, 80, 60},
    {"CANWORMB", kDirTrans, kPlain, 108, 40},   {"EPILOGUE", kDirTrans, kPlain, 108, 40},
    {"CAN2DIE", kDirTrans, kPlain, 108, 40},    {"CANN", kDirMovies, kArcade, 80, 62},
    {"CITA", kDirMovies, kArcade, 120, 12},     {"DEFI", kDirMovies, kArcade, 80, 106},
    {"VNM", kDirIntro, kPlain, 96, 72},         {"VCP", kDirIntro, kPlain, 160, 120},
    {"DEF2", kDirMovies, kArcade, 80, 106},     {"CAN2", kDirMovies, kArcade, 80, 62},
};

const char *const kDirNames[] = {"INTRO", "MOVIES", "TRANS"};

const uint16_t kVideoPlayed = 0;
const uint16_t kVideoSkipped = 1;
const uint16_t kVideoMissing = 999;

// The TV static before an arcade death video: a new frame every 255 ms (the
// game's timer, 0x26A77), for as long as the original took to open the video
// from the CD (here a fixed three frames).
const uint32_t kStaticFrameMillis = 255;
const uint32_t kStaticMillis = 3 * kStaticFrameMillis;

const int kNumCursors = 26;
const int kCursorReply = 15;
const uint32_t kCycleMillis = 300;

} // namespace

// ---- Videos -----------------------------------------------------------------------------

bool Game::startVideo(const std::string &dir, const std::string &name, int x, int y, bool keepScreen,
                      RleAnimation *staticNoise) {
    std::string path = dir + "/" + text::toUpper(name) + ".SMK";
    auto stream = _resources.openFile(path);
    auto decoder = std::make_unique<smacker::Decoder>();
    if (!stream || !decoder->load(std::move(stream))) {
        log::warning("Video %s not found or unreadable", path.c_str());
        return false;
    }
    log::debug(1, "Video %s (%dx%d, %d frames) at (%d, %d)", path.c_str(), decoder->width(), decoder->height(),
               decoder->frameCount(), x, y);
    _video = VideoPlayback();
    _video.active = true;
    _video.decoder = std::move(decoder);
    _video.x = x;
    _video.y = y;
    _video.startMillis = _millis;

    // The video is drawn over a black screen, or over the current picture.
    if (!keepScreen)
        draw::fill(_screen, 0);
    draw::create(_background, kScreenWidth, kScreenHeight);
    draw::blit(_background, _screen, 0, 0);
    _workBufferStale = true;
    if (staticNoise && staticNoise->frameCount()) {
        _video.staticNoise = std::move(*staticNoise);
        _video.staticUntil = _millis + kStaticMillis;
        _video.startMillis = _video.staticUntil;
    }

    audio::Mixer *mixer = _system->getMixer();
    if (mixer && _video.decoder->hasAudio()) {
        auto audio = std::make_shared<audio::QueuingStream>(_video.decoder->audioRate(), _video.decoder->audioStereo());
        _video.audio = audio;
        _video.audioHandle = mixer->play(audio, audio::SoundType::kMusic);
    }
    _thread.wait = kWaitVideo;
    updateVideo();
    return true;
}

void Game::updateVideo() {
    if (_millis < _video.staticUntil) {
        // The static, each frame at its own screen position.
        const RleAnimation &noise = _video.staticNoise;
        int frame = int((_millis + kStaticMillis - _video.staticUntil) / kStaticFrameMillis) % noise.frameCount();
        noise.drawFrame(frame, _background);
        draw::blit(_screen, _background, 0, 0);
        return;
    }
    smacker::Decoder &dec = *_video.decoder;
    uint64_t elapsed = uint64_t(_millis - _video.startMillis) * 1000;
    // Show every frame whose time has come; the last one stays for its full time.
    while (_video.framesShown < dec.frameCount() && uint64_t(_video.framesShown) * dec.frameMicros() <= elapsed) {
        if (!dec.decodeNextFrame()) {
            _video.framesShown = dec.frameCount();
            break;
        }
        _video.framesShown++;
        if (dec.paletteChanged()) {
            std::memcpy(_videoPalette, dec.palette(), 768);
            _palette = Palette();
            for (int i = 0; i < 256; i++)
                _palette.setColor(i, _videoPalette[i * 3], _videoPalette[i * 3 + 1], _videoPalette[i * 3 + 2]);
            _fadeLevel = 256;
            applyPalette();
        }
        draw::blit(_background, dec.surface(), _video.x, _video.y);
        if (_video.audio)
            _video.audio->queue(dec.takeAudio());
    }
    draw::blit(_screen, _background, 0, 0);
    if (uint64_t(dec.frameCount()) * dec.frameMicros() <= elapsed)
        finishVideo(false);
}

void Game::finishVideo(bool skipped) {
    if (!_video.active)
        return;
    if (_video.audio) {
        _video.audio->finish();
        if (skipped && _system->getMixer())
            _system->getMixer()->stop(_video.audioHandle);
    }
    if (!_video.resultVariable.empty()) {
        _variables[_video.resultVariable] = skipped ? kVideoSkipped : _video.result;
        log::debug(2, "  $%s = %u", _video.resultVariable.c_str(), _variables[_video.resultVariable]);
    }
    bool trans = _video.trans;
    bool credits = _video.thenCredits;
    _video = VideoPlayback();
    // The last frame and the video's palette stay until the script shows a
    // background (with its palette). After a TRANS video the scene's ambient
    // sounds start again.
    if (trans) {
        for (int sound : _ambientSounds) {
            Argument ambient;
            ambient.type = kArgSymbol;
            ambient.value = 0;
            playSound(sound, &ambient);
        }
    }
    _thread.wait = kWaitNone;
    // The epilogue is followed by the credits.
    if (credits)
        startCredits(true, false);
}

bool Game::playActionSequenceVideo(int type, const Argument *resultVar) {
    if (type < 0 || type >= 32 || kVideos[type].kind == kRejected)
        return false;
    const VideoEntry &v = kVideos[type];
    bool trans = v.dir == kDirTrans;
    if (trans) {
        // TRANS videos stop the ambient sounds and start them again afterwards.
        std::vector<int> ambients = _ambientSounds;
        if (audio::Mixer *mixer = _system->getMixer())
            for (audio::SoundHandle h : _ambients)
                mixer->stop(h);
        _ambients.clear();
        _ambientSounds = ambients;
    }
    if (v.kind == kArcade) {
        autosave(type);
        startArcade(type, resultVar);
        return true;
    }
    if (!startVideo(kDirNames[v.dir], v.name, v.x, v.y, v.kind == kPlainKeep)) {
        setVariable(resultVar, kVideoMissing);
        return true;
    }
    _video.trans = trans;
    _video.resultVariable = variableName(resultVar);
    _video.result = kVideoPlayed;
    if (type == 0x17)
        _video.thenCredits = true;
    return true;
}

int Game::playVideoFile(const std::string &path) {
    _system->initGraphics(kScreenWidth, kScreenHeight);
    draw::create(_screen, kScreenWidth, kScreenHeight);
    _system->setMouseCursorVisible(false);
    size_t slash = path.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash);
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (name.size() > 4 && text::equalsIgnoreCase(name.substr(name.size() - 4), ".SMK"))
        name.resize(name.size() - 4);
    if (!startVideo(dir, name, 0, 0, false))
        return 1;
    // Centre it.
    _video.x = (kScreenWidth - _video.decoder->width()) / 2;
    _video.y = (kScreenHeight - _video.decoder->height()) / 2;
    _nextStepTime = _system->getMillis();
    while (_video.active && !_quit && !_vm->shouldQuit()) {
        InputEvent event;
        while (_system->pollEvent(event)) {
            if (event.type == InputEvent::kQuit)
                _quit = true;
            else if (event.type == InputEvent::kKeyDown && event.key == key::kEscape)
                finishVideo(true);
        }
        uint32_t now = _system->getMillis();
        while (_video.active && int32_t(now - _nextStepTime) >= 0) {
            _millis += 20;
            updateVideo();
            _nextStepTime += 20;
        }
        _system->copyRectToScreen(_screen);
        _system->updateScreen();
        _system->delayMillis(2);
    }
    return 0;
}

// ---- Action sequences ---------------------------------------------------------------------

void Game::startArcade(int type, const Argument *resultVar) {
    // The scene's sounds stop; the cockpit picture the script has shown stays
    // around the video.
    if (audio::Mixer *mixer = _system->getMixer())
        for (audio::SoundHandle h : _ambients)
            mixer->stop(h);
    std::vector<int> ambients = _ambientSounds;
    _ambients.clear();
    _ambientSounds = ambients;

    _arcade = std::make_unique<Arcade>(_resources, _system->getMixer(), type, _arcadeDifficulty);
    // Some sequences continue from the previous one with a third of its damage.
    int damage = (type == 0x1A || type == 0x1E || type == 0x1F) ? _lastArcadeDamage / 3 : 0;
    _arcadeResultVariable = variableName(resultVar);
    _arcadeLeft = _arcadeRight = false;
    _arcadeStartMillis = _millis;
    _thread.wait = kWaitArcade;
    if (!_arcade->start(_screen, damage, _arcadeGroups)) {
        finishArcade();
        return;
    }
    _status.clear();
    updateCaption();
}

void Game::updateArcade() {
    Point aim = cursorHitPoint(false);
    // Clicks wait for the next video frame.
    if (_arcade->update(_millis - _arcadeStartMillis, aim, _arcadeLeft, _arcadeRight))
        _arcadeLeft = _arcadeRight = false;
    if (_arcade->takePaletteChanged()) {
        const uint8_t *p = _arcade->palette();
        for (int i = 0; i < 256; i++)
            _palette.setColor(i, p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
        _fadeLevel = 256;
        applyPalette();
    }
    draw::blit(_screen, _arcade->getScreen(), 0, 0);
    if (_arcade->isDone())
        finishArcade();
}

void Game::finishArcade() {
    uint16_t result = _arcade->getResult();
    if (result != kArcadeMissing) {
        _lastArcadeDamage = _arcade->getDamage();
        _arcade->saveGroups(_arcadeGroups);
    }
    std::string death = _arcade->getDeathVideo();
    std::string name = _arcade->getName();
    _arcade.reset();
    if (!_arcadeResultVariable.empty())
        _variables[_arcadeResultVariable] = result;
    log::debug(1, "Action sequence result %u", result);
    draw::create(_background, kScreenWidth, kScreenHeight);
    draw::blit(_background, _screen, 0, 0);
    _workBufferStale = true;
    _thread.wait = kWaitNone;

    // After the sequence the scene's ambient sounds start again.
    std::vector<int> ambients = _ambientSounds;
    _ambientSounds.clear();
    for (int sound : ambients) {
        Argument ambient;
        ambient.type = kArgSymbol;
        ambient.value = 0;
        playSound(sound, &ambient);
    }
    // Losing shows how.
    if (result == kArcadeKilled || result == kArcadeDoorIntact) {
        // TV static from the cockpit (PANEL/<NAME>STAT.RLE), then the death video.
        RleAnimation noise;
        if (auto s = _resources.openFile("PANEL/" + name + "STAT.RLE")) {
            uint8_t header[8];
            noise.loadWithHeader(*s, header);
        }
        startVideo("TRANS", death, 0, 0, true, &noise);
    }
}

void Game::moveMouse(int x, int y) {
    InputEvent event;
    event.type = InputEvent::kMouseMove;
    event.mouse = Point{x, y};
    handleEvent(event);
}

void Game::rightClick(int x, int y) {
    InputEvent event;
    event.type = InputEvent::kRightDown;
    event.mouse = Point{x, y};
    handleEvent(event);
}

// ---- Cursors ----------------------------------------------------------------------------

void Game::loadCursors() {
    // art\mouse.ani: u16 count (26), then per cursor a 12-byte header (byte 1:
    // transparent colour, u16 height, width, hotspot y, x) and raw pixels.
    _cursorsLoaded = true;
    auto s = _resources.openFile("ART/MOUSE.ANI");
    if (!s) {
        log::warning("art\\mouse.ani not found: using the system mouse pointer");
        return;
    }
    int count = std::min<int>(s->readUint16LE(), kNumCursors);
    for (int i = 0; i < count && !s->eos(); i++) {
        CursorImage c;
        s->readByte();
        c.transparent = s->readByte();
        c.h = s->readUint16LE();
        c.w = s->readUint16LE();
        c.hotY = s->readUint16LE();
        c.hotX = s->readUint16LE();
        s->readUint16LE();
        if (c.w > 640 || c.h > 480)
            break;
        c.pixels.resize(size_t(c.w) * c.h);
        if (s->read(c.pixels.data(), uint32_t(c.pixels.size())) != c.pixels.size())
            break;
        _cursors.push_back(std::move(c));
    }
    if (_cursors.empty()) {
        log::warning("art\\mouse.ani could not be read: using the system mouse pointer");
        return;
    }
    _system->setMouseCursorVisible(false);
}

int Game::currentCursor() const {
    if (_menu.active)
        return _menu.mode == GameMenu::kMain && _menu.hover >= 0 ? 15 : kCursorNormal;
    if (_arcade)
        return kCursorGunsight;
    if (_conv.active) {
        if (_conv.speaking)
            return kCursorReply;
        if (_conv.menuWaiting && _conv.hover >= 0)
            return kCursorReply;
    } else if (_thread.active && _thread.wait == kWaitAnim) {
        return kCursorNormal;
    }
    int i = _hoverHotspot;
    return i >= 0 && i < int(_hotspots.size()) ? _hotspots[i].cursor : kCursorNormal;
}

Point Game::cursorHitPoint(bool menu) const {
    // Scenes test the centre of the cursor image, the reply menu its hotspot.
    int id = currentCursor();
    if (id < 0 || id >= int(_cursors.size()))
        return _mouse;
    const CursorImage &c = _cursors[id];
    if (menu)
        return Point{_mouse.x + c.hotX, _mouse.y + c.hotY};
    return Point{_mouse.x + c.w / 2, _mouse.y + c.h / 2};
}

void Game::drawCursor(Surface &dst) {
    int id = currentCursor();
    if (_video.active || id < 0 || id >= int(_cursors.size()))
        return;
    const CursorImage &c = _cursors[id];
    // The game keeps the whole cursor on screen.
    int x = std::min<int>(_mouse.x, kScreenWidth - c.w - 1);
    int y = std::min<int>(_mouse.y, kScreenHeight - c.h - 1);
    for (int r = 0; r < c.h; r++) {
        for (int col = 0; col < c.w; col++) {
            uint8_t p = c.pixels[size_t(r) * c.w + col];
            int px = x + col;
            int py = y + r;
            if (p != c.transparent && px >= 0 && py >= 0 && px < dst.w && py < dst.h)
                *draw::at(dst, px, py) = p;
        }
    }
}

void Game::cyclePalette() {
    // Entries 1..5 rotate every 300 ms in scenes (the 250 Hz timer, 0x2B48B).
    if (_millis < _nextCycleMillis)
        return;
    _nextCycleMillis = _millis + kCycleMillis;
    const uint8_t *p = _palette.data();
    uint8_t last[3] = {p[15], p[16], p[17]};
    for (int i = 5; i > 1; i--)
        _palette.setColor(i, p[(i - 1) * 3], p[(i - 1) * 3 + 1], p[(i - 1) * 3 + 2]);
    _palette.setColor(1, last[0], last[1], last[2]);
    applyPalette();
}

} // namespace reluva::stharbinger
