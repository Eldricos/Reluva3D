// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Glide stand-in's GPU backend (docs/engines/glide.md, The GPU
// backend): a Renderer that draws on a gfx::Compute device by the same
// rules as the software reference (the same set-up on the CPU, the pixel
// pipeline in a compute kernel, rasterize.comp), keeping the frame buffer
// and texture memory on the GPU and copying them back when the pipeline's
// buffers are read or written from outside. And a renderer that draws
// every call both ways to compare them.

#include "common/glide/pipeline.h"
#include "common/gpu_compute.h"

#include <functional>
#include <memory>
#include <string>

namespace reluva::glide {

// Null (with `error` set) if the device can't run the kernel.
std::unique_ptr<Renderer> makeGpuRenderer(std::unique_ptr<gfx::Compute> gpu, std::string& error);

// How a frame drawn by one renderer differs from the same drawn by the
// software reference, at a buffer swap: both 5:6:5 frames widened to 8
// bits a channel (the top bits repeated).
struct FrameComparison {
    uint64_t swap = 0;          // the pipeline's swap count
    double meanDifference = 0;  // per channel, of 255, over the frame
    uint64_t pixelsDiffering = 0;  // any channel off at all
    uint64_t pixelsOver24 = 0;     // any channel off by more than 24
    int maxDifference = 0;
};

// Draws with `tested` (the GPU backend) into the pipeline as usual, and
// every call with the software reference too, into buffers of its own
// with the same state and textures; `onSwap` gets each frame's comparison.
// What's written into the buffers from outside (the linear frame buffer)
// is copied to the reference's.
std::unique_ptr<Renderer> makeComparingRenderer(std::unique_ptr<Renderer> tested,
                                                std::function<void(const FrameComparison&)> onSwap);

} // namespace reluva::glide
