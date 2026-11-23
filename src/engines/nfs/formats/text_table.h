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
    /** Where and how NFS II SE's front end draws a string: its record in the table (text.md). */
    struct Place {
        int fontSize = 0; // 13, 14, 16, 18 or 36 picks a font (nfs2sen.exe 0x4056e0); 24 has none
        // On the 640x480 screen: x at the left, right or middle of the
        // string, by `style`; y the top of its line.
        int x = 0;
        int y = 0;
        // Colours, 0-5, of a table in the program (0x4bfd9c): the string's,
        // and as it's picked (inferred).
        uint8_t colour = 0;
        uint8_t pickedColour = 0;
        // 0 left, 1 right, 2 centred; 3 to 5 the same over a shadow
        // (0x405540).
        uint8_t style = 0;
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
