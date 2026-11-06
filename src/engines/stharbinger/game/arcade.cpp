// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/game/arcade.h"

#include "common/audio/stream.h"
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
    const char* name;
    int x, y, w, h;
    int startFrame, endFrame;
    bool torpedoes, shieldHud, overlay;
    const char* deathVideo;
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

const SequenceInfo* findSequence(int type) {
    for (const SequenceInfo& s : kSequences)
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
        : segments_(std::move(segments)), order_(std::move(order)), tag_(tag) {}

    void playTag() {
        std::lock_guard<std::mutex> lock(mutex_);
        index_ = tag_;
        pos_ = 0;
        tagPlaying_ = true;
        ended_ = tag_ < 0;
    }
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        ended_ = true;
    }

    int read(int16_t* buffer, int numSamples) override {
        std::lock_guard<std::mutex> lock(mutex_);
        int n = 0;
        while (n < numSamples && !ended_) {
            if (index_ < 0 || index_ >= int(order_.size())) {
                ended_ = true;
                break;
            }
            const std::vector<int16_t>& seg = segments_[order_[index_]];
            if (pos_ >= seg.size()) {
                pos_ = 0;
                if (tagPlaying_) {
                    ended_ = true;
                    break;
                }
                // The next segment; wrap at the end or at the tag.
                index_++;
                if (index_ >= int(order_.size()) || index_ == tag_)
                    index_ = 0;
                if (segments_[order_[index_]].empty()) {
                    ended_ = true;
                    break;
                }
                continue;
            }
            buffer[n++] = seg[pos_++];
        }
        return n;
    }
    bool stereo() const override { return false; }
    int rate() const override { return kSampleRate; }
    bool ended() const override { return ended_; }

private:
    std::mutex mutex_;
    std::vector<std::vector<int16_t>> segments_;
    std::vector<int> order_;
    int tag_;
    int index_ = 0;
    size_t pos_ = 0;
    bool tagPlaying_ = false;
    bool ended_ = false;
};

Arcade::Arcade(Resources& resources, audio::Mixer* mixer, int type, int difficulty)
    : resources_(resources), mixer_(mixer), type_(type), difficulty_(difficulty) {
    const SequenceInfo* info = findSequence(type);
    if (info) {
        name_ = info->name;
        rect_ = Rect(info->x, info->y, info->x + info->w, info->y + info->h);
        startFrame_ = info->startFrame;
        endFrame_ = info->endFrame;
        torpedoes_ = info->torpedoes;
        shieldHud_ = info->shieldHud;
        overlay_ = info->overlay;
    }
}

Arcade::~Arcade() {
    if (mixer_) {
        mixer_->stop(musicHandle_);
        mixer_->stop(videoAudioHandle_);
    }
}

const char* Arcade::getDeathVideo() const {
    const SequenceInfo* info = findSequence(type_);
    return info ? info->deathVideo : "DEATHMIX";
}

int Arcade::random(int n) {
    randSeed_ = randSeed_ * 1103515245u + 12345u;
    int r = int((randSeed_ >> 16) & 0x7FFF);
    return n > 0 ? r % n : 0;
}

// ---- Loading ------------------------------------------------------------------------------

bool Arcade::loadSound(const std::string& name, std::vector<uint8_t>& data) const {
    auto s = resources_.openFile("SOUND/" + text::toUpper(name) + ".RAW");
    if (!s)
        return false;
    data = s->readAll();
    return !data.empty();
}

void Arcade::playSound(const std::vector<uint8_t>& data, audio::SoundType type) {
    if (mixer_ && !data.empty())
        mixer_->play(makeRawStream(data), type);
}

