// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/x86/cpu.h"

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace reluva::nfs {

// THRASH's drawing done natively (docs/formats/nfs/thrash.md): the
// triangles, quads, lines and points the game sends, rasterized in C++
// into the 16-bit (5:6:5) surface the software renderer would have drawn
// into. The renderer DLL still keeps its own books (textures, windows,
// page flips); this keeps a copy of each texture as the game gave it.
class NativeThrash {
public:
    explicit NativeThrash(x86::Cpu& cpu) : cpu_(cpu) {}

    // Where drawing goes: 16-bit 5:6:5 pixels in emulated memory.
    void setTarget(uint32_t address, int pitch, int width, int height);

    // Textures, by the renderer DLL's handle for them.
    void talloc(uint32_t handle, int width, int height, int format);
    // `palette`: format 2's 256 colours (0x00RRGGBB), 0 to keep the last.
    void tupdate(uint32_t handle, uint32_t pixels, uint32_t palette);
    void treset() { textures_.clear(); }

    void setState(uint32_t id, uint32_t value);
    void clip(int x0, int y0, int x1, int y1);

    // Vertices are THRASH's 32-byte ones in emulated memory.
    void triangle(uint32_t a, uint32_t b, uint32_t c);
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    void line(uint32_t a, uint32_t b);
    void point(uint32_t a);

    uint64_t pixels() const { return pixels_; }
    // Textures of a format not read yet, for reports.
    const std::map<int, int>& unknownFormats() const { return unknownFormats_; }

private:
    struct Vertex {
        float x, y, z, rhw;
        uint32_t diffuse, specular;
        float u, v;
    };
    struct Texture {
        int width = 0, height = 0, format = 0;
        bool transparent = false; // has texels to leave out
        std::vector<uint32_t> rgba; // 0xAARRGGBB
        std::vector<uint8_t> indices; // format 2's, as given
        std::array<uint32_t, 256> palette{};
    };
    Vertex vertex(uint32_t address) const;
    void rasterize(const Vertex& a, const Vertex& b, const Vertex& c);
    void plot(int x, int y, uint32_t argb);
    void put(uint8_t* pixel, uint32_t argb); // blends as the state says

    x86::Cpu& cpu_;
    uint32_t target_ = 0;
    int pitch_ = 0, width_ = 0, height_ = 0;
    int clipX0_ = 0, clipY0_ = 0, clipX1_ = 640, clipY1_ = 480;
    std::map<uint32_t, Texture> textures_;
    const Texture* texture_ = nullptr;
    uint32_t textureHandle_ = 0;
    int cull_ = 0;       // state 2: 0 none, 1 counter-clockwise, 2 clockwise
    bool gouraud_ = true; // state 6
    bool wrap_ = true;    // state 0xd: 1 wrap, 0 clamp
    int blend_ = 0;       // state 0x68: 0 alpha, 1 additive, 2 darken, 3 multiply
    uint64_t pixels_ = 0;
    std::map<int, int> unknownFormats_;
};

} // namespace reluva::nfs
