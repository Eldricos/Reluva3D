// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/game_files.h"

#include "common/fs.h"
#include "engines/storm3d/formats/zfs_archive.h"

#include <memory>

namespace reluva::storm3d {

Error GameFiles::open(const std::filesystem::path &install) {
    _install = install;
    _files.clear();
    auto folders = std::make_unique<DirectoryArchive>();
    if (Error err = folders->open(install))
        return err;
    // The loose files come first: which the game prefers isn't known yet,
    // and the install and the archives share no name.
    _files.add(std::move(folders), 1);
    for (const char *name : {"assets.zfs", "objtxt.zfs"}) {
        const auto path = fs::findFile(install, name);
        if (!path)
            continue;
        auto zfs = std::make_unique<ZfsArchive>();
        if (Error err = zfs->open(*path))
            return err;
        _files.add(std::move(zfs), 0);
    }
    return Error::success();
}

} // namespace reluva::storm3d