void Arcade::loadData() {
    std::vector<std::string> files;
    for (const std::string& name : resources_.getSearchSet().list())
        files.push_back(fs::normalizeDosPath(name));
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    auto endsWith = [](const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    auto inDir = [&](const std::string& f, const std::string& dir) {
        return f.compare(0, dir.size(), dir) == 0 && f.find('/', dir.size()) == std::string::npos;
    };

    // Explosions, sorted by name: EXPLLG01..05, then EXPLSM01..05.
    for (const std::string& f : files) {
        if (inDir(f, "EXPLODE/") && f.compare(8, 4, "EXPL") == 0 && endsWith(f, ".RLE")) {
            RleAnimation a;
            uint8_t header[8];
            if (auto s = resources_.openFile(f))
                if (a.loadWithHeader(*s, header))
                    explosions_.push_back(std::move(a));
        }
    }

    static const char* const kTorpedoNames[6] = {"T29L", "T17L", "T09L", "T29R", "T17R", "T09R"};
    uint8_t header[8];
    for (int i = 0; i < 6; i++)
        if (auto s = resources_.openFile(std::string("TORPEDOS/") + kTorpedoNames[i] + ".RLE"))
            torpedoAnims_[i].loadWithHeader(*s, header);
    if (auto s = resources_.openFile("TORPEDOS/T5ALL.RLE"))
        torpedoEnd_.loadWithHeader(*s, header);

    // Targets: every RLE file of the sequence's sprite folder, in name order.
    std::string dir = "SPRITES/" + name_ + "/";
    for (const std::string& f : files) {
        if (!inDir(f, dir) || !endsWith(f, ".RLE") || int(targets_.size()) >= kMaxSprites)
            continue;
        Target t;
        auto s = resources_.openFile(f);
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
        targets_.push_back(std::move(t));
    }
    for (Target& t : targets_)
        t.draw = &t.rle; // the vector has moved

    if (auto s = resources_.openFile("PANEL/RUNSH.RLE"))
        if (shieldHud_)
            shield_.loadWithHeader(*s, header);
    if (overlay_)
        if (auto s = resources_.openFile("ART/" + name_ + "RUN.RLE"))
            windshield_.loadWithHeader(*s, header);
    if (type_ == 0x06) {
        if (auto s = resources_.openFile("SPRITES/PDOOR.RLE")) {
            door_.loadWithHeader(*s, header);
            doorStart_ = header[0] | (header[1] << 8);
        }
    }

    for (int i = 0; i < 5; i++)
        loadSound(text::format("BANG%02d", i + 1), bangs_[i]);
    if (!loadSound("QUANT", quant_))
        loadSound("QUANTUM", quant_);
    loadSound("PHASOR", phasor_);
    loadSound("SHAKE", shake_);
    loadMusic();
    loadVoice();
    log::debug(1, "Arcade %s: %u targets, %u explosions, %u voice cues", name_.c_str(), unsigned(targets_.size()),
               unsigned(explosions_.size()), unsigned(voice_.size()));
}

void Arcade::loadMusic() {
    // SOUND/<name>.MUS: a number, then segment suffixes (NAME + suffix + .RAW);
    // the "tag" segment ends the music.
    auto s = resources_.openFile("SOUND/" + name_ + ".MUS");
    if (!s)
        return;
    std::vector<uint8_t> data = s->readAll();
    std::vector<std::string> lines = text::split(std::string(data.begin(), data.end()), '\n');
    if (lines.empty())
        return;
    musicValue_ = std::atoi(lines[0].c_str());
    std::map<std::string, int> loaded;
    for (size_t i = 1; i < lines.size() && musicOrder_.size() < 20; i++) {
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
            loadSound(name_ + suffix, raw);
            musicSegments_.push_back(decodeRaw(std::move(raw)));
            it = loaded.emplace(suffix, int(musicSegments_.size()) - 1).first;
        }
        if (suffix == "TAG")
            musicTag_ = int(musicOrder_.size());
        musicOrder_.push_back(it->second);
    }
}

void Arcade::loadVoice() {
    // VOICE/<name>.VCE: pairs of lines: a sound name, then a video frame or
    // d<n> (plays when damage first reaches 95 - 5 * (n / 5)).
    auto s = resources_.openFile("VOICE/" + name_ + ".VCE");
    if (!s)
        return;
    std::vector<uint8_t> data = s->readAll();
    std::vector<std::string> lines;
    for (std::string l : text::split(std::string(data.begin(), data.end()), '\n')) {
        l = text::trim(l);
        if (!l.empty())
            lines.push_back(l);
    }
    for (size_t i = 0; i + 1 < lines.size() && voice_.size() < 25; i += 2) {
        VoiceCue cue;
        std::string name;
        for (char c : lines[i]) {
            if (c < '0')
                break;
            name += c;
        }
        loadSound(name, cue.sound);
        const std::string& trigger = lines[i + 1];
        if (trigger[0] == 'd' || trigger[0] == 'D')
            cue.damageBand = std::atoi(trigger.c_str() + 1) / 5;
        else
            cue.frame = std::atoi(trigger.c_str());
        voice_.push_back(std::move(cue));
    }
}

