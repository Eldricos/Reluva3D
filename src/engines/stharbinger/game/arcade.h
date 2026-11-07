// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "common/draw.h"
#include "common/video/smacker.h"
#include "engines/stharbinger/formats/graphics.h"
#include "engines/stharbinger/platform.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stharbinger {

class Resources;

// What a group of targets (sprite id >= 100) carries into a later sequence:
// the Nemesis in the Defiant sequences.
struct ArcadeGroupState {
    int state = 0;
    int hits = 0;
};

// Results of an action sequence, as the scripts test them.
enum ArcadeResult {
    kArcadeSurvived = 0,        // the video ended with the player alive
    kArcadeWon = 0x8000,        // the boss (or the plan door) was destroyed
    kArcadeDoorIntact = 0xFFFD, // plan: the door survived (a crash)
    kArcadeKilled = 0xFFFE,     // damage reached 100
    kArcadeMissing = 999
};

// An action sequence (ACTIONSEQ types 2..6, 0x19..0x1B, 0x1E, 0x1F), after
// DEEP.EXE's arcade engine (Run_Arcade 0x261C1, logic 0x2807F, HUD 0x29484).
//
// A Smacker video plays in a rectangle of the cockpit picture. Targets are RLE
// sprites scheduled against the video frames; the player fires phasers (left
// button) and torpedoes (right button) at the cursor's centre. Enemy shots hit
// at once. Everything advances one step per video frame.
class Arcade {
public:
    Arcade(Resources& resources, audio::Mixer* mixer, int type, int difficulty);
    ~Arcade();

    // `screen` is the picture shown when the sequence starts (the cockpit).
    // Returns false if the video is missing.
    bool start(const Surface& screen, int initialDamage, const std::map<int, ArcadeGroupState>& groups);
    // Advances to `millis` after the start. The buttons are presses not yet
    // handled; `aim` is the gunsight's centre. Returns true if a video frame ran
    // (which handles the presses).
    bool update(uint32_t millis, Point aim, bool leftPressed, bool rightPressed);

    bool isDone() const { return done_; }
    uint16_t getResult() const { return result_; }
    int getDamage() const { return damage_; }
    const char* getDeathVideo() const;
    const std::string& getName() const { return name_; }
    void saveGroups(std::map<int, ArcadeGroupState>& out) const;

    const Surface& getScreen() const { return compose_; }
    // 256 RGB entries; true once after each change.
    const uint8_t* palette() const { return palette_; }
    bool takePaletteChanged();

    int getFrame() const { return frame_; }

private:
    struct Target {
        RleAnimation rle;
        int start = 0, hp = 1, damage = 1, period = 10, gun = 0xFF, id = 0;
        const RleAnimation* draw = nullptr; // the sprite, or its explosion
        int drawFrame = 0, pathFrame = 0;
        int hits = 0;
        int muzzle = 0;
        int state = 0; // 0 alive, 1 exploding, 2 gone
        bool groupGone = false;
    };

    struct Torpedo {
        int state = -1; // -1 free, 0..3 flying, 4 impact
        int aimX = 0, aimY = 0;
        struct Sprite {
            const RleAnimation* anim = nullptr;
            int frame = 0;
            int x = 0, y = 0;
        };
        Sprite sprites[4][4]; // [stage][left upper, left lower, right upper, right lower]
    };

    struct DoorExplosion {
        const RleAnimation* anim = nullptr;
        int frame = 0;
        int x = 0, y = 0;
        int delay = 0;
        int bang = 0;
    };

    struct VoiceCue {
        std::vector<uint8_t> sound;
        int frame = -1;
        int damageBand = -1;
    };

    void loadData();
    void loadMusic();
    void loadVoice();
    bool loadSound(const std::string& name, std::vector<uint8_t>& data) const;
    void playSound(const std::vector<uint8_t>& data, audio::SoundType type = audio::SoundType::Effects);

    void stepFrame(bool leftPressed, bool rightPressed);
    void updateDoor(bool leftPressed);
    void updateTargets();
    void enemyFire(Target& t, int x, int y);
    void fireTorpedo();
    void updateTorpedoes();
    void torpedoImpact(int x, int y);
    void firePhaser();
    void hitTarget(int index, int amount, bool torpedo);
    void destroyTarget(int index);
    void drawHud();
    void updateShake();
    void finishFrame();

    void drawPhaserBeam(int x0, int y0, int x1, int y1, uint8_t color);
    void drawEnemyBeam(int x0, int y0, int x1, int y1, uint8_t color);
    int random(int n);
    static int torpedoDirection(int dx, int dy);

    Resources& resources_;
    audio::Mixer* mixer_;
    int type_;
    int difficulty_;
    std::string name_;
    Rect rect_;
    int startFrame_ = 100, endFrame_ = 100;
    bool torpedoes_ = true, shieldHud_ = true, overlay_ = true;

    std::unique_ptr<smacker::Decoder> video_;
    std::shared_ptr<audio::QueuingStream> videoAudio_; // fed while the mixer plays it
    audio::SoundHandle videoAudioHandle_ = audio::kNoSound;
    Surface work_, compose_;
    uint8_t palette_[768] = {};
    bool paletteChanged_ = false;

    std::vector<Target> targets_;
    std::vector<RleAnimation> explosions_; // sorted by name: large first, then small
    RleAnimation torpedoAnims_[6];         // T29L T17L T09L T29R T17R T09R
    RleAnimation torpedoEnd_;              // T5ALL
    RleAnimation shield_, windshield_, door_;
    int doorStart_ = 0;
    int doorFrame_ = 0;
    int doorDamage_ = 0;
    bool doorDestroyed_ = false;
    int doorOverlayDelay_ = 0;
    std::vector<DoorExplosion> doorExplosions_;

    Torpedo torps_[4];
    bool torpedoFiredThisFrame_ = false;
    int targetUnderSight_ = -1;
    Point aim_;

    int frame_ = -1; // the video frame just decoded
    int damage_ = 0;
    int prevDamage_ = 0;
    int winCountdown_ = 0;
    int shakeCount_ = 0, shakeIntensity_ = 0;
    int shakeX_ = 0, shakeY_ = 0;
    uint16_t result_ = 0;
    bool done_ = false;
    uint32_t randSeed_ = 12345;

    struct PhaserShot {
        bool active = false;
        int x = 0, y = 0;
    } phaser_;

    // Sounds.
    std::vector<uint8_t> bangs_[5], quant_, phasor_, shake_;
    std::vector<std::vector<int16_t>> musicSegments_;
    std::vector<int> musicOrder_;
    int musicTag_ = -1;
    int musicValue_ = 0;
    int tagFrame_ = -1;
    class MusicStream;
    std::shared_ptr<MusicStream> music_; // its tag is played on cue
    audio::SoundHandle musicHandle_ = audio::kNoSound;
    std::vector<VoiceCue> voice_;
    int voiceFlags_[20] = {};
};

} // namespace reluva::stharbinger
