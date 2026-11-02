// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "common/wise.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace reluva {

// How many leading bytes of a file are hashed for detection. Hashing only the
// start keeps detection fast even for large archives.
constexpr uint64_t kDetectionHashBytes = 5000;

// One file that must be present for a game variant to match.
struct FileSignature {
    std::string name;     // relative path, matched case-insensitively
    std::string md5;      // of the first kDetectionHashBytes bytes; empty = any
    int64_t size = -1;    // exact file size; -1 = any
};

// One release of a game (language, platform, patch level...).
struct GameVariant {
    std::string gameId;      // stable id, e.g. "newworlds"
    std::string description; // shown to the user
    std::string language;    // "en", "de", ...
    std::string platform;    // "windows", ...
    std::vector<FileSignature> files;
};

struct DetectedGame {
    std::string engineId;
    std::string gameId;
    std::string description;
    std::string language;
    std::string platform;
};

// A game found somewhere it isn't to be run from (its CD, an install made
// by its own setup program, a copy of either), and the files Reluva needs
// of it: each copied from `from` to `to` (a relative path, '/' between
// folders) in the folder it will run from. `notes` say what was found
// and what's missing, for the person importing it.
struct ImportPlan {
    DetectedGame game;
    struct File {
        std::filesystem::path from;
        std::string to;
        uint64_t size = 0;
        // When `from` is an installer (a patch) holding the file: where.
        std::optional<WiseStream> packed;
    };
    std::vector<File> files;
    std::vector<std::string> notes;
    uint64_t totalBytes() const {
        uint64_t n = 0;
        for (const File& f : files)
            n += f.size;
        return n;
    }
};

// Copies a plan's files into `dest` (made if needed), each first to a
// ".part" file renamed when whole. `progress` is told the bytes copied so
// far and the total, often; returning false stops the import (what was
// copied stays). Fails on the first file that can't be read or written.
Error runImport(const ImportPlan& plan, const std::filesystem::path& dest,
                const std::function<bool(uint64_t done, uint64_t total)>& progress);

// Return every variant in `table` whose files all match inside `dir`.
std::vector<DetectedGame> detectFromTable(const std::string& engineId,
                                          const std::vector<GameVariant>& table,
                                          const std::filesystem::path& dir);

} // namespace reluva
