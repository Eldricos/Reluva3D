// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/installshield.h"
#include "common/iso9660.h"
#include "common/types.h"
#include "common/wise.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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

/**
 * What detection needs of a file: its size, and on demand the MD5 of its
 * first kDetectionHashBytes bytes (none if it can't be read).
 */
struct FoundFile {
    uint64_t size = 0;
    std::function<std::optional<std::string>()> md5;
};

/**
 * Finds a file by its relative path ('/' between folders), in any case;
 * none when it isn't there. Lets detection look in a folder or in a disc's
 * image alike.
 */
using FileLookup = std::function<std::optional<FoundFile>(const std::string &name)>;

/** A FileLookup for the files under `dir`. */
FileLookup folderLookup(const std::filesystem::path &dir);

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
    /** A file to copy, or to take out of an installer, a cabinet or a disc's image. */
    struct File {
        File() = default;
        File(std::filesystem::path from, std::string to, uint64_t size, std::optional<WiseStream> packed = {},
             std::optional<size_t> cabinetFile = {})
            : from(std::move(from)), to(std::move(to)), size(size), packed(std::move(packed)),
              cabinetFile(cabinetFile) {}

        std::filesystem::path from;
        std::string to;
        uint64_t size = 0;
        // When `from` is an installer (a patch) holding the file: where.
        std::optional<WiseStream> packed;
        // When `from` is `cabinet`'s header (data1.hdr): which of its files.
        std::optional<size_t> cabinetFile;
        // When the file is on a disc's image: its data track (`from` is
        // the track's image file), and the file's first sector there.
        std::optional<IsoTrack> disc;
        uint32_t discSector = 0;
    };
    std::vector<File> files;
    // The cabinet files are taken out of (a game's CDs'), when there is
    // one: kept while the plan waits for its other volumes, too.
    std::shared_ptr<const InstallShieldCabinet> cabinet;
    std::vector<std::string> notes;
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
 * on the first file that can't be read or written.
 */
Error runImport(const ImportPlan &plan, const std::filesystem::path &dest,
                const std::function<bool(uint64_t done, uint64_t total)> &progress);

/** Every variant in `table` whose files all match inside `dir` (a variant without files never does). */
std::vector<DetectedGame> detectFromTable(const std::string &engineId, const std::vector<GameVariant> &table,
                                          const std::filesystem::path &dir);
/** The same, the files found by `files`. */
std::vector<DetectedGame> detectFromTable(const std::string &engineId, const std::vector<GameVariant> &table,
                                          const FileLookup &files);

} // namespace reluva
