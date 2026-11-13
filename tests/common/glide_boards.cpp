// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Glide stand-in's boards as 3dfx's documents give them
// (docs/common/graphics/glide.md, Boards): the Voodoo Rush's frame buffer shared
// with the 2D desktop (its windowed memory, GR_CONTROL_RESIZE, what the
// linear frame buffer and the gamma table allow, grSstQueryHardware's
// GrSst96Config_t); the Banshee (its configuration, the 2048-pixel scan
// line); Glide 3's extensions on the boards that have them (the 6666
// palette, mirrored textures, chroma range, grGetString and
// grGetProcAddress); and the anti-aliased fringe of primitives clipped
// against the view volume.

#include "graphics/glide/glide2.h"
#include "emulation/x86/glide/glide2_x86.h"
#include "graphics/glide/glide3.h"
#include "emulation/x86/glide/glide3_x86.h"
#include "graphics/glide/pipeline.h"
#include "tests/support/test.h"

#include <filesystem>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace reluva::glide;

namespace {

Glide2Vertex vtx(float x, float y, float r = 255, float g = 255, float b = 255, float a = 255, float s = 0,
                 float t = 0) {
    Glide2Vertex v;
    v.x = x;
    v.y = y;
    v.r = r;
    v.g = g;
    v.b = b;
    v.a = a;
    v.oow = 1;
    for (auto &m : v.tmuvtx) {
        m.sow = s;
        m.tow = t;
        m.oow = 1;
    }
    return v;
}

int green(const Glide2 &g, int x, int y) {
    return g.lfbRead(kBufferBack, x, y, false) >> 5 & 63;
}
int red(const Glide2 &g, int x, int y) {
    return g.lfbRead(kBufferBack, x, y, false) >> 11 & 31;
}

// --- The Voodoo Rush's memory, shared with the 2D desktop -------------------------

void testRushDesktop() {
    // The SST-96 specification's windowed tables (System Resolutions):
    // the desktop, then the off-screen 3D buffers.
    Pipeline p(voodooRushBoard());
    // 16-bit 1024x768 desktop, 640x480 in place (one off-screen buffer):
    // its 2,187,264.
    CHECK(p.bufferBytes(640, 480, 2, 0, true) == 2187264);
    // 16-bit 1280x1024, with a 16-bit depth buffer: its 3,850,240.
    Board b;
    CHECK(boardByName("rush,desktop=1280x1024x16", b) && b.desktopWidth == 1280 && b.desktopBits == 16);
    CHECK(Pipeline(b).bufferBytes(640, 480, 2, 1, true) == 3850240);
    // 8-bit 1152x864, out of place (both colour buffers off screen): its
    // 2,224,128.
    CHECK(boardByName("rush,desktop=1152x864x8", b));
    CHECK(Pipeline(b).bufferBytes(640, 480, 2, 0, true) == 2224128);
    CHECK(!boardByName("rush,desktop=1024x768x12", b) && !boardByName("rush,desktop=0x768x16", b));
    // The host's desktop, made one a Rush could have had (setDesktop): a
    // true-colour one counts as 16 bits, its size as it is when the
    // tables have it.
    bool clamped = false;
    CHECK(boardByName("rush,desktop=1024x768x24", b, &clamped) && clamped && b.desktopBits == 16 &&
          b.desktopWidth == 1024 && b.desktopHeight == 768);
    CHECK(boardByName("rush,desktop=1152x864x16", b, &clamped) && !clamped && b.desktopWidth == 1152);
    // Smaller than the tables' sizes: as it is (the window can't be larger).
    CHECK(boardByName("rush,desktop=800x600x16", b, &clamped) && !clamped && b.desktopWidth == 800);
    CHECK(!Pipeline(b).fits(1024, 768, 2, 0, true) && Pipeline(b).fits(640, 480, 2, 1, true));
    // Larger: cut to the largest at its depth, 1280x1024 at 16 bits,
    // 1600x1200 at 8.
    Board host = voodooRushBoard();
    CHECK(setDesktop(host, 2560, 1440, 32, &clamped) && clamped && host.desktopWidth == 1280 &&
          host.desktopHeight == 1024 && host.desktopBits == 16);
    CHECK(Pipeline(host).bufferBytes(640, 480, 2, 1, true) == 3850240);
    CHECK(setDesktop(host, 1920, 1080, 8, &clamped) && clamped && host.desktopWidth == 1600 &&
          host.desktopHeight == 1080 && host.desktopBits == 8);
    CHECK(setDesktop(host, 1280, 1024, 15, &clamped) && clamped && host.desktopBits == 16);
    CHECK(!setDesktop(host, 1024, 768, 12) && host.desktopBits == 16);
    // A 2 MB Rush: a 16-bit 1280x1024 desktop would fill it; the next size
    // down, 1152x864, leaves room, "fb=" counted whatever the order.
    CHECK(boardByName("rush,desktop=1920x1200x32,fb=2", b, &clamped) && clamped && b.desktopWidth == 1152 &&
          b.desktopHeight == 864);
    // Boards with a frame buffer of their own take no desktop.
    Board ownFb = voodoo2Board();
    CHECK(setDesktop(ownFb, 1920, 1080, 32) && ownFb.desktopBits == 0);
    // Full screen: the 3D buffers alone (its full-screen table: 800x600
    // with a depth buffer, 2,880,000 bytes).
    CHECK(p.bufferBytes(800, 600, 2, 1, false) == 2880000);

    // "allows an application to run in a larger 3D window if a depth buffer
    // is not necessary" (Glide 2.4 Reference Manual, grSstWinOpen): on a
    // 1280x1024 desktop an 800x600 window fits without one, not with one.
    CHECK(boardByName("rush,desktop=1280x1024x16", b));
    Glide2 rush(b);
    int winW = 800;
    int winH = 600;
    rush.windowSize = [&](uint32_t, int &w, int &h) {
        w = winW;
        h = winH;
        return true;
    };
    CHECK(!rush.sstWinOpen(1, Glide2::kResolutionNone, 0, kColorArgb, 0, 2, 1));
    CHECK(rush.sstWinOpen(1, Glide2::kResolutionNone, 0, kColorArgb, 0, 2, 0));
    CHECK(rush.pipeline().windowed());
    rush.sstWinClose();
    // Full screen, the same buffers fit (the desktop gives its memory up).
    CHECK(rush.sstWinOpen(1, 8, 0, kColorArgb, 0, 2, 1) && !rush.pipeline().windowed());
    // GR_CONTROL_RESIZE "is ignored ... in full screen mode".
    winW = 320;
    CHECK(rush.sstControl(kControlResize) && rush.sstScreenWidth() == 800);
    rush.sstWinClose();

    // Windowed: GR_CONTROL_RESIZE takes the window's new size, the state
    // kept, the clip window the new window; too large for the memory left
    // it fails and nothing changes.
    Glide2 r2(voodooRushBoard()); // the 1024x768 16-bit desktop
    winW = 320;
    winH = 200;
    r2.windowSize = rush.windowSize;
    CHECK(r2.sstWinOpen(1, Glide2::kResolutionNone, 0, kColorArgb, 0, 2, 1));
    r2.fogMode(kFogWithTable);
    winW = 640;
    winH = 480;
    CHECK(r2.sstControl(kControlResize));
    CHECK(r2.sstScreenWidth() == 640 && r2.sstScreenHeight() == 480);
    CHECK(r2.pipeline().state().fogMode == kFogWithTable && r2.pipeline().state().clip[2] == 640);
    winW = 1000;
    winH = 700; // 1,572,864 + 2 x 1,400,000 > 4 MB
    CHECK(!r2.sstControl(kControlResize));
    CHECK(r2.sstScreenWidth() == 640);
    CHECK(r2.sstControl(kControlMove));

    // The linear frame buffer through the 2D chip: 5:6:5 and the auxiliary
    // buffer's 16 bits only, no pixel pipeline, the buffers' packed rows
    // (the width in quad pixels).
    Glide2LfbLock lock;
    CHECK(r2.lfbLock(1, kBufferBack, kLfb565, 0, false, lock) && lock.strideBytes == 640 * 2);
    CHECK(r2.lfbLock(1, kBufferBack, kLfbAny, 0, false, lock) && lock.writeMode == kLfb565);
    CHECK(!r2.lfbLock(1, kBufferBack, kLfb1555, 0, false, lock));
    CHECK(!r2.lfbLock(1, kBufferBack, kLfb8888, 0, false, lock));
    CHECK(!r2.lfbLock(1, kBufferBack, kLfb565, 0, true, lock));
    CHECK(r2.lfbLock(1, kBufferAux, kLfbZA16, 0, false, lock));
    CHECK(r2.lfbLock(0, kBufferBack, kLfb8888, 0, true, lock)); // a read lock: the mode and pipeline ignored
    winW = 322;
    CHECK(r2.sstControl(kControlResize));
    CHECK(r2.lfbLock(1, kBufferBack, kLfb565, 0, false, lock) && lock.strideBytes == 324 * 2);
    const uint32_t argb[2] = {0xffffffffu, 0xffffffffu};
    const uint16_t white[2] = {0xffff, 0xffff};
    CHECK(!r2.lfbWriteRegion(kBufferBack, 0, 0, kLfb8888, 2, 1, 8, reinterpret_cast<const uint8_t *>(argb)));
    CHECK(r2.lfbWriteRegion(kBufferBack, 0, 0, kLfb565, 2, 1, 4, reinterpret_cast<const uint8_t *>(white)));
    CHECK(!r2.lfbWriteRegion(kBufferBack, 0, 0, kLfb565, 2, 1, 4, reinterpret_cast<const uint8_t *>(white), 4, true));
    CHECK(r2.lfbRead(kBufferBack, 1, 0, false) == 0xffff);
    // Elsewhere the Voodoo2's 1024-pixel logical scan line.
    Glide2 v2;
    v2.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    CHECK(v2.lfbLock(1, kBufferBack, kLfb8888, 0, true, lock) && lock.strideBytes == 4096);

    // No gamma table: grGammaCorrectionValue "is a no-op on SST-96
    // systems"; Glide 3's grGet fails for the gamma queries.
    r2.gammaCorrectionValue(2.2f);
    CHECK(r2.pipeline().state().gamma[0] == 1.0f);
    Glide3 g3r(voodooRushBoard());
    int32_t v[4] = {-1, -1, -1, -1};
    CHECK(g3r.get(g3::kGammaTableEntries, 4, v) == 0 && v[0] == -1);
    CHECK(g3r.get(g3::kBitsGamma, 4, v) == 0 && v[0] == -1);
    CHECK(g3r.get(g3::kLfbPixelPipe, 4, v) == 4 && v[0] == 0);
    CHECK(Glide3(voodoo2Board()).get(g3::kLfbPixelPipe, 4, v) == 4 && v[0] == 1);
    CHECK(Glide3(voodoo2Board()).get(g3::kGammaTableEntries, 4, v) == 4 && v[0] == 32);

    // grSstQueryHardware through the binding: GR_SSTTYPE_SST96 with a
    // GrSst96Config_t (fbRam, nTexelfx, tmuConfig).
    reluva::x86::Cpu cpu(16u << 20);
    cpu.setStack(0x100000);
    reluva::x86::Arena arena(cpu, 0x200000, 0x800000);
    Glide2Dll dll(cpu, arena, voodooRushBoard());
    const uint32_t hw = arena.alloc(256);
    uint32_t result = 0;
    CHECK(!cpu.call(dll.exports().at("_grSstQueryHardware@4"), {hw}, result) && result == 1);
    CHECK(cpu.read32(hw + 4) == 1 && cpu.read32(hw + 8) == 4 && cpu.read32(hw + 12) == 1);
    CHECK(cpu.read32(hw + 16) == 1 && cpu.read32(hw + 20) == 2);
}

// --- The Banshee -----------------------------------------------------------------------

void testBanshee() {
    const Board b = bansheeBoard();
    CHECK(b.generation == Generation::kBanshee && b.tmus == 1 && b.uma && b.frameBufferMemory == 16u << 20);
    CHECK(b.textureAlign == 16 && b.fogTableEntries == 64 && b.gammaTableEntries == 256 && b.palette);
    CHECK(b.extensions == (kExtChromaRange | kExtTexMirror | kExtPalette6666));
    // Up to 1600x1200 (its RAMDAC's): GR_RESOLUTION_1600x1200 (14) opens,
    // 1792x1344 (19) doesn't; the Voodoo3 takes it.
    Glide3 g(b);
    CHECK(!g.sstWinOpen(0, 19, 0, kColorArgb, 0, 2, 0));
    CHECK(g.sstWinOpen(0, 14, 0, kColorArgb, 0, 2, 1));
    Glide3 v3;
    CHECK(v3.sstWinOpen(0, 19, 0, kColorArgb, 0, 2, 1));
    // grGet and grGetString.
    int32_t v[4] = {};
    CHECK(g.get(g3::kNumTmu, 4, v) == 4 && v[0] == 1);
    CHECK(g.get(g3::kMemoryUma, 4, v) == 4 && v[0] == int32_t(16u << 20));
    CHECK(g.get(g3::kRevisionFb, 4, v) == 4 && v[0] == 1);
    CHECK(g.getString(g3::kHardware) == "Voodoo Banshee");
    CHECK(g.getString(g3::kExtension) == "CHROMARANGE TEXMIRROR PALETTE6666");
    CHECK(Glide3(voodoo2Board()).getString(g3::kExtension) == "CHROMARANGE TEXMIRROR");
    // "If no extensions are supported, a single space" is returned.
    CHECK(Glide3(voodoo1Board()).getString(g3::kExtension) == " ");
    CHECK(Glide3(voodooRushBoard()).getString(g3::kExtension) == " ");
    // The 2048-pixel logical scan line of its linear frame buffer.
    Glide2LfbLock lock;
    CHECK(g.core().lfbLock(1, kBufferBack, kLfb565, 0, false, lock) && lock.strideBytes == 4096);
    CHECK(g.core().lfbLock(1, kBufferBack, kLfb8888, 0, false, lock) && lock.strideBytes == 8192);
    // Glide 2 calls it GR_SSTTYPE_Banshee (4) with one TMU.
    Glide2HwConfig hw;
    CHECK(Glide2(b).sstQueryHardware(hw) && hw.type == 4 && hw.nTexelfx == 1 && hw.fbRamMb == 16);
}

// --- Glide 3's extensions ------------------------------------------------------------------

void texturedSetup(Glide2 &g) {
    g.ditherMode(kDitherDisable);
    g.colorCombine(kFuncScaleOther, kFactorOne, kLocalIterated, kOtherTexture, false);
    g.alphaCombine(kFuncScaleOther, kFactorOne, kLocalIterated, kOtherTexture, false);
    g.bufferClear(0, 0, 0);
}

void testPalette6666() {
    Glide3 g(bansheeBoard());
    CHECK(g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1));
    texturedSetup(g.core());
    // An 8x8 P_8 texture of index 1, entry 1 opaque red, entry 2 half
    // transparent green.
    std::vector<uint8_t> idx(64, 1);
    idx[0] = 2;
    const uint32_t at = g.core().texMinAddress(0);
    const Glide3TexInfo info{3, 3, 0, kTexP8};
    g.texDownloadMipMap(0, at, kLevelsBoth, info, idx.data());
    g.texSource(0, at, kLevelsBoth, info);
    std::vector<uint32_t> pal(256, 0);
    pal[1] = 0xffff0000u;
    pal[2] = 0x8000ff00u;
    g.texDownloadTablePartial(kTablePalette6666, reinterpret_cast<const uint8_t *>(pal.data()), 0, 255);
    // Packed 6:6:6:6 (alpha 23-18) and read as format 6.
    CHECK(g.pipeline().palette(0)[1] == (63u << 18 | 63u << 12));
    CHECK(g.pipeline().palette(0)[2] == (32u << 18 | 63u << 6));
    CHECK(g.pipeline().palette6666(0) && g.pipeline().textureFormat(0) == 6);
    // Drawn: red where index 1; the alpha from the palette (32 of 63
    // widened: 0x82) tested against a reference between it and 255.
    g.core().alphaTestFunction(kCmpGreater);
    g.core().alphaTestReferenceValue(0x90);
    g.core().drawTriangle(vtx(0, 0, 255, 255, 255, 255, 0, 0), vtx(64, 0, 255, 255, 255, 255, 256, 0),
                          vtx(0, 64, 255, 255, 255, 255, 0, 256));
    CHECK(red(g.core(), 20, 20) == 31 && green(g.core(), 20, 20) == 0);
    CHECK(g.core().lfbRead(kBufferBack, 2, 2, false) == 0); // texel 0, alpha 0x82: tested out
    g.core().alphaTestReferenceValue(0x80);
    g.core().drawTriangle(vtx(0, 0, 255, 255, 255, 255, 0, 0), vtx(64, 0, 255, 255, 255, 255, 256, 0),
                          vtx(0, 64, 255, 255, 255, 255, 0, 256));
    CHECK(green(g.core(), 2, 2) == 63 && red(g.core(), 2, 2) == 0);
    // A plain palette afterwards: format 5 again, opaque.
    g.texDownloadTablePartial(kTablePalette, reinterpret_cast<const uint8_t *>(pal.data()), 0, 255);
    CHECK(!g.pipeline().palette6666(0) && g.pipeline().textureFormat(0) == kTexP8);
    // Not on a Voodoo2 (no such format there): ignored.
    Glide3 v2(voodoo2Board());
    v2.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    v2.texDownloadTablePartial(kTablePalette6666, reinterpret_cast<const uint8_t *>(pal.data()), 0, 255);
    CHECK(!v2.pipeline().palette6666(0) && v2.pipeline().palette(0)[1] == 0);
}

