// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/config.h"
#include "common/detection.h"
#include "engines/engine.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
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
    void setStatus(std::string message) { _status = std::move(message); }

private:
    void drawUi(std::optional<GameTarget>& launch, bool& quit);
    void handlePickedFolder();
    void addGame(const DetectedGame& game, const std::string& path);
    GameTarget targetFromConfig(const std::string& name) const;

    static void pickFolderCallback(void* userdata, const char* const* files, int filter);

    SdlSystem& _sys;
    Config& _config;
    std::string _selected;
    std::string _status;

    // The folder dialog answers on another thread.
    std::mutex _pickMutex;
    std::optional<std::string> _pickedFolder;
    std::optional<std::string> _pickError;
    std::atomic<bool> _pickInProgress{false};

    // Folder with several matches, waiting for the user to choose one.
    std::string _pendingPath;
    std::vector<DetectedGame> _pendingChoices;
};

} // namespace reluva
