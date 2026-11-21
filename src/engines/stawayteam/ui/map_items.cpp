// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The officers' items: the one each has chosen, and using it: a grenade
// thrown, a hypospray used on an officer (docs/engines/stawayteam/systems/
// items.md). The program's own item code isn't traced: what's here is read
// from the item definitions' and their models' fields.

#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

constexpr float kPi = 3.14159265f;
// A grenade's flight and fuse when its model doesn't give them (inferred).
constexpr float kDefaultThrowSpeed = 400.0f;
constexpr float kDefaultFuse = 3.0f;
// Near enough an officer to use an item on them: about the items' Strike
// Damage Radius (20) and a figure's width (inferred).
constexpr float kUseReach = 30.0f;

int directionOf(float dx, float dy) {
    const int d = 6 - int(std::lround(std::atan2(dy, dx) / (kPi / 6.0f)));
    return (d % 12 + 12) % 12;
}

bool shoots(const ItemDefinition &d) {
    return d.weaponDamage > 0.0f &&
           (d.dischargeAction == "CShootEnergyWeaponAction" || d.dischargeAction == "CShootProjectileWeaponAction" ||
            d.dischargeAction == "CStrikeTargetAction");
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
            o.items.push_back({held.name, held.count > 0 ? held.count : rounds > 0 ? rounds : -1});
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
    int &count = o.items[size_t(o.item)].count;
    if (count == 0)
        return true;
    if (count > 0)
        --count;
    o.discharge = def->dischargeSequence.empty() ? "Shoot1" : def->dischargeSequence;
    o.shooting = 1e-6f;
    if (o.task == Officer::Task::kThrow) {
        // Thrown from where they stand at the place: it flies there at its
        // model's Max Velocity, and goes off its Shot Duration after it's
        // thrown (inferred: the throw's arc and bounces aren't there).
        o.direction = directionOf(o.taskX - o.x, o.taskY - o.y);
        const ModelInfo &info = modelInfo(def->projectile);
        _thrown.push_back({def, def->projectile, o.x, o.y, o.taskX, o.taskY,
                           info.fuse > 0.0f ? info.fuse : kDefaultFuse, int(officer)});
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
        if (o.task == Officer::Task::kThrow) {
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
    for (Thrown &t : _thrown) {
        const float speed = [&] {
            const float v = modelInfo(t.model).maxVelocity;
            return v > 0.0f ? v : kDefaultThrowSpeed;
        }();
        const float dx = t.toX - t.x;
        const float dy = t.toY - t.y;
        const float distance = std::hypot(dx, dy);
        const float step = std::min(distance, speed * seconds);
        if (distance > 0.0f) {
            t.x += dx / distance * step;
            t.y += dy / distance * step;
        }
        t.left -= seconds;
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

} // namespace reluva::stawayteam
