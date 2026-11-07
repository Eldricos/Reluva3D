// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/conditions.h"

#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace reluva::awe {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

// A clause's words, split at spaces (0x43ea30).
std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            i++;
        const size_t start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t')
            i++;
        if (i > start)
            out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

// `value` against an argument: "<n", ">n" or "n" (wtoi: "" is 0).
bool compareNumber(int value, std::string_view arg) {
    auto number = [](std::string_view s) { return std::atoi(std::string(s).c_str()); };
    if (!arg.empty() && arg.front() == '<')
        return value < number(arg.substr(1));
    if (!arg.empty() && arg.front() == '>')
        return value > number(arg.substr(1));
    return value == number(arg);
}

// The case-blind "has it in it" of 0x43e750.
bool within(std::string_view text, std::string_view part) {
    return fs::toLower(text).find(fs::toLower(part)) != std::string::npos;
}

int valueOf(const GameState& state, const char* name) {
    const auto v = state.values.find(name);
    return v == state.values.end() ? 0 : v->second;
}

// Every passport, and every fingerprint card, for "has all" (0x419560).
constexpr const char* kPassports[] = {
    "passarbuthnot", "passconstantine", "passcount",       "passdebenham", "passdragomiroff", "passengineer",
    "passfireman",   "passfoscarelli",  "passhardman",     "passhardmanreal", "passhubbard",   "passklaus",
    "passlucien",    "passmacqueen",    "passmasterman",   "passmatteo",   "passmichel",      "passohlsson",
    "passschmidt"};
constexpr const char* kPrints[] = {
    "printarbuthnot", "printconstantine", "printcount",    "printcountess", "printdebenham", "printdragomiroff",
    "printengineer",  "printfireman",     "printfoscarelli", "printhardman", "printhubbard",  "printklaus",
    "printlucien",    "printmacqueen",    "printmasterman", "printmatteo",  "printmichel",   "printohlsson",
    "printschmidt"};

bool testClause(std::string_view clause, const GameState& state) {
    // '!' first, or a space then '!' (0x407cb0).
    clause = trim(clause);
    bool negate = false;
    if (!clause.empty() && clause.front() == '!')
        negate = true, clause = trim(clause.substr(1));
    const std::vector<std::string> w = words(clause);
    if (w.empty())
        return !negate;
    const std::string word = fs::toLower(w[0]);
    auto arg = [&](size_t i) -> std::string { return i < w.size() ? w[i] : std::string(); };
    bool result = false;
    if (word == "1") {
        result = true;
    } else if (word == "0") {
        result = false;
    } else if (word == "act" || word == "chapter") {
        // "act [<|>]06a" by number, then letter (0x407d84); "chapter" by
        // number alone.
        int major = 0, minor = 0;
        if (state.act.size() >= 2) {
            major = std::atoi(state.act.substr(0, state.act.size() - 1).c_str());
            minor = std::toupper(uint8_t(state.act.back())) - 0x40;
        }
        std::string a = arg(1);
        char op = 0;
        if (!a.empty() && (a[0] == '<' || a[0] == '>'))
            op = a[0], a.erase(0, 1);
        const int wantMajor = a.empty() ? 0 : std::atoi(a.substr(0, a.size() - 1).c_str());
        const int wantMinor = a.empty() ? 0 : std::toupper(uint8_t(a.back())) - 0x40;
        const int have = word == "act" ? major * 64 + minor : major;
        const int want = word == "act" ? wantMajor * 64 + wantMinor : wantMajor;
        if (state.act.empty())
            result = false;
        else
            result = op == '<' ? have < want : op == '>' ? have > want : have == want;
    } else if (word == "action") {
        // The save or load screen's (0x407ec4); false without one.
        result = state.action && state.action(fs::toLower(arg(1)));
    } else if (word == "beenhere" || word == "beenthere") {
        // beenhere <scene|this> [act|ever] (0x408a2f): the scene's visit
        // tag holds the acts it was visited in.
        std::string scene = fs::toLower(arg(1));
        const std::string when = fs::toLower(arg(2));
        if (scene == "this")
            scene = fs::toLower(state.scene);
        else
            scene += ".ac2";
        const auto it = state.texts.find(scene);
        if (it == state.texts.end())
            result = false;
        else if (when == "ever")
            result = true;
        else
            result = within(it->second, when.empty() ? state.act : when);
    } else if (word == "cantalk") {
        // A dry run of "talk" with the same words (0x407f24).
        std::vector<std::string> talk(w.begin() + 1, w.end());
        result = state.canTalk && state.canTalk(talk);
    } else if (word == "has") {
        // has <item> [available] | has all passport|fingerprint (0x4082e5).
        const std::string item = fs::toLower(arg(1));
        if (item == "all") {
            const std::string kind = fs::toLower(arg(2));
            if (within(kind, "passport") || within(kind, "fingerprint")) {
                result = true;
                if (within(kind, "passport")) {
                    for (const char* p : kPassports)
                        result = result && state.itemState(p) != 0;
                } else {
                    for (const char* p : kPrints)
                        result = result && state.itemState(p) != 0;
                }
            }
        } else if (fs::toLower(arg(2)) == "available") {
            result = state.itemAvailable(item);
        } else {
            result = state.itemState(item) != 0;
        }
    } else if (word == "lastscene") {
        result = within(state.lastScene, arg(1));
    } else if (word == "location") {
        result = within(state.location, arg(1));
    } else if (word == "master") {
        result = valueOf(state, "master") != 0;
    } else if (word == "moving" || word == "time" || word == "weather") {
        result = compareNumber(valueOf(state, word.c_str()), arg(1));
    } else if (word == "rand") {
        // Holds one time in n (0x4084c7).
        const int n = std::atoi(arg(1).c_str());
        result = n != 0 && std::rand() % n == 0;
    } else if (word == "scene") {
        result = within(state.scene, arg(1));
    } else if (word == "tag" || word == "timer") {
        // -99 for one that isn't there (0x4109f0, 0x410b40). "tag <name>
        // nouse": no item may be in hand.
        const std::string name = fs::toLower(arg(1));
        std::string value = arg(2);
        int v = -99;
        if (word == "tag") {
            const auto it = state.tags.find(name);
            if (it != state.tags.end())
                v = it->second;
        } else {
            for (const auto& [n, t] : state.timers)
                if (n == name) {
                    v = t;
                    break;
                }
        }
        if (word == "tag" && fs::toLower(value) == "nouse") {
            if (!state.held.empty())
                return negate;
            value.clear();
        }
        result = value.empty() ? v != -99 : compareNumber(v, value);
    } else if (word == "talk") {
        // talk <name> <act> <code> (0x408636 -> 0x439f60).
        if (w.size() >= 4) {
            const std::string name = fs::toLower(w[1]), act = fs::toLower(w[2]), code = fs::toLower(w[3]);
            auto letter = [](char c) { return std::toupper(uint8_t(c)) - 'A'; };
            if (code.size() == 1) {
                result = state.partTold && state.partTold(name, act, letter(code[0]));
            } else if (code.size() >= 2) {
                const auto it = state.talks.find(talkKey(name, act, letter(code[0]), letter(code[1])));
                if (it != state.talks.end()) {
                    const TalkProgress& p = it->second;
                    result = p.status != 0 || p.nest == -1 || (code.size() >= 3 && p.nest > letter(code[2]));
                }
            }
        }
    } else if (word == "use") {
        // The item in hand is that one (0x408885); "use" alone always holds.
        result = arg(1).empty() || (!state.held.empty() && fs::toLower(state.held) == fs::toLower(arg(1)));
    } else if (word == "used") {
        result = state.findItem(arg(1), {4}) != nullptr; // 0x41ba20
    }
    // Anything else (debug, demo, test, words it doesn't know) doesn't
    // hold; nor does "wearing" (what the player's model wears, 0x426260),
    // which nothing asks.
    return result != negate;
}

} // namespace

