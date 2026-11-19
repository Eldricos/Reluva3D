// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/config.h"
#include "devtools/ctest_runner.h"
#include "devtools/test_runs.h"
#include "gui/launcher.h"

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace reluva::devtools {

/**
 * reluva-viewer's Tests window: the build's tests, run through ctest, the
 * game-files steps on the games in the launcher's list.
 *
 * It's there only when reluva-viewer runs from the build folder it was
 * built in, the one holding the tests: elsewhere there are none to run.
 */
class TestsWindow : public LauncherExtension {
public:
    /**
     * @param config  The launcher's, for its games.
     * @param dataDir Where the launcher keeps its files: the baselines go there by default.
     */
    TestsWindow(const Config &config, const std::filesystem::path &dataDir);

    void drawButtons(const std::string &selected) override;
    void drawWindows() override;

private:
    // The launcher's games that have game-files steps.
    std::vector<TestGame> games() const;
    // The steps for `g`'s game.
    std::vector<const TestEntry *> stepsFor(const TestGame &g) const;
    void drawChoices();
    void drawResults();
    void runPicked();
    void runAll();
    void run(const std::vector<std::string> &tests, const std::vector<TestGame> &games);

    const Config &_config;
    std::filesystem::path _buildDir;
    std::string _why; ///< why there are no tests, if there aren't
    std::vector<TestEntry> _tests;
    std::unique_ptr<CtestRunner> _runner;

    bool _open = false;
    std::set<std::string> _picked;      ///< tests, by name
    std::set<std::string> _pickedGames; ///< the launcher's entries, by name
    Records _records = Records::kCommitted;
    std::string _recordsFolder;
    int _jobs = 1;
    bool _confirmUpdate = false; ///< the dialog before writing over the committed records
    bool _confirmingAll = false; ///< whether that's for Run All, not Run Selected
    bool _showLog = false;
    size_t _shown = SIZE_MAX; ///< the result whose output is shown
};

} // namespace reluva::devtools
