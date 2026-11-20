// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace reluva {

/**
 * The MD4 digest (RFC 1320) of `len` bytes: id Tech 3 games check their
 * .pk3 files with it.
 */
std::array<uint8_t, 16> md4(const void *data, size_t len);

} // namespace reluva
