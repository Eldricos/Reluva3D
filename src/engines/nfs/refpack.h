// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::nfs {

// Whether data starts with a RefPack header (EA's LZ77 compression, used
// by .qfs files), as docs/formats/nfs/refpack.md describes.
bool isRefPack(const std::vector<uint8_t>& data);

// Whether data starts with the header of one of EA's other compression
// schemes (a second byte 0xFB, as `32 FB` on two of NFS III's 8-bit track
// texture sets), which Reluva doesn't read yet.
bool isOtherEaCompression(const std::vector<uint8_t>& data);

// Decompresses RefPack data. Fails if it's damaged or doesn't come to
// exactly the size its header gives.
Error decompressRefPack(const std::vector<uint8_t>& data, std::vector<uint8_t>& out);

// `data` decompressed if it's RefPack, else unchanged; fails for EA's
// other compression schemes.
Error unpackIfRefPack(std::vector<uint8_t>& data);

} // namespace reluva::nfs
