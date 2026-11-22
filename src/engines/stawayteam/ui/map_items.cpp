// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The officers' items: the one each has chosen, and using it: a grenade
// thrown, a hypospray used on an officer, a mine put down, a tricorder
// scanning (docs/engines/stawayteam/systems/items.md). The program's own item code isn't traced: what's here is read
// from the item definitions' and their models' fields.

#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

constexpr float kPi = 3.14159265f;
// A grenade's flight and fuse when its model doesn't give them (inferred).
constexpr float kDefaultThrowSpeed = 400.0f;
constexpr float kDefaultFuse = 3.0f;
// A thrown thing's Gravity, taken as pixels a second each 30th of a second
// (inferred), and a bounce too slow to be one, pixels a second (inferred).
constexpr float kGravityScale = 30.0f;
constexpr float kDefaultGravity = 17.0f;
constexpr float kRestingBounce = 40.0f;
// Near enough an officer to use an item on them: about the items' Strike
// Damage Radius (20) and a figure's width (inferred).
constexpr float kUseReach = 30.0f;
// A part this near where's clicked is what's clicked (inferred).
constexpr float kPartReach = 30.0f;
// A tricorder's words typed at its Text Speed, characters a second,
// without one (the items' 30).
constexpr float kScanSpeed = 30.0f;
// How long a mine's put down without its model giving it (inferred).
constexpr float kDefaultArm = 2.0f;

const char *const kScanSound = "Devices/Tricorder Science 3 pulse.ogg"; // the tricorders' Default Use Sound

int directionOf(float dx, float dy) {
    const int d = 6 - int(std::lround(std::atan2(dy, dx) / (kPi / 6.0f)));
    return (d % 12 + 12) % 12;
}

bool shoots(const ItemDefinition &d) {
    return d.weaponDamage > 0.0f &&
           (d.dischargeAction == "CShootEnergyWeaponAction" || d.dischargeAction == "CShootProjectileWeaponAction" ||
            d.dischargeAction == "CStrikeTargetAction");
}

// Whether its own Process Activity is one Reluva runs: something put
// down, set up where it's going, or an action.
bool ownProcess(const ItemDefinition &d) {
    const TextObject *list = d.process ? d.process->object("Process Activity") : nullptr;
    if (!list)
        return false;
    for (const TextField *f : list->items())
        if (f->object && (f->object->className == "CDropItemAI" || f->object->className == "CGotoPointAndDoActionAI" ||
                          f->object->className == "CActionAI"))
            return true;
    return false;
}

float gravityOf(const ItemDefinition &d) {
    return (d.gravity > 0.0f ? d.gravity : kDefaultGravity) * kGravityScale;
}

bool throws(const ItemDefinition &d) {
    return d.dischargeAction == "CThrowProjectileWeaponAction" && !d.projectile.empty();
}

} // namespace

void MapScreen::setupItems() {
    // Each officer's items as the roster gives them: its Count of uses, or,
    // given as -1, its definition's Rounds; the weapon they shoot with
    // chosen (inferred).
    for (Officer &o : _officers) {
        for (const TeamMember::Item &held : _team[o.member].inventory) {
            const auto def = _items.find(held.name);
            const int rounds = def != _items.end() ? def->second.rounds : 0;
            const bool stun = def != _items.end() && def->second.stuns;
            o.items.push_back({held.name, held.count > 0 ? held.count : rounds > 0 ? rounds : -1, stun});
            if (o.weapon && def != _items.end() && &def->second == o.weapon && o.item < 0)
                o.item = int(o.items.size()) - 1;
        }
    }
}

const ItemDefinition *MapScreen::itemOf(const Officer &o) const {
    if (o.item < 0 || size_t(o.item) >= o.items.size())
        return nullptr;
    const auto def = _items.find(o.items[size_t(o.item)].name);
    return def != _items.end() ? &def->second : nullptr;
}

