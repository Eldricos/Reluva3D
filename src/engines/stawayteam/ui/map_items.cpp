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
// A grenade's fuse when its model doesn't give it (inferred).
constexpr float kDefaultFuse = 3.0f;
// A thrown thing's Gravity in pixels a second each second, as a falling
// particle's (0x5f91bf), and the Gravity AI's own when it gives none.
constexpr float kGravityScale = ParticleSystem::kGravityScale;
constexpr float kDefaultGravity = 17.0f;
// Near enough an officer to use an item on them: about the items' Strike
// Damage Radius (20) and a figure's width (inferred).
constexpr float kUseReach = 30.0f;
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
    // The Psychic Rack on an enemy: one it can't take over, its Fail Sound
    // (0x5d5304); else taken over, walked into range first.
    if (def->psyche) {
        const int enemy = enemyAt(x, y);
        if (enemy < 0)
            return false;
        if (!canMeld(size_t(_selected), size_t(enemy))) {
            if (_mixer)
                if (auto sound = gameSound(_data, def->failSound))
                    _mixer->play(sound, audio::SoundType::kEffects);
            return true;
        }
        o.task = Officer::Task::kMeld;
        o.taskPart = enemy;
        o.attacking = -1;
        o.path.clear();
        return true;
    }
    // An overloaded item is thrown, whatever it is (0x5cf3c0).
    if (throws(*def) || o.items[size_t(o.item)].overload >= 0.0f) {
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
    if (def->health > 0.0f || def->enhanceTime > 0.0f) {
        // An officer healed or their weapons enhanced; else a part with a
        // process for it (Mission 13's hostages revived, a Hypospray Use;
        // inferred).
        const int target = officerAt(x, y);
        if (target < 0)
            return startProcess(x, y);
        if (target == _selected && !def->canUseOnSelf)
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
        // Thrown from their hand's Height toward the place, at 45 degrees up,
        // as fast as would land it there from the ground, by its Gravity,
        // times its Velocity Factor (0x5f8fc0): the game's 0.725 lands it
        // about half way, to bounce on. No faster than its model's Max
        // Velocity (inferred). It goes off its Shot Duration after it's
        // thrown.
        o.direction = directionOf(o.taskX - o.x, o.taskY - o.y);
        // Overloaded, it's its Overload Projectile, going off as its
        // overload runs out (inferred), and it's gone from them.
        const float overload = o.items[size_t(o.item)].overload;
        const std::string &projectile = overload >= 0.0f ? def->overloadProjectile : def->projectile;
        if (overload >= 0.0f && !def->overloadSequence.empty())
            o.discharge = def->overloadSequence;
        const ModelInfo &info = modelInfo(projectile);
        const float fuse = overload >= 0.0f ? overload : info.fuse > 0.0f ? info.fuse : kDefaultFuse;
        Thrown t{def, projectile, o.x, o.y, o.taskX, o.taskY, fuse, int(officer)};
        ItemDefinition thrownAs = *def;
        if (overload >= 0.0f) {
            thrownAs.gravity = def->overloadGravity;
            thrownAs.velocityFactor = def->overloadVelocityFactor;
        }
        t.gravity = gravityOf(thrownAs);
        const float distance = std::hypot(o.taskX - o.x, o.taskY - o.y);
        float speed = std::sqrt(t.gravity * distance) * thrownAs.velocityFactor;
        if (info.maxVelocity > 0.0f)
            speed = std::min(speed, info.maxVelocity);
        const float along = speed * std::cos(kPi / 4.0f);
        t.z = def->height;
        t.vx = distance > 0.0f ? (o.taskX - o.x) / distance * along : 0.0f;
        t.vy = distance > 0.0f ? (o.taskY - o.y) / distance * along : 0.0f;
        t.vz = speed * std::sin(kPi / 4.0f);
        t.id = ++_thrownIds;
        _thrown.push_back(t);
        if (_mixer)
            if (auto sound = gameSound(_data, def->shotSound))
                _mixer->play(sound, audio::SoundType::kEffects);
        if (overload >= 0.0f)
            removeItem(o, size_t(o.item));
        return true;
    }
    // Used on an officer: its Health action's Health given, less the more
    // Stamina they have, the hurt they've taken going down to none, so up
    // to their whole (0x4d6cd9, 0x4a99a0, 0x4951f0); or their weapons
    // enhanced for its time, from then whatever was left (0x5cf050); with
    // its Use Sound.
    Officer &target = _officers[size_t(o.taskOfficer)];
    if (&target != &o)
        o.direction = directionOf(target.x - o.x, target.y - o.y);
    target.health = std::min(target.maxHealth, target.health + def->health / staminaFactor(size_t(o.taskOfficer)));
    if (def->enhanceTime > 0.0f)
        target.enhanced = def->enhanceTime;
    if (_mixer)
        if (auto sound = gameSound(_data, def->useSound))
            _mixer->play(sound, audio::SoundType::kEffects);
    makeNoise(o.x, o.y, def->shotNoise, def->shotNoiseTime);
    return true;
}

