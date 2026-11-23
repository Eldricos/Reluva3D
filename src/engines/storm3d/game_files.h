// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * A game's files, searched as one: the install's folders, then its ZFS
 * archives (`assets.zfs`, `objtxt.zfs`). Names are paths from the
 * install's folder ("textures/rgb/bresear.tga"), matched ignoring case;
 * an archive's files are at the top ("fdestroy.odf").
 */
class GameFiles {
public:
    /**
     * Lists the install's folders and opens its archives.
     * @return a failure if the folder can't be listed or an archive that's
     *         there can't be read; a missing archive isn't one
     */
    Error open(const std::filesystem::path &install);

    /** The files, searched in order. */
    const SearchSet &files() const { return _files; }
    /** Whether there's a file of that name. */
    bool has(std::string_view name) const { return _files.has(name); }
    /** A file's whole contents, decoded if it's in an archive. */
    Error read(std::string_view name, std::vector<uint8_t> &out) const { return _files.read(name, out); }
    /** Every file's name, sorted, each once. */
    std::vector<std::string> list() const { return _files.list(); }
    /** The install's folder. */
    const std::filesystem::path &install() const { return _install; }

private:
    std::filesystem::path _install;
    SearchSet _files;
};

} // namespace reluva::storm3d
