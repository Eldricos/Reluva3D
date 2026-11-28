// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "image/image.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::nfs {

/**
 * Shape record types: the first byte of each block
 * (docs/engines/nfs/formats/fsh.md).
 */
enum ShapeType : uint8_t {
    kShape4Bit = 0x7a,      // palette indices, two to a byte (NFS II SE)
    kShape8Bit = 0x7b,      // palette indices
    kShape16Bit565 = 0x78,  // 5:6:5
    kShape16Bit1555 = 0x7e, // alpha bit, 5:5:5
    kShape24Bit = 0x7f,     // B, G, R
    kShape32Bit = 0x7d,     // B, G, R, A
    kPalette24Bit = 0x24,   // R, G, B (NFS II SE)
    kPalette16Bit1555 = 0x2d,
    kPalette16Bit565 = 0x29, // NFS II SE
    kPalette32Bit = 0x2a,
    kShapeText = 0x6f,
    kGauges = 0x7c, // a cockpit's gauges (Cockpit)
};

/** One picture of an .fsh bank, with the blocks attached to it. */
struct Shape {
    std::string name; // 4 characters in the bank's directory
    uint8_t type = 0;
    int width = 0;
    int height = 0;
    // the picture's centre or hot spot (may be negative)
    int centerX = 0;
    int centerY = 0;
    // where it's placed on screen (may be negative)
    int x = 0;
    int y = 0;
    std::vector<uint8_t> pixels;    // as stored: width * height pixels, rows top to bottom
    std::vector<uint8_t> palette;   // for 4- and 8-bit shapes, and a palette entry: RGBA per colour
    bool bankPalette = false;       // `palette` is the bank's palette entry's, not its own
    std::vector<std::string> texts; // attached text (render hints such as "<additive>")
    /** An attached block Reluva doesn't read into fields. */
    struct Block {
        uint8_t type = 0;
        std::vector<uint8_t> data; // after the type and next-block bytes
    };
    std::vector<Block> unknown; // attached blocks not understood yet
};

/**
 * An .fsh file ("SHPI"): a directory of named shapes. An entry may be a
 * palette alone (NFS II SE's "!pal" or "0000"): its type is the palette's,
 * its width the number of colours, and it has no pixels.
 */
struct ShapeBank {
    std::string id; // the directory's id, "GIMX" in NFS III
    std::vector<Shape> shapes;
};

/**
 * Reads an .fsh file, or a .qfs (RefPack- or EA Huffman-compressed .fsh).
 * A 4- or 8-bit picture without a palette of its own takes the bank's
 * palette entry's, if it has one (bankPalette).
 */
Error parseShapeBank(std::vector<uint8_t> data, ShapeBank &out);

/**
 * A shape as RGBA; a palette entry as a row of its colours. 4- and 8-bit
 * shapes without a palette fail; pixels whose index is past the palette's
 * end are clear.
 */
Error decodeShape(const Shape &shape, image::Image &out);
/**
 * A picture of sfx.fsh as nfs3.exe hands it to a Glide card (0x4d4080 ->
 * 0x4d3f50 -> 0x4d37a0), decoded: a 32-bit shape goes up at 4:4:4:4, each
 * channel's top four bits (0x4d27e0), which the card widens back by 17,
 * when the loader asks for that format (`fourFour`: glw0-glw4, 0x4d5370)
 * or, asked for none (shad, the particles' SPnn and SNnn), when its alpha
 * is neither all 255 nor only 0 and 255 (0x4d1950 gives 2). Other shapes
 * are as decodeShape gives them.
 */
Error decodeForGlide(const Shape &shape, image::Image &out, bool fourFour = false);

/**
 * One of a cockpit picture's gauges, from its 0x7C block (fsh.md, Gauges):
 * in the 640x480 frame of dash.qfs's picture 0000.
 */
struct Gauge {
    float centerX = 0;
    float centerY = 0;
    float hub = 0;    // the needle starts this far from the centre
    bool kmh = false; // the speedometer's units: km/h, or mph
    // the readings at p0 and p1 (rpm in thousands)
    float low = 0;
    float high = 0;
    float p0x = 0;
    float p0y = 0;
    float p1x = 0;
    float p1y = 0;
    /** Whether the gauge is drawn: not when its two points are the same. */
    bool shown() const { return p0x != p1x || p0y != p1y; }
};
/** A cockpit picture's needles: their colour, width and two gauges. */
struct Cockpit {
    // a pixel of the picture in the needles' colour
    int colorX = 0;
    int colorY = 0;
    float width = 0; // a needle's width at its base; the tip is half
    Gauge speedometer;
    Gauge tachometer;
};
/** The shape's gauges; false when it has no gauges block, or a short one. */
bool readCockpit(const Shape &shape, Cockpit &out);

/** A needle's corners on screen: base left, base right, tip right, tip left. */
struct Needle {
    float x[4] = {};
    float y[4] = {};
};
/**
 * A needle at a reading, as the game finds it (fsh.md). It points from the
 * centre toward p0 at the low reading and p1 at the high, turning
 * clockwise on screen (anticlockwise when low isn't below high), as long
 * as the mean of their distances. Readings past the range turn on past p0
 * or p1; the game doesn't clamp them.
 * @param width The needle's width at its base; the tip is half as wide.
 */
Needle needle(const Gauge &gauge, float width, float reading);

/**
 * How far the steering wheel (picture 0001) turns, in turns clockwise on
 * screen, for the car's steering (-128 to 127) at a speed in metres a
 * second: a quarter turn at full lock standing still, less as the car goes
 * faster, none at about 400 mph (fsh.md).
 */
float wheelTurns(int steering, float speed);
/** A speed in metres a second, from a speedometer's reading in its units. */
float metresPerSecond(const Gauge &speedometer, float reading);

/**
 * The gear indicator's pictures by the car's gear: reverse, neutral, then
 * first to sixth.
 */
constexpr const char *kGearPictures[] = {"geaR", "geaN", "gea1", "gea2", "gea3", "gea4", "gea5", "gea6"};

/** Whether a shape type is a palette's (a block, or NFS II SE's palette entries). */
bool isPaletteType(uint8_t type);

/** "8-bit", "16-bit 1:5:5:5"... for messages and viewers; "unknown" for others. */
const char *shapeTypeName(uint8_t type);

} // namespace reluva::nfs
