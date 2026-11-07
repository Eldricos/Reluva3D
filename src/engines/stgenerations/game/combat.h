// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <deque>
#include <map>
#include <memory>
#include <vector>

#include "engines/stgenerations/audio.h"
#include "engines/stgenerations/canvas.h"
#include "engines/stgenerations/platform.h"
#include "engines/stgenerations/game/random.h"
#include "engines/stgenerations/formats/combatdata.h"
#include "engines/stgenerations/game/combatrender.h"
#include "engines/stgenerations/formats/resources.h"
#include "engines/stgenerations/formats/rgbimage.h"

namespace reluva::stgenerations {

// Space combat (sgens.exe module 0x47B375, the "sc" library): the
// Enterprise against the ships of COMBAT.DAT's SCn.MIS, seen through the
// bridge's viewscreen above the tactical console.
//
// First the briefing (COMBATn.BMP with its PROCEED button), then the
// fight: the helm (by hand, or Close, Evade, Maintain distance, Full stop
// and Warp out), phasers in their firing arcs, photon torpedo salvos,
// shields by facing, damage to systems, waves of enemies, explosions, the
// crew's reports with subtitles. The enemies fly the original's own AI
// (0x4A7C91): they head for the Enterprise until within three quarters of
// phaser range, then hold their turn until they are out again; they fire
// every seven seconds, swerve off when about to collide, break off when
// badly hurt, and Soran keeps away while he has company. The TACTICS
// scripts (*.BIN) in COMBAT.DAT are left over from development: the game
// never attaches one to a ship.
//
// It ends won (every enemy gone, or Soran's ship beaten), lost (the
// Enterprise warped out) or with the Enterprise destroyed.
class SpaceCombat {
public:
    enum Result { kRunning, kWon, kLost, kDestroyed };
    static const int kWidth = 640, kHeight = 480;

    SpaceCombat(const ResourceManager& resources, Mixer* mixer, RandomSource& random);
    ~SpaceCombat();

    // Load combat n (1..25): its briefing, ships and console.
    bool load(int mission);
    void setSubtitles(bool on) { _subtitles = on; }
    // 0 Captain (more damage taken), 1 Lieutenant, 2 Ensign (less).
    void setDifficulty(int difficulty) { _difficulty = difficulty; }

    void update(uint32_t ms);
    void draw(Canvas& screen);
    void keyDown(const KeyState& key);
    void keyUp(const KeyState& key);
    void mouseDown(int x, int y);
    void mouseUp(int x, int y);
    // Esc: the options menu.
    bool takeOptionsRequest();

    // Over, once the closing speech and music have had their time.
    bool isFinished() const { return _finished; }
    Result getResult() const { return _result; }

    // Stop every sound (when the menu opens, say).
    void silence();

private:
    struct Ship;
    struct Torpedo {
        Vec3 position, velocity;
        int owner, target;
        uint32_t life;
        int damage;
        const SprAnimation* sprite;
    };
    struct Beam {
        int from, to;
        Vec3 end;
        uint32_t time;
        uint32_t colour;
        int damage, facingFrom;
        bool hit, applied;
    };
    struct Explosion {
        Vec3 position;
        double size;
        uint32_t time;
        const SprAnimation* sprite;
    };
    enum Helm { kClose = 0, kEvade = 1, kMaintain = 2, kFullStop = 3, kStandBy = 4, kWarpOut = 5, kMaintainStatus = 6 };
    enum Phase { kBriefing, kFight, kEnding };

    // Data.
    std::unique_ptr<SeekableReadStream> open(const std::string& name) const;
    const CombatImage* image(const std::string& name);
    const CombatImage* texture(const std::string& name);
    const SprAnimation* sprite(const std::string& name);
    const Model3D* model(const std::string& name);
    const std::vector<CombatMaterial>* materials(const std::string& name);
    const ShipClass* findClass(const std::string& token) const;
    void addShip(const MissionShip& line);

