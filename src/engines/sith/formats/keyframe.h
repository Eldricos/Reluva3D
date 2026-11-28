// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/sith/formats/text.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::sith {

/**
 * A .key animation: for each node of a model it moves, its position and
 * rotation at keyframes, and how fast each changes until the next
 * (docs/engines/sith/games/jk/formats/key.md).
 */
struct Keyframes {
    /** A node's state at a frame, and its change each frame after it. */
    struct Entry {
        int frame = 0;
        uint32_t flags = 0;
        Vec3 position;
        /** Pitch, yaw and roll, in degrees. */
        Vec3 rotation;
        Vec3 positionDelta;
        Vec3 rotationDelta;
    };

    /** The entries of the node of the model with this name. */
    struct Node {
        int number = 0;
        std::string meshName;
        std::vector<Entry> entries;
    };

    /** A point in the animation the game acts on: a footstep, a blow. */
    struct Marker {
        float frame = 0;
        int type = 0;
    };

    uint32_t flags = 0;
    uint32_t type = 0;
    int frames = 0;
    float fps = 0;
    int joints = 0;
    std::vector<Marker> markers;
    std::vector<Node> nodes;

    /**
     * A node's position and rotation at `frame` (which may fall between
     * frames): its last keyframe's, moved on by its change each frame.
     * @return False if the node has no entries.
     */
    bool at(const Node &node, float frame, Vec3 &position, Vec3 &rotation) const;
};

/** Reads a .key file; fails, saying where, if a line can't be read. */
Error parseKeyframes(const std::vector<uint8_t> &data, Keyframes &out);

} // namespace reluva::sith
