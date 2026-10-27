// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/gfx3d.h"

#include <cstdint>
#include <utility>
#include <vector>

// How the game finds the object under the mouse (motoe.exe's scene pick,
// 0x435eb0), for its cursor and its clicks. See docs/formats/awe/ac2.md.

namespace reluva::awe {

// An object the pick may find: one shown, with what the pick asks of it.
struct PickCandidate {
    float depth = 0;        // d0, else z (a character's: where it stands)
    bool character = false; // NPCX or PCXX
    bool pickFirst = false; // +0xb70
    bool outline = false;   // an outline (listD): a region
    bool rect = false;      // a rectangle that isn't empty
    bool clickable = false; // a script, or a conditional one that holds (0x40e570)
    bool hit = false;       // the point in its outline, else in its rectangle (0x40ef70)
    bool boneHit = false;   // the ray through the point meets a Bip01 bone's box (0x428c80)
};

// The candidate picked (its index), -1 if none. Tried front to back (by
// depth, the later in the list first where equal), in three passes, the
// first hit of a pass the answer: pickFirst objects with an outline or no
// rectangle; then the characters (those with only a rectangle by their
// bones' boxes, if they can be clicked or an item is in hand); then
// everything.
int pick(const std::vector<PickCandidate>& candidates, bool itemInHand);

// Whether the pixel (x, y) is in the polygon, filled by winding (as
// CreatePolygonRgn with WINDING, then PtInRegion).
bool inPolygon(const std::vector<std::pair<uint32_t, uint32_t>>& points, int x, int y);

// Whether a ray (from, along dir) meets a box (lo..hi) in the axes the
// affine `m` puts in the world (GrannyRayIntersectsBox).
bool rayHitsBox(const gfx::Mat4& m, const float lo[3], const float hi[3], const gfx::Vec3& from, const gfx::Vec3& dir);

} // namespace reluva::awe
