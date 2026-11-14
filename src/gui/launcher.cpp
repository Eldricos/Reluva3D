// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "gui/launcher.h"

#include "backends/platform/sdl3/sdl_system.h"
#include "common/settings.h"
#include "engines/registry.h"

#include <imgui.h>
#include <algorithm>
#include <utility>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

namespace reluva {

namespace {

// The settings every engine has (common/settings.h), as the launcher shows
// them. Each one's first choice is Reluva's default.
const std::vector<GameOption> &sharedSettings() {
    static const std::vector<GameOption> kList = [] {
        std::vector<GameOption> v;
        v.push_back({settings::kFullscreen,
                     "Start in",
                     "Alt+Enter switches between the two in a game.",
                     true,
                     {{"0", "A window"}, {"1", "Full screen"}}});
        v.push_back({settings::kWindowScale,
                     "Window size",
                     "The window's size for a game, as a multiple of the game's own picture.",
                     true,
                     {{"", "As it is"},
                      {"auto", "2x where it fits, else 1x"},
                      {"1", "1x"},
                      {"2", "2x"},
                      {"3", "3x"},
                      {"4", "4x"}}});
        v.push_back({settings::kIntegerScaling,
                     "Scaling",
                     "Whole multiples keep every pixel the same size, with black borders.\n"
                     "They only show where the picture is scaled by an uneven amount (most full screens).",
                     true,
                     {{"0", "Any size, letterboxed"}, {"1", "Whole multiples only"}}});
        v.push_back({settings::kSmooth,
                     "Pixels",
                     "Smoothing only shows where the picture is scaled by an uneven amount.",
                     true,
                     {{"0", "Sharp"}, {"1", "Smoothed"}}});
        v.push_back({settings::kVsync,
                     "Vertical sync",
                     "Wait for the display's refresh: no tearing.",
                     true,
                     {{"1", "On"}, {"0", "Off"}}});
        GameOption volume{
            settings::kMasterVolume, "Master volume", "Every sound, as a share of how the game plays it.", true, {}};
        volume.choices.push_back({"100", "100%"});
        for (int p = 0; p <= 200; p += 10)
            if (p != 100)
                volume.choices.push_back({std::to_string(p), std::to_string(p) + "%"});
        v.push_back(volume);
        return v;
    }();
    return kList;
}

// A checkbox option as a list, for showing it beside its inherited value.
GameOption asChoices(const GameOption &o) {
    if (!o.choices.empty())
        return o;
    GameOption c = o;
    c.choices = o.defaultOn ? std::vector<std::pair<std::string, std::string>>{{"1", "On"}, {"0", "Off"}}
                            : std::vector<std::pair<std::string, std::string>>{{"0", "Off"}, {"1", "On"}};
    return c;
}

std::string shownValue(const GameOption &o, const std::string &value) {
    for (const auto &[v, text] : o.choices)
        if (v == value)
            return text;
    return value;
}

} // namespace

Launcher::Launcher(SdlSystem &system, Config &config, LauncherPurpose purpose)
    : _sys(system), _config(config), _purpose(purpose) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr; // our layout is fixed; don't litter imgui.ini

    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0)
        scale = 1.0f;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;

    ImGui_ImplSDL3_InitForSDLRenderer(_sys.window(), _sys.renderer());
    ImGui_ImplSDLRenderer3_Init(_sys.renderer());

    _selected = _config.get(Config::kGlobalSection, "last_target");
}

Launcher::~Launcher() {
    // An import still copying stops first.
    _importCancel = true;
    if (_importThread.joinable())
        _importThread.join();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

std::optional<GameTarget> Launcher::run() {
    _sys.resetForLauncher();
    SDL_Renderer *renderer = _sys.renderer();

    std::optional<GameTarget> launch;
    bool quit = false;
    while (!launch && !quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT)
                quit = true;
        }
        if (SDL_GetWindowFlags(_sys.window()) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(16);
            continue;
        }

        handlePickedFolder();
        finishImport();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        drawUi(launch, quit);
        ImGui::Render();

        ImGuiIO &io = ImGui::GetIO();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 20, 22, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    if (launch) {
        _config.set(Config::kGlobalSection, "last_target", launch->target);
        _config.save();
    }
    return launch;
}

