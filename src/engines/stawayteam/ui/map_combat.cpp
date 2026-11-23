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
// An officer's model's 12 directions, 0 facing left, turning anticlockwise.
constexpr int kDirections = 12;
// How long a beam's shown whose weapon gives no Shot Duration (inferred).
constexpr float kBeamTime = 0.25f;
// A strike reaches about a figure's width at least (inferred).
constexpr float kStrikeReach = 30.0f;
// Of the damage the enemy taken over by the Psychic Rack takes, its holder
// takes as much, but no more than half the health they have left
// (0x43b900).
constexpr float kSharedAtMost = 0.5f;

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
    // What stands in the way: of a thrown thing, the parts that collide,
    // whatever their height; of seeing, those standing full height (their
    // Collideable and Full Height); each by its model's Bounds Poly, which
    // is drawn about where it stands (inferred).
    for (size_t i = 0; i < _map.parts.size(); ++i) {
        const MapPart &p = _map.parts[i];
        if (p.model.empty() || !p.collideable || _enemyOfPart.count(i))
            continue;
        const ModelInfo &info = modelInfo(p.model);
        if (info.footprint.size() < 3)
            continue;
        Wall w{i, p.fullHeight, {}, 1e9f, 1e9f, -1e9f, -1e9f};
        for (const auto &[x, y] : info.footprint) {
            w.points.emplace_back(p.x + x, p.y + y);
            w.x0 = std::min(w.x0, p.x + x);
            w.y0 = std::min(w.y0, p.y + y);
            w.x1 = std::max(w.x1, p.x + x);
            w.y1 = std::max(w.y1, p.y + y);
        }
        _walls.push_back(std::move(w));
    }
    for (size_t i = 0; i < _map.parts.size(); ++i)
        if (_map.parts[i].polygon.size() >= 3 && _map.parts[i].collideable)
            _bouncers.push_back(i);
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
        if (!w.full || w.x1 < lx || w.x0 > hx || w.y1 < ly || w.y0 > hy)
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

bool MapScreen::throwPasses(float x0, float y0, float x1, float y1, float &vx, float &vy) const {
    // Anything active and collideable stops it, seen or not, whatever
    // either's height: its move asks along the ground for all that's Active
    // and Collideable, not Visible nor Half or Full Height (0x4effe0,
    // 0x4ef720). So a polygon only while it's active: Mission 01's Fake
    // Grenade Bouncer poly, made so for half a second as a grenade passes
    // its trigger. Off the first side crossed, mirrored about it (inferred:
    // the bounce isn't traced).
    const auto off = [&](float ax, float ay, float bx, float by) {
        if (!crosses(x0, y0, x1, y1, ax, ay, bx, by))
            return false;
        const float length = std::hypot(bx - ax, by - ay);
        if (length <= 0.0f)
            return false;
        const float nx = -(by - ay) / length;
        const float ny = (bx - ax) / length;
        const float dot = vx * nx + vy * ny;
        vx -= 2.0f * dot * nx;
        vy -= 2.0f * dot * ny;
        return true;
    };
    const auto blocks = [&](size_t part) {
        const PartState &state = _partStates[part];
        return state.active && !state.deleted && state.collideable;
    };
    for (const Wall &w : _walls) {
        if (!blocks(w.part))
            continue;
        for (size_t k = 0; k < w.points.size(); ++k) {
            const auto [ax, ay] = w.points[k];
            const auto [bx, by] = w.points[(k + 1) % w.points.size()];
            if (off(ax, ay, bx, by))
                return false;
        }
    }
    for (size_t i : _bouncers) {
        if (!blocks(i))
            continue;
        const auto &pts = _map.parts[i].polygon;
        for (size_t k = 0; k + 1 < pts.size(); ++k)
            if (off(pts[k].first, pts[k].second, pts[k + 1].first, pts[k + 1].second))
                return false;
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
    // A beam weapon's line from its muzzle (Forward Position along the way
    // they face, Lateral Position across it, Height up) to the target, as
    // high on them (inferred), shown its Shot Duration, fading (inferred).
    const float dx = tx - x;
    const float dy = ty - y;
    const float distance = std::hypot(dx, dy);
    if (weapon.beam && distance > 0.0f) {
        const float ux = dx / distance;
        const float uy = dy / distance;
        const float mx = x + ux * weapon.forward - uy * weapon.lateral;
        const float my = y + uy * weapon.forward + ux * weapon.lateral - weapon.height;
        const float time = weapon.shotDuration > 0.0f ? weapon.shotDuration : kBeamTime;
        _beams.push_back({mx, my, tx, ty - weapon.height, weapon.beamOutside, weapon.beamMiddle, time, time});
    }
    const Animation &a = animation(model, weapon.dischargeSequence.empty() ? "Shoot1" : weapon.dischargeSequence);
    const int per = a.perDirection();
    return per > 0 && a.fps > 0.0f ? float(per) / a.fps : weapon.shotDuration;
}

float MapScreen::enhancement(int officer) const {
    // An enhanced item's Weapon Damage and Item Range are a quarter more
    // (0x5cded0, 0x5cfda2).
    const bool enhanced =
        officer >= 0 && size_t(officer) < _officers.size() && _officers[size_t(officer)].enhanced > 0.0f;
    return enhanced ? 1.25f : 1.0f;
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
        e.route.clear();
        setState(e, "Stun");
        e.target = -1;
        return;
    }
    const float damage = stun ? 0.0f : weapon.weaponDamage * enhancement(attacker);
    e.health -= damage;
    if (int(enemy) == _melded && damage > 0.0f) {
        // Its holder hurt with it (0x43b900).
        Officer &holder = _officers[size_t(_melder)];
        holder.health = std::max(0.0f, holder.health - std::min(damage, holder.health * kSharedAtMost));
        officerHit(size_t(_melder));
    }
    if (e.health <= 0.0f) {
        e.health = 0.0f;
        e.dead = 0.0f;
        setState(e, "Dead");
        e.target = -1;
        runAction(_map.parts[e.part].destroyedAction, attacker, int(e.part));
        runDeathBehaviors(e.part, attacker);
        partFell(e.part);
        return;
    }
    // One that's Target If Hit turns on whoever hit it, and attacks; one
    // hurt enough retreats. One taken over does as it's told.
    if (int(enemy) == _melded)
        return;
    if (attacker >= 0 && (readText(e.ai, "Target If Hit") == "1" || stun) && e.state != "Attack" &&
        e.state != "Retreat")
        attackOfficer(e, attacker);
    maybeRetreat(e);
}