void MapScreen::explode(const Thrown &t) {
    // Its Explosion where it lies, its Death Sound and its noise; every
    // character within its Explosion Radial Damage, an oval on the ground
    // as the map's foreshortening has it (0x4883bf), the team too (only
    // Active asked of them, 0x4883b8), with a clear line to it if it
    // Requires LOS For Damage: hurt by twice its Explosion Radial Damage at
    // the middle, less the further off, to none at its edge (0x49676b,
    // 0x488357), whatever the item's own Weapon Damage; its model's Collide
    // Damage Type goes with it, none a stun (0x495220).
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
    const auto off = [&](float x, float y) { return std::hypot(x - t.x, (y - t.y) / ParticleSystem::kYToX); };
    const auto reaches = [&](float x, float y) {
        return off(x, y) <= radius && (!info.explosionNeedsSight || clearSight(t.x, t.y, x, y));
    };
    const auto at = [&](float x, float y) {
        ItemDefinition weakened = *t.item;
        weakened.weaponDamage = 2.0f * radius * (1.0f - off(x, y) / radius);
        return weakened;
    };
    for (size_t i = 0; i < _enemies.size(); ++i) {
        const Enemy &e = _enemies[i];
        if (e.alive() && _partStates[e.part].active && !_partStates[e.part].deleted && reaches(e.x, e.y))
            hurtEnemy(i, at(e.x, e.y), t.thrower, false);
    }
    for (size_t i = 0; i < _officers.size(); ++i)
        if (_officers[i].alive() && reaches(_officers[i].x, _officers[i].y))
            hurtOfficer(i, at(_officers[i].x, _officers[i].y), false);
}

bool MapScreen::inItemRange(const ItemDefinition &item, float fromX, float fromY, float x, float y, float range) const {
    // Its Item Range on the ground, the map's foreshortening undone across
    // it (0x5c6d78); a clear line only for an item that Needs LOS: none of
    // the game's (0x5c6c4e).
    const float across = (y - fromY) / ParticleSystem::kYToX;
    if (range > 0.0f && std::hypot(x - fromX, across) > range)
        return false;
    return !item.needLos || clearSight(fromX, fromY, x, y);
}

void MapScreen::removeItem(Officer &o, size_t index) {
    o.items.erase(o.items.begin() + std::ptrdiff_t(index));
    if (o.item == int(index))
        o.item = -1;
    else if (o.item > int(index))
        --o.item;
    std::fill(o.shown.begin(), o.shown.end(), 0);
    std::vector<std::string> names;
    for (const Officer::Held &h : o.items)
        names.push_back(h.name);
    o.weapon = weaponOf(names);
}

void MapScreen::updateOverloads(float seconds) {
    // Counting down, it beeps (Weapons/Overload.wav), the beeps closer as
    // it runs out: the next after the part of its whole left, in seconds;
    // run out, its Overload Projectile goes off where its holder is, and
    // it's gone (0x5cf235).
    for (size_t i = 0; i < _officers.size(); ++i) {
        Officer &o = _officers[i];
        for (size_t k = 0; k < o.items.size(); ++k) {
            Officer::Held &h = o.items[k];
            if (h.overload < 0.0f)
                continue;
            h.overload -= seconds;
            const auto def = _items.find(h.name);
            if (h.overload < 0.0f) {
                if (def != _items.end() && !def->second.overloadProjectile.empty())
                    explode({&def->second, def->second.overloadProjectile, o.x, o.y, o.x, o.y, 0.0f, int(i)});
                removeItem(o, k);
                break;
            }
            h.beep -= seconds;
            if (h.beep <= 0.0f) {
                h.beep = h.overload / kOverloadTime;
                if (_mixer)
                    if (auto sound = gameSound(_data, "Weapons/Overload.wav"))
                        _mixer->play(sound, audio::SoundType::kEffects);
            }
        }
    }
}

