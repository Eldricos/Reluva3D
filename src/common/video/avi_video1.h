// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/image.h"

namespace reluva::video {
// Bounded single-stream AVI / 8/16-bit Microsoft Video 1 (CRAM/MSVC).
// Indexed AVI uses a static header palette; palette changes are unsupported.
// Frames are decoded independently of platform codecs; unsupported AVI fails.
struct AviVideo1 {
    int width = 0, height = 0;
    uint32_t rate = 0, scale = 0;
    std::vector<image::Image> frames;
    Error read(const std::vector<uint8_t>& bytes);
};
} // namespace reluva::video
