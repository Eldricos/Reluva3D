// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Glide 2 (glide2x.dll's API) on the pipeline core (docs/common/graphics/glide.md):
// each function with Glide 2.x's meaning, as the Glide 2.4 Reference
// Manual and Programming Guide describe it, taking host values and
// pointers. glide2_x86.h puts a guest's calls through it. The names drop
// the "gr" (grDrawTriangle is drawTriangle); the utility functions keep
// "gu".

#include "graphics/glide/pipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace reluva::glide {

/**
 * Glide 2's GrVertex with two TMUs' coordinates (GLIDE_NUM_TMU 2, the
 * build glide2x.dll's callers use: 60 bytes): the fields in the header's
 * order.
 */
struct Glide2Vertex {
    float x = 0;
    float y = 0;
    float z = 0;
    float r = 0;
    float g = 0;
    float b = 0;
    float ooz = 0;
    float a = 0;
    float oow = 0;
    /** One TMU's s/w, t/w and 1/w. */
    struct Tmu {
        float sow = 0;
        float tow = 0;
        float oow = 0;
    } tmuvtx[2];
};
static_assert(sizeof(Glide2Vertex) == 60, "GrVertex is 60 bytes");

/**
 * GrTexInfo without its data pointer, in Glide 2's LOD and aspect constants
 * (GR_LOD_256 is 0, GR_ASPECT_8x1 is 0).
 */
struct Glide2TexInfo {
    int smallLod = 0;
    int largeLod = 0;
    int aspectRatio = 3; // 1x1
    int format = 0;
};

/** What grSstQueryHardware reports, for one board. */
struct Glide2HwConfig {
    int numSst = 1;
    int type = 3; // GR_SSTTYPE_Voodoo2
    int fbRamMb = 4;
    int fbiRev = 2;
    int nTexelfx = 2;
    bool sliDetect = false;
    int tmuRev[2] = {1, 1};
    int tmuRamMb[2] = {4, 4};
};

/** A linear frame buffer lock (grLfbLock's GrLfbInfo_t, less the pointer). */
struct Glide2LfbLock {
    int buffer = kBufferBack;
    int writeMode = kLfb565;
    bool originLowerLeft = false;
    bool pixelPipeline = false;
    int strideBytes = 2048; // the Voodoo's 1024-pixel logical scan line
    int bytesPerPixel = 2;
};

/**
 * Glide 2's functions on a pipeline of their own, named as this file's
 * opening comment says. Colours are in the window's colour format
 * (grSstWinOpen's) unless a function says otherwise.
 */
class Glide2 {
public:
    /** Glide 2 for the board, its window not yet open. */
    explicit Glide2(Board board = {});

    /** The pipeline it draws on. */
    Pipeline &pipeline() { return _pipeline; }
    // Lines for a log: each function not stood in for, the first time.
    std::function<void(const std::string &line)> log;
    std::string logName = "glide2x"; // the library its lines are from
    // A window's client size, for grSstWinOpen(GR_RESOLUTION_NONE) on a
    // board that allows it (the Voodoo Rush); unset, 640x480.
    std::function<bool(uint32_t hwnd, int &width, int &height)> windowSize;
    // Called after each grBufferSwap, the new front buffer ready for
    // pipeline().present().
    std::function<void()> onSwap;
    /**
     * Whether the front buffer (the one shown) was drawn into, cleared or
     * written through a lock since the last call or swap: a host shows it
     * again then, as the monitor would (a program can draw straight to it).
     */
    bool takeFrontChanged() {
        const bool changed = _frontChanged;
        _frontChanged = false;
        return changed;
    }
    /** Notes, once, that `function` was called but isn't (fully) carried out. */
    void notImplemented(const std::string &function, const std::string &what = "not implemented");

    // Start-up and shut-down.

