// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "devtools/test_runs.h"

#include <algorithm>
#include <regex>
#include <set>

namespace reluva::devtools {

namespace {

std::vector<std::string> split(std::string_view s, char by) {
    std::vector<std::string> parts;
    size_t from = 0;
    while (true) {
        const size_t at = s.find(by, from);
        parts.emplace_back(s.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from));
        if (at == std::string_view::npos)
            return parts;
        from = at + 1;
    }
}

// The variables cmake/ReluvaGamedata.cmake reads besides each game's.
constexpr const char *kRecords = "RELUVA_GAMEDATA_RECORDS";
constexpr const char *kRecordsRoot = "RELUVA_GAMEDATA_RECORDS_ROOT";
constexpr const char *kUpdate = "RELUVA_GAMEDATA_UPDATE";
constexpr const char *kTarget = "RELUVA_GAMEDATA_TARGET";

} // namespace

std::vector<TestEntry> readTestList(std::string_view text) {
    std::vector<TestEntry> tests;
    for (std::string line : split(text, '\n')) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::vector<std::string> fields = split(line, '\t');
        if (fields.empty() || fields[0].empty() || fields[0][0] == '#')
            continue;
        TestEntry t;
        t.name = fields[0];
        if (fields.size() > 1)
            for (const std::string &label : split(fields[1], ','))
                if (!label.empty())
                    t.labels.push_back(label);
        if (fields.size() >= 5) {
            t.engine = fields[2];
            t.gameId = fields[3];
            t.variable = fields[4];
        }
        tests.push_back(std::move(t));
    }
    return tests;
}

std::vector<CtestRun> planRuns(const std::vector<TestEntry> &all, const std::vector<std::string> &tests,
                               const std::vector<TestGame> &games, Records records, const std::string &recordsFolder) {
    std::map<std::string, const TestEntry *> byName;
    std::set<std::string> variables;
    for (const TestEntry &t : all) {
        byName[t.name] = &t;
        if (t.needsGame())
            variables.insert(t.variable);
    }

    // The k-th entry for a game goes in run k: one folder per variable a run.
    std::vector<CtestRun> runs(1);
    std::map<std::string, int> entriesSeen; // by engine and game id
    std::vector<std::pair<const TestGame *, size_t>> placed;
    for (const TestGame &g : games) {
        const size_t run = size_t(entriesSeen[g.engine + "/" + g.gameId]++);
        if (run >= runs.size())
            runs.resize(run + 1);
        placed.emplace_back(&g, run);
    }
    for (const std::string &name : tests) {
        const auto it = byName.find(name);
        if (it == byName.end())
            continue;
        const TestEntry &t = *it->second;
        bool onAGame = false;
        if (t.needsGame())
            for (const auto &[g, run] : placed)
                if (g->engine == t.engine && g->gameId == t.gameId) {
                    runs[run].tests.push_back(t.name);
                    runs[run].set[t.variable] = g->path;
                    runs[run].on[t.name] = g->target;
                    onAGame = true;
                }
        // A step with no game picked is still run: it finds the game in the
        // launcher's list itself, or is skipped.
        if (!onAGame)
            runs[0].tests.push_back(t.name);
    }

    std::vector<CtestRun> planned;
    for (CtestRun &run : runs) {
        if (run.tests.empty())
            continue;
        switch (records) {
        case Records::kCommitted:
            break;
        case Records::kCompareMine:
            run.set[kRecordsRoot] = recordsFolder;
            break;
        case Records::kMakeMine:
            run.set[kRecordsRoot] = recordsFolder;
            run.set[kUpdate] = "1";
            break;
        case Records::kUpdateCommitted:
            run.set[kUpdate] = "1";
            break;
        }
        for (const std::string &v : variables)
            if (!run.set.count(v))
                run.unset.push_back(v);
        for (const char *v : {kRecords, kRecordsRoot, kUpdate, kTarget})
            if (!run.set.count(v))
                run.unset.emplace_back(v);
        planned.push_back(std::move(run));
    }
    return planned;
}

std::string exactPattern(const std::vector<std::string> &names) {
    std::string pattern = "^(";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i)
            pattern += '|';
        for (const char c : names[i]) {
            if (std::string_view("\\^$.|?*+()[]{}").find(c) != std::string_view::npos)
                pattern += '\\';
            pattern += c;
        }
    }
    return pattern + ")$";
}

std::optional<CtestProgress> readCtestLine(std::string_view line) {
    // ctest's own forms (CTest's progress output): a test started, and
    // "<n>/<total> Test #<n>: <name> ....<state> <seconds> sec" as it ends,
    // the state after "***" unless it passed.
    static const std::regex kStart(R"(^\s*Start\s+\d+:\s+(\S+)\s*$)");
    static const std::regex kEnd(R"(^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.*\s*(\*\*\*)?(.*?)\s+([0-9.]+)\s+sec\s*$)");
    const std::string s(line);
    std::smatch m;
    if (std::regex_match(s, m, kStart))
        return CtestProgress{m[1], TestState::kRunning, 0, {}};
    if (!std::regex_match(s, m, kEnd))
        return std::nullopt;
    CtestProgress p{m[1], TestState::kFailed, std::stod(m[4]), m[3]};
    if (p.why == "Passed")
        p.state = TestState::kPassed;
    // "Not Run" alone is a test whose program couldn't be started: a failure.
    else if (p.why == "Skipped" || p.why == "Not Run (Disabled)")
        p.state = TestState::kSkipped;
    if (p.state != TestState::kFailed)
        p.why.clear();
    return p;
}

} // namespace reluva::devtools