bool Arcade::start(const Surface& screen, int initialDamage, const std::map<int, ArcadeGroupState>& groups) {
    auto stream = resources_.openFile("MOVIES/" + name_ + ".SMK");
    video_ = std::make_unique<smacker::Decoder>();
    if (!stream || !video_->load(std::move(stream))) {
        log::warning("Action sequence video MOVIES/%s.SMK not found", name_.c_str());
        result_ = kArcadeMissing;
        done_ = true;
        return false;
    }
    // The cockpit picture stays around the video.
    draw::create(work_, 640, 480);
    draw::blit(work_, screen, 0, 0);
    draw::create(compose_, 640, 480);
    draw::blit(compose_, screen, 0, 0);
    loadData();
    damage_ = initialDamage;
    // The Defiant's second sequence continues the first one's groups.
    if (type_ == 0x1E) {
        for (Target& t : targets_) {
            auto it = groups.find(t.id);
            if (t.id >= 100 && it != groups.end()) {
                t.state = it->second.state;
                t.hits = it->second.hits;
            }
        }
    }
    if (mixer_ && video_->hasAudio() && type_ == 0x1A) {
        auto audio = std::make_shared<audio::QueuingStream>(video_->audioRate(), video_->audioStereo());
        videoAudio_ = audio;
        videoAudioHandle_ = mixer_->play(audio, audio::SoundType::Music);
    }
    if (mixer_ && !musicOrder_.empty()) {
        auto music = std::make_shared<MusicStream>(musicSegments_, musicOrder_, musicTag_);
        music_ = music;
        musicHandle_ = mixer_->play(music, audio::SoundType::Music, mixerVolume(kVolumeArcadeMusic));
    }
    return true;
}

void Arcade::saveGroups(std::map<int, ArcadeGroupState>& out) const {
    for (const Target& t : targets_) {
        if (t.id < 100)
            continue;
        ArcadeGroupState& g = out[t.id];
        if (t.groupGone || t.state == 2)
            g.state = 2;
        g.hits = std::max(g.hits, t.hits);
    }
}

bool Arcade::takePaletteChanged() {
    bool c = paletteChanged_;
    paletteChanged_ = false;
    return c;
}

// ---- The frame loop -------------------------------------------------------------------------

bool Arcade::update(uint32_t millis, Point aim, bool leftPressed, bool rightPressed) {
    aim_ = aim;
    uint64_t elapsed = uint64_t(millis) * 1000;
    bool stepped = false;
    while (!done_ && uint64_t(frame_ + 1) * video_->frameMicros() <= elapsed) {
        stepFrame(leftPressed, rightPressed);
        leftPressed = rightPressed = false;
        stepped = true;
    }
    return stepped;
}

void Arcade::stepFrame(bool leftPressed, bool rightPressed) {
    if (!video_->decodeNextFrame()) {
        finishFrame();
        done_ = true;
        return;
    }
    frame_++;
    if (video_->paletteChanged()) {
        std::memcpy(palette_, video_->palette(), 768);
        paletteChanged_ = true;
    }
    if (videoAudio_)
        videoAudio_->queue(video_->takeAudio());
    else
        video_->takeAudio();
    draw::blit(work_, video_->surface(), rect_.left, rect_.top);

    // The video into the picture, shifted by the shake (the uncovered strip keeps
    // the last frame).
    for (int y = rect_.top; y < rect_.bottom; y++) {
        int dy = y + shakeY_;
        if (dy < rect_.top || dy >= rect_.bottom)
            continue;
        for (int x = rect_.left; x < rect_.right; x++) {
            int dx = x + shakeX_;
            if (dx >= rect_.left && dx < rect_.right)
                *draw::at(compose_, dx, dy) = *draw::at(work_, x, y);
        }
    }

    // Shields recharge.
    if (damage_ && frame_ % (difficulty_ == 0 ? 10 : 13) == 0)
        damage_--;

    torpedoFiredThisFrame_ = false;
    if (type_ == 0x06)
        updateDoor(leftPressed);
    updateTargets();
    if (rightPressed && torpedoes_ && rect_.contains(aim_.x, aim_.y))
        fireTorpedo();
    updateTorpedoes();
    if (leftPressed && !torpedoFiredThisFrame_ && rect_.contains(aim_.x, aim_.y))
        firePhaser();
    drawHud();
    updateShake();
    finishFrame();
}

