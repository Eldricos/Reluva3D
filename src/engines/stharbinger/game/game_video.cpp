// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Videos (ACTIONSEQ, the intro) and the mouse cursors, after DEEP.EXE
// (video table 0x24E8F, plain playback 0x26104, cursors 0x1925F).

#include "engines/stharbinger/game/game.h"

#include "common/audio/stream.h"
#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

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
    const char* name;
    VideoDir dir;
    VideoKind kind;
    int x, y;
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

const char* const kDirNames[] = {"INTRO", "MOVIES", "TRANS"};

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

bool Game::startVideo(const std::string& dir, const std::string& name, int x, int y, bool keepScreen,
                      RleAnimation* staticNoise) {
    std::string path = dir + "/" + text::toUpper(name) + ".SMK";
    auto stream = resources_.openFile(path);
    auto decoder = std::make_unique<smacker::Decoder>();
    if (!stream || !decoder->load(std::move(stream))) {
        log::warning("Video %s not found or unreadable", path.c_str());
        return false;
    }
    log::debug(1, "Video %s (%dx%d, %d frames) at (%d, %d)", path.c_str(), decoder->width(), decoder->height(),
               decoder->frameCount(), x, y);
    video_ = VideoPlayback();
    video_.active = true;
    video_.decoder = std::move(decoder);
    video_.x = x;
    video_.y = y;
    video_.startMillis = millis_;

    // The video is drawn over a black screen, or over the current picture.
    if (!keepScreen)
        draw::fill(screen_, 0);
    draw::create(background_, kScreenWidth, kScreenHeight);
    draw::blit(background_, screen_, 0, 0);
    workBufferStale_ = true;
    if (staticNoise && staticNoise->frameCount()) {
        video_.staticNoise = std::move(*staticNoise);
        video_.staticUntil = millis_ + kStaticMillis;
        video_.startMillis = video_.staticUntil;
    }

    audio::Mixer* mixer = system_->getMixer();
    if (mixer && video_.decoder->hasAudio()) {
        auto audio = std::make_shared<audio::QueuingStream>(video_.decoder->audioRate(), video_.decoder->audioStereo());
        video_.audio = audio;
        video_.audioHandle = mixer->play(audio, audio::SoundType::Music);
    }
    thread_.wait = kWaitVideo;
    updateVideo();
    return true;
}

void Game::updateVideo() {
    if (millis_ < video_.staticUntil) {
        // The static, each frame at its own screen position.
        const RleAnimation& noise = video_.staticNoise;
        int frame = int((millis_ + kStaticMillis - video_.staticUntil) / kStaticFrameMillis) % noise.frameCount();
        noise.drawFrame(frame, background_);
        draw::blit(screen_, background_, 0, 0);
        return;
    }
    smacker::Decoder& dec = *video_.decoder;
    uint64_t elapsed = uint64_t(millis_ - video_.startMillis) * 1000;
    // Show every frame whose time has come; the last one stays for its full time.
    while (video_.framesShown < dec.frameCount() && uint64_t(video_.framesShown) * dec.frameMicros() <= elapsed) {
        if (!dec.decodeNextFrame()) {
            video_.framesShown = dec.frameCount();
            break;
        }
        video_.framesShown++;
        if (dec.paletteChanged()) {
            std::memcpy(videoPalette_, dec.palette(), 768);
            palette_ = Palette();
            for (int i = 0; i < 256; i++)
                palette_.setColor(i, videoPalette_[i * 3], videoPalette_[i * 3 + 1], videoPalette_[i * 3 + 2]);
            fadeLevel_ = 256;
            applyPalette();
        }
        draw::blit(background_, dec.surface(), video_.x, video_.y);
        if (video_.audio)
            video_.audio->queue(dec.takeAudio());
    }
    draw::blit(screen_, background_, 0, 0);
    if (uint64_t(dec.frameCount()) * dec.frameMicros() <= elapsed)
        finishVideo(false);
}

