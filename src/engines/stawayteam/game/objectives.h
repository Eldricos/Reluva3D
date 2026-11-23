// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stawayteam/formats/text_object.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/** An objective's state, in the order the program counts them (0x47fa73). */
enum class ObjectiveStatus { kDisabled, kIncomplete, kComplete, kFailed };

/** The status a mission's files name ("Complete", "Failed"...); Incomplete for any other. */
ObjectiveStatus objectiveStatus(const std::string &name);

/**
 * One of a mission's objectives (`CMissionObjective` and its kinds), as
 * the mission's file or a trigger gives it
 * (docs/engines/stawayteam/systems/objectives.md).
 */
struct Objective {
    std::string name;      // how the triggers name it
    std::string className; // its kind: CCountMissionObjective, CProtectMissionObjective...
    ObjectiveStatus status = ObjectiveStatus::kIncomplete;
    bool visible = true;   // its Display Status
    bool required = false; // Required To End Mission: a primary objective
    int points = 0;
    const TextObject *failAction = nullptr;
    const TextObject *successAction = nullptr;
    std::string script; // a script of Levels/, and its line describing it
    std::string line;
    int needed = 0; // a count objective's Count Before Success
    int count = 0;
    std::string who;  // whom a protect or rescue objective is about ("$instigator", a part's name)
    int officer = -1; // the officer it protects, once known
    std::string item; // a need-item objective's Item Needed
};

/** Reads an objective; its actions point into `o`, which must outlive it. */
Objective readObjective(const TextObject &o);

/**
 * A mission's objectives (its `CMissionObjectiveManager`): those it starts
 * with, and its Win Action.
 */
struct MissionObjectives {
    std::shared_ptr<const TextObject> manager; // keeps the actions alive
    std::vector<Objective> objectives;
    const TextObject *winAction = nullptr;
    int baseScore = 0;
};

/** Reads `manager` (its file's `Objective`), keeping it. */
MissionObjectives readMissionObjectives(std::shared_ptr<const TextObject> manager);

} // namespace reluva::stawayteam
