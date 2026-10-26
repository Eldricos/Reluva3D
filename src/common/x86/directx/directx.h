// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/x86/win32.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace reluva::x86 {

// DirectDraw and Direct3D 7 for the programs of 1997 to 2001 that use
// them (DDRAW.dll's DirectDrawCreate, DirectDrawCreateEx and
// DirectDrawEnumerate, and the objects they lead to), written from
// Microsoft's DirectX 7 documentation. Surfaces live in the process's
// memory, as the programs lock them and write to them; Direct3D's drawing
// is done into them by a software rasterizer. What reaches the screen
// (flips to, and blits onto, the primary surface) is handed to the engine
// as a frame.
class DirectX {
public:
    // Adds DDRAW.dll's functions to `process`.
    explicit DirectX(Win32Process& process);
    ~DirectX();

    // What's on the screen: the primary surface as of the last flip or
    // blit onto it, as 0xAARRGGBB, `width` × `height`.
    struct Frame {
        int width = 0, height = 0;
        std::vector<uint32_t> pixels;
        uint64_t number = 0; // counts the frames shown
    };
    const Frame& frame() const;
    // Called each time a new frame is shown.
    std::function<void(const Frame&)> presented;

    // The display modes offered (width, height, bits per pixel); the
    // desktop's comes from the process.
    struct Mode {
        int width, height, bits;
    };
    std::vector<Mode> modes = {{640, 480, 16}, {800, 600, 16}, {1024, 768, 16},
                               {640, 480, 32}, {800, 600, 32}, {1024, 768, 32}};
    // Video memory reported (bytes): what's free is this less the surfaces
    // made in it.
    uint32_t videoMemory = 32u << 20;

    struct State;

private:
    std::unique_ptr<State> s_;
};

} // namespace reluva::x86