void Arcade::finishFrame() {
    // After each frame: the music's tag, voice cues, death and victory.
    if (frame_ == startFrame_) {
        uint32_t elapsedMs = uint32_t(uint64_t(startFrame_) * video_->frameMicros() / 1000);
        tagFrame_ = endFrame_ - int(int64_t(elapsedMs) * musicValue_ / 10000) + endFrame_ / 40;
    }
    if (frame_ == tagFrame_ && music_)
        music_->playTag();

    for (VoiceCue& cue : voice_) {
        if (cue.frame >= 0 && cue.frame == frame_)
            playSound(cue.sound, audio::SoundType::Speech);
        if (cue.damageBand >= 0 && cue.damageBand < 20 && voiceFlags_[cue.damageBand] == 1) {
            voiceFlags_[cue.damageBand] = 2;
            playSound(cue.sound, audio::SoundType::Speech);
        }
    }

    if (damage_ >= 100) {
        result_ = kArcadeKilled;
    } else if (damage_ / 5 != prevDamage_ / 5 && damage_ > prevDamage_) {
        int band = 19 - damage_ / 5;
        if (band >= 0 && band < 20 && voiceFlags_[band] == 0)
            voiceFlags_[band] = 1;
    }
    prevDamage_ = damage_;
    if (winCountdown_ && --winCountdown_ == 1)
        result_ = kArcadeWon;

    if (result_ != 0 || frame_ + 1 >= video_->frameCount() || done_) {
        if (type_ == 0x06 && result_ == 0)
            result_ = doorDestroyed_ ? kArcadeWon : kArcadeDoorIntact;
        done_ = true;
        if (music_)
            music_->stop();
        if (videoAudio_)
            videoAudio_->finish();
        log::debug(1, "Arcade %s ended at frame %d: result %u, damage %d", name_.c_str(), frame_, result_, damage_);
    }
}

// ---- Targets --------------------------------------------------------------------------------

void Arcade::updateTargets() {
    targetUnderSight_ = -1;
    for (int i = 0; i < int(targets_.size()); i++) {
        Target& t = targets_[i];
        if (t.state == 2 || t.start > frame_)
            continue;
        const RleAnimation::Frame& path = t.rle.getFrame(t.pathFrame);
        const RleAnimation::Frame& draw = t.draw->getFrame(t.drawFrame);
        int x = path.x + rect_.left + shakeX_;
        int y = path.y + rect_.top + shakeY_;
        if (t.state == 1) {
            // An explosion is centred on the target's path.
            x -= (draw.w - path.w) / 2;
            y -= (draw.h - path.h) / 2;
        }
        if (t.state == 0 && aim_.x >= x && aim_.x < x + draw.w && aim_.y >= y && aim_.y < y + draw.h)
            targetUnderSight_ = i;
        t.draw->drawFrameAt(t.drawFrame, compose_, x, y, &rect_);
        enemyFire(t, x, y);

        // One sprite frame per video frame; the target is gone when either list ends.
        t.drawFrame++;
        t.pathFrame++;
        if (t.drawFrame >= t.draw->frameCount() || t.pathFrame >= t.rle.frameCount())
            t.state = 2;
    }
}

void Arcade::enemyFire(Target& t, int x, int y) {
    int delay = t.id >= 100 ? t.id - 100 : t.id;
    int period = std::max(1, t.period + 1 - difficulty_);
    if (t.state != 0 || doorDestroyed_ || winCountdown_ || t.start + delay >= frame_ || t.gun == 0xFF ||
        (frame_ - t.start) % period != 0)
        return;
    const RleAnimation::Frame& f = t.draw->getFrame(t.drawFrame);
    int mx = f.muzzleX[t.muzzle & 1], my = f.muzzleY[t.muzzle & 1];
    if (mx == 0 && my == 0)
        return;
    int sx = x + mx, sy = y + my;
    if (!rect_.contains(sx, sy))
        return;
    if (shakeCount_ == 0)
        playSound(shake_);
    // Enemy shots hit at once.
    damage_ += t.damage;
    int cx = std::clamp(sx, rect_.left + rect_.width() / 8, rect_.right - rect_.width() / 8);
    drawEnemyBeam(sx, sy, cx, rect_.bottom - 6, t.damage > 10 ? 7 : 1);
    shakeCount_ = 4;
    shakeIntensity_ = (std::min(t.damage, 3) - 1) * 2;
    if (t.gun == 2)
        t.muzzle ^= 1;
}

void Arcade::hitTarget(int index, int amount, bool torpedo) {
    Target& t = targets_[index];
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
        for (int i = 0; i < int(targets_.size()); i++)
            if (i != index && targets_[i].id == t.id)
                targets_[i].hits += amount;
}

