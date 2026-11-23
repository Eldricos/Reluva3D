// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "graphics/gfx3d.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::tpw {

/**
 * A model's animation (an `.MD2` of header flag 0x20, as SimCoaster's
 * `Data/levels/arabian/rides/wateride/waterideb.MD2` for `wateride.MD2`):
 * tracks moving the model's nodes over its frames. Its moves of the
 * whole parts are read; its moves of single vertices and of textures
 * are counted, not read. See
 * docs/engines/tpw/games/simcoaster/formats/md2-animation.md.
 */
class Animation {
public:
    /** Frames a second, as the viewer plays them: a guess, not yet set beside a recording. */
    static constexpr float kFramesPerSecond = 25;

    /** How a position track runs between its keys. */
    enum class Curve : uint8_t { kNone, kLinear, kBezier };

    /** A rotation at a frame: a unit quaternion, w, x, y, z. */
    struct RotationKey {
        uint32_t frame = 0;
        std::array<float, 4> q{1, 0, 0, 0};
    };
    /** A scale at a frame. */
    struct ScaleKey {
        uint32_t frame = 0;
        gfx::Vec3 scale{1, 1, 1};
    };

    /** What moves one of the model's nodes. */
    struct Track {
        /** What it moves: a node, by its place in Model::animationOrder() (meshes first). */
        uint32_t target = 0;
        uint32_t flags = 0; // which kinds of track it has: not all worked out
        uint32_t frames = 0;
        Curve curve = Curve::kNone;
        std::vector<uint32_t> positionFrames;
        /**
         * Linear: a position at each key. Bezier: each key, then the curve's
         * two control points to the next key, so 3 * (keys - 1) + 1.
         */
        std::vector<gfx::Vec3> positions;
        std::vector<RotationKey> rotations;
        std::vector<ScaleKey> scales;
        /** Whether it moves vertices or textures too, which aren't read. */
        bool hasOther = false;
    };

    /**
     * Reads the animation. Fails for a file that isn't one, or on a track
     * past the end.
     */
    Error parse(const std::vector<uint8_t> &data);

    /** Its length, in frames. */
    uint32_t frames() const { return _frames; }
    const std::vector<Track> &tracks() const { return _tracks; }

    /**
     * A node's matrix in its parent's space at a frame (fractions allowed),
     * as `track` moves it from `rest`: what the track has replaces the rest
     * pose's translation, rotation or scale.
     */
    static gfx::Mat4 pose(const Track &track, float frame, const gfx::Mat4 &rest);

private:
    uint32_t _frames = 0;
    std::vector<Track> _tracks;
};

} // namespace reluva::tpw
