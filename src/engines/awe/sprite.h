// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/image.h"
#include "common/types.h"

// How the game draws a scene's 2D objects (pictures, video loops): with
// Direct3D 8's ID3DXSprite::DrawTransform, the matrix from
// D3DXMatrixTransformation (motoe.exe 0x437ac0). The picture's pixels are
// scaled, turned about its top-left corner (its rotation centre is left
// at 0), then moved to the object's position. See docs/formats/awe/ac2.md.

namespace reluva::awe {

struct SpriteTransform {
    float x = 0, y = 0;           // where the top-left corner goes
    float scaleX = 1, scaleY = 1; // negative mirrors
    float roll = 0;               // radians, clockwise on the screen
    bool plain() const { return scaleX == 1 && scaleY == 1 && roll == 0; }
    // Where the picture's point (u, v) lands.
    void apply(float u, float v, float& outX, float& outY) const;
};

// Draws `img` over `dst`, blended by its alpha. With `keepAlpha`, `dst`'s
// alpha becomes the coverage (for an overlay that goes over a 3D frame);
// otherwise `dst` is taken as opaque.
void drawSprite(Surface& dst, const image::Image& img, const SpriteTransform& t, bool keepAlpha);

} // namespace reluva::awe
