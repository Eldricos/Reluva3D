// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// General-purpose GPU work for code that draws by its own rules (the Glide
// stand-in's GPU backend, graphics/glide): storage buffers, compute kernels
// that read some and write others, uploads and read-backs. Separate from
// gfx::Renderer3D (fixed-function drawing into a window's frame), and
// usable without a window: a backend registers a factory once
// (setComputeFactory) and code asks for a device with createCompute().
//
// Work is recorded in order and run in that order; download() runs what's
// recorded and waits for it, flush() only sends it on.

#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace reluva::gfx {

// A compute kernel's code and the resources it declares. SPIR-V for Vulkan
// (set 0: read-only storage buffers; set 1: read-write storage buffers;
// set 2: uniform buffers) and the same as Metal source (buffer indices:
// uniforms, then read-only, then read-write), entry point "main" / "main0".
struct ComputeKernelCode {
    const uint8_t *spirv = nullptr;
    size_t spirvSize = 0;
    const char *msl = nullptr;
    uint32_t readOnlyBuffers = 0, readWriteBuffers = 0, uniformBuffers = 0;
    uint32_t threadsX = 1, threadsY = 1, threadsZ = 1;
};

using ComputeBuffer = uint32_t; // 0 = none
using ComputeKernel = uint32_t;

class Compute {
public:
    virtual ~Compute() = default;
    // The GPU and driver doing the work, for diagnostics.
    virtual std::string deviceName() const = 0;
    virtual Error createBuffer(uint32_t bytes, ComputeBuffer &out) = 0;
    virtual void destroyBuffer(ComputeBuffer buffer) = 0;
    virtual Error createKernel(const ComputeKernelCode &code, ComputeKernel &out) = 0;
    // Copies `bytes` into the buffer at `offset`, after the work recorded
    // before. `replaceAll`: nothing else in the buffer is wanted any more
    // (the GPU may then use fresh memory rather than wait).
    virtual void upload(ComputeBuffer buffer, uint32_t offset, const void *data, uint32_t bytes,
                        bool replaceAll = false) = 0;
    // Runs the kernel over groupsX x groupsY x groupsZ workgroups, with
    // these buffers bound in order and `uniforms` as its one uniform block.
    virtual void dispatch(ComputeKernel kernel, const std::vector<ComputeBuffer> &readOnly,
                          const std::vector<ComputeBuffer> &readWrite, const void *uniforms, uint32_t uniformBytes,
                          uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;
    // Runs everything recorded, then copies `bytes` from the buffer.
    virtual Error download(ComputeBuffer buffer, uint32_t offset, void *out, uint32_t bytes) = 0;
    // Sends what's recorded to the GPU without waiting.
    virtual void flush() = 0;
};

using ComputeFactory = std::function<std::unique_ptr<Compute>(std::string &error)>;
// Set by the program's backend at start-up (none: no GPU compute).
void setComputeFactory(ComputeFactory factory);
// A device of its own for the caller (null, with `error` set, if there is
// none).
std::unique_ptr<Compute> createCompute(std::string &error);

} // namespace reluva::gfx
