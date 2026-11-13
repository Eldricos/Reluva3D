// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A 3dfx Voodoo board as Glide drives it (docs/common/graphics/glide.md): the
// version-neutral core under the Glide 2 front end (glide2.h) and, later,
// Glide 3's. It holds the board's configuration, the rendering state,
// the frame buffer (16-bit 5:6:5 colour buffers and the 16-bit auxiliary
// buffer for depth or alpha), each TMU's texture memory with its palette
// and NCC tables, and draws through a Renderer: the software reference
// rasterizer (software_renderer.cpp) unless another is set.

#include "graphics/glide/glide_defs.h"
#include "graphics/glide/pixel.h"
#include "graphics/glide/textures.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace reluva::glide {

constexpr int kMaxTmus = 3;

// Which Voodoo (docs/common/graphics/glide.md, Boards).
enum class Generation { kVoodoo1, kVoodooRush, kVoodoo2, kBanshee, kVoodoo3 };

// Glide 3's extensions a board's hardware has (Board::extensions).
enum Extension : uint32_t { kExtChromaRange = 1, kExtTexMirror = 2, kExtPalette6666 = 4 };

// The default is the Voodoo2 (voodoo2Board()).
struct Board {
    Generation generation = Generation::kVoodoo2;
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
    int maxWidth = 1024;
    int maxHeight = 768;
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
    // The 2D desktop's share of the frame buffer memory, on a board whose
    // 3D shares one memory with it in a window (the Voodoo Rush: "windowed
    // 3D requires the additional memory space of the 2D desktop", SST-96
    // specification, System Resolutions). desktopBits 16 is the
    // specification's "in-place 3D": the front buffer is the desktop's own
    // window, only the back buffers off screen; 8 is "out-of-place", every
    // colour buffer off screen. 0 for boards with a frame buffer of their
    // own (docs/common/graphics/glide.md, Boards).
    int desktopWidth = 0;
    int desktopHeight = 0;
    int desktopBits = 0;
    // A linear frame buffer lock's scan line in pixels (GrLfbInfo_t's
    // strideInBytes over the pixel size): the Voodoo Graphics' and
    // Voodoo2's 1024-pixel logical scan line, the Banshee's and Voodoo3's
    // 2048 ("all linear frame buffer accesses assume a 2048-pixel logical
    // scan line width"); 0 for the Voodoo Rush, whose buffers are
    // "separate linearly packed buffers" with an X stride in quad pixels
    // (SST-96 specification, Changes from SST-1, colBufferSetup): the
    // buffer's width rounded up to 4 pixels.
    int lfbStride = 1024;
    // What the linear frame buffer takes. The Voodoo Rush's goes "through
    // the 2D engine" (SST-96 specification, Changes from SST-1): no pixel
    // pipeline and no format but the buffers' own, 5:6:5 and the
    // auxiliary buffer's 16 bits (Glide 3.0 Reference Manual,
    // grLfbWriteRegion: "Not all 3Dfx graphics subsystems will support
    // pixel pipeline processing on LFB writes (e.g. Voodoo Rush)";
    // Programming Guide 3.0, chapter 11: "pixel pipe writes or source
    // formats (e.g., Voodoo Rush)").
    bool lfbPixelPipeline = true;
    bool lfbFormats = true;
    // Glide 3's extensions the board's hardware has (Glide 3.0 Programming
    // Guide, chapter 13), each from its specification: kExtChromaRange
    // (the chromaRange register: Voodoo2, Banshee, Voodoo3),
    // kExtTexMirror (textureMode's mirroring: Voodoo2, tLOD's tmirrors and
    // tmirrort: Banshee, Voodoo3), kExtPalette6666 (texture format 6,
    // "8 bit Palette to RGBA": Banshee, Voodoo3). None on the Voodoo
    // Graphics and Rush ("These extensions are not available on systems
    // with Voodoo Graphics and Voodoo Rush hardware").
    uint32_t extensions = kExtChromaRange | kExtTexMirror;
};

