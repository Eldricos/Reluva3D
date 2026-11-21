// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/system.h"
#include "engines/stawayteam/formats/binary_object.h"
#include "image/image.h"

#include <cstdint>
#include <vector>

namespace reluva::stawayteam {

/**
 * Frames played one after another, as a sequence's or a model's are, for
 * the data viewer: at their sequence's frames a second, or a frame at a
 * time when paused.
 */
class Animation {
public:
    /**
     * Frames `first` to `first + count` of a sequence, played at `fps`.
     * A frame the sequence doesn't have is shown as nothing.
     */
    void set(const SequenceData &seq, size_t first, size_t count, double fps);
    void clear() { _frames.clear(); }

    size_t size() const { return _frames.size(); }
    size_t current() const { return _current; }
    bool paused() const { return _paused; }
    void togglePause() { _paused = !_paused; }
    /** One frame on or back, and paused there. */
    void step(int by);
    /** Moves on with the time; true if the frame changed. */
    bool update(uint64_t nowMs);
    /**
     * Draws the current frame with its origin at (x, y), `zoom` times
     * larger, on `backdrop` (draw.h) over the bounds of every frame.
     */
    void draw(Surface &s, int x, int y, int zoom, int backdrop) const;
    /** The bounds of every frame about the origin: left, top, right, bottom. */
    void bounds(int &left, int &top, int &right, int &bottom) const;

private:
    struct Shown {
        image::Image image; // empty where the sequence has no frame
        int originX = 0;
        int originY = 0;
    };
    std::vector<Shown> _frames;
    size_t _current = 0;
    double _fps = 10;
    bool _paused = false;
    uint64_t _lastMs = 0;
};

} // namespace reluva::stawayteam
