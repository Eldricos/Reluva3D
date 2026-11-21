// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3 {

/**
 * How many archives one game folder may add; game folders mounted later
 * get priorities above every archive of earlier ones.
 */
constexpr int kPrioritiesPerGameDir = 1000;

/**
 * Adds one game folder of an install (the base game's, or a mod's) to the
 * search set, the way id Tech 3 games search their files (see
 * docs/engines/idtech3/formats/pk3.md): loose files first, then the folder's .pk3
 * files, later names (compared in upper case) before earlier ones, so
 * pak1.pk3 overrides pak0.pk3 and pak_x.pk3 overrides paka.pk3.
 *
 * A .pk3 that can't be opened is skipped and reported in `warnings`; a
 * missing folder, or one with kPrioritiesPerGameDir paks or more, is an
 * error.
 * @param layer Orders game folders, higher searched first: mountGame gives
 *        the base game's 0 and 1, a mod's 2 and 3.
 * @param skip Paks left out (lower case): the Expansion Pack's, when it
 *        isn't wanted.
 */
Error mountGameDir(SearchSet &set, const std::filesystem::path &install, std::string_view gameDir, int layer,
                   std::vector<std::string> &warnings, const std::vector<std::string> &skip = {});

/**
 * The paks for downloaded paks (and the like) start at this priority, above
 * every game folder mountGame adds.
 */
constexpr int kDownloadPriority = 4 * kPrioritiesPerGameDir;
/**
 * Replaces what `set` holds with the game's files, as Elite Force's engines
 * search them: the base game's folder in the install, then in the home
 * folder (Reluva's save folder for the game, if any), then the mod's (if
 * one is given) in each, later ones searched first. The base game's
 * install folder must be there; the rest are added if they are. `skip`
 * applies to the base game's install folder.
 */
Error mountGame(SearchSet &set, const std::filesystem::path &install, const std::filesystem::path &home,
                std::string_view baseDir, std::string_view mod, std::vector<std::string> &warnings,
                const std::vector<std::string> &skip = {});
/**
 * A name fs_game may take: a single folder's name, not the base game's (any
 * case) and not starting with '.'; empty if it's none (the game itself).
 */
std::string modFolderName(std::string_view value, std::string_view baseDir);
/**
 * Where a mod's own files go in a game's save folder (its saves, settings,
 * screenshots): mods/`mod`; the save folder itself for none.
 */
std::filesystem::path modSavePath(const std::filesystem::path &savePath, std::string_view mod);

/** A mod folder: its name, and its description to show. */
struct ModFolder {
    std::string name;
    std::string description;
};
/**
 * The mods an install holds, as Elite Force's engines list them for
 * "$modlist" (FS_GetFileList; 1.2's and the Expansion Pack's alike): the
 * install's folders but the base game's and those starting with '.', that
 * hold a .pk3 of their own, in Windows' order (fs::windowsNameLess); each
 * with the first 48 bytes of its description.txt, or its name.
 */
std::vector<ModFolder> listMods(const std::filesystem::path &install, std::string_view baseDir);
/**
 * The mods into a buffer of `size` bytes as the engines write it: each
 * mod's name and description, each with its NUL, stopping at the first
 * that would leave 2 bytes or fewer.
 * @return How many were written.
 */
uint32_t packModList(const std::vector<ModFolder> &mods, uint32_t size, std::vector<uint8_t> &out);

} // namespace reluva::idtech3