void Arcade::destroyTarget(int index) {
    Target& t = targets_[index];
    t.state = 1;
    int half = int(explosions_.size()) / 2;
    if (half > 0) {
        // A small explosion if it covers the target by its fifth frame, else a large one.
        int r = half + random(half);
        const RleAnimation::Frame& f = t.draw->getFrame(t.drawFrame < t.draw->frameCount() ? t.drawFrame : 0);
        const RleAnimation& small = explosions_[r];
        int smallArea = small.frameCount() > 4 ? small.getFrame(4).w * small.getFrame(4).h : 0;
        t.draw = smallArea >= f.w * f.h ? &small : &explosions_[r - half];
        t.drawFrame = 0;
        playSound(bangs_[std::min(r - half, 4)]);
    }
    if (t.id > 100) {
        // The rest of the group vanishes.
        for (int i = 0; i < int(targets_.size()); i++) {
            if (i != index && targets_[i].id == t.id) {
                targets_[i].state = 2;
                targets_[i].groupGone = true;
            }
        }
        t.groupGone = true;
    }
    if (t.id >= 200)
        winCountdown_ = kWinCountdown;
}

// ---- The player's weapons ---------------------------------------------------------------------

void Arcade::firePhaser() {
    int launchY = rect_.top + rect_.height() * 3 / 4;
    drawPhaserBeam(rect_.left, launchY, aim_.x, aim_.y, kPhaserColor);
    drawPhaserBeam(rect_.right - 1, launchY, aim_.x, aim_.y, kPhaserColor);
    playSound(phasor_);
    if (targetUnderSight_ >= 0 && targets_[targetUnderSight_].state == 0)
        hitTarget(targetUnderSight_, 1, false);
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
    Torpedo* slot = nullptr;
    for (Torpedo& t : torps_)
        if (t.state < 0) {
            slot = &t;
            break;
        }
    if (!slot)
        return;
    torpedoFiredThisFrame_ = true;
    playSound(quant_);
    slot->state = 0;
    slot->aimX = aim_.x;
    slot->aimY = aim_.y;
    int launchY = rect_.top + rect_.height() * 3 / 4;
    int dy = aim_.y - launchY;
    for (int side = 0; side < 2; side++) {
        int dx = side == 0 ? aim_.x - rect_.left : aim_.x - (rect_.right - 1);
        int dir = torpedoDirection(dx, dy);
        for (int k = 0; k < 4; k++) {
            const RleAnimation* anim = k < 3 ? &torpedoAnims_[side * 3 + k] : &torpedoEnd_;
            int frame = k < 3 ? dir : 0;
            if (frame >= anim->frameCount())
                frame = 0;
            int ox = (dx * kTorpedoStep[k] + 8) / 16;
            int oy = (dy * kTorpedoStep[k] + 8) / 16;
            int x = side == 0 ? ox + rect_.left : ox + rect_.right - (anim->frameCount() ? anim->getFrame(0).w : 0);
            for (int pair = 0; pair < 2; pair++) {
                Torpedo::Sprite& s = slot->sprites[k][side * 2 + pair];
                s.anim = anim;
                s.frame = frame;
                s.x = x;
                s.y = oy + (pair == 0 ? -kTorpedoHalfGap[k] : kTorpedoHalfGap[k]) + launchY;
            }
        }
    }
}

void Arcade::updateTorpedoes() {
    for (Torpedo& t : torps_) {
        if (t.state < 0)
            continue;
        if (t.state < 4) {
            for (const Torpedo::Sprite& s : t.sprites[t.state])
                if (s.anim && s.anim->frameCount())
                    s.anim->drawFrameAt(s.frame, compose_, s.x + shakeX_, s.y + shakeY_, &rect_);
            t.state++;
            continue;
        }
        // Hits on the fifth frame, where the player aimed.
        torpedoImpact(t.aimX, t.aimY);
        t.state = -1;
    }
}

void Arcade::torpedoImpact(int x, int y) {
    if (type_ == 0x06 && !doorDestroyed_ && door_.frameCount()) {
        const RleAnimation::Frame& d = door_.getFrame(doorFrame_);
        Rect r(d.x + rect_.left, d.y + rect_.top, d.x + rect_.left + d.w, d.y + rect_.top + d.h);
        if (r.contains(x, y))
            doorDamage_ += 6;
    }
    for (int i = 0; i < int(targets_.size()); i++) {
        Target& t = targets_[i];
        if (t.state != 0 || t.start >= frame_)
            continue;
        const RleAnimation::Frame& f = t.draw->getFrame(std::min(t.drawFrame, t.draw->frameCount() - 1));
        int tx = f.x + rect_.left, ty = f.y + rect_.top;
        if (x >= tx && x <= tx + f.w && y >= ty && y <= ty + f.h) {
            hitTarget(i, 2, true);
            return;
        }
    }
}