    /** grGlideInit, which has nothing to do, and grGlideShutdown, which closes the window. */
    ///@{
    void glideInit();
    void glideShutdown();
    ///@}
    /** grGlideGetVersion's string: a Voodoo2's Glide 2.4x, as NFS III's driver wants. */
    std::string glideGetVersion() const;
    /** grSstQueryBoards: always one board. */
    bool sstQueryBoards(Glide2HwConfig &out) const;
    /** grSstQueryHardware: the board's configuration. */
    bool sstQueryHardware(Glide2HwConfig &out) const;
    /** grSstSelect: there's only the one board. */
    void sstSelect(int which);
    /**
     * grSstWinOpen, opening the pipeline; `refresh` is ignored.
     * @param resolution A GR_RESOLUTION_* constant, or kResolutionNone for
     *        the window's own size (windowSize) on a board with windowed
     *        Glide.
     * @return False if it's already open, the resolution isn't one the
     *         board takes, or its memory can't hold the buffers.
     */
    bool sstWinOpen(uint32_t hwnd, int resolution, int refresh, int colorFormat, int origin, int colorBuffers,
                    int auxBuffers);
    /** grSstWinClose. */
    void sstWinClose();
    /**
     * grSstControl/grSstControlMode: GR_CONTROL_ACTIVATE, DEACTIVATE,
     * RESIZE (a windowed Rush's buffers to its window's size, by
     * windowSize), MOVE. False only when a resize fails.
     */
    bool sstControl(uint32_t code);
    /** Whether the window is open. */
    bool sstIsOpen() const { return _pipeline.isOpen(); }
    /** Whether GR_CONTROL_ACTIVATE (the default) or DEACTIVATE was the last asked. */
    bool active() const { return _active; }
    /** grSstScreenWidth and grSstScreenHeight: the window's size in pixels. */
    ///@{
    uint32_t sstScreenWidth() const;
    uint32_t sstScreenHeight() const;
    ///@}
    /** grSstStatus: always idle, with empty FIFOs. */
    uint32_t sstStatus() const;
    /**
     * grSstVRetraceOn: false and true by turns, so that a program waiting
     * for the retrace to begin or to end moves on.
     */
    bool sstVRetraceOn();
    /** grSstVideoLine: a made-up line on the screen, changing with each call. */
    uint32_t sstVideoLine();
    /** grSstOrigin: 1 for the lower left, else the upper left. */
    void sstOrigin(int origin);
    /** The width and height a GR_RESOLUTION_* constant names (false if none). */
    static bool resolutionSize(int resolution, int &width, int &height);
    static constexpr int kResolutionNone = 0xff; // GR_RESOLUTION_NONE

    // Drawing.

    /** grDrawTriangle, grDrawLine and grDrawPoint. */
    ///@{
    void drawTriangle(const Glide2Vertex &a, const Glide2Vertex &b, const Glide2Vertex &c);
    void drawLine(const Glide2Vertex &a, const Glide2Vertex &b);
    void drawPoint(const Glide2Vertex &a);
    ///@}
    /** grDrawPolygon: a convex polygon, a fan from its first vertex. */
    void drawPolygon(int count, const Glide2Vertex *vertices);
    /**
     * Anti-aliased (grAADrawTriangle with the edges to anti-alias,
     * grAADrawLine, grAADrawPoint; Pipeline::aaTriangle says how).
     */
    ///@{
    void aaDrawTriangle(const Glide2Vertex &a, const Glide2Vertex &b, const Glide2Vertex &c, bool ab, bool bc, bool ca);
    void aaDrawLine(const Glide2Vertex &a, const Glide2Vertex &b);
    void aaDrawPoint(const Glide2Vertex &a);
    ///@}
    /**
     * grAADrawPolygon(VertexList): a fan with its outside edges
     * anti-aliased ("All exterior polygon edges are anti-aliased").
     */
    void aaDrawPolygon(int count, const Glide2Vertex *vertices);
    /**
     * The same with vertices already in the pipeline's form (Glide 3's
     * layouts), noted for takeFrontChanged as the others are.
     */
    ///@{
    void triangle(const Vertex &a, const Vertex &b, const Vertex &c);
    void line(const Vertex &a, const Vertex &b);
    void point(const Vertex &a);
    void aaTriangle(const Vertex &a, const Vertex &b, const Vertex &c, bool ab, bool bc, bool ca);
    void aaLine(const Vertex &a, const Vertex &b);
    void aaPoint(const Vertex &a);
    ///@}
    /**
     * A fan of `count` vertices, its outside edges anti-aliased: all of
     * them, or with `edges`, edge i (vertex i to i + 1, the last back to 0)
     * where edges[i] is set.
     */
    void aaPolygon(int count, const Vertex *vertices, const bool *edges = nullptr);
    /** grBufferClear. */
    void bufferClear(uint32_t color, int alpha, int depth);
    /** grBufferSwap: swaps at once, whatever the interval, then calls onSwap. */
    void bufferSwap(int interval);
    /** grBufferNumPending: none, as swaps are done at once. */
    int bufferNumPending() const { return 0; }
    /** grRenderBuffer: the front or back buffer; anything else is ignored. */
    void renderBuffer(int buffer);

