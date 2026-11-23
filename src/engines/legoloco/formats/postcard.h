// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::legoloco {

/** A postcard file's size: the game's card record, saved as it is in memory. */
constexpr size_t kPostcardSize = 920;

/** A sticker on a postcard: a clip art picture, centred on its place. */
struct PostcardSticker {
    uint8_t set = 0;    // its clip art category and digit, packed (clipArtName)
    uint8_t number = 0; // in its set, from 1
    int x = 0;          // the centre, in pixels on the card
    int y = 0;
    int width = 0;
    int height = 0;
};

/**
 * A postcard (`.crd`): who it's to and from, its message, stamp and
 * background, and its stickers (docs/engines/legoloco/formats/crd.md).
 */
struct Postcard {
    uint32_t number = 0; // as its file is named, 0910000 for 0910000.crd
    std::string to;      // the addressee's name; "Anyone" unless sent to a player
    std::string from;
    std::string message;
    uint8_t stamp = 0;         // clip art S0nnn; 0 for none
    uint8_t background = 0;    // clip art Rdnnn's number; 0 for none
    uint8_t backgroundSet = 0; // Rdnnn's digit, plus one
    std::vector<PostcardSticker> stickers;
    // Fields whose use isn't known, kept as they are: the number at +0x04
    // (set from the player's profile when a card is made), +0x36 (cleared
    // when a card's attachment is let go), +0x38 and +0x3c's three bytes.
    uint32_t field04 = 0;
    uint16_t field36 = 0;
    uint32_t field38 = 0;
    uint8_t field3c[3] = {};
};

/** Reads a postcard file. */
Error parsePostcard(const std::vector<uint8_t> &data, Postcard &out);

/**
 * The clip art file a sticker's set and number name (`d1004.bmp`), in
 * art-res's `Clipart` folder or the resource file's `CLIPART`, as the game
 * spells it (the files' own case varies).
 */
std::string clipArtName(uint8_t set, uint8_t number);

/** The set a stamp's and a background's numbers belong to (`category` 31 and 30). */
uint8_t clipArtSet(int category, int digit);

} // namespace reluva::legoloco
