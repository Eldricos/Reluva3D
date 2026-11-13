// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <vector>

namespace reluva {

// Decompresses a PKWARE Data Compression Library ("implode") stream, as made by
// the DOS-era PKWARE DCL. Follows Mark Adler's blast.c. Returns false on corrupt
// or truncated input; `out` then holds what was decoded so far.
bool decompressDCL(const uint8_t *src, uint32_t srcSize, std::vector<uint8_t> &out);

} // namespace reluva
