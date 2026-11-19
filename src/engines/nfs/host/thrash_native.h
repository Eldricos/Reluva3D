// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/cpu.h"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace reluva::nfs {

// A frame of THRASH's drawing as a list, for drawing on the GPU: screen
// space triangles in batches of one state, in the order drawn.
struct ThrashFrame {
    struct Vertex {
        // pixels, and 1/w for perspective-correct texturing
        float x;
        float y;
        float z;
        float rhw;
        uint32_t diffuse; // 0xAARRGGBB
        float u;
        float v;
    };
    struct Batch {
        uint32_t texture = 0; // the renderer DLL's handle; 0 for none
        bool wrap = true;
        bool blend = false; // alpha blending on (state 0xa), as `blendMode` (state 0x68) says
        int blendMode = 0;
        int cull = 0; // state 2
        // The texture filter: -1 as the page draws (ThrashGpu::nearest),
        // 0 point-sampled, 1 bilinear. Set only for d3da's device
        // (NativeThrash::setDirect3D), from state 7.
        int filter = -1;
        // Fog (states 0xe, 0xf, 0x15): what 0xe gave, 0 for none, and the
        // colour (0x00RRGGBB). See glideFog().
        float fog = 0;
        uint32_t fogColour = 0;
        int clip[4] = {0, 0, 0, 0}; // x0, y0, x1, y1 (exclusive)
        // vertices, three a triangle
        uint32_t first = 0;
        uint32_t count = 0;
    };
    struct Texture {
        uint32_t handle = 0;
        int width = 0;
        int height = 0;
        std::vector<uint32_t> rgba; // 0xAARRGGBB
    };
    int width = 640;
    int height = 480;
    std::vector<Vertex> vertices;
    std::vector<Batch> batches;
    bool resetTextures = false;    // all textures before these gone (treset)
    std::vector<Texture> textures; // made or changed since the last frame
    // The rectangle a page flip shows of it (x0, y0, x1, y1): outside it,
    // the screen keeps what was there.
    int flip[4] = {0, 0, 640, 480};
};

// How much fog covers what's at w (a vertex's 1/rhw), 0 to 1, for the
// value setstate(0xe) gave: the curve voodoo2a.dll builds Glide's fog
// table from (docs/engines/nfs/games/nfs3/systems/thrash.md, Fog). 0 gives none.
float glideFog(float w, float value);

// THRASH's drawing done natively (docs/engines/nfs/games/nfs3/systems/thrash.md): the
// triangles, quads, lines and points the game sends, rasterized in C++
// into the 16-bit (5:6:5) surface the software renderer would have drawn
// into. The renderer DLL still keeps its own books (textures, windows,
// page flips); this keeps a copy of each texture as the game gave it.
class NativeThrash {
public:
    explicit NativeThrash(x86::Cpu &cpu) : _cpu(cpu) {}

    // Where drawing goes: 16-bit 5:6:5 pixels in emulated memory.
    void setTarget(uint32_t address, int pitch, int width, int height);

    // Textures, by the renderer DLL's handle for them.
    void talloc(uint32_t handle, int width, int height, int format);
    // `palette`: format 2's 256 colours (0x00RRGGBB), 0 to keep the last.
    // Returns what softtria's does: the handle, or 0 for formats 3 and 4
    // given no pixels.
    uint32_t tupdate(uint32_t handle, uint32_t pixels, uint32_t palette);
    void treset() {
        _textures.clear();
        _changed.clear();
        _resetTextures = true;
    }
    // Draws into a list rather than into memory, from now on.
    void setRecording(bool on) { _recording = on; }
    // Draws into memory too while recording (to compare the two).
    void setAlsoRasterize(bool on) { _alsoRasterize = on; }
    // A filled rectangle (a clear), into the list.
    void fill(int x0, int y0, int x1, int y1, uint32_t argb);
    // The list drawn since the last call, with the textures changed.
    ThrashFrame takeFrame();

