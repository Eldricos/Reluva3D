// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/tpw/ui/model_view.h"

#include <cstddef>

namespace reluva::tpw {

/**
 * The game's windows, buttons and tabs are flat models (`ui/!window_s.MD2`,
 * `ui/filebut.MD2`), laid out on a screen of 2048 × 1536 units, y down
 * from 0 to −1536: 3.2 units a pixel at 640 × 480, and a texel of their
 * textures a pixel. See docs/engines/tpw/games/simcoaster/formats/ui-models.md.
 */
constexpr float kUiUnitsPerPixel = 3.2f;

/** A rectangle of the screen, in pixels. */
struct ScreenRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

/**
 * Draws one of a UI model's meshes onto an RGBA32 surface, its textures'
 * alpha blended over what's there, at the place the model gives it.
 */
void drawUiMesh(Surface &screen, const LoadedModel &model, size_t mesh);

/**
 * Draws one of a UI model's meshes stretched to fill `to`, as the game
 * draws a window or a button of any size from one: its edges keep their
 * sizes, the parts between them stretch.
 */
void drawUiMesh(Surface &screen, const LoadedModel &model, size_t mesh, const ScreenRect &to);

/** A UI model's mesh's place on the screen, as drawUiMesh puts it. */
ScreenRect uiMeshRect(const LoadedModel &model, size_t mesh);

} // namespace reluva::tpw
