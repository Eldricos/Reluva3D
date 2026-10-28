// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/filesystem.h"

#include "common/fs.h"
#include "engines/age/ar_archive.h"

#include <algorithm>
#include <memory>

namespace reluva::age {

Error mountGame(SearchSet& set, const std::filesystem::path& install, std::vector<std::string>& warnings) {
    std::vector<std::filesystem::path> archives;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(install, ec), end; !ec && it != end; it.increment(ec))
        if (fs::toLower(it->path().extension().string()) == ".ar" && it->is_regular_file(ec))
            archives.push_back(it->path());
    if (archives.empty())
        return Error::fail("No .ar archives in " + install.string());
    std::sort(archives.begin(), archives.end(), [](const auto& a, const auto& b) {
        return fs::toLower(a.filename().string()) < fs::toLower(b.filename().string());
    });

    int priority = 0;
    for (const auto& p : archives) {
        auto ar = std::make_unique<ArArchive>();
        if (Error err = ar->open(p)) {
            warnings.push_back(err.message);
            continue;
        }
        set.add(std::move(ar), ++priority);
    }

    // The loose files, above the archives. They include the archives
    // themselves and the program's files, which is harmless.
    auto loose = std::make_unique<DirectoryArchive>();
    if (Error err = loose->open(install))
        return err;
    set.add(std::move(loose), ++priority);
    return Error::success();
}

} // namespace reluva::age