    void setState(uint32_t id, uint32_t value);
    // Draws as d3da.dll sets up its Direct3D device and keeps it
    // (docs/engines/nfs/games/nfs3/systems/thrash.md, d3da's device defaults), for a game told
    // its renderer is d3da: textures bilinear-filtered (state 7 picks: 0
    // point-sampled, 1 bilinear; nothing else changes it), no dithering
    // (d3da's setstate ignores state 5). Fog, the blend modes and the
    // texture blend (modulated, colour and alpha) are as without it: the
    // game sends d3da no fog, and the rest is the same.
    void setDirect3D(bool on);
    bool direct3D() const { return _direct3D; }
    // Textures are bilinear-filtered, as both DLLs ask for (voodoo2a.dll
    // sends itself state 7 1 after setvideomode, 0x6000208f; d3da's device
    // starts LINEAR): thrash.md, Texture filtering. With this on they are
    // point-sampled instead (reluva.ini's texturefilter=point); in the
    // Direct3D look state 7 still picks.
    void setPointSampled(bool on);
    void clip(int x0, int y0, int x1, int y1);

    // Vertices are THRASH's 32-byte ones in emulated memory.
    void triangle(uint32_t a, uint32_t b, uint32_t c);
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    void line(uint32_t a, uint32_t b);
    void point(uint32_t a);

    uint64_t pixels() const { return _pixels; }
    // Textures of a format not read yet, for reports.
    const std::map<int, int> &unknownFormats() const { return _unknownFormats; }

private:
    struct Vertex {
        float x;
        float y;
        float z;
        float rhw;
        uint32_t diffuse;
        uint32_t specular;
        float u;
        float v;
    };
    struct Texture {
        int width = 0;
        int height = 0;
        int format = 0;
        bool transparent = false;     // has texels to leave out
        std::vector<uint32_t> rgba;   // 0xAARRGGBB
        std::vector<uint8_t> indices; // format 2's, as given
        std::array<uint32_t, 256> palette{};
    };
    Vertex vertex(uint32_t address) const;
    // A texel at (fu, fv) in texels, wrapped or clamped: the nearest, or
    // bilinear-filtered.
    static uint32_t samplePoint(const Texture &t, float fu, float fv, bool wrap);
    static uint32_t sampleBilinear(const Texture &t, float fu, float fv, bool wrap);
    void rasterize(const Vertex &a, const Vertex &b, const Vertex &c);
    void record(const Vertex &a, const Vertex &b, const Vertex &c);
    // The batch to add to, a new one if the state changed.
    ThrashFrame::Batch &batch();
    void plot(int x, int y, uint32_t argb);
    // Blends as the state says, then to 5:6:5 (dithered at (x, y) while
    // dithering is on).
    void put(uint8_t *pixel, uint32_t argb, int x, int y);

    x86::Cpu &_cpu;
    uint32_t _target = 0;
    int _pitch = 0;
    int _width = 0;
    int _height = 0;
    int _clipX0 = 0;
    int _clipY0 = 0;
    int _clipX1 = 640;
    int _clipY1 = 480;
    std::map<uint32_t, Texture> _textures;
    const Texture *_texture = nullptr;
    uint32_t _textureHandle = 0;
    int _cull = 0; // state 2: 0 none, 1 counter-clockwise, 2 clockwise
    // State 5: dithering. voodoo2a.dll's setstate 5 calls grDitherMode(2)
    // (4x4) for a value not 0, else grDitherMode(0); Glide starts at 4x4.
    bool _dither = true;
    bool _gouraud = true;       // state 6
    bool _direct3D = false;     // d3da's device (setDirect3D)
    bool _bilinear = true;      // textures filtered: both DLLs' default, and d3da's state 7
    bool _pointSampled = false; // held point-sampled (setPointSampled)
    bool _wrap = true;          // state 0xd: 1 wrap, 0 clamp
    int _blend = 0;             // state 0x68: 0 alpha, 1 additive, 2 darken, 3 multiply
    bool _alphaBlend = false;   // state 0xa
    // Fog: on or off as the last of states 0xe and 0x15 said, with the
    // last nonzero 0xe's table (Glide's 64 entries, of 255) and 0xf's colour.
    bool _fogOn = false;
    float _fogValue = 0;
    std::array<float, 64> _fogTable{};
    uint32_t _fogColour = 0;
    float fogAt(float w) const; // fogTable_ read as Glide reads it, 0 to 1
    bool _recording = false;
    bool _alsoRasterize = false;
    ThrashFrame _frame;
    std::set<uint32_t> _changed; // textures made or updated since the last frame
    bool _resetTextures = false;
    uint64_t _pixels = 0;
    std::map<int, int> _unknownFormats;
};

} // namespace reluva::nfs
