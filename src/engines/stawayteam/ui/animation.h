// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "image/image.h"

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
    std::vector<Picture> frames;
    float fps = 0.0f;

    /**
     * Reads `Cache/Sequences/<name>.seq16`, or a model's sequence `sequence`
     * from `Cache/Models/<name>.mdl16`, its first direction's frames only.
     */
    Error load(const Archive &data, const std::string &name, const std::string &sequence = "");
    /** The frame shown `ms` milliseconds in, the first `offset` frames passed over; nullptr if it has none. */
    const Picture *at(uint64_t ms, int offset = 0) const;
};

/** `Cache/<name>.frm16` as a picture; empty if it won't read. */
image::Image cachePicture(const Archive &data, const std::string &name);

/** Draws `p` by its origin at (x, y), its colours scaled by `brightness` (0-1); nothing for nullptr. */
void blitBy(Surface &screen, const Animation::Picture *p, int x, int y, float brightness = 1.0f);

} // namespace reluva::stawayteam
