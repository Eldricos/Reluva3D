// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/player_files.h"

#include <fstream>
#include <iterator>

namespace reluva::storm3d {

Error PlayerFiles::read(const std::string &name, std::vector<uint8_t> &out) const {
    if (!_folder.empty()) {
        std::ifstream in(_folder / name, std::ios::binary);
        if (in) {
            out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return Error::success();
        }
    }
    return _files.read(name, out);
}

Error PlayerFiles::write(const std::string &name, const std::vector<uint8_t> &data) const {
    if (_folder.empty())
        return Error::fail(name + ": no save folder");
    std::error_code ec;
    std::filesystem::create_directories(_folder, ec);
    std::ofstream out(_folder / name, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    return out ? Error::success() : Error::fail(name + ": can't be written");
}

} // namespace reluva::storm3d
