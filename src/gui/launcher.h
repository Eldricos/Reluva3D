// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/config.h"
#include "common/detection.h"
#include "engines/engine.h"

#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace reluva {

class SdlSystem;

/** What the launcher's games are started for. */
enum class LauncherPurpose {
    kPlay, ///< reluva: playing them
    kView  ///< reluva-viewer: their engines' data viewers, and the testbed
};

// The game list UI, drawn with Dear ImGui on the SDL renderer.
class Launcher {
public:
    Launcher(SdlSystem &system, Config &config, LauncherPurpose purpose = LauncherPurpose::kPlay);
    ~Launcher();
    Launcher(const Launcher &) = delete;
    Launcher &operator=(const Launcher &) = delete;

    // Show the launcher until the user starts a game (returned) or quits.
    std::optional<GameTarget> run();

    // Shown in the status bar next time the launcher appears.
    void setStatus(std::string message) { _status = std::move(message); }
    // Where imported games go by default (a folder each).
    void setLibraryDir(std::filesystem::path dir) { _libraryDir = std::move(dir); }

private:
    void drawUi(std::optional<GameTarget> &launch, bool &quit);
    void handlePickedFolder();
    void addGame(const DetectedGame &game, const std::string &path);
    GameTarget targetFromConfig(const std::string &name) const;
    // The target as stored, its description first brought up to date with
    // what's in its folder now (a patch added since, say).
    GameTarget launchTarget(const std::string &name);
    void offerImport(std::vector<ImportPlan> plans, const std::string &source);
    void drawImport();
    void startImport();
    void finishImport();

    static void pickFolderCallback(void *userdata, const char *const *files, int filter);

    SdlSystem &_sys;
    Config &_config;
    LauncherPurpose _purpose;
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

    // What the next folder picked is for.
    enum class Pick {
        kAddGame,
        kImportSource,
        kImportDestination,
        kPatchForImport,
        kPatchForGame
    } _pickFor = Pick::kAddGame;
    void pickPatch(Pick purpose);
    void pickExpansion(Pick purpose); // a folder: an expansion's CD or install

    // Importing: what was found and where it goes; then the copying, on a
    // thread of its own, and how it ended.
    std::filesystem::path _libraryDir;
    std::optional<ImportPlan> _importPlan;
    std::string _importSource;
    std::filesystem::path _importDest;
    std::thread _importThread;
    std::atomic<bool> _importRunning{false};
    std::atomic<bool> _importCancel{false};
    std::atomic<bool> _importDone{false};
    std::atomic<uint64_t> _importBytes{0};
    std::atomic<uint64_t> _importTotal{0};
    std::mutex _importMutex;
    std::string _importError;
    // Applying a patch to a game already added (into its own folder),
    // rather than importing a new one.
    std::string _patchTarget;

    // The selected game's options (MetaEngine::options), found again when
    // the selection or its folder's contents may have changed.
    std::string _optionsFor;
    std::vector<GameOption> _options;
    void drawOptions();
    // Every game's options, for the list (found once per game).
    std::map<std::string, std::vector<GameOption>> _listOptions;
    std::string listLabel(const std::string &name);

    // Settings at three levels (common/settings.h): for every game, for an
    // engine's games, and with each game. The Settings window edits the
    // first two; the selected game's own are shown with its options.
    bool _settingsOpen = false;
    std::string _settingsEngine; // the engine whose games' settings the window shows; "" for every game
    void drawSettingsWindow();
    // One setting kept in `section`: its value there, or the one above it
    // (`engineId`'s, then every game's, then the default) when unset.
    void drawSetting(const std::string &section, const std::string &engineId, const GameOption &option);
    // `from`: where it comes from ("default", "every game's", "engine's").
    std::string inheritedValue(const std::string &section, const std::string &engineId, const GameOption &option,
                               std::string *from = nullptr) const;
};

} // namespace reluva
