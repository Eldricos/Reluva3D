// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/botf/formats/text_table.h"
#include "engines/botf/ui/assets.h"

#include <cstdint>

// The tips the game shows for its controls (docs/engines/botf/systems/screens.md,
// "The game menu").

namespace reluva::botf {

/**
 * The game's tool tips: teachtip.tdb's, keyed by the id of the widget each
 * is for, drawn by the pointer in a black box with a purple border.
 */
class ToolTips {
public:
    /** Reads teachtip.tdb from `assets`, which must outlive it; without it there are none. */
    void load(Assets &assets);
    /** Whether there's a tip for widget `id`. */
    bool has(int32_t id) const { return _tips.find(uint32_t(id)) != nullptr; }
    /** Widget `id`'s tip, by the pointer at (x, y), kept on the 800x600 screen; nothing if it has none. */
    void draw(Surface &screen, int32_t id, int x, int y) const;

private:
    Assets *_assets = nullptr;
    TextTable _tips;
};

} // namespace reluva::botf
