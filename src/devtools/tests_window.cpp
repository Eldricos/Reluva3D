// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "devtools/tests_window.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace reluva::devtools {

namespace {

// Given by src/devtools/CMakeLists.txt: the build reluva-viewer was made in.
constexpr const char *kBuildDir = RELUVA_BUILD_DIR;
constexpr const char *kCtest = RELUVA_CTEST;
constexpr const char *kBuildConfig = RELUVA_BUILD_CONFIG;

bool hasLabel(const TestEntry &t, const char *label) {
    return std::find(t.labels.begin(), t.labels.end(), label) != t.labels.end();
}

ImVec4 stateColour(TestState s) {
    switch (s) {
    case TestState::kPassed:
        return {0.45f, 0.85f, 0.45f, 1};
    case TestState::kFailed:
        return {1.0f, 0.45f, 0.40f, 1};
    case TestState::kSkipped:
        return {0.85f, 0.75f, 0.35f, 1};
    case TestState::kRunning:
        return {0.50f, 0.70f, 1.0f, 1};
    case TestState::kNotRun:
        break;
    }
    return {0.6f, 0.6f, 0.6f, 1};
}

const char *stateText(TestState s) {
    switch (s) {
    case TestState::kPassed:
        return "passed";
    case TestState::kFailed:
        return "FAILED";
    case TestState::kSkipped:
        return "skipped";
    case TestState::kRunning:
        return "running";
    case TestState::kNotRun:
        break;
    }
    return "-";
}

// A checkbox over a set: ticked when `name` is in it.
bool pickBox(const char *label, std::set<std::string> &set, const std::string &name) {
    bool on = set.count(name) != 0;
    if (!ImGui::Checkbox(label, &on))
        return false;
    if (on)
        set.insert(name);
    else
        set.erase(name);
    return true;
}

} // namespace

TestsWindow::TestsWindow(const Config &config, const std::filesystem::path &dataDir)
    : _config(config), _buildDir(kBuildDir), _recordsFolder((dataDir / "gamedata-records").string()) {
    _jobs = std::max(1, SDL_GetNumLogicalCPUCores() / 2);

    // A copy run from anywhere but its own build folder has no tests to run
    // (and that folder may hold another build by now).
    const char *base = SDL_GetBasePath();
    std::error_code ec;
    const std::filesystem::path here = base ? std::filesystem::weakly_canonical(base, ec) : std::filesystem::path();
    const std::filesystem::path build = std::filesystem::weakly_canonical(_buildDir, ec);
    const std::string rel = here.lexically_relative(build).generic_string();
    std::ifstream list(_buildDir / "reluva-tests.txt", std::ios::binary);
    if (here.empty() || rel.empty() || rel.rfind("..", 0) == 0) {
        _why = "reluva-viewer is running from outside the build folder it was built in (" + _buildDir.string() +
               "), so its tests aren't here to run.";
    } else if (!list) {
        _why = "This build has no tests (configured with RELUVA_BUILD_TESTS off?): there's no " +
               (_buildDir / "reluva-tests.txt").string() + ".";
    } else {
        std::stringstream text;
        text << list.rdbuf();
        _tests = readTestList(text.str());
        _runner = std::make_unique<CtestRunner>(kCtest, _buildDir, kBuildConfig);
    }
}

std::vector<TestGame> TestsWindow::games() const {
    std::vector<TestGame> found;
    for (const std::string &name : _config.targets()) {
        TestGame g{name, _config.get(name, "engine"), _config.get(name, "gameid"), _config.get(name, "path")};
        if (!stepsFor(g).empty())
            found.push_back(g);
    }
    return found;
}

std::vector<const TestEntry *> TestsWindow::stepsFor(const TestGame &g) const {
    std::vector<const TestEntry *> steps;
    for (const TestEntry &t : _tests)
        if (t.needsGame() && t.engine == g.engine && t.gameId == g.gameId)
            steps.push_back(&t);
    return steps;
}

