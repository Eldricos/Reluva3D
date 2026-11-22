// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/nfs/folders.h"

#include <filesystem>
#include <string>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * The tracks (GameData/Tracks/se/\*.trk), by file name without its extension
 * ("TR00"), from the install and the CD, sorted, each once. The install
 * leaves them on the CD, from which the game reads them (install.win).
 */
std::vector<std::string> tracks(const GameFolders &folders);

/**
 * The car models (the .geo files in GameData/Sim/CarData/cardata.viv: the
 * racing cars, the traffic and the bonus vehicles), by name, sorted, from
 * the install's cardata.viv or else the CD's; empty if neither can be read.
 */
std::vector<std::string> carModels(const GameFolders &folders);

/** Where the game's two programs are; an empty path for one not found. */
struct Programs {
    std::filesystem::path software; ///< nfs2sen.exe, drawing in software
    std::filesystem::path glide;    ///< nfs2sea.exe, drawing through 3dfx's Glide
};

/** The programs in the install, or else on the CD. */
Programs programs(const GameFolders &folders);

/** Why `folders` can't be NFS II SE's (no tracks in them), or success. */
Error checkFolders(const GameFolders &folders);

} // namespace reluva::nfs::nfs2se
