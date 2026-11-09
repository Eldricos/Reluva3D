// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A 3dfx Voodoo board as Glide drives it (docs/engines/glide.md): the
// version-neutral core under the Glide 2 front end (glide2.h) and, later,
// Glide 3's. It holds the board's configuration, the rendering state,
// the frame buffer (16-bit 5:6:5 colour buffers and the 16-bit auxiliary
// buffer for depth or alpha), each TMU's texture memory with its palette
// and NCC tables, and draws through a Renderer: the software reference
// rasterizer (software_renderer.cpp) unless another is set.

#include "common/glide/glide_defs.h"
#include "common/glide/pixel.h"
#include "common/glide/textures.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace reluva::glide {

constexpr int kMaxTmus = 3;

// Which Voodoo. The core draws the same for each so far; the field is
// where differences between them belong (docs/engines/glide.md, Extension
// points).
enum class Generation { Voodoo1, VoodooRush, Voodoo2, Banshee, Voodoo3 };

struct Board {
    Generation generation = Generation::Voodoo2;
    int tmus = 2;
    uint32_t tmuMemory = 4u << 20;  // bytes, each TMU
    uint32_t frameBufferMemory = 4u << 20;
    int fbiRevision = 2; // the Voodoo2 reports 2, as the Voodoo Graphics did (its specification, Revision_ID)
    int tmuRevision = 1; // a palette needs revision 1 or later (Glide 2.4 Programming Guide, The Color Palette)
    bool sli = false;
    int maxWidth = 1024, maxHeight = 768;
    // The fog table's entries as Glide 3's grGet(GR_FOG_TABLE_ENTRIES)
    // reports them: 64 on the Voodoo Graphics, Voodoo2 and Voodoo3 (each
    // specification's fogTable register). The core keeps 64; a front end
    // resamples a larger table to them.
    int fogTableEntries = 64;
    // grGet(GR_GAMMA_TABLE_ENTRIES): Reluva's choice, 32 (the documents
    // give no number for these boards).
    int gammaTableEntries = 32;
};

// The boards a host can stand in for: NFS III's Voodoo2 (12 MB: 4 MB of
// frame buffer, two TMUs of 4 MB) and a Voodoo3 for Glide 3 programs
// (16 MB; how Reluva splits it is its own choice, docs/engines/glide.md).
Board voodoo2Board();
Board voodoo3Board();

// A vertex as the pipeline takes it, whatever the API's layout.
struct Vertex {
    float x = 0, y = 0; // pixels, the origin as grSstOrigin says
    float ooz = 0;      // the z buffer's value (0-65535)
    float oow = 1;      // 1/w for the w buffer and table fog
    float r = 0, g = 0, b = 0, a = 0; // 0-255
    struct Tmu {
        float sow = 0, tow = 0, oow = 1; // s/w and t/w (s, t 0-256 across the texture) and 1/w
    };
    std::array<Tmu, kMaxTmus> tmu{};
};

struct CombineUnit {
    CombineSettings settings;
    int local = kLocalIterated, other = kOtherIterated;
};

struct TmuState {
    bool hasSource = false; // grTexSource called
    uint32_t start = 0;     // the source's start address
    int evenOdd = kLevelsBoth;
    TextureInfo info;
    bool multibase = false;
    std::array<uint32_t, 4> multibaseAddress{}; // GR_TEXBASE_256, _128, _64, _32_TO_1
    bool clampS = false, clampT = false;
    int minFilter = kFilterPoint, magFilter = kFilterPoint;
    int mipmapMode = kMipmapDisable;
    bool lodBlend = false;
    float lodBias = 0;
    CombineSettings rgb{kFuncLocal, kFactorZero, false}, alpha{kFuncLocal, kFactorZero, false};
    int nccTable = 0;
    int detailBias = 0, detailScale = 0;
    float detailMax = 0;
};