// The boards a host can stand in for, each as its specification gives it
// (docs/common/graphics/glide.md, Boards; sizes that vary between cards are
// Reluva's choice of a common one):
// - Voodoo Graphics (SST-1): one TMU of 2 MB, 2 MB of frame buffer (the
//   4 MB card), up to 800x600 (Programming Guide table 3.3).
// - Voodoo Rush (SST-96): one TMU of 2 MB without a palette, 4 MB of frame
//   buffer shared with the 2D desktop (1024x768 at 16 bits, a choice;
//   setDesktop gives it the host's),
//   windowed Glide, no gamma table, the linear frame buffer through the 2D
//   chip.
// - Voodoo2: NFS III's 12 MB card, two TMUs of 4 MB and 4 MB of frame
//   buffer, up to 1024x768.
// - Voodoo Banshee: 16 MB of unified memory ("The minimum amount of memory
//   supported by Banshee is 4 Mbytes, with a maximum of 16"; "The amount
//   of texture memory is only limited by the maximum amount of Banshee
//   frame buffer memory"), one TMU, up to 1600x1200 (its RAMDAC's), the
//   palette with the 6666 format (Banshee specification, Functional
//   Overview, Modifications from SST1).
// - Voodoo3: the same with two TMUs, up to 2048x1536 (a choice).
Board voodoo1Board();
Board voodooRushBoard();
Board voodoo2Board();
Board bansheeBoard();
Board voodoo3Board();
// By name: "voodoo1" (or "voodoo", "sst1"), "rush", "voodoo2", "banshee",
// "voodoo3", then optionally ",tmu=MB", ",fb=MB", ",tmus=N", ",tmurev=N",
// ",desktop=WxHxBITS" (the Rush's 2D desktop as setDesktop takes it,
// applied after the other options; "desktop=0" for none)
// ("voodoo1,tmu=4"); false for another. `desktopClamped`, if given, is set
// when setDesktop didn't take the desktop as given.
bool boardByName(const std::string &spec, Board &out, bool *desktopClamped = nullptr);
// The 2D desktop a windowed board (the Voodoo Rush) shares its memory
// with, as the host's desktop is: width x height at `bits` per pixel (8,
// 15, 16, 24 or 32). The SST-96 specification's windowed tables (System
// Resolutions) have only 8- and 16-bit desktops ("8-bit Desktop ...
// Out-of-Place 3D", "16-bit Desktop ... In-Place 3D", the latter
// "requires equal 2D and 3D color depths"), of 1024x768 to 1600x1200 at
// 8 bits and 1024x768 to 1280x1024 at 16. A desktop a Rush couldn't have
// had is made one it could (a choice, docs/common/graphics/glide.md, Boards): 15
// bits counts as 16, 24 or 32 as 16 (the depth its owner would have set to
// play in a window), a width or height past the tables' largest at that
// depth is cut to it, and one whose memory alone would fill the frame
// buffer drops to the next smaller of the tables' sizes. Smaller desktops
// are taken as they are. Returns false, changing nothing, for another
// depth or a size of 0 or less; sets `clamped` when the desktop isn't
// taken as given. Takes nothing on a board without a shared desktop
// (Board::windowed false), and returns true.
bool setDesktop(Board &board, int width, int height, int bits, bool *clamped = nullptr);

// A vertex as the pipeline takes it, whatever the API's layout.
struct Vertex {
    // pixels, the origin as grSstOrigin says
    float x = 0;
    float y = 0;
    float ooz = 0; // the z buffer's value (0-65535)
    float oow = 1; // 1/w for the w buffer and table fog
    // 0-255
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 0;
    struct Tmu {
        // s/w and t/w (s, t 0-256 across the texture) and 1/w
        float sow = 0;
        float tow = 0;
        float oow = 1;
    };
    std::array<Tmu, kMaxTmus> tmu{};
};

struct CombineUnit {
    CombineSettings settings;
    int local = kLocalIterated;
    int other = kOtherIterated;
};