void MapScreen::chooseItem(int officer, int item) {
    if (officer < 0 || size_t(officer) >= _officers.size())
        return;
    Officer &o = _officers[size_t(officer)];
    if (item < 0 || size_t(item) >= o.items.size())
        return;
    o.item = item;
    // Its place on the bar shows it.
    const std::vector<std::vector<int>> places = itemPlaces(o);
    o.shown.resize(places.size(), 0);
    for (size_t p = 0; p < places.size(); ++p)
        for (size_t k = 0; k < places[p].size(); ++k)
            if (places[p][k] == item)
                o.shown[p] = int(k);
    // A weapon chosen is what they shoot with.
    if (const ItemDefinition *def = itemOf(o); def && shoots(*def))
        o.weapon = def;
}

bool MapScreen::useItem(float x, float y) {
    if (_selected < 0)
        return false;
    Officer &o = _officers[size_t(_selected)];
    const ItemDefinition *def = itemOf(o);
    if (!def || o.down() || o.items[size_t(o.item)].count == 0)
        return false;
    if (throws(*def)) {
        o.task = Officer::Task::kThrow;
        o.taskX = x;
        o.taskY = y;
        o.attacking = -1;
        o.path.clear();
        return true;
    }
    // A mine is put down as one (it has a trigger, or is set off by its
    // panel); other things put down, set up or used by the item's own
    // Process Activity go through that.
    const bool mine = !def->deployModel.empty() && (def->remote || modelInfo(def->deployModel).triggerRadius > 0.0f);
    const bool scans = def->processType == "Scan" || def->processType == "Medical Scan";
    if (!mine && !scans && ownProcess(*def)) {
        o.task = Officer::Task::kItemProcess;
        o.taskX = x;
        o.taskY = y;
        o.taskPart = partAt(x, y);
        o.taskStep = 0;
        o.attacking = -1;
        o.path.clear();
        return true;
    }
    if (mine) {
        // Put down where's clicked (CDropItemAI, inferred: walked to first).
        o.task = Officer::Task::kPlace;
        o.taskX = x;
        o.taskY = y;
        o.attacking = -1;
        o.path.clear();
        return true;
    }
    if (def->processType == "Scan" || def->processType == "Medical Scan") {
        // A part with its own process for the tricorder goes through that
        // (its CProcessListAI's); else what's there, a part with a model,
        // is scanned, within its Item Range, walked to if not (inferred).
        if (startProcess(x, y))
            return true;
        const int part = partAt(x, y);
        if (part < 0)
            return false;
        o.task = Officer::Task::kScan;
        o.taskPart = part;
        o.attacking = -1;
        o.path.clear();
        return true;
    }
    if (def->health > 0.0f) {
        const int target = officerAt(x, y);
        if (target < 0 || (target == _selected && !def->canUseOnSelf))
            return false;
        o.task = Officer::Task::kUseOn;
        o.taskOfficer = target;
        o.attacking = -1;
        o.path.clear();
        return true;
    }
    return false;
}