void MapScreen::officerHit(size_t officer) {
    // With Pause Game When Hit, time stops for a hit to an officer out of
    // the view, or on a map not shown; the first time, the rules' First
    // Hit Action runs too (Data's word on pausing), the help turned off
    // for good, and time stops (0x5ca290).
    if (!_options.pauseWhenHit)
        return;
    const Officer &o = _officers[officer];
    const float left = _cameraX - float(shownW()) / 2.0f;
    const float top = _cameraY - float(shownH()) / 2.0f;
    const bool inView =
        _active && o.x >= left && o.y >= top && o.x <= left + float(shownW()) && o.y <= top + float(shownH());
    if (!inView)
        _paused = true;
    if (!_options.showPauseHelp)
        return;
    _options.showPauseHelp = false;
    (void)_options.save();
    runAction(_rules.object("First Hit Action"), int(officer), -1);
    _paused = true;
}

void MapScreen::hurtOfficer(size_t officer, const ItemDefinition &weapon, bool stun) {
    Officer &o = _officers[officer];
    if (!o.alive())
        return;
    officerHit(officer);
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
    for (Beam &b : _beams)
        b.left -= seconds;
    std::erase_if(_beams, [](const Beam &b) { return b.left <= 0.0f; });
    for (Flash &f : _flashes)
        f.time += seconds;
    std::erase_if(_flashes, [&](const Flash &f) {
        const Animation &a = animation(f.model, "Idle");
        return a.fps <= 0.0f || f.time * a.fps >= float(a.perDirection());
    });
    for (Officer &o : _officers) {
        o.reload = std::max(0.0f, o.reload - seconds);
        o.enhanced = std::max(0.0f, o.enhanced - seconds);
        if (!o.alive()) {
            o.dead += seconds;
            continue;
        }
        if (o.stunned > 0.0f) {
            o.stunned = std::max(0.0f, o.stunned - seconds);
            continue;
        }
        if (o.held > 0.0f)
            continue;
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
        // An overloaded weapon doesn't shoot: it's thrown (0x5cf080).
        bool overloaded = false;
        for (const Officer::Held &h : o.items)
            overloaded = overloaded || (o.weapon && h.name == o.weapon->name && h.overload >= 0.0f);
        if (!e.alive() || !shown(e.part) || !o.weapon || overloaded) {
            // Done with it: they stop where they are (inferred).
            o.attacking = -1;
            o.path.clear();
            continue;
        }
        // In its range and sight they stop and shoot, each Reuse Delay;
        // else they go to it by the waypoints (inferred).
        // A strike (the neck pinch's, CStrikeTargetAction) reaches only its
        // Strike Damage Radius: they walk up to it (its CStrikeTargetAI).
        const float distance = std::hypot(e.x - o.x, e.y - o.y);
        const float reach = o.weapon->dischargeAction == "CStrikeTargetAction"
                                ? std::max(o.weapon->strikeRadius, kStrikeReach)
                                : o.weapon->range * enhancement(int(&o - _officers.data()));
        if (distance <= reach && clearSight(o.x, o.y, e.x, e.y)) {
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
                // Up: its Stun state's Next State, Investigate (looking
                // about where it is) or Recharge, else its patrol (inferred).
                const TextObject *stun = e.ai->object("Stun");
                const std::string next = stun ? readText(stun, "Next State") : std::string();
                if (stateKey(next) == "Investigate")
                    investigate(e, e.x, e.y);
                else
                    setState(e, next.empty() || next == "Any" ? "Patrol" : next);
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
