// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

// Oodle1, the compression of Granny 2 file sections: an LZ77 coder whose
// lengths, offsets and literals go through adaptive models and a range
// decoder. Worked out from granny2.dll; see docs/formats/awe/gr2.md.

namespace reluva::awe {

// Decompresses one section. The data is in three parts, ending at `stop0`,
// `stop1` and out.size(); `out` is already sized to the section's size.
Error oodle1Decompress(const std::vector<uint8_t>& in, uint32_t stop0, uint32_t stop1, std::vector<uint8_t>& out);

// Decompresses a section in any format the game's files use (0 none,
// 2 Oodle1): a Gr2Decompressor.
Error gr2Decompress(uint32_t format, const std::vector<uint8_t>& in, uint32_t stop0, uint32_t stop1,
                    std::vector<uint8_t>& out);

} // namespace reluva::awe