struct TmuState {
    bool hasSource = false; // grTexSource called
    uint32_t start = 0;     // the source's start address
    int evenOdd = kLevelsBoth;
    TextureInfo info;
    bool multibase = false;
    std::array<uint32_t, 4> multibaseAddress{}; // GR_TEXBASE_256, _128, _64, _32_TO_1
    bool clampS = false;
    bool clampT = false;
    // Glide 3's GR_TEXTURECLAMP_MIRROR_EXT (the TEXMIRROR extension): the
    // texture repeated mirrored, every other copy turned round (Glide 3.0
    // Programming Guide, chapter 13, Figure 13.2; the Banshee's and
    // Voodoo3's tLOD tmirrors and tmirrort). Instead of wrapping.
    bool mirrorS = false;
    bool mirrorT = false;
    int minFilter = kFilterPoint;
    int magFilter = kFilterPoint;
    int mipmapMode = kMipmapDisable;
    bool lodBlend = false;
    float lodBias = 0;
    CombineSettings rgb{kFuncLocal, kFactorZero, false};
    CombineSettings alpha{kFuncLocal, kFactorZero, false};
    int nccTable = 0;
    int detailBias = 0;
    int detailScale = 0;
    float detailMax = 0;
};

struct State {
    CombineUnit color{{kFuncScaleOther, kFactorOne, false}, kLocalIterated, kOtherIterated};
    CombineUnit alpha{{kFuncScaleOther, kFactorOne, false}, kLocalConstant, kOtherConstant};
    bool alphaControlsLighting = false;
    uint32_t constantColor = 0; // 0xAARRGGBB
    int blendRgbSrc = kBlendOne;
    int blendRgbDst = kBlendZero;
    int blendAlphaSrc = kBlendOne;
    int blendAlphaDst = kBlendZero;
    int alphaTest = kCmpAlways;
    int alphaRef = 0;
    bool chromakey = false;
    uint32_t chromakeyColor = 0; // 0x00RRGGBB
    // Glide 3's chroma range (the CHROMARANGE extension, Programming Guide
    // 3.0, chapter 13): with the chroma-key on, a pixel whose "other"
    // colour has every component within [min, max] is dropped, instead of
    // one equal to chromakeyColor (GR_CHROMARANGE_RGB_ALL_EXT, inclusive).
    bool chromaRange = false;
    // 0x00RRGGBB
    uint32_t chromaRangeMin = 0;
    uint32_t chromaRangeMax = 0;
    int cull = kCullDisable;
    int depthMode = kDepthDisable;
    int depthFunc = kCmpLess;
    bool depthMask = false;
    int depthBias = 0; // signed 16-bit
    bool colorMask = true;
    bool alphaMask = true;
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
    uint64_t pixelsIn = 0;
    uint64_t chromaFail = 0;
    uint64_t depthFail = 0;
    uint64_t alphaFail = 0;
    uint64_t pixelsOut = 0;
    uint64_t trianglesIn = 0;
    uint64_t trianglesOut = 0;
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
    virtual void triangle(Pipeline &p, const Vertex &a, const Vertex &b, const Vertex &c) = 0;
    virtual void line(Pipeline &p, const Vertex &a, const Vertex &b) = 0;
    virtual void point(Pipeline &p, const Vertex &a) = 0;
    // Single pixels each with its own values (drawing coordinates), and the
    // gradients for the texture LOD if `derivatives`: the coverage pixels
    // of the anti-aliased primitives (Pipeline::aaTriangle...).
    virtual void fragments(Pipeline &p, const raster::Fragments &f) = 0;
    virtual void clear(Pipeline &p, uint32_t argb, int alpha, int depth) = 0;
    virtual void sync(Pipeline &) {}
    // The same for one buffer (kBufferFront...), when only it is read (a
    // presented frame).
    virtual void syncBuffer(Pipeline &p, int /*buffer*/) { sync(p); }
    // A TMU's texture memory changed, `bytes` from `start`.
    virtual void textureMemoryChanged(Pipeline &, int /*tmu*/, uint32_t /*start*/, uint32_t /*bytes*/) {}
    // A TMU's palette or NCC tables changed.
    virtual void tablesChanged(Pipeline &, int /*tmu*/) {}
    // The front and back buffers were swapped (after the swap).
    virtual void swapped(Pipeline &) {}
    // A pixel was written straight into a buffer (slot, x, y), by the
    // linear frame buffer (Pipeline::lfbWrite), after a sync.
    virtual void hostWrote(Pipeline &, int /*slot*/, int /*x*/, int /*y*/) {}
};

// The software reference rasterizer, drawing into the pipeline's buffers.
std::unique_ptr<Renderer> makeSoftwareRenderer();

