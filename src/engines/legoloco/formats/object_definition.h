// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::legoloco {

/** A keyword line of an object definition, with the words after it. */
struct DefinitionLine {
    std::string keyword;
    std::vector<std::string> values;
    int line = 0; // in the file, from 1
};

/**
 * A grid of squares a piece fills: `physical_occupancy` (with layers) or
 * `bitmap_occupancy` (one layer). The game keeps at most 13 columns, 9 rows
 * and 7 layers.
 */
struct Occupancy {
    int columns = 0;
    int rows = 0;
    int layers = 1;
    std::vector<int> cells; // layer by layer, each row by row

    /** A cell's value. */
    int at(int layer, int row, int column) const { return cells[size_t((layer * rows + row) * columns + column)]; }
};

/**
 * One line of the frame-set table: a named run of the picture's frames, and
 * what follows it. The names of the fields after `split` are from the
 * files' own comments.
 */
struct FrameSet {
    std::string name;
    uint16_t start = 0;
    uint16_t end = 0;
    uint16_t delay = 1; // the game makes 0 into 1
    uint8_t split = 0;
    int32_t restartDelay = 0;
    int16_t next = -1; // the frame set after this one, or -1
    int16_t sound = 0;
    int32_t replayDelay = 0;
    uint16_t unnamed = 0; // read by the game; the files' comments leave it out
    uint8_t flipX = 0;
};

/**
 * An object definition (`.dat`): the text describing a piece, person,
 * vehicle or button, beside the picture of the same name
 * (docs/engines/legoloco/formats/dat.md). Parts end with `-9`; what follows
 * the last is comment.
 */
struct ObjectDefinition {
    std::vector<std::vector<DefinitionLine>> parts;
    std::optional<Occupancy> physical;
    std::optional<Occupancy> bitmap;
    int totalFrames = 0;
    int16_t cursorFrameSets[2] = {0, 0}; // `cursor_frame_set`'s two values
    std::vector<FrameSet> frameSets;

    /** The first line of a keyword in any part, matched ignoring case, or nullptr. */
    const DefinitionLine *find(std::string_view keyword) const;
};

/** Reads an object definition's text. */
Error parseObjectDefinition(std::string_view text, ObjectDefinition &out);

} // namespace reluva::legoloco
