// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/nfs2se/formats/hrz.h"

#include <array>
#include <cstdint>

namespace reluva::nfs::nfs2se {

/** How many depth buckets the software program sorts its polygons into (0x45c5f0). */
constexpr int kDepthBuckets = 1240;

/**
 * The software program's depth cueing (docs/engines/nfs/games/nfs2se/
 * formats/hrz.md, The software depth cueing): the light, 0 to 15, of a
 * polygon in each depth bucket, from the sky file's four (distance,
 * light) points (0x470d10, 0x45be20). A bucket is half a unit of the
 * polygon's mean depth in the view, from 10 units nearer than its list's
 * bias: depthBucket.
 */
std::array<uint8_t, kDepthBuckets> depthCueLights(const Sky &sky);

/** The bucket of a polygon `depth` units deep in a list of bias `bias` units; outside 0-1239, not drawn. */
inline int depthBucket(float depth, float bias) {
    const float b = 2 * (depth + 10 - bias);
    return b < 0 ? -1 : static_cast<int>(b);
}

} // namespace reluva::nfs::nfs2se