    // State.

    /** The state functions of the same names, grColorCombine to grChromakeyValue. */
    ///@{
    void colorCombine(int function, int factor, int local, int other, bool invert);
    void alphaCombine(int function, int factor, int local, int other, bool invert);
    void alphaControlsITRGBLighting(bool enable);
    void alphaBlendFunction(int rgbSrc, int rgbDst, int alphaSrc, int alphaDst);
    void alphaTestFunction(int function);
    void alphaTestReferenceValue(int value);
    void chromakeyMode(int mode);
    void chromakeyValue(uint32_t color);
    ///@}
    /**
     * Glide 3's CHROMARANGE extension (grChromaRangeModeExt,
     * grChromaRangeExt), on a board that has it. The range's two colours
     * may come in either order; only GR_CHROMARANGE_RGB_ALL_EXT is done,
     * another mode taken as it.
     */
    ///@{
    void chromaRangeMode(bool enable);
    void chromaRange(uint32_t color0, uint32_t color1, int mode);
    ///@}
    /** grClipWindow: values past the screen are held to it. */
    void clipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY);
    /** grColorMask and grConstantColorValue. */
    ///@{
    void colorMask(bool rgb, bool alpha);
    void constantColorValue(uint32_t color);
    ///@}
    /** grConstantColorValue4: each channel 0-255, clamped. */
    void constantColorValue4(float a, float r, float g, float b);
    /** grCullMode, grDepthBiasLevel and grDepthBufferFunction. */
    ///@{
    void cullMode(int mode);
    void depthBiasLevel(int level);
    void depthBufferFunction(int function);
    ///@}
    /**
     * grDepthBufferMode. The compare-to-bias modes are plain z or w
     * buffering on a Voodoo Graphics of Pixelfx revision 1, which hasn't
     * them.
     */
    void depthBufferMode(int mode);
    /** The board it stands in for. */
    const Board &board() const { return _board; }
    /** grDepthMask. */
    void depthMask(bool enable);
    /**
     * grDisableAllEffects: blending, alpha test, chroma-key, fog and depth
     * buffering off; clip, dither and masks stay.
     */
    void disableAllEffects();
    /** grDitherMode, grFogColorValue, grFogMode and grFogTable (64 entries). */
    ///@{
    void ditherMode(int mode);
    void fogColorValue(uint32_t color);
    void fogMode(int mode);
    void fogTable(const uint8_t table[64]);
    ///@}
    /**
     * grGammaCorrectionValue: a gamma from above 0 to 20, others ignored;
     * nothing on a board without a gamma table.
     */
    void gammaCorrectionValue(float gamma);
    /** grHints: only GR_HINT_STWHINT's mask is kept. */
    void hints(int type, uint32_t mask);
    /**
     * grGlideGetState/SetState: the state saved under a number the caller
     * keeps in its GrState. The newest 64 are kept; another number is
     * ignored.
     */
    ///@{
    uint32_t glideGetState();
    void glideSetState(uint32_t token);
    ///@}

    // Textures.

    /**
     * grTexMinAddress: 0, or on a UMA board the first address after the
     * window's buffers.
     */
    uint32_t texMinAddress(int tmu) const;
    /**
     * grTexMaxAddress: where a 1x1 level that ends at the memory's end
     * starts, on the board's texture alignment.
     */
    uint32_t texMaxAddress(int tmu) const;
    /** grTexCalcMemRequired and grTexTextureMemRequired, in bytes rounded up to 8. */
    ///@{
    uint32_t texCalcMemRequired(int smallLod, int largeLod, int aspect, int format) const;
    uint32_t texTextureMemRequired(int evenOdd, const Glide2TexInfo &info) const;
    ///@}
    /**
     * The bytes grTexDownloadMipMap reads from its data: every level, large
     * to small, rows packed.
     */
    static uint32_t mipmapDataBytes(const Glide2TexInfo &info);
    /** The bytes of one level's data. */
    static uint32_t levelDataBytes(int lod, int aspect, int format);
    /** grTexSource; nothing for a TMU the board hasn't. */
    void texSource(int tmu, uint32_t start, int evenOdd, const Glide2TexInfo &info);
    /**
     * grTexDownloadMipMap, grTexDownloadMipMapLevel and
     * grTexDownloadMipMapLevelPartial: `data` as mipmapDataBytes and
     * levelDataBytes count it (for a partial level, from row `firstRow`);
     * `start` is the address of the mipmap's largest level.
     */
    ///@{
    void texDownloadMipMap(int tmu, uint32_t start, int evenOdd, const Glide2TexInfo &info, const uint8_t *data);
    void texDownloadMipMapLevel(int tmu, uint32_t start, int thisLod, int largeLod, int aspect, int format, int evenOdd,
                                const uint8_t *data);
    void texDownloadMipMapLevelPartial(int tmu, uint32_t start, int thisLod, int largeLod, int aspect, int format,
                                       int evenOdd, const uint8_t *data, int firstRow, int lastRow);
    ///@}
    /**
     * grTexDownloadTable and grTexDownloadTablePartial (entries `first` to
     * `last`, still indexed from the start of `data`). A table the board
     * hasn't is ignored.
     * @param data 256 0x00RRGGBB entries for the palette (0xAARRGGBB for
     *        Glide 3's GR_TEXTABLE_PALETTE_6666_EXT), a GuNccTable's first
     *        64 bytes (yRGB, iRGB, qRGB) for an NCC table.
     */
    ///@{
    void texDownloadTable(int tmu, int type, const uint8_t *data);
    void texDownloadTablePartial(int tmu, int type, const uint8_t *data, int first, int last);
    ///@}
    /**
     * grTexNCCTable: which NCC table (0 or 1) the TMU reads. This and the
     * other texture state functions do nothing for a TMU past kMaxTmus.
     */
    void texNCCTable(int tmu, int table);
    /**
     * grTexClampMode: GR_TEXTURECLAMP_MIRROR_EXT only on a board with the
     * TEXMIRROR extension, wrapped elsewhere.
     */
    void texClampMode(int tmu, int s, int t);
    /** grTexCombine. */
    void texCombine(int tmu, int rgbFunction, int rgbFactor, int alphaFunction, int alphaFactor, bool rgbInvert,
                    bool alphaInvert);
    /**
     * grTexCombineFunction (and the utility library's guTexCombineFunction):
     * a GR_TEXTURECOMBINE_* put as grTexCombine settings.
     */
    void texCombineFunction(int tmu, int function);
    /** grTexDetailControl: the scale held to 0-7, the maximum to 0-1. */
    void texDetailControl(int tmu, int lodBias, int detailScale, float detailMax);
    /** grTexFilterMode. */
    void texFilterMode(int tmu, int minFilter, int magFilter);
    /** grTexLodBiasValue: held to -8-7.75 and rounded to the nearest quarter. */
    void texLodBiasValue(int tmu, float bias);
    /** grTexMipMapMode and grTexMultibase. */
    ///@{
    void texMipMapMode(int tmu, int mode, bool lodBlend);
    void texMultibase(int tmu, bool enable);
    ///@}
    /** grTexMultibaseAddress: only the range and start are kept. */
    void texMultibaseAddress(int tmu, int range, uint32_t start, int evenOdd, const Glide2TexInfo &info);
    /**
     * The utility library's guColorCombineFunction and guAlphaSource, put as
     * grColorCombine and grAlphaCombine settings.
     */
    ///@{
    void guColorCombineFunction(int function);
    void guAlphaSource(int mode);
    ///@}

    // The utility library's texture manager: guTexAllocateMemory and the
    // rest (Glide 2.4 Reference Manual). Glide places each mipmap in a
    // TMU's memory, from grTexMinAddress up, and keeps its attributes under
    // a handle (GrMipMapId_t, 0 up; kNullMipMap if it doesn't fit).

    static constexpr uint32_t kNullMipMap = 0xffffffffu; // GR_NULL_MIPMAP_HANDLE
    /** A mipmap the texture manager placed, with the attributes guTexSource sets. */
    struct MipMap {
        bool valid = false;
        int tmu = 0;
        int evenOdd = kLevelsBoth;
        int width = 0;
        int height = 0;
        Glide2TexInfo info;
        int mipmapMode = kMipmapDisable;
        int clampS = kClampWrap;
        int clampT = kClampWrap;
        int minFilter = kFilterPoint;
        int magFilter = kFilterPoint;
        float lodBias = 0;
        bool lodBlend = false;
        uint32_t start = 0;
        uint32_t size = 0;
        bool hasNcc = false;
        NccTable ncc;
    };
    /**
     * Places a mipmap on the TMU after the last one placed, moved on to the
     * next 2 MB line if it would cross one.
     * @return Its handle, or kNullMipMap if the TMU isn't the board's or
     *         the mipmap doesn't fit.
     */
    uint32_t guTexAllocateMemory(int tmu, int evenOdd, int width, int height, int format, int mipmapMode, int smallLod,
                                 int largeLod, int aspect, int clampS, int clampT, int minFilter, int magFilter,
                                 float lodBias, bool lodBlend);
    /**
     * Changes a mipmap's attributes, -1 for each not changed; false if the
     * handle isn't one or the mipmap would no longer fit its memory.
     */
    bool guTexChangeAttributes(uint32_t id, int width, int height, int format, int mipmapMode, int smallLod,
                               int largeLod, int aspect, int clampS, int clampT, int minFilter, int magFilter);
    /**
     * Every level from `data` (largest first, as grTexDownloadMipMap
     * reads); `ncc` (a GuNccTable's first 64 bytes) kept for an NCC format.
     */
    void guTexDownloadMipMap(uint32_t id, const uint8_t *data, const uint8_t *ncc);
    /** One level; the bytes it took from `data` (0 if none). */
    uint32_t guTexDownloadMipMapLevel(uint32_t id, int lod, const uint8_t *data);
    /** The mipmap guTexSource last made current on the TMU, or kNullMipMap. */
    uint32_t guTexGetCurrentMipMap(int tmu) const;
    /** A handle's mipmap, or null if it isn't one. */
    const MipMap *guTexGetMipMapInfo(uint32_t id) const;
    /** The bytes left on the TMU after the last mipmap placed (0 for a TMU the board hasn't). */
    uint32_t guTexMemQueryAvail(int tmu) const;
    /** Frees every mipmap: every handle goes. */
    void guTexMemReset();
    /**
     * Makes a mipmap current on its TMU with all its attributes, and its
     * NCC table, if it has one, as the TMU's table 0.
     */
    void guTexSource(uint32_t id);

    // The linear frame buffer.

    /**
     * grLfbLock: fills `out` for lfbWrite. Reads are always 5:6:5 (or the
     * auxiliary buffer's 16 bits).
     * @return False if the window isn't open or hasn't the buffer, the
     *         board doesn't take the write mode or the pixel pipeline, or the
     *         mode doesn't suit the buffer (kLfbZA16 is the auxiliary
     *         buffer's alone).
     */
    bool lfbLock(int type, int buffer, int writeMode, int origin, bool pixelPipeline, Glide2LfbLock &out);
    /** grLfbUnlock: always succeeds. */
    bool lfbUnlock(int type, int buffer);
    /** What linear frame buffer writes without alpha or depth of their own write: 0-255, 0-65535. */
    ///@{
    void lfbConstantAlpha(int alpha);
    void lfbConstantDepth(int depth);
    ///@}
    /**
     * grLfbWriteColorFormat: the order of the colour channels in linear
     * frame buffer writes from now on (GR_COLORFORMAT_*), until the next
     * grSstWinOpen, which goes back to the window's own.
     */
    void lfbWriteColorFormat(int format);
    /**
     * grLfbWriteColorSwizzle: each 32-bit word written through a lock has
     * its four bytes reversed (swizzleBytes), then its two 16-bit halves
     * swapped (swapWords; not for the 888 and 8888 modes), as the Voodoo2
     * specification's lfbMode bits 12 and 11 do.
     */
    void lfbWriteColorSwizzle(bool swizzleBytes, bool swapWords);
    /** What lfbWriteColorSwizzle asked for. */
    ///@{
    bool lfbSwizzleBytes() const { return _lfbSwizzleBytes; }
    bool lfbSwapWords() const { return _lfbSwapWords; }
    ///@}
    /**
     * A 32-bit word written through a lock in `mode`, swizzled and swapped
     * as grLfbWriteColorSwizzle asks (for the 16-bit modes the word holds
     * two pixels, the left one in the low half).
     */
    uint32_t lfbSwizzle(int mode, uint32_t word) const;
    /**
     * A pixel as written in `mode` (its channels in the LFB colour format's
     * order) put in the canonical order Pipeline::lfbWrite takes (ARGB;
     * 5:6:5 red high; 1:5:5:5 alpha high; colour in the high half of the
     * depth modes).
     */
    uint32_t lfbCanonical(int mode, uint32_t value) const;
    /**
     * One pixel written through a lock (`value` as the lock's write mode
     * has it), at (x, y) from the lock's origin.
     */
    void lfbWrite(const Glide2LfbLock &lock, int x, int y, uint32_t value);
    /**
     * A pixel as a lock reads it: 5:6:5 (or the auxiliary buffer's 16 bits);
     * 0 outside the screen or with no such buffer.
     */
    uint16_t lfbRead(int buffer, int x, int y, bool originLowerLeft) const;
    /**
     * grLfbWriteRegion: `data` in `format` (a write mode, or kLfbRle16),
     * `stride` bytes a row (RLE16's runs go across the rows, the stride
     * unused).
     * @param dataBytes How much `data` may be read (it bounds RLE16's runs).
     * @param pixelPipeline Through the pixel pipeline, as Glide 3's
     *        grLfbWriteRegion may ask.
     * @return False if the window isn't open or hasn't the buffer, or the
     *         board doesn't take the format or the pixel pipeline.
     */
    bool lfbWriteRegion(int buffer, uint32_t x, uint32_t y, int format, uint32_t width, uint32_t height, int stride,
                        const uint8_t *data, size_t dataBytes = SIZE_MAX, bool pixelPipeline = false);
    /**
     * grLfbReadRegion: 16-bit pixels, `stride` bytes a row; false if the
     * window isn't open, hasn't the buffer, or the stride is too short.
     */
    bool lfbReadRegion(int buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t stride,
                       uint8_t *data);

    // Statistics.

    /**
     * grSstPerfStats: pixels in, chroma-key failures, depth test failures,
     * alpha test failures and pixels out since the last reset, wrapping as
     * the hardware's 24-bit counters do.
     */
    void sstPerfStats(uint32_t out[5]) const;
    /** grSstResetPerfStats. */
    void sstResetPerfStats();
    /** grTriStats: the triangles processed and drawn since resetTriStats. */
    void triStats(uint32_t &processed, uint32_t &drawn) const;
    /** grResetTriStats. */
    void resetTriStats();

