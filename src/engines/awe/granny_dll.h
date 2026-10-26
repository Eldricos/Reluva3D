// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace reluva::awe {

// The game's own granny2.dll, run in the x86 interpreter, for what its
// files need that Reluva doesn't do natively yet: decompressing their
// sections (Oodle1) and their Bink-compressed textures. Only two of its
// exports are called, with the arguments its public header documents.
class GrannyDll {
public:
    Error load(const std::vector<uint8_t>& dll);
    bool loaded() const { return module_ != nullptr; }

    // Decompresses `in` into `out` (sized to the decompressed size).
    Error decompress(uint32_t format, const std::vector<uint8_t>& in, uint32_t stop0, uint32_t stop1,
                     std::vector<uint8_t>& out);
    // Decodes a texture stored with Bink encoding (Encoding 3) into RGBA
    // pixels, rows top to bottom. `flags` is the texture's SubFormat, plus
    // 1 if its layout has alpha.
    Error binkTexture(int width, int height, uint32_t flags, const std::vector<uint8_t>& in,
                      std::vector<uint8_t>& rgba);

    // A granny_camera as the game sets one up from a scene's CAMR record:
    // vertical field of view (radians), near and far planes, the point it
    // looks at, its elevation, azimuth and roll (radians), and its offset
    // from that point. Gives its view and projection matrices (Granny's,
    // column-major, as gfx::Mat4) for a width x height window, and
    // optionally where `points` (x, y, z each) land in the window (x, y in
    // pixels, z depth).
    struct Camera {
        float fovY = 0.785f, nearPlane = 4, farPlane = 1000;
        float position[3] = {0, 0, 0}, elevAzimRoll[3] = {0, 0, 0}, offset[3] = {0, 0, 0};
    };
    Error camera(const Camera& in, int width, int height, float view[16], float projection[16],
                 const std::vector<float>* points = nullptr, std::vector<float>* windowPoints = nullptr);

private:
    std::unique_ptr<x86::Cpu> cpu_;
    std::unique_ptr<x86::Win32Process> process_;
    const x86::Win32Process::Module* module_ = nullptr;
    uint32_t decompressData_ = 0, binkDecompressTexture_ = 0;
};

} // namespace reluva::awe
