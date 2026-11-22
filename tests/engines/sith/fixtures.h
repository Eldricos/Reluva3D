// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Small stand-ins for Jedi Knight's install and CDs, for Sith's tests: the
// files' names and places as the game has them, none of its data.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace reluva::sith::testing {

namespace stdfs = std::filesystem;

/** Writes `bytes` to `path`, making its folders. */
inline void writeBytes(const stdfs::path &path, const std::vector<uint8_t> &bytes) {
    stdfs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

/** An empty file at `path`. */
inline void touch(const stdfs::path &path) {
    writeBytes(path, {});
}

/** A .gob archive holding `files` (name, contents), in that order. */
inline std::vector<uint8_t> makeGob(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files) {
    constexpr size_t kHeader = 12;
    constexpr size_t kEntry = 136;
    auto put32 = [](std::vector<uint8_t> &out, size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out[at + i] = uint8_t(v >> (8 * i));
    };
    std::vector<uint8_t> out(kHeader + 4 + files.size() * kEntry);
    std::copy_n("GOB ", 4, out.begin());
    put32(out, 4, 0x14);
    put32(out, 8, kHeader);
    put32(out, kHeader, uint32_t(files.size()));
    for (size_t i = 0; i < files.size(); ++i) {
        const size_t entry = kHeader + 4 + i * kEntry;
        put32(out, entry, uint32_t(out.size()));
        put32(out, entry + 4, uint32_t(files[i].second.size()));
        std::copy(files[i].first.begin(), files[i].first.end(), out.begin() + std::ptrdiff_t(entry + 8));
        out.insert(out.end(), files[i].second.begin(), files[i].second.end());
    }
    return out;
}

/** An install's files, the names in the case the installer gives them. */
inline void makeInstall(const stdfs::path &dir) {
    touch(dir / "JK.EXE");
    touch(dir / "Resource/Res2.gob");
    touch(dir / "Resource/Res1hi.gob");
    touch(dir / "Episode/JK1.GOB");
}

/** CD `number` (1 or 2), with its marker file and a cut scene. */
inline void makeCd(const stdfs::path &dir, int number) {
    writeBytes(dir / "GAMEDATA/RESOURCE/JK_.CD", number == 1 ? std::vector<uint8_t>{0xc4, 0x32, 0x92, 0x69}
                                                             : std::vector<uint8_t>{0x84, 0x33, 0x92, 0x69});
    touch(dir / "GAMEDATA/RESOURCE/VIDEO" / (number == 1 ? "01-02A.SMK" : "23A.SMK"));
}

} // namespace reluva::sith::testing
