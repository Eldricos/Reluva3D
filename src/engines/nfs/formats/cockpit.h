// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "image/fsh.h"
#include "image/image.h"

namespace reluva::nfs {

// NFS III's code names the shared picture reader's types and functions
// as its own.
using image::decodeShape;
using image::parseShapeBank;
using image::Shape;
using image::ShapeBank;
using image::shapeTypeName;
using enum image::ShapeType;

/** The attached block holding a cockpit picture's gauges (Cockpit). */
constexpr uint8_t kGauges = 0x7c;

/**
 * A picture of sfx.fsh as nfs3.exe hands it to a Glide card (0x4d4080 ->
 * 0x4d3f50 -> 0x4d37a0), decoded: a 32-bit shape goes up at 4:4:4:4, each
 * channel's top four bits (0x4d27e0), which the card widens back by 17,
 * when the loader asks for that format (`fourFour`: glw0-glw4, 0x4d5370)
 * or, asked for none (shad, the particles' SPnn and SNnn), when its alpha
 * is neither all 255 nor only 0 and 255 (0x4d1950 gives 2). Other shapes
 * are as decodeShape gives them.
 */
Error decodeForGlide(const image::Shape &shape, image::Image &out, bool fourFour = false);

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
bool readCockpit(const image::Shape &shape, Cockpit &out);

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

} // namespace reluva::nfs
