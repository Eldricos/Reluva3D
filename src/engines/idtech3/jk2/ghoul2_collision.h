// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/formats/ghoul2.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::idtech3::jk2 {

/**
 * A triangle of a Ghoul2 model a trace met, as single player's engine
 * records it for the game (a CCollisionRecord, 64 bytes, after the trace's
 * own fields; docs/engines/idtech3/games/jk2/systems/sp-ghoul2.md,
 * Collision).
 */
struct Ghoul2Hit {
    float distance = 100000; // from the trace's start
    int32_t entity = -1;     // -1: a free record
    int32_t model = 0;       // in the instance
    int32_t polygon = 0;     // the triangle, in its surface
    int32_t surface = 0;
    std::array<float, 3> position{};
    std::array<float, 3> normal{}; // the triangle's, one long
    int32_t flags = 0;             // kGhoul2FrontFace, or 0 for the back
    float baryI = 0;
    float baryJ = 0;
};

constexpr int32_t kGhoul2FrontFace = 1;
/// The records a trace holds.
constexpr size_t kMaxGhoul2Hits = 16;

/// The trace types the game asks for: none, every triangle met, or the first.
enum class Ghoul2Trace : int32_t { kNone = 0, kCollide = 1, kReturnOnHit = 2 };

/**
 * The triangles of an instance's models (posed, as Ghoul2Instances::drawn
 * gives them) that a trace from `start` to `end` meets, added to `hits`
 * (at most kMaxGhoul2Hits in all) and sorted nearest first. The models
 * stand where `world` places them (Ghoul2Instances::worldMatrix), scaled
 * by `scale` (0 for 1 on an axis). With a `radius` (half the traced box's
 * width) a triangle counts if it comes near the ray, as the original's
 * looser test does; without, the segment must cross it.
 */
void traceGhoul2(const std::vector<Ghoul2Pose> &models, const Bone34 &world, const std::array<float, 3> &scale,
                 const std::array<float, 3> &start, const std::array<float, 3> &end, int32_t entity, Ghoul2Trace type,
                 int lod, float radius, std::vector<Ghoul2Hit> &hits);

} // namespace reluva::idtech3::jk2
