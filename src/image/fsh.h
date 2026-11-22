// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "image/image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::image {

/**
 * Shape record types: the first byte of each block
 * (docs/common/image/fsh.md).
 */
enum ShapeType : uint8_t {
    kShape8Bit = 0x7b,      // palette indices
    kShape16Bit565 = 0x78,  // 5:6:5
    kShape16Bit1555 = 0x7e, // alpha bit, 5:5:5
    kShape24Bit = 0x7f,     // B, G, R
    kShape32Bit = 0x7d,     // B, G, R, A
    kPalette24Bit = 0x24,
    kPalette16Bit1555 = 0x2d,
    kPalette32Bit = 0x2a,
    kShapeText = 0x6f,
    kShapeName = 0x70, // the picture's whole name
};

/** Set in a picture's type when its pixels are compressed with RefPack. */
constexpr uint8_t kShapeCompressed = 0x80;

/** One picture of an .fsh bank, with the blocks attached to it. */
struct Shape {
    std::string name;     // 4 characters in the bank's directory
    std::string longName; // its whole name, from a name block; empty without one
    uint8_t type = 0;     // kShapeCompressed taken off
    bool compressed = false;
    int width = 0;
    int height = 0;
    // the picture's centre or hot spot (may be negative)
    int centerX = 0;
    int centerY = 0;
    // where it's placed on screen (may be negative)
    int x = 0;
    int y = 0;
    std::vector<uint8_t> pixels;    // as stored: width * height pixels, rows top to bottom
    std::vector<uint8_t> palette;   // for 8-bit shapes: RGBA per colour
    std::vector<std::string> texts; // attached text (render hints such as "<additive>")
    /** An attached block Reluva doesn't read into fields. */
    struct Block {
        uint8_t type = 0;
        std::vector<uint8_t> data; // after the type and next-block bytes
    };
    std::vector<Block> unknown; // attached blocks not understood yet
};

/** An .fsh file ("SHPI"): a directory of named shapes. */
struct ShapeBank {
    std::string id; // the directory's id: "GIMX" in NFS III, "G231" in SimCoaster
    std::vector<Shape> shapes;
};

/** Reads an .fsh file, or a .qfs (RefPack- or EA Huffman-compressed .fsh). */
Error parseShapeBank(std::vector<uint8_t> data, ShapeBank &out);

/**
 * A shape as RGBA. 8-bit shapes without a palette fail; pixels whose index
 * is past the palette's end are clear.
 */
Error decodeShape(const Shape &shape, image::Image &out);
/** "8-bit", "16-bit 1:5:5:5"... for messages and viewers; "unknown" for others. */
const char *shapeTypeName(uint8_t type);

} // namespace reluva::image
