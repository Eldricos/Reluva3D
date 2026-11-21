// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/formats/text_object.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::stawayteam {

struct BinaryObject;

/** A sequence's own data (CSequence, CRotationSequence): its frames. */
struct SequenceData {
    std::array<int32_t, 4> box{}; // the frames' bounds: left, top, right, bottom
    std::vector<std::optional<Frame>> frames;
    // CRotationSequence's 8 bytes for each frame, as two numbers: not yet
    // known what they are.
    std::vector<std::array<int32_t, 2>> frameExtra;
};

/** A model's rotation sequence, under its name (CModel). */
struct NamedSequence {
    std::string name;
    std::unique_ptr<BinaryObject> sequence; // nullptr where the model has none
};

/** A point of a waypoint map (CPathWayPoint). */
struct WayPoint {
    int32_t flags = 0;
    int32_t x = 0;
    int32_t y = 0;
    std::vector<int32_t> lines; // the map's lines it's on
};

/** A line between two waypoints (CPathLineSegment). */
struct PathLine {
    int32_t flags = 0;
    float maxPassWidth = 0;
    float length = 0;
    float angleAToB = 0;
    int32_t a = 0; // its ends, by the waypoints' numbers
    int32_t b = 0;
};

/**
 * One of the game's objects as its binary writer saves it: a sequence, a
 * model, a waypoint map (docs/engines/stawayteam/formats/binary-object.md).
 * Its properties are kept as an object in text would have them, every value
 * as text, so they're read the same way whichever kind of file they're
 * from; what its class saves of its own is in the fields after.
 */
struct BinaryObject {
    TextObject object;
    std::optional<SequenceData> sequence; // CSequence and its kinds
    std::string modelLabel;               // CModel's string before its sequences
    std::vector<NamedSequence> sequences; // CModel's
    std::vector<WayPoint> wayPoints;      // CWayPointMap's
    std::vector<PathLine> lines;          // CWayPointMap's
};

/**
 * Reads the object a binary file holds: of class `className` (a file's
 * kind says it: a .mdl16 is a CModel, a .way a CWayPointMap), or, if
 * empty, of the class its version says (a .seq16: a CSequence or a
 * CFont).
 * @return An error if it isn't one of the game's objects, if it holds a
 *         class or kind of value not worked out, or if anything is left after it.
 */
Error readBinaryObject(const uint8_t *data, size_t size, const std::string &className, BinaryObject &out);

} // namespace reluva::stawayteam
