// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A mission's objectives, as the triggers change them, and its end
// (docs/engines/stawayteam/systems/objectives.md).

#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cstdio>
#include <functional>

namespace reluva::stawayteam {

namespace {

// The screen the windows are centred on.
constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}

} // namespace

void MapScreen::arrive(size_t officer, const Officer &state, float x, float y) {
    // As they left, but here, with nothing under way; their weapon this
    // map's own copy of it.
    Officer &o = _officers[officer];
    const ItemDefinition *weapon = state.weapon;
    o = state;
    o.present = true;
    o.x = x;
    o.y = y;
    o.settled = 0;
    o.path.clear();
    o.attacking = -1;
    o.task = Officer::Task::kNone;
    o.shooting = 0.0f;
    o.chosen = false;
    o.weapon = nullptr;
    if (weapon) {
        const auto it = _items.find(weapon->name);
        o.weapon = it != _items.end() ? &it->second : nullptr;
    }
}

void MapScreen::depart(size_t officer) {
    Officer &o = _officers[officer];
    o.present = false;
    o.chosen = false;
    o.path.clear();
    o.attacking = -1;
    o.task = Officer::Task::kNone;
    if (_selected == int(officer)) {
        _selected = -1;
        for (size_t i = 0; i < _officers.size() && _selected < 0; ++i)
            if (_officers[i].chosen)
                _selected = int(i);
    }
}

bool MapScreen::partPosition(const std::string &name, float &x, float &y) const {
    const std::vector<size_t> parts = partsNamed(name);
    if (parts.empty())
        return false;
    placeOf(parts.front(), x, y);
    return true;
}

std::string MapScreen::objectiveText(const Objective &o) const {
    // Its line of its script, a count's "%d" its count so far ("(%d of 2)"),
    // a rescue's "%s" whom it's of (inferred).
    std::vector<uint8_t> bytes;
    std::string text;
    if (!_data.read("Levels/" + o.script + ".RDS", bytes)) {
        const RdsScript rds = readRdsScript(std::string(bytes.begin(), bytes.end()));
        if (const std::string *line = rds.find(o.line))
            text = *line;
    }
    const size_t at = text.find("%d");
    if (at != std::string::npos)
        text.replace(at, 2, std::to_string(o.count));
    const size_t named = text.find("%s");
    if (named != std::string::npos)
        text.replace(named, 2,
                     o.officer >= 0 && size_t(o.officer) < _officers.size()
                         ? _team[_officers[size_t(o.officer)].member].name()
                         : o.who);
    return text;
}

void MapScreen::setObjectives(MissionObjectives objectives) {
    // The mission's own first, then those the map has added already (its
    // spawn points').
    _progress->mission = std::move(objectives);
    // Whom they protect or rescue, an officer by name; one not on the team
    // yet is a part of the map (Mission 08's Worf).
    for (Objective &o : _progress->mission.objectives)
        for (size_t i = 0; i < _officers.size(); ++i)
            if (!o.who.empty() && (lower(_team[_officers[i].member].name()) == lower(o.who) ||
                                   lower(_team[_officers[i].member].id) == lower(o.who)))
                o.officer = int(i);
    _progress->objectives.insert(_progress->objectives.begin(), _progress->mission.objectives.begin(),
                                 _progress->mission.objectives.end());
}

std::vector<ObjectiveLine> MapScreen::objectiveLines() const {
    std::vector<ObjectiveLine> out;
    for (const Objective &o : _progress->objectives)
        if (o.visible && o.line != "!None")
            out.push_back({o.status, objectiveText(o), o.required});
    return out;
}

void MapScreen::addObjective(Objective o, int instigator) {
    // A protect or rescue objective's "$instigator" is the officer who set
    // it off; else an officer by name (inferred).
    if (lower(o.who) == "$instigator")
        o.officer = instigator;
    else
        for (const Officer &officer : _officers)
            if (_team[officer.member].name() == o.who || _team[officer.member].id == o.who)
                o.officer = int(&officer - _officers.data());
    _progress->objectives.push_back(std::move(o));
}

void MapScreen::setObjectiveStatus(size_t objective, ObjectiveStatus status) {
    Objective &o = _progress->objectives[objective];
    if (o.status == status)
        return;
    o.status = status;
    const TextObject *then = status == ObjectiveStatus::kComplete ? o.successAction
                             : status == ObjectiveStatus::kFailed ? o.failAction
                                                                  : nullptr;
    runAction(then, -1);
    // With every objective required to end the mission complete, its Win
    // Action, once (0x50c9df).
    if (_progress->won || !_progress->mission.winAction)
        return;
    bool required = false;
    for (const Objective &each : _progress->objectives)
        if (each.required && each.status != ObjectiveStatus::kDisabled) {
            if (each.status != ObjectiveStatus::kComplete)
                return;
            required = true;
        }
    if (required) {
        // The rest still to do have failed, without their Fail Actions
        // (0x50ca2c, inferred for the actions), then the Win Action once
        // (0x50ca56).
        _progress->won = true;
        for (Objective &each : _progress->objectives)
            if (each.status == ObjectiveStatus::kIncomplete)
                each.status = ObjectiveStatus::kFailed;
        runAction(_progress->mission.winAction, -1);
    }
}

