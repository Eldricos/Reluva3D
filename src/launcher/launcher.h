// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/config.h"
#include "common/detection.h"
#include "engines/engine.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace reluva {

class SdlSystem;

// The game list UI, drawn with Dear ImGui on the SDL renderer.
class Launcher {
public:
    Launcher(SdlSystem& system, Config& config);
    ~Launcher();
    Launcher(const Launcher&) = delete;
    Launcher& operator=(const Launcher&) = delete;

    // Show the launcher until the user starts a game (returned) or quits.
    std::optional<GameTarget> run();

    // Shown in the status bar next time the launcher appears.
    void setStatus(std::string message) { status_ = std::move(message); }
    // Where imported games go by default (a folder each).
    void setLibraryDir(std::filesystem::path dir) { libraryDir_ = std::move(dir); }

private:
    void drawUi(std::optional<GameTarget>& launch, bool& quit);
    void handlePickedFolder();
    void addGame(const DetectedGame& game, const std::string& path);
    GameTarget targetFromConfig(const std::string& name) const;
    // The target as stored, its description first brought up to date with
    // what's in its folder now (a patch added since, say).
    GameTarget launchTarget(const std::string& name);
    void offerImport(std::vector<ImportPlan> plans, const std::string& source);
    void drawImport();
    void startImport();
    void finishImport();

    static void pickFolderCallback(void* userdata, const char* const* files, int filter);

    SdlSystem& sys_;
    Config& config_;
    std::string selected_;
    std::string status_;

    // The folder dialog answers on another thread.
    std::mutex pickMutex_;
    std::optional<std::string> pickedFolder_;
    std::optional<std::string> pickError_;
    std::atomic<bool> pickInProgress_{false};

    // Folder with several matches, waiting for the user to choose one.
    std::string pendingPath_;
    std::vector<DetectedGame> pendingChoices_;

    // What the next folder picked is for.
    enum class Pick { AddGame, ImportSource, ImportDestination } pickFor_ = Pick::AddGame;

    // Importing: what was found and where it goes; then the copying, on a
    // thread of its own, and how it ended.
    std::filesystem::path libraryDir_;
    std::optional<ImportPlan> importPlan_;
    std::string importSource_;
    std::filesystem::path importDest_;
    std::thread importThread_;
    std::atomic<bool> importRunning_{false}, importCancel_{false}, importDone_{false};
    std::atomic<uint64_t> importBytes_{0}, importTotal_{0};
    std::mutex importMutex_;
    std::string importError_;
};

} // namespace reluva
