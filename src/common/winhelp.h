// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Windows Help files (.hlp): their topics' titles, text, links and
// pictures, from the format's public description (Manfred Winterhoff's
// helpfile.txt, which documents helpdeco). Fonts, macros and pictures'
// hot spots are skipped. See docs/common/winhelp.md.

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::winhelp {

/** Where a picture stands in a topic's text: this mark, the picture's number (|bm<n>), this mark again. */
constexpr char kPictureMark = '\x01';
/**
 * Where a link starts in a topic's text: this mark, the index of the topic it goes to in readHelp()'s list,
 * this mark again; then its words and kLinkEnd.
 */
constexpr char kLinkMark = '\x02';
/** Where a link's words end in a topic's text. */
constexpr char kLinkEnd = '\x03';

/** One of a help file's topics: its title and its text. */
struct Topic {
    std::string title;
    // Paragraphs separated by blank lines; a table's cells by tabs, its
    // rows by blank lines. Windows' quotes and dashes as plain ASCII. With
    // the marks above for pictures and links (plainText() leaves them out).
    std::string text;
};
/** A topic's text without the marks: pictures left out, links' words kept as plain words. */
std::string plainText(const std::string &text);

/**
 * Reads a help file's topics, in the file's order.
 *
 * Help files of Windows 3.0 aren't read; a topic's non-scrolling region is left out.
 * @return An error if it isn't a help file this reads, or has no topics.
 */
Error readHelp(const std::vector<uint8_t> &file, std::vector<Topic> &out);

/** A picture (|bm<n>: the help compiler's own form of a bitmap), as RGBA. */
struct Picture {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};
/**
 * Reads picture `number` (|bm<number>) of a help file, opaque, its rows from the top.
 *
 * Only the first picture stored under that number, and only bitmaps: metafiles aren't read.
 * @return An error naming what didn't read; `out` is then empty.
 */
Error readPicture(const std::vector<uint8_t> &file, int number, Picture &out);

// The pieces, for tests.

/**
 * Unpacks the help compiler's LZ77: a flag byte, then eight items, each a byte as it is or a
 * 16-bit word of a length and a distance back.
 *
 * Data that ends early or reaches back past the start gives what was unpacked up to there.
 */
std::vector<uint8_t> decompressLz77(const uint8_t *data, size_t size);
/**
 * Unpacks the help compiler's run-length packing: a byte with the top bit set, that many (less the
 * bit) bytes as they are; else the next byte that many times. A run cut short by the end gives what's there.
 */
std::vector<uint8_t> unpackRuns(const uint8_t *data, size_t size);

} // namespace reluva::winhelp
