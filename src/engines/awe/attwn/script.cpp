// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/script.h"

#include "common/fs.h"
#include "engines/awe/game/conditions.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::awe::attwn {

namespace {

// A tag's value as the game's store gives it (0x412700): -99 if it's unset,
// which these words take as 0.
int tagOr0(const GameState &state, const std::string &name) {
    const auto it = state.tags.find(name);
    return it == state.tags.end() || it->second == -99 ? 0 : it->second;
}

bool set(const GameState &state, const char *tag) {
    const auto it = state.tags.find(tag);
    return it != state.tags.end() && it->second != -99;
}

// The machines (0x40e2d0): what trying each runs.
std::string machine(const GameState &state, const std::string &name, const std::string &part) {
    const bool ready = machineReady(state, name);
    if (name == "applepress") {
        if (!ready) {
            // What's missing, the apples first; without the cheesecloth
            // alone, nothing.
            if (!set(state, "pressapples"))
                return "talk kitchen 5";
            if (!set(state, "presspropellor"))
                return "talk kitchen 7";
            return set(state, "pressglass") ? "" : "talk kitchen 8";
        }
        // Cider of the apples from the basket, if there's no cider yet;
        // else juice (0x40e3b1).
        const bool cider = !state.itemState("ciderglass") && state.findItem("basketofapples", {4});
        return std::string("sound sfx\\applepress, hide handletop, show applepressdown, wait 1, sounda sfx\\juice, ") +
               (cider ? "show glassfulldark" : "show glassfulllight") + ", hide glassempty, wait 1, " +
               (cider ? "take ciderglass" : "take juiceglass") +
               ", hide glassfulldark, hide glassfulllight, hide apples, hide applepressdown, show handletop, "
               "removetag pressapples, removetag pressglass, " +
               (cider ? "talk kitchen 10" : "talk kitchen 9");
    }
    if (name == "gramophone") {
        if (!set(state, "gramophone_needle") || !set(state, "gramophone_replacerecord"))
            return !state.itemState("record") && set(state, "gramophone_needle") ? "talk entryway 5"
                                                                                 : "talk narracottgen 2";
        const bool tubes = set(state, "gramophone_tube1") && set(state, "gramophone_tube2");
        return std::string("show switch, show recordturn, ") + (tubes ? "talk entryway 4" : "talk narracottgen 140") +
               ", hide recordturn, hide switch";
    }
    if (name == "projector") {
        // A film in it: one of the three put in, or the BBC's not taken
        // out (0x40e6c3).
        const bool home = set(state, "projector_moviehome");
        const bool hello = set(state, "projector_moviehello");
        const bool film = set(state, "projector_moviebbc") || home || hello || !state.itemState("moviebbc");
        const bool screenUp = set(state, "screenup");
        std::string out =
            std::string("show switch, sounda sfx\\filmloop loop, ") + (film ? "show filmspin" : "show emptyspin");
        if (!set(state, "projector_bulb")) {
            out += ", talk narracottgen 139";
        } else {
            out += ", fade out, ";
            if (!screenUp && film && !home && hello)
                return out + "movie clip-hello, movie credits, reset, scene mainmenu01"; // the end (0x40e863)
            out += screenUp ? "movie clip-ninemore inscene silent"
                   : !film  ? "movie clip-blank inscene silent"
                   : home   ? "movie clip-home inscene silent"
                            : "movie clip-bbc inscene silent";
            out += ", fade in, ";
            out += screenUp ? "talk screeningroom 10"
                   : !film  ? "talk narracottgen 38 2"
                   : home   ? "talk narracottgen 137"
                            : "talk narracottgen 136";
        }
        return out + (film ? ", hide filmspin" : ", hide emptyspin") + ", stop filmloop, hide switch";
    }
    if (name == "parlorradio") {
        // Its music again first (0x40e935): the radio plays once it works.
        if (ready)
            return "updatemusic, wait 1, talk parlor 8";
        if (set(state, "parlorradio_tube1") && set(state, "parlorradio_tube2") && set(state, "parlorradio_tube3") &&
            set(state, "parlorradio_power"))
            return "updatemusic, talk parlor 6";
        if (part == "power" && set(state, "parlorradio_power"))
            return "updatemusic, talk parlor 7";
        return "updatemusic";
    }
    if (name == "saferadio") {
        if (!ready)
            return "updatemusic";
        return std::string("updatemusic, wait 1, ") +
               (set(state, "saferadio_mic") ? "talk narracottgen 91" : "talk saferoom 14");
    }
    return "";
}

} // namespace

