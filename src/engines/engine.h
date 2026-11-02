// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"
#include "common/system.h"
#include "common/types.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reluva {

// A game the user has added, as stored in the config file.
struct GameTarget {
    std::string target;   // config section name, unique per install
    std::string engineId;
    std::string gameId;
    std::string description;
    std::filesystem::path path; // game data directory
    // Where the engine may keep files for this game (settings, saves); may
    // not exist yet. Set by the frontend.
    std::filesystem::path savePath;
    // Everything stored for it, the engine's own options among them
    // (MetaEngine::options).
    std::map<std::string, std::string> settings;
};

// A choice the launcher offers for a game, stored with it as `key` = "1"
// or "0".
struct GameOption {
    std::string key, label, tooltip;
    bool defaultOn = true;
    // A list to pick from instead of a checkbox: each value (kept in the
    // entry's settings) and what's shown for it; the first is the default.
    std::vector<std::pair<std::string, std::string>> choices;
};

// A running game. Created by its MetaEngine, run once, then destroyed.
class Engine {
public:
    virtual ~Engine() = default;

    // Runs the game until the player quits or asks to return to the launcher.
    virtual Error run() = 0;
};

// Static description of an engine: its name, how to recognise its games,
// and how to create an instance.
class MetaEngine {
public:
    virtual ~MetaEngine() = default;

    virtual const char* id() const = 0;
    virtual const char* name() const = 0;

    virtual std::vector<DetectedGame> detect(const std::filesystem::path& dir) const = 0;
    // The games in `source` that can be imported (copied into a folder of
    // their own to run from): none unless the engine knows how.
    virtual std::vector<ImportPlan> planImport(const std::filesystem::path& /*source*/) const { return {}; }
    // The choices it offers for the game in `dir` (none by default).
    virtual std::vector<GameOption> options(const std::string& /*gameId*/, const std::filesystem::path& /*dir*/) const {
        return {};
    }
    // Adds what `file` holds for `plan`'s game (its patch: the patched
    // files, in place of the plan's own); false, with why in `message`, if
    // it isn't one the engine knows.
    virtual bool addToImport(ImportPlan& /*plan*/, const std::filesystem::path& /*file*/,
                             std::string& message) const {
        message = "not a file Reluva can use for this game";
        return false;
    }
    virtual std::unique_ptr<Engine> create(System& system, const GameTarget& target) const = 0;
};

} // namespace reluva
