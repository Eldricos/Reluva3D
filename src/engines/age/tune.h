// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace reluva::age {

// The game's tuning text: a "type: a" line, then named blocks of
// "Name value..." lines and nested blocks, as in
//
//   type: a
//   clear-morning {
//     KeyColor 0.9 0.9 0.8
//     Engine {
//       MaxRPM 8500
//     }
//   }
//
// (lighting .lt00-.lt15, cars' .vehCarSim, cameras...). See
// docs/formats/age/tune.md.
struct TuneNode {
    std::string name;
    std::vector<std::string> values;
    std::vector<TuneNode> children;

    // The first child of that name (ignoring case), or nullptr.
    const TuneNode* child(std::string_view name) const;
    // A child's i-th value as a number, or `fallback`.
    float number(std::string_view name, size_t i = 0, float fallback = 0) const;
};

// The file's blocks as the root's children.
Error parseTune(std::string_view text, TuneNode& root);

} // namespace reluva::age
