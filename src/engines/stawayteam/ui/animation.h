// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "image/image.h"

#include <array>
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
    int rotations = 1; // directions, the first facing 0 and the rest turning round evenly
    // A rotation sequence's frames' movement: a direction's first frame its
    // whole cycle's, each other how far into it that frame is (inferred).
    std::vector<std::array<int32_t, 2>> moves;

    /**
     * Reads `Cache/Sequences/<name>.seq16`, or a model's sequence `sequence`
     * from `Cache/Models/<name>.mdl16`.
     */
    Error load(const Archive &data, const std::string &name, const std::string &sequence = "");
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
};

/** `Cache/<name>.frm16` as a picture; empty if it won't read. */
image::Image cachePicture(const Archive &data, const std::string &name);

/**
 * Draws `p` by its origin at (x, y), its colours scaled by `brightness`
 * (0-1), showing `opacity` (0-255) of it; nothing for nullptr.
 */
void blitBy(Surface &screen, const Animation::Picture *p, int x, int y, float brightness = 1.0f, int opacity = 255);

} // namespace reluva::stawayteam
