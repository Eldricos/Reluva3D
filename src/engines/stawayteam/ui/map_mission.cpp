// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A mission's objectives, as the triggers change them, and its end
// (docs/engines/stawayteam/systems/objectives.md).

#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

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

std::string MapScreen::objectiveText(const Objective &o) const {
    // Its line of its script, a count's "%d" its count so far ("(%d of 2)").
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
    return text;
}

void MapScreen::setObjectives(MissionObjectives objectives) {
    // The mission's own first, then those the map has added already (its
    // spawn points').
    _mission = std::move(objectives);
    _objectives.insert(_objectives.begin(), _mission.objectives.begin(), _mission.objectives.end());
}

std::vector<ObjectiveLine> MapScreen::objectiveLines() const {
    std::vector<ObjectiveLine> out;
    for (const Objective &o : _objectives)
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
    _objectives.push_back(std::move(o));
}

void MapScreen::setObjectiveStatus(size_t objective, ObjectiveStatus status) {
    Objective &o = _objectives[objective];
    if (o.status == status)
        return;
    o.status = status;
    const TextObject *then = status == ObjectiveStatus::kComplete ? o.successAction
                             : status == ObjectiveStatus::kFailed ? o.failAction
                                                                  : nullptr;
    runAction(then, -1);
    // With every objective required to end the mission complete, its Win
    // Action, once (inferred: the manager's check isn't traced).
    if (_won || !_mission.winAction)
        return;
    bool required = false;
    for (const Objective &each : _objectives)
        if (each.required && each.status != ObjectiveStatus::kDisabled) {
            if (each.status != ObjectiveStatus::kComplete)
                return;
            required = true;
        }
    if (required) {
        _won = true;
        runAction(_mission.winAction, -1);
    }
}

void MapScreen::officerFell(size_t officer) {
    // Who's protected has failed to be (inferred).
    for (size_t i = 0; i < _objectives.size(); ++i)
        if (_objectives[i].officer == int(officer) && _objectives[i].className == "CProtectMissionObjective" &&
            _objectives[i].status == ObjectiveStatus::kIncomplete)
            setObjectiveStatus(i, ObjectiveStatus::kFailed);
}

void MapScreen::runSpawnActions() {
    // Each spawn point's Spawn Action, its officer the instigator: Mission
    // 01's add an objective to protect them (inferred: run as the team's
    // put there).
    for (size_t i = 0; i < _officers.size(); ++i) {
        const std::string spawn = "away team spawn point " + std::to_string(i + 1);
        for (const MapPart &p : _map.parts)
            if (lower(p.name) == spawn)
                for (const TextObject *ai : p.activities)
                    if (ai->className == "CSpawnPointAI")
                        runAction(ai->object("Spawn Action"), int(i));
    }
}

void MapScreen::endMission(bool completed, const std::string &script, const std::string &line) {
    if (_endGame)
        return;
    _completed = completed;
    _missionOverText.clear();
    std::vector<uint8_t> bytes;
    if (!_data.read("Levels/" + script + ".RDS", bytes)) {
        const RdsScript rds = readRdsScript(std::string(bytes.begin(), bytes.end()));
        if (const std::string *text = rds.find(line))
            _missionOverText = *text;
    }
    _objectivesWindow.reset();
    _endGame = std::make_unique<EndGameWindow>(_data, _rules, completed, _missionOverText, kScreenW, kScreenH);
}

bool MapScreen::runMissionAction(const TextObject *action, int instigator) {
    const std::string &kind = action->className;
    const auto named = [&](const std::function<void(size_t)> &each) {
        const std::string name = readText(action, "Objective Name");
        for (size_t i = 0; i < _objectives.size(); ++i)
            if (_objectives[i].name == name)
                each(i);
    };
    if (kind == "CAddMissionObjectiveAction") {
        if (const TextObject *o = action->object("Objective"))
            addObjective(readObjective(*o), instigator);
    } else if (kind == "CSetMissionObjectiveStatusAction") {
        const ObjectiveStatus status = objectiveStatus(readText(action, "Status"));
        named([&](size_t i) { setObjectiveStatus(i, status); });
    } else if (kind == "CSetMissionObjectiveCountAction") {
        // Its Count added (always 1 in the missions), complete at Count
        // Before Success (inferred).
        const int count = int(action->integer("Count").value_or(1));
        named([&](size_t i) {
            Objective &o = _objectives[i];
            o.count += count;
            if (o.needed > 0 && o.count >= o.needed && o.status == ObjectiveStatus::kIncomplete)
                setObjectiveStatus(i, ObjectiveStatus::kComplete);
        });
    } else if (kind == "CSetMissionObjectiveDisplayStatusAction") {
        const bool visible = readText(action, "Display Status") != "Hidden";
        named([&](size_t i) { _objectives[i].visible = visible; });
    } else if (kind == "CSetNextMissionAction") {
        _nextMission = readText(action, "Next Mission");
    } else if (kind == "CDisplayMissionCompletedScreenAction" || kind == "CDisplayMissionFailedScreenAction") {
        endMission(kind == "CDisplayMissionCompletedScreenAction", readText(action, "Script"),
                   readText(action, "Mission Over Text"));
    } else {
        return false;
    }
    return true;
}

} // namespace reluva::stawayteam