bool MapScreen::doTask(size_t officer) {
    Officer &o = _officers[officer];
    const ItemDefinition *def = itemOf(o);
    if (!def)
        return true;
    if (o.task == Officer::Task::kScan) {
        // The tricorder's Default Use Sound, and the model's description
        // typed out on its panel (CTricorderModelDescriptionAction).
        float px = 0.0f;
        float py = 0.0f;
        placeOf(size_t(o.taskPart), px, py);
        o.direction = directionOf(px - o.x, py - o.y);
        const ModelInfo &info = modelInfo(_map.parts[size_t(o.taskPart)].model);
        _scanText = describe(info.descriptionScript, info.descriptionLine);
        _scanTyped = 0.0f;
        if (_mixer)
            if (auto sound = gameSound(_data, def->useSound.empty() ? kScanSound : def->useSound))
                _mixer->play(sound, audio::SoundType::kEffects);
        return true;
    }
    int &count = o.items[size_t(o.item)].count;
    if (count == 0)
        return true;
    if (count > 0)
        --count;
    o.discharge = def->dischargeSequence.empty() ? "Shoot1" : def->dischargeSequence;
    o.shooting = 1e-6f;
    if (o.task == Officer::Task::kPlace) {
        // Put down where they stand, its model's Default Sequence playing
        // (inferred: its Deploy first isn't).
        o.direction = directionOf(o.taskX - o.x, o.taskY - o.y);
        _mines.push_back({def, def->deployModel, o.taskX, o.taskY, 0.0f, false, 0.0f, int(officer)});
        return true;
    }
    if (o.task == Officer::Task::kThrow) {
        // Thrown from their hand's Height at the place: along the ground at
        // its model's Max Velocity, up so its Gravity brings it down there;
        // it goes off its Shot Duration after it's thrown (inferred: the
        // throw's speed and Gravity's units aren't traced).
        o.direction = directionOf(o.taskX - o.x, o.taskY - o.y);
        const ModelInfo &info = modelInfo(def->projectile);
        Thrown t{def,         def->projectile, o.x, o.y, o.taskX, o.taskY, info.fuse > 0.0f ? info.fuse : kDefaultFuse,
                 int(officer)};
        const float speed = info.maxVelocity > 0.0f ? info.maxVelocity : kDefaultThrowSpeed;
        const float distance = std::hypot(o.taskX - o.x, o.taskY - o.y);
        const float flight = std::max(0.1f, distance / speed);
        const float g = gravityOf(*def);
        t.z = def->throwHeight;
        t.vx = distance > 0.0f ? (o.taskX - o.x) / flight : 0.0f;
        t.vy = distance > 0.0f ? (o.taskY - o.y) / flight : 0.0f;
        t.vz = g * flight / 2.0f - t.z / flight;
        _thrown.push_back(t);
        if (_mixer)
            if (auto sound = gameSound(_data, def->shotSound))
                _mixer->play(sound, audio::SoundType::kEffects);
        return true;
    }
    // Used on an officer: its Health action's Health given, up to their
    // whole (inferred), with its Use Sound.
    Officer &target = _officers[size_t(o.taskOfficer)];
    if (&target != &o)
        o.direction = directionOf(target.x - o.x, target.y - o.y);
    target.health = std::min(target.maxHealth, target.health + def->health);
    if (_mixer)
        if (auto sound = gameSound(_data, def->useSound))
            _mixer->play(sound, audio::SoundType::kEffects);
    makeNoise(o.x, o.y, def->shotNoise, def->shotNoiseTime);
    return true;
}

void MapScreen::explode(const Thrown &t) {
    // Its Explosion where it lies, its Death Sound and its noise; everyone
    // within its Explosion Radial Damage, with a clear line to it if it
    // needs one, stunned for the grenade's Stun Duration if it does no
    // damage (a concussion grenade's), else hurt (inferred: whom it reaches
    // isn't traced; the team too).
    const ModelInfo &info = modelInfo(t.model);
    if (!info.explosion.empty())
        _flashes.push_back({info.explosion, t.x, t.y});
    if (_mixer)
        if (auto sound = gameSound(_data, info.deathSound))
            _mixer->play(sound, audio::SoundType::kEffects);
    makeNoise(t.x, t.y, info.noiseRadius, info.noiseAge);
    // Its model's Destroyed Action, where it went off, when it's more than
    // a noise: the EM pulse's holds the Electronic Equipment near (inferred:
    // run with no part as its $Trigger).
    if (const BinaryObject *model = modelObject(t.model))
        if (const TextObject *entity = model->object.object("Default Entity"))
            if (const TextObject *destroyed = entity->object("Destroyed Action");
                destroyed && destroyed->className != "CAddNoiseAction") {
                _hasHere = true;
                _hereX = t.x;
                _hereY = t.y;
                runAction(destroyed, -1, -1);
                _hasHere = false;
            }
    const float radius = info.explosionRadius;
    if (radius <= 0.0f)
        return;
    const bool stun = t.item->weaponDamage <= 0.0f && t.item->stunDuration > 0.0f;
    const auto reaches = [&](float x, float y) {
        return std::hypot(x - t.x, y - t.y) <= radius && (!info.explosionNeedsSight || clearSight(t.x, t.y, x, y));
    };
    for (size_t i = 0; i < _enemies.size(); ++i) {
        const Enemy &e = _enemies[i];
        if (e.alive() && _partStates[e.part].active && !_partStates[e.part].deleted && reaches(e.x, e.y))
            hurtEnemy(i, *t.item, t.thrower, stun);
    }
    for (size_t i = 0; i < _officers.size(); ++i)
        if (_officers[i].alive() && reaches(_officers[i].x, _officers[i].y))
            hurtOfficer(i, *t.item, stun);
}

