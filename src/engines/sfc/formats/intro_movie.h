// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::sfc {

/**
 * The opening movie inside SFC2Intro.exe (docs/engines/sfc/games/sfc2/formats/intro.md):
 * a Bink movie straight after the self-player's program, where its PE
 * sections end, its size the Bink header's.
 * @param program The whole of SFC2Intro.exe.
 * @param movie Set to the Bink movie's bytes.
 * @return An error if `program` isn't a PE file or holds no Bink movie there.
 */
Error introMovie(const std::vector<uint8_t> &program, std::vector<uint8_t> &movie);

} // namespace reluva::sfc
