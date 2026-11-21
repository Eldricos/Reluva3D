// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/pages/file_list.h"

#include "common/events.h"
#include "common/fs.h"
#include "engines/stawayteam/pages/draw.h"

#include <algorithm>

namespace reluva::stawayteam {

FileList::FileList(const Archive &data, const std::function<bool(const std::string &)> &wanted) {
    for (std::string &name : data.list())
        if (wanted(fs::toLower(name)))
            _names.push_back(std::move(name));
    std::sort(_names.begin(), _names.end(),
              [](const std::string &a, const std::string &b) { return fs::toLower(a) < fs::toLower(b); });
}

std::string FileList::position() const {
    if (_names.empty())
        return "nothing";
    return std::to_string(_current + 1) + "/" + std::to_string(_names.size()) + "  " + _names[_current];
}

bool FileList::handleKey(uint32_t key) {
    if (_names.empty())
        return false;
    const size_t n = _names.size();
    if (key == key::kRight) {
        _current = (_current + 1) % n;
    } else if (key == key::kLeft) {
        _current = (_current + n - 1) % n;
    } else if (key == key::kPageDown) {
        _current = std::min(_current + 10, n - 1);
    } else if (key == key::kPageUp) {
        _current = _current >= 10 ? _current - 10 : 0;
    } else if (key == key::kHome) {
        _current = 0;
    } else if (key == key::kEnd) {
        _current = n - 1;
    } else if (key == 'f') {
        const std::string folder = folderOf(_names[_current]);
        const size_t start = _current;
        do
            _current = (_current + 1) % n;
        while (_current != start && folderOf(_names[_current]) == folder);
    } else {
        return false;
    }
    return true;
}

} // namespace reluva::stawayteam
