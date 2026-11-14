// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "devtools/test_runs.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reluva::devtools {

/** How one test of a run went, as ctest told. */
struct TestResult {
    std::string name;
    std::string on; ///< a game-files step's launcher entry ("" for the rest)
    TestState state = TestState::kNotRun;
    double seconds = 0;
    std::string why;                 ///< a failure's: "Failed", "Timeout"...
    std::vector<std::string> output; ///< what it printed, for a failure (ctest --output-on-failure)
};

/**
 * Runs ctest in a build folder, on a thread of its own, for the Tests
 * window: the window asks for what's happened so far each frame.
 *
 * The thread owns the ctest process, so a run goes on, and its output keeps
 * being read, while the window isn't drawn (a game or a viewer open).
 */
class CtestRunner {
public:
    /**
     * @param ctest    ctest's program.
     * @param buildDir The build folder its tests are in.
     * @param config   The configuration, for a build of several (Visual Studio's); "" for one.
     */
    CtestRunner(std::filesystem::path ctest, std::filesystem::path buildDir, std::string config);
    ~CtestRunner();
    CtestRunner(const CtestRunner &) = delete;
    CtestRunner &operator=(const CtestRunner &) = delete;

    /** Starts `runs`, one after the other, `jobs` tests at once; false if one is going already. */
    bool start(std::vector<CtestRun> runs, int jobs);
    /** Stops the run: ctest is asked to stop, then made to if it hasn't within a few seconds. */
    void cancel() { _cancel = true; }
    bool running() const { return _running; }
    /** Whether the last run was stopped before it ended. */
    bool stopped() const { return !_running && _cancel; }

    /** What's happened so far: each test's result, in the order they were asked for. */
    std::vector<TestResult> results() const;
    /** Every line ctest has printed, and the program's own about each run. */
    std::vector<std::string> log() const;
    /** How long the last run took, or this one so far, in milliseconds. */
    uint64_t elapsedMs() const;

private:
    void work(std::vector<CtestRun> runs, int jobs);
    // One ctest process; false if it couldn't be started.
    bool runOne(const CtestRun &run, size_t index, int jobs);
    void addLine(size_t run, const std::string &line);
    void note(const std::string &line);

    std::filesystem::path _ctest;
    std::filesystem::path _buildDir;
    std::string _config;
    std::thread _thread;
    std::atomic<bool> _running{false};
    std::atomic<bool> _cancel{false};

    // Shared with the thread.
    mutable std::mutex _mutex;
    std::vector<TestResult> _results;
    std::vector<std::string> _log;
    std::vector<std::map<std::string, size_t>> _byRun; // each run's results, by the test's name
    size_t _failedLast = SIZE_MAX;                     // the result whose output ctest is printing
    uint64_t _startedMs = 0;
    uint64_t _endedMs = 0;
};

} // namespace reluva::devtools