// The objectives are told of each death (0x5b7880, after its "Died"
// sequence, 0x494eae) and each joining the team (0x5be8e0), those still
// to do each judging it (0x50c870); "$instigator" or "$trigger" in a
// protect's or rescue's whom is made the name of who that is as it's
// added (0x50c270).

void MapScreen::officerFell(size_t officer) {
    // Who's protected or to be rescued, dead, has failed to be (0x50b610,
    // 0x50c0d0).
    for (size_t i = 0; i < _progress->objectives.size(); ++i) {
        const Objective &o = _progress->objectives[i];
        if (o.officer == int(officer) &&
            (o.className == "CProtectMissionObjective" || o.className == "CRescueMissionObjective") &&
            o.status == ObjectiveStatus::kIncomplete)
            setObjectiveStatus(i, ObjectiveStatus::kFailed);
    }
    checkNeedItems();
}

void MapScreen::partFell(size_t part) {
    // A character killed who was to be rescued or protected, by name, or
    // an officer off the team in their stand-in: that objective fails
    // (Mission 08's Worf: the mission's lost).
    const std::string name = lower(_map.parts[part].name);
    for (size_t i = 0; i < _progress->objectives.size(); ++i) {
        const Objective &o = _progress->objectives[i];
        const bool them =
            o.officer >= 0 ? size_t(o.officer) < _officers.size() && _officers[size_t(o.officer)].standIn == int(part)
                           : !name.empty() && lower(o.who) == name;
        if (them && (o.className == "CProtectMissionObjective" || o.className == "CRescueMissionObjective") &&
            o.status == ObjectiveStatus::kIncomplete)
            setObjectiveStatus(i, ObjectiveStatus::kFailed);
    }
    checkNeedItems();
}

void MapScreen::joinedTeam(size_t officer) {
    // Who was to be rescued is (0x50c103).
    for (size_t i = 0; i < _progress->objectives.size(); ++i) {
        const Objective &o = _progress->objectives[i];
        if (o.officer == int(officer) && o.className == "CRescueMissionObjective" &&
            o.status == ObjectiveStatus::kIncomplete)
            setObjectiveStatus(i, ObjectiveStatus::kComplete);
    }
}

void MapScreen::checkNeedItems() {
    // Failed once no one on the team still living has the item among
    // theirs, used up or not (0x50bb20). None of the missions has one.
    for (size_t i = 0; i < _progress->objectives.size(); ++i) {
        const Objective &o = _progress->objectives[i];
        if (o.className != "CNeedItemMissionObjective" || o.item.empty() || o.status != ObjectiveStatus::kIncomplete)
            continue;
        bool held = false;
        for (const Officer &officer : _officers)
            if (officer.alive() && officer.standIn < 0)
                for (const Officer::Held &h : officer.items)
                    held = held || h.name == o.item;
        if (!held)
            setObjectiveStatus(i, ObjectiveStatus::kFailed);
    }
}

size_t MapScreen::addMember(const TeamMember &member, bool present, float x, float y) {
    // As the team's set up: their model's Hit Points, their items as the
    // roster gives them, the weapon they shoot with chosen.
    _team.push_back(member);
    Officer o;
    o.member = _team.size() - 1;
    o.x = x;
    o.y = y;
    o.present = present;
    const ModelInfo &info = modelInfo(member.bodyModel);
    o.maxHealth = info.hitPoints > 0 ? float(info.hitPoints) : kDefaultHealth;
    o.health = o.maxHealth;
    std::vector<std::string> names;
    for (const TeamMember::Item &held : member.inventory) {
        names.push_back(held.name);
        const auto def = _items.find(held.name);
        const int rounds = def != _items.end() ? def->second.rounds : 0;
        o.items.push_back({held.name,
                           held.count > 0 ? held.count
                           : rounds > 0   ? rounds
                                          : -1,
                           def != _items.end() && def->second.stuns});
    }
    o.weapon = weaponOf(names);
    for (size_t k = 0; k < o.items.size(); ++k)
        if (o.weapon && o.items[k].name == o.weapon->name && o.item < 0)
            o.item = int(k);
    _officers.push_back(o);
    _hud->addMember(member);
    return _officers.size() - 1;
}