const GameState::Item* GameState::findItem(const std::string& name, std::initializer_list<int> states) const {
    const std::string n = fs::toLower(name);
    for (const Item& i : items)
        if (i.name == n && (states.size() == 0 || std::find(states.begin(), states.end(), i.state) != states.end()))
            return &i;
    return nullptr;
}

GameState::Item* GameState::findItem(const std::string& name, std::initializer_list<int> states) {
    return const_cast<Item*>(std::as_const(*this).findItem(name, states));
}

int GameState::itemState(const std::string& item) const {
    const Item* i = findItem(item);
    return i ? i->state : 0;
}

bool GameState::itemAvailable(const std::string& item) const { return findItem(item, {1, 2}) != nullptr; }

int GameState::freeSlot() const {
    int slot = 0;
    for (bool taken = true; taken; slot += taken) {
        taken = false;
        for (const Item& i : items)
            taken |= i.slot == slot && i.state >= 1 && i.state <= 3;
    }
    return slot;
}

std::string talkKey(std::string_view name, std::string_view act, int part, int thread) {
    return fs::toLower(name) + "|" + fs::toLower(act) + "|" + std::to_string(part) + "|" + std::to_string(thread);
}

bool testCondition(std::string_view condition, const GameState& state) {
    // 0x407ae0: none, "" or "1" holds.
    std::string s(trim(condition));
    if (s.empty() || s == "1")
        return true;
    // A part in brackets first, standing as 1 or 0 after what comes before
    // it; what follows its ')' goes.
    if (const size_t open = s.find('('); open != std::string::npos) {
        std::string inner = s.substr(open + 1);
        if (const size_t close = inner.find(')'); close != std::string::npos)
            inner.resize(close);
        s = s.substr(0, open) + (testCondition(inner, state) ? "1" : "0");
    }
    // A '|' anywhere: any clause will do; else all must hold. Empty clauses
    // are skipped (wcstok).
    const bool any = s.find('|') != std::string::npos;
    for (size_t start = 0; start <= s.size();) {
        const size_t at = s.find_first_of(",|", start);
        const std::string_view clause = trim(std::string_view(s).substr(start, at == std::string::npos ? at : at - start));
        if (!clause.empty()) {
            const bool holds = testClause(clause, state);
            if (state.onTest)
                state.onTest(clause, holds);
            if (any && holds)
                return true;
            if (!any && !holds)
                return false;
        }
        if (at == std::string::npos)
            break;
        start = at + 1;
    }
    return !any;
}

} // namespace reluva::awe
