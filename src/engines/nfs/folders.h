// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace reluva::nfs {

// Where the game's files are. NFS III installs everything but the music
// (GameData/Audio/pc) and the movies (FeData/movies), which it reads from
// the CD (docs/engines/nfs.md).
struct GameFolders {
    std::filesystem::path install;
    std::filesystem::path cd; // empty: no CD's files found
    std::string cdSource;     // how `cd` was found, for messages
};

// Whether a folder holds the CD's music.
bool hasCdFiles(const std::filesystem::path& dir);

// The CD's folder for an install, the first of:
// - the target's `cdpath` setting (relative to the install if not absolute);
// - the install itself, when the CD's files were copied into it or the
//   game was added from the CD;
// - a folder beside the install with the game's executable and the CD's
//   files (a copy of the CD next to the install).
GameFolders findFolders(const std::filesystem::path& install, const std::map<std::string, std::string>& settings,
                        const char* executable);

// The track folders (GameData/Tracks/trkNNN) and car folders (GameData/
// CarModel/<car> with a car.viv, the Traffic folder left out) in an
// install, sorted by name.
std::vector<std::string> trackFolders(const std::filesystem::path& install);
std::vector<std::string> carFolders(const std::filesystem::path& install);

// The files in `relative` under `dir` whose extension is `extension`
// (".mus"), ignoring case, sorted; empty if the folder isn't there or
// `dir` is empty.
std::vector<std::string> filesWithExtension(const std::filesystem::path& dir, const char* relative,
                                            const char* extension);

} // namespace reluva::nfs
