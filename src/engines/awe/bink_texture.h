// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::awe {

// Decodes a texture stored with Granny's Bink encoding (a .gr2 texture's
// Encoding 3) into RGBA pixels, rows top to bottom, as granny2.dll's
// GrannyBinkDecompressTexture does into an R, G, B, A layout. `flags` is
// the texture's SubFormat, plus 1 if its layout has alpha. Only what the
// game's files use is decoded: flags 4 and 5 (a wavelet codec, colours
// as luma and two differences, 5 with an alpha plane); others fail.
// docs/formats/awe/gr2.md describes the format.
Error decodeBinkTexture(int width, int height, uint32_t flags, const std::vector<uint8_t>& in,
                        std::vector<uint8_t>& rgba);

} // namespace reluva::awe
