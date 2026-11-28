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
// The noise a Team Member's bolt makes where it meets something (0x535ef4).
constexpr float kHitNoise = 200.0f;
constexpr float kHitNoiseAge = 1.5f;
// A shot meets a character this near its line, on the ground
// unforeshortened: their outline, about 9 either side (inferred).
constexpr float kHitReach = 9.0f;
// An energy weapon's bolt's speed, pixels a second (0x535c19).
constexpr float kBoltSpeed = 1500.0f;
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

const ModelInfo &MapScreen::modelInfo(const std::string &model) const {
    auto it = _modelInfos.find(model);
    if (it == _modelInfos.end()) {
        ModelInfo info;
        (void)loadModelInfo(_data, model, info);
        it = _modelInfos.emplace(model, std::move(info)).first;
    }
    return it->second;
}

const ItemDefinition *MapScreen::weaponOf(const std::vector<std::string> &items) const {
    // The first of its inventory whose Inventory Type is Primary, as an
    // enemy attacking takes (0x42945a, 0x418d70); without one, the first
    // that shoots, or else the first that strikes (inferred), which for
    // all the maps' enemies is the same.
    for (const std::string &name : items) {
        const auto it = _items.find(name);
        if (it != _items.end() && it->second.type == "Primary")
            return &it->second;
    }
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
        Wall w{i, p.fullHeight, p.halfHeight, {}, 1e9f, 1e9f, -1e9f, -1e9f};
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
        // What's Active, Collideable and Full Height, seen or not: sight's
        // line test asks for those, not Visible (0x435024, 0x4ef771).
        const PartState &state = _partStates[w.part];
        if (!state.active || state.deleted || !state.collideable)
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

bool MapScreen::throwPasses(float x0, float y0, float x1, float y1, float &vx, float &vy, size_t *struck,
                            const ModelInfo *model) const {
    // Anything active and collideable stops it, seen or not, whatever
    // either's height: its move asks along the ground for all that's Active
    // and Collideable, not Visible nor Half or Full Height (0x4effe0,
    // 0x4ef720). So a polygon only while it's active: Mission 01's Fake
    // Grenade Bouncer poly, made so for half a second as a grenade passes
    // its trigger. Off the first side crossed (0x4efb20): what of its
    // speed goes into the side is turned back by its model's Bounce Factor,
    // none if no more than its Min Bounce; what goes along the side loses
    // its Bounce Friction, none left if no more than its Min Bounce
    // Friction (0x4efe08, 0x498193). Without a model, 1, 0.1, 0 and 0.1
    // (0x4981cc). The characters aren't in its way: each side
    // of a collision may turn it down (0x4ef85b, 0x4ef87d), and a grenade's
    // CIgnoreCollisionAI turns down any CEntityWalking (0x4381c0), the kind
    // every character on the maps is.
    const auto off = [&](float ax, float ay, float bx, float by) {
        if (!crosses(x0, y0, x1, y1, ax, ay, bx, by))
            return false;
        const float length = std::hypot(bx - ax, by - ay);
        if (length <= 0.0f)
            return false;
        // The side's normal toward where it came from.
        float nx = -(by - ay) / length;
        float ny = (bx - ax) / length;
        if ((x0 - ax) * nx + (y0 - ay) * ny < 0.0f) {
            nx = -nx;
            ny = -ny;
        }
        const float dot = vx * nx + vy * ny;
        if (dot >= 0.0f)
            return true;
        const float factor = model ? model->bounceFactor : 1.0f;
        const float minBounce = model ? model->minBounce : 0.1f;
        const float friction = model ? model->bounceFriction : 0.0f;
        const float minFriction = model ? model->minBounceFriction : 0.1f;
        float tx = (vx - dot * nx) * (1.0f - friction);
        float ty = (vy - dot * ny) * (1.0f - friction);
        if (tx * tx + ty * ty <= minFriction * minFriction)
            tx = ty = 0.0f;
        const float back = dot * factor;
        if (back * back > minBounce * minBounce) {
            tx -= back * nx;
            ty -= back * ny;
        }
        vx = tx;
        vy = ty;
        return true;
    };
    // But not a part with a CPassThroughTriggerAI: its AIs let it through
    // (0x4ef966, 0x492e40), as that one does (0x43a640), and it's only
    // touched. So Mission 01's grenade bouncers' triggers, collideable as
    // they are.
    const auto blocks = [&](size_t part) {
        const PartState &state = _partStates[part];
        if (!state.active || state.deleted || !state.collideable)
            return false;
        const std::vector<const TextObject *> &ais = _map.parts[part].activities;
        return std::none_of(ais.begin(), ais.end(),
                            [](const TextObject *ai) { return ai->className == "CPassThroughTriggerAI"; });
    };
    // A side turns it only as it goes in: the move takes away only what of
    // it goes into the side met (0x4efcf1), so one leaving an outline it's
    // in, thrown from where its thrower stands in it, goes on.
    for (const Wall &w : _walls) {
        if (!blocks(w.part) || inside(w.points, x0, y0))
            continue;
        for (size_t k = 0; k < w.points.size(); ++k) {
            const auto [ax, ay] = w.points[k];
            const auto [bx, by] = w.points[(k + 1) % w.points.size()];
            if (off(ax, ay, bx, by)) {
                if (struck)
                    *struck = w.part;
                return false;
            }
        }
    }
    for (size_t i : _bouncers) {
        const auto &pts = _map.parts[i].polygon;
        if (!blocks(i) || inside(pts, x0, y0))
            continue;
        for (size_t k = 0; k + 1 < pts.size(); ++k)
            if (off(pts[k].first, pts[k].second, pts[k + 1].first, pts[k + 1].second)) {
                if (struck)
                    *struck = i;
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
    // A click on it with a weapon chosen runs the weapon's own process on
    // it, it having none of the weapon's Process Type nor a Use
    // (0x43d23a): for each chosen officer it's in reach of, its Item Range
    // on the ground, a quarter more enhanced (0x5c6d30); one out of reach
    // isn't given it (0x5c6cd2), so doesn't walk up.
    const Enemy &e = _enemies[size_t(enemy)];
    for (Officer &o : _officers)
        if (o.chosen && o.alive() && o.weapon &&
            inItemRange(*o.weapon, o.x, o.y, e.x, e.y, o.weapon->range * enhancement(int(&o - _officers.data())))) {
            o.attacking = enemy;
            o.path.clear();
        }
}

float MapScreen::shoot(const ItemDefinition &weapon, const std::string &model, float x, float y, float tx, float ty,
                       int instigator) {
    // Its Shot Sound. A beam's flashes are on its ends (below); another's
    // Shot Flash where the shooter stands and its Hit Flash on the one hit
    // (inferred).
    if (_mixer)
        if (auto sound = gameSound(_data, weapon.shotSound))
            _mixer->play(sound, audio::SoundType::kEffects);
    if (!weapon.shotFlash.empty() && !weapon.beam)
        _flashes.push_back({weapon.shotFlash, x, y});
    // A strike's Hit Flash is on the one struck; a bolt's where it meets
    // someone.
    if (!weapon.hitFlash.empty() && weapon.dischargeAction == "CStrikeTargetAction")
        _flashes.push_back({weapon.hitFlash, tx, ty});
    // A beam weapon's line, the CPhaserShot (0x5c95d0): from its muzzle
    // (Forward Position along the way they face, Lateral Position across
    // it, Height up) toward the target as high on them (inferred), its head
    // going out at 1500 a second, stopping where its bolt meets someone or
    // a wall, or at its reach: the weapon's Item Range, a quarter more
    // for an enhanced shooter (0x5c98a5, the item's 0x5cdf00); after its
    // Shot Duration its tail follows at the same pace, and it's gone when
    // that catches up (0x535c00).
    const float dx = tx - x;
    const float dy = ty - y;
    const float distance = std::hypot(dx, dy);
    _pendingBeam = -1;
    if (weapon.beam && distance > 0.0f) {
        const float mx = x + dx / distance * weapon.forward - dy / distance * weapon.lateral;
        const float my = y + dy / distance * weapon.forward + dx / distance * weapon.lateral - weapon.height;
        const float bx = tx - mx;
        const float by = ty - weapon.height - my;
        const float along = std::hypot(bx, by);
        if (along > 0.0f) {
            Beam b;
            b.x0 = b.x1 = b.sx = mx;
            b.y0 = b.y1 = b.sy = my;
            b.ux = bx / along;
            b.uy = by / along;
            b.lift = weapon.height;
            b.reach = weapon.range * enhancement(instigator);
            b.left = weapon.shotDuration > 0.0f ? weapon.shotDuration : kBeamTime;
            b.outside = weapon.beamOutside;
            b.middle = weapon.beamMiddle;
            b.id = _pendingBeam = ++_beamIds;
            b.shotFlash = weapon.shotFlash;
            b.hitFlash = weapon.hitFlash;
            _beams.push_back(b);
        }
    }
    const Animation &a = animation(model, weapon.dischargeSequence.empty() ? "Shoot1" : weapon.dischargeSequence);
    const int per = a.perDirection();
    return per > 0 && a.fps > 0.0f ? float(per) / a.fps : weapon.shotDuration;
}

void MapScreen::shotThrough(float x0, float y0, float x1, float y1, int instigator, std::vector<size_t> *crossed) {
    // The shot's a CPhaserShot, a bolt going on from the muzzle at its
    // speed, with a CShotAI (0x5c95d0, 0x535c00), which is what a trigger
    // Triggered By Projectiles answers to (0x417fb3); a touching trigger
    // asks for what's in its shape (0x417670). Here the stretch it flew,
    // coming into and leaving each it crosses (inferred: its shape against
    // the trigger's isn't traced).
    const auto meets = [&](const Trigger &t) {
        const MapPart &p = _map.parts[t.part];
        if (t.ai->className.find("Oval") != std::string::npos) {
            const float rx = float(t.ai->number("X Radius").value_or(0.0));
            const float ry = rx * float(t.ai->number("YX Ratio").value_or(1.0));
            if (rx <= 0.0f || ry <= 0.0f)
                return false;
            float px = 0.0f;
            float py = 0.0f;
            placeOf(t.part, px, py);
            // The nearest point of the line to the oval's middle, measured
            // in its radii.
            const float ax = (x0 - px) / rx;
            const float ay = (y0 - py) / ry;
            const float dx = (x1 - x0) / rx;
            const float dy = (y1 - y0) / ry;
            const float length = dx * dx + dy * dy;
            const float k = length > 0.0f ? std::clamp(-(ax * dx + ay * dy) / length, 0.0f, 1.0f) : 0.0f;
            return std::hypot(ax + k * dx, ay + k * dy) <= 1.0f;
        }
        const auto &pts = p.polygon;
        if (pts.size() < 3)
            return false;
        if (inside(pts, x0, y0) || inside(pts, x1, y1))
            return true;
        for (size_t k = 0; k < pts.size(); ++k) {
            const auto [ax, ay] = pts[k];
            const auto [bx, by] = pts[(k + 1) % pts.size()];
            if (crosses(x0, y0, x1, y1, ax, ay, bx, by))
                return true;
        }
        return false;
    };
    const auto flag = [](const TextObject *o, const char *key) { return o->number(key).value_or(0.0) != 0.0; };
    for (Trigger &t : _triggers) {
        const PartState &state = _partStates[t.part];
        const std::string &kind = t.ai->className;
        if (t.done || !state.active || state.deleted ||
            (kind != "CTouchingOvalTriggerAI" && kind != "CTouchingPolygonTriggerAI") ||
            !flag(t.ai, "Triggered By Projectiles") ||
            (crossed && std::find(crossed->begin(), crossed->end(), t.part) != crossed->end()) || !meets(t))
            continue;
        if (crossed)
            crossed->push_back(t.part);
        for (const char *key : {"Enter Action", "Exit Action"}) {
            const TextObject *action = t.ai->object(key);
            if (!action || t.done)
                continue;
            if (flag(t.ai, "Trigger Only Once"))
                t.done = true;
            runAction(action, instigator, int(t.part));
        }
        if (t.done && flag(t.ai, "Auto-Flip Switch"))
            _partStates[t.part].active = false;
    }
}

float MapScreen::enhancement(int officer) const {
    // An enhanced item's Weapon Damage and Item Range are a quarter more
    // (0x5cded0, 0x5cfda2).
    const bool enhanced =
        officer >= 0 && size_t(officer) < _officers.size() && _officers[size_t(officer)].enhanced > 0.0f;
    return enhanced ? 1.25f : 1.0f;
}

namespace {

constexpr float kReaction = 2.5f;

} // namespace

bool MapScreen::unavoidable(const std::string &type) const {
    auto it = _unavoidable.find(type);
    if (it == _unavoidable.end()) {
        std::vector<uint8_t> bytes;
        TextObject def;
        const bool yes = !type.empty() && !_data.read("Damage Types/" + type + ".txt", bytes) &&
                         !readTextObject(std::string(bytes.begin(), bytes.end()), def) &&
                         def.integer("Unavoidable damage").value_or(0) != 0;
        it = _unavoidable.emplace(type, yes).first;
    }
    return it->second;
}

void MapScreen::hurtEnemy(size_t enemy, const ItemDefinition &weapon, int attacker, bool stun) {
    Enemy &e = _enemies[enemy];
    if (!e.alive())
        return;
    // A Borg shield (CShieldAI, 0x43de10): a damage type that isn't
    // Unavoidable adapts it, by 0.33 if it's the type that last hit it, else
    // 0.2, every shield of its Who To Adapt taking the same (0x43dc40);
    // once that's 1, the type does nothing, the shield's Render Effect
    // shown (0x510800), and the hit's as if it weren't (0x4952e2).
    if (e.shield && !stun && e.shield->object("Render Effect") && !unavoidable(weapon.damageType)) {
        float &level = e.adapted[weapon.damageType];
        if (level >= 1.0f) {
            if (const TextObject *effect = e.shield->object("Render Effect"))
                _flashes.push_back(
                    {readText(effect, "Model"), e.x, e.y, 0.0f, readText(effect, "Sequence"), int(enemy)});
            return;
        }
        level += weapon.damageType == e.lastDamage ? 0.33f : 0.2f;
        e.lastDamage = weapon.damageType;
        const float shared = level;
        if (readText(e.shield, "Who To Adapt") == "Enemy")
            for (Enemy &other : _enemies)
                if (other.shield)
                    other.adapted[weapon.damageType] = shared;
    }
    // Hit by someone other than its target, it's slow to take a target
    // for 2.5 seconds (CHitTargetMessage, 0x434617).
    if (attacker != e.target)
        e.reaction = kReaction;
    if (const TextObject *action = e.ai->object("Damage Action"))
        runAction(action, attacker, int(e.part));
    // A weapon set to stun puts it down for the weapon's Stun Duration,
    // then back to its patrol, unless its Stun State is Attack (145 of the
    // maps' enemies: the Borg's), which it then does (inferred).
    const bool stunnable = readText(e.ai, "Stun State") != "Attack";
    if (stun && stunnable) {
        // Its Stun state's stun AI given the stun's time (0x42cdb7), its
        // target dropped, and where its instigator stands, whoever it is,
        // taken as its place to go to (0x42cdd8, 0x42cdf9): up again, its
        // Next State goes there. With no instigator it isn't put in its
        // Stun state at all.
        float px = e.x;
        float py = e.y;
        if (!placeOfInstigator(attacker, px, py))
            return;
        standUp(e);
        e.stunLength = stunLength(weapon);
        e.stunTotal = e.stunned = stunTotal(_map.parts[e.part].model, e.stunLength);
        e.route.clear();
        setState(e, "Stun");
        e.target = -1;
        e.seenX = px;
        e.seenY = py;
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
    // One that's Target If Hit, hit while it isn't attacking, turns to
    // whoever hit it and goes to look where they are, its Investigate
    // (0x4343db, 0x434432); one hurt enough retreats. One taken over does
    // as it's told.
    if (int(enemy) == _melded)
        return;
    if (attacker >= 0 && size_t(attacker) < _officers.size() && readText(e.ai, "Target If Hit") == "1" &&
        e.state != "Attack" && e.state != "Retreat") {
        const Officer &o = _officers[size_t(attacker)];
        e.angle = std::atan2(o.y - e.y, e.x - o.x); // facing angle: 0 is left
        investigate(e, o.x, o.y);
    }
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

int MapScreen::firstInLine(float x0, float y0, float x1, float y1, int shooter) const {
    return firstOnStretch(x0, y0, x1, y1, shooter, 0.0f, x1, y1);
}

int MapScreen::firstOnStretch(float x0, float y0, float x1, float y1, int shooter, float from, float aimX,
                              float aimY) const {
    // The bolt meets the first thing standing full height on its line, the
    // shooter left out, and, for a Team Member's, the Team Members
    // (0x535dcf, 0x535e05): an enemy between an officer and their target
    // is hit, as is anyone between an enemy and its. The characters by a
    // figure's width either side of the line (inferred: their outlines).
    const float dx = x1 - x0;
    const float dy = (y1 - y0) / ParticleSystem::kYToX;
    const float length = dx * dx + dy * dy;
    int best = kNobody;
    float nearest = 2.0f;
    const auto consider = [&](float cx, float cy, int who) {
        const float px = cx - x0;
        const float py = (cy - y0) / ParticleSystem::kYToX;
        const float t = length > 0.0f ? (px * dx + py * dy) / length : 0.0f;
        if (t <= from || t > 1.0f + kHitReach / std::sqrt(std::max(length, 1.0f)))
            return;
        if (std::hypot(px - t * dx, py - t * dy) < kHitReach && t < nearest) {
            nearest = t;
            best = who;
        }
    };
    // One lying stunned isn't in the way, but is hit aimed at (inferred).
    const auto aimed = [&](float cx, float cy) { return std::hypot(cx - aimX, cy - aimY) < 1.0f; };
    if (shooter < 0)
        for (size_t i = 0; i < _officers.size(); ++i) {
            const Officer &o = _officers[i];
            if (o.present && o.alive() && (o.stunned <= 0.0f || aimed(o.x, o.y)))
                consider(o.x, o.y, int(i));
        }
    for (size_t i = 0; i < _enemies.size(); ++i) {
        const Enemy &e = _enemies[i];
        if (kEnemyInstigator - int(i) != shooter && e.alive() && shown(e.part) &&
            (e.stunned <= 0.0f || aimed(e.x, e.y)))
            consider(e.x, e.y, kEnemyInstigator - int(i));
    }
    // A decoy standing, a Team Member, in an enemy's way.
    if (shooter < 0)
        for (const Deployed &d : _deployed) {
            const MapPart &p = _map.parts[d.part];
            if (d.fading < 0.0f && d.health > 0.0f && shown(d.part) && inCategory(p.category, "Decoy"))
                consider(p.x, p.y, kPartTarget - int(d.part));
        }
    return best;
}

void MapScreen::hitWith(int who, const ItemDefinition &weapon, int shooter, bool stun) {
    if (who >= 0 && size_t(who) < _officers.size())
        hurtOfficer(size_t(who), weapon, stun);
    else if (who <= kEnemyInstigator && size_t(kEnemyInstigator - who) < _enemies.size())
        hurtEnemy(size_t(kEnemyInstigator - who), weapon, shooter, stun);
    else if (who <= kPartTarget && size_t(kPartTarget - who) < _map.parts.size())
        hurtDecoy(size_t(kPartTarget - who), weapon, shooter);
}

bool MapScreen::hasShot(const ItemDefinition &weapon, float energy) {
    return weapon.dischargeAction != "CShootEnergyWeaponAction" || energy >= weapon.energyUse;
}

void MapScreen::spendShot(const ItemDefinition &weapon, float &energy) {
    if (weapon.dischargeAction == "CShootEnergyWeaponAction")
        energy = std::max(0.0f, energy - weapon.energyUse);
}

float *MapScreen::weaponEnergy(Officer &o) {
    if (!o.weapon)
        return nullptr;
    for (Officer::Held &h : o.items)
        if (h.name == o.weapon->name)
            return &h.energy;
    return nullptr;
}

float MapScreen::staminaFactor(size_t officer) const {
    for (const TeamMember::Attribute &a : _team[_officers[officer].member].attributes)
        if (a.name == "Stamina")
            return float(a.value) / 100.0f * 1.5f + 0.5f;
    return 1.0f;
}

void MapScreen::hurtOfficer(size_t officer, const ItemDefinition &weapon, bool stun) {
    Officer &o = _officers[officer];
    if (!o.alive())
        return;
    officerHit(officer);
    if (stun) {
        // A stun AI made theirs, their orders and what they were doing
        // dropped (0x53a46b), lying them down as long as the weapon stuns
        // for.
        o.stunLength = stunLength(weapon);
        o.stunTotal = o.stunned = stunTotal(_team[o.member].bodyModel, o.stunLength);
        o.path.clear();
        o.attacking = -1;
        o.task = Officer::Task::kNone;
        o.taskProcess = nullptr;
        o.taskStep = 0;
        return;
    }
    // An officer's hurt by the difficulty's share of it, and by less the
    // more Stamina they have (0x4a9ab0): on easy 0.55 of it, on normal all
    // of it (0x63cdb8); above, more by a third of it for each level, to
    // twice it.
    float damage = weapon.weaponDamage;
    if (_difficulty <= 1)
        damage *= 0.55f;
    else if (_difficulty > 2)
        damage += std::min(damage, float(_difficulty - 2) * damage * 0.33f);
    o.health = std::max(0.0f, o.health - damage / staminaFactor(officer));
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

void MapScreen::fireBolt(const ItemDefinition &weapon, float x, float y, float tx, float ty, int self, int attacker,
                         bool stun) {
    // Up to the weapon's range, at least as far as the one aimed at.
    const float distance = std::hypot(tx - x, ty - y);
    if (distance <= 0.0f)
        return;
    const float range = std::max(weapon.range * enhancement(self), distance + kHitReach);
    _bolts.push_back({&weapon,
                      self,
                      attacker,
                      stun,
                      x,
                      y,
                      (tx - x) / distance,
                      (ty - y) / distance,
                      tx,
                      ty,
                      0.0f,
                      range,
                      {},
                      _pendingBeam});
    _pendingBeam = -1;
}

void MapScreen::updateBolts(float seconds) {
    // Each frame the bolt's head goes on at its speed, no further than its
    // range, and the whole line from its start to its head is tried for the
    // first in its way, or a wall (0x535df6), so one walking into a beam
    // that's out is met; a beam's is tried till it meets something or is
    // gone (0x535d96).
    for (size_t i = 0; i < _bolts.size();) {
        Bolt &b = _bolts[i];
        const float from = b.head;
        b.head = std::min(b.range, b.head + kBoltSpeed * seconds);
        const float x0 = b.x + b.ux * from;
        const float y0 = b.y + b.uy * from;
        const float x1 = b.x + b.ux * b.head;
        const float y1 = b.y + b.uy * b.head;
        const Bolt bolt = b;
        if (bolt.weapon->dischargeAction == "CShootEnergyWeaponAction")
            shotThrough(x0, y0, x1, y1, bolt.self, &_bolts[i].crossed);
        const int hit = firstOnStretch(bolt.x, bolt.y, x1, y1, bolt.self, 0.0f, bolt.aimX, bolt.aimY);
        const bool beamOut =
            std::any_of(_beams.begin(), _beams.end(), [&](const Beam &beam) { return beam.id == bolt.beam; });
        bool done = bolt.head >= bolt.range && !beamOut;
        if (hit != kNobody) {
            float hx = 0.0f;
            float hy = 0.0f;
            if (hit >= 0) {
                hx = _officers[size_t(hit)].x;
                hy = _officers[size_t(hit)].y;
            } else {
                hx = _enemies[size_t(kEnemyInstigator - hit)].x;
                hy = _enemies[size_t(kEnemyInstigator - hit)].y;
            }
            // A beam's Ending Flash shows on its head (0x535e6e); else one
            // where it meets them.
            bool onBeam = false;
            for (Beam &beam : _beams)
                if (beam.id == bolt.beam)
                    onBeam = beam.hit = true;
            if (!onBeam && !bolt.weapon->hitFlash.empty())
                _flashes.push_back({bolt.weapon->hitFlash, hx, hy});
            // A Team Member's bolt makes a noise of 200 for 1.5 seconds
            // where it meets someone (0x535ef4).
            if (bolt.self >= 0 && hit <= kEnemyInstigator)
                makeNoise(hx, hy, kHitNoise, kHitNoiseAge);
            hitWith(hit, *bolt.weapon, bolt.attacker, bolt.stun);
            done = true;
        } else if (!clearSight(bolt.x, bolt.y, x1, y1)) {
            done = true;
        }
        if (done) {
            // Its beam's head stops there (0x535e5c).
            const float ex = hit >= 0 ? _officers[size_t(hit)].x
                                      : (hit != kNobody ? _enemies[size_t(kEnemyInstigator - hit)].x : x1);
            const float ey = hit >= 0 ? _officers[size_t(hit)].y
                                      : (hit != kNobody ? _enemies[size_t(kEnemyInstigator - hit)].y : y1);
            for (Beam &beam : _beams)
                if (beam.id == bolt.beam)
                    beam.reach = std::max(0.0f, (ex - beam.sx) * beam.ux + (ey - beam.lift - beam.sy) * beam.uy);
            _bolts.erase(_bolts.begin() + ptrdiff_t(i));
        } else
            ++i;
    }
}

void MapScreen::updateCombat(float seconds) {
    updateBolts(seconds);
    for (Beam &b : _beams) {
        b.left -= seconds;
        b.time += seconds;
        if (b.left > 0.0f)
            b.head = std::min(b.reach, b.head + kBoltSpeed * seconds);
        else
            b.tail = std::min(b.head, b.tail + kBoltSpeed * seconds);
        b.head = std::min(b.head, b.reach);
        b.x0 = b.sx + b.ux * b.tail;
        b.y0 = b.sy + b.uy * b.tail;
        b.x1 = b.sx + b.ux * b.head;
        b.y1 = b.sy + b.uy * b.head;
    }
    std::erase_if(_beams, [](const Beam &b) { return b.left <= 0.0f && b.tail >= b.head; });
    for (Flash &f : _flashes)
        f.time += seconds;
    std::erase_if(_flashes, [&](const Flash &f) {
        const Animation &a = animation(f.model, f.sequence);
        return a.fps <= 0.0f || f.time * a.fps >= float(a.perDirection());
    });
    // An energy weapon's energy comes back at its Energy Recharge a second,
    // to 100 (0x5d08a8); a charged item's is its own (map_gadgets.cpp).
    const auto recharge = [&](const ItemDefinition &def, float &energy) {
        if (def.dischargeAction == "CShootEnergyWeaponAction" && !def.charged)
            energy = std::min(100.0f, energy + def.energyRecharge * seconds);
    };
    for (Officer &o : _officers) {
        for (Officer::Held &h : o.items)
            if (const auto def = _items.find(h.name); def != _items.end())
                recharge(def->second, h.energy);
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
        // A strike (the neck pinch's, CStrikeTargetAction) reaches only its
        // Strike Damage Radius: they walk up to it (its CStrikeTargetAI).
        const bool strike = o.weapon->dischargeAction == "CStrikeTargetAction";
        if (strike && std::hypot(e.x - o.x, e.y - o.y) > std::max(o.weapon->strikeRadius, kStrikeReach)) {
            if (o.path.empty())
                o.path = route(o, e.x, e.y);
            continue;
        }
        // A gun's CShootWeaponAI, Fire Until Dead: where they stand, each
        // shot once the weapon's ready, however far off it is; its line
        // blocked as they come to shoot, they're done with it (0x5600a0,
        // its line tried at 0x5603fc).
        o.path.clear();
        o.direction = directionOf(e.x - o.x, e.y - o.y);
        float *energy = weaponEnergy(o);
        if (o.reload > 0.0f || (energy && !hasShot(*o.weapon, *energy)))
            continue;
        if (!strike && !clearSight(o.x, o.y, e.x, e.y)) {
            o.attacking = -1;
            continue;
        }
        if (energy)
            spendShot(*o.weapon, *energy);
        o.reload = o.weapon->reuseDelay;
        o.discharge = o.weapon->dischargeSequence.empty() ? "Shoot1" : o.weapon->dischargeSequence;
        shoot(*o.weapon, _team[o.member].bodyModel, o.x, o.y, e.x, e.y, int(&o - _officers.data()));
        makeNoise(o.x, o.y, o.weapon->shotNoise, o.weapon->shotNoiseTime);
        o.shooting = 1e-6f;
        const int self = int(&o - _officers.data());
        if (strike)
            hurtEnemy(i, *o.weapon, self, stuns(o));
        else
            fireBolt(*o.weapon, o.x, o.y, e.x, e.y, self, self, stuns(o));
    }
    for (Enemy &e : _enemies) {
        if (e.weapon)
            recharge(*e.weapon, e.energy);
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
            // Lying once it's fallen, it's out of the way till it's up
            // again: its Collideable put aside, then back (0x5e0e9a,
            // 0x5e1008).
            const Animation &death = animation(_map.parts[e.part].model, "Death");
            const float fall = death.fps > 0.0f ? float(death.perDirection()) / death.fps : 0.0f;
            if (!e.stunLaid && e.stunned > 0.0f && e.stunTotal - e.stunned >= fall) {
                e.stunKept = _partStates[e.part].collideable;
                _partStates[e.part].collideable = false;
                e.stunLaid = true;
            }
            if (e.stunned <= 0.0f) {
                standUp(e);
                // Up, its stun AI done: its Stun state's Next State
                // (0x42ce3f), Investigate going to where the shot came
                // from, or Recharge; else its patrol (inferred).
                const TextObject *stun = e.ai->object("Stun");
                const std::string next = stun ? readText(stun, "Next State") : std::string();
                if (stateKey(next) == "Investigate")
                    investigate(e, e.seenX, e.seenY);
                else
                    setState(e, next.empty() || next == "Any" ? "Patrol" : next);
            }
        }
    }
}

bool MapScreen::placeOfInstigator(int who, float &x, float &y) const {
    if (who >= 0 && size_t(who) < _officers.size()) {
        x = _officers[size_t(who)].x;
        y = _officers[size_t(who)].y;
        return true;
    }
    if (who <= kEnemyInstigator && size_t(kEnemyInstigator - who) < _enemies.size()) {
        x = _enemies[size_t(kEnemyInstigator - who)].x;
        y = _enemies[size_t(kEnemyInstigator - who)].y;
        return true;
    }
    for (const Thrown &t : _thrown)
        if (who == kThrownInstigator - t.id) {
            x = t.x;
            y = t.y;
            return true;
        }
    return false;
}

void MapScreen::standUp(Enemy &e) {
    if (e.stunLaid && e.stunKept)
        _partStates[e.part].collideable = true;
    e.stunLaid = false;
}

float MapScreen::stunLength(const ItemDefinition &weapon) const {
    // Its Stun Duration by the difficulty: on easy 1.66 of it, on normal
    // all of it (0x64a22c); above, less by a quarter of it for each level,
    // at most three quarters (0x5dae98).
    if (_difficulty <= 1)
        return weapon.stunDuration * 1.66f;
    if (_difficulty == 2)
        return weapon.stunDuration;
    return weapon.stunDuration * (1.0f - std::min(float(_difficulty - 2) * 0.25f, 0.75f));
}

float MapScreen::stunTotal(const std::string &model, float length) {
    // Its Die Sequence once, Death, then its Dead Sequence, Dead, for the
    // Time To Stun, then its Stand Up Sequence, GetUp, or without one its
    // Death backwards (the CStunAI's defaults, 0x5e04da; its states,
    // 0x5e0c50).
    const auto seconds = [&](const char *sequence) {
        const Animation &a = animation(model, sequence);
        return a.fps > 0.0f ? float(a.perDirection()) / a.fps : 0.0f;
    };
    const float up = animation(model, "GetUp").perDirection() > 0 ? seconds("GetUp") : seconds("Death");
    return seconds("Death") + length + up;
}

std::string MapScreen::combatSequence(float shooting, float dead, const Stun &stun, int &frame,
                                      const std::string &model, const std::string &discharge) {
    // Fallen, its Death once and then Dead; stunned, falling so, lying
    // its while, then getting up (0x5e0c50); shooting, its Shoot1 through.
    const auto into = [&](const char *sequence, float time) {
        const Animation &a = animation(model, sequence);
        frame = std::min(int(time * a.fps), std::max(0, a.perDirection() - 1));
    };
    if (stun.left > 0.0f && dead < 0.0f) {
        const Animation &death = animation(model, "Death");
        const float fall = death.fps > 0.0f ? float(death.perDirection()) / death.fps : 0.0f;
        float t = stun.total - stun.left;
        if (t < fall) {
            into("Death", t);
            return "Death";
        }
        t -= fall;
        if (t < stun.length) {
            frame = 0;
            return "Dead";
        }
        t -= stun.length;
        if (animation(model, "GetUp").perDirection() > 0) {
            into("GetUp", t);
            return "GetUp";
        }
        frame = std::max(0, death.perDirection() - 1 - int(t * death.fps));
        return "Death";
    }
    if (dead >= 0.0f) {
        const Animation &death = animation(model, "Death");
        if (death.fps > 0.0f && dead * death.fps < float(death.perDirection())) {
            into("Death", dead);
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
