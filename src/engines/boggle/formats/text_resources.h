// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <array>
#include <string_view>

namespace reluva::boggle {
using Palette = std::array<Color, 256>;
struct DiceSet {
    // Bytes are preserved, including legacy high-byte letters and multi-letter
    // faces. The reader does not apply gameplay normalization or randomization.
    std::vector<std::array<std::string, 6>> dice;
    size_t side = 0;
};
Error readPalette(std::string_view text, Palette& out);
Error readDiceSet(std::string_view text, DiceSet& out);
} // namespace reluva::boggle
