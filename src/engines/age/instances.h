// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::age {

// A model placed in a city (.inst: city/sf.inst, city/london.inst). See
// docs/formats/age/inst.md.
struct Instance {
    uint16_t block = 0; // the PSDL block it stands in
    uint16_t flags = 0; // meaning not known yet
    std::string model;  // geometry/<model>.pkg
    // Three axes (X, Y, Z: may be scaled), then the position.
    std::array<float, 12> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
};

Error readInstances(const std::vector<uint8_t>& data, std::vector<Instance>& out);

} // namespace reluva::age
