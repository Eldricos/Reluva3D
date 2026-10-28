// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "launcher/launcher.h"

#include "backends/sdl3/sdl_system.h"
#include "engines/registry.h"

#include <imgui.h>
#include <algorithm>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

namespace reluva {

Launcher::Launcher(SdlSystem& system, Config& config) : sys_(system), config_(config) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr; // our layout is fixed; don't litter imgui.ini

    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0)
        scale = 1.0f;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;

    ImGui_ImplSDL3_InitForSDLRenderer(sys_.window(), sys_.renderer());
    ImGui_ImplSDLRenderer3_Init(sys_.renderer());

    selected_ = config_.get(Config::kGlobalSection, "last_target");
}

Launcher::~Launcher() {
    // An import still copying stops first.
    importCancel_ = true;
    if (importThread_.joinable())
        importThread_.join();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

std::optional<GameTarget> Launcher::run() {
    sys_.resetForLauncher();
    SDL_Renderer* renderer = sys_.renderer();

    std::optional<GameTarget> launch;
    bool quit = false;
    while (!launch && !quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT)
                quit = true;
        }
        if (SDL_GetWindowFlags(sys_.window()) & SDL_WINDOW_MINIMIZED) {
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

        ImGuiIO& io = ImGui::GetIO();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 20, 22, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    if (launch) {
        config_.set(Config::kGlobalSection, "last_target", launch->target);
        config_.save();
    }
    return launch;
}

void Launcher::drawUi(std::optional<GameTarget>& launch, bool& quit) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Reluva", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::TextUnformatted("Reluva");
    ImGui::SameLine();
    ImGui::TextDisabled("v0.1 - classic game engine reimplementations");
    ImGui::Separator();

    const float buttonsWidth = 200.0f * ImGui::GetStyle().FontScaleDpi;
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    // Game list
    ImGui::BeginChild("games", ImVec2(-buttonsWidth, -footer), ImGuiChildFlags_Borders);
    const auto targets = config_.targets();
    if (targets.empty())
        ImGui::TextDisabled("No games yet. Use \"Add Game...\" and pick a game's install folder.");
    for (const std::string& name : targets) {
        std::string label = config_.get(name, "description", name) + "##" + name;
        if (ImGui::Selectable(label.c_str(), selected_ == name, ImGuiSelectableFlags_AllowDoubleClick)) {
            selected_ = name;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !importRunning_)
                launch = launchTarget(name);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", config_.get(name, "path").c_str());
    }
    ImGui::EndChild();

    // Buttons
    ImGui::SameLine();
    ImGui::BeginGroup();
    const ImVec2 bsize(-1, 0);
    const bool haveSelection = config_.hasSection(selected_) && selected_ != Config::kGlobalSection;

    ImGui::BeginDisabled(!haveSelection || importRunning_);
    if (ImGui::Button("Start", bsize))
        launch = launchTarget(selected_);
    ImGui::EndDisabled();

    ImGui::BeginDisabled(pickInProgress_ || importRunning_ || importPlan_);
    if (ImGui::Button("Add Game...", bsize)) {
        pickInProgress_ = true;
        pickFor_ = Pick::AddGame;
        SDL_ShowOpenFolderDialog(pickFolderCallback, this, sys_.window(), nullptr, false);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play a game from the folder it's in");
    if (ImGui::Button("Import Game...", bsize)) {
        pickInProgress_ = true;
        pickFor_ = Pick::ImportSource;
        SDL_ShowOpenFolderDialog(pickFolderCallback, this, sys_.window(), nullptr, false);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy a game's files from its CD or an install into a folder of Reluva's,\n"
                          "only those Reluva needs; nothing is installed");
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!haveSelection || importRunning_);
    if (ImGui::Button("Remove", bsize)) {
        config_.removeSection(selected_);
        config_.save();
        status_ = "Removed " + selected_;
        selected_.clear();
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
    ImGui::TextUnformatted(status_.empty() ? "Ready." : status_.c_str());

    drawImport();

    // Several games matched one folder: let the user pick.
    if (!pendingChoices_.empty()) {
        ImGui::OpenPopup("Choose game");
        if (ImGui::BeginPopupModal("Choose game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Several games match this folder:");
            for (const DetectedGame& g : pendingChoices_) {
                std::string label = g.description + " (" + g.language + ", " + g.platform + ")";
                if (ImGui::Selectable(label.c_str())) {
                    addGame(g, pendingPath_);
                    pendingChoices_.clear();
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
            if (ImGui::Button("Cancel")) {
                pendingChoices_.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

void SDLCALL Launcher::pickFolderCallback(void* userdata, const char* const* files, int) {
    auto* self = static_cast<Launcher*>(userdata);
    {
        std::lock_guard lock(self->pickMutex_);
        if (!files)
            self->pickError_ = std::string("Folder dialog failed: ") + SDL_GetError();
        else if (files[0])
            self->pickedFolder_ = files[0];
    }
    self->pickInProgress_ = false;
}

void Launcher::handlePickedFolder() {
    std::optional<std::string> folder, error;
    {
        std::lock_guard lock(pickMutex_);
        folder.swap(pickedFolder_);
        error.swap(pickError_);
    }
    if (error)
        status_ = *error;
    if (!folder)
        return;

    if (pickFor_ == Pick::ImportDestination) {
        // A folder of the game's own inside the one chosen.
        if (importPlan_)
            importDest_ = std::filesystem::path(*folder) / importDest_.filename();
        return;
    }
    if (pickFor_ == Pick::ImportSource) {
        auto plans = planImports(*folder);
        if (plans.empty())
            status_ = "Nothing Reluva can import found in " + *folder;
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
            status_ = "No supported game found in " + *folder;
    } else if (found.size() == 1) {
        addGame(found.front(), *folder);
    } else {
        pendingPath_ = *folder;
        pendingChoices_ = std::move(found);
    }
}

void Launcher::addGame(const DetectedGame& game, const std::string& path) {
    std::string name = game.gameId;
    for (int n = 2; config_.hasSection(name); ++n)
        name = game.gameId + "-" + std::to_string(n);

    config_.set(name, "engine", game.engineId);
    config_.set(name, "gameid", game.gameId);
    config_.set(name, "description", game.description);
    config_.set(name, "language", game.language);
    config_.set(name, "platform", game.platform);
    config_.set(name, "path", path);
    if (Error err = config_.save())
        status_ = err.message;
    else
        status_ = "Added " + game.description;
    selected_ = name;
}

GameTarget Launcher::launchTarget(const std::string& name) {
    // A patch (or more of the game) may have been added to the folder since.
    const std::string engine = config_.get(name, "engine"), gameId = config_.get(name, "gameid");
    for (const DetectedGame& g : detectGames(config_.get(name, "path")))
        if (g.engineId == engine && g.gameId == gameId && g.description != config_.get(name, "description")) {
            config_.set(name, "description", g.description);
            config_.save();
            break;
        }
    return targetFromConfig(name);
}

void Launcher::offerImport(std::vector<ImportPlan> plans, const std::string& source) {
    importPlan_ = std::move(plans.front());
    importSource_ = source;
    // A new folder in the library, named after the game.
    std::filesystem::path dest = libraryDir_ / importPlan_->game.gameId;
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(dest, ec); ++n)
        dest = libraryDir_ / (importPlan_->game.gameId + "-" + std::to_string(n));
    importDest_ = dest;
}

void Launcher::drawImport() {
    if (!importPlan_) {
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
    const ImportPlan& plan = *importPlan_;
    ImGui::TextUnformatted(plan.game.description.c_str());
    ImGui::TextDisabled("from %s", importSource_.c_str());
    ImGui::Spacing();
    for (const std::string& note : plan.notes)
        ImGui::BulletText("%s", note.c_str());
    ImGui::Spacing();
    ImGui::Text("%zu files, %.0f MB, copied to:", plan.files.size(), double(plan.totalBytes()) / (1024 * 1024));
    ImGui::TextWrapped("%s", importDest_.string().c_str());
    ImGui::Spacing();
    if (importRunning_) {
        const uint64_t total = std::max<uint64_t>(importTotal_, 1);
        ImGui::ProgressBar(float(double(importBytes_) / double(total)), ImVec2(-1, 0));
        ImGui::BeginDisabled(importCancel_);
        if (ImGui::Button("Cancel"))
            importCancel_ = true;
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Import"))
            startImport();
        ImGui::SameLine();
        ImGui::BeginDisabled(pickInProgress_);
        if (ImGui::Button("Change folder...")) {
            pickInProgress_ = true;
            pickFor_ = Pick::ImportDestination;
            SDL_ShowOpenFolderDialog(pickFolderCallback, this, sys_.window(), nullptr, false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            importPlan_.reset();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

void Launcher::startImport() {
    std::error_code ec;
    if (std::filesystem::exists(importDest_, ec) && !std::filesystem::is_empty(importDest_, ec)) {
        status_ = importDest_.string() + " isn't empty: choose another folder";
        return;
    }
    importCancel_ = false;
    importDone_ = false;
    importBytes_ = 0;
    importTotal_ = importPlan_->totalBytes();
    importRunning_ = true;
    status_ = "Importing " + importPlan_->game.description + "...";
    importThread_ = std::thread([this, plan = *importPlan_, dest = importDest_] {
        Error err = runImport(plan, dest, [this](uint64_t done, uint64_t total) {
            importBytes_ = done;
            importTotal_ = total;
            return !importCancel_;
        });
        {
            std::lock_guard lock(importMutex_);
            importError_ = err ? err.message : std::string();
        }
        importDone_ = true;
    });
}

void Launcher::finishImport() {
    if (!importDone_)
        return;
    importThread_.join();
    importDone_ = false;
    importRunning_ = false;
    std::string error;
    {
        std::lock_guard lock(importMutex_);
        error.swap(importError_);
    }
    const ImportPlan plan = std::move(*importPlan_);
    importPlan_.reset();
    if (!error.empty()) {
        status_ = error + (importCancel_ ? "; the files copied so far are in " + importDest_.string() : "");
        return;
    }
    // Added as the folder it went to, as detected there.
    for (const DetectedGame& g : detectGames(importDest_))
        if (g.engineId == plan.game.engineId && g.gameId == plan.game.gameId) {
            addGame(g, importDest_.string());
            status_ = "Imported " + g.description + " to " + importDest_.string();
            return;
        }
    status_ = "Imported to " + importDest_.string() + ", but the game isn't found there";
}

GameTarget Launcher::targetFromConfig(const std::string& name) const {
    return GameTarget{name, config_.get(name, "engine"), config_.get(name, "gameid"),
                      config_.get(name, "description", name), config_.get(name, "path"), {}};
}

} // namespace reluva
