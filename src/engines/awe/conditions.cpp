// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/conditions.h"

#include "common/fs.h"

#include <cctype>
#include <cstdlib>
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

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    for (size_t start = 0;;) {
        const size_t at = s.find(sep, start);
        out.push_back(trim(s.substr(start, at == std::string_view::npos ? at : at - start)));
        if (at == std::string_view::npos)
            return out;
        start = at + 1;
    }
}

// `value` against an argument: "<n", ">n", "n", or none (not 0).
template <typename T> bool compare(const T& value, std::string_view arg, const T& zero, T (*parse)(std::string_view)) {
    if (arg.empty())
        return value != zero;
    if (arg.front() == '<')
        return value < parse(trim(arg.substr(1)));
    if (arg.front() == '>')
        return value > parse(trim(arg.substr(1)));
    return value == parse(arg);
}

int parseNumber(std::string_view s) { return std::atoi(std::string(s).c_str()); }
std::string parseWord(std::string_view s) { return fs::toLower(s); }

bool testClause(std::string_view clause, const GameState& state) {
    bool negate = false;
    while (!clause.empty() && clause.front() == '!')
        negate = !negate, clause = trim(clause.substr(1));
    if (clause.empty())
        return !negate;
    const size_t space = clause.find(' ');
    const std::string word = fs::toLower(clause.substr(0, space));
    const std::string_view arg = space == std::string_view::npos ? std::string_view() : trim(clause.substr(space + 1));
    bool result;
    if (word == "tag") {
        // "tag name [n]": the tag's name, then how it compares.
        const size_t space2 = arg.find(' ');
        const std::string name = fs::toLower(arg.substr(0, space2));
        const std::string_view value = space2 == std::string_view::npos ? std::string_view() : trim(arg.substr(space2 + 1));
        const auto it = state.tags.find(name);
        if (value.empty())
            result = it != state.tags.end();
        else
            result = it != state.tags.end() && compare<int>(it->second, value, 0, parseNumber);
    } else if (word == "has")
        result = state.items.count(fs::toLower(arg)) != 0;
    else if (word == "act")
        // Acts are numbered with a letter ("06a"), so they compare as text.
        result = !state.act.empty() && compare<std::string>(state.act, arg, std::string(), parseWord);
    else if (!word.empty() && (std::isdigit(uint8_t(word[0])) || word[0] == '-'))
        result = parseNumber(word) != 0;
    else {
        const auto v = state.values.find(word);
        result = compare<int>(v == state.values.end() ? 0 : v->second, arg, 0, parseNumber);
    }
    return result != negate;
}

} // namespace

bool testCondition(std::string_view condition, const GameState& state) {
    condition = trim(condition);
    if (condition.empty())
        return true;
    for (std::string_view alternative : split(condition, '|')) {
        bool all = true;
        for (std::string_view clause : split(alternative, ','))
            all = all && testClause(clause, state);
        if (all)
            return true;
    }
    return false;
}

} // namespace reluva::awe
