// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::age {

// Which of a city's PSDL blocks can be seen from each (city/<map>.cpvs,
// "PVS0"): for each block, two bits for every block, 3 seen and 0 not. See
// docs/formats/age/pvs.md.
struct Pvs {
    size_t blocks = 0;                      // the PSDL's blocks, block 0 included
    std::vector<std::vector<uint8_t>> rows; // rows[b - 1]: block b's, unpacked

    bool empty() const { return rows.empty(); }
    // Whether `to` can be seen from `from` (both 1-based); true when either
    // isn't covered.
    bool sees(size_t from, size_t to) const;
};

Error readCpvs(const std::vector<uint8_t>& data, Pvs& out);

} // namespace reluva::age
