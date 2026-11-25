// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva {

/** A file taken out of a Microsoft cabinet: its name (as stored, \ between folders) and bytes. */
struct CabinetFile {
    std::string name;
    std::vector<uint8_t> data;
};

/**
 * The files of every Microsoft cabinet (MSCF) found in `file`, a cabinet or
 * a program holding one (a self-extracting patch); docs/common/mscab.md.
 * Their data stored or compressed with MSZIP; Quantum and LZX aren't read.
 * @return An error if a cabinet is damaged or compressed otherwise; none
 *         found is no error (`out` empty).
 */
Error readMicrosoftCabinets(const std::vector<uint8_t> &file, std::vector<CabinetFile> &out);

} // namespace reluva