struct State {
    CombineUnit color{{kFuncScaleOther, kFactorOne, false}, kLocalIterated, kOtherIterated};
    CombineUnit alpha{{kFuncScaleOther, kFactorOne, false}, kLocalConstant, kOtherConstant};
    bool alphaControlsLighting = false;
    uint32_t constantColor = 0; // 0xAARRGGBB
    int blendRgbSrc = kBlendOne, blendRgbDst = kBlendZero, blendAlphaSrc = kBlendOne, blendAlphaDst = kBlendZero;
    int alphaTest = kCmpAlways;
    int alphaRef = 0;
    bool chromakey = false;
    uint32_t chromakeyColor = 0; // 0x00RRGGBB
    int cull = kCullDisable;
    int depthMode = kDepthDisable, depthFunc = kCmpLess;
    bool depthMask = false;
    int depthBias = 0; // signed 16-bit
    bool colorMask = true, alphaMask = true;
    int dither = kDither4x4;
    int fogMode = kFogDisable;
    uint32_t fogColor = 0; // 0x00RRGGBB
    std::array<uint8_t, 64> fogTable{};
    int clip[4] = {0, 0, 640, 480}; // x0, y0, x1, y1: the first inclusive, the second exclusive
    int renderBuffer = kBufferBack;
    bool originLowerLeft = false;
    uint32_t stwHints = 0;
    int lfbConstantAlpha = 0xff;
    int lfbConstantDepth = 0;
    // The video's gamma per channel (red, green, blue): Glide 2's
    // grGammaCorrectionValue sets all three, Glide 3's guGammaCorrectionRGB
    // each.
    std::array<float, 3> gamma{1.0f, 1.0f, 1.0f};
    std::array<TmuState, kMaxTmus> tmus{};
};

// Statistics the hardware counts (grSstPerfStats).
struct PixelStats {
    uint64_t pixelsIn = 0, chromaFail = 0, depthFail = 0, alphaFail = 0, pixelsOut = 0;
    uint64_t trianglesIn = 0, trianglesOut = 0;
};

class Pipeline;

// What draws: the software reference rasterizer, or later another (a GPU
// backend). It reads the pipeline's state, buffers and texture memory, and
// writes the buffers; sync() is called before the buffers are read or
// written from outside (a linear frame buffer lock, a presented frame).
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void triangle(Pipeline& p, const Vertex& a, const Vertex& b, const Vertex& c) = 0;
    virtual void line(Pipeline& p, const Vertex& a, const Vertex& b) = 0;
    virtual void point(Pipeline& p, const Vertex& a) = 0;
    virtual void clear(Pipeline& p, uint32_t argb, int alpha, int depth) = 0;
    virtual void sync(Pipeline&) {}
    // The same for one buffer (kBufferFront...), when only it is read (a
    // presented frame).
    virtual void syncBuffer(Pipeline& p, int /*buffer*/) { sync(p); }
    // A TMU's texture memory changed, `bytes` from `start`.
    virtual void textureMemoryChanged(Pipeline&, int /*tmu*/, uint32_t /*start*/, uint32_t /*bytes*/) {}
    // A TMU's palette or NCC tables changed.
    virtual void tablesChanged(Pipeline&, int /*tmu*/) {}
    // The front and back buffers were swapped (after the swap).
    virtual void swapped(Pipeline&) {}
    // A pixel was written straight into a buffer (slot, x, y), by the
    // linear frame buffer (Pipeline::lfbWrite), after a sync.
    virtual void hostWrote(Pipeline&, int /*slot*/, int /*x*/, int /*y*/) {}
};

// The software reference rasterizer, drawing into the pipeline's buffers.
std::unique_ptr<Renderer> makeSoftwareRenderer();

class Pipeline {
public:
    explicit Pipeline(Board board = {});
    ~Pipeline();
    Pipeline(Pipeline&&) noexcept = default;
    Pipeline& operator=(Pipeline&&) noexcept = default;

    const Board& board() const { return board_; }
    // The renderer from now on. Call it before drawing (with the buffers
    // and texture memory as they are, a GPU backend copies them over).
    void setRenderer(std::unique_ptr<Renderer> renderer) { renderer_ = std::move(renderer); }
    Renderer& renderer() { return *renderer_; }