void TestsWindow::drawButtons(const std::string &selected) {
    const ImVec2 size(-1, 0);
    const bool running = _runner && _runner->running();
    if (ImGui::Button(running ? "Tests... (running)" : "Tests...", size))
        _open = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Run the build's tests, the game-files steps on the games in this list");

    // The selected game's steps, at once.
    const TestGame g{selected, _config.get(selected, "engine"), _config.get(selected, "gameid"),
                     _config.get(selected, "path")};
    const std::vector<const TestEntry *> steps =
        selected.empty() || !_runner ? std::vector<const TestEntry *>{} : stepsFor(g);
    ImGui::BeginDisabled(steps.empty() || running);
    if (ImGui::Button("Test Game", size)) {
        std::vector<std::string> names;
        for (const TestEntry *t : steps)
            names.push_back(t->name);
        run(names, {g});
        _open = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(steps.empty() ? "The selected game's engine has no game-files steps"
                                        : "Run every game-files step on the selected game");
}

void TestsWindow::drawWindows() {
    if (!_open)
        return;
    const float scale = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowSize(ImVec2(980 * scale, 620 * scale), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Tests", &_open)) {
        ImGui::End();
        return;
    }
    if (!_runner) {
        ImGui::TextWrapped("%s", _why.c_str());
        ImGui::End();
        return;
    }
    ImGui::BeginChild("choices", ImVec2(380 * scale, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    drawChoices();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("results", ImVec2(0, 0), ImGuiChildFlags_Borders);
    drawResults();
    ImGui::EndChild();

    if (_confirmUpdate) {
        ImGui::OpenPopup("Update the committed records?");
        _confirmUpdate = false;
    }
    if (ImGui::BeginPopupModal("Update the committed records?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This run's records will be written over the repository's, in\n"
                               "tests/engines/<engine>/data/gamedata/, for each step that runs.\n\n"
                               "Every developer and CI compare with those: check the change\n"
                               "with git diff before committing it.");
        if (ImGui::Button("Run and write them")) {
            if (_confirmingAll)
                runAll();
            else
                runPicked();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

void TestsWindow::drawChoices() {
    const bool running = _runner->running();
    ImGui::BeginDisabled(running);

    ImGui::SeparatorText("Games");
    const std::vector<TestGame> list = games();
    if (list.empty())
        ImGui::TextDisabled("No game in the launcher's list\nhas game-files steps.");
    for (const TestGame &g : list) {
        const std::string label = _config.get(g.target, "description", g.target) + "##" + g.target;
        pickBox(label.c_str(), _pickedGames, g.target);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s", g.target.c_str(), g.path.c_str());
    }

    // By engine, then by area: the steps (run on the games ticked), then
    // the tests that need no game files.
    ImGui::SeparatorText("Game-files steps");
    std::map<std::string, std::vector<const TestEntry *>> steps;
    std::map<std::string, std::vector<const TestEntry *>> others;
    for (const TestEntry &t : _tests)
        (t.needsGame() ? steps : others)[t.needsGame() ? t.engine : t.area()].push_back(&t);
    const auto group = [&](const std::string &title, const std::vector<const TestEntry *> &tests) {
        size_t picked = 0;
        for (const TestEntry *t : tests)
            picked += _picked.count(t->name);
        const std::string label = title + " (" + std::to_string(picked) + "/" + std::to_string(tests.size()) + ")";
        bool all = picked == tests.size();
        ImGui::PushID(title.c_str());
        if (ImGui::Checkbox("##all", &all)) {
            for (const TestEntry *t : tests) {
                if (all)
                    _picked.insert(t->name);
                else
                    _picked.erase(t->name);
            }
        }
        ImGui::SameLine();
        // Its ID is the group's alone, not its count, so it stays open as
        // tests are ticked.
        if (ImGui::TreeNode("tests", "%s", label.c_str())) {
            for (const TestEntry *t : tests) {
                pickBox(t->name.c_str(), _picked, t->name);
                if (hasLabel(*t, "gamedata") && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Run on the games ticked above (%s)", t->variable.c_str());
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    // An engine names a group in both sections ("awe"): each section is an
    // ID scope of its own.
    ImGui::PushID("steps");
    for (const auto &[engine, tests] : steps)
        group(engine, tests);
    ImGui::PopID();
    ImGui::SeparatorText("Tests without game files");
    ImGui::PushID("others");
    for (const auto &[area, tests] : others)
        group(area, tests);
    ImGui::PopID();

    ImGui::SeparatorText("Records");
    const char *kinds[] = {"The committed ones", "Compare with my baseline", "Make my baseline",
                           "Update the committed ones"};
    int kind = int(_records);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##records", &kind, kinds, 4))
        _records = Records(kind);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("What the game-files steps compare their records with, or write them as.\n"
                          "A baseline of your own is kept a folder per engine, for comparing\n"
                          "your changes on your machine (the committed records were made on Linux).");
    if (_records == Records::kCompareMine || _records == Records::kMakeMine) {
        char folder[1024];
        std::snprintf(folder, sizeof folder, "%s", _recordsFolder.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##folder", folder, sizeof folder))
            _recordsFolder = folder;
    }
    ImGui::SetNextItemWidth(120 * ImGui::GetStyle().FontScaleDpi);
    ImGui::SliderInt("tests at once", &_jobs, 1, std::max(1, SDL_GetNumLogicalCPUCores()));

    ImGui::Spacing();
    // Writing over the committed records is asked about first.
    const bool ask = _records == Records::kUpdateCommitted;
    if (ImGui::Button("Run Selected")) {
        _confirmingAll = false;
        if (ask)
            _confirmUpdate = true;
        else
            runPicked();
    }
    ImGui::SameLine();
    if (ImGui::Button("Run All")) {
        _confirmingAll = true;
        if (ask)
            _confirmUpdate = true;
        else
            runAll();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Every test, and every game-files step on every game in the list");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        _picked.clear();
        _pickedGames.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Stop"))
        _runner->cancel();
    ImGui::EndDisabled();
}

void TestsWindow::runPicked() {
    std::vector<TestGame> picked;
    for (const TestGame &g : games())
        if (_pickedGames.count(g.target))
            picked.push_back(g);
    // A step runs on the games ticked; with none of its game ticked, not at all.
    std::vector<std::string> names;
    for (const TestEntry &t : _tests) {
        if (!_picked.count(t.name))
            continue;
        const bool onAGame = std::any_of(picked.begin(), picked.end(), [&](const TestGame &g) {
            return g.engine == t.engine && g.gameId == t.gameId;
        });
        if (!t.needsGame() || onAGame)
            names.push_back(t.name);
    }
    run(names, picked);
}

void TestsWindow::runAll() {
    std::vector<std::string> names;
    for (const TestEntry &t : _tests)
        names.push_back(t.name);
    std::vector<TestGame> all = games();
    // A step with no game would find one in the launcher's list itself; here
    // every game in the list is given, so none is left to it.
    std::vector<std::string> kept;
    for (const std::string &name : names) {
        const auto t = std::find_if(_tests.begin(), _tests.end(), [&](const TestEntry &e) { return e.name == name; });
        const bool onAGame = std::any_of(
            all.begin(), all.end(), [&](const TestGame &g) { return g.engine == t->engine && g.gameId == t->gameId; });
        if (!t->needsGame() || onAGame)
            kept.push_back(name);
    }
    run(kept, all);
}

void TestsWindow::run(const std::vector<std::string> &tests, const std::vector<TestGame> &games) {
    if (!_runner || tests.empty())
        return;
    _shown = SIZE_MAX;
    _runner->start(planRuns(_tests, tests, games, _records, _recordsFolder), _jobs);
}

void TestsWindow::drawResults() {
    const std::vector<TestResult> results = _runner->results();
    size_t counts[5] = {};
    for (const TestResult &r : results)
        counts[size_t(r.state)]++;
    const uint64_t ms = _runner->elapsedMs();
    if (results.empty()) {
        ImGui::TextDisabled("Pick tests and games on the left, then Run Selected; or Run All.");
    } else {
        const bool running = _runner->running();
        ImGui::Text(
            "%s  %zu passed, %zu failed, %zu skipped, %zu %s  (%llu:%02llu)",
            running              ? "Running."
            : _runner->stopped() ? "Stopped."
                                 : "Done.",
            counts[size_t(TestState::kPassed)], counts[size_t(TestState::kFailed)], counts[size_t(TestState::kSkipped)],
            counts[size_t(TestState::kNotRun)] + counts[size_t(TestState::kRunning)], running ? "to run" : "not run",
            (unsigned long long)(ms / 60000), (unsigned long long)(ms / 1000 % 60));
    }

    const float outputHeight = _shown < results.size() || _showLog ? ImGui::GetContentRegionAvail().y * 0.45f : 0;
    if (ImGui::BeginTable("results", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0, -outputHeight - ImGui::GetFrameHeightWithSpacing()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_WidthStretch);
        const float scale = ImGui::GetStyle().FontScaleDpi;
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 110 * scale);
        ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 80 * scale);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70 * scale);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < results.size(); ++i) {
            const TestResult &r = results[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(int(i));
            if (ImGui::Selectable(r.name.c_str(), _shown == i, ImGuiSelectableFlags_SpanAllColumns))
                _shown = _shown == i ? SIZE_MAX : i;
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.on.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(stateColour(r.state), "%s",
                               r.state == TestState::kFailed && r.why != "Failed" ? r.why.c_str() : stateText(r.state));
            ImGui::TableNextColumn();
            if (r.state != TestState::kNotRun && r.state != TestState::kRunning)
                ImGui::Text("%.2f s", r.seconds);
        }
        ImGui::EndTable();
    }

    ImGui::Checkbox("ctest's output", &_showLog);
    if (_shown < results.size()) {
        const TestResult &r = results[_shown];
        // A game-files step keeps each command's whole output, and the
        // previous run's beside it, in the build (cmake/ReluvaGamedata.cmake).
        const TestEntry *entry = nullptr;
        for (const TestEntry &t : _tests)
            if (t.name == r.name)
                entry = &t;
        if (entry && entry->needsGame()) {
            ImGui::SameLine();
            if (ImGui::Button("Open its output folder")) {
                const std::string dir = (_buildDir / "gamedata" / entry->engine).generic_string();
                // file:///C:/... on Windows, file:///home/... elsewhere.
                SDL_OpenURL(((dir.rfind('/', 0) == 0 ? "file://" : "file:///") + dir).c_str());
            }
        }
        ImGui::BeginChild("output", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        if (r.output.empty())
            ImGui::TextDisabled(r.state == TestState::kFailed ? "It printed nothing."
                                                              : "Only a failed test's output is kept.");
        for (const std::string &line : r.output)
            ImGui::TextUnformatted(line.c_str());
        ImGui::EndChild();
    } else if (_showLog) {
        const std::vector<std::string> log = _runner->log();
        ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        ImGuiListClipper clipper;
        clipper.Begin(int(log.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                ImGui::TextUnformatted(log[size_t(i)].c_str());
        if (_runner->running() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
}

} // namespace reluva::devtools
