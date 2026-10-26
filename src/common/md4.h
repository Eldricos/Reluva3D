// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace reluva {

// MD4 (RFC 1320), which id Tech 3 games use for the checksums of their
// .pk3 files: the 16-byte digest of `len` bytes.
std::array<uint8_t, 16> md4(const void* data, size_t len);

} // namespace reluva
