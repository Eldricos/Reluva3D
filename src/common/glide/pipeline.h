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
#include <string>
#include <vector>

namespace reluva::glide {

constexpr int kMaxTmus = 3;

// Which Voodoo (docs/engines/glide.md, Boards).
enum class Generation { Voodoo1, VoodooRush, Voodoo2, Banshee, Voodoo3 };

struct Board {
    Generation generation = Generation::Voodoo2;
    int tmus = 2;
    // Bytes each TMU addresses. On a UMA board (Banshee, Voodoo3) every TMU
    // reads the one memory the frame buffer is in too, and this is all of
    // it.
    uint32_t tmuMemory = 4u << 20;
    uint32_t frameBufferMemory = 4u << 20;
    bool uma = false;
    int fbiRevision = 2; // the Voodoo2 reports 2, as the Voodoo Graphics did (its specification, Revision_ID)
    int tmuRevision = 1; // a palette needs revision 1 or later (Glide 2.4 Programming Guide, The Color Palette)
    // Whether the TMUs have the 8-bit palette (GR_TEXFMT_P_8, AP_88): not
    // on TMU revision 0 (Glide 2.4 Programming Guide, "The Color Palette
    // (not implemented in TMU Revision 0)"), nor on the Voodoo Rush (the
    // SST-96 specification's texture formats leave both out).
    bool palette = true;
    bool sli = false;
    int maxWidth = 1024, maxHeight = 768;
    // GR_RESOLUTION_NONE (the application's window) allowed: the Voodoo
    // Rush (SST-96) only (Glide 2.4 Reference Manual, grSstWinOpen).
    bool windowed = false;
    // grGet(GR_TEXTURE_ALIGN): texture base addresses in 8-byte units on
    // the Voodoo2 (its texBaseAddr), 16-byte on the Voodoo3 (its
    // texbaseaddr, "in 16-byte units").
    int textureAlign = 8;
    // The fog table's entries as Glide 3's grGet(GR_FOG_TABLE_ENTRIES)
    // reports them: 64 on the Voodoo Graphics, Voodoo2 and Voodoo3 (each
    // specification's fogTable register). The core keeps 64; a front end
    // resamples a larger table to them.
    int fogTableEntries = 64;
    // grGet(GR_GAMMA_TABLE_ENTRIES): 32 on the Voodoo Graphics and Voodoo2
    // (their clutData register: a 33-entry table, "the 5 MSBs ... used to
    // index", the 33rd closing the last interval), 256 on the Voodoo3 (its
    // "Triple 256x8 lookup tables").
    int gammaTableEntries = 32;
};

// The boards a host can stand in for, each as its specification gives it
// (docs/engines/glide.md, Boards; sizes that vary between cards are
// Reluva's choice of a common one):
// - Voodoo Graphics (SST-1): one TMU of 2 MB, 2 MB of frame buffer (the
//   4 MB card), up to 800x600 (Programming Guide table 3.3).
// - Voodoo Rush (SST-96): one TMU of 2 MB without a palette, 4 MB of frame
//   buffer shared with the 2D desktop, windowed Glide.
// - Voodoo2: NFS III's 12 MB card, two TMUs of 4 MB and 4 MB of frame
//   buffer, up to 1024x768.
// - Voodoo Banshee and Voodoo3: 16 MB of unified memory (the Voodoo3
//   specification: "The amount of texture memory is only limited by the
//   maximum amount of ... frame buffer memory"), one TMU and two.
Board voodoo1Board();
Board voodooRushBoard();
Board voodoo2Board();
Board bansheeBoard();
Board voodoo3Board();
// By name: "voodoo1" (or "voodoo", "sst1"), "rush", "voodoo2", "banshee",
// "voodoo3", then optionally ",tmu=MB", ",fb=MB", ",tmus=N", ",tmurev=N"
// ("voodoo1,tmu=4"); false for another.
bool boardByName(const std::string& spec, Board& out);

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
namespace raster {
struct Fragments; // raster_setup.h
}

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
    // Single pixels each with its own values (drawing coordinates), and the
    // gradients for the texture LOD if `derivatives`: the coverage pixels
    // of the anti-aliased primitives (Pipeline::aaTriangle...).
    virtual void fragments(Pipeline& p, const raster::Fragments& f) = 0;
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
    // Anti-aliased primitives (Glide 2's grAADraw*, Glide 3's
    // GR_AA_ORDERED): drawn with the alpha iterator standing for the
    // pixel's coverage (docs/engines/glide.md, Anti-aliasing). A triangle:
    // its pixels as grDrawTriangle draws them, and outside each edge asked
    // for (ab, bc, ca) a fringe whose alpha falls from the vertex alpha to
    // 0 a pixel out. A line: the pixels within a pixel of it, alpha falling
    // with the distance. A point: the four pixels round it, by their share
    // of a pixel-sized square.
    void aaTriangle(const Vertex& a, const Vertex& b, const Vertex& c, bool ab, bool bc, bool ca);
    void aaLine(const Vertex& a, const Vertex& b);
    void aaPoint(const Vertex& a);
    void fragments(const raster::Fragments& f);
    // grBufferClear: the clip window of the render buffer, and of the
    // auxiliary buffer when depth (or alpha) buffering is on and writable.
    void clear(uint32_t argb, int alpha, int depth);

    // --- Textures ---------------------------------------------------------
    uint32_t tmuMemorySize() const { return board_.tmuMemory; }
    // Where texture memory starts: 0, or on a UMA board the first address
    // after the open window's buffers (aligned to the board's texture
    // alignment).
    uint32_t textureBase() const { return textureBase_; }
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
    // grLoadGammaTable: the hardware table's first `n` entries per channel
    // (0-255), the rest kept; the table has the board's
    // gammaTableEntries, read by interpolating between them (Glide 3.0
    // Reference Manual: values "interpolated to generate an output gamma
    // value"; the Voodoo2's clutData). A gamma value (state().gamma) set
    // afterwards replaces it (setGammaValue).
    void loadGammaTable(int n, const uint32_t* r, const uint32_t* g, const uint32_t* b);
    void setGammaValue() { gammaLoaded_ = false; }
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
    uint32_t textureBase_ = 0;
    bool gammaLoaded_ = false;
    std::array<std::vector<uint8_t>, 3> gammaTable_;
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