class Pipeline {
public:
    explicit Pipeline(Board board = {});
    ~Pipeline();
    Pipeline(Pipeline &&) noexcept = default;
    Pipeline &operator=(Pipeline &&) noexcept = default;

    const Board &board() const { return _board; }
    // The renderer from now on. Call it before drawing (with the buffers
    // and texture memory as they are, a GPU backend copies them over).
    void setRenderer(std::unique_ptr<Renderer> renderer) { _renderer = std::move(renderer); }
    Renderer &renderer() { return *_renderer; }

    // --- The display ------------------------------------------------------
    // Sets up the buffers for a resolution and resets the state to Glide's
    // defaults (grSstWinOpen): false if the board's memory can't hold them.
    // `windowed`: in the application's window on the desktop, on a board
    // whose desktop shares the memory (the Voodoo Rush).
    bool open(int width, int height, int colorBuffers, int auxBuffers, bool windowed = false);
    // The bytes the buffers take, and whether they fit the board's memory
    // (with the desktop's when `windowed`, as the SST-96 specification's
    // tables count it: the desktop, the off-screen colour buffers, the
    // auxiliary buffer).
    uint64_t bufferBytes(int width, int height, int colorBuffers, int auxBuffers, bool windowed) const;
    bool fits(int width, int height, int colorBuffers, int auxBuffers, bool windowed) const;
    // A windowed board's back and auxiliary buffers resized to the window's
    // new size, the state kept but the clip window (the whole window again)
    // (grSstControlMode(GR_CONTROL_RESIZE)): false, nothing changed, if
    // the memory can't hold them ("may fail if there is not enough
    // offscreen video memory").
    bool resize(int width, int height);
    bool windowed() const { return _windowed; }
    // A linear frame buffer lock's scan line, in pixels (Board::lfbStride).
    int lfbStride() const;
    void close();
    bool isOpen() const { return _open; }
    // How many times open() succeeded: new buffers each time.
    uint64_t opened() const { return _opened; }
    int width() const { return _width; }
    int height() const { return _height; }
    int colorBuffers() const { return _colorBuffers; }
    // Swaps the front and back buffers (with three, the oldest becomes the
    // back). Counts swaps for grBufferNumPending's sake: none stay pending.
    void swap();
    uint64_t swaps() const { return _swaps; }

    State &state() { return _state; }
    const State &state() const { return _state; }
    // Glide's defaults for the state (grSstWinOpen), the clip window the
    // screen.
    void resetState();

    // --- Drawing ----------------------------------------------------------
    void triangle(const Vertex &a, const Vertex &b, const Vertex &c);
    void line(const Vertex &a, const Vertex &b);
    void point(const Vertex &a);
    // Anti-aliased primitives (Glide 2's grAADraw*, Glide 3's
    // GR_AA_ORDERED): drawn with the alpha iterator standing for the
    // pixel's coverage (docs/common/graphics/glide.md, Anti-aliasing). A triangle:
    // its pixels as grDrawTriangle draws them, and outside each edge asked
    // for (ab, bc, ca) a fringe whose alpha falls from the vertex alpha to
    // 0 a pixel out. A line: the pixels within a pixel of it, alpha falling
    // with the distance. A point: the four pixels round it, by their share
    // of a pixel-sized square.
    void aaTriangle(const Vertex &a, const Vertex &b, const Vertex &c, bool ab, bool bc, bool ca);
    void aaLine(const Vertex &a, const Vertex &b);
    void aaPoint(const Vertex &a);
    void fragments(const raster::Fragments &f);
    // grBufferClear: the clip window of the render buffer, and of the
    // auxiliary buffer when depth (or alpha) buffering is on and writable.
    void clear(uint32_t argb, int alpha, int depth);

