// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/formats/viv.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reluva::nfs {

/**
 * One of the game's files: loose in the install or on the CD, or inside a
 * .viv archive.
 */
struct FileRef {
    std::string name;           // relative path for display, "GameData/CarModel/Diab/car.viv/dash.qfs"
    std::filesystem::path file; // the file on disk (the .viv for archived files)
    std::string inner;          // the name inside the .viv; empty for loose files
};

/**
 * Every file of the game, from the install and then the CD (for files the
 * install doesn't have), .viv archives' contents included.
 */
class GameFiles {
public:
    /**
     * Lists the folders' files again, each folder's sorted by name. An
     * archive that can't be opened is listed without contents.
     */
    void scan(const GameFolders &folders);

    /** Every file scan() found, in its order. */
    const std::vector<FileRef> &files() const { return _files; }
    /**
     * The files whose name ends with one of the extensions given (".fsh", in
     * lower case), ignoring the names' case, in scan order.
     */
    std::vector<FileRef> withExtensions(std::initializer_list<const char *> extensions) const;
    /** The front-end string tables (FeData/text/text.*), loose files only. */
    std::vector<FileRef> textTables() const;

    /**
     * A file's whole contents. The last .viv read from stays open for the
     * next read, so calls from several threads at once aren't safe.
     */
    Error read(const FileRef &ref, std::vector<uint8_t> &out) const;

private:
    std::vector<FileRef> _files;
    // The .viv last read from, kept open for the next read.
    mutable std::filesystem::path _cachedPath;
    mutable std::unique_ptr<VivArchive> _cached;
};

} // namespace reluva::nfs