    // Simulation.
    void updateShip(Ship& ship, double dt);
    void steer(Ship& ship, const Vec3& point, double dt, bool away = false);
    void fly(Ship& ship, double dt);
    void updateAi(Ship& ship, double dt);
    // Head for a point well off the Enterprise (0x4AFF2A).
    void runOff(Ship& ship);
    bool isCollisionImminent(const Ship& a, const Ship& b) const;
    void updateHelm(double dt);
    void firePhasers(int shooter);
    void firePhotons(int shooter);
    void updateWeapons(uint32_t ms);
    // Which shield of target faces a point (0x54E278).
    int facing(const Ship& target, const Vec3& from) const;
    // The octant of a point seen from a ship: 4 starboard, 2 aft, 1 dorsal.
    int octant(const Ship& ship, const Vec3& point) const;
    void hit(int target, int facing, double damage, int aimed);
    void destroy(int ship);
    void nextTarget(bool announce);
    void checkEnd();
    void finish(Result result);
    double systemFraction(const Ship& ship, int system) const;
    bool isEnemyAlive(const Ship& ship) const;

    // Sound.
    void speak(int id, bool force = false);
    void playEffect(const std::string& name, int volume = 255);
    void playMusic(const std::string& name, bool loop);

    // Drawing.
    void drawView(Canvas& screen);
    void drawConsole(Canvas& screen);
    void drawImage(Canvas& screen, const CombatImage* image, int x, int y, const Rect* clip = nullptr);
    void drawText(Canvas& screen, const std::string& text, int x, int y, bool small = false);
    void drawBars(Canvas& screen, const Ship& ship, int x, int y);
    void drawRadar(Canvas& screen);

    const ResourceManager& _resources;
    Mixer* _mixer;
    RandomSource& _random;
    int _mission = 0;
    int _difficulty = 1;
    bool _subtitles = true;

    std::vector<ShipClass> _classes;
    std::map<std::string, std::unique_ptr<CombatImage>> _images;
    std::map<std::string, std::unique_ptr<SprAnimation>> _sprites;
    std::map<std::string, std::unique_ptr<Model3D>> _models;
    std::map<std::string, std::unique_ptr<std::vector<CombatMaterial>>> _materials;
    uint32_t _palette[256] = {};
    MetFont _font, _smallFont;
    RgbImage _briefing;
    RgbImage _proceed;
    int _proceedX = 535, _proceedY = 429;

    std::vector<Ship> _ships; // [0] the Enterprise
    std::vector<Torpedo> _torpedoes;
    std::vector<Beam> _beams;
    std::vector<Explosion> _explosions;
    std::vector<Vec3> _stars;
    CombatRenderer _renderer;

    Phase _phase = kBriefing;
    Result _result = kRunning;
    bool _finished = false;
    uint32_t _endTime = 0;
    uint32_t _time = 0;
    int _target = -1;
    int _aimedSystem = -1;
    Helm _helm = kStandBy;
    // The CONN line (hotspot 2), which calls up the helm menu over the Enterprise's panel.
    const Rect kConnLine = Rect(262, 440, 439, 461);
    bool _helmMenu = false;
    double _keepRange = 0;
    bool _enemyView = false;
    bool _phasersOnline = true, _photonsOnline = true;
    uint32_t _phaserLockout = 0, _photonLockout = 0;
    // Keys held: yaw, pitch, roll (-1..1) and speed changes.
    int _turnKeys[3] = {};
    int _speedKey = 0;
    int _pressed = -1;
    bool _optionsRequest = false;
    uint32_t _lifeSupportOut = 0;

    SoundHandle _speech, _music, _effect;
    std::string _subtitle;
    uint32_t _subtitleLeft = 0;
    std::string _message;
    uint32_t _messageLeft = 0;
    std::map<int, uint32_t> _spoken;
};

} // namespace reluva::stgenerations

