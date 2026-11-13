// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/combat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engines/stgenerations/audio.h"
#include "engines/stgenerations/debug.h"
#include "engines/stgenerations/formats/bitmap.h"

namespace reluva::stgenerations {

namespace {

// Not M_PI: MSVC only has it with _USE_MATH_DEFINES.
constexpr double kPi = 3.14159265358979323846;

const char *const kArchive = "COMBAT.DAT";
// The viewscreen: the console's hole (0x54F650).
const int kViewX = 52, kViewY = 10, kViewW = 532, kViewH = 231;
// Rotation at full deflection: 228 angle units a frame at about 15 frames a second.
const double kMaxTurn = 75 * kPi / 180;
// The AI's clock (0x47B83E) runs at 291.3 ticks a second (0x537260).
uint32_t ticks(int count) {
    return (uint32_t)(count * 1000 / 291.3 + 0.5);
}
// The AI fires every 2037 ticks, about seven seconds (0x4A7C91).
const uint32_t kAiFireMillis = ticks(0x7F5);
// Running off picks a new point at most every ten seconds (0x4AFF2A).
const uint32_t kFleeMillis = ticks(0xB5E);
// A swerve from a collision holds for five seconds (0x4A53A1).
const uint32_t kAvoidMillis = ticks(0x5AF);
const uint32_t kEndingMillis = 15000;
const uint32_t kLifeSupportMillis = 30000;
const int kStars = 1500;
const int kStreaks = 7;

// PROCEED's place on each COMBATn.BMP (table 0x548618).
const int kProceed[25][2] = {{535, 429}, {374, 436}, {372, 118}, {364, 432}, {532, 132}, {376, 438}, {516, 428},
                             {376, 434}, {527, 430}, {508, 421}, {375, 434}, {518, 430}, {380, 432}, {380, 432},
                             {370, 446}, {380, 432}, {380, 432}, {370, 446}, {370, 446}, {370, 446}, {370, 446},
                             {380, 432}, {380, 432}, {380, 432}, {373, 125}};

// Which shield faces each octant, by the number of shields (0x54E278).
const int kFacing[5][8] = {{0, 0, 0, 0, 0, 0, 0, 0},
                           {4, 4, 4, 4, 4, 4, 4, 4},
                           {3, 3, 4, 4, 3, 3, 4, 4},
                           {2, 2, 3, 3, 2, 2, 4, 4},
                           {1, 1, 2, 2, 3, 3, 4, 4}};
// The chance of blowing up for 4..8 systems destroyed (0x55009C).
const int kExplodeChance[9] = {0, 0, 0, 0, 10, 20, 40, 80, 85};

// The speech table's WAVs (0x54A980), by id.
std::string speechFile(int id) {
    if (id >= 0x6C && id <= 0x70)
        return text::format("EXPLO%d.WAV", id - 0x6B);
    if (id >= 0x83 && id <= 0x85)
        return text::format("CRUS%04d.WAV", 51 + id - 0x83);
    if (id == 0x86)
        return "DATA0103.WAV";
    if (id >= 0x87 && id <= 0x95)
        return text::format("DATA%04d.WAV", 139 + id - 0x87);
    if (id >= 0x96 && id <= 0xAC)
        return text::format("GEOR%04d.WAV", 143 + id - 0x96);
    if (id >= 0xAD && id <= 0xB7)
        return text::format("PICA%04d.WAV", 262 + id - 0xAD);
    if (id >= 0xB9 && id <= 0xCA)
        return text::format("RIKE%04d.WAV", 249 + id - 0xB9);
    if (id == 0xCB || id == 0xCC)
        return text::format("TROI%04d.WAV", 76 + id - 0xCB);
    if (id >= 0xCD && id <= 0x10F)
        return text::format("WORF%04d.WAV", 154 + id - 0xCD);
    if (id == 0x110)
        return "WORF0284.WAV";
    if (id == 0x111)
        return "AFUW0579.WAV";
    if (id == 0x112)
        return "AFUW0591.WAV";
    if (id >= 0x113 && id <= 0x130)
        return text::format("COMP%04d.WAV", 34 + id - 0x113);
    return std::string();
}

double clamp1(double v) {
    return std::max(-1.0, std::min(1.0, v));
}

uint32_t darken(uint32_t c, double f) {
    uint32_t r = (uint32_t)(((c >> 16) & 0xFF) * f);
    uint32_t g = (uint32_t)(((c >> 8) & 0xFF) * f);
    uint32_t b = (uint32_t)((c & 0xFF) * f);
    return 0xFF000000u | r << 16 | g << 8 | b;
}

} // namespace

struct SpaceCombat::Ship {
    std::string type;
    // The class's three letters, also the console pictures' (SENT_0...).
    std::string token;
    // On the console: the class's name, numbered from the second of a kind.
    std::string name;
    // The effects' prefix: E, K, R, C, P or _.
    char effects = '_';
    const ShipClass *cls = nullptr;
    const Model3D *model = nullptr, *lowModel = nullptr;
    const std::vector<CombatMaterial> *materials = nullptr, *lowMaterials = nullptr;
    double radius = 100;
    Vec3 position;
    Mat3 orientation;
    double speed = 0, setting = 0;
    double yawIn = 0, pitchIn = 0, rollIn = 0;
    double energy[ShipClass::kSystemCount] = {};
    double damage[ShipClass::kSystemCount] = {};
    double reload[4] = {};
    int loaded[4] = {};
    int stock = 0;
    bool alive = true, hidden = false, gone = false;
    int wave = 1;
    // 1 Soran's own ship, 2 the Q'urash.
    int soran = 0;
    int aimed = -1;
    int number = 1;
    uint32_t aiTimer = 3000;
    // Flying at waypoint (+0x188 with flag 0x20), or holding the last turn.
    bool hasWaypoint = false;
    Vec3 waypoint;
    // When the next point to run to may be picked (+0xE9C), and when a swerve ends (+0xEB0).
    uint32_t fleeAt = 0, avoidUntil = 0;
    int destroyedReported = 0;
    // The model's largest extent (+0x30) and the mean of its three, which sizes the shields.
    double size = 0, meanSize = 0;
    // The shield hit's dome (+0xE60, SHIELD_c.3DV) and the shield facing sphere (+0x524, SHLD8k_c.3DV).
    const Model3D *shieldModel = nullptr, *sphereModel = nullptr;
    const std::vector<CombatMaterial> *shieldMaterials = nullptr, *sphereMaterials = nullptr;
    // A shield hit showing (+0xE64): how long since, and towards what.
    bool flashing = false;
    uint32_t flashTime = 0;
    Vec3 flashFrom;
};

SpaceCombat::SpaceCombat(const ResourceManager &resources, Mixer *mixer, RandomSource &random)
    : _resources(resources), _mixer(mixer), _random(random) {
    _renderer.setViewport(kViewW, kViewH, 60);
}

SpaceCombat::~SpaceCombat() {
    silence();
}

void SpaceCombat::silence() {
    if (!_mixer)
        return;
    _mixer->stopHandle(_speech);
    _mixer->stopHandle(_music);
    _mixer->stopHandle(_effect);
}

// Data

std::unique_ptr<SeekableReadStream> SpaceCombat::open(const std::string &name) const {
    return _resources.open({kArchive, name});
}

const CombatImage *SpaceCombat::image(const std::string &name) {
    std::string key = text::toUpper(name);
    auto it = _images.find(key);
    if (it != _images.end())
        return it->second.get();
    std::unique_ptr<CombatImage> result(new CombatImage());
    std::unique_ptr<SeekableReadStream> stream = open(key);
    if (!stream || !result->loadRas(*stream))
        result.reset();
    const CombatImage *found = result.get();
    _images[key] = std::move(result);
    return found;
}

const CombatImage *SpaceCombat::texture(const std::string &name) {
    std::string key = "tex:" + text::toUpper(name);
    auto it = _images.find(key);
    if (it != _images.end())
        return it->second.get();
    std::unique_ptr<CombatImage> result(new CombatImage());
    std::unique_ptr<SeekableReadStream> stream = open(name);
    bool ok = stream && (text::hasSuffixIgnoreCase(name, ".lbm") ? result->loadLbm(*stream) : result->loadGif(*stream));
    if (!ok)
        result.reset();
    const CombatImage *found = result.get();
    _images[key] = std::move(result);
    return found;
}

const SprAnimation *SpaceCombat::sprite(const std::string &name) {
    std::string key = text::toUpper(name);
    auto it = _sprites.find(key);
    if (it != _sprites.end())
        return it->second.get();
    std::unique_ptr<SprAnimation> result(new SprAnimation());
    std::unique_ptr<SeekableReadStream> stream = open(key);
    if (!stream || !result->load(*stream))
        result.reset();
    const SprAnimation *found = result.get();
    _sprites[key] = std::move(result);
    return found;
}

const Model3D *SpaceCombat::model(const std::string &name) {
    std::string key = text::toUpper(name);
    auto it = _models.find(key);
    if (it != _models.end())
        return it->second.get();
    std::unique_ptr<Model3D> result(new Model3D());
    std::unique_ptr<SeekableReadStream> stream = open(key);
    if (!stream || !result->load(*stream))
        result.reset();
    const Model3D *found = result.get();
    _models[key] = std::move(result);
    return found;
}

const std::vector<CombatMaterial> *SpaceCombat::materials(const std::string &name) {
    std::string key = text::toUpper(name);
    auto it = _materials.find(key);
    if (it != _materials.end())
        return it->second.get();
    std::unique_ptr<std::vector<CombatMaterial>> result(new std::vector<CombatMaterial>());
    std::unique_ptr<SeekableReadStream> stream = open(key);
    if (!stream || !CombatMaterial::loadList(*stream, *result))
        result.reset();
    const std::vector<CombatMaterial> *found = result.get();
    _materials[key] = std::move(result);
    return found;
}

const ShipClass *SpaceCombat::findClass(const std::string &token) const {
    for (const ShipClass &c : _classes) {
        if (c.token == token)
            return &c;
    }
    return _classes.empty() ? nullptr : &_classes[0];
}

bool SpaceCombat::load(int mission) {
    _mission = mission;
    std::unique_ptr<SeekableReadStream> systems = open("SYSTEMS.DAT");
    if (!systems)
        return false;
    std::vector<uint8_t> text = systems->readAll();
    if (!ShipClass::loadAll(std::string(text.begin(), text.end()), _classes))
        return false;
    std::unique_ptr<SeekableReadStream> scenario = open(text::format("SC%d.MIS", mission));
    if (!scenario)
        return false;
    text = scenario->readAll();
    std::vector<MissionShip> lines;
    if (!loadCombatMission(std::string(text.begin(), text.end()), lines))
        return false;
    std::unique_ptr<SeekableReadStream> pal = open("STANDARD.PAL");
    if (pal)
        loadStandardPalette(*pal, _palette);
    std::unique_ptr<SeekableReadStream> font = open("FONTSCAN.MET");
    if (font)
        _font.load(*font);
    font = open("FONTLIT.MET");
    if (font)
        _smallFont.load(*font);

    // The Enterprise first.
    _ships.clear();
    for (const MissionShip &line : lines) {
        if (line.type == "player")
            addShip(line);
    }
    if (_ships.empty())
        return false;
    for (const MissionShip &line : lines) {
        if (line.type != "player")
            addShip(line);
    }

    // The briefing.
    BitmapDecoder decoder;
    std::unique_ptr<SeekableReadStream> brief = open(text::format("COMBAT%d.BMP", mission));
    if (brief && decoder.loadStream(*brief) && decoder.hasPalette())
        _briefing.load(decoder);
    BitmapDecoder proceed;
    std::unique_ptr<SeekableReadStream> button = open("PROCEED.BMP");
    if (button && proceed.loadStream(*button) && proceed.hasPalette())
        _proceed.load(proceed, 2);
    if (mission >= 1 && mission <= 25) {
        _proceedX = kProceed[mission - 1][0];
        _proceedY = kProceed[mission - 1][1];
    }
    _phase = _briefing.isLoaded() ? kBriefing : kFight;
    if (_phase == kBriefing) {
        ResourceRef ref = {"OPTIONS.DAT", text::format("CLOOP%d.WAV", 1 + (int)_random.getRandomNumber(3))};
        std::unique_ptr<SoundStream> stream = _resources.loadSound(ref);
        if (stream && _mixer)
            _mixer->playStream(Mixer::kMusicSoundType, &_music, makeLooping(std::move(stream), 0));
    } else {
        playMusic(text::format("SCLOOP%d.WAV", 1 + (int)_random.getRandomNumber(3)), true);
    }

    for (int i = 0; i < kStars; i++) {
        Vec3 star((int)_random.getRandomNumber(2000) - 1000, (int)_random.getRandomNumber(2000) - 1000,
                  (int)_random.getRandomNumber(2000) - 1000);
        _stars.push_back(star.normalized());
        _starTypes.push_back((int)_random.getRandomNumber(12));
    }
    for (int i = 0; i < kStreaks; i++)
        _streaks.push_back(newStreak());
    // 0x49461C: the nearest ship is the first target, and the Enterprise
    // starts facing it, level; the mission file's heading for it is unused.
    Ship &e = _ships[0];
    _target = -1;
    double nearest = 0;
    for (size_t i = 1; i < _ships.size(); i++) {
        double distance = (_ships[i].position - e.position).length();
        if (isEnemyAlive(_ships[i]) && (_target < 0 || distance < nearest)) {
            _target = (int)i;
            nearest = distance;
        }
    }
    if (_target > 0 && nearest > 0)
        e.orientation = Mat3::lookAlong(_ships[_target].position - e.position);
    return true;
}

void SpaceCombat::addShip(const MissionShip &line) {
    // 0x4A4DA2: the types, their models and classes.
    // The console's names are the class names of 0x54E660; it has none of
    // its own for the Garidian ship and the starbase.
    struct Type {
        const char *name, *model, *token;
        char effects;
        const char *display;
    };
    static const Type kTypes[] = {
        {"player", "ENTERPRS", "ent", 'E', "NCC-1701D"}, {"sm_klingon", "KLINGON", "bop", 'K', "BIRD OF PREY"},
        {"klingon", "VORCHA", "vor", 'K', "VORCHA"},     {"romulan", "ROMULAN", "war", 'R', "WARBIRD"},
        {"chodak", "CHODAK", "cho", 'C', "DREADNOUGHT"}, {"garidian", "GARIDIAN", "war", 'R', "WARBIRD"},
        {"probe", "PROBE", "pro", 'P', "PROBE"},         {"starbase", "STARBASE", "pro", '_', "STARBASE"}};
    const Type *type = nullptr;
    for (const Type &t : kTypes) {
        if (line.type == t.name)
            type = &t;
    }
    if (!type) {
        log::warning("Space combat: no ship type %s", line.type.c_str());
        return;
    }
    Ship ship;
    ship.type = line.type;
    ship.token = type->token;
    ship.name = type->display;
    ship.effects = type->effects;
    ship.cls = findClass(type->token);
    ship.model = model(std::string(type->model) + ".3DV");
    ship.materials = materials(std::string(type->model) + ".MTL");
    ship.lowModel = model(std::string(type->model) + ".3DR");
    ship.lowMaterials = materials(std::string(type->model) + ".MTR");
    if (ship.model) {
        ship.radius = std::max(50.0f, ship.model->radius);
        const float *e = ship.model->extent;
        ship.size = std::max(e[0], std::max(e[1], e[2]));
        ship.meanSize = (e[0] + e[1] + e[2]) / 3;
    }
    // Its shields (0x4A48DD): a copy of each model for every ship, named by
    // its place in the mission, 1-9 then a-f.
    int slot = (int)_ships.size() + 1;
    char c = slot <= 9 ? (char)('0' + slot) : (char)('a' + slot - 10);
    ship.shieldModel = model(text::format("SHIELD_%c.3DV", c));
    ship.shieldMaterials = materials(text::format("SHIELD_%c.MTL", c));
    if (ship.cls && ship.cls->shields > 0) {
        ship.sphereModel = model(text::format("SHLD8%d_%c.3DV", ship.cls->shields == 4 ? 4 : 0, c));
        ship.sphereMaterials = materials(text::format("SHLD8%d_0.MTL", ship.cls->shields));
    }
    // The mission's (x, y, z) is east, north and altitude (0x4A36A4).
    ship.position = Vec3(line.x, line.z, line.y);
    // Headings turn from +y towards -x (90 faces -x): that way 66 of the 68
    // enemies the missions start with face the Enterprise.
    ship.orientation = Mat3::fromYawPitch(-line.yaw * kPi / 180, line.pitch * kPi / 180);
    ship.wave = line.wave;
    ship.hidden = line.wave > 1;
    ship.soran = line.ai == "soran" || line.ai == "soran9" ? 1 : line.ai == "quarash" ? 2 : 0;
    if (line.ai.compare(0, 9, "standard-") == 0) {
        int system = std::atoi(line.ai.c_str() + 9);
        ship.aimed = system >= 5 && system <= 12 ? system : -1;
    }
    if (ship.cls) {
        for (int s = 0; s < ShipClass::kSystemCount; s++)
            ship.energy[s] = std::max(0, ship.cls->systems[s].max);
        for (int t = 0; t < 4; t++)
            ship.loaded[t] = std::max(0, ship.cls->systems[ShipClass::kPhoton0 + t].max);
        ship.stock = ship.cls->torpedoes;
    }
    ship.aiTimer = 2000 + _random.getRandomNumber(5000);
    for (const Ship &other : _ships) {
        if (other.type == ship.type)
            ship.number++;
    }
    // The second of a kind and on are numbered.
    if (ship.number > 1)
        ship.name += text::format(" %d", ship.number);
    _ships.push_back(ship);
}

double SpaceCombat::systemFraction(const Ship &ship, int system) const {
    if (!ship.cls || ship.cls->systems[system].max <= 0)
        return 0;
    double max = ship.cls->systems[system].max;
    return std::max(0.0, (max - ship.damage[system]) / max);
}

bool SpaceCombat::isEnemyAlive(const Ship &ship) const {
    return ship.alive && !ship.hidden && !ship.gone;
}

// Simulation

void SpaceCombat::update(uint32_t ms) {
    ms = std::min<uint32_t>(ms, 100);
    _time += ms;
    if (_subtitleLeft)
        _subtitleLeft = ms >= _subtitleLeft ? 0 : _subtitleLeft - ms;
    if (_mixer && !_mixer->isSoundHandleActive(_speech) && !_subtitleLeft)
        _subtitle.clear();
    if (_messageLeft)
        _messageLeft = ms >= _messageLeft ? 0 : _messageLeft - ms;

    if (_phase == kBriefing)
        return;
    double dt = ms / 1000.0;
    if (_phase == kEnding) {
        _endTime += ms;
        bool quiet = !_mixer || (!_mixer->isSoundHandleActive(_speech) && !_mixer->isSoundHandleActive(_music));
        if (_endTime >= kEndingMillis || (quiet && _endTime >= 3000))
            _finished = true;
    }

    _phaserLockout = _phaserLockout > ms ? _phaserLockout - ms : 0;
    _photonLockout = _photonLockout > ms ? _photonLockout - ms : 0;
    if (_phase == kFight)
        updateHelm(dt);
    for (int i = 0; i < (int)_ships.size(); i++) {
        Ship &ship = _ships[i];
        if (!ship.alive || ship.hidden || ship.gone)
            continue;
        if (i > 0 && _phase == kFight)
            updateAi(ship, dt);
        updateShip(ship, dt);
    }
    updateWeapons(ms);
    for (Explosion &e : _explosions)
        e.time += ms;
    // Shield hits run their animation, then end (0x497D2E); the
    // Enterprise's at once, as it isn't seen.
    for (size_t i = 0; i < _ships.size(); i++) {
        Ship &ship = _ships[i];
        if (!ship.flashing)
            continue;
        ship.flashTime += ms;
        const SprAnimation *spr = sprite(text::format("%c_SHIELD.SPR", ship.effects == 'P' ? '_' : ship.effects));
        if (i == 0 || !spr || ship.flashTime >= spr->frames.size() * spr->frameMillis)
            ship.flashing = false;
    }
    // The streaming stars grow out from the middle with the speed, at the
    // original's rate a frame taken as twenty frames a second.
    const Ship &enterprise = _ships[0];
    if (enterprise.cls && enterprise.cls->maxSpeed) {
        double growth = std::pow(1 + enterprise.speed / enterprise.cls->maxSpeed * 0.075, dt * 20);
        for (Streak &streak : _streaks)
            streak.scale *= growth;
    }
    _explosions.erase(std::remove_if(_explosions.begin(), _explosions.end(),
                                     [](const Explosion &e) {
                                         return !e.sprite || e.time >= e.sprite->frames.size() * e.sprite->frameMillis;
                                     }),
                      _explosions.end());
    if (_phase == kFight)
        checkEnd();
}

void SpaceCombat::updateShip(Ship &ship, double dt) {
    const ShipClass *cls = ship.cls;
    if (!cls)
        return;
    // Shields and phaser banks recharge; the tubes reload from the stock.
    double power = std::max(0.25, systemFraction(ship, ShipClass::kPower));
    for (int s = ShipClass::kShield1; s <= ShipClass::kShield1 + 3; s++) {
        double cap = std::max(0.0, cls->systems[s].max - ship.damage[s]);
        ship.energy[s] = std::min(cap, ship.energy[s] + cls->systems[s].charge * dt * power * 0.3);
    }
    double weapons = systemFraction(ship, ShipClass::kWeapons);
    for (int s = ShipClass::kPhaser0; s < ShipClass::kPhoton0; s++) {
        double cap = std::max(0.0, cls->systems[s].max - ship.damage[s]);
        ship.energy[s] = std::min(cap, ship.energy[s] + cls->systems[s].charge * dt * weapons * power * 0.15);
    }
    double life = systemFraction(ship, ShipClass::kLifeSupport);
    for (int t = 0; t < 4; t++) {
        int max = cls->systems[ShipClass::kPhoton0 + t].max;
        if (max <= 0 || ship.loaded[t] >= max || ship.stock <= 0)
            continue;
        // 8 - 7 x life support (0x4A97B7): slower with the crew in trouble.
        ship.reload[t] += dt;
        if (ship.reload[t] >= 1.5 + 6.5 * (1 - life)) {
            ship.reload[t] = 0;
            ship.loaded[t]++;
            ship.stock--;
        }
    }
    // Damage control mends what isn't destroyed.
    double repair = systemFraction(ship, ShipClass::kDamageControl);
    for (int s = 1; s < ShipClass::kSystemCount; s++) {
        int max = cls->systems[s].max;
        if (max > 0 && ship.damage[s] > 0 && ship.damage[s] < max)
            ship.damage[s] = std::max(0.0, ship.damage[s] - cls->systems[s].repair * dt * repair * 0.01);
    }
    fly(ship, dt);
}

void SpaceCombat::fly(Ship &ship, double dt) {
    // Turning slows with speed (table 0x54F150: 1.0 at rest to 0.21 flat out).
    double maxSpeed = ship.cls ? ship.cls->maxSpeed : 500;
    double fraction = std::min(1.0, std::fabs(ship.speed) / std::max(1.0, maxSpeed));
    double rate = kMaxTurn * std::min(1.0, (ship.cls ? ship.cls->agility : 50) / 100.0) * (1 - 0.79 * fraction);
    ship.orientation.rotate(ship.yawIn * rate * dt, ship.pitchIn * rate * dt, ship.rollIn * rate * dt);
    double engines = systemFraction(ship, ShipClass::kEngines);
    double target = ship.setting / 100 * maxSpeed * engines;
    ship.speed += (target - ship.speed) * std::min(1.0, dt * 1.2);
    ship.position = ship.position + ship.orientation.forward * (ship.speed * dt);
}

void SpaceCombat::steer(Ship &ship, const Vec3 &point, double dt, bool away) {
    Vec3 local = ship.orientation.inverse(point - ship.position);
    if (away)
        local = local * -1;
    double yaw = std::atan2(local.x, local.z);
    double pitch = std::atan2(local.y, std::hypot(local.x, local.z));
    ship.yawIn = clamp1(yaw * 2);
    ship.pitchIn = clamp1(pitch * 2);
    // Keep the wings level.
    ship.rollIn = clamp1(ship.orientation.right.y * 2);
    (void)dt;
}

void SpaceCombat::updateAi(Ship &ship, double dt) {
    // The enemies' own AI (0x4A7C91), with the steering of 0x4A53A1. The
    // TACTICS scripts (*.BIN) in COMBAT.DAT are never attached to a ship.
    const Ship &enterprise = _ships[0];
    if (!enterprise.alive || !ship.cls)
        return;
    double distance = (enterprise.position - ship.position).length();
    uint32_t ms = (uint32_t)(dt * 1000);
    int index = (int)(&ship - &_ships[0]);

    // Every seven seconds: phasers in range, and the tube facing the
    // Enterprise if it has torpedoes and is in range.
    ship.aiTimer = ship.aiTimer > ms ? ship.aiTimer - ms : 0;
    if (ship.aiTimer == 0) {
        ship.aiTimer = kAiFireMillis;
        if (distance < ship.cls->phaserRange)
            firePhasers(index);
        firePhotons(index);
    }

    // About to collide: swerve to a point beside and below the Enterprise.
    if (isCollisionImminent(ship, enterprise)) {
        if (ship.avoidUntil < _time) {
            ship.waypoint = enterprise.position + Vec3(_random.getRandomNumber(1) ? 1100 : -1100, -1100,
                                                       _random.getRandomNumber(1) ? 1100 : -1100);
            ship.hasWaypoint = true;
            ship.avoidUntil = _time + kAvoidMillis;
        }
    } else if (ship.avoidUntil) {
        ship.hasWaypoint = false;
        ship.avoidUntil = 0;
    }

    if (ship.avoidUntil <= _time) {
        // Soran keeps off while any other ship is left in the fight, or
        // still to come (the ship list, 0x4A2450).
        int others = 0;
        for (const Ship &s : _ships)
            others += &s != &ship && &s != &enterprise && s.alive && !s.gone;
        bool keepsAway = ship.soran && others > 0;
        if (distance >= 0.75 * ship.cls->phaserRange) {
            if (keepsAway) {
                runOff(ship);
            } else {
                ship.waypoint = enterprise.position;
                ship.hasWaypoint = true;
            }
        } else if (keepsAway) {
            runOff(ship);
        }
    }

    // Full ahead at the waypoint; with none, the last turn holds.
    ship.setting = 100;
    if (ship.hasWaypoint)
        steer(ship, ship.waypoint, dt);
}

void SpaceCombat::runOff(Ship &ship) {
    // 0x4AFF2A: two phaser ranges off the Enterprise to either side, and
    // below it.
    if (_time < ship.fleeAt)
        return;
    const Ship &enterprise = _ships[0];
    double r = 2.0 * enterprise.cls->phaserRange;
    ship.fleeAt = _time + kFleeMillis;
    ship.waypoint =
        enterprise.position + Vec3(_random.getRandomNumber(1) ? r : -r, -r, _random.getRandomNumber(1) ? r : -r);
    ship.hasWaypoint = true;
}

bool SpaceCombat::isCollisionImminent(const Ship &a, const Ship &b) const {
    // 0x4A7880: within 1000, moving or close, and too near now or a second
    // from now (speeds are per second).
    double distance = (a.position - b.position).length();
    if (distance > 1000)
        return false;
    Vec3 va = a.orientation.forward * a.speed;
    Vec3 vb = b.orientation.forward * b.speed;
    if (std::fabs(a.speed) < 2 && std::fabs(b.speed) < 2 && distance > a.radius + b.radius + 500)
        return false;
    double clearance = std::max(a.radius, b.radius) + std::max(va.length(), vb.length()) * 8;
    if (distance < clearance || ((a.position + va) - (b.position + vb)).length() < clearance)
        return true;
    return (va - vb).length() * 4 > distance;
}

void SpaceCombat::updateHelm(double dt) {
    Ship &e = _ships[0];
    if (!e.alive)
        return;
    const Ship *target =
        _target >= 0 && _target < (int)_ships.size() && isEnemyAlive(_ships[_target]) ? &_ships[_target] : nullptr;
    switch (_helm) {
    case kStandBy:
    case kMaintainStatus: {
        // By hand: the inputs follow the keys, and fall back when let go.
        double *inputs[3] = {&e.yawIn, &e.pitchIn, &e.rollIn};
        for (int i = 0; i < 3; i++) {
            double want = _steering && i < 2 ? _mouseTurn[i] : _turnKeys[i];
            double step = dt * (want != 0 ? 2.0 : 3.0);
            *inputs[i] += clamp1((want - *inputs[i]) / std::max(step, 1e-6)) * step;
            *inputs[i] = clamp1(*inputs[i]);
        }
        if (_speedKey)
            e.setting = std::max(-100.0, std::min(100.0, e.setting + _speedKey * 60 * dt));
        break;
    }
    case kClose:
        if (target) {
            steer(e, target->position, dt);
            double d = (target->position - e.position).length();
            e.setting = d > e.cls->closeRadius ? 100 : 30;
        }
        break;
    case kEvade: {
        Vec3 centre;
        int n = 0;
        for (size_t i = 1; i < _ships.size(); i++) {
            if (isEnemyAlive(_ships[i])) {
                centre = centre + _ships[i].position;
                n++;
            }
        }
        if (n)
            steer(e, centre * (1.0 / n), dt, true);
        e.setting = 100;
        break;
    }
    case kMaintain:
        if (target) {
            steer(e, target->position, dt);
            double d = (target->position - e.position).length();
            e.setting = std::max(-50.0, std::min(100.0, (d - _keepRange) / 50));
        }
        break;
    case kFullStop:
        e.yawIn = e.pitchIn = e.rollIn = 0;
        e.setting = 0;
        break;
    case kWarpOut:
        break;
    }
}

int SpaceCombat::octant(const Ship &ship, const Vec3 &point) const {
    Vec3 local = ship.orientation.inverse(point - ship.position);
    return (local.x > 0 ? 4 : 0) | (local.z < 0 ? 2 : 0) | (local.y > 0 ? 1 : 0);
}

int SpaceCombat::facing(const Ship &target, const Vec3 &from) const {
    int shields = target.cls ? std::max(0, std::min(4, target.cls->shields)) : 0;
    return kFacing[shields][octant(target, from)];
}

void SpaceCombat::firePhasers(int shooter) {
    Ship &ship = _ships[shooter];
    int targetIndex = shooter == 0 ? _target : 0;
    if (!ship.cls || targetIndex < 0 || targetIndex >= (int)_ships.size() || !isEnemyAlive(_ships[targetIndex]) ||
        !_ships[targetIndex].alive)
        return;
    const Ship &target = _ships[targetIndex];
    double distance = (target.position - ship.position).length();
    if (distance > ship.cls->phaserRange) {
        if (shooter == 0)
            speak(0x112);
        return;
    }
    int arc = octant(ship, target.position);
    double computer = std::max(0.5, systemFraction(ship, ShipClass::kComputer));
    bool fired = false;
    for (int s = ShipClass::kPhaser0; s < ShipClass::kPhoton0; s++) {
        const ShipClass::SystemInfo &bank = ship.cls->systems[s];
        if (bank.max <= 0 || !(bank.arcs & (1 << arc)) || ship.energy[s] < 1)
            continue;
        // The hit roll (0x49BE85).
        double range = ship.cls->phaserRange;
        double score = computer * ((range - distance + target.radius) * 0.75 / range + 0.25);
        bool hit = score >= 0.11 * (1 + _random.getRandomNumber(1000) / 1000.0);
        Beam beam;
        beam.from = shooter;
        beam.to = targetIndex;
        beam.end = target.position;
        if (!hit) {
            Vec3 off((int)_random.getRandomNumber(2000) - 1000, (int)_random.getRandomNumber(2000) - 1000,
                     (int)_random.getRandomNumber(2000) - 1000);
            beam.end = target.position + off.normalized() * (distance * 0.3 + target.radius * 2);
        }
        beam.time = 0;
        beam.colour = ship.effects == 'E'   ? 0xFFFF7020u
                      : ship.effects == 'R' ? 0xFF40FF60u
                      : ship.effects == 'C' ? 0xFF60A0FFu
                                            : 0xFFFF3030u;
        beam.damage = (int)ship.energy[s];
        beam.hit = hit;
        beam.applied = false;
        beam.facingFrom = 0;
        ship.energy[s] = 0;
        _beams.push_back(beam);
        fired = true;
    }
    if (fired) {
        playEffect(
            text::format("%c_PHASER.WAV", ship.effects == 'P' ? '_' : ship.effects),
            (int)std::max(40.0, 255 * (1 - std::min(1.0, (ship.position - _ships[0].position).length() / 8500))));
    }
}

void SpaceCombat::firePhotons(int shooter) {
    Ship &ship = _ships[shooter];
    int targetIndex = shooter == 0 ? _target : 0;
    if (!ship.cls || targetIndex < 0 || targetIndex >= (int)_ships.size() || !isEnemyAlive(_ships[targetIndex]))
        return;
    const Ship &target = _ships[targetIndex];
    if ((target.position - ship.position).length() > ship.cls->photonRange) {
        if (shooter == 0)
            speak(0x112);
        return;
    }
    int arc = octant(ship, target.position);
    // The tube facing the target fires all it holds (0x4999AB).
    for (int t = 0; t < 4; t++) {
        int s = ShipClass::kPhoton0 + t;
        const ShipClass::SystemInfo &tube = ship.cls->systems[s];
        if (tube.max <= 0 || !(tube.arcs & (1 << arc)) || ship.damage[s] >= tube.max)
            continue;
        if (ship.loaded[t] <= 0) {
            if (shooter == 0)
                speak(0x111);
            return;
        }
        int count = ship.loaded[t];
        ship.loaded[t] = 0;
        Vec3 aim = (target.position - ship.position).normalized();
        for (int i = 0; i < count; i++) {
            Torpedo torpedo;
            double spread = (i - (count - 1) / 2.0) * 0.08;
            Vec3 dir = (aim + ship.orientation.right * spread).normalized();
            torpedo.position = ship.position + dir * ship.radius;
            torpedo.velocity = dir * std::max(2000.0, std::fabs(ship.speed) + 2000);
            torpedo.owner = shooter;
            torpedo.target = targetIndex;
            torpedo.life = 20000;
            torpedo.damage = ship.cls->photonDamage;
            torpedo.sprite = sprite(text::format("%c_PHOTON.SPR", ship.effects == 'P' ? '_' : ship.effects));
            if (!torpedo.sprite)
                torpedo.sprite = sprite("__PHOTON.SPR");
            _torpedoes.push_back(torpedo);
        }
        playEffect(text::format("%c_PHOTON.WAV", ship.effects == 'P' ? '_' : ship.effects));
        return;
    }
}

void SpaceCombat::updateWeapons(uint32_t ms) {
    // Beams hurt as their animation reaches its fourth frame.
    for (Beam &beam : _beams) {
        beam.time += ms;
        if (!beam.applied && beam.time >= 300) {
            beam.applied = true;
            if (beam.hit && beam.to < (int)_ships.size() && _ships[beam.to].alive && !_ships[beam.to].gone) {
                const Ship &from = _ships[beam.from];
                int aimed = beam.from == 0 ? _aimedSystem : from.aimed;
                hit(beam.to, facing(_ships[beam.to], from.position), beam.damage, aimed, from.position);
            }
        }
    }
    _beams.erase(std::remove_if(_beams.begin(), _beams.end(), [](const Beam &b) { return b.time >= 600; }),
                 _beams.end());

    double dt = ms / 1000.0;
    for (Torpedo &t : _torpedoes) {
        t.life = t.life > ms ? t.life - ms : 0;
        if (t.target >= 0 && t.target < (int)_ships.size() && _ships[t.target].alive && !_ships[t.target].gone) {
            const Ship &target = _ships[t.target];
            Vec3 to = target.position - t.position;
            double speed = std::max(1750.0, t.velocity.length());
            // Homing, with a limited turn.
            Vec3 want = to.normalized() * speed;
            t.velocity = (t.velocity + (want - t.velocity) * std::min(1.0, dt * 2.5)).normalized() * speed;
            if (to.length() < target.radius + speed * dt) {
                int owner = t.owner;
                // The torpedo is gone; the shield's flash is all that shows (0x49A3D1).
                hit(t.target, facing(target, t.position), t.damage, owner == 0 ? _aimedSystem : _ships[owner].aimed,
                    t.position);
                t.life = 0;
                continue;
            }
        }
        t.position = t.position + t.velocity * dt;
    }
    _torpedoes.erase(std::remove_if(_torpedoes.begin(), _torpedoes.end(), [](const Torpedo &t) { return t.life == 0; }),
                     _torpedoes.end());
}

void SpaceCombat::shieldHit(int index, const Vec3 &from, int shield) {
    // 0x497B99: the shield's dome takes the ship's ?_SHIELD.SPR as its
    // texture and shows towards what hit it until the animation ends.
    Ship &ship = _ships[index];
    if (!ship.shieldModel)
        return;
    ship.flashing = true;
    ship.flashTime = 0;
    ship.flashFrom = from;
    if (index == 0) {
        jolt(index, shield);
        playEffect(text::format("SHIELDS%d.WAV", 1 + (int)_random.getRandomNumber(2)));
    } else {
        playEffect("KSHIELD1.WAV");
    }
}

void SpaceCombat::jolt(int index, int shield) {
    // 0x497B00, for the Enterprise only: the screen shakes for 0x91 ticks;
    // a shield more than four tenths gone also dims it, for longer the
    // worse it is (0x47BC14).
    if (index != 0 || shield < 1 || shield > 4)
        return;
    const Ship &ship = _ships[0];
    _shakeUntil = _time + kShakeTicks * 1000 / kTicksPerSecond;
    int max = ship.cls ? ship.cls->systems[shield].max : 0;
    if (max <= 0)
        return;
    int tenths = (int)(ship.damage[shield] * 10) / max;
    if (tenths > 4) {
        _dimLength = (uint32_t)((tenths * 4 - 16) * 9 * 1000 / kTicksPerSecond);
        _dimStart = _time;
        if (!_dimLevel)
            _dimLevel = -127;
    }
}

void SpaceCombat::drawJolt(Canvas &screen) {
    // Dimming: a level running from -127 to 127 over its time; the screen
    // is a quarter as bright in the middle half of it, half at the ends.
    if (_dimLevel) {
        int half = std::max<int>(1, _dimLength / 2);
        int level = (int)(((int)(_time - _dimStart) - half) * 127 / half);
        if (level == 0)
            level = 1;
        _dimLevel = std::max(_dimLevel, level);
        if (_dimLevel > 127) {
            _dimLevel = 0;
        } else {
            int shift = 2 - std::abs(_dimLevel) * 2 / 127;
            if (shift > 0) {
                uint32_t mask = (0xFFu >> shift) * 0x010101u;
                for (int y = 0; y < screen.h; y++) {
                    uint32_t *row = (uint32_t *)screen.getBasePtr(0, y);
                    for (int x = 0; x < screen.w; x++)
                        row[x] = 0xFF000000u | ((row[x] >> shift) & mask);
                }
            }
        }
    }
    // Shaking: every other frame the picture moves right by up to five and
    // down by up to three pixels (0x4995D8's random 0..n).
    if (_time < _shakeUntil) {
        _shakeFrame = !_shakeFrame;
        int dx = _shakeFrame ? 0 : (int)_random.getRandomNumber(5);
        int dy = _shakeFrame ? 0 : (int)_random.getRandomNumber(3);
        if (dx || dy) {
            for (int y = screen.h - 1; y >= 0; y--) {
                uint32_t *row = (uint32_t *)screen.getBasePtr(0, y);
                if (y < dy) {
                    std::fill(row, row + screen.w, 0xFF000000u);
                    continue;
                }
                const uint32_t *src = (const uint32_t *)screen.getBasePtr(0, y - dy);
                memmove(row + dx, src, (screen.w - dx) * sizeof(uint32_t));
                std::fill(row, row + dx, 0xFF000000u);
            }
        }
    }
}

void SpaceCombat::hit(int index, int shield, double amount, int aimed, const Vec3 &from) {
    Ship &ship = _ships[index];
    if (!ship.alive || !ship.cls || _phase != kFight)
        return;
    const ShipClass &cls = *ship.cls;
    // Before the damage (0x49A3D1, 0x49B6C5): a shield still up flashes,
    // unless it is already; otherwise the Enterprise only shakes.
    bool up = shield >= 1 && shield <= 4 && ship.damage[shield] != cls.systems[shield].max;
    if (up && !ship.flashing)
        shieldHit(index, from, shield);
    else
        jolt(index, shield);
    // The difficulty weighs on the Enterprise (0x4B08C3).
    if (index == 0)
        amount *= _difficulty == 0 ? 1.2 : _difficulty == 2 ? 0.85 : 1.0;
    if (ship.token == "cho")
        amount /= 2;

    // The shield takes it, or some of it leaks in (0x4B0103).
    double leak = amount;
    if (shield >= 1 && shield <= 4 && cls.systems[shield].max > 0 && ship.damage[shield] < cls.systems[shield].max) {
        double energy = ship.energy[shield];
        double part;
        if (energy < amount) {
            part = energy;
            leak = amount - energy;
        } else {
            int column = std::max(0, std::min(12, (int)((120 - energy) / 10)));
            if ((int)_random.getRandomNumber(99) < cls.bleed[shield][column]) {
                leak = amount * 2 / 3;
                part = amount / 3;
            } else {
                leak = 0;
                part = amount;
            }
        }
        ship.energy[shield] = std::max(0.0, ship.energy[shield] - part);
        bool wasUp = ship.damage[shield] < cls.systems[shield].max * 0.5;
        ship.damage[shield] += part * 0.5;
        bool down = ship.damage[shield] >= cls.systems[shield].max;
        if (down)
            ship.energy[shield] = 0;
        if (index == 0) {
            static const int kHalf[5] = {0, 0xBA, 0xBE, 0xC0, 0xBC};
            static const int kGone[5] = {0, 0xBB, 0xBF, 0xC1, 0xBD};
            if (down)
                speak(kGone[shield]);
            else if (wasUp && ship.damage[shield] >= cls.systems[shield].max * 0.5)
                speak(kHalf[shield]);
        } else if (down) {
            static const int kTheirs[5] = {0, 0xFD, 0x100, 0xFF, 0xFE};
            speak(kTheirs[shield]);
        }
    }

    // What leaks in hits a system: the one aimed at, or one at random.
    for (int step = 0; step < 5 && leak > 0.5; step++) {
        int system = -1;
        if (aimed >= 5 && aimed <= 12 && cls.systems[aimed].max > 0 && ship.damage[aimed] < cls.systems[aimed].max &&
            (int)_random.getRandomNumber(99) <= cls.systems[aimed].aimed) {
            system = aimed;
        } else {
            int r = (int)_random.getRandomNumber(99);
            for (int s = 12; s >= 5; s--) {
                if (cls.systems[s].max > 0 && cls.systems[s].threshold <= r) {
                    system = s;
                    break;
                }
            }
        }
        if (system < 0)
            break;
        double room = cls.systems[system].max - ship.damage[system];
        if (room <= 0) {
            leak *= 0.8;
            continue;
        }
        double taken = std::min(room, leak);
        bool before = ship.damage[system] >= cls.systems[system].max;
        ship.damage[system] += taken;
        leak = (leak - taken) * 0.9;
        bool now = ship.damage[system] >= cls.systems[system].max;
        if (now && !before) {
            if (index == 0) {
                static const int kLines[13] = {0, 0, 0, 0, 0, 0xD2, 0x84, 0x9F, 0xAB, 0x91, 0x8B, 0xC7, 0};
                speak(kLines[system]);
                if (system == ShipClass::kLifeSupport)
                    _lifeSupportOut = 1;
            } else {
                speak(0x104 + system - 5);
            }
        }
    }

    // Enough systems gone, and the ship blows up (0x55009C).
    int destroyed = 0;
    for (int s = ShipClass::kWeapons; s <= ShipClass::kCloak; s++)
        destroyed += cls.systems[s].max > 0 && ship.damage[s] >= cls.systems[s].max;
    if ((int)_random.getRandomNumber(99) < kExplodeChance[std::min(8, destroyed)]) {
        destroy(index);
        return;
    }
    // An enemy with two systems or more gone breaks off (0x4B0103).
    if (index != 0 && destroyed > 1)
        runOff(ship);
    // Soran's ship is beaten once it can't run or has no power.
    if (ship.soran && (ship.damage[ShipClass::kEngines] >= cls.systems[ShipClass::kEngines].max ||
                       ship.damage[ShipClass::kPower] >= cls.systems[ShipClass::kPower].max)) {
        if (ship.soran == 2) {
            speak(0xB9, true);
            speak(0xB6, true);
        } else {
            speak(_random.getRandomNumber(1) ? 0xCD : 0xCE, true);
        }
        ship.gone = true;
        finish(kWon);
    }
}

void SpaceCombat::destroy(int index) {
    Ship &ship = _ships[index];
    ship.alive = false;
    Explosion e;
    e.position = ship.position;
    e.size = std::max(400.0, ship.radius * 4);
    e.time = 0;
    e.sprite = sprite(text::format("%d_BOOM.SPR", 1 + (int)_random.getRandomNumber(4)));
    e.ship = true;
    if (e.sprite)
        _explosions.push_back(e);
    playEffect(text::format("EXPLO%d.WAV", 1 + (int)_random.getRandomNumber(3)));
    if (index == 0) {
        speak(0xC8, true);
        finish(kDestroyed);
        return;
    }
    speak(0x10D);
    if (index == _target)
        nextTarget(false);
}

void SpaceCombat::nextTarget(bool announce) {
    int count = (int)_ships.size();
    for (int k = 1; k <= count; k++) {
        int i = ((_target < 1 ? 0 : _target) + k) % count;
        if (i == 0 || !isEnemyAlive(_ships[i]))
            continue;
        _target = i;
        if (announce) {
            const Ship &t = _ships[i];
            int base = t.token == "war"   ? 0xEA
                       : t.token == "bop" ? 0xEF
                       : t.token == "cho" ? 0xF5
                       : t.token == "vor" ? 0xFB
                                          : 0xFA;
            int soranLine = t.token == "war" ? 0xEE : t.token == "bop" ? 0xF4 : t.token == "cho" ? 0xF9 : 0x110;
            speak(t.soran ? soranLine : base + std::min(t.number - 1, t.token == "bop" ? 4 : 3));
        }
        return;
    }
    _target = -1;
}

void SpaceCombat::checkEnd() {
    Ship &e = _ships[0];
    if (_lifeSupportOut) {
        _lifeSupportOut += 16;
        if (_lifeSupportOut >= kLifeSupportMillis) {
            speak(0x85, true);
            finish(kDestroyed);
            return;
        }
    }
    if (!e.alive)
        return;
    bool any = false;
    for (size_t i = 1; i < _ships.size(); i++)
        any |= isEnemyAlive(_ships[i]);
    if (any)
        return;
    // The next wave arrives, round the Enterprise (0x4A2FA9).
    int next = 0;
    for (size_t i = 1; i < _ships.size(); i++) {
        if (_ships[i].hidden && _ships[i].alive && (next == 0 || _ships[i].wave < next))
            next = _ships[i].wave;
    }
    if (next) {
        for (size_t i = 1; i < _ships.size(); i++) {
            Ship &s = _ships[i];
            if (s.hidden && s.wave == next) {
                s.hidden = false;
                s.position = s.position + e.position;
            }
        }
        nextTarget(true);
        return;
    }
    speak(0xB7, true);
    finish(kWon);
}

void SpaceCombat::finish(Result result) {
    if (_phase != kFight)
        return;
    _result = result;
    _phase = kEnding;
    _endTime = 0;
    playMusic(result == kWon ? "SCWIN.WAV" : "SCLOSE.WAV", false);
}

// Sound

void SpaceCombat::speak(int id, bool force) {
    std::string file = speechFile(id);
    if (file.empty() || !_mixer)
        return;
    // Each line waits a while before it can be said again.
    auto said = _spoken.find(id);
    if (!force && said != _spoken.end() && _time - said->second < 8000)
        return;
    if (!force && _mixer->isSoundHandleActive(_speech))
        return;
    std::unique_ptr<SoundStream> stream = _resources.loadSound({kArchive, file});
    if (!stream)
        return;
    _spoken[id] = _time;
    _mixer->stopHandle(_speech);
    _mixer->playStream(Mixer::kSpeechSoundType, &_speech, std::move(stream));
    _subtitle = _resources.loadSubtitle({kArchive, file});
    _subtitleLeft = 2500;
    if (!_subtitle.empty())
        log::debug(1, "%s", _subtitle.c_str());
}

void SpaceCombat::playEffect(const std::string &name, int volume) {
    if (!_mixer)
        return;
    std::unique_ptr<SoundStream> stream = _resources.loadSound({kArchive, name});
    if (stream)
        _mixer->playStream(Mixer::kSFXSoundType, &_effect, std::move(stream), -1,
                           (uint8_t)std::max(0, std::min(255, volume)));
}

void SpaceCombat::playMusic(const std::string &name, bool loop) {
    if (!_mixer)
        return;
    _mixer->stopHandle(_music);
    std::unique_ptr<SoundStream> stream = _resources.loadSound({kArchive, name});
    if (!stream)
        return;
    if (loop)
        _mixer->playStream(Mixer::kMusicSoundType, &_music, makeLooping(std::move(stream), 0));
    else
        _mixer->playStream(Mixer::kMusicSoundType, &_music, std::move(stream));
}

// Input

void SpaceCombat::keyDown(const KeyState &key) {
    if (_phase == kBriefing) {
        if (key.keycode == reluva::key::Return || key.keycode == reluva::key::Space ||
            key.keycode == reluva::key::Escape)
            mouseUp(_proceedX + 10, _proceedY + 10);
        return;
    }
    if (_phase == kEnding) {
        if (key.keycode == reluva::key::Escape || key.keycode == reluva::key::Space ||
            key.keycode == reluva::key::Return)
            _finished = true;
        return;
    }
    Helm before = _helm;
    // Esc is the menu whatever the map says (0x47AF5B).
    if (key.keycode == reluva::key::Escape) {
        _optionsRequest = true;
        return;
    }
    uint8_t action = _keys.get(KeyMap::virtualKey(key.keycode));
    if (action != KeyMap::kNone) {
        shipAction(action, true);
    } else {
        switch (key.keycode) {
        case reluva::key::Tab:
        case 't':
            nextTarget(true);
            break;
        case 'v':
            toggleEnemyView();
            break;
        default:
            if (extraAction(key.keycode) >= 0)
                shipAction(extraAction(key.keycode), true);
            break;
        }
    }
    helmChanged(before);
}

void SpaceCombat::helmChanged(Helm before) {
    if (_helm == before)
        return;
    speak(0xAF + _helm, true);
    if (_helm == kMaintain && _target >= 0)
        _keepRange = (_ships[_target].position - _ships[0].position).length();
    if (_helm == kWarpOut) {
        // Warp needs a fifth of the engines (0x4A53A1).
        if (systemFraction(_ships[0], ShipClass::kEngines) >= 0.21) {
            finish(kLost);
        } else {
            speak(0x9E, true);
            _helm = kStandBy;
        }
    }
}

void SpaceCombat::keyUp(const KeyState &key) {
    uint8_t action = _keys.get(KeyMap::virtualKey(key.keycode));
    if (action != KeyMap::kNone)
        shipAction(action, false);
    else if (extraAction(key.keycode) >= 0)
        shipAction(extraAction(key.keycode), false);
}

void SpaceCombat::toggleEnemyView() {
    _enemyView = !_enemyView;
    speak(_enemyView ? 0x12A : 0x129);
}

void SpaceCombat::releaseKeys() {
    _turnKeys[0] = _turnKeys[1] = _turnKeys[2] = 0;
    _speedKey = 0;
}

int SpaceCombat::extraAction(uint32_t key) {
    switch (key) {
    case 'f':
        return KeyMap::kShipPhasers;
    case 'g':
        return KeyMap::kShipPhotons;
    case 'q':
        return KeyMap::kShipRollLeft;
    case 'e':
        return KeyMap::kShipRollRight;
    case '=':
        return KeyMap::kShipFaster;
    case '-':
        return KeyMap::kShipSlower;
    default:
        return -1;
    }
}

void SpaceCombat::shipAction(int action, bool down) {
    if (action >= KeyMap::kShipHelm && action <= KeyMap::kShipHelm + (int)kMaintainStatus) {
        if (down)
            _helm = (Helm)(action - KeyMap::kShipHelm);
        return;
    }
    // The flight keys hold a rate while down; any of them takes the helm
    // to Stand By (0x49557B).
    int axis = -1;
    int direction = 0;
    switch (action) {
    case KeyMap::kShipPhasers:
        if (down && _phasersOnline && !_phaserLockout) {
            _phaserLockout = 1000;
            speak(0xAD);
            firePhasers(0);
        }
        return;
    case KeyMap::kShipPhotons:
        if (down && _photonsOnline && !_photonLockout) {
            _photonLockout = 1000;
            speak(0xAE);
            firePhotons(0);
        }
        return;
    case KeyMap::kShipLeft:
    case KeyMap::kShipRight:
        axis = 0;
        direction = action == KeyMap::kShipLeft ? -1 : 1;
        break;
    case KeyMap::kShipDive:
    case KeyMap::kShipClimb:
        // Dive, as a flight stick, is up.
        axis = 1;
        direction = action == KeyMap::kShipDive ? -1 : 1;
        break;
    case KeyMap::kShipRollLeft:
    case KeyMap::kShipRollRight:
        axis = 2;
        direction = action == KeyMap::kShipRollLeft ? -1 : 1;
        break;
    case KeyMap::kShipFaster:
    case KeyMap::kShipSlower:
        if (down) {
            flyByHand();
            _speedKey = action == KeyMap::kShipFaster ? 1 : -1;
        } else {
            _speedKey = 0;
        }
        return;
    default:
        return;
    }
    if (down) {
        flyByHand();
        _turnKeys[axis] = direction;
    } else {
        _turnKeys[axis] = 0;
    }
}

bool SpaceCombat::takeOptionsRequest() {
    bool request = _optionsRequest;
    _optionsRequest = false;
    return request;
}

void SpaceCombat::moveSlider(Slider &slider, int y) {
    slider.row = std::max(0, std::min(y - slider.slot.top, slider.slot.height() - 1));
}

void SpaceCombat::drawSlider(Canvas &screen, const Slider &slider) {
    const Rect &slot = slider.slot;
    int split = slot.top + slider.row;
    Rect above(slot.left, slot.top, slot.right, split);
    Rect below(slot.left, split, slot.right, slot.bottom);
    drawImage(screen, image(slider.unlit), slot.left, slot.top, &above);
    drawImage(screen, image(slider.lit), slot.left, slot.top, &below);
}

void SpaceCombat::mouseMove(int x, int y) {
    if (_dragging)
        moveSlider(*_dragging, y);
    if (_steering)
        steerByMouse(x, y);
}

void SpaceCombat::flyByHand() {
    if (_helm != kStandBy) {
        _helm = kStandBy;
        speak(0xB3);
    }
}

void SpaceCombat::steerByMouse(int x, int y) {
    // 0x4AEFD4: (middle - mouse) * 100 / half the viewscreen hotspot,
    // (0,0)-(639,249), for each axis. 0x4A6F7D takes the yaw rate down by
    // x, turning left, and the pitch rate up by y, as Climb does: left of
    // the middle turns left, above it climbs.
    _mouseTurn[0] = clamp1((x - 319) / 319.0);
    _mouseTurn[1] = clamp1((124 - y) / 124.0);
}

void SpaceCombat::mouseDown(int x, int y) {
    _pressed = -1;
    for (Slider *slider : {&_viewSlider, &_radarSlider}) {
        if (_phase == kFight && slider->slot.contains(x, y)) {
            _dragging = slider;
            moveSlider(*slider, y);
            return;
        }
    }
    // The display buttons at the panels' feet (hotspots 15, 16, 0x4AEC22).
    for (int panel = 0; panel < 2; panel++) {
        if (_phase == kFight && kViewButton[panel].contains(x, y)) {
            _shieldView[panel] = !_shieldView[panel];
            speak(_shieldView[panel] ? 0x12C : 0x12B, true);
            return;
        }
    }
    // Held on the viewscreen, the ship turns towards the mouse.
    if (_phase == kFight && Rect(0, 0, 640, 250).contains(x, y)) {
        flyByHand();
        _steering = true;
        steerByMouse(x, y);
        return;
    }
    if (_phase == kFight && Rect(202, 380, 307, 400).contains(x, y))
        _pressed = 0;
    else if (_phase == kFight && Rect(320, 380, 425, 400).contains(x, y))
        _pressed = 1;
    else if (_phase == kFight && kConnLine.contains(x, y))
        _helmMenu = !_helmMenu;
}

void SpaceCombat::mouseUp(int x, int y) {
    int pressed = _pressed;
    _pressed = -1;
    if (_dragging) {
        _dragging = nullptr;
        return;
    }
    if (_steering) {
        _steering = false;
        _mouseTurn[0] = _mouseTurn[1] = 0;
        return;
    }
    if (_phase == kBriefing) {
        if (Rect(_proceedX, _proceedY, _proceedX + 83, _proceedY + 19).contains(x, y)) {
            playEffect("PROCEED.WAV");
            if (_mixer)
                _mixer->stopHandle(_music);
            _phase = kFight;
            playMusic(text::format("SCLOOP%d.WAV", 1 + (int)_random.getRandomNumber(3)), true);
        }
        return;
    }
    if (_phase != kFight)
        return;
    if (pressed == 0 && Rect(202, 380, 307, 400).contains(x, y)) {
        shipAction(KeyMap::kShipPhasers, true);
    } else if (pressed == 1 && Rect(320, 380, 425, 400).contains(x, y)) {
        shipAction(KeyMap::kShipPhotons, true);
    } else if (Rect(307, 380, 320, 400).contains(x, y)) {
        _phasersOnline = !_phasersOnline;
        speak(_phasersOnline ? 0x12D : 0x12E, true);
    } else if (Rect(425, 380, 438, 400).contains(x, y)) {
        _photonsOnline = !_photonsOnline;
        speak(_photonsOnline ? 0x12F : 0x130, true);
    } else if (Rect(177, 317, 190, 343).contains(x, y)) {
        nextTarget(true);
    } else if (Rect(177, 344, 190, 372).contains(x, y)) {
        toggleEnemyView();
    } else if (_helmMenu && !kConnLine.contains(x, y)) {
        // The helm menu (CONNMNU0), a row each 20 pixels: let go over one to
        // give the order, anywhere else to put it away.
        _helmMenu = false;
        int row = (y - 319) / 20;
        if (Rect(472, 319, 639, 459).contains(x, y) && row >= 0 && row <= 6) {
            Helm before = _helm;
            shipAction(KeyMap::kShipHelm + row, true);
            helmChanged(before);
        }
    } else if (Rect(1, 252, 175, 462).contains(x, y) && _target >= 0) {
        // The enemy's systems: aim at the next one (weapons .. cloak).
        const ShipClass *cls = _ships[_target].cls;
        for (int k = 0; k < 8; k++) {
            _aimedSystem = _aimedSystem < 5 || _aimedSystem >= 12 ? 5 : _aimedSystem + 1;
            if (cls && cls->systems[_aimedSystem].max > 0)
                break;
        }
        speak(0x120 + _aimedSystem - 5, true);
    }
}

// Drawing

void SpaceCombat::drawImage(Canvas &screen, const CombatImage *img, int x, int y, const Rect *clip) {
    if (!img)
        return;
    Rect area(0, 0, screen.w, screen.h);
    if (clip)
        area.clip(*clip);
    for (int sy = 0; sy < img->h; sy++) {
        int dy = y + sy;
        if (dy < area.top || dy >= area.bottom)
            continue;
        uint32_t *row = (uint32_t *)screen.getBasePtr(0, dy);
        for (int sx = 0; sx < img->w; sx++) {
            int dx = x + sx;
            uint32_t c = img->get(sx, sy);
            if (dx >= area.left && dx < area.right && (c >> 24))
                row[dx] = c;
        }
    }
}

void SpaceCombat::drawText(Canvas &screen, const std::string &text, int x, int y, bool small) {
    // FONTSCAN for messages, FONTLIT (capitals and digits) for the readouts.
    const CombatImage *strip = image(small ? "A-Z_0-9.RAS" : "SCANNER.RAS");
    if (!strip)
        return;
    const MetFont &font = small ? _smallFont : _font;
    for (char ch : text) {
        int c = (uint8_t)ch;
        if (c < 32 || c > 127)
            c = 32;
        const MetFont::Cell &cell = font.cells[c - 32];
        for (int gy = 0; gy < cell.h; gy++) {
            for (int gx = 0; gx < cell.w; gx++) {
                int sx = cell.x + gx;
                int sy = cell.y + gy;
                int dx = x + gx;
                int dy = y + gy;
                if (sx >= strip->w || sy >= strip->h || dx < 0 || dy < 0 || dx >= screen.w || dy >= screen.h)
                    continue;
                uint32_t p = strip->get(sx, sy);
                // The strip's glyphs are light on dark.
                if ((p >> 24) && ((p >> 16 & 0xFF) + (p >> 8 & 0xFF) + (p & 0xFF)) > 150)
                    *(uint32_t *)screen.getBasePtr(dx, dy) = p;
            }
        }
        // FONTLIT's cells hold their own spacing.
        x += small ? cell.w : std::max(cell.w, 3) + 1;
        if (x > screen.w)
            break;
    }
}

void SpaceCombat::drawBars(Canvas &screen, const Ship &ship, int x, int y) {
    // Twelve columns, shields to cloak: the dark covers what's lost.
    if (!ship.cls)
        return;
    for (int s = 1; s <= 12; s++) {
        int max = ship.cls->systems[s].max;
        if (max <= 0)
            continue;
        double level = s <= 4 ? ship.energy[s] / max : systemFraction(ship, s);
        level = std::max(0.0, std::min(1.0, level));
        int left = x + 1 + 14 * (s - 1);
        int top = y + 9;
        int height = 41;
        int dark = (int)(height * (1 - level));
        for (int dy = 0; dy < dark; dy++) {
            uint32_t *row = (uint32_t *)screen.getBasePtr(0, top + dy);
            for (int dx = 1; dx < 12; dx++)
                row[left + dx] = darken(row[left + dx], 0.25);
        }
    }
}

int SpaceCombat::systemLook(const Ship &ship, int system) const {
    // 0x49E232: the bright picture for a system that works, the dim one
    // for one that is down or damaged, the red one for one destroyed.
    if (!ship.cls || ship.cls->systems[system].max <= 0)
        return 1;
    double max = ship.cls->systems[system].max;
    if (ship.damage[system] >= max)
        return 2;
    if (system <= 4)
        return ship.energy[system] > 0 && ship.damage[system] <= 0 ? 0 : 1;
    return ship.damage[system] > 0 ? 1 : 0;
}

void SpaceCombat::drawSchematic(Canvas &screen, const Ship &ship, int x, int y) {
    // 0x49EAF6: S???_STR numbers each pixel with its system, 1-12; those
    // take S???_0, _1 or _2 by that system's look, the rest S???_1.
    std::string token = text::toUpper(ship.token);
    const CombatImage *map = image(text::format("S%s_STR.RAS", token.c_str()));
    const CombatImage *looks[3];
    for (int k = 0; k < 3; k++)
        looks[k] = image(text::format("S%s_%d.RAS", token.c_str(), k));
    if (!map || !looks[0] || !looks[1] || !looks[2] || map->indices.size() != (size_t)map->w * map->h) {
        drawImage(screen, looks[0], x, y);
        return;
    }
    int look[13] = {};
    for (int s = 1; s <= 12; s++)
        look[s] = systemLook(ship, s);
    for (int sy = 0; sy < map->h; sy++) {
        int dy = y + sy;
        if (dy < 0 || dy >= screen.h)
            continue;
        uint32_t *row = (uint32_t *)screen.getBasePtr(0, dy);
        for (int sx = 0; sx < map->w; sx++) {
            int dx = x + sx;
            if (dx < 0 || dx >= screen.w)
                continue;
            uint8_t system = map->indices[(size_t)sy * map->w + sx];
            const CombatImage *from = looks[system >= 1 && system <= 12 ? look[system] : 1];
            if (sx >= from->w || sy >= from->h)
                continue;
            uint32_t c = from->get(sx, sy);
            if (c >> 24)
                row[dx] = c;
        }
    }
}

void SpaceCombat::drawShieldView(Canvas &screen, const Ship &ship, const Ship &other, const Rect &panel) {
    // 0x49F302 / 0x49F88F: below the bars (57 rows down) to row 461, a
    // width a multiple of four; black, with the ship seen from three times
    // its largest extent out towards the other, 60 degrees across.
    int left = panel.left;
    int top = panel.top + 57;
    int w = panel.width() & ~3;
    int h = 461 - top;
    Rect area(left, top, left + w, top + h);
    for (int y = area.top; y < area.bottom; y++) {
        uint32_t *row = (uint32_t *)screen.getBasePtr(0, y);
        std::fill(row + area.left, row + area.right, 0xFF000000u);
    }
    const Model3D *m = ship.model;
    if (!m || !ship.materials)
        return;
    Vec3 towards = other.position - ship.position;
    if (towards.length() < 1e-6)
        towards = ship.orientation.forward;
    Vec3 dir = towards.normalized();
    Vec3 camPos = ship.position + dir * (ship.size * 3);
    _panelRenderer.setViewport(w, h, 60);
    _panelRenderer.setCamera(camPos, Mat3::lookAlong(dir * -1.0, ship.orientation.up));
    _panelRenderer.begin(screen, left, top);
    _panelRenderer.drawModel(
        *m, *ship.materials, [this](const std::string &name) { return texture(name); }, ship.position,
        ship.orientation);
    // Round it the shield sphere (0x49FC1E), a colour to each facing,
    // sized 0.8 of the ship's largest extent per 100 units (0x4A3BB4).
    if (ship.sphereModel && ship.sphereMaterials)
        _panelRenderer.drawModel(
            *ship.sphereModel, *ship.sphereMaterials, [](const std::string &) { return (const CombatImage *)nullptr; },
            ship.position, ship.orientation, ship.size * 0.8 / 100);
}

void SpaceCombat::drawStar(int x, int y, int type) {
    // 0x4AE2BF: by type, a cross, a single dim pixel, an X, a plus, two
    // bars; the rest one pixel that flickers between two colours.
    struct Dot {
        int dx, dy;
        uint8_t colour;
    };
    static const std::vector<Dot> kShapes[8] = {
        {},
        {{0, 0, 0x87}, {-1, 0, 0x84}, {1, 0, 0x84}, {-2, 0, 0x82}, {2, 0, 0x82}, {0, 1, 0x82}, {0, -1, 0x82}},
        {},
        {{0, 0, 0xA9}},
        {{0, 0, 0x84}, {-1, -1, 0x82}, {1, -1, 0x82}, {-1, 1, 0x82}, {1, 1, 0x82}},
        {{0, 0, 0x84}, {-1, 0, 0x82}, {1, 0, 0x82}, {0, 1, 0x82}, {0, -1, 0x82}},
        {{0, 0, 0x84}, {-1, 0, 0x82}, {1, 0, 0x82}},
        {{0, 0, 0x84}, {-1, 0, 0x87}, {1, 0, 0x87}}};
    if (type >= 1 && type <= 7 && type != 2) {
        for (const Dot &dot : kShapes[type])
            _renderer.plot(x + dot.dx, y + dot.dy, _palette[dot.colour]);
        return;
    }
    // One time in eight the brighter one, as the original's roll; a hash
    // rather than the game's dice, which the fight's rolls use.
    uint32_t roll = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ _frames * 83492791u;
    _renderer.plot(x, y, _palette[(roll >> 7) % 8 ? 0x84 : 0x87]);
}

SpaceCombat::Streak SpaceCombat::newStreak() {
    Streak streak;
    streak.x = (int)_random.getRandomNumber(1903) - 952;
    streak.y = (int)_random.getRandomNumber(1903) - 952;
    streak.scale = 1;
    streak.type = (int)_random.getRandomNumber(12);
    return streak;
}

void SpaceCombat::drawRadar(Canvas &screen) {
    // 0x4AB102 / 0x4AB5C1: round the Enterprise, which faces up the
    // screen. Each ship sits on the plane (sideways 102 pixels and ahead
    // 24 pixels to the range) on a stalk as tall as its height (24 pixels
    // to the range): white with the marker on top if level or above, red
    // with it at the foot if below.
    const int cx = 321;
    const int cy = 320;
    // Half the plane's width, a row at a time out from the middle (0x54F750).
    static const int kHalfWidth[19] = {102, 102, 102, 102, 101, 101, 100, 99, 98, 97,
                                       96,  94,  92,  90,  87,  85,  81,  78, 75};
    drawImage(screen, image("RADARM.RAS"), cx - 102, 323 - 48);
    const Ship &e = _ships[0];
    double range = getRadarRange();
    for (size_t i = 0; i < _ships.size(); i++) {
        const Ship &s = _ships[i];
        if (!isEnemyAlive(s) && i != 0)
            continue;
        Vec3 local = e.orientation.inverse(s.position - e.position);
        if (std::fabs(local.x) > range || std::fabs(local.y) > range || std::fabs(local.z) > range)
            continue;
        int across = (int)(local.x * 102 / range);
        int ahead = (int)(local.z * 24 / range);
        int height = (int)(local.y * 24 / range);
        int x = cx + across;
        int foot = cy - ahead;
        int tip = foot - height;
        int top = std::min(foot, tip);
        int bottom = std::max(foot, tip);
        if (std::abs(across) > 96 || std::abs(ahead) > 18 || top < 280 || bottom > 360 ||
            across < -kHalfWidth[std::abs(ahead)] || across > kHalfWidth[std::abs(ahead)] - 5)
            continue;
        uint32_t stalk = height < 0 ? 0xFFFF0000u : 0xFFFFFFFFu;
        for (int y = top; y <= bottom; y++)
            *(uint32_t *)screen.getBasePtr(x, y) = stalk;
        std::string token = text::toUpper(s.token);
        const CombatImage *blip = image(text::format("S%sMAP%s.RAS", token.c_str(), (int)i == _target ? "H" : ""));
        if (!blip)
            blip = image(text::format("S%sMAP.RAS", token.c_str()));
        if (blip)
            drawImage(screen, blip, x - 5, (height < 0 ? bottom : top) - 5);
    }
    // A ship blowing up shows as an explosion, flashing (0x4AB6DA: SEXP1
    // and SEXP2 by turns, a pixel further right as they are 9 wide).
    for (const Explosion &boom : _explosions) {
        if (!boom.ship)
            continue;
        Vec3 local = e.orientation.inverse(boom.position - e.position);
        if (std::fabs(local.x) > range || std::fabs(local.y) > range || std::fabs(local.z) > range)
            continue;
        int x = cx + (int)(local.x * 102 / range);
        int y = cy - (int)(local.z * 24 / range) - (int)(local.y * 24 / range);
        if (x < 219 || x > 423 || y < 280 || y > 360)
            continue;
        drawImage(screen, image(_frames & 1 ? "SEXP2.RAS" : "SEXP1.RAS"), x - 4, y - 5);
    }
    if (_target >= 0 && _target < (int)_ships.size())
        drawText(screen, text::format("RANGE: %6.0f", (_ships[_target].position - e.position).length()), 209, 365,
                 true);
}

void SpaceCombat::drawView(Canvas &screen) {
    const Ship &e = _ships[0];
    // The camera sits out along its own line of sight from the Enterprise,
    // so the Enterprise itself is never in view.
    Vec3 camPos;
    Mat3 camRot;
    if (_enemyView && _target >= 0 && isEnemyAlive(_ships[_target])) {
        // 0x4A1E85: one radius out towards the target, looking at it, the
        // Enterprise's up kept.
        Vec3 dir = (_ships[_target].position - e.position).normalized();
        camPos = e.position + dir * (e.radius + getViewZoom() * 600.0);
        camRot = Mat3::lookAlong(dir, e.orientation.up);
    } else {
        // 0x4A0D13: two of its radii out along its axis, ahead of it, so the
        // Enterprise itself is out of sight.
        camRot = e.orientation;
        camPos = e.position + e.orientation.forward * (e.radius * 2 + getViewZoom() * 600.0);
    }
    _renderer.setCamera(camPos, camRot);
    Rect view(kViewX, kViewY, kViewX + kViewW, kViewY + kViewH);
    for (int y = view.top; y < view.bottom; y++) {
        uint32_t *row = (uint32_t *)screen.getBasePtr(0, y);
        std::fill(row + view.left, row + view.right, 0xFF000000u);
    }
    _renderer.begin(screen, kViewX, kViewY);

    // The stars, far away: only the camera's turn moves them, and the
    // zoom spreads them from the centre (0x4ADC6A).
    double spread = _renderer.getFocal() * (1 + getViewZoom() / 30.0);
    for (const Vec3 &star : _stars) {
        Vec3 local = camRot.inverse(star);
        if (local.z <= 0.05)
            continue;
        int x = (int)(kViewW / 2.0 + spread * local.x / local.z);
        int y = (int)(kViewH / 2.0 - spread * local.y / local.z);
        drawStar(x, y, _starTypes[&star - &_stars[0]]);
    }
    // The streaming stars, started again once out of the view, or back in
    // the middle when going astern.
    for (Streak &streak : _streaks) {
        int x = (int)(kViewW / 2.0 + streak.x * streak.scale * (1 + getViewZoom() / 30.0));
        int y = (int)(kViewH / 2.0 + streak.y * streak.scale * (1 + getViewZoom() / 30.0));
        bool middle = streak.scale < 1 && std::abs(x - kViewW / 2) < 30 && std::abs(y - kViewH / 2) < 30;
        if (x < 4 || x >= kViewW - 4 || y < 4 || y >= kViewH - 4 || middle) {
            streak = newStreak();
            continue;
        }
        drawStar(x, y, streak.type);
    }
    _frames++;

    auto lookup = [this](const std::string &name) { return texture(name); };
    for (size_t i = 0; i < _ships.size(); i++) {
        const Ship &s = _ships[i];
        if (!s.alive || s.hidden || s.gone || i == 0)
            continue;
        double distance = (s.position - camPos).length();
        // The detailed model near, the rough one further off (0x54E5D8).
        bool near = distance < std::max(6000.0, s.radius * 40);
        const Model3D *m = near || !s.lowModel ? s.model : s.lowModel;
        const std::vector<CombatMaterial> *mats = near || !s.lowModel ? s.materials : s.lowMaterials;
        if (m && mats) {
            _renderer.drawModel(*m, *mats, lookup, s.position, s.orientation);
        } else {
            double sx;
            double sy;
            double depth;
            if (_renderer.project(s.position, sx, sy, depth))
                _renderer.plot((int)sx, (int)sy, 0xFFFFFFFFu);
        }
        // The target's box (0x4A2AD2).
        if ((int)i == _target) {
            double sx;
            double sy;
            double depth;
            if (_renderer.project(s.position, sx, sy, depth)) {
                int r = std::max(6, (int)(s.radius * _renderer.getFocal() / depth));
                for (int k = -r; k <= r; k++) {
                    if (std::abs(k) < r / 2)
                        continue;
                    _renderer.plot((int)sx + k, (int)sy - r, 0xFFFFE040u);
                    _renderer.plot((int)sx + k, (int)sy + r, 0xFFFFE040u);
                    _renderer.plot((int)sx - r, (int)sy + k, 0xFFFFE040u);
                    _renderer.plot((int)sx + r, (int)sy + k, 0xFFFFE040u);
                }
            }
        }
    }
    for (const Beam &b : _beams) {
        Vec3 from = _ships[b.from].position;
        _renderer.drawLine(from, b.end, b.colour, 2);
    }
    for (const Torpedo &t : _torpedoes) {
        const SprAnimation *spr = t.sprite;
        if (!spr || spr->frames.empty())
            continue;
        const std::vector<uint8_t> &frame = spr->frames[(_time / spr->frameMillis) % spr->frames.size()];
        _renderer.drawBillboard(
            t.position, 220, spr->w, spr->h,
            [&](int x, int y) {
                uint8_t v = frame[(size_t)y * spr->w + x];
                return v ? _palette[v] : 0u;
            },
            true);
    }
    // Shield hits (0x497D2E): the dome twice the ship's size out from its
    // middle towards what hit it, facing that way, sized by the ship (the
    // model fits the Enterprise, whose sizes average 412.667) and textured
    // with the ship's shield animation.
    for (size_t i = 1; i < _ships.size(); i++) {
        const Ship &s = _ships[i];
        if (!s.flashing || !s.alive || s.hidden || s.gone || !s.shieldModel || !s.shieldMaterials)
            continue;
        const SprAnimation *spr = sprite(text::format("%c_SHIELD.SPR", s.effects == 'P' ? '_' : s.effects));
        if (!spr || spr->frames.empty())
            continue;
        const std::vector<uint8_t> &frame =
            spr->frames[std::min(spr->frames.size() - 1, (size_t)(s.flashTime / spr->frameMillis))];
        _flashImage.w = spr->w;
        _flashImage.h = spr->h;
        _flashImage.pixels.resize(frame.size());
        for (size_t p = 0; p < frame.size(); p++)
            _flashImage.pixels[p] = frame[p] ? _palette[frame[p]] : 0xFF000000u;
        Vec3 towards = (s.flashFrom - s.position).normalized();
        _renderer.drawModel(
            *s.shieldModel, *s.shieldMaterials, [this](const std::string &) { return &_flashImage; },
            s.position + towards * (s.size * 2), Mat3::lookAlong(towards), s.meanSize / 412.667);
    }
    for (const Explosion &e2 : _explosions) {
        const SprAnimation *spr = e2.sprite;
        if (!spr || spr->frames.empty())
            continue;
        size_t index = std::min(spr->frames.size() - 1, (size_t)(e2.time / spr->frameMillis));
        const std::vector<uint8_t> &frame = spr->frames[index];
        _renderer.drawBillboard(
            e2.position, e2.size, spr->w, spr->h,
            [&](int x, int y) {
                uint8_t v = frame[(size_t)y * spr->w + x];
                return v ? _palette[v] : 0u;
            },
            true);
    }
}

void SpaceCombat::drawConsole(Canvas &screen) {
    drawImage(screen, image("COMBAT.RAS"), 0, 0);
    const Ship &e = _ships[0];

    // Left, the target; right, the Enterprise and the helm menu. Each
    // shows its system display, or its shield facing display below the
    // bars (hotspots 1 and 0).
    const Rect kPanel[2] = {Rect(472, 252, 629, 462), Rect(1, 252, 175, 462)};
    if (_target >= 0 && _target < (int)_ships.size() && isEnemyAlive(_ships[_target])) {
        const Ship &t = _ships[_target];
        if (systemFraction(e, ShipClass::kSensors) <= 0) {
            drawImage(screen, image(text::format("STATIC%d.RAS", 1 + (int)(_time / 100) % 3)), 0, 252);
        } else {
            drawSchematic(screen, t, 0, 252);
            drawBars(screen, t, 0, 252);
            if (_shieldView[1])
                drawShieldView(screen, t, e, kPanel[1]);
        }
    }
    drawSchematic(screen, e, 466, 255);
    drawBars(screen, e, 466, 255);
    if (_shieldView[0] && _target >= 0 && isEnemyAlive(_ships[_target]))
        drawShieldView(screen, e, _ships[_target], kPanel[0]);
    drawImage(screen, image(_shieldView[1] ? "BSCANLF.RAS" : "BSCANLN.RAS"), 174, 435);
    drawImage(screen, image(_shieldView[0] ? "BSCANRF.RAS" : "BSCANRN.RAS"), 449, 435);
    // The helm menu, while it is called up from the CONN line (0x4AECC6).
    const CombatImage *menu = image("CONNMNU0.RAS");
    const CombatImage *lit = image("CONNMNU1.RAS");
    if (_helmMenu) {
        drawImage(screen, menu, 472, 310);
        if (lit && _helm <= kMaintainStatus) {
            Rect band(472, 310 + _helm * 20 + 9, 472 + lit->w, 310 + _helm * 20 + 29);
            drawImage(screen, lit, 472, 310, &band);
        }
    }
    // The CONN line: the order in hand, a row of CONNBUTT each (0x49FD41).
    const CombatImage *conn = image("CONNBUTT.RAS");
    if (conn && _helm < kMaintainStatus) {
        Rect clip(262, 442, 262 + 176, 442 + 17);
        drawImage(screen, conn, 262, 442 - _helm * 19, &clip);
    }
    // Names and speeds (0x49F302), as a share of the ship's top speed.
    auto speedOf = [](const Ship &s) { return s.cls && s.cls->maxSpeed ? s.speed / s.cls->maxSpeed : 0.0; };
    drawText(screen, e.name, 604, 310, true);
    drawText(screen, text::format("SPEED: %.3f", speedOf(e)), 470, 449, true);
    if (_target >= 0 && _target < (int)_ships.size() && isEnemyAlive(_ships[_target])) {
        drawText(screen, _ships[_target].name, 2, 310, true);
        drawText(screen, text::format("SPEED: %.3f", speedOf(_ships[_target])), 123, 449, true);
    }

    // The fire buttons: 0 dim, 1 lit, 2 normal.
    bool target = _target >= 0;
    drawImage(screen,
              image(!_phasersOnline || !target ? "BPHASER0.RAS"
                    : _pressed == 0            ? "BPHASER1.RAS"
                                               : "BPHASER2.RAS"),
              202, 379);
    drawImage(screen,
              image(!_photonsOnline || !target ? "BPHOTON0.RAS"
                    : _pressed == 1            ? "BPHOTON1.RAS"
                                               : "BPHOTON2.RAS"),
              320, 379);
    drawImage(screen, image(_phasersOnline ? "BPHASERN.RAS" : "BPHASERF.RAS"), 307, 380);
    drawImage(screen, image(_photonsOnline ? "BPHOTONN.RAS" : "BPHOTONF.RAS"), 425, 380);
    drawImage(screen, image(_enemyView ? "BENEMYN.RAS" : "BENEMYF.RAS"), 174, 344);
    drawSlider(screen, _viewSlider);
    drawSlider(screen, _radarSlider);

    // The tubes, and the torpedoes left.
    if (e.cls) {
        for (int t = 0; t < 2; t++) {
            bool loaded = e.loaded[t] > 0;
            drawImage(screen, image(loaded ? "TUBEBLUE.RAS" : "TUBERED.RAS"), t == 0 ? 321 : 399, 402);
        }
        // In 0-9BIG's digits, 6 x 20 a cell, 8 apart (font 1, 0x49E01B).
        const CombatImage *digits = image("0-9BIG.RAS");
        std::string count = text::format("%3d", e.stock + e.loaded[0] + e.loaded[1]);
        for (size_t c = 0; digits && c < count.size(); c++) {
            if (count[c] < '0' || count[c] > '9')
                continue;
            int digit = count[c] - '0';
            int x = 369 + 8 * (int)c;
            Rect cell(x, 408, x + 6, 428);
            drawImage(screen, digits, x, 408 - 20 * digit, &cell);
        }
        // The phasers' charge.
        const CombatImage *meter = image("PHASERPW.RAS");
        if (meter) {
            drawImage(screen, meter, 203, 402);
            double charge = 0;
            double max = 0;
            for (int s = ShipClass::kPhaser0; s < ShipClass::kPhoton0; s++) {
                if (e.cls->systems[s].max > 0) {
                    charge += e.energy[s];
                    max += e.cls->systems[s].max;
                }
            }
            int cover = max > 0 ? (int)(meter->w * (1 - charge / max)) : meter->w;
            for (int y = 402; y < 402 + meter->h; y++) {
                uint32_t *row = (uint32_t *)screen.getBasePtr(0, y);
                for (int x = 203 + meter->w - cover; x < 203 + meter->w; x++)
                    row[x] = darken(row[x], 0.3);
            }
        }
    }
    drawRadar(screen);

    // The message line: subtitles, or how it ended.
    std::string line;
    if (_subtitles && !_subtitle.empty())
        line = _subtitle;
    else if (_phase == kEnding)
        line = _result == kWon ? "VICTORY" : _result == kLost ? "WARPED OUT OF THE SYSTEM" : "THE ENTERPRISE IS LOST";
    drawText(screen, line.substr(0, 110), 4, 462);
}

void SpaceCombat::draw(Canvas &screen) {
    if (_phase == kBriefing) {
        for (int y = 0; y < screen.h; y++) {
            uint32_t *row = (uint32_t *)screen.getBasePtr(0, y);
            std::fill(row, row + screen.w, 0xFF000000u);
        }
        _briefing.draw(screen, 0, (screen.w - _briefing.getWidth()) / 2, (screen.h - _briefing.getFrameHeight()) / 2);
        _proceed.draw(screen, _pressed == 2 ? 1 : 0, _proceedX, _proceedY);
        return;
    }
    drawView(screen);
    drawConsole(screen);
    drawJolt(screen);
}

} // namespace reluva::stgenerations
