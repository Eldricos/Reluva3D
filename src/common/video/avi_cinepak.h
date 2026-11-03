// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/image.h"
#include "common/audio/sound.h"
namespace reluva::video {
// Bounded classic AVI: Cinepak video stream 0, optional PCM stream 1.
// Full-width, vertically stacked strips; no platform codec or original code.
struct AviCinepak {
    int width=0,height=0;
    uint32_t rate=0,scale=0;
    std::vector<image::Image> frames;
    audio::Sound soundtrack;
    uint64_t durationMillis() const;
    size_t frameAt(uint64_t elapsed, bool loop = false) const;
    Error read(const std::vector<uint8_t>& bytes);
};
} // namespace reluva::video