bool machineReady(const GameState &state, const std::string &machine) {
    static const std::vector<std::pair<std::string, std::vector<const char *>>> kParts = {
        {"applepress", {"presspropellor", "presscheesecloth", "pressapples", "pressglass"}},
        {"parlorradio",
         {"parlorradio_tube1", "parlorradio_tube2", "parlorradio_tube3", "parlorradio_power", "parlorradio_wire"}},
        {"saferadio", {"saferadio_tube1", "saferadio_tube2", "saferadio_lamp"}},
    };
    for (const auto &[name, tags] : kParts)
        if (name == machine)
            return std::all_of(tags.begin(), tags.end(), [&](const char *t) { return set(state, t); });
    return false;
}

bool condition(const GameState &state, const std::vector<std::string> &words, bool &holds) {
    if (!words.empty() && words[0] == "test") {
        // "test <machine>" (0x40a8b2)
        holds = machineReady(state, fs::toLower(words.size() > 1 ? words[1] : std::string()));
        return true;
    }
    if (words.empty() || words[0] != "suspicion")
        return false;
    const int value = tagOr0(state, "suspicion_" + fs::toLower(words.size() > 1 ? words[1] : std::string()));
    const std::string n = words.size() > 2 ? words[2] : std::string();
    if (!n.empty() && n[0] == '<')
        holds = value < std::atoi(n.c_str() + 1);
    else if (!n.empty() && n[0] == '>')
        holds = value > std::atoi(n.c_str() + 1);
    else
        holds = value == std::atoi(n.c_str());
    return true;
}

bool verb(GameState &state, const std::vector<std::string> &words, std::string &then) {
    if (!words.empty() && fs::toLower(words[0]) == "test") {
        then = machine(state, fs::toLower(words.size() > 1 ? words[1] : std::string()),
                       fs::toLower(words.size() > 2 ? words[2] : std::string()));
        return true;
    }
    if (!words.empty() && fs::toLower(words[0]) == "suspicion") {
        // "suspicion <who> [+|-]n" (0x40d3df): the tag set, or moved by n
        // from what it was (0 if unset), kept to 0 or 1.
        const std::string tag = "suspicion_" + (words.size() > 1 ? fs::toLower(words[1]) : std::string());
        const std::string n = words.size() > 2 ? words[2] : std::string("0");
        int value = std::atoi(n.c_str() + (n[0] == '+' || n[0] == '-' ? 1 : 0));
        if (!n.empty() && n[0] == '+')
            value = tagOr0(state, tag) + value;
        else if (!n.empty() && n[0] == '-')
            value = tagOr0(state, tag) - value;
        state.tags[tag] = std::clamp(value, 0, 1);
        return true;
    }
    if (words.empty() || fs::toLower(words[0]) != "inc")
        return false;
    // The act's name as the game writes it ("%02d%c", 0x4028c0), against
    // the third to fifth words, whatever their case (0x402a20).
    bool named = false;
    for (size_t i = 2; i < words.size() && i < 5; i++)
        named |= !state.act.empty() && fs::toLower(words[i]) == fs::toLower(state.act);
    if (named) {
        const bool needed = words.size() > 1 && fs::toLower(words[1]) == "needed";
        const std::string tag = fs::toLower(state.act) + (needed ? "_neededcounter" : "_possiblecounter");
        state.tags[tag] = tagOr0(state, tag) + 1;
    }
    return true;
}

std::string movie(const GameState &state, const std::string &name) {
    if (name != "coda")
        return name;
    const bool lombard = set(state, "9a_lombardsaved");
    const bool vera = set(state, "10a_verasaved");
    return lombard && vera ? "coda#2bothalive"
           : lombard       ? "coda#3lombardalive"
           : vera          ? "coda#4veraalive"
                           : "coda#1neitheralive";
}

} // namespace reluva::awe::attwn
