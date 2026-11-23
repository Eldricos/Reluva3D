// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/directx/device.h"
#include "emulation/x86/directx/internal.h"
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
 *
 * The surfaces drawn into are kept on the GPU between batches: before a
 * batch, the rows the program changed since are sent; after it, nothing
 * comes back until sync(), which brings back the rows the GPU drew.
 * Whatever reads or writes the surfaces' memory has to sync first.
 */
class GpuRaster {
public:
    /**
     * One on its own GPU device (gfx::createCompute).
     * @return nullptr, with why in `error`, if there's no GPU or the kernel
     *         can't run on it.
     */
    static std::unique_ptr<GpuRaster> create(std::string &error);
    ~GpuRaster();

    /**
     * Whether it can draw `d`'s pixels: a 16-bit 565 or 32-bit X888 target,
     * a depth buffer of 2 or 4 bytes a pixel, if any.
     */
    static bool canDraw(const Draw &d);
    /** Whether it can clear these: a target as canDraw takes, a depth buffer of 2 or 4 bytes a pixel. */
    static bool canClear(const Surface *target, const Surface *depth);
    /**
     * Adds a draw's triangles (projected, as Draw::rasterize takes them) to
     * those drawn at the next flush. A draw into other surfaces than those
     * waiting flushes them first.
     */
    void add(const Draw &d, const std::vector<std::array<ClipVertex, 3>> &triangles);
    /**
     * Adds a clear, in its turn with the draws: `areas` of the target to
     * `color` (in its format) and of the depth buffer to `depthValue`
     * (either surface null for none).
     * @return False, having done nothing, for a depth buffer alone that
     *         isn't the waiting batch's: the caller clears it, after sync().
     */
    bool clear(const std::shared_ptr<Surface> &target, const std::shared_ptr<Surface> &depth,
               const std::vector<Rect> &areas, uint32_t color, uint32_t depthValue);
    /**
     * Adds a copy of `src`'s `from` onto `to` in `dst`, in its turn with
     * the draws, if `dst` is kept on the GPU: unstretched, in the same
     * format, keyed (by raw values from `keyLow` to `keyHigh`) or not.
     * The source's pixels are taken now.
     * @return False, having done nothing, for any other: the caller copies
     *         it, after sync(&dst).
     */
    bool blit(const Surface &dst, const Surface &src, const Rect &from, const Rect &to, bool keyed, uint32_t keyLow,
              uint32_t keyHigh);
    /**
     * Adds a fill of `to` in `s` with `value` (in its format), in its turn
     * with the draws, if `s` is kept on the GPU: a target, or the depth
     * buffer of the target last drawn.
     * @return False, having done nothing, for any other.
     */
    bool fill(const Surface &s, const Rect &to, uint32_t value);
    /** Whether draws are waiting for flush(). */
    bool pending() const { return !_bounds.empty(); }
    /** Sends what's waiting to the GPU; its pixels stay there. */
    Error flush();
    /** Flushes, then brings what the GPU drew back into the surfaces' memory. */
    Error sync();
    /** The same for one surface (if it's kept on the GPU), before its pixels are read or written. */
    Error sync(const Surface *s);

private:
    explicit GpuRaster(std::unique_ptr<gfx::Compute> compute) : _gpu(std::move(compute)) {}
    // Where a texture's levels are in texture memory, as last uploaded.
    struct TextureSlot {
        std::vector<uint64_t> versions;
        std::vector<std::array<uint32_t, 4>> levels; // base, key base, width, height
    };
    // A surface kept on the GPU: its memory, its copy there, and its words
    // as the two last agreed on.
    struct Resident {
        std::weak_ptr<Surface> surface;
        uint8_t *at = nullptr;
        int width = 0;
        int height = 0;
        int pitch = 0;
        uint32_t bytes = 0;
        gfx::ComputeBuffer buffer = 0;
        bool sent = false;            // its copy there made
        std::vector<uint32_t> shadow; // a word a pixel
        int drawnTop = 0;             // the rows the GPU drew since the last sync
        int drawnBottom = -1;
    };
    const TextureSlot &textureSlot(const DecodedTexture &t);
    uint32_t stateRecord(const Draw &d);
    void addTriangle(const Draw &d, uint32_t state, const ClipVertex &a, const ClipVertex &b, const ClipVertex &c);
    Error ensureBuffer(gfx::ComputeBuffer &buffer, uint32_t &capacity, uint32_t bytes);
    // The record for `s` (its memory as it is now), made if new; or found.
    Resident *resident(const std::shared_ptr<Surface> &s);
    Resident *find(const Surface *s);
    // Batches go into these: a new pair flushes what's waiting first.
    void use(Resident *target, Resident *depth);
    Error send(Resident &r);
    Error bringBack(Resident &r);

    std::unique_ptr<gfx::Compute> _gpu;
    gfx::ComputeKernel _kernel = 0;
    gfx::ComputeBuffer _batchBuffer = 0;
    gfx::ComputeBuffer _texBuffer = 0;
    gfx::ComputeBuffer _noDepth = 0; // bound without a depth buffer
    uint32_t _batchCapacity = 0;
    uint32_t _texCapacity = 0;

    std::vector<std::unique_ptr<Resident>> _resident;
    Resident *_target = nullptr; // the waiting batch's
    Resident *_depth = nullptr;
    // The batch being built: state records, then triangles and clears and
    // their spans.
    std::vector<uint32_t> _states;
    std::vector<uint32_t> _lastState; // the last record's words, to reuse it
    uint32_t _lastStateAt = 0;
    std::vector<uint32_t> _prims;
    std::vector<uint32_t> _spans;
    std::vector<std::array<int, 4>> _bounds; // each one's x0, y0, x1, y1
    // Texture memory: its words as uploaded, and where each texture is.
    std::vector<uint32_t> _texWords;
    uint32_t _texUploaded = 0; // words already on the GPU
    std::map<const DecodedTexture *, TextureSlot> _textures;
};

} // namespace reluva::x86::dx
