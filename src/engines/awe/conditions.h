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
// The game's evaluator (motoe.exe 0x407ae0, a clause 0x407cb0):
//
//   condition := [prefix '(' condition ')'] clauses
//   clauses := clause ((',' | '|') clause)*
//   clause := ['!'] word [argument...]
//
// A part in brackets is worked out first and stands as "1" or "0" (what
// follows its ')' is dropped). Then, if a '|' is anywhere, any clause
// holding will do; else they must all hold.
//
// Words: act, action, beenhere/beenthere, cantalk, chapter, has, lastscene,
// location, master, moving, rand, scene, tag, talk, time, timer, use, used,
// wearing, weather; "1" holds; "0", debug, demo and words it doesn't know
// don't. "talk <name> <act> <code>" asks a conversation's progress: three
// letters (part, thread, line), whether the thread got past that line;
// two, whether it was heard to its end; one, whether the part was
// (0x439f60). A number argument may start with '<' or '>'; without either
// it must be equal. "tag <name> [n]" and "timer <name> [n]" compare a tag
// or timer (-99 if there's none; without a number, whether there is one);
// "tag <name> nouse" holds if it's there and no item is in hand.
// "beenhere"/"beenthere <scene|this> [act|ever]" ask whether a scene was
// visited in this act (or that act, or ever); "lastscene", "scene" and
// "location <name>" whether the scene left last, the scene or the
// location has the name in it. "has <item> [available]" whether the item
// is in the inventory (and not used up or given away: state 1 or 2);
// "has all passport|fingerprint" whether every passport or fingerprint
// card is; "used <item>" whether it's used up (state 4); "use <item>"
// whether it's the item in hand.

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
    // The inventory (0x49881c): every item Poirot has, whatever its state,
    // and the states other than 2 (in the bag): 1 just taken, 3 in hand, 4
    // used up (kept for "restore"), 5 a passport, 6 a fingerprint card, 7
    // taken away by "remove all". `held` is the item in hand (the cursor).
    std::set<std::string> items;
    std::map<std::string, int> itemStates;
    std::string held;
    // Where the items are kept in the inventory window (0x41bc10): each
    // item's slot (16 to a page), the page shown, and the item just taken
    // that blinks there (one at a time).
    std::map<std::string, int> itemSlots;
    std::vector<std::string> itemOrder; // in the order they came
    int itemPage = 0;
    std::string fresh;
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
    // "cantalk <name> <code> [options]": whether that "talk" would start
    // (the dialogue's dry run, 0x407f24).
    std::function<bool(const std::vector<std::string>& words)> canTalk;
    std::string location; // the location's folder ("calais.hall"), for "location"

    // An item's state: 0 if it isn't there.
    int itemState(const std::string& item) const;
    // Whether it's in the bag or just taken (1, 2), for "has <x> available".
    bool itemAvailable(const std::string& item) const;
};

std::string talkKey(std::string_view name, std::string_view act, int part, int thread);

// Whether `condition` holds. An empty one does, and "1"; "0" never does.
// "tag name" holds if the tag exists (whatever its value); with a number,
// by its value (0x408557).
bool testCondition(std::string_view condition, const GameState& state);

} // namespace reluva::awe
