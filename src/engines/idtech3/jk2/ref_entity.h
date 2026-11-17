// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/ref_entity.h"

namespace reluva::idtech3::jk2 {

// Jedi Outcast's refEntity_t (multiplayer's, 212 bytes), as the GPL Jedi
// Outcast source lays it out (see
// docs/engines/idtech3/games/jk2/systems/syscalls.md): Quake III's first
// three fields, then the axes, a word saying they're scaled, the origin
// and old origin, custom shader, colour, texture offsets, radius,
// rotation, shader time and frame (the part every effect has), then the
// lighting origin, shadow plane, old frame, back lerp, skin number and
// custom skin, and the saber and Ghoul2 fields after. Its entity types:
// model 0, sprite 2, oriented quad 3, beam 4, saber glow 5, electricity 6,
// portal camera 7, line 8, oriented line 9, cylinder 10. Its effects keep their sizes in
// the common part, as its renderer reads them (docs/engines/idtech3/formats/efx.md):
// a line's or electricity's width is its radius, electricity's chaos the
// first axis' first word; a cylinder runs from the old origin (the radius
// round) to the origin (the rotation round); a quad faces its first axis.
constexpr RefEntityLayout kRefEntityLayout = [] {
    RefEntityLayout l;
    l.size = 212;
    l.axes = 12;
    l.origin = 52;
    l.oldOrigin = 64;
    l.customShader = 76;
    l.rgba = 80;
    l.radius = 92;
    l.rotation = 96;
    l.shaderTime = 100;
    l.frame = 104;
    l.lightingOrigin = 108;
    l.oldFrame = 124;
    l.backLerp = 128;
    l.skinNum = 132;
    l.customSkin = 136;
    l.width = 92;
    l.endWidth = 96;
    l.jitter = 12;
    l.cylinderBase = 64;
    l.quadFacesAxis = true;
    l.modelScale = 196;
    l.ghoul2 = 208;
    l.typeSprite = 2;
    l.typeQuad = 3;
    l.typeBeam = 4;
    l.typeSaberGlow = 5;
    l.saberLength = 180;
    l.typeBolt = 6;
    l.ravenBolt = true;
    l.typePortal = 7;
    l.typeLine = 8;
    l.typeOrientedLine = 9;
    l.typeCylinder = 10;
    return l;
}();

// Single player's refEntity_t (168 bytes), as the GPL Jedi Outcast
// single-player source lays it out: Quake III's up to the shader's time,
// then a sprite's radius and rotation (or a saber glow's length, 136),
// the Ghoul2 angles, the model's scale (152) and a pointer to its
// CGhoul2Info_v (164). Its entity types: model 0, sprite 2, oriented quad
// 3, line 4, electricity 5, cylinder 6, beam 8, saber glow 9, portal
// camera 10. Its effects keep their sizes in the common part as
// multiplayer's do (the effects code is the same), in its own places.
constexpr RefEntityLayout kSpRefEntityLayout = [] {
    RefEntityLayout l;
    l.size = 168;
    l.radius = 132;
    l.rotation = 136;
    l.width = 132;
    l.endWidth = 136;
    l.jitter = 28;
    l.cylinderBase = 84;
    l.quadFacesAxis = true;
    l.modelScale = 152;
    l.ghoul2 = 164;
    l.typeSprite = 2;
    l.typeQuad = 3;
    l.typeLine = 4;
    l.typeBolt = 5;
    l.ravenBolt = true;
    l.typeCylinder = 6;
    l.typeBeam = 8;
    l.typeSaberGlow = 9;
    l.saberLength = 136;
    l.typePortal = 10;
    return l;
}();

} // namespace reluva::idtech3::jk2
