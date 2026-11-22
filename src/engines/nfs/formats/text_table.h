// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::nfs {

/**
 * A front-end string table (FeData/text/text.eng and its other
 * languages), as docs/engines/nfs/formats/text.md describes.
 * Strings are kept as stored, in Windows-1252, the code page the game's
 * fonts are drawn in.
 */
struct TextTable {
    /** Where NFS II SE's front end puts a string: its record in the table (text.md). */
    struct Place {
        int fontSize = 0;                 // 16, 18, 24 or 36: its swiss*.ffn fonts' sizes
        int x = 0;                        // on the 640x480 screen
        int y = 0;                        //
        std::array<uint8_t, 3> unknown{}; // bytes 1-3, each 0-5: not worked out
    };

    std::vector<std::string> strings;
    std::vector<Place> places; // NFS II SE's front-end tables, one per string; empty for the others
};

/**
 * Reads a table, either layout (text.md): NFS III's and NFS II SE's
 * textsim.*, offsets alone; NFS II SE's front end's, records with each
 * string's place. Fails if an offset points outside the file.
 */
Error parseTextTable(const std::vector<uint8_t> &data, TextTable &out);

/** Windows-1252 text as UTF-8 (the five unassigned bytes become U+FFFD). */
std::string cp1252ToUtf8(std::string_view text);

} // namespace reluva::nfs
