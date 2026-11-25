// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace reluva {

/**
 * Inflates one raw deflate stream (RFC 1951, no zlib or gzip header) from
 * the start of `data`, whatever size it comes to: for streams laid end to
 * end with nothing to say where each ends, as in a Wise installer.
 * (inflateRaw, in zip.h, is for a stream whose size is known.)
 * @param consumed The bytes of `data` it took, up to the end of the byte
 *        its last bit is in: where the next stream starts.
 * @param history How many of `out`'s bytes to keep, the stream's output
 *        after them, its back-references able to reach into them: what a
 *        stream continuing another (a Microsoft cabinet's MSZIP blocks)
 *        may refer to. `maxSize` counts them.
 * @return An error on damaged data, or past `maxSize` bytes out.
 */
Error inflateStream(const uint8_t *data, size_t dataSize, std::vector<uint8_t> &out, size_t &consumed,
                    size_t maxSize = size_t(1) << 31, size_t history = 0);

} // namespace reluva
