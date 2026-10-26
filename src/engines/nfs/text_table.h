// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::nfs {

// A front-end string table (FeData/text/text.eng and its other
// languages), as docs/formats/nfs/text.md describes. Strings are kept as
// stored, in Windows-1252, the code page the game's fonts are drawn in.
struct TextTable {
    std::vector<std::string> strings;
};

Error parseTextTable(const std::vector<uint8_t>& data, TextTable& out);

// Windows-1252 text as UTF-8 (the five unassigned bytes become U+FFFD).
std::string cp1252ToUtf8(std::string_view text);

} // namespace reluva::nfs
