// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The mind meld: Slovaak's Psychic Rack taking an enemy over
// (docs/engines/stawayteam/systems/items.md#the-psychic-rack).

#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

// Degrees a second it turns to shoot: as the enemies do.
constexpr float kTurnSpeed = 360.0f;

float angleOf(float dx, float dy) {
    return std::atan2(dy, dx);
}

} // namespace

bool MapScreen::canMeld(size_t officer, size_t enemy) const {
    // Not while it's on; and an enemy standing, not of the team nor Immune
    // to Psyche Displacement, nor one already taken (0x5d4ff0); with a
    // use's worth of charge left (0x5d5145).
    const Officer &o = _officers[officer];
    const Enemy &e = _enemies[enemy];
    if (_melded >= 0 || o.down() || o.item < 0 || e.down() || !shown(e.part))
        return false;
    const auto def = _items.find(o.items[size_t(o.item)].name);
    if (def == _items.end() || !def->second.psyche || o.items[size_t(o.item)].energy < def->second.energyUse)
        return false;
    const std::string &category = _map.parts[e.part].category;
    return !inCategory(category, "Immune to Psyche Displacement") && !inCategory(category, "Team Member") &&
           !inCategory(category, "Player");
}

void MapScreen::startMeld(size_t officer, size_t enemy) {
    // The enemy's own mind put aside, it's made one of the team's for the
    // while (0x5d522c): the others go after it as they would an officer;
    // the one holding it stands still. It's chosen in their place
    // (inferred).
    Officer &o = _officers[officer];
    Enemy &e = _enemies[enemy];
    o.items[size_t(o.item)].on = true;
    o.path.clear();
    o.attacking = -1;
    o.task = Officer::Task::kNone;
    _melded = int(enemy);
    _melder = int(officer);
    std::string &category = _map.parts[e.part].category;
    _meldCategory = category;
    category = category.empty() ? "Team Member" : category + ",Team Member";
    e.route.clear();
    e.target = -1;
    e.foe = -1;
    e.state = "Controlled";
    e.stateTime = 0.0f;
    chooseMelded();
}

void MapScreen::chooseMelded() {
    if (_melded < 0)
        return;
    for (Officer &o : _officers)
        o.chosen = false;
    _selected = -1;
    _meldChosen = true;
}

void MapScreen::breakMeld() {
    // Its own mind put back (0x43b9d0), with the Break Meld Sound
    // (0x5d5420): told to look where the one who took it stands, a
    // CInvestigateMessage (0x438320); stunned meanwhile, it's left in its
    // Stun state, lying as it was.
    if (_melded < 0)
        return;
    Enemy &e = _enemies[size_t(_melded)];
    Officer &o = _officers[size_t(_melder)];
    _map.parts[e.part].category = _meldCategory;
    e.route.clear();
    e.foe = -1;
    e.target = -1;
    if (e.alive() && e.stunned > 0.0f)
        setState(e, "Stun");
    else if (e.alive())
        investigate(e, o.x, o.y);
    for (Officer::Held &h : o.items) {
        const auto def = _items.find(h.name);
        if (def == _items.end() || !def->second.psyche || !h.on)
            continue;
        h.on = false;
        if (_mixer)
            if (auto sound = gameSound(_data, def->second.breakSound))
                _mixer->play(sound, audio::SoundType::kEffects);
    }
    _melded = -1;
    _melder = -1;
    _meldChosen = false;
}

void MapScreen::orderMelded(float x, float y) {
    // Given a CPlayerAI as the officers have (0x5d5204), it's ordered as
    // they are: an enemy clicked, its weapon's process on it if it's in
    // the weapon's reach, else nothing (0x5c6cd2); else to the place.
    Enemy &e = _enemies[size_t(_melded)];
    const int other = enemyAt(x, y);
    if (other == _melded)
        return;
    if (other >= 0) {
        const Enemy &f = _enemies[size_t(other)];
        if (!e.weapon || !inItemRange(*e.weapon, e.x, e.y, f.x, f.y, e.weapon->range))
            return;
        e.route.clear();
        e.foe = other;
        return;
    }
    e.route.clear();
    e.foe = -1;
    e.route = route(e, x, y);
}

void MapScreen::updateMeld(float seconds) {
    // The Psychic Rack's charge: down by its Energy Use a second while it's
    // on, else up by its Energy Recharge (as the charged items', 0x5d2d30).
    for (Officer &o : _officers)
        for (Officer::Held &h : o.items) {
            const auto def = _items.find(h.name);
            if (def == _items.end() || !def->second.psyche)
                continue;
            if (h.on)
                h.energy = std::max(0.0f, h.energy - def->second.energyUse * seconds);
            else
                h.energy = std::min(100.0f, h.energy + def->second.energyRecharge * seconds);
        }
    if (_melded < 0)
        return;
    Officer &o = _officers[size_t(_melder)];
    Enemy &e = _enemies[size_t(_melded)];
    // It ends as the charge runs out, or either of them falls (0x43b8ce).
    bool on = false;
    for (const Officer::Held &h : o.items)
        if (h.on && h.energy > 0.0f)
            if (const auto def = _items.find(h.name); def != _items.end() && def->second.psyche)
                on = true;
    if (!on || !o.alive() || o.down() || !e.alive() || !shown(e.part)) {
        breakMeld();
        return;
    }
    o.path.clear();
    o.attacking = -1;
    if (e.down())
        return;
    if (e.foe < 0) {
        followRoute(e, seconds, false);
        return;
    }
    // Sent after an enemy, by the weapon's CShootWeaponAI as an officer
    // is: it turns and shoots, each shot once the weapon's ready, however
    // far off it is, till it's dead; its line blocked as it comes to
    // shoot, it's done with it (0x5600a0).
    const Enemy &f = _enemies[size_t(e.foe)];
    if (!f.alive() || !shown(f.part) || !e.weapon) {
        e.foe = -1;
        return;
    }
    turnTo(e, angleOf(f.x - e.x, f.y - e.y), kTurnSpeed, seconds);
    if (e.reload > 0.0f || !hasShot(*e.weapon, e.energy))
        return;
    if (!clearSight(e.x, e.y, f.x, f.y)) {
        e.foe = -1;
        return;
    }
    spendShot(*e.weapon, e.energy);
    e.reload = e.weapon->reuseDelay;
    shoot(*e.weapon, _map.parts[e.part].model, e.x, e.y, f.x, f.y, kEnemyInstigator - int(&e - _enemies.data()));
    e.shooting = 1e-6f;
    hurtEnemy(size_t(e.foe), *e.weapon, kEnemyInstigator - int(&e - _enemies.data()), e.stuns);
}

} // namespace reluva::stawayteam