void MapScreen::updateItems(float seconds) {
    for (size_t i = 0; i < _officers.size(); ++i) {
        Officer &o = _officers[i];
        if (o.task == Officer::Task::kNone)
            continue;
        if (o.task == Officer::Task::kProcess) {
            if (o.down() || runProcess(i, seconds))
                o.task = Officer::Task::kNone;
            continue;
        }
        if (o.task == Officer::Task::kItemProcess) {
            if (o.down() || o.held > 0.0f || runItemProcess(i, seconds))
                o.task = Officer::Task::kNone;
            continue;
        }
        const ItemDefinition *def = itemOf(o);
        if (!def || o.down()) {
            o.task = Officer::Task::kNone;
            continue;
        }
        // Near enough, they use it; else they walk there by the waypoints
        // (inferred). A throw needs the place in the item's range and a
        // clear line to it.
        float x = o.taskX;
        float y = o.taskY;
        bool near = false;
        if (o.task == Officer::Task::kPlace) {
            near = std::hypot(x - o.x, y - o.y) <= kUseReach;
        } else if (o.task == Officer::Task::kScan) {
            placeOf(size_t(o.taskPart), x, y);
            near = (def->range <= 0.0f || std::hypot(x - o.x, y - o.y) <= def->range) && clearSight(o.x, o.y, x, y);
        } else if (o.task == Officer::Task::kThrow) {
            near = (def->range <= 0.0f || std::hypot(x - o.x, y - o.y) <= def->range) && clearSight(o.x, o.y, x, y);
        } else {
            const Officer &target = _officers[size_t(o.taskOfficer)];
            if (!target.alive()) {
                o.task = Officer::Task::kNone;
                continue;
            }
            x = target.x;
            y = target.y;
            near = std::hypot(x - o.x, y - o.y) <= kUseReach;
        }
        if (near) {
            o.path.clear();
            if (doTask(i))
                o.task = Officer::Task::kNone;
        } else if (o.path.empty()) {
            o.path = _wayPoints.points.empty() ? std::vector<std::pair<float, float>>{{x, y}}
                                               : _wayPoints.path(o.x, o.y, x, y);
        }
    }
    _scanTyped += seconds * kScanSpeed;
    updateMines(seconds);
    // In flight it falls by its Gravity; landing, it bounces up by its
    // model's Bounce Factor of its fall, with its Bounce Sound, losing its
    // Bounce Friction of its speed along the ground; too slow a bounce and
    // it lies there (inferred).
    for (Thrown &t : _thrown) {
        t.left -= seconds;
        if (t.resting)
            continue;
        const ModelInfo &info = modelInfo(t.model);
        t.x += t.vx * seconds;
        t.y += t.vy * seconds;
        t.vz -= gravityOf(*t.item) * seconds;
        t.z += t.vz * seconds;
        if (t.z > 0.0f)
            continue;
        t.z = 0.0f;
        t.vz = -t.vz * info.bounceFactor;
        t.vx *= 1.0f - info.bounceFriction;
        t.vy *= 1.0f - info.bounceFriction;
        if (t.vz < kRestingBounce) {
            t.resting = true;
            t.vx = t.vy = t.vz = 0.0f;
            continue;
        }
        if (_mixer)
            if (auto sound = gameSound(_data, t.item->bounceSound))
                _mixer->play(sound, audio::SoundType::kEffects);
    }
    for (size_t i = 0; i < _thrown.size();) {
        if (_thrown[i].left > 0.0f) {
            ++i;
            continue;
        }
        const Thrown t = _thrown[i];
        _thrown.erase(_thrown.begin() + ptrdiff_t(i));
        explode(t);
    }
}

const TextObject *MapScreen::processFor(size_t part, const Officer &o) const {
    // One for their chosen item (its Required Selected Item), first; else
    // one to Use with none (inferred: the program's choosing isn't traced).
    // One that needs an item in the inventory needs them to have it, its
    // uses spent or not (a stasis field's to pick it up).
    const ItemDefinition *item = itemOf(o);
    const auto carries = [&](const std::string &name) {
        if (name.empty() || name == "!None")
            return true;
        for (const Officer::Held &h : o.items)
            if (h.name == name)
                return true;
        return false;
    };
    for (const bool withItem : {true, false})
        for (const TextObject *ai : _map.parts[part].activities) {
            if (ai->className != "CProcessListAI")
                continue;
            const TextObject *list = ai->object("Process");
            if (!list)
                continue;
            for (const TextField *f : list->items()) {
                const TextObject *process = f->object.get();
                if (!process || !carries(readText(process, "Required Item In Inventory")))
                    continue;
                const std::string needs = readText(process, "Required Selected Item");
                if (withItem && item && needs == item->name)
                    return process;
                if (!withItem && (needs.empty() || needs == "!None") && readText(process, "Process Type") == "Use")
                    return process;
            }
        }
    return nullptr;
}