void testMirror() {
    // An 8x1 texture whose texel i has red 4i; a 16-pixel row with s from 0
    // to 512: pixel x samples texel x of the first copy, then the second.
    auto row = [](Board board, int clamp, int *out) {
        Glide2 g(board);
        g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
        texturedSetup(g);
        std::vector<uint16_t> tex(8);
        for (int i = 0; i < 8; ++i)
            tex[size_t(i)] = uint16_t((4 * i) << 11);
        const uint32_t at = g.texMinAddress(0);
        const Glide2TexInfo info{5, 5, 0, kTexRgb565}; // GR_LOD_8, GR_ASPECT_8x1
        g.texDownloadMipMap(0, at, kLevelsBoth, info, reinterpret_cast<const uint8_t *>(tex.data()));
        g.texSource(0, at, kLevelsBoth, info);
        g.texClampMode(0, clamp, kClampClamp);
        g.drawTriangle(vtx(0, 0, 255, 255, 255, 255, 0, 0), vtx(16, 0, 255, 255, 255, 255, 512, 0),
                       vtx(16, 4, 255, 255, 255, 255, 512, 32));
        g.drawTriangle(vtx(0, 0, 255, 255, 255, 255, 0, 0), vtx(16, 4, 255, 255, 255, 255, 512, 32),
                       vtx(0, 4, 255, 255, 255, 255, 0, 32));
        for (int x = 0; x < 16; ++x)
            out[x] = red(g, x, 2);
    };
    int wrap[16];
    int mirror[16];
    int v1[16];
    row(voodoo2Board(), kClampWrap, wrap);
    row(voodoo2Board(), kClampMirror, mirror);
    row(voodoo1Board(), kClampMirror, v1); // no TEXMIRROR on the Voodoo Graphics: wrapped
    for (int x = 0; x < 16; ++x) {
        CHECK(wrap[x] == 4 * (x & 7));
        CHECK(mirror[x] == (x < 8 ? 4 * x : 4 * (15 - x)));
        CHECK(v1[x] == wrap[x]);
    }
}