    // --- The display ------------------------------------------------------
    // Sets up the buffers for a resolution and resets the state to Glide's
    // defaults (grSstWinOpen): false if the board's memory can't hold them.
    bool open(int width, int height, int colorBuffers, int auxBuffers);
    void close();
    bool isOpen() const { return open_; }
    // How many times open() succeeded: new buffers each time.
    uint64_t opened() const { return opened_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int colorBuffers() const { return colorBuffers_; }
    // Swaps the front and back buffers (with three, the oldest becomes the
    // back). Counts swaps for grBufferNumPending's sake: none stay pending.
    void swap();
    uint64_t swaps() const { return swaps_; }

    State& state() { return state_; }
    const State& state() const { return state_; }
    // Glide's defaults for the state (grSstWinOpen), the clip window the
    // screen.
    void resetState();

    // --- Drawing ----------------------------------------------------------
    void triangle(const Vertex& a, const Vertex& b, const Vertex& c);
    void line(const Vertex& a, const Vertex& b);
    void point(const Vertex& a);
    // grBufferClear: the clip window of the render buffer, and of the
    // auxiliary buffer when depth (or alpha) buffering is on and writable.
    void clear(uint32_t argb, int alpha, int depth);

    // --- Textures ---------------------------------------------------------
    uint32_t tmuMemorySize() const { return board_.tmuMemory; }
    // One level (rows firstRow-lastRow of it) of a texture whose largest
    // level `evenOdd` includes starts at `start`, from `data` (the level's
    // rows, tightly packed).
    void downloadLevel(int tmu, uint32_t start, const TextureInfo& info, int evenOdd, int lod, const uint8_t* data,
                       int firstRow, int lastRow);
    void setPalette(int tmu, int first, int last, const uint32_t* entries); // 0x00RRGGBB
    void setNcc(int tmu, int which, const NccTable& table);
    const uint8_t* tmuMemory(int tmu) const { return tmuMemory_[size_t(tmu)].data(); }
    const uint32_t* palette(int tmu) const { return palettes_[size_t(tmu)].data(); }
    const NccTable& ncc(int tmu, int which) const { return ncc_[size_t(tmu)][size_t(which & 1)]; }
    // Where a level of the TMU's current source is (grTexMultibase's
    // addresses if set), its first byte.
    uint32_t levelAddress(int tmu, int lod) const;

    // --- The frame buffer -------------------------------------------------
    // The pixels of a buffer (kBufferFront, kBufferBack, kBufferTriple,
    // kBufferAux), `width()` pixels a row, row 0 the top of the screen;
    // null if there's no such buffer.
    uint16_t* buffer(int id);
    const uint16_t* buffer(int id) const;
    // Where a buffer's pixels are kept: colour buffers in slots 0-2 (a swap
    // changes which slot is the front, not the pixels), the auxiliary
    // buffer in slot 3; -1 if there's no such buffer. A renderer that keeps
    // its own copies (the GPU's) keeps them by slot.
    static constexpr int kSlots = 4;
    int slotOf(int id) const;
    uint16_t* slotPixels(int slot);
    // TMU memory written from outside a download (a renderer's copy, a
    // comparison's), the renderer told.
    void writeTmuMemory(int tmu, uint32_t at, const uint8_t* data, size_t bytes);
    // Writes one pixel the linear frame buffer way (grLfbLock's pointer,
    // grLfbWriteRegion): `value` in write mode `mode` (32-bit modes in
    // ARGB order), at (x, y) from the top left. Through the pixel pipeline
    // (blending, fog, depth test...) if `pipeline`, else straight into the
    // buffer (dithered for the 24-bit modes, as the specification says).
    void lfbWrite(int buffer, int x, int y, int mode, uint32_t value, bool pipeline);
    // Makes sure what the renderer drew is in the buffers (a GPU backend's
    // frames copied back), before they are read or written directly.
    void sync();

    // --- Output -----------------------------------------------------------
    // The front buffer as the monitor would show it: widened to 8 bits a
    // channel, through the gamma table. outputFilter, if set, takes over
    // the conversion (a later Voodoo2/3 video filter belongs there).
    void present(Surface& out);
    using GammaTables = std::array<std::array<uint8_t, 256>, 3>; // red, green, blue
    std::function<void(const uint16_t* pixels, int width, int height, const GammaTables& gamma, Surface& out)>
        outputFilter;

    PixelStats stats;

private:
    Board board_;
    std::unique_ptr<Renderer> renderer_;
    State state_;
    bool open_ = false;
    uint64_t opened_ = 0;
    int width_ = 0, height_ = 0, colorBuffers_ = 2;
    bool hasAux_ = false;
    // Colour buffers by slot; front_ and back_ index them.
    std::vector<std::vector<uint16_t>> color_;
    int front_ = 0, back_ = 1, triple_ = 2;
    std::vector<uint16_t> aux_;
    uint64_t swaps_ = 0;
    std::vector<std::vector<uint8_t>> tmuMemory_;
    std::vector<std::array<uint32_t, 256>> palettes_;
    std::vector<std::array<NccTable, 2>> ncc_;
};

} // namespace reluva::glide