void Game::finishVideo(bool skipped) {
    if (!video_.active)
        return;
    if (video_.audio) {
        video_.audio->finish();
        if (skipped && system_->getMixer())
            system_->getMixer()->stop(video_.audioHandle);
    }
    if (!video_.resultVariable.empty()) {
        variables_[video_.resultVariable] = skipped ? kVideoSkipped : video_.result;
        log::debug(2, "  $%s = %u", video_.resultVariable.c_str(), variables_[video_.resultVariable]);
    }
    bool trans = video_.trans;
    bool credits = video_.thenCredits;
    video_ = VideoPlayback();
    // The last frame and the video's palette stay until the script shows a
    // background (with its palette). After a TRANS video the scene's ambient
    // sounds start again.
    if (trans) {
        for (int sound : ambientSounds_) {
            Argument ambient;
            ambient.type = kArgSymbol;
            ambient.value = 0;
            playSound(sound, &ambient);
        }
    }
    thread_.wait = kWaitNone;
    // The epilogue is followed by the credits.
    if (credits)
        startCredits(true, false);
}

bool Game::playActionSequenceVideo(int type, const Argument* resultVar) {
    if (type < 0 || type >= 32 || kVideos[type].kind == kRejected)
        return false;
    const VideoEntry& v = kVideos[type];
    bool trans = v.dir == kDirTrans;
    if (trans) {
        // TRANS videos stop the ambient sounds and start them again afterwards.
        std::vector<int> ambients = ambientSounds_;
        if (audio::Mixer* mixer = system_->getMixer())
            for (audio::SoundHandle h : ambients_)
                mixer->stop(h);
        ambients_.clear();
        ambientSounds_ = ambients;
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
    video_.trans = trans;
    video_.resultVariable = variableName(resultVar);
    video_.result = kVideoPlayed;
    if (type == 0x17)
        video_.thenCredits = true;
    return true;
}

int Game::playVideoFile(const std::string& path) {
    system_->initGraphics(kScreenWidth, kScreenHeight);
    draw::create(screen_, kScreenWidth, kScreenHeight);
    system_->setMouseCursorVisible(false);
    size_t slash = path.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash);
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (name.size() > 4 && text::equalsIgnoreCase(name.substr(name.size() - 4), ".SMK"))
        name.resize(name.size() - 4);
    if (!startVideo(dir, name, 0, 0, false))
        return 1;
    // Centre it.
    video_.x = (kScreenWidth - video_.decoder->width()) / 2;
    video_.y = (kScreenHeight - video_.decoder->height()) / 2;
    nextStepTime_ = system_->getMillis();
    while (video_.active && !quit_ && !vm_->shouldQuit()) {
        InputEvent event;
        while (system_->pollEvent(event)) {
            if (event.type == InputEvent::kQuit)
                quit_ = true;
            else if (event.type == InputEvent::kKeyDown && event.key == key::Escape)
                finishVideo(true);
        }
        uint32_t now = system_->getMillis();
        while (video_.active && int32_t(now - nextStepTime_) >= 0) {
            millis_ += 20;
            updateVideo();
            nextStepTime_ += 20;
        }
        system_->copyRectToScreen(screen_);
        system_->updateScreen();
        system_->delayMillis(2);
    }
    return 0;
}

// ---- Action sequences ---------------------------------------------------------------------

void Game::startArcade(int type, const Argument* resultVar) {
    // The scene's sounds stop; the cockpit picture the script has shown stays
    // around the video.
    if (audio::Mixer* mixer = system_->getMixer())
        for (audio::SoundHandle h : ambients_)
            mixer->stop(h);
    std::vector<int> ambients = ambientSounds_;
    ambients_.clear();
    ambientSounds_ = ambients;

    arcade_ = std::make_unique<Arcade>(resources_, system_->getMixer(), type, arcadeDifficulty_);
    // Some sequences continue from the previous one with a third of its damage.
    int damage = (type == 0x1A || type == 0x1E || type == 0x1F) ? lastArcadeDamage_ / 3 : 0;
    arcadeResultVariable_ = variableName(resultVar);
    arcadeLeft_ = arcadeRight_ = false;
    arcadeStartMillis_ = millis_;
    thread_.wait = kWaitArcade;
    if (!arcade_->start(screen_, damage, arcadeGroups_)) {
        finishArcade();
        return;
    }
    status_.clear();
    updateCaption();
}