bool MapScreen::runTeamAction(const TextObject *action, int instigator, int trigger) {
    const std::string &kind = action->className;
    if (kind == "CAddToTeamAction") {
        // A character joins the team where they stand (Data, Worf, a
        // rescued warrior): the program takes the same character, which
        // must be a CEntityStarTrekTeamMember, gives it a CPlayerAI if it
        // has none and puts it on the team (0x57a9f0, 0x5be8e0). Here, the
        // roster's officer of their name, else one made of their part, its
        // model their body, in its place.
        const std::string name = readText(action, "Who To Add");
        std::vector<size_t> parts;
        const int who = whoNamed(name, instigator, trigger);
        if (who >= 0)
            return true;
        if (who <= kEnemyInstigator && size_t(kEnemyInstigator - who) < _enemies.size())
            parts.push_back(_enemies[size_t(kEnemyInstigator - who)].part);
        else
            parts = partsNamed(name, trigger);
        if (_roster.empty())
            (void)loadRoster(_data, _roster);
        for (size_t part : parts) {
            const MapPart &p = _map.parts[part];
            if (_partStates[part].deleted)
                continue;
            // One of the team's, back where their stand-in is.
            bool back = false;
            for (Officer &o : _officers)
                if (o.standIn == int(part)) {
                    placeOf(part, o.x, o.y);
                    o.present = true;
                    o.standIn = -1;
                    _partStates[part].deleted = true;
                    back = true;
                    joinedTeam(size_t(&o - _officers.data()));
                }
            if (back || p.className != "CEntityStarTrekTeamMember")
                continue;
            TeamMember member;
            member.firstName = p.name;
            member.bodyModel = p.model;
            for (const TeamMember &m : _roster) {
                const std::string full = lower(m.name());
                const std::string mine = lower(p.name);
                if (full == mine || lower(m.id) == mine || (!m.firstName.empty() && mine.ends_with(lower(m.firstName))))
                    member = m;
            }
            float x = 0.0f;
            float y = 0.0f;
            placeOf(part, x, y);
            _partStates[part].deleted = true;
            const size_t officer = addMember(member, true, x, y);
            // Whose rescue or protection names them is theirs now.
            for (Objective &o : _progress->objectives)
                if (o.officer < 0 && lower(o.who) == lower(p.name))
                    o.officer = int(officer);
            joinedTeam(officer);
        }
    } else if (kind == "CRemoveFromTeamAction") {
        // They leave the team but stay where they are, a part of the map in
        // their place for what's done to them: Mission 13's hostages, laid
        // out by a task and given a process to revive them by, which adds
        // them back (inferred: the program keeps the same character).
        const int who = whoNamed(readText(action, "Who To Remove"), instigator, trigger);
        if (who >= 0 && size_t(who) < _officers.size() && _officers[size_t(who)].present) {
            Officer &o = _officers[size_t(who)];
            const TeamMember &m = _team[o.member];
            MapPart p;
            p.name = m.name();
            p.model = m.bodyModel;
            p.overlay = m.headModel;
            p.x = o.x;
            p.y = o.y;
            p.angle = float(o.direction) * 2.0f * 3.14159265f / 12.0f; // of the 12 directions
            p.sequence = "Idle";
            p.collideable = false;
            const size_t part = _map.parts.size();
            _map.parts.push_back(p);
            _partStates.push_back({true, true, false, false});
            _byName[lower(p.name)].push_back(part);
            _standing.push_back(part);
            depart(size_t(who));
            o.standIn = int(part);
            for (const TextObject *ai : o.addedAIs)
                giveAI(part, ai);
            o.addedAIs.clear();
        }
    } else if (kind == "CPauseActorAction" || kind == "CUnpauseActorAction") {
        // An actor held still, and let go (a scientist till they're
        // reached; inferred).
        const int who = whoNamed(readText(action, "Target"), instigator, trigger);
        if (kind == "CPauseActorAction")
            hold(who, kHeldForever);
        else if (who >= 0 && size_t(who) < _officers.size())
            _officers[size_t(who)].held = 0.0f;
        else if (who <= kEnemyInstigator && size_t(kEnemyInstigator - who) < _enemies.size())
            _enemies[size_t(kEnemyInstigator - who)].held = 0.0f;
    } else {
        return false;
    }
    return true;
}

void MapScreen::runSpawnActions() {
    // Each officer's spawn point's Spawn Action as they're put there, the
    // officer the instigator and the point the trigger (0x415ca9): Mission
    // 01's add an objective to protect them.
    for (size_t i = 0; i < _officers.size(); ++i) {
        if (_officers[i].spawn < 0)
            continue;
        for (const TextObject *ai : _map.parts[size_t(_officers[i].spawn)].activities)
            if (ai->className == "CSpawnPointAI")
                runAction(ai->object("Spawn Action"), int(i), _officers[i].spawn);
    }
}