private:
    Vertex convert(const Glide2Vertex &v) const;
    uint32_t toArgb(uint32_t color) const { return toArgb(color, _colorFormat); } // the window's colour format
    static uint32_t toArgb(uint32_t color, int format); // a GR_COLORFORMAT_* colour to 0xAARRGGBB
    static TextureInfo textureInfo(int smallLod, int largeLod, int aspect, int format);
    static int lodToLog2(int lod) { return 8 - lod; }
    static int aspectToLog2(int aspect) { return 3 - aspect; }

    Board _board;
    Pipeline _pipeline;
    int _colorFormat = kColorArgb;
    int _lfbColorFormat = -1; // grLfbWriteColorFormat's, -1 the window's
    bool _lfbSwizzleBytes = false;
    bool _lfbSwapWords = false;
    bool _active = true;
    uint32_t _window = 0; // grSstWinOpen's window
    uint32_t _retraceCalls = 0;
    int _locks = 0;
    bool _frontChanged = false;
    void drawn() { _frontChanged |= _pipeline.state().renderBuffer == kBufferFront; }
    std::set<std::string> _noted;
    std::map<uint32_t, State> _savedStates;
    std::vector<MipMap> _mipmaps;
    std::array<uint32_t, kMaxTmus> _texNext{}; // the texture manager's next free address
    std::array<uint32_t, kMaxTmus> _texCurrent{kNullMipMap, kNullMipMap, kNullMipMap};
    uint32_t _nextStateToken = 1;
    uint64_t _statsBase[5] = {};
    uint64_t _triBase[2] = {};
};

} // namespace reluva::glide
