// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "graphics/cursor.h"

#include <cstdint>
#include <vector>

namespace reluva {

/**
 * Windows' pointer files: a cursor (`.cur`) or icon (`.ico`), one picture,
 * or an animated cursor (`.ani`), pictures shown in turn
 * (docs/common/graphics/cursors.md).
 */
struct CursorFile {
    /** One step of the animation: a picture, for so long. */
    struct Step {
        int frame = 0;
        uint32_t ms = 0;
    };
    std::vector<Cursor> frames;
    /** In order, then again from the first; one step of no time for a still cursor. */
    std::vector<Step> steps;

    /** The picture shown `ms` milliseconds into the animation; null if there's none. */
    const Cursor *at(uint64_t ms) const;
};

/** Reads a `.cur`, `.ico` or `.ani` file. */
Error readCursorFile(const std::vector<uint8_t> &data, CursorFile &out);

/**
 * Reluva's own arrow pointer, in place of the system's arrow, whose
 * picture isn't Reluva's to have: white, edged in black, its tip the
 * hotspot.
 */
Cursor standardArrow();

} // namespace reluva