void MapScreen::endMission(bool completed, const std::string &script, const std::string &line) {
    if (_endGame || _progress->over)
        return;
    _progress->over = true;
    _progress->completed = completed;
    _progress->overText.clear();
    std::vector<uint8_t> bytes;
    if (!_data.read("Levels/" + script + ".RDS", bytes)) {
        const RdsScript rds = readRdsScript(std::string(bytes.begin(), bytes.end()));
        if (const std::string *text = rds.find(line))
            _progress->overText = *text;
    }
    _objectivesWindow.reset();
    _endGame = std::make_unique<EndGameWindow>(_data, _rules, completed, _progress->overText, kScreenW, kScreenH);
}

bool MapScreen::runMissionAction(const TextObject *action, int instigator) {
    const std::string &kind = action->className;
    const auto named = [&](const std::function<void(size_t)> &each) {
        const std::string name = readText(action, "Objective Name");
        for (size_t i = 0; i < _progress->objectives.size(); ++i)
            if (_progress->objectives[i].name == name)
                each(i);
    };
    if (kind == "CAddMissionObjectiveAction") {
        if (const TextObject *o = action->object("Objective"))
            addObjective(readObjective(*o), instigator);
    } else if (kind == "CSetMissionObjectiveStatusAction") {
        const ObjectiveStatus status = objectiveStatus(readText(action, "Status"));
        named([&](size_t i) { setObjectiveStatus(i, status); });
    } else if (kind == "CSetMissionObjectiveCountAction") {
        // Its Count added (always 1 in the missions); at Count Before
        // Success the count's held there and the objective's made
        // complete, whatever it was (0x50b100).
        const int count = int(action->integer("Count").value_or(1));
        named([&](size_t i) {
            Objective &o = _progress->objectives[i];
            if (o.className != "CCountMissionObjective")
                return;
            o.count += count;
            if (o.count >= o.needed) {
                o.count = o.needed;
                setObjectiveStatus(i, ObjectiveStatus::kComplete);
            }
        });
    } else if (kind == "CSetMissionObjectiveDisplayStatusAction") {
        const bool visible = readText(action, "Display Status") != "Hidden";
        named([&](size_t i) { _progress->objectives[i].visible = visible; });
    } else if (kind == "CStarTrekRelocateTeamAction" || kind == "CStarTrekRelocateAction" ||
               kind == "CStarTrekRelocateGroupAction") {
        // The officers in its Touching Poly, or else Who To Switch (the
        // instigator, or an officer by name), only those chosen with Only
        // Relocated Selected Members, go to New Location on New Map Name,
        // which is then shown with Make this map active (inferred: the
        // program's switching isn't traced).
        Relocation r;
        r.map = readText(action, "New Map Name");
        r.location = readText(action, "New Location");
        r.show = action->integer("Make this map active").value_or(1) != 0;
        const bool chosenOnly = action->integer("Only Relocated Selected Members").value_or(0) != 0;
        const std::string poly = readText(action, "Touching Poly");
        const std::string who = lower(readText(action, "Who To Switch"));
        std::vector<std::pair<float, float>> area;
        for (size_t part : partsNamed(poly))
            if (_map.parts[part].polygon.size() >= 3)
                area = _map.parts[part].polygon;
        for (size_t i = 0; i < _officers.size(); ++i) {
            const Officer &o = _officers[i];
            if (!o.present || !o.alive() || (chosenOnly && !o.chosen))
                continue;
            bool in = false;
            if (!area.empty())
                in = inside(area, o.x, o.y);
            else if (kind == "CStarTrekRelocateGroupAction")
                // Its Group Name, the movement group a process's
                // CGotoTargetsAI sent (Mission 18's "video room #1-5#"):
                // whoever set it off and those chosen with them (inferred).
                in = int(i) == instigator || o.chosen;
            else if (who == "$instigator")
                in = int(i) == instigator;
            else
                in = lower(_team[o.member].name()) == who || lower(_team[o.member].id) == who;
            if (in)
                r.officers.push_back(int(i));
        }
        if (!r.officers.empty())
            _relocations.push_back(std::move(r));
    } else if (kind == "CDiscoverInventoryNextBriefingAction") {
        // Found: described in the next briefing (5 in the maps).
        const std::string item = readText(action, "Item to Discover");
        if (std::find(_progress->discover.begin(), _progress->discover.end(), item) == _progress->discover.end())
            _progress->discover.push_back(item);
    } else if (kind == "CSetNextMissionAction") {
        _progress->nextMission = readText(action, "Next Mission");
    } else if (kind == "CDisplayMissionCompletedScreenAction" || kind == "CDisplayMissionFailedScreenAction") {
        endMission(kind == "CDisplayMissionCompletedScreenAction", readText(action, "Script"),
                   readText(action, "Mission Over Text"));
    } else {
        return false;
    }
    return true;
}

} // namespace reluva::stawayteam
