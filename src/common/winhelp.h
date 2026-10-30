// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Windows Help files (.hlp): their topics' titles, text, links and
// pictures, from the format's public description (Manfred Winterhoff's
// helpfile.txt, which documents helpdeco). Fonts, macros and pictures'
// hot spots are skipped. See docs/formats/winhelp.md.

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::winhelp {

// In a topic's text, where a picture stands and where a link's words are.
constexpr char kPictureMark = '\x01'; // kPictureMark, the picture's number (|bm<n>), kPictureMark
constexpr char kLinkMark = '\x02';    // kLinkMark, the topic's index, kLinkMark, its words, kLinkEnd
constexpr char kLinkEnd = '\x03';

struct Topic {
    std::string title;
    // Paragraphs separated by blank lines; a table's cells by tabs, its
    // rows by blank lines. Windows' quotes and dashes as plain ASCII. With
    // the marks above for pictures and links (plainText() leaves them out).
    std::string text;
};
std::string plainText(const std::string& text);

Error readHelp(const std::vector<uint8_t>& file, std::vector<Topic>& out);

// A picture (|bm<n>: the help compiler's own form of a bitmap), as RGBA.
struct Picture {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
Error readPicture(const std::vector<uint8_t>& file, int number, Picture& out);

// The pieces, for tests: the help compiler's LZ77, and its numbers that
// take one or two bytes (two or four for the longs) as their lowest bit says.
std::vector<uint8_t> decompressLz77(const uint8_t* data, size_t size);
// Its run-length packing: a byte with the top bit set, that many (less the
// bit) bytes as they are; else the next byte that many times.
std::vector<uint8_t> unpackRuns(const uint8_t* data, size_t size);

} // namespace reluva::winhelp
