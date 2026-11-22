// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/ldl.h"

#include "common/md5.h"
#include "common/pe.h"

#include <fstream>
#include <iterator>

namespace reluva::storm3d {

namespace {

// The key's address in the English 1.0 Armada.exe (in .data), and its MD5:
// the key itself is the game's and stays in the player's copy.
constexpr uint64_t kKeyAddress = 0x604548;
constexpr const char *kKeyMd5 = "a7a514b8967277ed3bbe9be5eb3cda6b";

} // namespace

Error readLdlKey(const std::filesystem::path &exe, std::vector<uint8_t> &key) {
    const std::string where = exe.string();
    std::ifstream in(exe, std::ios::binary);
    if (!in)
        return Error::fail("Cannot open " + where);
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    PeImage pe;
    if (Error err = parsePe(file, pe))
        return Error::fail(where + ": " + err.message);
    const uint64_t rva = kKeyAddress - pe.imageBase;
    for (const PeImage::Section &s : pe.sections) {
        if (rva < s.virtualAddress || rva + kLdlKeySize > uint64_t(s.virtualAddress) + s.rawSize)
            continue;
        const uint64_t offset = s.rawOffset + (rva - s.virtualAddress);
        if (offset + kLdlKeySize > file.size())
            break;
        key.assign(file.begin() + std::ptrdiff_t(offset), file.begin() + std::ptrdiff_t(offset + kLdlKeySize));
        Md5 md5;
        md5.update(key.data(), key.size());
        if (md5.hexDigest() == kKeyMd5)
            return Error::success();
        break;
    }
    key.clear();
    return Error::fail(where + " doesn't have the .ldl files' key where the English 1.0 Armada.exe has it");
}

void decodeLdl(std::vector<uint8_t> &data, const std::vector<uint8_t> &key) {
    if (key.empty())
        return;
    for (size_t i = 0; i < data.size(); ++i)
        data[i] ^= key[i % key.size()];
}

} // namespace reluva::storm3d
