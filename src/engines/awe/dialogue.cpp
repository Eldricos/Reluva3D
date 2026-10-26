// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/dialogue.h"

#include "common/fs.h"

#include <cstdlib>

namespace reluva::awe {

namespace {

// A cell, "" for the tables' "0" (none).
std::string cellOf(const Table& t, size_t row, int column) {
    const std::string& c = t.cell(row, column);
    return c == "0" ? std::string() : c;
}

} // namespace

std::string DialogueLine::voiceFile() const {
    return "sound/vo/" + fs::toLower(name + act + part + thread + nest) + ".wav";
}

std::string DialogueLine::eventFile() const {
    return "text/dialogue/events/" + fs::toLower(name + "_" + act + "_" + part + thread + nest) + ".txt";
}

void DialogueBook::add(const Table& t) {
    const int name = t.column("Name"), act = t.column("Act"), part = t.column("Conver."), thread = t.column("Thread"),
              nest = t.column("Nest"), speaker = t.column("Speaker"), target = t.column("Target"),
              line = t.column("Line"), condition = t.column("Condition"), repeat = t.column("Repeat"),
              before = t.column("Prevent"), event = t.column("Event"), after = t.column("Povent"),
              duration = t.column("Duration"), optional = t.column("Optional");
    if (name < 0 || line < 0)
        return;
    for (size_t r = 0; r < t.rows.size(); r++) {
        DialogueLine l;
        l.name = fs::toLower(t.cell(r, name));
        if (l.name.empty())
            continue;
        // Act, part, thread and nest as they are ("02a", "a", "1"), not
        // "0" for none: a part or thread can be called 0.
        l.act = fs::toLower(t.cell(r, act));
        l.part = fs::toLower(t.cell(r, part));
        l.thread = fs::toLower(t.cell(r, thread));
        l.nest = fs::toLower(t.cell(r, nest));
        l.speaker = fs::toLower(cellOf(t, r, speaker));
        l.target = fs::toLower(cellOf(t, r, target));
        l.text = t.cell(r, line);
        l.condition = cellOf(t, r, condition);
        l.repeat = cellOf(t, r, repeat);
        l.optional = cellOf(t, r, optional);
        l.before = cellOf(t, r, before);
        l.event = cellOf(t, r, event);
        l.after = cellOf(t, r, after);
        l.duration = float(std::atof(t.cell(r, duration).c_str()));
        conversations_[l.name].push_back(std::move(l));
    }
}

const std::vector<DialogueLine>& DialogueBook::conversation(std::string_view name) const {
    static const std::vector<DialogueLine> none;
    const auto it = conversations_.find(fs::toLower(name));
    return it == conversations_.end() ? none : it->second;
}

} // namespace reluva::awe
