// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// What reluva-viewer's Tests window runs, worked out apart from its window
// and from running ctest, so reluva-tests can check it.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::devtools {

/** One ctest test, as the build's reluva-tests.txt lists it (cmake/ReluvaEngines.cmake). */
struct TestEntry {
    std::string name;                ///< "awe.unit.formats", "awe.gamedata.route"
    std::vector<std::string> labels; ///< ctest's: "awe", "unit"; "gamedata", "awe"
    // A game-files step's: what the launcher stores for the game, and the
    // variable naming its folder. Empty for every other test.
    std::string engine;
    std::string gameId;
    std::string variable;

    /** Whether it needs a game's own files. */
    bool needsGame() const { return !variable.empty(); }
    /** "awe" for "awe.unit.formats": the engine, "common" or "tests". */
    std::string area() const { return name.substr(0, name.find('.')); }
};

/** The tests in reluva-tests.txt's text; lines that don't read are left out. */
std::vector<TestEntry> readTestList(std::string_view text);

/** A game in the launcher's list, as a game-files step needs it. */
struct TestGame {
    std::string target; ///< its entry's name in reluva.ini
    std::string engine;
    std::string gameId;
    std::string path; ///< its folder
};

/** Which records the game-files steps compare with (cmake/ReluvaGamedata.cmake). */
enum class Records {
    kCommitted,      ///< the repository's, as CI and everyone else compare
    kCompareMine,    ///< a baseline of one's own, in a folder per engine
    kMakeMine,       ///< this run's, written as that baseline
    kUpdateCommitted ///< this run's, written over the repository's
};

/** One ctest run: the tests, and the environment it needs besides the program's own. */
struct CtestRun {
    std::vector<std::string> tests;
    std::map<std::string, std::string> set; ///< variables to set
    std::vector<std::string> unset;         ///< variables to clear, so the program's own can't steer the run
    std::map<std::string, std::string> on;  ///< a game-files step's game: the launcher's entry, by test name
};

/**
 * The runs that take `tests` (by name), each game-files step on each of
 * `games` it's for.
 *
 * One run holds every test it can: a game's folder is given to its steps
 * through one variable, so a second entry for the same game (another
 * release) needs a run of its own.
 * @param recordsFolder The baseline's folder, for Records::kCompareMine and kMakeMine.
 */
std::vector<CtestRun> planRuns(const std::vector<TestEntry> &all, const std::vector<std::string> &tests,
                               const std::vector<TestGame> &games, Records records, const std::string &recordsFolder);

/** ctest's -R pattern matching exactly these names. */
std::string exactPattern(const std::vector<std::string> &names);

/** How a test ended, or that it started. */
enum class TestState { kNotRun, kRunning, kPassed, kFailed, kSkipped };

/** What a line of ctest's progress says about a test. */
struct CtestProgress {
    std::string name;
    TestState state = TestState::kNotRun;
    double seconds = 0; ///< for a test that ended
    std::string why;    ///< for a failure: "Failed", "Timeout", "Exception: ..."
};

/**
 * A line of ctest's output, if it's one saying a test started
 * ("    Start  75: awe.gamedata.release") or how it ended
 * ("1/1 Test #75: awe.gamedata.release ....***Failed    0.38 sec").
 */
std::optional<CtestProgress> readCtestLine(std::string_view line);

} // namespace reluva::devtools
