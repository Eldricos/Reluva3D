// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace reluva {

// INI-style settings file. The [reluva] section holds global settings, and
// the settings for every game; [engine:<id>] sections hold the settings for
// one engine's games; every other section is a game target (one installed
// game the user added), with its own settings. A game's settings are all
// three merged, the most specific one set winning (settingsFor).
class Config {
public:
    static constexpr const char *kGlobalSection = "reluva";
    // The section of an engine's settings, for all its games.
    static std::string engineSection(const std::string &engineId) { return "engine:" + engineId; }
    static bool isEngineSection(const std::string &section) { return section.rfind("engine:", 0) == 0; }

    Error load(const std::filesystem::path &path);
    Error save() const;

    std::string get(const std::string &section, const std::string &key, const std::string &fallback = {}) const;
    void set(const std::string &section, const std::string &key, const std::string &value);
    // Removes a key (a setting goes back to what's set above it).
    void unset(const std::string &section, const std::string &key);
    bool has(const std::string &section, const std::string &key) const;

    bool hasSection(const std::string &section) const;
    void removeSection(const std::string &section);
    std::vector<std::string> targets() const;
    // Every key and value of a section (none if there's no such section).
    std::map<std::string, std::string> values(const std::string &section) const;
    // A game's settings: the global section's, then its engine's, then its
    // own, each replacing the one before.
    std::map<std::string, std::string> settingsFor(const std::string &target) const;

private:
    std::filesystem::path _path;
    std::map<std::string, std::map<std::string, std::string>> _sections;
};

} // namespace reluva
