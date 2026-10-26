// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/viv.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reluva::nfs {

// One of the game's files: loose in the install or on the CD, or inside a
// .viv archive.
struct FileRef {
    std::string name;           // relative path for display, "GameData/CarModel/Diab/car.viv/dash.qfs"
    std::filesystem::path file; // the file on disk (the .viv for archived files)
    std::string inner;          // the name inside the .viv; empty for loose files
};

// Every file of the game, from the install and then the CD (for files the
// install doesn't have), .viv archives' contents included.
class GameFiles {
public:
    void scan(const GameFolders& folders);

    const std::vector<FileRef>& files() const { return files_; }
    // The files whose name (the part after the last '/') ends with one of
    // the extensions given (".fsh"), ignoring case, in scan order.
    std::vector<FileRef> withExtensions(std::initializer_list<const char*> extensions) const;
    // The front-end string tables (FeData/text/text.*).
    std::vector<FileRef> textTables() const;

    Error read(const FileRef& ref, std::vector<uint8_t>& out) const;

private:
    std::vector<FileRef> files_;
    // The .viv last read from, kept open for the next read.
    mutable std::filesystem::path cachedPath_;
    mutable std::unique_ptr<VivArchive> cached_;
};

} // namespace reluva::nfs