// ---- The plan door (type 6) -------------------------------------------------------------------

void Arcade::updateDoor(bool leftPressed) {
    if (!door_.frameCount())
        return;
    if (frame_ > doorStart_ && doorFrame_ + 1 < door_.frameCount())
        doorFrame_++;
    const RleAnimation::Frame& d = door_.getFrame(doorFrame_);
    Rect r(d.x + rect_.left, d.y + rect_.top, d.x + rect_.left + d.w, d.y + rect_.top + d.h);
    if (!doorDestroyed_) {
        if (leftPressed && r.contains(aim_.x, aim_.y))
            doorDamage_ += 2;
        if (doorDamage_ >= kDoorHealth) {
            // Nine large explosions on a 3x3 grid, a few frames apart.
            doorDestroyed_ = true;
            doorOverlayDelay_ = 2;
            int half = int(explosions_.size()) / 2;
            for (int i = 0; i < 9 && half > 0; i++) {
                DoorExplosion e;
                int index = random(half);
                e.anim = &explosions_[index];
                e.bang = std::min(index / 2, 4);
                e.delay = random(5) + 1;
                e.x = r.left + kDoorGridX[i] * r.width() / 4;
                e.y = r.top + kDoorGridY[i] * r.height() / 4;
                doorExplosions_.push_back(e);
            }
        }
    } else {
        // The blown-open door follows the camera.
        if (doorOverlayDelay_ > 0)
            doorOverlayDelay_--;
        else
            door_.drawFrameAt(doorFrame_, compose_, r.left, r.top, &rect_);
    }
    for (DoorExplosion& e : doorExplosions_) {
        if (e.delay == 1)
            playSound(bangs_[e.bang]);
        if (e.delay > 0) {
            e.delay--;
            continue;
        }
        if (e.frame < e.anim->frameCount()) {
            const RleAnimation::Frame& f = e.anim->getFrame(e.frame);
            e.anim->drawFrameAt(e.frame, compose_, e.x - f.w / 2, e.y - f.h / 2, &rect_);
            e.frame++;
        }
    }
}

// ---- HUD, shake and beams ----------------------------------------------------------------------

void Arcade::drawHud() {
    // The shield indicator on the cockpit panel (blinking every tenth frame).
    if (shieldHud_ && shield_.frameCount()) {
        int n = shield_.frameCount();
        int index = std::max(std::min(damage_ * n / 100, n), 1) - 1;
        if (frame_ % 10 == 9)
            index = 0;
        shield_.drawFrame(index, compose_);
    }
    // The windshield frame over the video.
    for (int i = 0; i < windshield_.frameCount(); i++) {
        const RleAnimation::Frame& f = windshield_.getFrame(i);
        windshield_.drawFrameAt(i, compose_, f.x, f.y, &rect_);
    }
}

void Arcade::updateShake() {
    if (shakeCount_ > 0) {
        int row = std::clamp(shakeIntensity_, 0, 5);
        shakeX_ = kShakeX[row][shakeCount_];
        shakeY_ = kShakeY[row][shakeCount_];
        shakeCount_--;
        // Colour 12 flashes yellow and orange (0x27760).
        static const uint8_t kFlash[2][3] = {{58, 33, 5}, {63, 63, 0}};
        const uint8_t* c = kFlash[shakeCount_ & 1];
        for (int k = 0; k < 3; k++)
            palette_[12 * 3 + k] = uint8_t((c[k] << 2) | (c[k] >> 4));
        paletteChanged_ = true;
    } else {
        shakeX_ = shakeY_ = 0;
    }
}

void Arcade::drawPhaserBeam(int x0, int y0, int x1, int y1, uint8_t color) {
    stharbinger::drawPhaserBeam(compose_, x0, y0, x1, y1, color);
}

void Arcade::drawEnemyBeam(int x0, int y0, int x1, int y1, uint8_t color) {
    stharbinger::drawEnemyBeam(compose_, x0, y0, x1, y1, color, 20, rect_.bottom, [this](int n) { return random(n); });
}

} // namespace reluva::stharbinger