bool MapScreen::startProcess(float x, float y) {
    if (_selected < 0)
        return false;
    Officer &o = _officers[size_t(_selected)];
    const int part = partAt(x, y);
    if (o.down() || part < 0)
        return false;
    const TextObject *process = processFor(size_t(part), o);
    if (!process)
        return false;
    o.task = Officer::Task::kProcess;
    o.taskPart = part;
    o.taskProcess = process;
    o.taskStep = 0;
    o.attacking = -1;
    o.path.clear();
    return true;
}

bool MapScreen::runProcess(size_t officer, float seconds) {
    // Its Process Activity one after another: going to a place (a
    // CGoToAI's Destination, a CGotoRadiusAI's within its Radius, a
    // CGotoTargetsAI's Target, "#1-4#" the first), and actions run, the
    // officer their instigator and the part $Trigger (inferred).
    (void)seconds;
    Officer &o = _officers[officer];
    // One for their item, a tricorder's scan, from within its Item Range
    // and in sight, walked to first (inferred: as the item's own scan).
    const ItemDefinition *def = itemOf(o);
    if (o.taskStep == 0 && def && def->range > 0.0f && readText(o.taskProcess, "Required Selected Item") == def->name) {
        float x = 0.0f;
        float y = 0.0f;
        placeOf(size_t(o.taskPart), x, y);
        if (std::hypot(x - o.x, y - o.y) > def->range || !clearSight(o.x, o.y, x, y)) {
            if (o.path.empty())
                o.path = _wayPoints.points.empty() ? std::vector<std::pair<float, float>>{{x, y}}
                                                   : _wayPoints.path(o.x, o.y, x, y);
            return false;
        }
        o.path.clear();
    }
    const TextObject *list = o.taskProcess->object("Process Activity");
    const auto steps = list ? list->items() : std::vector<const TextField *>{};
    while (o.taskStep < steps.size()) {
        const TextObject *step = steps[o.taskStep]->object.get();
        if (!step) {
            ++o.taskStep;
            continue;
        }
        const std::string &kind = step->className;
        if (kind == "CGoToAI" || kind == "CGotoRadiusAI" || kind == "CGotoTargetsAI") {
            std::string name = readText(step, kind == "CGotoTargetsAI" ? "Target" : "Destination");
            if (const size_t hash = name.find('#'); hash != std::string::npos)
                name = name.substr(0, hash) + "1";
            const std::vector<size_t> places = partsNamed(name, o.taskPart);
            if (places.empty()) {
                ++o.taskStep;
                continue;
            }
            float x = 0.0f;
            float y = 0.0f;
            placeOf(places.front(), x, y);
            int dx = 0;
            int dy = 0;
            readPoint(step, "Offset", dx, dy);
            x += float(dx);
            y += float(dy);
            const float radius =
                kind == "CGotoRadiusAI" ? std::max(kUseReach, float(step->number("Radius").value_or(0.0))) : kUseReach;
            const float ratio = kind == "CGotoRadiusAI" ? float(step->number("Y to X Ratio").value_or(1.0)) : 1.0f;
            const float ex = (o.x - x) / radius;
            const float ey = (o.y - y) / (radius * (ratio > 0.0f ? ratio : 1.0f));
            if (ex * ex + ey * ey <= 1.0f) {
                // There: its Done action (a stasis field's picks it up).
                o.path.clear();
                ++o.taskStep;
                const TextObject *done =
                    step->object("Done action") ? step->object("Done action") : step->object("Done Action");
                if (done) {
                    runAction(done, int(officer), o.taskPart);
                    if (o.task != Officer::Task::kProcess)
                        return true;
                }
                continue;
            }
            if (o.path.empty())
                o.path = _wayPoints.points.empty() ? std::vector<std::pair<float, float>>{{x, y}}
                                                   : _wayPoints.path(o.x, o.y, x, y);
            return false;
        }
        if (kind == "CActionAI") {
            if (_mixer)
                if (auto sound = gameSound(_data, readText(o.taskProcess, "Default Use Sound")))
                    _mixer->play(sound, audio::SoundType::kEffects);
            ++o.taskStep;
            runAction(step->object("Action"), int(officer), o.taskPart);
            return o.task != Officer::Task::kProcess || o.taskStep >= steps.size();
        }
        ++o.taskStep;
    }
    return true;
}

