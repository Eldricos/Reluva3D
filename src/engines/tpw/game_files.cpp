// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/game_files.h"

#include "common/fs.h"
#include "engines/tpw/formats/wad.h"

#include <algorithm>
#include <memory>

namespace reluva::tpw {

Error mountGame(SearchSet &set, const std::filesystem::path &install, std::vector<std::string> &warnings) {
    const auto data = fs::findDirectory(install, "Data");
    if (!data)
        return Error::fail("No Data folder in " + install.string());
    std::vector<std::filesystem::path> archives;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(*data, ec), end; !ec && it != end; it.increment(ec))
        if (fs::toLower(it->path().extension().string()) == ".wad" && it->is_regular_file(ec))
            archives.push_back(it->path());
    std::sort(archives.begin(), archives.end());

    for (const auto &path : archives) {
        auto wad = std::make_unique<WadArchive>();
        std::filesystem::path folder = path.lexically_relative(*data);
        folder.replace_extension();
        if (Error err = wad->open(path, folder.generic_string())) {
            warnings.push_back(err.message);
            continue;
        }
        set.add(std::move(wad), 0);
    }

    // The loose files, above the archives; the archives themselves among
    // them, which is harmless.
    auto loose = std::make_unique<DirectoryArchive>();
    if (Error err = loose->open(*data))
        return err;
    set.add(std::move(loose), 1);
    return Error::success();
}

} // namespace reluva::tpw