void Game::updateArcade() {
    Point aim = cursorHitPoint(false);
    // Clicks wait for the next video frame.
    if (arcade_->update(millis_ - arcadeStartMillis_, aim, arcadeLeft_, arcadeRight_))
        arcadeLeft_ = arcadeRight_ = false;
    if (arcade_->takePaletteChanged()) {
        const uint8_t* p = arcade_->palette();
        for (int i = 0; i < 256; i++)
            palette_.setColor(i, p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
        fadeLevel_ = 256;
        applyPalette();
    }
    draw::blit(screen_, arcade_->getScreen(), 0, 0);
    if (arcade_->isDone())
        finishArcade();
}

void Game::finishArcade() {
    uint16_t result = arcade_->getResult();
    if (result != kArcadeMissing) {
        lastArcadeDamage_ = arcade_->getDamage();
        arcade_->saveGroups(arcadeGroups_);
    }
    std::string death = arcade_->getDeathVideo();
    std::string name = arcade_->getName();
    arcade_.reset();
    if (!arcadeResultVariable_.empty())
        variables_[arcadeResultVariable_] = result;
    log::debug(1, "Action sequence result %u", result);
    draw::create(background_, kScreenWidth, kScreenHeight);
    draw::blit(background_, screen_, 0, 0);
    workBufferStale_ = true;
    thread_.wait = kWaitNone;

    // After the sequence the scene's ambient sounds start again.
    std::vector<int> ambients = ambientSounds_;
    ambientSounds_.clear();
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
        if (auto s = resources_.openFile("PANEL/" + name + "STAT.RLE")) {
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
    cursorsLoaded_ = true;
    auto s = resources_.openFile("ART/MOUSE.ANI");
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
        cursors_.push_back(std::move(c));
    }
    if (cursors_.empty()) {
        log::warning("art\\mouse.ani could not be read: using the system mouse pointer");
        return;
    }
    system_->setMouseCursorVisible(false);
}

int Game::currentCursor() const {
    if (menu_.active)
        return menu_.mode == GameMenu::kMain && menu_.hover >= 0 ? 15 : kCursorNormal;
    if (arcade_)
        return kCursorGunsight;
    if (conv_.active) {
        if (conv_.speaking)
            return kCursorReply;
        if (conv_.menuWaiting && conv_.hover >= 0)
            return kCursorReply;
    } else if (thread_.active && thread_.wait == kWaitAnim) {
        return kCursorNormal;
    }
    int i = hoverHotspot_;
    return i >= 0 && i < int(hotspots_.size()) ? hotspots_[i].cursor : kCursorNormal;
}

Point Game::cursorHitPoint(bool menu) const {
    // Scenes test the centre of the cursor image, the reply menu its hotspot.
    int id = currentCursor();
    if (id < 0 || id >= int(cursors_.size()))
        return mouse_;
    const CursorImage& c = cursors_[id];
    if (menu)
        return Point{mouse_.x + c.hotX, mouse_.y + c.hotY};
    return Point{mouse_.x + c.w / 2, mouse_.y + c.h / 2};
}

void Game::drawCursor(Surface& dst) {
    int id = currentCursor();
    if (video_.active || id < 0 || id >= int(cursors_.size()))
        return;
    const CursorImage& c = cursors_[id];
    // The game keeps the whole cursor on screen.
    int x = std::min<int>(mouse_.x, kScreenWidth - c.w - 1);
    int y = std::min<int>(mouse_.y, kScreenHeight - c.h - 1);
    for (int r = 0; r < c.h; r++) {
        for (int col = 0; col < c.w; col++) {
            uint8_t p = c.pixels[size_t(r) * c.w + col];
            int px = x + col, py = y + r;
            if (p != c.transparent && px >= 0 && py >= 0 && px < dst.w && py < dst.h)
                *draw::at(dst, px, py) = p;
        }
    }
}

void Game::cyclePalette() {
    // Entries 1..5 rotate every 300 ms in scenes (the 250 Hz timer, 0x2B48B).
    if (millis_ < nextCycleMillis_)
        return;
    nextCycleMillis_ = millis_ + kCycleMillis;
    const uint8_t* p = palette_.data();
    uint8_t last[3] = {p[15], p[16], p[17]};
    for (int i = 5; i > 1; i--)
        palette_.setColor(i, p[(i - 1) * 3], p[(i - 1) * 3 + 1], p[(i - 1) * 3 + 2]);
    palette_.setColor(1, last[0], last[1], last[2]);
    applyPalette();
}

} // namespace reluva::stharbinger
