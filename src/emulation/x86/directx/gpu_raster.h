// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/directx/device.h"
#include "graphics/gpu_compute.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::x86::dx {

/**
 * Draws Direct3D's triangles on the GPU, by the software rasterizer's rules
 * (docs/common/x86/directx.md, Drawing on the GPU): each triangle set up
 * here as Draw::rasterize sets it up (its pixels on each row, exactly, and
 * its values' planes), its pixels worked out by a compute kernel
 * (shaders/rasterize.comp) as Draw::shade and Draw::finish work them out.
 * The draws given go to the GPU at flush(), the target's and depth
 * buffer's pixels with them, and come back into the surfaces' memory.
 */
class GpuRaster {
public:
    /**
     * One on its own GPU device (gfx::createCompute).
     * @return nullptr, with why in `error`, if there's no GPU or the kernel
     *         can't run on it.
     */
    static std::unique_ptr<GpuRaster> create(std::string &error);

    /**
     * Whether it can draw `d`'s pixels: a 16-bit 565 or 32-bit X888 target,
     * a depth buffer of 2 or 4 bytes a pixel, if any.
     */
    static bool canDraw(const Draw &d);
    /**
     * Adds a draw's triangles (projected, as Draw::rasterize takes them) to
     * those drawn at the next flush. A draw into other surfaces than those
     * waiting flushes them first.
     */
    void add(const Draw &d, const std::vector<std::array<ClipVertex, 3>> &triangles);
    /** Whether draws are waiting for flush(). */
    bool pending() const { return !_prims.empty(); }
    /**
     * Draws what's waiting into its target and depth buffer, their memory
     * read first and written back after.
     */
    Error flush();

private:
    explicit GpuRaster(std::unique_ptr<gfx::Compute> compute) : _gpu(std::move(compute)) {}
    // Where a texture's levels are in texture memory, as last uploaded.
    struct TextureSlot {
        std::vector<uint64_t> versions;
        std::vector<std::array<uint32_t, 4>> levels; // base, key base, width, height
    };
    const TextureSlot &textureSlot(const DecodedTexture &t);
    uint32_t stateRecord(const Draw &d);
    void addTriangle(const Draw &d, uint32_t state, const ClipVertex &a, const ClipVertex &b, const ClipVertex &c);
    Error ensureBuffer(gfx::ComputeBuffer &buffer, uint32_t &capacity, uint32_t bytes);

    std::unique_ptr<gfx::Compute> _gpu;
    gfx::ComputeKernel _kernel = 0;
    gfx::ComputeBuffer _batchBuffer = 0;
    gfx::ComputeBuffer _texBuffer = 0;
    gfx::ComputeBuffer _frameBuffer = 0;
    uint32_t _batchCapacity = 0;
    uint32_t _texCapacity = 0;
    uint32_t _frameCapacity = 0;

    // The surfaces the waiting draws go into, and their layout.
    std::shared_ptr<Surface> _target;
    std::shared_ptr<Surface> _depth;
    Draw::PixelConfig _surfaces;
    // The batch being built: state records, then triangles and their spans.
    std::vector<uint32_t> _states;
    std::vector<uint32_t> _lastState; // the last record's words, to reuse it
    uint32_t _lastStateAt = 0;
    std::vector<uint32_t> _prims;
    std::vector<uint32_t> _spans;
    std::vector<std::array<int, 4>> _bounds; // each triangle's x0, y0, x1, y1
    // Texture memory: its words as uploaded, and where each texture is.
    std::vector<uint32_t> _texWords;
    uint32_t _texUploaded = 0; // words already on the GPU
    std::map<const DecodedTexture *, TextureSlot> _textures;
};

} // namespace reluva::x86::dx
