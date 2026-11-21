// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::legoisland {

/** One of a path's named kinds of trigger: "TRC", "TRS", "TRM"... */
struct GphTriggerKind {
    std::string name;
    uint32_t flags = 0; // 0xFFFFFF in its low bits, and 0x11 to 0x3F in its top byte
};

/** A line between two points of a path: the boundary of one face, or of two. */
struct GphEdge {
    uint16_t flags = 0; // 4: one face; 15: two
    uint16_t from = 0;  // points
    uint16_t to = 0;
    // A face it bounds, and the edges after and before it round that face,
    // each sharing one of its points; and the same for the second face, if any.
    std::array<uint16_t, 3> first{};
    std::array<uint16_t, 3> second{};
    bool twoFaces = false;
    std::array<float, 3> direction{}; // from `from` to `to`, of length 1
    float length = 0;
};

/** A trigger on a face: one of the path's kinds, with a value. */
struct GphTrigger {
    uint16_t kind = 0; // into GphFile::kinds
    uint32_t value = 0;
    float along = 0;
};

/** A face of a path: an area the characters and vehicles can be on, bounded by edges. */
struct GphFace {
    std::vector<uint16_t> edges;
    std::array<uint8_t, 2> unknown{};             // 0 to 7, 1 to 8 (formats/obj.md)
    std::string name;                             // "EDG10_01"
    std::array<float, 4> plane{};                 // its plane: a normal and a distance
    std::vector<std::array<float, 4>> edgePlanes; // one an edge, upright, through it
    std::array<float, 3> center{};
    float radius = 0;
    std::vector<GphTrigger> triggers;
    std::array<float, 3> triggerDirection{}; // only when it has triggers
};

/**
 * A path (`.gph`, an SI file's): the ground characters and vehicles move
 * on, as faces between edges between points, and the triggers on them
 * (docs/engines/legoisland/formats/obj.md).
 */
struct GphFile {
    std::vector<GphTriggerKind> kinds;
    std::vector<std::array<float, 3>> points;
    std::vector<GphEdge> edges;
    std::vector<GphFace> faces;

    /** Reads it from its bytes, which must end where its last face does. */
    Error parse(const std::vector<uint8_t> &data);
};

} // namespace reluva::legoisland