void Launcher::drawUi(std::optional<GameTarget> &launch, bool &quit) {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Reluva", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    const bool viewing = _purpose == LauncherPurpose::kView;
    ImGui::TextUnformatted(viewing ? "Reluva data viewer" : "Reluva");
    ImGui::SameLine();
    ImGui::TextDisabled(viewing ? "v0.1 - what the games' files hold, for working on the engines"
                                : "v0.1 - classic game engine reimplementations");
    ImGui::Separator();

    const float buttonsWidth = 200.0f * ImGui::GetStyle().FontScaleDpi;
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    // Game list
    ImGui::BeginChild("games", ImVec2(-buttonsWidth, -footer), ImGuiChildFlags_Borders);
    const auto targets = _config.targets();
    if (targets.empty())
        ImGui::TextDisabled("No games yet. Use \"Add Game...\" and pick a game's install folder.");
    for (const std::string &name : targets) {
        std::string label = listLabel(name) + "##" + name;
        if (ImGui::Selectable(label.c_str(), _selected == name, ImGuiSelectableFlags_AllowDoubleClick)) {
            _selected = name;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !_importRunning)
                launch = launchTarget(name);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _config.get(name, "path").c_str());
    }
    ImGui::EndChild();

    // Buttons
    ImGui::SameLine();
    ImGui::BeginGroup();
    const ImVec2 bsize(-1, 0);
    const bool haveSelection = _config.hasSection(_selected) && _selected != Config::kGlobalSection;

    ImGui::BeginDisabled(!haveSelection || _importRunning);
    if (ImGui::Button(viewing ? "View" : "Start", bsize))
        launch = launchTarget(_selected);
    ImGui::EndDisabled();

    ImGui::BeginDisabled(_pickInProgress || _importRunning || _importPlan);
    if (ImGui::Button("Add Game...", bsize)) {
        _pickInProgress = true;
        _pickFor = Pick::kAddGame;
        SDL_ShowOpenFolderDialog(pickFolderCallback, this, _sys.window(), nullptr, false);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play a game from the folder it's in");
    if (ImGui::Button("Import Game...", bsize)) {
        _pickInProgress = true;
        _pickFor = Pick::kImportSource;
        SDL_ShowOpenFolderDialog(pickFolderCallback, this, _sys.window(), nullptr, false);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy a game's files from its CD or an install into a folder of Reluva's,\n"
                          "only those Reluva needs; nothing is installed");
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!haveSelection || _importRunning || _importPlan || _pickInProgress);
    if (ImGui::Button("Apply Patch...", bsize))
        pickPatch(Pick::kPatchForGame);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Update the selected game with its patch's installer\n(e.g. eliteforcepatch1_2.exe), on any system");
    if (ImGui::Button("Add Expansion...", bsize))
        pickExpansion(Pick::kPatchForGame);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add an expansion to the selected game from its CD\n(or a folder it was installed to)");
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!haveSelection || _importRunning);
    if (ImGui::Button("Remove", bsize)) {
        _config.removeSection(_selected);
        _config.save();
        _status = "Removed " + _selected;
        _selected.clear();
    }
    ImGui::EndDisabled();

    drawOptions();

    ImGui::Spacing();
    if (ImGui::Button("Settings...", bsize))
        _settingsOpen = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Display and sound settings for every game, and each engine's settings for its games");
    // The testbed checks the backend, with no game: for developers only.
    if (viewing && ImGui::Button("Run Testbed", bsize))
        launch = GameTarget{"testbed", "testbed", "testbed", "Testbed", {}, {}, _config.settingsFor("testbed")};

    ImGui::Spacing();
    if (ImGui::Button("Quit", bsize))
        quit = true;
    ImGui::EndGroup();

    // Status bar
    ImGui::Separator();
    ImGui::TextUnformatted(_status.empty() ? "Ready." : _status.c_str());

    drawImport();
    drawSettingsWindow();

    // Several games matched one folder: let the user pick.
    if (!_pendingChoices.empty()) {
        ImGui::OpenPopup("Choose game");
        if (ImGui::BeginPopupModal("Choose game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Several games match this folder:");
            for (const DetectedGame &g : _pendingChoices) {
                std::string label = g.description + " (" + g.language + ", " + g.platform + ")";
                if (ImGui::Selectable(label.c_str())) {
                    addGame(g, _pendingPath);
                    _pendingChoices.clear();
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
            if (ImGui::Button("Cancel")) {
                _pendingChoices.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

void SDLCALL Launcher::pickFolderCallback(void *userdata, const char *const *files, int) {
    auto *self = static_cast<Launcher *>(userdata);
    {
        std::lock_guard lock(self->_pickMutex);
        if (!files)
            self->_pickError = std::string("Folder dialog failed: ") + SDL_GetError();
        else if (files[0])
            self->_pickedFolder = files[0];
    }
    self->_pickInProgress = false;
}

void Launcher::handlePickedFolder() {
    std::optional<std::string> folder;
    std::optional<std::string> error;
    {
        std::lock_guard lock(_pickMutex);
        folder.swap(_pickedFolder);
        error.swap(_pickError);
    }
    if (error)
        _status = *error;
    if (!folder)
        return;

    if (_pickFor == Pick::kPatchForImport || _pickFor == Pick::kPatchForGame) {
        // A patch for the import shown, or for the selected game (a plan
        // of the patch's files alone, into its folder).
        if (_pickFor == Pick::kPatchForGame) {
            const std::string path = _config.get(_selected, "path");
            const std::string engine = _config.get(_selected, "engine");
            const std::string gameId = _config.get(_selected, "gameid");
            ImportPlan plan;
            for (const DetectedGame &g : detectGames(path))
                if (g.engineId == engine && g.gameId == gameId)
                    plan.game = g;
            if (plan.game.gameId.empty()) {
                _status = "The game isn't found in " + path + " any more";
                return;
            }
            _importPlan = std::move(plan);
            _importSource = *folder;
            _importDest = path;
            _patchTarget = _selected;
        }
        std::string message;
        const MetaEngine *meta = _importPlan ? findEngine(_importPlan->game.engineId) : nullptr;
        if (!meta || !meta->addToImport(*_importPlan, *folder, message)) {
            _status = std::filesystem::path(*folder).filename().string() + ": " + message;
            if (_pickFor == Pick::kPatchForGame) {
                _importPlan.reset();
                _patchTarget.clear();
            }
        }
        return;
    }
    if (_pickFor == Pick::kImportDestination) {
        // A folder of the game's own inside the one chosen.
        if (_importPlan)
            _importDest = std::filesystem::path(*folder) / _importDest.filename();
        return;
    }
    if (_pickFor == Pick::kImportSource) {
        auto plans = planImports(*folder);
        if (plans.empty())
            _status = "Nothing Reluva can import found in " + *folder;
        else
            offerImport(std::move(plans), *folder);
        return;
    }
    auto found = detectGames(*folder);
    if (found.empty()) {
        // Perhaps the game's CD, or an install that isn't laid out to run
        // from: offered as an import instead.
        auto plans = planImports(*folder);
        if (!plans.empty())
            offerImport(std::move(plans), *folder);
        else
            _status = "No supported game found in " + *folder;
    } else if (found.size() == 1) {
        addGame(found.front(), *folder);
    } else {
        _pendingPath = *folder;
        _pendingChoices = std::move(found);
    }
}

void Launcher::addGame(const DetectedGame &game, const std::string &path) {
    std::string name = game.gameId;
    for (int n = 2; _config.hasSection(name); ++n)
        name = game.gameId + "-" + std::to_string(n);

    _config.set(name, "engine", game.engineId);
    _config.set(name, "gameid", game.gameId);
    _config.set(name, "description", game.description);
    _config.set(name, "language", game.language);
    _config.set(name, "platform", game.platform);
    _config.set(name, "path", path);
    if (Error err = _config.save())
        _status = err.message;
    else
        _status = "Added " + game.description;
    _selected = name;
}

std::string Launcher::listLabel(const std::string &name) {
    // The description, and the options turned from their defaults (two
    // entries for one folder, one without the Expansion Pack, told apart).
    auto it = _listOptions.find(name);
    if (it == _listOptions.end()) {
        const MetaEngine *meta = findEngine(_config.get(name, "engine"));
        it = _listOptions
                 .emplace(name, meta ? meta->options(_config.get(name, "gameid"), _config.get(name, "path"))
                                     : std::vector<GameOption>{})
                 .first;
    }
    std::string label = _config.get(name, "description", name);
    for (const GameOption &o : it->second) {
        if (!o.choices.empty()) {
            const std::string value = _config.get(name, o.key, o.choices.front().first);
            for (const auto &[v, shown] : o.choices)
                if (v == value && v != o.choices.front().first)
                    label += " [" + o.label + ": " + shown + "]";
            continue;
        }
        const bool on = _config.get(name, o.key, o.defaultOn ? "1" : "0") != "0";
        if (on != o.defaultOn)
            label += " [" + o.label + ": " + (on ? "on" : "off") + "]";
    }
    return label;
}

void Launcher::drawOptions() {
    if (_optionsFor != _selected) {
        _optionsFor = _selected;
        _options.clear();
        if (_config.hasSection(_selected) && _selected != Config::kGlobalSection)
            if (const MetaEngine *meta = findEngine(_config.get(_selected, "engine")))
                _options = meta->options(_config.get(_selected, "gameid"), _config.get(_selected, "path"));
    }
    const bool haveGame =
        _config.hasSection(_selected) && _selected != Config::kGlobalSection && !Config::isEngineSection(_selected);
    if (!haveGame)
        return;
    ImGui::Spacing();
    ImGui::Separator();
    for (const GameOption &o : _options) {
        if (!o.choices.empty()) {
            // A list: the value kept, or the first (the default).
            const std::string value = _config.get(_selected, o.key, o.choices.front().first);
            std::string shown = o.choices.front().second;
            for (const auto &[v, text] : o.choices)
                if (v == value)
                    shown = text;
            ImGui::TextUnformatted(o.label.c_str());
            if (!o.tooltip.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", o.tooltip.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(("##" + o.key).c_str(), shown.c_str())) {
                for (const auto &[v, text] : o.choices)
                    if (ImGui::Selectable(text.c_str(), v == value)) {
                        _config.set(_selected, o.key, v);
                        _config.save();
                        _listOptions.erase(_selected);
                    }
                ImGui::EndCombo();
            }
            continue;
        }
        bool on = _config.get(_selected, o.key, o.defaultOn ? "1" : "0") != "0";
        if (ImGui::Checkbox(o.label.c_str(), &on)) {
            _config.set(_selected, o.key, on ? "1" : "0");
            _config.save();
        }
        if (!o.tooltip.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", o.tooltip.c_str());
    }
    // The game's own settings: each as set for its engine's games (or every
    // game) until changed here.
    const std::string engineId = _config.get(_selected, "engine");
    if (ImGui::CollapsingHeader("This game's settings")) {
        if (const MetaEngine *meta = findEngine(engineId))
            for (const GameOption &o : meta->engineOptions())
                drawSetting(_selected, engineId, o);
        for (const GameOption &o : sharedSettings())
            drawSetting(_selected, engineId, o);
    }
}

std::string Launcher::inheritedValue(const std::string &section, const std::string &engineId, const GameOption &option,
                                     std::string *from) const {
    const GameOption o = asChoices(option);
    const std::string engineSection = engineId.empty() ? std::string() : Config::engineSection(engineId);
    if (section != engineSection && !engineSection.empty() && _config.has(engineSection, o.key)) {
        if (from)
            *from = "engine's";
        return _config.get(engineSection, o.key);
    }
    if (section != Config::kGlobalSection && _config.has(Config::kGlobalSection, o.key)) {
        if (from)
            *from = "every game's";
        return _config.get(Config::kGlobalSection, o.key);
    }
    if (from)
        *from = "default";
    return o.choices.front().first;
}

void Launcher::drawSetting(const std::string &section, const std::string &engineId, const GameOption &option) {
    const GameOption o = asChoices(option);
    const bool set = _config.has(section, o.key);
    std::string from;
    const std::string inherited = inheritedValue(section, engineId, o, &from);
    // Unset, it's what the level above has: shown with where that is.
    const std::string inheritedText = shownValue(o, inherited) + " (" + from + ")";
    const std::string shown = set ? shownValue(o, _config.get(section, o.key)) : inheritedText;
    ImGui::TextUnformatted(o.label.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s%s", o.tooltip.empty() ? "" : (o.tooltip + "\n\n").c_str(),
                          set ? "Set here; the first choice in the list takes the level above's again."
                              : "Not set here: as the level above has it (engine's: all this engine's games; "
                                "every game's: Settings..., Every game).");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo(("##" + section + o.key).c_str(), shown.c_str())) {
        if (ImGui::Selectable(inheritedText.c_str(), !set)) {
            _config.unset(section, o.key);
            _config.save();
        }
        for (const auto &[v, text] : o.choices)
            if (ImGui::Selectable((text + "##" + v).c_str(), set && _config.get(section, o.key) == v)) {
                _config.set(section, o.key, v);
                _config.save();
            }
        ImGui::EndCombo();
    }
}

void Launcher::drawSettingsWindow() {
    if (!_settingsOpen)
        return;
    const float scale = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowSize(ImVec2(460 * scale, 560 * scale), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &_settingsOpen)) {
        ImGui::End();
        return;
    }
    // Whose settings: every game's, or one engine's games' (those it has).
    const MetaEngine *engine = _settingsEngine.empty() ? nullptr : findEngine(_settingsEngine);
    const std::string current = engine ? std::string("All ") + engine->name() + " games" : "Every game";
    ImGui::TextUnformatted("Settings for");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##settingsfor", current.c_str())) {
        if (ImGui::Selectable("Every game", !engine))
            _settingsEngine.clear();
        for (const MetaEngine *e : allEngines()) {
            if (std::string(e->id()) == "testbed")
                continue;
            if (ImGui::Selectable((std::string("All ") + e->name() + " games").c_str(), engine == e))
                _settingsEngine = e->id();
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("A game's own settings are shown with it, below its buttons.");
    ImGui::Separator();
    const std::string section = engine ? Config::engineSection(engine->id()) : Config::kGlobalSection;
    if (engine) {
        const auto own = engine->engineOptions();
        if (!own.empty()) {
            ImGui::SeparatorText(engine->name());
            for (const GameOption &o : own)
                drawSetting(section, engine->id(), o);
        }
    }
    ImGui::SeparatorText("Display and sound");
    for (const GameOption &o : sharedSettings())
        drawSetting(section, engine ? engine->id() : "", o);
    ImGui::End();
}

GameTarget Launcher::launchTarget(const std::string &name) {
    // A patch (or more of the game) may have been added to the folder since.
    const std::string engine = _config.get(name, "engine");
    const std::string gameId = _config.get(name, "gameid");
    for (const DetectedGame &g : detectGames(_config.get(name, "path")))
        if (g.engineId == engine && g.gameId == gameId && g.description != _config.get(name, "description")) {
            _config.set(name, "description", g.description);
            _config.save();
            break;
        }
    return targetFromConfig(name);
}

void Launcher::pickPatch(Pick purpose) {
    static const SDL_DialogFileFilter kFilters[] = {{"Patch installers", "exe"}, {"All files", "*"}};
    _pickInProgress = true;
    _pickFor = purpose;
    SDL_ShowOpenFileDialog(pickFolderCallback, this, _sys.window(), kFilters, 2, nullptr, false);
}

void Launcher::pickExpansion(Pick purpose) {
    _pickInProgress = true;
    _pickFor = purpose;
    SDL_ShowOpenFolderDialog(pickFolderCallback, this, _sys.window(), nullptr, false);
}

void Launcher::offerImport(std::vector<ImportPlan> plans, const std::string &source) {
    _patchTarget.clear();
    _importPlan = std::move(plans.front());
    _importSource = source;
    // A new folder in the library, named after the game.
    std::filesystem::path dest = _libraryDir / _importPlan->game.gameId;
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(dest, ec); ++n)
        dest = _libraryDir / (_importPlan->game.gameId + "-" + std::to_string(n));
    _importDest = dest;
}

void Launcher::drawImport() {
    if (!_importPlan) {
        // Done or given up: the window goes.
        if (ImGui::IsPopupOpen("Import game") && ImGui::BeginPopupModal("Import game")) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return;
    }
    ImGui::OpenPopup("Import game");
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.8f, 0));
    if (!ImGui::BeginPopupModal("Import game", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const ImportPlan &plan = *_importPlan;
    const bool patching = !_patchTarget.empty();
    ImGui::TextUnformatted(plan.game.description.c_str());
    ImGui::TextDisabled("%s %s", patching ? "adding from" : "from", _importSource.c_str());
    ImGui::Spacing();
    for (const std::string &note : plan.notes)
        ImGui::BulletText("%s", note.c_str());
    ImGui::Spacing();
    ImGui::Text("%zu files, %.0f MB, %s:", plan.files.size(), double(plan.totalBytes()) / (1024 * 1024),
                patching ? "written into the game's folder" : "copied to");
    ImGui::TextWrapped("%s", _importDest.string().c_str());
    ImGui::Spacing();
    if (_importRunning) {
        const uint64_t total = std::max<uint64_t>(_importTotal, 1);
        ImGui::ProgressBar(float(double(_importBytes) / double(total)), ImVec2(-1, 0));
        ImGui::BeginDisabled(_importCancel);
        if (ImGui::Button("Cancel"))
            _importCancel = true;
        ImGui::EndDisabled();
    } else {
        ImGui::BeginDisabled(plan.files.empty() || _pickInProgress);
        if (ImGui::Button(patching ? "Apply" : "Import"))
            startImport();
        ImGui::EndDisabled();
        ImGui::BeginDisabled(_pickInProgress);
        if (!patching) {
            if (!plan.addPatchLabel.empty()) {
                ImGui::SameLine();
                if (ImGui::Button(plan.addPatchLabel.c_str()))
                    pickPatch(Pick::kPatchForImport);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", plan.addPatchTooltip.c_str());
            }
            if (!plan.addFolderLabel.empty()) {
                ImGui::SameLine();
                if (ImGui::Button(plan.addFolderLabel.c_str()))
                    pickExpansion(Pick::kPatchForImport);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", plan.addFolderTooltip.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("Change folder...")) {
                _pickInProgress = true;
                _pickFor = Pick::kImportDestination;
                SDL_ShowOpenFolderDialog(pickFolderCallback, this, _sys.window(), nullptr, false);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            _importPlan.reset();
            _patchTarget.clear();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

void Launcher::startImport() {
    std::error_code ec;
    if (_patchTarget.empty() && std::filesystem::exists(_importDest, ec) &&
        !std::filesystem::is_empty(_importDest, ec)) {
        _status = _importDest.string() + " isn't empty: choose another folder";
        return;
    }
    _importCancel = false;
    _importDone = false;
    _importBytes = 0;
    _importTotal = _importPlan->totalBytes();
    _importRunning = true;
    _status = "Importing " + _importPlan->game.description + "...";
    _importThread = std::thread([this, plan = *_importPlan, dest = _importDest] {
        Error err = runImport(plan, dest, [this](uint64_t done, uint64_t total) {
            _importBytes = done;
            _importTotal = total;
            return !_importCancel;
        });
        {
            std::lock_guard lock(_importMutex);
            _importError = err ? err.message : std::string();
        }
        _importDone = true;
    });
}

void Launcher::finishImport() {
    if (!_importDone)
        return;
    _importThread.join();
    _importDone = false;
    _importRunning = false;
    std::string error;
    {
        std::lock_guard lock(_importMutex);
        error.swap(_importError);
    }
    const ImportPlan plan = std::move(*_importPlan);
    _importPlan.reset();
    const std::string patched = std::exchange(_patchTarget, std::string());
    if (!error.empty()) {
        _status = error +
                  (_importCancel && patched.empty() ? "; the files copied so far are in " + _importDest.string() : "");
        return;
    }
    _optionsFor.clear(); // what the folder holds may have changed
    _listOptions.clear();
    if (!patched.empty()) {
        // The game's description brought up to date ("... (1.2)").
        launchTarget(patched);
        _status = "Patched " + _config.get(patched, "description");
        return;
    }
    // Added as the folder it went to, as detected there.
    for (const DetectedGame &g : detectGames(_importDest))
        if (g.engineId == plan.game.engineId && g.gameId == plan.game.gameId) {
            addGame(g, _importDest.string());
            _status = "Imported " + g.description + " to " + _importDest.string();
            return;
        }
    _status = "Imported to " + _importDest.string() + ", but the game isn't found there";
}

GameTarget Launcher::targetFromConfig(const std::string &name) const {
    return GameTarget{name,
                      _config.get(name, "engine"),
                      _config.get(name, "gameid"),
                      _config.get(name, "description", name),
                      _config.get(name, "path"),
                      {},
                      _config.settingsFor(name)};
}

} // namespace reluva