void testChromaRange() {
    Glide2 g;
    g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    g.ditherMode(kDitherDisable);
    // The "other" colour is the iterated one.
    g.colorCombine(kFuncScaleOther, kFactorOne, kLocalIterated, kOtherIterated, false);
    g.bufferClear(0, 0, 0);
    auto square = [&](float x, int r, int gg, int b) {
        g.drawTriangle(vtx(x, 0, float(r), float(gg), float(b)), vtx(x + 8, 0, float(r), float(gg), float(b)),
                       vtx(x + 8, 8, float(r), float(gg), float(b)));
        g.drawTriangle(vtx(x, 0, float(r), float(gg), float(b)), vtx(x + 8, 8, float(r), float(gg), float(b)),
                       vtx(x, 8, float(r), float(gg), float(b)));
    };
    g.chromakeyMode(1);
    g.chromakeyValue(0xff404040u);
    // "order-insensitive": the bounds given either way round per component.
    g.chromaRange(0xff10ff10u, 0xff800080u, 0);
    g.chromaRangeMode(true);
    square(0, 0x40, 0x40, 0x40);  // in the range: dropped
    square(10, 0x10, 0xff, 0x80); // on its corners: dropped (inclusive)
    square(20, 0x81, 0x40, 0x40); // red past it: drawn
    square(30, 0x40, 0x40, 0x90); // blue past it: drawn
    CHECK(g.lfbRead(kBufferBack, 4, 4, false) == 0 && g.lfbRead(kBufferBack, 14, 4, false) == 0);
    CHECK(g.lfbRead(kBufferBack, 24, 4, false) != 0 && g.lfbRead(kBufferBack, 34, 4, false) != 0);
    // Ranging off: the key alone again ("it will remain enabled").
    g.chromaRangeMode(false);
    square(40, 0x40, 0x40, 0x40); // the key: dropped
    square(50, 0x41, 0x40, 0x40); // drawn
    CHECK(g.lfbRead(kBufferBack, 44, 4, false) == 0 && g.lfbRead(kBufferBack, 54, 4, false) != 0);
    // Not on the Voodoo Graphics.
    Glide2 v1(voodoo1Board());
    v1.chromaRangeMode(true);
    CHECK(!v1.pipeline().state().chromaRange);

    // grGetProcAddress through the binding hands out the procedures where
    // the board has CHROMARANGE, and they work.
    reluva::x86::Cpu cpu(16u << 20);
    cpu.setStack(0x100000);
    reluva::x86::Arena arena(cpu, 0x200000, 0x800000);
    Glide3Dll dll(cpu, arena, bansheeBoard());
    const uint32_t name = arena.alloc(32);
    cpu.writeString(name, "grChromaRangeExt", 32);
    uint32_t proc = 0;
    uint32_t result = 0;
    CHECK(!cpu.call(dll.exports().at("_grGetProcAddress@4"), {name}, proc) && proc != 0);
    CHECK(!cpu.call(proc, {0xff102030u, 0xff050505u, 0}, result));
    CHECK(dll.glide().pipeline().state().chromaRangeMin == 0x050505u);
    CHECK(dll.glide().pipeline().state().chromaRangeMax == 0x102030u);
    cpu.writeString(name, "grChromaRangeModeExt", 32);
    CHECK(!cpu.call(dll.exports().at("_grGetProcAddress@4"), {name}, proc) && proc != 0);
    CHECK(!cpu.call(proc, {1}, result) && dll.glide().pipeline().state().chromaRange);
    cpu.writeString(name, "grTexChromaModeExt", 32); // TEXCHROMA: not offered
    CHECK(!cpu.call(dll.exports().at("_grGetProcAddress@4"), {name}, proc) && proc == 0);
    Glide3Dll v1dll(cpu, arena, voodoo1Board());
    cpu.writeString(name, "grChromaRangeExt", 32);
    CHECK(!cpu.call(v1dll.exports().at("_grGetProcAddress@4"), {name}, proc) && proc == 0);
}