void MapScreen::updateItems(float seconds) {
    updateOverloads(seconds);
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
        // (inferred). A throw or scan needs the place in the item's range.
        float x = o.taskX;
        float y = o.taskY;
        bool near = false;
        if (o.task == Officer::Task::kPlace) {
            near = std::hypot(x - o.x, y - o.y) <= kUseReach;
        } else if (o.task == Officer::Task::kScan) {
            placeOf(size_t(o.taskPart), x, y);
            near = inItemRange(*def, o.x, o.y, x, y, def->range);
        } else if (o.task == Officer::Task::kMeld) {
            // Taken over from within its Item Range (inferred: walked to
            // first, as a scan's).
            const Enemy &e = _enemies[size_t(o.taskPart)];
            if (!e.alive()) {
                o.task = Officer::Task::kNone;
                continue;
            }
            x = e.x;
            y = e.y;
            if (inItemRange(*def, o.x, o.y, x, y, def->range)) {
                o.path.clear();
                o.task = Officer::Task::kNone;
                if (canMeld(i, size_t(o.taskPart)))
                    startMeld(i, size_t(o.taskPart));
                continue;
            }
        } else if (o.task == Officer::Task::kThrow) {
            const float range = def->range * enhancement(int(&o - _officers.data()));
            near = inItemRange(*def, o.x, o.y, x, y, range);
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
            o.path = route(o, x, y);
        }
    }
    _scanTyped += seconds * kScanSpeed;
    updateMines(seconds);
    // In flight it falls by its Gravity; landing, it bounces up by its
    // model's Bounce Factor of its fall, with its Bounce Sound, losing its
    // Bounce Friction of its speed along the ground; a bounce slower than
    // its Min Bounce is none, and less speed along the ground than its Min
    // Bounce Friction stops it there (0x51fc10).
    for (Thrown &t : _thrown) {
        t.left -= seconds;
        if (t.resting)
            continue;
        const ModelInfo &info = modelInfo(t.model);
        // A wall or collideable polygon in the way turns it, with its
        // bounce's sound (inferred).
        if (throwPasses(t.x, t.y, t.x + t.vx * seconds, t.y + t.vy * seconds, t.vx, t.vy)) {
            t.x += t.vx * seconds;
            t.y += t.vy * seconds;
        } else if (_mixer) {
            if (auto sound = gameSound(_data, t.item->bounceSound))
                _mixer->play(sound, audio::SoundType::kEffects);
        }
        t.vz -= t.gravity * seconds;
        t.z += t.vz * seconds;
        if (t.z > 0.0f)
            continue;
        t.z = 0.0f;
        const float fall = -t.vz;
        t.vz = fall * info.bounceFactor;
        t.vx *= 1.0f - info.bounceFriction;
        t.vy *= 1.0f - info.bounceFriction;
        if (std::hypot(t.vx, t.vy) < info.minBounceFriction)
            t.vx = t.vy = 0.0f;
        if (t.vz < info.minBounce)
            t.vz = 0.0f;
        // Stopped along the ground, and bouncing no higher than a frame's
        // fall, it lies there (inferred).
        if (t.vx == 0.0f && t.vy == 0.0f && t.vz <= t.gravity * seconds) {
            t.resting = true;
            t.vz = 0.0f;
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

const TextObject *MapScreen::processOfType(size_t part, const std::string &type) const {
    for (const TextObject *ai : _map.parts[part].activities) {
        const TextObject *list = ai->className == "CProcessListAI" ? ai->object("Process") : nullptr;
        for (const TextField *f : list ? list->items() : std::vector<const TextField *>{})
            if (f->object && (type.empty() || readText(f->object.get(), "Process Type") == type))
                return f->object.get();
    }
    return nullptr;
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
    o.taskX = x;
    o.taskY = y;
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
    // One for their item, a tricorder's scan, from within its Item Range,
    // walked to first (inferred: as the item's own scan).
    const ItemDefinition *def = itemOf(o);
    if (o.taskStep == 0 && def && def->range > 0.0f && readText(o.taskProcess, "Required Selected Item") == def->name) {
        // A polygon's is where it was pointed at, having no place of its own.
        float x = o.taskX;
        float y = o.taskY;
        if (!_map.parts[size_t(o.taskPart)].model.empty())
            placeOf(size_t(o.taskPart), x, y);
        if (!inItemRange(*def, o.x, o.y, x, y, def->range)) {
            if (o.path.empty())
                o.path = route(o, x, y);
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
                o.path = route(o, x, y);
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
    const auto processes = [](const MapPart &p) -> const TextObject * {
        for (const TextObject *a : p.activities)
            if (a->className == "CProcessListAI")
                return a;
        return nullptr;
    };
    // Many parts carry a CProcessListAI with no processes in it.
    const auto hasProcesses = [&](const MapPart &p) {
        const TextObject *list = processes(p);
        const TextObject *items = list ? list->object("Process") : nullptr;
        return items && !items->items().empty();
    };
    // One whose processes name a Hot Spot is under the pointer only in that
    // part's shape, the pick dropped if it isn't (0x5c0396): Mission 10's
    // holo control panel by its polygon (inferred: the polygon taken
    // first, as the panel's used by it beside it).
    for (size_t i = 0; i < _map.parts.size(); ++i) {
        const MapPart &p = _map.parts[i];
        const TextObject *list = p.model.empty() || !shown(i) ? nullptr : processes(p);
        const std::string spot = list ? readText(list, "Hot Spot") : std::string();
        const std::vector<size_t> found = spot.empty() ? std::vector<size_t>{} : partsNamed(spot);
        if (!found.empty() && _map.parts[found.front()].polygon.size() >= 3 &&
            inside(_map.parts[found.front()].polygon, x, y))
            return int(i);
    }
    const auto spotted = [&](size_t i) {
        const TextObject *list = processes(_map.parts[i]);
        const std::string spot = list ? readText(list, "Hot Spot") : std::string();
        return !spot.empty() && !partsNamed(spot).empty();
    };
    // Else the picture drawn on top there, or at one of the 8 pixels round
    // it, in the program's order (0x649ac0): first one with a process list,
    // then any (0x5cb520, 0x5cb4b0); a picture's there by its strip's
    // bounds (0x4834d0) and its pixel's not being see-through (inferred:
    // the image's own test isn't traced).
    constexpr std::array<std::array<int, 2>, 9> kAround = {
        {{0, 0}, {0, -1}, {-1, 0}, {1, 0}, {-1, -1}, {1, -1}, {-1, 1}, {0, 1}, {1, 1}}};
    const int px = int(std::floor(x));
    const int py = int(std::floor(y));
    const auto on = [](const Drawn &d, int at, int up) {
        if (at < d.clipLeft || at >= d.clipRight)
            return false;
        const int u = at - d.x + d.picture->originX;
        const int v = up - d.y + d.picture->originY;
        const image::Image &img = d.picture->image;
        return u >= 0 && v >= 0 && u < img.w && v < img.h && img.pixel(u, v)[3] != 0;
    };
    int best = -1;
    for (const bool needList : {true, false}) {
        for (const auto &[dx, dy] : kAround) {
            for (auto d = _drawn.rbegin(); d != _drawn.rend() && best < 0; ++d)
                if (d->part < _map.parts.size() && shown(d->part) && (!needList || processes(_map.parts[d->part])) &&
                    on(*d, px + dx, py + dy))
                    best = int(d->part);
            if (best >= 0)
                break;
        }
        if (best >= 0)
            break;
    }
    if (best >= 0 && spotted(size_t(best)))
        best = -1;
    // Else, or over a model with none, a polygon with processes it's in:
    // Mission 16's gas, scanned by the tricorder, over its carpets
    // (inferred: which comes first, a model or a polygon, isn't traced).
    if (best < 0 || !hasProcesses(_map.parts[size_t(best)]))
        for (size_t i = 0; i < _map.parts.size(); ++i) {
            const MapPart &p = _map.parts[i];
            if (p.model.empty() && p.polygon.size() >= 3 && hasProcesses(p) && shown(i) && inside(p.polygon, x, y))
                return int(i);
        }
    return best;
}

void MapScreen::hoverAt(float x, float y) {
    // The process for their chosen item there, or the one to Use with none,
    // with Execute if Hovering, runs with nothing clicked (0x43cd10,
    // 0x43d020): Mission 16's gas, scanned as the tricorder's moved over
    // it. Once as the pointer comes onto the part (inferred).
    const int part = partAt(x, y);
    if (part == _hovered)
        return;
    _hovered = part;
    if (part < 0 || _selected < 0)
        return;
    const Officer &o = _officers[size_t(_selected)];
    if (o.down() || (o.task == Officer::Task::kProcess && o.taskPart == part))
        return;
    const TextObject *process = processFor(size_t(part), o);
    if (process && process->number("Execute if Hovering").value_or(0.0) != 0.0)
        startProcess(x, y);
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

void MapScreen::toggleOverload() {
    if (_selected < 0)
        return;
    Officer &o = _officers[size_t(_selected)];
    const ItemDefinition *def = itemOf(o);
    if (!def || !def->overloadable)
        return;
    Officer::Held &h = o.items[size_t(o.item)];
    h.overload = h.overload >= 0.0f ? -1.0f : kOverloadTime;
    h.beep = 0.0f;
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
