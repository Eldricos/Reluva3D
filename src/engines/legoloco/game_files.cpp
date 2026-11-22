// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoloco/game_files.h"

#include "common/fs.h"
#include "common/pe.h"

#include <fstream>
#include <iterator>

namespace reluva::legoloco {

Error readResourceNames(const std::vector<uint8_t> &exe, std::map<uint32_t, std::string> &out) {
    out.clear();
    PeImage image;
    if (Error err = parsePe(exe, image))
        return err;
    std::map<uint32_t, std::string> strings;
    if (Error err = readPeStrings(exe, image, strings))
        return err;
    // The table holds the interface's words and the minifigures' names too;
    // a resource's name is always a path.
    for (auto &[number, text] : strings)
        if (text.find('\\') != std::string::npos)
            out.emplace(number, std::move(text));
    return Error::success();
}

Error GameFiles::open(const std::filesystem::path &root) {
    std::filesystem::path base = root;
    if (const auto artRes = fs::findDirectory(root, "art-res")) {
        _artRes = *artRes;
    } else if (fs::findFile(root, "resource.rfh")) {
        _artRes = root;
        base = root.parent_path();
    } else {
        return Error::fail("no art-res in " + root.string());
    }
    if (Error err = _resources.open(_artRes))
        return err;
    const auto exe = fs::findFile(base, "Exe/Loco.exe");
    if (!exe)
        return Error::fail("no Exe/Loco.exe in " + base.string());
    std::ifstream in(*exe, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (Error err = readResourceNames(bytes, _names))
        return Error::fail(exe->string() + ": " + err.message);
    return Error::success();
}

int GameFiles::findResource(uint32_t number, const char *extension) const {
    const auto it = _names.find(number);
    if (it == _names.end())
        return -1;
    return _resources.find(it->second + "." + extension);
}

std::optional<std::filesystem::path> GameFiles::findFile(const std::string &relative) const {
    return fs::findFile(_artRes, relative);
}

std::optional<std::filesystem::path> GameFiles::findDirectory(const std::string &relative) const {
    return fs::findDirectory(_artRes, relative);
}

} // namespace reluva::legoloco
