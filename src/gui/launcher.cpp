// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "gui/launcher.h"

#include "backends/platform/sdl3/sdl_system.h"
#include "engines/registry.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

namespace reluva {

Launcher::Launcher(SdlSystem &system, Config &config) : _sys(system), _config(config) {
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

    ImGui::TextUnformatted("Reluva");
    ImGui::SameLine();
    ImGui::TextDisabled("v0.1 - classic game engine reimplementations");
    ImGui::Separator();

    const float buttonsWidth = 200.0f * ImGui::GetStyle().FontScaleDpi;
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    // Game list
    ImGui::BeginChild("games", ImVec2(-buttonsWidth, -footer), ImGuiChildFlags_Borders);
    const auto targets = _config.targets();
    if (targets.empty())
        ImGui::TextDisabled("No games yet. Use \"Add Game...\" and pick a game's install folder.");
    for (const std::string &name : targets) {
        std::string label = _config.get(name, "description", name) + "##" + name;
        if (ImGui::Selectable(label.c_str(), _selected == name, ImGuiSelectableFlags_AllowDoubleClick)) {
            _selected = name;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                launch = targetFromConfig(name);
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

    ImGui::BeginDisabled(!haveSelection);
    if (ImGui::Button("Start", bsize))
        launch = targetFromConfig(_selected);
    ImGui::EndDisabled();

    ImGui::BeginDisabled(_pickInProgress);
    if (ImGui::Button("Add Game...", bsize)) {
        _pickInProgress = true;
        SDL_ShowOpenFolderDialog(pickFolderCallback, this, _sys.window(), nullptr, false);
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!haveSelection);
    if (ImGui::Button("Remove", bsize)) {
        _config.removeSection(_selected);
        _config.save();
        _status = "Removed " + _selected;
        _selected.clear();
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (ImGui::Button("Run Testbed", bsize))
        launch = GameTarget{"testbed", "testbed", "testbed", "Testbed", {}, {}};

    ImGui::Spacing();
    if (ImGui::Button("Quit", bsize))
        quit = true;
    ImGui::EndGroup();

    // Status bar
    ImGui::Separator();
    ImGui::TextUnformatted(_status.empty() ? "Ready." : _status.c_str());

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
    std::optional<std::string> folder, error;
    {
        std::lock_guard lock(_pickMutex);
        folder.swap(_pickedFolder);
        error.swap(_pickError);
    }
    if (error)
        _status = *error;
    if (!folder)
        return;

    auto found = detectGames(*folder);
    if (found.empty()) {
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

GameTarget Launcher::targetFromConfig(const std::string &name) const {
    return GameTarget{name,
                      _config.get(name, "engine"),
                      _config.get(name, "gameid"),
                      _config.get(name, "description", name),
                      _config.get(name, "path"),
                      {}};
}

} // namespace reluva
