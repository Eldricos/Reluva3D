// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/age/vecmath.h"

#include <string>
#include <vector>

namespace reluva::age {

// Paths for things that move about a city by themselves (.pathset,
// "PTH1", race/<city>/): ferries, boats and ducks on the water, the
// leaves of drawbridges, parked cars and trains. See
// docs/formats/age/pathset.md.
struct PathsetPath {
  std::string name;  // as stored: a model, maybe after "open:" or "inactive:"
  std::string model; // without the prefix
  bool open = false, inactive = false;
  uint32_t start = 0; // the second word: the point it starts at, it seems
  std::vector<uint32_t> ids; // each point's first word (PSDL blocks, it seems)
  std::vector<Vec3> points;
  uint32_t end = 0; // the last word: low byte 1 or 2, high byte 8-120
  bool backAndForth() const { return (end & 0xff) == 1; }
  float speed() const {
    return float(end >> 8 & 0xff) / 10;
  } // m/s, as Reluva reads it
};

// Reads as many whole paths as there are (one retail file stops short).
Error readPathset(const std::vector<uint8_t> &data,
                  std::vector<PathsetPath> &out);

} // namespace reluva::age
