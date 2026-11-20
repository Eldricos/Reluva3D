// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace reluva::botf {

/**
 * Stbof.ini's KEY=VALUE lines (docs/engines/botf/systems/game-files.md), keys
 * in upper case. Lines without '=' are skipped.
 */
std::map<std::string, std::string> parseStbofIni(std::string_view text);

/**
 * Where the game's files are. A "minimum" install leaves music.snd, the
 * movies and the language folder's files on the CD (Stbof.ini's CDPATH), so
 * a name is looked for, ignoring case, in:
 *   the game folder, its <LANGUAGE> folder (a copied ENGLISH/ or GERMAN/),
 *   the CD folder, and the CD's <LANGUAGE> folder.
 */
class GameFiles {
public:
    explicit GameFiles(const std::filesystem::path &gameDir);

    std::optional<std::filesystem::path> find(std::string_view name) const;

    /** "ENGLISH" unless Stbof.ini says otherwise. */
    const std::string &language() const { return _language; }
    /** The speech archive for the language: english.snd, german.snd. */
    std::string speechFile() const;
    /** Stbof.ini's CDPATH, when that folder exists here. */
    const std::optional<std::filesystem::path> &cdDir() const { return _cd; }
    /** Stbof.ini's values, as parseStbofIni() gives them; empty without one. */
    const std::map<std::string, std::string> &ini() const { return _ini; }

private:
    std::filesystem::path _game;
    std::optional<std::filesystem::path> _cd;
    std::string _language = "ENGLISH";
    std::map<std::string, std::string> _ini;
};

} // namespace reluva::botf
