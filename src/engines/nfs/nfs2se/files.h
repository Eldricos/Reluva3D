// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/game_files.h"

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

/**
 * The race's palette (RGBA), the palette entry of the HUD's pictures
 * (GameData/DashHud/pc/hud.fsh), which the dashboards share; empty if it
 * can't be read. The software game draws its pause screens, whose
 * pictures have no palette, in it: their 3dfx copies, in colour, agree with
 * it (docs/engines/nfs/formats/fsh.md).
 */
std::vector<uint8_t> racePalette(const GameFiles &files);

/** Why `folders` can't be NFS II SE's (no tracks in them), or success. */
Error checkFolders(const GameFolders &folders);

} // namespace reluva::nfs::nfs2se
