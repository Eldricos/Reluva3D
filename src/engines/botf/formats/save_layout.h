// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/bytes.h"
#include "common/text.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// Where a saved game's files keep what Reluva reads and writes of them, for
// save_game.cpp and save_writer.cpp (docs/engines/botf/formats/sav.md).

namespace reluva::botf::saved {

// result.lst's chunks' headers, and its results' headers.
constexpr size_t kChunkHeaderSize = 12;
constexpr size_t kResultHeaderSize = 40;
constexpr size_t kOutpostSize = 92;
constexpr size_t kRaceSize = 64;
constexpr size_t kBuildingSize = 8;
// treaty's: a count and the next treaty's number, then each treaty.
constexpr size_t kTreatyHeaderSize = 8;
constexpr size_t kTreatySize = 0x74;

// gameInfo's fields used here, as the Ultimate Editor's GameInfo.java
// gives them.
constexpr size_t kDescriptionAt = 0x0D;
constexpr size_t kDescriptionSize = 51;
constexpr size_t kTurnAt = 0x40;
constexpr size_t kMultiplayerAt = 0x4C;
constexpr size_t kUsed = kMultiplayerAt + 4;
constexpr size_t kEmpireAt = 0x252;

// The saved game's text files are compressed with the window full of
// spaces; gameInfo is one of them.
constexpr uint8_t kTextWindowFill = 0x20;

inline std::string field(const std::vector<uint8_t> &data, size_t at, size_t size) {
    size_t len = 0;
    while (len < size && data[at + len])
        ++len;
    return text::windows1252ToUtf8(std::string_view(reinterpret_cast<const char *>(&data[at]), len));
}

inline int16_t s16(const std::vector<uint8_t> &d, size_t at) {
    return int16_t(readLE16(&d[at]));
}

inline int32_t s32(const std::vector<uint8_t> &d, size_t at) {
    return int32_t(readLE32(&d[at]));
}

inline double readDouble(const std::vector<uint8_t> &d, size_t at) {
    const uint64_t bits = readLE32(&d[at]) | uint64_t(readLE32(&d[at + 4])) << 32;
    double v = 0;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

// galInfo: the galaxy's size (0x08), and how many systems (0x0C), stellar
// objects (0x0E) and sectors (0x14) the other files hold.
constexpr size_t kGalaxySizeAt = 0x08;
constexpr size_t kSystemCountAt = 0x0C;
constexpr size_t kStellarCountAt = 0x0E;
constexpr size_t kSectorCountAt = 0x14;
constexpr size_t kGalInfoSize = 48;

// sector.lst: a 104-byte entry a sector, row by row, each starting with its
// row and column.
constexpr size_t kSectorSize = 104;

// systInfo: a system's fixed part, then its planets (76 bytes each), a word
// for each planet, its specials (64 bytes each), a word for each trade
// route, and, for the player's, the production queue.
constexpr size_t kSystemSize = 0x328;
constexpr size_t kPlanetSize = 76;
constexpr size_t kSpecialSize = 64;
constexpr size_t kProductionQueueSize = 108;

// stellInfo: an 88-byte entry an object.
constexpr size_t kStellarSize = 88;

// GWTForce: a 108-byte entry a task force.
constexpr size_t kTaskForceSize = 108;

// GShipList: a 116-byte entry a ship.
constexpr size_t kShipSize = 116;

// The races techinfo has room for: the five empires and 30 minor races.
constexpr size_t kRaceCount = 35;

} // namespace reluva::botf::saved
