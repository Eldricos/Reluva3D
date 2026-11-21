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
    size_t type = 0;
    size_t flags = 4;
    size_t model = 8;
    size_t lightingOrigin = 12;
    size_t shadowPlane = 24; // the height its projected shadow lies at
    size_t axes = 28;
    size_t origin = 68;
    size_t frame = 80;
    size_t oldOrigin = 84;
    size_t oldFrame = 96;
    size_t backLerp = 100;
    size_t skinNum = 104;
    size_t customSkin = 108;
    size_t customShader = 112;
    size_t rgba = 116;
    size_t shaderTime = 128;
    // The effects' fields: a sprite's radius and rotation; a line's
    // widths and its texture's repeats; a curve's control points;
    // electricity's jitter; a cylinder's repeats round it. 0 for a field the
    // game hasn't.
    size_t radius = 132;
    size_t rotation = 136;
    size_t width = 0;
    size_t endWidth = 0;
    size_t repeat = 0;
    size_t control1 = 0;
    size_t control2 = 0;
    size_t jitter = 0;
    size_t cylinderRepeat = 0;
    // Where a cylinder's first width is (a point's offset): 0 for the
    // origin, its other end a height along the first axis; else its other
    // end is the origin.
    size_t cylinderBase = 0;
    // A quad's corners: from its second and third axes, or (true) made
    // square to its first, the way it faces, and turned by its rotation.
    bool quadFacesAxis = false;
    // Raven's: the model's scale on each axis (0 for 1), and its Ghoul2
    // instance (0: none).
    size_t modelScale = 0;
    size_t ghoul2 = 0;
    // The kinds of entity: -1 where the game has none. A beam is Quake
    // III's: a thin red tube from the origin to the old origin, no shader.
    int32_t typeModel = 0;
    int32_t typeSprite = 2;
    int32_t typeQuad = -1;
    int32_t typeLine = -1;
    int32_t typeOrientedLine = -1;
    int32_t typeTaperedLine = -1;
    int32_t typeCurve = -1;
    int32_t typeCylinder = -1;
    int32_t typeBolt = -1;
    // Jedi Outcast's saber glow: sprites along the blade from the origin
    // down its first axis, as long as the float at `saberLength`.
    int32_t typeSaberGlow = -1;
    size_t saberLength = 0;
    // Electricity drawn as Raven's Jedi Outcast renderer draws it (its
    // shape from the seed in `frame`), not Elite Force's way.
    bool ravenBolt = false;
    int32_t typeBeam = 3;
    int32_t typePortal = 7;
};

// Render flags (Quake III's, the same in the games after it): drawn only
// in mirrors and portals (the player's own body); not through them;
// drawn in front of everything else, its depth kept to the nearest 0.3
// of the range (the player's weapon and its flash); lit from the
// lighting origin.
namespace refent {
constexpr int32_t kThirdPerson = 2;
constexpr int32_t kFirstPerson = 4;
constexpr int32_t kDepthHack = 8;
constexpr int32_t kUseLightingOrigin = 0x80;
// Its shadow plane (refEntity_t's, at 24) is set: under shadow mode 3 it
// casts a projected shadow there.
constexpr int32_t kShadowPlane = 0x100;
// Raven's: electricity narrowing to nothing at its end (RF_TAPERED).
constexpr int32_t kTapered = 0x8000;
constexpr float kDepthHackRange = 0.3f;
} // namespace refent

} // namespace reluva::idtech3
