// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva {

// A Wise installer, as games' patches came (Elite Force's
// eliteforcepatch1_2.exe): a small Windows program, then the files it
// installs, each a raw deflate stream followed by its CRC-32, end to end;
// the first two its own (its script and screens). No names are kept with
// them; a file is told by its contents.
struct WiseStream {
    uint64_t offset = 0;     // of the deflate stream in the installer
    uint64_t packedSize = 0; // the stream's bytes
    uint64_t size = 0;       // the file's
    uint32_t crc = 0;
    std::string headMd5; // of its first kDetectionHashBytes bytes, as detection's signatures
};

// The installer's files, in order; fails if `file` isn't a Wise installer.
Error readWiseInstaller(const std::filesystem::path& file, std::vector<WiseStream>& out);
// One of them unpacked, its CRC checked.
Error extractWiseStream(const std::filesystem::path& file, const WiseStream& stream, std::vector<uint8_t>& out);

} // namespace reluva
