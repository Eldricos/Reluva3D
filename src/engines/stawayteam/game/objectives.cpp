// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/objectives.h"

namespace reluva::stawayteam {

namespace {

std::string text(const TextObject &o, const char *key) {
    const std::string *value = o.value(key);
    return value ? *value : std::string();
}

} // namespace

ObjectiveStatus objectiveStatus(const std::string &name) {
    if (name == "Disabled")
        return ObjectiveStatus::kDisabled;
    if (name == "Complete")
        return ObjectiveStatus::kComplete;
    if (name == "Failed")
        return ObjectiveStatus::kFailed;
    return ObjectiveStatus::kIncomplete;
}

Objective readObjective(const TextObject &o) {
    Objective out;
    out.name = text(o, "Objective Name");
    out.className = o.className;
    out.status = objectiveStatus(text(o, "Mission Status"));
    out.visible = text(o, "Display Status") != "Hidden";
    out.required = o.integer("Required To End Mission").value_or(0) != 0;
    out.points = int(o.integer("Points").value_or(0));
    out.failAction = o.object("Fail Action");
    out.successAction = o.object("Success Action");
    out.script = text(o, "Objective Script");
    out.line = text(o, "Objective Line");
    out.needed = int(o.integer("Count Before Success").value_or(0));
    out.count = int(o.integer("Current Count").value_or(0));
    out.who = text(o, "WhoToProtect");
    if (out.who.empty())
        out.who = text(o, "WhoToRescue");
    return out;
}

MissionObjectives readMissionObjectives(std::shared_ptr<const TextObject> manager) {
    MissionObjectives out;
    out.manager = std::move(manager);
    if (!out.manager)
        return out;
    out.winAction = out.manager->object("Win Action");
    out.baseScore = int(out.manager->integer("Base Score for Mission").value_or(0));
    if (const TextObject *list = out.manager->object("Objectives"))
        for (const TextField *item : list->items())
            if (item->object)
                out.objectives.push_back(readObjective(*item->object));
    return out;
}

} // namespace reluva::stawayteam
