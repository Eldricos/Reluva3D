// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/system_fonts.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace reluva::ttf {

namespace {

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::filesystem::path fromEnvironment(const char *name) {
    const char *v = std::getenv(name);
    return v && *v ? std::filesystem::path(v) : std::filesystem::path();
}

} // namespace

std::vector<std::filesystem::path> systemFontFolders() {
    std::vector<std::filesystem::path> folders;
    auto add = [&](const std::filesystem::path &base, const char *rest) {
        if (!base.empty())
            folders.push_back(base / rest);
    };
#if defined(_WIN32)
    add(fromEnvironment("WINDIR"), "Fonts");
    add(fromEnvironment("LOCALAPPDATA"), "Microsoft/Windows/Fonts");
#elif defined(__APPLE__)
    folders.emplace_back("/System/Library/Fonts/Supplemental");
    folders.emplace_back("/Library/Fonts");
    add(fromEnvironment("HOME"), "Library/Fonts");
#else
    folders.emplace_back("/usr/share/fonts");
    folders.emplace_back("/usr/local/share/fonts");
    add(fromEnvironment("XDG_DATA_HOME"), "fonts");
    add(fromEnvironment("HOME"), ".local/share/fonts");
    add(fromEnvironment("HOME"), ".fonts");
#endif
    return folders;
}

std::vector<uint8_t> findFont(const std::vector<std::filesystem::path> &folders,
                              const std::vector<std::string> &names) {
    for (const std::filesystem::path &folder : folders) {
        std::error_code ec;
        auto it = std::filesystem::recursive_directory_iterator(
            folder, std::filesystem::directory_options::skip_permission_denied, ec);
        for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            // A few levels: the font packages' own folders, not the whole disk.
            if (it.depth() > 3)
                it.disable_recursion_pending();
            if (!it->is_regular_file(ec))
                continue;
            const std::string name = lower(it->path().filename().string());
            for (const std::string &wanted : names)
                if (name == lower(wanted)) {
                    std::ifstream in(it->path(), std::ios::binary);
                    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    if (!data.empty())
                        return data;
                }
        }
    }
    return {};
}

std::vector<uint8_t> installedArial(bool bold) {
    if (bold)
        return findFont(systemFontFolders(), {"arialbd.ttf", "Arial Bold.ttf", "Arial_Bold.ttf"});
    return findFont(systemFontFolders(), {"arial.ttf"});
}

} // namespace reluva::ttf
