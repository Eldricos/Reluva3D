// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * Files of DATA.dat a page shows, sorted, and which is shown: Left/Right
 * step through them, PageUp/PageDown by 10, Home/End to the ends, F to the
 * next folder's first.
 */
class FileList {
public:
    /** The archive's files `wanted` says yes to. */
    FileList(const Archive &data, const std::function<bool(const std::string &lowerName)> &wanted);

    size_t size() const { return _names.size(); }
    bool empty() const { return _names.empty(); }
    size_t index() const { return _current; }
    const std::string &current() const { return _names[_current]; }
    const std::string &name(size_t i) const { return _names[i]; }
    /** "3/40  Folder/File.ext": where it is, for the status line. */
    std::string position() const;
    /** Moves by a key it knows; false for a key it doesn't. */
    bool handleKey(uint32_t key);

private:
    std::vector<std::string> _names;
    size_t _current = 0;
};

} // namespace reluva::stawayteam
