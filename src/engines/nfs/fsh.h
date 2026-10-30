// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/image.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::nfs {

// Shape record types: the first byte of each block (docs/formats/nfs/fsh.md).
enum ShapeType : uint8_t {
    kShape8Bit = 0x7b,     // palette indices
    kShape16Bit565 = 0x78, // 5:6:5
    kShape16Bit1555 = 0x7e, // alpha bit, 5:5:5
    kShape24Bit = 0x7f,    // B, G, R
    kShape32Bit = 0x7d,    // B, G, R, A
    kPalette24Bit = 0x24,
    kPalette16Bit1555 = 0x2d,
    kPalette32Bit = 0x2a,
    kShapeText = 0x6f,
    kGauges = 0x7c, // a cockpit's gauges (Cockpit)
};

// One picture of an .fsh bank, with the blocks attached to it.
struct Shape {
    std::string name; // 4 characters in the bank's directory
    uint8_t type = 0;
    int width = 0, height = 0;
    int centerX = 0, centerY = 0; // the picture's centre or hot spot (may be negative)
    int x = 0, y = 0;             // where it's placed on screen (may be negative)
    std::vector<uint8_t> pixels;  // as stored: width * height pixels, rows top to bottom
    std::vector<uint8_t> palette; // for 8-bit shapes: RGBA per colour
    std::vector<std::string> texts; // attached text (render hints such as "<additive>")
    struct Block {
        uint8_t type = 0;
        std::vector<uint8_t> data; // after the type and next-block bytes
    };
    std::vector<Block> unknown; // attached blocks not understood yet
};

// An .fsh file ("SHPI"): a directory of named shapes.
struct ShapeBank {
    std::string id; // the directory's id, "GIMX" in NFS III
    std::vector<Shape> shapes;
};

// Reads an .fsh file, or a .qfs (RefPack-compressed .fsh).
Error parseShapeBank(std::vector<uint8_t> data, ShapeBank& out);

// A shape as RGBA. 8-bit shapes without a palette fail.
Error decodeShape(const Shape& shape, image::Image& out);

// A cockpit picture's gauges, from its 0x7C block (fsh.md, Gauges): in
// the 640x480 frame of dash.qfs's picture 0000.
struct Gauge {
    float centerX = 0, centerY = 0;
    float hub = 0;     // the needle starts this far from the centre
    bool kmh = false;  // the speedometer's units: km/h, or mph
    float low = 0, high = 0; // the readings at p0 and p1 (rpm in thousands)
    float p0x = 0, p0y = 0, p1x = 0, p1y = 0;
    // A gauge whose two points are the same isn't drawn.
    bool shown() const { return p0x != p1x || p0y != p1y; }
};
struct Cockpit {
    int colorX = 0, colorY = 0; // a pixel of the picture in the needles' colour
    float width = 0;            // a needle's width at its base; the tip is half
    Gauge speedometer, tachometer;
};
// False when the shape has no gauges block, or a short one.
bool readCockpit(const Shape& shape, Cockpit& out);

// A needle's corners at a reading, as the game finds them (fsh.md): base
// left, base right, tip right, tip left. It points from the centre toward
// p0 at the low reading and p1 at the high, turning clockwise on screen
// (anticlockwise when low isn't below high), as long as the mean of their
// distances. Readings past the range turn on past p0 or p1; the game
// doesn't clamp them.
struct Needle {
    float x[4] = {}, y[4] = {};
};
Needle needle(const Gauge& gauge, float width, float reading);

// How far the steering wheel (picture 0001) turns, in turns clockwise on
// screen, for the car's steering (-128 to 127) at a speed in metres a
// second: a quarter turn at full lock standing still, less as the car goes
// faster, none at about 400 mph (fsh.md).
float wheelTurns(int steering, float speed);
// A speed in metres a second, from a speedometer's reading in its units.
float metresPerSecond(const Gauge& speedometer, float reading);

// The gear indicator's pictures by the car's gear: reverse, neutral, then
// first to sixth.
constexpr const char* kGearPictures[] = {"geaR", "geaN", "gea1", "gea2", "gea3", "gea4", "gea5", "gea6"};

// "8-bit", "16-bit 1:5:5:5"... for messages and viewers.
const char* shapeTypeName(uint8_t type);

} // namespace reluva::nfs
