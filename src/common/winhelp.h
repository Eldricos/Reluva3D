// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Windows Help files (.hlp): their topics' titles and text, from the
// format's public description (Manfred Winterhoff's helpfile.txt, which
// documents helpdeco). Pictures, hot spots and fonts are skipped. See
// docs/formats/winhelp.md.

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::winhelp {

struct Topic {
    std::string title;
    // Paragraphs separated by blank lines; a table's cells by tabs, its
    // rows by blank lines. Windows' quotes and dashes as plain ASCII.
    std::string text;
};

Error readHelp(const std::vector<uint8_t>& file, std::vector<Topic>& out);

// The pieces, for tests: the help compiler's LZ77, and its numbers that
// take one or two bytes (two or four for the longs) as their lowest bit says.
std::vector<uint8_t> decompressLz77(const uint8_t* data, size_t size);

} // namespace reluva::winhelp
