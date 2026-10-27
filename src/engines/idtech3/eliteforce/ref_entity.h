// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/ref_entity.h"

namespace reluva::idtech3::eliteforce {

// Elite Force's refEntity_t: Quake III's 140 bytes, then 20 of its own (see
// eliteforce-syscalls.md), with its effect types read from what its client
// game adds when firing: a sprite facing the camera (1: radius at 136,
// rotation in degrees at 132; its axes are left as they were cleared), a
// quad in the entity's own axes (2: left and up, radius at 136; the first
// axis faces out of what it lies on), a line from the origin to the old
// origin (9: width at 132, the texture repeated along it as often as 140
// says), and one tapering from the width at 132 to the one at 136 (11).
// More, from the other weapons: a cubic Bezier curve from the origin
// through control points at 136 and 148 to the old origin (12), a ring or
// cone round the first axis (13: radii at 132 and 136, height at 140, 144
// the texture's repeats round it), and electricity from the origin to the
// old origin (14: width 132, jitter 136, texture scale 140). A mirror's or
// portal's camera is 8 (game-run -cgame on a map with a mirror: origin and
// old origin the same, at the mirror; axes as cleared).
constexpr RefEntityLayout kRefEntityLayout = [] {
    RefEntityLayout l;
    l.size = 160;
    l.rotation = 132;
    l.radius = 136;
    l.width = 132;
    l.endWidth = 136;
    l.repeat = 140;
    l.control1 = 136;
    l.control2 = 148;
    l.jitter = 136;
    l.cylinderRepeat = 144;
    l.typeSprite = 1;
    l.typeQuad = 2;
    l.typeLine = 9;
    l.typeTaperedLine = 11;
    l.typeCurve = 12;
    l.typeCylinder = 13;
    l.typeBolt = 14;
    l.typePortal = 8;
    return l;
}();

} // namespace reluva::idtech3::eliteforce
