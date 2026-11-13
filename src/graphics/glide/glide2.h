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

// Glide 2's GrVertex with two TMUs' coordinates (GLIDE_NUM_TMU 2, the
// build glide2x.dll's callers use: 60 bytes): the fields in the header's
// order.
struct Glide2Vertex {
    float x = 0, y = 0, z = 0;
    float r = 0, g = 0, b = 0;
    float ooz = 0, a = 0, oow = 0;
    struct Tmu {
        float sow = 0, tow = 0, oow = 0;
    } tmuvtx[2];
};
static_assert(sizeof(Glide2Vertex) == 60, "GrVertex is 60 bytes");

// GrTexInfo without its data pointer: Glide 2's LOD and aspect constants
// (GR_LOD_256 is 0, GR_ASPECT_8x1 is 0).
struct Glide2TexInfo {
    int smallLod = 0, largeLod = 0, aspectRatio = 3, format = 0;
};

// What grSstQueryHardware reports, one board.
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

// A linear frame buffer lock (grLfbLock's GrLfbInfo_t, less the pointer).
struct Glide2LfbLock {
    int buffer = kBufferBack;
    int writeMode = kLfb565;
    bool originLowerLeft = false;
    bool pixelPipeline = false;
    int strideBytes = 2048; // the Voodoo's 1024-pixel logical scan line
    int bytesPerPixel = 2;
};

class Glide2 {
public:
    explicit Glide2(Board board = {});

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
    // Whether the front buffer (the one shown) was drawn into, cleared or
    // written through a lock since the last call: a host shows it again
    // then, as the monitor would (a program can draw straight to it).
    bool takeFrontChanged() {
        const bool changed = _frontChanged;
        _frontChanged = false;
        return changed;
    }
    // Notes, once, that `function` was called but isn't (fully) carried out.
    void notImplemented(const std::string &function, const std::string &what = "not implemented");

    // --- Start-up and shut-down ------------------------------------------
    void glideInit();
    void glideShutdown();
    std::string glideGetVersion() const; // grGlideGetVersion's string
    bool sstQueryBoards(Glide2HwConfig &out) const;
    bool sstQueryHardware(Glide2HwConfig &out) const;
    void sstSelect(int which);
    bool sstWinOpen(uint32_t hwnd, int resolution, int refresh, int colorFormat, int origin, int colorBuffers,
                    int auxBuffers);
    void sstWinClose();
    // grSstControl/grSstControlMode: GR_CONTROL_ACTIVATE, DEACTIVATE,
    // RESIZE (a windowed Rush's buffers to its window's size, by
    // windowSize), MOVE.
    bool sstControl(uint32_t code);
    bool sstIsOpen() const { return _pipeline.isOpen(); }
    bool active() const { return _active; } // GR_CONTROL_ACTIVATE (the default) or DEACTIVATE
    uint32_t sstScreenWidth() const;
    uint32_t sstScreenHeight() const;
    uint32_t sstStatus() const;
    bool sstVRetraceOn();
    uint32_t sstVideoLine();
    void sstOrigin(int origin);
    // The width and height a GR_RESOLUTION_* constant names (false if none).
    static bool resolutionSize(int resolution, int &width, int &height);
    static constexpr int kResolutionNone = 0xff; // GR_RESOLUTION_NONE

    // --- Drawing ------------------------------------------------------------
    void drawTriangle(const Glide2Vertex &a, const Glide2Vertex &b, const Glide2Vertex &c);
    void drawLine(const Glide2Vertex &a, const Glide2Vertex &b);
    void drawPoint(const Glide2Vertex &a);
    // A convex polygon, a fan from its first vertex.
    void drawPolygon(int count, const Glide2Vertex *vertices);
    // Anti-aliased (grAADrawTriangle with the edges to anti-alias,
    // grAADrawLine, grAADrawPoint; Pipeline::aaTriangle says how).
    void aaDrawTriangle(const Glide2Vertex &a, const Glide2Vertex &b, const Glide2Vertex &c, bool ab, bool bc, bool ca);
    void aaDrawLine(const Glide2Vertex &a, const Glide2Vertex &b);
    void aaDrawPoint(const Glide2Vertex &a);
    // grAADrawPolygon(VertexList): a fan with its outside edges
    // anti-aliased ("All exterior polygon edges are anti-aliased").
    void aaDrawPolygon(int count, const Glide2Vertex *vertices);
    // The same with vertices already in the pipeline's form (Glide 3's
    // layouts), counted as drawn.
    void triangle(const Vertex &a, const Vertex &b, const Vertex &c);
    void line(const Vertex &a, const Vertex &b);
    void point(const Vertex &a);
    void aaTriangle(const Vertex &a, const Vertex &b, const Vertex &c, bool ab, bool bc, bool ca);
    void aaLine(const Vertex &a, const Vertex &b);
    void aaPoint(const Vertex &a);
    // A fan of `count` vertices, its outside edges anti-aliased: all of
    // them, or with `edges`, edge i (vertex i to i + 1, the last back to 0)
    // where edges[i] is set.
    void aaPolygon(int count, const Vertex *vertices, const bool *edges = nullptr);
    void bufferClear(uint32_t color, int alpha, int depth);
    void bufferSwap(int interval);
    int bufferNumPending() const { return 0; }
    void renderBuffer(int buffer);