    // --- Textures ---------------------------------------------------------
    uint32_t tmuMemorySize() const { return _board.tmuMemory; }
    // Where texture memory starts: 0, or on a UMA board the first address
    // after the open window's buffers (aligned to the board's texture
    // alignment).
    uint32_t textureBase() const { return _textureBase; }
    // One level (rows firstRow-lastRow of it) of a texture whose largest
    // level `evenOdd` includes starts at `start`, from `data` (the level's
    // rows, tightly packed).
    void downloadLevel(int tmu, uint32_t start, const TextureInfo &info, int evenOdd, int lod, const uint8_t *data,
                       int firstRow, int lastRow);
    void setPalette(int tmu, int first, int last, const uint32_t *entries); // 0x00RRGGBB
    // Glide 3's PALETTE6666 extension: entries `first`-`last` from
    // 0xAARRGGBB words, each channel's top 6 bits packed into the
    // palette's 24 (alpha 23-18, red 17-12, green 11-6, blue 5-0), and the
    // TMU's GR_TEXFMT_P_8 textures read through it as texture format 6,
    // "8 bit Palette to RGBA" (Banshee specification, textureMode's
    // formats). A plain palette download (setPalette) ends it.
    void setPalette6666(int tmu, int first, int last, const uint32_t *entries);
    bool palette6666(int tmu) const { return _palette6666[size_t(tmu)]; }
    // The format a TMU's source is read in: its own, or format 6 for a P_8
    // texture with the 6666 palette.
    int textureFormat(int tmu) const;
    void setNcc(int tmu, int which, const NccTable &table);
    const uint8_t *tmuMemory(int tmu) const { return _tmuMemory[size_t(tmu)].data(); }
    const uint32_t *palette(int tmu) const { return _palettes[size_t(tmu)].data(); }
    const NccTable &ncc(int tmu, int which) const { return _ncc[size_t(tmu)][size_t(which & 1)]; }
    // Where a level of the TMU's current source is (grTexMultibase's
    // addresses if set), its first byte.
    uint32_t levelAddress(int tmu, int lod) const;

    // --- The frame buffer -------------------------------------------------
    // The pixels of a buffer (kBufferFront, kBufferBack, kBufferTriple,
    // kBufferAux), `width()` pixels a row, row 0 the top of the screen;
    // null if there's no such buffer.
    uint16_t *buffer(int id);
    const uint16_t *buffer(int id) const;
    // Where a buffer's pixels are kept: colour buffers in slots 0-2 (a swap
    // changes which slot is the front, not the pixels), the auxiliary
    // buffer in slot 3; -1 if there's no such buffer. A renderer that keeps
    // its own copies (the GPU's) keeps them by slot.
    static constexpr int kSlots = 4;
    int slotOf(int id) const;
    uint16_t *slotPixels(int slot);
    // TMU memory written from outside a download (a renderer's copy, a
    // comparison's), the renderer told.
    void writeTmuMemory(int tmu, uint32_t at, const uint8_t *data, size_t bytes);
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
    void present(Surface &out);
    using GammaTables = std::array<std::array<uint8_t, 256>, 3>; // red, green, blue
    // grLoadGammaTable: the hardware table's first `n` entries per channel
    // (0-255), the rest kept; the table has the board's
    // gammaTableEntries, read by interpolating between them (Glide 3.0
    // Reference Manual: values "interpolated to generate an output gamma
    // value"; the Voodoo2's clutData). A gamma value (state().gamma) set
    // afterwards replaces it (setGammaValue).
    void loadGammaTable(int n, const uint32_t *r, const uint32_t *g, const uint32_t *b);
    void setGammaValue() { _gammaLoaded = false; }
    std::function<void(const uint16_t *pixels, int width, int height, const GammaTables &gamma, Surface &out)>
        outputFilter;

    PixelStats stats;

private:
    Board _board;
    std::unique_ptr<Renderer> _renderer;
    State _state;
    bool _open = false;
    uint64_t _opened = 0;
    int _width = 0;
    int _height = 0;
    int _colorBuffers = 2;
    bool _hasAux = false;
    bool _windowed = false;
    bool _hasAuxAsked = false;
    uint32_t _textureBase = 0;
    bool _gammaLoaded = false;
    std::array<std::vector<uint8_t>, 3> _gammaTable;
    // Colour buffers by slot; front_ and back_ index them.
    std::vector<std::vector<uint16_t>> _color;
    int _front = 0;
    int _back = 1;
    int _triple = 2;
    std::vector<uint16_t> _aux;
    uint64_t _swaps = 0;
    std::vector<std::vector<uint8_t>> _tmuMemory;
    std::vector<std::array<uint32_t, 256>> _palettes;
    std::array<bool, kMaxTmus> _palette6666{};
    std::vector<std::array<NccTable, 2>> _ncc;
};

} // namespace reluva::glide
