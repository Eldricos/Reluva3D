// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/player_files.h"

#include "common/text.h"

#include <algorithm>
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
    std::filesystem::create_directories((_folder / name).parent_path(), ec);
    std::ofstream out(_folder / name, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    return out ? Error::success() : Error::fail(name + ": can't be written");
}

std::vector<std::string> PlayerFiles::list(const std::string &folder, const std::string &suffix) const {
    std::vector<std::string> names;
    std::error_code ec;
    if (_folder.empty() || !std::filesystem::is_directory(_folder / folder, ec))
        return names;
    for (const auto &entry : std::filesystem::directory_iterator(_folder / folder, ec))
        if (entry.is_regular_file(ec) && text::hasSuffixIgnoreCase(entry.path().filename().string(), suffix))
            names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

void PlayerFiles::remove(const std::string &name) const {
    if (_folder.empty())
        return;
    std::error_code ec;
    std::filesystem::remove(_folder / name, ec);
}

} // namespace reluva::storm3d
