// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/tables.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

// The game's dialogue: Text/Dialogue/act<NN><x>.txt for each act, and
// gen_antoinette.txt, gen_comments.txt and gen_characters.txt for every
// act. A conversation (its Name) has parts (Conver.), each of threads
// (Thread) of lines in order (Nest). See docs/formats/awe/dialogue.md.

namespace reluva::awe {

struct DialogueLine {
    std::string name, act, part, thread, nest; // where it is
    std::string speaker, target;               // "antoinette"; whom they face ("antoinette up")
    std::string text;
    std::string condition;                     // when it can be said
    std::string repeat, optional;
    std::string before, event, after;          // commands: Prevent, Event, Povent
    float duration = 0;

    // Its recording: sound/vo/<name><act><part><thread><nest>.wav
    // ("dia_observation02aa1a.wav").
    std::string voiceFile() const;
    // Its event file, if it has one: text/dialogue/events/<name>_<act>_
    // <part><thread><nest>.txt ("dia_countess_07a_bbb.txt").
    std::string eventFile() const;
};

class DialogueBook {
public:
    // Adds a table's lines (a later table's conversation of the same name
    // adds to it).
    void add(const Table& table);
    // A conversation's lines in table order; empty if there's none.
    const std::vector<DialogueLine>& conversation(std::string_view name) const;
    size_t conversationCount() const { return conversations_.size(); }
    const std::map<std::string, std::vector<DialogueLine>>& conversations() const { return conversations_; }

private:
    std::map<std::string, std::vector<DialogueLine>> conversations_; // by lower-case name
};

} // namespace reluva::awe