    // --- State ----------------------------------------------------------------
    void colorCombine(int function, int factor, int local, int other, bool invert);
    void alphaCombine(int function, int factor, int local, int other, bool invert);
    void alphaControlsITRGBLighting(bool enable);
    void alphaBlendFunction(int rgbSrc, int rgbDst, int alphaSrc, int alphaDst);
    void alphaTestFunction(int function);
    void alphaTestReferenceValue(int value);
    void chromakeyMode(int mode);
    void chromakeyValue(uint32_t color);
    // Glide 3's CHROMARANGE extension (grChromaRangeModeExt,
    // grChromaRangeExt), on a board that has it.
    void chromaRangeMode(bool enable);
    void chromaRange(uint32_t color0, uint32_t color1, int mode);
    void clipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY);
    void colorMask(bool rgb, bool alpha);
    void constantColorValue(uint32_t color);
    void constantColorValue4(float a, float r, float g, float b);
    void cullMode(int mode);
    void depthBiasLevel(int level);
    void depthBufferFunction(int function);
    void depthBufferMode(int mode);
    const Board &board() const { return _board; }
    void depthMask(bool enable);
    void disableAllEffects();
    void ditherMode(int mode);
    void fogColorValue(uint32_t color);
    void fogMode(int mode);
    void fogTable(const uint8_t table[64]);
    void gammaCorrectionValue(float gamma);
    void hints(int type, uint32_t mask);
    // grGlideGetState/SetState: the state saved under a number the caller
    // keeps in its GrState.
    uint32_t glideGetState();
    void glideSetState(uint32_t token);

    // --- Textures ---------------------------------------------------------
    uint32_t texMinAddress(int tmu) const;
    uint32_t texMaxAddress(int tmu) const;
    uint32_t texCalcMemRequired(int smallLod, int largeLod, int aspect, int format) const;
    uint32_t texTextureMemRequired(int evenOdd, const Glide2TexInfo &info) const;
    // The bytes grTexDownloadMipMap reads from its data: every level, large
    // to small, rows packed.
    static uint32_t mipmapDataBytes(const Glide2TexInfo &info);
    // ... and one level.
    static uint32_t levelDataBytes(int lod, int aspect, int format);
    void texSource(int tmu, uint32_t start, int evenOdd, const Glide2TexInfo &info);
    void texDownloadMipMap(int tmu, uint32_t start, int evenOdd, const Glide2TexInfo &info, const uint8_t *data);
    void texDownloadMipMapLevel(int tmu, uint32_t start, int thisLod, int largeLod, int aspect, int format, int evenOdd,
                                const uint8_t *data);
    void texDownloadMipMapLevelPartial(int tmu, uint32_t start, int thisLod, int largeLod, int aspect, int format,
                                       int evenOdd, const uint8_t *data, int firstRow, int lastRow);
    // `data`: 256 0x00RRGGBB entries for the palette (0xAARRGGBB for
    // Glide 3's GR_TEXTABLE_PALETTE_6666_EXT), a GuNccTable's first 64
    // bytes (yRGB, iRGB, qRGB) for an NCC table.
    void texDownloadTable(int tmu, int type, const uint8_t *data);
    void texDownloadTablePartial(int tmu, int type, const uint8_t *data, int first, int last);
    void texNCCTable(int tmu, int table);
    void texClampMode(int tmu, int s, int t);
    void texCombine(int tmu, int rgbFunction, int rgbFactor, int alphaFunction, int alphaFactor, bool rgbInvert,
                    bool alphaInvert);
    void texCombineFunction(int tmu, int function); // grTexCombineFunction, guTexCombineFunction
    void texDetailControl(int tmu, int lodBias, int detailScale, float detailMax);
    void texFilterMode(int tmu, int minFilter, int magFilter);
    void texLodBiasValue(int tmu, float bias);
    void texMipMapMode(int tmu, int mode, bool lodBlend);
    void texMultibase(int tmu, bool enable);
    void texMultibaseAddress(int tmu, int range, uint32_t start, int evenOdd, const Glide2TexInfo &info);
    void guColorCombineFunction(int function);
    void guAlphaSource(int mode);

    // --- The utility library's texture manager ------------------------------
    // guTexAllocateMemory and the rest (Glide 2.4 Reference Manual): Glide
    // places each mipmap in a TMU's memory, from grTexMinAddress up, and
    // keeps its attributes under a handle (GrMipMapId_t, 0 up;
    // kNullMipMap if it doesn't fit).
    static constexpr uint32_t kNullMipMap = 0xffffffffu; // GR_NULL_MIPMAP_HANDLE
    struct MipMap {
        bool valid = false;
        int tmu = 0;
        int evenOdd = kLevelsBoth;
        int width = 0, height = 0;
        Glide2TexInfo info;
        int mipmapMode = kMipmapDisable;
        int clampS = kClampWrap, clampT = kClampWrap;
        int minFilter = kFilterPoint, magFilter = kFilterPoint;
        float lodBias = 0;
        bool lodBlend = false;
        uint32_t start = 0, size = 0;
        bool hasNcc = false;
        NccTable ncc;
    };
    uint32_t guTexAllocateMemory(int tmu, int evenOdd, int width, int height, int format, int mipmapMode, int smallLod,
                                 int largeLod, int aspect, int clampS, int clampT, int minFilter, int magFilter,
                                 float lodBias, bool lodBlend);
    // -1 for each attribute not changed; false if the handle isn't one or
    // the mipmap would no longer fit its memory.
    bool guTexChangeAttributes(uint32_t id, int width, int height, int format, int mipmapMode, int smallLod,
                               int largeLod, int aspect, int clampS, int clampT, int minFilter, int magFilter);
    // Every level from `data` (largest first, as grTexDownloadMipMap
    // reads); `ncc` (a GuNccTable's first 64 bytes) kept for an NCC format.
    void guTexDownloadMipMap(uint32_t id, const uint8_t *data, const uint8_t *ncc);
    // One level; the bytes it took from `data` (0 if none).
    uint32_t guTexDownloadMipMapLevel(uint32_t id, int lod, const uint8_t *data);
    uint32_t guTexGetCurrentMipMap(int tmu) const;
    const MipMap *guTexGetMipMapInfo(uint32_t id) const;
    uint32_t guTexMemQueryAvail(int tmu) const;
    void guTexMemReset();
    void guTexSource(uint32_t id);

    // --- The linear frame buffer ----------------------------------------
    bool lfbLock(int type, int buffer, int writeMode, int origin, bool pixelPipeline, Glide2LfbLock &out);
    bool lfbUnlock(int type, int buffer);
    void lfbConstantAlpha(int alpha);
    void lfbConstantDepth(int depth);
    // grLfbWriteColorFormat: the order of the colour channels in linear
    // frame buffer writes from now on (GR_COLORFORMAT_*), until the next
    // grSstWinOpen, which goes back to the window's own.
    void lfbWriteColorFormat(int format);
    // grLfbWriteColorSwizzle: each 32-bit word written through a lock has
    // its four bytes reversed (swizzleBytes), then its two 16-bit halves
    // swapped (swapWords; not for the 888 and 8888 modes), as the Voodoo2
    // specification's lfbMode bits 12 and 11 do.
    void lfbWriteColorSwizzle(bool swizzleBytes, bool swapWords);
    bool lfbSwizzleBytes() const { return _lfbSwizzleBytes; }
    bool lfbSwapWords() const { return _lfbSwapWords; }
    // A 32-bit word written through a lock in `mode`, swizzled and swapped
    // as grLfbWriteColorSwizzle asks (for the 16-bit modes the word holds
    // two pixels, the left one in the low half).
    uint32_t lfbSwizzle(int mode, uint32_t word) const;
    // A pixel as written in `mode` (its channels in the LFB colour format's
    // order) put in the canonical order Pipeline::lfbWrite takes (ARGB;
    // 5:6:5 red high; 1:5:5:5 alpha high; colour in the high half of the
    // depth modes).
    uint32_t lfbCanonical(int mode, uint32_t value) const;
    // One pixel written through a lock (`value` as the lock's write mode
    // has it), at (x, y) from the lock's origin.
    void lfbWrite(const Glide2LfbLock &lock, int x, int y, uint32_t value);
    // A pixel as a lock reads it: 5:6:5 (or the auxiliary buffer's 16 bits).
    uint16_t lfbRead(int buffer, int x, int y, bool originLowerLeft) const;
    // `dataBytes`: how much `data` may be read (it bounds RLE16's runs).
    bool lfbWriteRegion(int buffer, uint32_t x, uint32_t y, int format, uint32_t width, uint32_t height, int stride,
                        const uint8_t *data, size_t dataBytes = SIZE_MAX, bool pixelPipeline = false);
    bool lfbReadRegion(int buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t stride,
                       uint8_t *data);

    // Statistics.
    void sstPerfStats(uint32_t out[5]) const;
    void sstResetPerfStats();
    void triStats(uint32_t &processed, uint32_t &drawn) const;
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
    bool _lfbSwizzleBytes = false, _lfbSwapWords = false;
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
