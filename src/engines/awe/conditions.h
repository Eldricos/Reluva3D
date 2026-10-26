// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The conditions the game's scenes and tables test: when a scene's object
// is shown ("moving >0"), which of an object's scripts runs ("act 04a,
// !tag micheladmonish"), which music plays. See docs/formats/awe/ac2.md.
//
//   condition := alternative ('|' alternative)*
//   alternative := clause (',' clause)*
//   clause := ['!'] word [argument]
//
// Words the game's evaluator knows (motoe.exe 0x407d00..0x408a00) include
// act, chapter, debug, has, location, master, moving, rand, scene, tag,
// talk, time, timer, use, used, wearing and weather. "talk <name> <act>
// <code>" asks a conversation's progress: three letters (part, thread,
// line), whether the thread got past that line; two, whether it was heard
// to its end; one, whether the part was (0x439f60). A number argument
// may start with '<' or '>'; without either it must be equal. "timer
// <name> [n]" compares a timer (-99 if there's none; without a number,
// whether there is one). "beenhere"/"beenthere <scene|this> [act|ever]"
// ask whether a scene was visited in this act (or that act, or ever);
// "lastscene <name>" whether the scene left last was that one.

namespace reluva::awe {

// Where a conversation's thread got to (the story state's list at
// +0x2b8, saved with the game): heard to its end, and the line it
// stopped before (-1 none).
struct TalkProgress {
    int status = 0;
    int nest = -1;
};

struct GameState {
    std::string act;                 // "06a"; empty before the first
    std::map<std::string, int> values; // moving, weather, master...
    std::map<std::string, int> tags; // story tags: numbers set by scripts ("tag name 1")
    std::set<std::string> items;     // what Poirot carries
    // Timers (story state +0x1c): seconds since "timer <name>", plus what
    // "time <n>" added. In order; a name may be there twice (the first
    // counts).
    std::vector<std::pair<std::string, int>> timers;
    // Text tags: "savedscene" ("Calais.1-4_1-2.02.ac2 -1 0"), and a tag
    // per scene visited, by its file name, holding the acts it was
    // visited in ("02a 06b").
    std::map<std::string, std::string> texts;
    std::string scene;     // the scene shown, by its file name ("salon_01_06a.ac2")
    std::string lastScene; // the one before ("lastscene")
    // Conversations' progress, by talkKey(). Only lettered parts.
    std::map<std::string, TalkProgress> talks;
    // "talk <name> <act> <part>": whether every line of the part not
    // marked Optional was said (the dialogue's own test, 0x439e40).
    std::function<bool(const std::string& name, const std::string& act, int part)> partTold;
    // "action <word>": what the save or load screen allows (up, down...).
    std::function<bool(const std::string& word)> action;
};

std::string talkKey(std::string_view name, std::string_view act, int part, int thread);

// Whether `condition` holds. An empty one does; "0" never does. Words not
// known here count as 0 (or as false, for those taking a name). "tag name"
// holds if the tag exists (whatever its value); with a number, by its
// value (0x408557).
bool testCondition(std::string_view condition, const GameState& state);

} // namespace reluva::awe
