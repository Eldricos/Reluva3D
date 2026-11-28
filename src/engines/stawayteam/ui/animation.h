// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "image/image.h"

#include <array>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/** A sequence's frames, as pictures with their origins, and its speed. */
struct Animation {
    struct Picture {
        image::Image image;
        int originX = 0;
        int originY = 0;
    };
    std::vector<Picture> frames; // a direction's after another's
    float fps = 0.0f;
    // Drawn on the ground, its part's Rendering Height not lifting it, or
    // as a shadow, the height casting it down and left (0x491444).
    bool onGround = false;
    bool shadow = false;
    // Its frames cut into strips, each sorted to draw by its own place:
    // the first strip's offset added to where it stands, and each cut's x
    // from there and the offset of the strip right of it (0x5518d0,
    // 0x5512c0).
    struct Cut {
        int x = 0;
        int sortingY = 0;
    };
    int sortingY = 0;
    std::vector<Cut> cuts;
    int rotations = 1; // directions, the first facing 0 and the rest turning round evenly
    // A rotation sequence's frames' movement: a direction's first frame its
    // whole cycle's, each other how far into it that frame is (0x5511d0).
    std::vector<std::array<int32_t, 2>> moves;
    /** A noise made as a frame shows: a footstep's (its sequence's Trigger). */
    struct Noise {
        int frame = 0; // of each direction's
        float radius = 0.0f;
        float age = 0.0f;      // seconds it's heard for
        std::string attribute; // the maker's that quietens it ("Stealth"), if any
        // Its sound (a CAddFootstepAndSoundBySurfaceTypeAction's Default
        // Sound): none in the game's models.
        std::string sound;
    };
    std::vector<Noise> noises;

    /**
     * Reads `Cache/Sequences/<name>.seq16`, or a model's sequence `sequence`
     * from `Cache/Models/<name>.mdl16`.
     */
    Error load(const Archive &data, const std::string &name, const std::string &sequence = "");
    /** Reads the .seq16 at `path` (a cursor's, `Cache/Interface/...`). */
    Error loadFile(const Archive &data, const std::string &path);
    /**
     * The frame shown `ms` milliseconds in, the first `offset` frames passed
     * over, in direction `direction`; nullptr if it has none.
     */
    const Picture *at(uint64_t ms, int offset = 0, int direction = 0) const;
    /** Frame `frame` of direction `direction`, counted round; nullptr if it has none. */
    const Picture *frame(int frame, int direction) const;
    /** A direction's frames. */
    int perDirection() const { return frames.empty() ? 0 : int(frames.size()) / rotations; }
    /** The direction facing `radians` (0 the first direction's way, turning as its directions do). */
    int direction(float radians) const;
    /** How far a cycle of direction `direction` goes, in pixels; 0 if it doesn't move. */
    float cycleDistance(int direction) const;
    /**
     * The way direction `direction`'s cycle goes, made 1 long, into `x` and
     * `y`; false if it doesn't move.
     */
    bool cycleWay(int direction, float &x, float &y) const;
    /**
     * Where in direction `direction`'s cycle (in frames, 0 to its count)
     * one is `distance` pixels into it (0 to cycleDistance()): the frames'
     * movement read backwards, interpolated between frames.
     */
    float frameAt(int direction, float distance) const;
    /**
     * Walks `seconds` on in direction `direction`, its frames shown at
     * `rate` a second, `travelled` (pixels into the cycle) moved on: the
     * frames' own movement between the old place in the cycle and the new
     * (0x5512e0, 0x44a044), but no further than `limit`. Returns how far
     * it went, `used` set to the seconds that took; -1 for a sequence that
     * doesn't move.
     */
    float walk(int direction, float &travelled, float seconds, float rate, float limit, float &used) const;

private:
    // How far into the cycle frame `frame` (0 to per, fractions between)
    // is, the frames' movement interpolated (0x5511d0).
    float distanceAt(int direction, float frame) const;
    Error loadFrom(const Archive &data, const std::string &path, const std::string &sequence);
};

/** `Cache/<name>.frm16` as a picture; empty if it won't read. */
image::Image cachePicture(const Archive &data, const std::string &name);

/**
 * Draws `p` by its origin at (x, y), its colours scaled by `brightness`
 * (0-1), showing `opacity` (0-255) of it; nothing for nullptr.
 */
void blitBy(Surface &screen, const Animation::Picture *p, int x, int y, float brightness = 1.0f, int opacity = 255,
            int clipLeft = INT_MIN, int clipRight = INT_MAX);

/**
 * `p` under a warp effect (CSimpleWarpEffect, 0x563f80): each pixel it
 * shows taken `amount` (0-1) of the way to `noise`'s, the noise laid from
 * its origin over and over; all noise at 1 (0x564ee0).
 */
Animation::Picture warped(const Animation::Picture &p, const image::Image &noise, float amount);

/**
 * How far a warp's gone to its effect after `seconds` of `time`: all of it
 * at first, held a quarter of the time with Fade In, then less and less
 * to none (0x583a00).
 */
float warpAmount(float seconds, float time, bool fadeIn);

/**
 * How much of a part's shown after `seconds` of a warp of `time`: with
 * Fade In, coming in with Warp In, going out without, held a quarter of
 * the time first, and left all there or gone once it's done; all of it
 * without Fade In (0x583a00, 0x583ad0).
 */
float warpShown(float seconds, float time, bool fadeIn, bool warpIn);

} // namespace reluva::stawayteam
