// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace reluva {

// Inflates one raw deflate stream (RFC 1951, no zlib or gzip header) from
// the start of `data`, whatever its size, and says how many bytes of
// `data` it took (up to the end of the byte its last bit is in): for
// streams laid end to end with nothing to say where each ends, as a Wise
// installer's. Fails on damaged data or past `maxSize` bytes out.
Error inflateStream(const uint8_t* data, size_t dataSize, std::vector<uint8_t>& out, size_t& consumed,
                    size_t maxSize = size_t(1) << 31);

} // namespace reluva
