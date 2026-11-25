// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/installshield.h"
#include "common/types.h"
#include "common/wise.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva {

/**
 * How many leading bytes of a file are hashed for detection. Hashing only
 * the start keeps detection fast even for large archives.
 */
constexpr uint64_t kDetectionHashBytes = 5000;

/** One file that must be present for a game variant to match. */
struct FileSignature {
    std::string name;  // relative path, matched case-insensitively
    std::string md5;   // of the first kDetectionHashBytes bytes; empty = any
    int64_t size = -1; // exact file size; -1 = any
};

/** One release of a game (language, platform, patch level...), and the files that tell it. */
struct GameVariant {
    std::string gameId;      // stable id, e.g. "newworlds"
    std::string description; // shown to the user
    std::string language;    // "en", "de", ...
    std::string platform;    // "windows", ...
    std::vector<FileSignature> files;
};

/** A game found in a folder: which engine runs it, and which release it is. */
struct DetectedGame {
    std::string engineId;
    std::string gameId;
    std::string description;
    std::string language;
    std::string platform;
};

/**
 * A game found somewhere it isn't to be run from (its CD, an install made
 * by its own setup program, a copy of either), and the files Reluva needs
 * of it: each copied from `from` to `to` (a relative path, '/' between
 * folders) in the folder it will run from. `notes` say what was found
 * and what's missing, for the person importing it.
 */
struct ImportPlan {
    DetectedGame game;
    /** A file to copy, or to take out of an installer or a cabinet. */
    struct File {
        std::filesystem::path from;
        std::string to;
        uint64_t size = 0;
        // When `from` is an installer (a patch) holding the file: where.
        std::optional<WiseStream> packed;
        // When `from` is `cabinet`'s header (data1.hdr): which of its files.
        std::optional<size_t> cabinetFile;
    };
    std::vector<File> files;
    // The cabinet files are taken out of (a game's CDs'), when there is
    // one: kept while the plan waits for its other volumes, too.
    std::shared_ptr<const InstallShieldCabinet> cabinet;
    std::vector<std::string> notes;
    // Run once the files are copied, in their folder: a patch applied by
    // running the patch's own program, say. Its error is the import's.
    std::function<Error(const std::filesystem::path &dest)> afterCopy;
    // What else the person importing may add (MetaEngine::addToImport): a
    // patch's installer, and a folder (an expansion's CD, say), with how the
    // launcher offers them; an empty label offers none.
    std::string addPatchLabel = "Add patch...";
    std::string addPatchTooltip = "Apply the game's patch (its installer, e.g. eliteforcepatch1_2.exe) as it's copied";
    std::string addFolderLabel = "Add expansion...";
    std::string addFolderTooltip = "Copy an expansion too, from its CD (or a folder it was installed to)";
    /** The bytes the files come to, for the import's progress. */
    uint64_t totalBytes() const {
        uint64_t n = 0;
        for (const File &f : files)
            n += f.size;
        return n;
    }
};

/**
 * Copies a plan's files into `dest` (made if needed), each first to a
 * ".part" file renamed when whole, so an import stopped halfway leaves no
 * file cut short. `progress` is told the bytes copied so far and the total,
 * often; returning false stops the import (what was copied stays). Fails
 * on the first file that can't be read or written. Then the plan's
 * afterCopy, if it has one.
 */
Error runImport(const ImportPlan &plan, const std::filesystem::path &dest,
                const std::function<bool(uint64_t done, uint64_t total)> &progress);

/** Importing from an InstallShield cabinet, a game's CDs. */
///@{
/** The groups named, as setup installs them; none if one of them isn't in `cabinet`. */
std::vector<InstallShieldGroup> installShieldGroups(const InstallShieldCabinet &cabinet,
                                                    const std::vector<std::string> &names);
/**
 * Whether `variant`'s files are among the groups' with their sizes. Their
 * MD5s are of what's unpacked, so not known yet: detection checks them
 * once the game is imported.
 */
bool installShieldMatches(const GameVariant &variant, const InstallShieldCabinet &cabinet,
                          const std::vector<InstallShieldGroup> &groups);
/**
 * The plan's files: the groups', as setup would install them, but for
 * those under a folder in `leaveOut`; a file loose on a CD (version 5's)
 * from the folder of the CD it's on: the header's or a volume's. Until every volume they're in
 * is found there are none: the notes say which are still wanted.
 */
void planInstallShield(ImportPlan &plan, const std::shared_ptr<const InstallShieldCabinet> &cabinet,
                       const std::vector<InstallShieldGroup> &groups, const std::vector<std::string> &leaveOut = {});
/**
 * Adds the volumes in `folder` (the game's other CD) to the plan's
 * cabinet and plans again.
 * @return False, with why in `message`, if none of the volumes wanted are there.
 */
bool addInstallShieldVolumes(ImportPlan &plan, const std::filesystem::path &folder,
                             const std::vector<InstallShieldGroup> &groups, std::string &message,
                             const std::vector<std::string> &leaveOut = {});
///@}

/** Every variant in `table` whose files all match inside `dir` (a variant without files never does). */
std::vector<DetectedGame> detectFromTable(const std::string &engineId, const std::vector<GameVariant> &table,
                                          const std::filesystem::path &dir);

} // namespace reluva
