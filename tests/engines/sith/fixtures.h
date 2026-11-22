// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Small stand-ins for Jedi Knight's install and CDs, for Sith's tests: the
// files' names and places as the game has them, none of its data.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
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
