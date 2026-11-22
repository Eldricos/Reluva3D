// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Fighting on a map: what blocks sight, the weapons, shots, damage and
// death (docs/engines/stawayteam/systems/maps.md). The program's own
// weapon code isn't traced: what's here is read from the item definitions'
// and the models' fields.

#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

constexpr float kPi = 3.14159265f;
// Hit points for a character whose model gives none (inferred).
constexpr float kDefaultHealth = 100.0f;
// An officer's model's 12 directions, 0 facing left, turning anticlockwise.
constexpr int kDirections = 12;

// Which side of the line a-b point c is on.
float side(float ax, float ay, float bx, float by, float cx, float cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

bool crosses(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
    const float d1 = side(ax, ay, bx, by, cx, cy);
    const float d2 = side(ax, ay, bx, by, dx, dy);
    const float d3 = side(cx, cy, dx, dy, ax, ay);
    const float d4 = side(cx, cy, dx, dy, bx, by);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

int directionOf(float dx, float dy) {
    const int d = 6 - int(std::lround(std::atan2(dy, dx) / (kPi / 6.0f)));
    return (d % kDirections + kDirections) % kDirections;
}

} // namespace

const ModelInfo &MapScreen::modelInfo(const std::string &model) {
    auto it = _modelInfos.find(model);
    if (it == _modelInfos.end()) {
        ModelInfo info;
        (void)loadModelInfo(_data, model, info);
        it = _modelInfos.emplace(model, std::move(info)).first;
    }
    return it->second;
}

const ItemDefinition *MapScreen::weaponOf(const std::vector<std::string> &items) const {
    // The first that shoots, or else the first that strikes, as a Targ's
    // jaws or the neural disrupter (inferred: which a character uses isn't
    // traced). Mines and grenades are thrown or laid, and aren't here yet.
    const ItemDefinition *strikes = nullptr;
    for (const std::string &name : items) {
        const auto it = _items.find(name);
        if (it == _items.end() || it->second.weaponDamage <= 0.0f)
            continue;
        const std::string &action = it->second.dischargeAction;
        if (action == "CShootEnergyWeaponAction" || action == "CShootProjectileWeaponAction")
            return &it->second;
        if (action == "CStrikeTargetAction" && !strikes)
            strikes = &it->second;
    }
    return strikes;
}

void MapScreen::setupCombat() {
    _items = loadItems(_data);
    // What stands in the way of seeing: the parts that collide and stand
    // full height (their Collideable and Full Height), by their model's
    // Bounds Poly, which is drawn about where it stands (inferred).
    for (size_t i = 0; i < _map.parts.size(); ++i) {
        const MapPart &p = _map.parts[i];
        if (p.model.empty() || !p.collideable || !p.fullHeight || _enemyOfPart.count(i))
            continue;
        const ModelInfo &info = modelInfo(p.model);
        if (info.footprint.size() < 3)
            continue;
        Wall w{i, {}, 1e9f, 1e9f, -1e9f, -1e9f};
        for (const auto &[x, y] : info.footprint) {
            w.points.emplace_back(p.x + x, p.y + y);
            w.x0 = std::min(w.x0, p.x + x);
            w.y0 = std::min(w.y0, p.y + y);
            w.x1 = std::max(w.x1, p.x + x);
            w.y1 = std::max(w.y1, p.y + y);
        }
        _walls.push_back(std::move(w));
    }
    // The characters' health is their model's Hit Points; an enemy's
    // weapon is in the inventory the map gives it, or else its model's.
    for (Enemy &e : _enemies) {
        const MapPart &p = _map.parts[e.part];
        const ModelInfo &info = modelInfo(p.model);
        e.maxHealth = info.hitPoints > 0 ? float(info.hitPoints) : kDefaultHealth;
        e.health = e.maxHealth;
        e.weapon = weaponOf(p.inventory.empty() ? info.inventory : p.inventory);
        // The map can set its weapon to stun (the Borg's guns).
        e.stuns = p.stuns || (e.weapon && e.weapon->stuns);
    }
    for (Officer &o : _officers) {
        const ModelInfo &info = modelInfo(_team[o.member].bodyModel);
        o.maxHealth = info.hitPoints > 0 ? float(info.hitPoints) : kDefaultHealth;
        o.health = o.maxHealth;
        std::vector<std::string> items;
        for (const TeamMember::Item &item : _team[o.member].inventory)
            items.push_back(item.name);
        o.weapon = weaponOf(items);
    }
}

bool MapScreen::clearSight(float x0, float y0, float x1, float y1) const {
    const float lx = std::min(x0, x1);
    const float hx = std::max(x0, x1);
    const float ly = std::min(y0, y1);
    const float hy = std::max(y0, y1);
    for (const Wall &w : _walls) {
        if (w.x1 < lx || w.x0 > hx || w.y1 < ly || w.y0 > hy)
            continue;
        if (!shown(w.part) || !_partStates[w.part].collideable)
            continue;
        const size_t n = w.points.size();
        for (size_t k = 0; k < n; ++k) {
            const auto [ax, ay] = w.points[k];
            const auto [bx, by] = w.points[(k + 1) % n];
            if (crosses(x0, y0, x1, y1, ax, ay, bx, by))
                return false;
        }
    }
    return true;
}

int MapScreen::enemyAt(float x, float y) const {
    // About its figure, as an officer's (inferred).
    for (size_t i = 0; i < _enemies.size(); ++i) {
        const Enemy &e = _enemies[i];
        if (!e.alive() || !shown(e.part))
            continue;
        if (std::abs(e.x - x) <= 12.0f && y >= e.y - 20.0f && y <= e.y + 24.0f)
            return int(i);
    }
    return -1;
}

void MapScreen::attack(int enemy) {
    if (enemy < 0 || size_t(enemy) >= _enemies.size())
        return;
    for (Officer &o : _officers)
        if (o.chosen && o.alive() && o.weapon) {
            o.attacking = enemy;
            o.path.clear();
        }
}

float MapScreen::shoot(const ItemDefinition &weapon, const std::string &model, float x, float y, float tx, float ty) {
    // Its Shot Sound; its Shot Flash where the shooter stands and its Hit
    // Flash on the one hit (inferred: where on them, and the beam between,
    // aren't traced).
    if (_mixer)
        if (auto sound = gameSound(_data, weapon.shotSound))
            _mixer->play(sound, audio::SoundType::kEffects);
    if (!weapon.shotFlash.empty())
        _flashes.push_back({weapon.shotFlash, x, y});
    if (!weapon.hitFlash.empty())
        _flashes.push_back({weapon.hitFlash, tx, ty});
    const Animation &a = animation(model, weapon.dischargeSequence.empty() ? "Shoot1" : weapon.dischargeSequence);
    const int per = a.perDirection();
    return per > 0 && a.fps > 0.0f ? float(per) / a.fps : weapon.shotDuration;
}

void MapScreen::hurtEnemy(size_t enemy, const ItemDefinition &weapon, int attacker, bool stun) {
    Enemy &e = _enemies[enemy];
    if (!e.alive())
        return;
    if (const TextObject *action = e.ai->object("Damage Action"))
        runAction(action, attacker, int(e.part));
    // A weapon set to stun puts it down for the weapon's Stun Duration,
    // then back to its patrol, unless its Stun State is Attack (145 of the
    // maps' enemies: the Borg's), which it then does (inferred).
    const bool stunnable = readText(e.ai, "Stun State") != "Attack";
    if (stun && stunnable) {
        e.stunned = weapon.stunDuration;
        e.state = "Stun";
        e.target = -1;
        e.stateTime = 0.0f;
        e.route.clear();
        return;
    }
    if (!stun)
        e.health -= weapon.weaponDamage;
    if (e.health <= 0.0f) {
        e.health = 0.0f;
        e.dead = 0.0f;
        e.state = "Dead";
        e.target = -1;
        runAction(_map.parts[e.part].destroyedAction, attacker, int(e.part));
        return;
    }
    // One that's Target If Hit turns on whoever hit it, and attacks; one
    // hurt enough retreats.
    if (attacker >= 0 && (readText(e.ai, "Target If Hit") == "1" || stun) && e.state != "Attack" &&
        e.state != "Retreat")
        attackOfficer(e, attacker);
    maybeRetreat(e);
}

void MapScreen::hurtOfficer(size_t officer, const ItemDefinition &weapon, bool stun) {
    Officer &o = _officers[officer];
    if (!o.alive())
        return;
    if (stun) {
        // Down for the weapon's Stun Duration, their orders dropped
        // (inferred).
        o.stunned = std::max(o.stunned, weapon.stunDuration);
        o.path.clear();
        o.attacking = -1;
        return;
    }
    o.health = std::max(0.0f, o.health - weapon.weaponDamage);
    if (o.health > 0.0f)
        return;
    // Fallen: out of the player's hands, and failing whatever objective
    // protects them.
    o.dead = 0.0f;
    o.path.clear();
    o.attacking = -1;
    if (o.chosen) {
        o.chosen = false;
        _selected = -1;
        for (size_t i = 0; i < _officers.size() && _selected < 0; ++i)
            if (_officers[i].chosen)
                _selected = int(i);
    }
    officerFell(officer);
}

void MapScreen::updateCombat(float seconds) {
    for (Flash &f : _flashes)
        f.time += seconds;
    std::erase_if(_flashes, [&](const Flash &f) {
        const Animation &a = animation(f.model, "Idle");
        return a.fps <= 0.0f || f.time * a.fps >= float(a.perDirection());
    });
    for (Officer &o : _officers) {
        o.reload = std::max(0.0f, o.reload - seconds);
        if (!o.alive()) {
            o.dead += seconds;
            continue;
        }
        if (o.stunned > 0.0f) {
            o.stunned = std::max(0.0f, o.stunned - seconds);
            continue;
        }
        if (o.shooting > 0.0f) {
            o.shooting += seconds;
            const Animation &a = animation(_team[o.member].bodyModel, o.discharge);
            if (a.fps <= 0.0f || o.shooting * a.fps >= float(a.perDirection()))
                o.shooting = 0.0f;
        }
        if (o.attacking < 0)
            continue;
        const size_t i = size_t(o.attacking);
        const Enemy &e = _enemies[i];
        if (!e.alive() || !shown(e.part) || !o.weapon) {
            // Done with it: they stop where they are (inferred).
            o.attacking = -1;
            o.path.clear();
            continue;
        }
        // In its range and sight they stop and shoot, each Reuse Delay;
        // else they go to it by the waypoints (inferred).
        const float distance = std::hypot(e.x - o.x, e.y - o.y);
        if (distance <= o.weapon->range && clearSight(o.x, o.y, e.x, e.y)) {
            o.path.clear();
            o.direction = directionOf(e.x - o.x, e.y - o.y);
            if (o.reload <= 0.0f) {
                o.reload = o.weapon->reuseDelay;
                o.discharge = o.weapon->dischargeSequence.empty() ? "Shoot1" : o.weapon->dischargeSequence;
                shoot(*o.weapon, _team[o.member].bodyModel, o.x, o.y, e.x, e.y);
                makeNoise(o.x, o.y, o.weapon->shotNoise, o.weapon->shotNoiseTime);
                o.shooting = 1e-6f;
                hurtEnemy(i, *o.weapon, int(&o - _officers.data()), stuns(o));
            }
        } else if (o.path.empty()) {
            o.path = _wayPoints.points.empty() ? std::vector<std::pair<float, float>>{{e.x, e.y}}
                                               : _wayPoints.path(o.x, o.y, e.x, e.y);
        }
    }
    for (Enemy &e : _enemies) {
        e.reload = std::max(0.0f, e.reload - seconds);
        if (!e.alive()) {
            e.dead += seconds;
            continue;
        }
        if (e.shooting > 0.0f) {
            e.shooting += seconds;
            const Animation &a = animation(_map.parts[e.part].model, "Shoot1");
            if (a.fps <= 0.0f || e.shooting * a.fps >= float(a.perDirection()))
                e.shooting = 0.0f;
        }
        if (e.stunned > 0.0f) {
            e.stunned = std::max(0.0f, e.stunned - seconds);
            if (e.stunned <= 0.0f) {
                e.state = "Patrol";
                e.stateTime = 0.0f;
            }
        }
    }
}

std::string MapScreen::combatSequence(float shooting, float dead, float stunned, int &frame, const std::string &model,
                                      const std::string &discharge) {
    // Fallen, its Death once and then Dead; stunned, as fallen till it's
    // up (inferred); shooting, its Shoot1 through.
    const auto into = [&](const char *sequence, float time) {
        const Animation &a = animation(model, sequence);
        frame = std::min(int(time * a.fps), std::max(0, a.perDirection() - 1));
    };
    if (dead >= 0.0f || stunned > 0.0f) {
        const Animation &death = animation(model, "Death");
        const float time = dead >= 0.0f ? dead : 0.0f;
        if (dead >= 0.0f && death.fps > 0.0f && time * death.fps < float(death.perDirection())) {
            into("Death", time);
            return "Death";
        }
        if (dead < 0.0f) {
            into("Death", 1e6f);
            return "Death";
        }
        frame = 0;
        return "Dead";
    }
    if (shooting > 0.0f) {
        into(discharge.c_str(), shooting);
        return discharge;
    }
    return {};
}

} // namespace reluva::stawayteam
