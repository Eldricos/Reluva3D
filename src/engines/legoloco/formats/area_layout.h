// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace reluva::legoloco {

/**
 * An arrangement of area layouts (`layouts\3 x 2.lay`): how many areas,
 * in how many columns and rows, and the area boards' names
 * (docs/engines/legoloco/formats/lay.md).
 */
struct AreaLayout {
    int areas = 0;
    int columns = 0;
    int rows = 0;
    std::vector<std::string> boards; // "area1.sav"...
};

/** Reads an arrangement's text. */
Error parseAreaLayout(std::string_view text, AreaLayout &out);

/** Reads `layouts\index.lay`: the arrangements' names ("3 X 2"), a line each. */
std::vector<std::string> parseLayoutIndex(std::string_view text);

} // namespace reluva::legoloco