// --- The anti-aliased fringe of clipped primitives ----------------------------------

struct ClipV {
    float x;
    float y;
    float z;
    float w;
    uint32_t argb;
};

Glide3 clipAa() {
    Glide3 g(voodoo2Board());
    g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    Glide2 &c = g.core();
    c.ditherMode(kDitherDisable);
    c.colorCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    c.alphaCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    c.alphaBlendFunction(kBlendSrcAlpha, kBlendOneMinusSrcAlpha, kBlendZero, kBlendZero);
    c.bufferClear(0, 0, 0);
    g.coordinateSpace(g3::kClipCoords);
    g.vertexLayout(g3::kParamXy, 0, g3::kParamEnable);
    g.vertexLayout(g3::kParamZ, 8, g3::kParamEnable);
    g.vertexLayout(g3::kParamW, 12, g3::kParamEnable);
    g.vertexLayout(g3::kParamPargb, 16, g3::kParamEnable);
    return g;
}

const uint8_t *at(const ClipV &v) {
    return reinterpret_cast<const uint8_t *>(&v);
}

void testClippedFringe() {
    // A triangle half through the far plane: (-0.5, -0.5) and (0.5, -0.5)
    // at z 0, (0, 0.5) at z 2. Clipped where z = w, half way up: rows 120
    // (a-b) to 240 (the plane's edge); x 160-480 at the top.
    const ClipV a{-0.5f, -0.5f, 0, 1, 0xffffffffu};
    const ClipV b{0.5f, -0.5f, 0, 1, 0xffffffffu};
    const ClipV c{0, 0.5f, 2, 1, 0xffffffffu};
    Glide3 g = clipAa();
    g.aaDrawTriangle(at(a), at(b), at(c), true, true, true);
    const Glide2 &core = g.core();
    CHECK(green(core, 320, 180) == 63);
    // Outside a-b (row 119, its centre half a pixel above the edge): the
    // fringe, about half covered.
    CHECK(std::abs(green(core, 320, 119) - 31) <= 2);
    // Outside the edge clipping made (row 240): a fringe too, as
    // guAADrawTriangleWithClip's "All edges of the clipped triangle will be
    // anti-aliased".
    CHECK(std::abs(green(core, 320, 240) - 31) <= 2);
    CHECK(green(core, 320, 242) == 0);
    // Outside b-c, by its slanting piece (x ~ 440 at row 180): partial.
    int partial = 0;
    for (int x = 430; x < 460; ++x) {
        const int v = green(core, x, 180);
        partial += v > 0 && v < 63;
    }
    CHECK(partial >= 1);
    // Edges not asked for keep none: only b-c here.
    Glide3 one = clipAa();
    one.aaDrawTriangle(at(a), at(b), at(c), false, true, false);
    CHECK(green(one.core(), 320, 119) == 0); // a-b: none
    CHECK(green(one.core(), 320, 240) > 0);  // the clipped edge: one
    int left = 0;
    for (int x = 185; x < 215; ++x) {
        const int v = green(one.core(), x, 180);
        left += v > 0 && v < 63;
    }
    CHECK(left == 0); // c-a: none
    // GR_AA_ORDERED's grDrawTriangle: every edge, the clipped one too.
    Glide3 ordered = clipAa();
    ordered.enable(g3::kAaOrdered);
    ordered.drawTriangle(at(a), at(b), at(c));
    CHECK(std::abs(green(ordered.core(), 320, 119) - 31) <= 2);
    CHECK(std::abs(green(ordered.core(), 320, 240) - 31) <= 2);
    // Wholly inside, as before: the same pixels as the window-coordinate
    // triangle's.
    Glide3 inside = clipAa();
    const ClipV ia{-0.5f, -0.5f, 0, 1, 0xffffffffu};
    const ClipV ib{0.5f, -0.5f, 0, 1, 0xffffffffu};
    const ClipV ic{0, 0.5f, 0, 1, 0xffffffffu};
    inside.aaDrawTriangle(at(ia), at(ib), at(ic), true, true, true);
    Glide2 window;
    window.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    window.ditherMode(kDitherDisable);
    window.colorCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    window.alphaCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    window.alphaBlendFunction(kBlendSrcAlpha, kBlendOneMinusSrcAlpha, kBlendZero, kBlendZero);
    window.bufferClear(0, 0, 0);
    Glide2Vertex wa = vtx(160, 120);
    Glide2Vertex wb = vtx(480, 120);
    Glide2Vertex wc = vtx(320, 360);
    window.aaDrawTriangle(wa, wb, wc, true, true, true);
    int same = 0;
    for (int y = 110; y < 370; ++y)
        for (int x = 150; x < 490; ++x)
            same += green(window, x, y) == green(inside.core(), x, y);
    CHECK(same == 260 * 340);
    // A GR_POLYGON quad through the far plane: clipped whole, its outside
    // edges with a fringe, the fan's inner edge without.
    Glide3 quad = clipAa();
    quad.enable(g3::kAaOrdered);
    const ClipV q0{-0.5f, -0.5f, 0, 1, 0xffffffffu};
    const ClipV q1{0.5f, -0.5f, 0, 1, 0xffffffffu};
    const ClipV q2{0.5f, 0.5f, 2, 1, 0xffffffffu};
    const ClipV q3{-0.5f, 0.5f, 2, 1, 0xffffffffu};
    quad.drawVertexArray(g3::kPolygon, {at(q0), at(q1), at(q2), at(q3)});
    for (int y = 125; y < 235; y += 10)
        for (int x = 165; x < 475; x += 10)
            CHECK(green(quad.core(), x, y) == 63); // no fringe over the inside
    CHECK(std::abs(green(quad.core(), 320, 240) - 31) <= 2);
    CHECK(std::abs(green(quad.core(), 159, 180) - 31) <= 2);
}

} // namespace

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.glide_boards",
           {
               {"rush desktop", [](const std::filesystem::path &) { testRushDesktop(); }},
               {"banshee", [](const std::filesystem::path &) { testBanshee(); }},
               {"palette6666", [](const std::filesystem::path &) { testPalette6666(); }},
               {"mirror", [](const std::filesystem::path &) { testMirror(); }},
               {"chroma range", [](const std::filesystem::path &) { testChromaRange(); }},
               {"clipped fringe", [](const std::filesystem::path &) { testClippedFringe(); }},
           });