std::string MapScreen::describe(const std::string &script, const std::string &line) const {
    std::vector<uint8_t> bytes;
    if (script.empty() || _data.read("Levels/" + script + ".RDS", bytes))
        return {};
    const RdsScript rds = readRdsScript(std::string(bytes.begin(), bytes.end()));
    const std::string *text = rds.find(line);
    return text ? *text : std::string();
}

int MapScreen::partAt(float x, float y) const {
    int best = -1;
    float nearest = kPartReach;
    for (size_t i = 0; i < _map.parts.size(); ++i) {
        const MapPart &p = _map.parts[i];
        if (p.model.empty() || !shown(i))
            continue;
        float px = 0.0f;
        float py = 0.0f;
        placeOf(i, px, py);
        const float d = std::hypot(px - x, py - y);
        if (d < nearest) {
            nearest = d;
            best = int(i);
        }
    }
    return best;
}

bool MapScreen::stuns(const Officer &o) const {
    // Their weapon as they've set it.
    for (const Officer::Held &h : o.items)
        if (o.weapon && h.name == o.weapon->name)
            return h.stun;
    return o.weapon && o.weapon->stuns;
}

void MapScreen::toggleStun() {
    if (_selected < 0)
        return;
    Officer &o = _officers[size_t(_selected)];
    const ItemDefinition *def = itemOf(o);
    if (def && def->multipleSettings)
        o.items[size_t(o.item)].stun = !o.items[size_t(o.item)].stun;
}

void MapScreen::detonate() {
    // Every remote mine put down goes (inferred: whoever put it down).
    for (Mine &m : _mines)
        if (m.item->remote)
            m.setOff = true;
}

void MapScreen::updateMines(float seconds) {
    // A proximity mine arms its arm time after it's put down; an enemy in
    // its oval sets it off: its Activate Sound and noise, and it goes its
    // fuse after; as it goes, its explosion, as a grenade's, hurting by the
    // mine's Weapon Damage (the mine's model's CActionAI; inferred for its
    // running here).
    for (Mine &m : _mines) {
        m.placed += seconds;
        const ModelInfo &info = modelInfo(m.model);
        const float arm = info.armTime > 0.0f ? info.armTime : kDefaultArm;
        if (m.setOff) {
            m.fuse -= seconds;
            continue;
        }
        if (m.item->remote || m.placed < arm || info.triggerRadius <= 0.0f)
            continue;
        for (const Enemy &e : _enemies) {
            if (!e.alive() || !_partStates[e.part].active || _partStates[e.part].deleted)
                continue;
            const float dx = (e.x - m.x) / info.triggerRadius;
            const float dy = (e.y - m.y) / (info.triggerRadius * info.triggerRatio);
            if (dx * dx + dy * dy > 1.0f)
                continue;
            m.setOff = true;
            m.fuse = info.fuseAfterTrigger;
            if (_mixer)
                if (auto sound = gameSound(_data, info.activateSound))
                    _mixer->play(sound, audio::SoundType::kEffects);
            makeNoise(m.x, m.y, info.triggerNoise, info.triggerNoiseAge);
            break;
        }
    }
    for (size_t i = 0; i < _mines.size();) {
        if (!_mines[i].setOff || _mines[i].fuse > 0.0f) {
            ++i;
            continue;
        }
        const Mine m = _mines[i];
        _mines.erase(_mines.begin() + ptrdiff_t(i));
        explode({m.item, m.model, m.x, m.y, m.x, m.y, 0.0f, m.placedBy});
    }
}

} // namespace reluva::stawayteam
