// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>

namespace reluva::idtech3 {

// refEntity_t, what a client game adds to the scene, as each game lays it
// out. The fields up to the shader's time are Quake III's in all of them;
// what follows, and the numbering of the kinds of entity past a model,
// is each game's own (eliteforce/ref_entity.h has Elite Force's).
struct RefEntityLayout {
    size_t size = 140; // Quake III's
    // Quake III's: its type (0: a model), render flags, model, lighting
    // origin, axes (forward, left, up), origin, frame, old origin, old
    // frame and how far back towards it, skin number, custom skin and
    // shader, colour, the shader's start time.
    size_t type = 0, flags = 4, model = 8, lightingOrigin = 12, axes = 28, origin = 68, frame = 80, oldOrigin = 84,
           oldFrame = 96, backLerp = 100, skinNum = 104, customSkin = 108, customShader = 112, rgba = 116,
           shaderTime = 128;
    // The effects' fields: a sprite's radius and rotation; a line's
    // widths and its texture's repeats; a curve's control points;
    // electricity's jitter; a cylinder's repeats round it.
    size_t radius = 132, rotation = 136, width = 0, endWidth = 0, repeat = 0, control1 = 0, control2 = 0, jitter = 0,
           cylinderRepeat = 0;
    // The kinds of entity: -1 where the game has none.
    int32_t typeModel = 0, typeSprite = 2, typeQuad = -1, typeLine = -1, typeTaperedLine = -1, typeCurve = -1,
            typeCylinder = -1, typeBolt = -1, typePortal = 7;
};

// Render flags (Quake III's, the same in the games after it): drawn only
// in mirrors and portals (the player's own body); not through them;
// drawn in front of everything else, its depth kept to the nearest 0.3
// of the range (the player's weapon and its flash); lit from the
// lighting origin.
namespace refent {
constexpr int32_t ThirdPerson = 2, FirstPerson = 4, DepthHack = 8, UseLightingOrigin = 0x80;
constexpr float kDepthHackRange = 0.3f;
} // namespace refent

} // namespace reluva::idtech3
