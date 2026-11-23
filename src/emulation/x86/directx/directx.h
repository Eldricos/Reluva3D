// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace reluva::x86 {

/**
 * DirectDraw and Direct3D 7 for the programs of 1997 to 2001 that use
 * them (DDRAW.dll's DirectDrawCreate, DirectDrawCreateEx and
 * DirectDrawEnumerate, and the objects they lead to), written from
 * Microsoft's DirectX 7 documentation. Surfaces live in the process's
 * memory, as the programs lock them and write to them; Direct3D's drawing
 * is done into them by a software rasterizer. What reaches the screen
 * (flips to, and blits onto, the primary surface) is handed to the engine
 * as a frame.
 */
class DirectX {
public:
    /**
     * Adds DDRAW.dll's functions to `process`. They point into this object,
     * so it has to outlive the program's use of them.
     */
    explicit DirectX(Win32Process &process);
    ~DirectX();

    /**
     * What's on the screen: the primary surface as of the last flip or
     * blit onto it, as 0xAARRGGBB, `width` × `height`.
     */
    struct Frame {
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels;
        uint64_t number = 0; // counts the frames shown
    };
    /** The frame shown last. */
    const Frame &frame() const;
    // Called each time a new frame is shown.
    std::function<void(const Frame &)> presented;
    /**
     * Called each time a program sets a Direct3D transform (SetTransform,
     * MultiplyTransform): which (D3DTRANSFORMSTATE_WORLD 1, VIEW 2,
     * PROJECTION 3), and the matrix it is now, row-major as Direct3D's.
     */
    std::function<void(uint32_t state, const std::array<float, 16> &matrix)> transformSet;
    /** A Direct3D draw, as the program gave it. */
    struct Draw {
        uint32_t primitive = 0; // D3DPRIMITIVETYPE
        uint32_t fvf = 0;
        uint32_t stride = 0;
        std::vector<uint8_t> vertices; // vertexCount * stride bytes, as in memory
        std::vector<uint16_t> indices;
        // The first stage's texture: where its pixels are in the process
        // (telling one texture from another), and its size; 0 for none.
        uint32_t texture = 0;
        int textureWidth = 0;
        int textureHeight = 0;
    };
    // Called for each draw (DrawPrimitive and the like), before it's drawn.
    std::function<void(const Draw &)> drawn;

    /** A display mode offered: its size, and bits per pixel. */
    struct Mode {
        int width;
        int height;
        int bits;
    };
    // The display modes offered; the desktop's comes from the process.
    std::vector<Mode> modes = {{640, 480, 16}, {800, 600, 16}, {1024, 768, 16},
                               {640, 480, 32}, {800, 600, 32}, {1024, 768, 32}};
    // Video memory reported (bytes): what's free is this less the surfaces
    // made in it.
    uint32_t videoMemory = 32u << 20;
    /**
     * How a draw's triangles are drawn: on this thread (kOne), or by rows on
     * as many threads as there are processors, each at its own pace (kAuto
     * and kAlways, which do the same). What's drawn is the same either way.
     */
    enum class DrawThreads { kOne, kAuto, kAlways } drawThreads = DrawThreads::kAuto;
    // Frames shown a flip late: at a flip the frame before is shown, its
    // drawing finished by then, and this one's drawing goes on while the
    // program works out the next. The program needn't wait for the drawing
    // unless it reads what's drawn. Off, a flip waits for its frame's
    // drawing and shows it at once, which tests and frame counts need.
    bool presentLate = false;

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
