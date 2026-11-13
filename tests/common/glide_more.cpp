// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// More of the Glide stand-in (graphics/glide, docs/common/graphics/glide.md): the
// linear frame buffer's write formats, colour formats and swizzles; the
// anti-aliased primitives; the utility library (texture manager, .3df
// files, RLE16, endian swaps); the board generations; Glide 3's clipping
// against the view volume.

#include "graphics/glide/glide2.h"
#include "emulation/x86/glide/glide2_x86.h"
#include "graphics/glide/glide3.h"
#include "emulation/x86/glide/glide3_x86.h"
#include "graphics/glide/glide_utils.h"
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

Glide2 openGlide2(int colorFormat = kColorArgb) {
    Glide2 g;
    g.sstWinOpen(0, 7, 0, colorFormat, 0, 2, 1);
    g.ditherMode(kDitherDisable);
    return g;
}

// --- 21c: linear frame buffer write formats ------------------------------------

void testLfbFormats() {
    Glide2 g = openGlide2();
    Glide2LfbLock lock;
    const uint16_t red565 = 31 << 11;
    const uint16_t blue565 = 31;
    // 5:6:5 in each colour format (Programming Guide table 11.2): ABGR and
    // BGRA put blue in the high bits.
    CHECK(g.lfbLock(1, kBufferBack, kLfb565, 0, false, lock));
    g.lfbWrite(lock, 0, 0, red565);
    CHECK(g.lfbRead(kBufferBack, 0, 0, false) == red565);
    g.lfbWriteColorFormat(kColorAbgr);
    g.lfbWrite(lock, 1, 0, red565);
    CHECK(g.lfbRead(kBufferBack, 1, 0, false) == blue565);
    g.lfbWriteColorFormat(kColorRgba);
    g.lfbWrite(lock, 2, 0, red565);
    CHECK(g.lfbRead(kBufferBack, 2, 0, false) == red565);
    // 5:5:5: ARGB x-R-G-B; RGBA R-G-B-x (shifted up one).
    g.lfbWriteColorFormat(kColorArgb);
    CHECK(g.lfbLock(1, kBufferBack, kLfb555, 0, false, lock));
    g.lfbWrite(lock, 3, 0, 31u << 10);
    CHECK(g.lfbRead(kBufferBack, 3, 0, false) == red565);
    g.lfbWriteColorFormat(kColorRgba);
    g.lfbWrite(lock, 4, 0, 31u << 11);
    CHECK(g.lfbRead(kBufferBack, 4, 0, false) == red565);
    g.lfbWriteColorFormat(kColorBgra);
    g.lfbWrite(lock, 5, 0, 31u << 11); // blue in the top field
    CHECK(g.lfbRead(kBufferBack, 5, 0, false) == blue565);
    // 5:5:5 widened by repeating its top bits: green 31 of 5 bits is 63.
    g.lfbWriteColorFormat(kColorArgb);
    g.lfbWrite(lock, 6, 0, 31u << 5);
    CHECK(g.lfbRead(kBufferBack, 6, 0, false) == 63u << 5);
    // 8:8:8 and 8:8:8:8 in the four orders.
    CHECK(g.lfbLock(1, kBufferBack, kLfb888, 0, false, lock));
    g.lfbWrite(lock, 7, 0, 0x00ff0000u);
    CHECK(g.lfbRead(kBufferBack, 7, 0, false) == red565);
    g.lfbWriteColorFormat(kColorRgba);
    g.lfbWrite(lock, 8, 0, 0xff000000u);
    CHECK(g.lfbRead(kBufferBack, 8, 0, false) == red565);
    g.lfbWriteColorFormat(kColorBgra);
    g.lfbWrite(lock, 9, 0, 0xff000000u);
    CHECK(g.lfbRead(kBufferBack, 9, 0, false) == blue565);
    g.lfbWriteColorFormat(kColorAbgr);
    g.lfbWrite(lock, 10, 0, 0x00ff0000u);
    CHECK(g.lfbRead(kBufferBack, 10, 0, false) == blue565);
    // The depth modes: colour in the high half, depth in the low (table
    // 11.3), both written when the pipeline is bypassed.
    g.lfbWriteColorFormat(kColorArgb);
    CHECK(g.lfbLock(1, kBufferBack, kLfb565Depth, 0, false, lock));
    g.lfbWrite(lock, 11, 0, uint32_t(red565) << 16 | 0x1234);
    CHECK(g.lfbRead(kBufferBack, 11, 0, false) == red565);
    CHECK(g.pipeline().buffer(kBufferAux)[11] == 0x1234);
    CHECK(g.lfbLock(1, kBufferBack, kLfb555Depth, 0, false, lock));
    g.lfbWrite(lock, 12, 0, (31u << 10) << 16 | 0x4321);
    CHECK(g.lfbRead(kBufferBack, 12, 0, false) == red565 && g.pipeline().buffer(kBufferAux)[12] == 0x4321);
    // 1555 with depth and alpha buffering on (depth off, alpha written):
    // alpha into the auxiliary buffer, the depth left (Voodoo2
    // specification, lfbMode).
    g.depthBufferMode(kDepthDisable);
    g.colorMask(true, true);
    CHECK(g.lfbLock(1, kBufferBack, kLfb1555Depth, 0, false, lock));
    g.lfbWrite(lock, 13, 0, (0x8000u | 31u) << 16 | 0x5555);
    CHECK(g.lfbRead(kBufferBack, 13, 0, false) == blue565 && g.pipeline().buffer(kBufferAux)[13] == 0xff);
    // A new window goes back to its own format.
    g.sstWinClose();
    g.sstWinOpen(0, 7, 0, kColorAbgr, 0, 2, 1);
    g.ditherMode(kDitherDisable);
    CHECK(g.lfbLock(1, kBufferBack, kLfb888, 0, false, lock));
    g.lfbWrite(lock, 0, 1, 0x000000ffu); // ABGR: red low
    CHECK(g.lfbRead(kBufferBack, 0, 1, false) == red565);
    // Regions take the colour format too.
    g.lfbWriteColorFormat(kColorArgb);
    const uint32_t px = 0x00ff0000u;
    CHECK(g.lfbWriteRegion(kBufferBack, 5, 5, kLfb888, 1, 1, 4, reinterpret_cast<const uint8_t *>(&px)));
    CHECK(g.lfbRead(kBufferBack, 5, 5, false) == red565);
}

void testLfbSwizzle() {
    // Swizzles as the Voodoo2 specification's lfbMode orders them: bytes
    // reversed first, then the 16-bit halves (not for 888 and 8888).
    Glide2 g = openGlide2();
    CHECK(g.lfbSwizzle(kLfb565, 0x11223344u) == 0x11223344u);
    g.lfbWriteColorSwizzle(true, false);
    CHECK(g.lfbSwizzle(kLfb565, 0x11223344u) == 0x44332211u);
    g.lfbWriteColorSwizzle(false, true);
    CHECK(g.lfbSwizzle(kLfb565, 0x11223344u) == 0x33441122u);
    CHECK(g.lfbSwizzle(kLfb8888, 0x11223344u) == 0x11223344u);
    g.lfbWriteColorSwizzle(true, true);
    CHECK(g.lfbSwizzle(kLfb565Depth, 0x11223344u) == 0x22114433u);
    CHECK(g.lfbSwizzle(kLfb888, 0x11223344u) == 0x44332211u);

    // Through the x86 binding: a lock whose writes are byte-swizzled (a
    // big-endian program's pixels).
    reluva::x86::Cpu cpu(16u << 20);
    cpu.setStack(0x100000);
    reluva::x86::Arena arena(cpu, 0x200000, 0x800000);
    Glide2Dll dll(cpu, arena);
    uint32_t result = 0;
    CHECK(!cpu.call(dll.exports().at("_grSstWinOpen@28"), {0, 7, 0, 0, 0, 2, 1}, result) && result == 1);
    CHECK(!cpu.call(dll.exports().at("_grDitherMode@4"), {0}, result));
    CHECK(!cpu.call(dll.exports().at("_grLfbWriteColorSwizzle@8"), {1, 0}, result));
    const uint32_t info = arena.alloc(20);
    cpu.write32(info, 20);
    CHECK(!cpu.call(dll.exports().at("_grLfbLock@24"), {1, kBufferBack, kLfb565, 0, 0, info}, result) && result);
    const uint32_t ptr = cpu.read32(info + 4);
    // Pixels 0 and 1 of row 2 as a big-endian host lays out 0xf800, 0x001f.
    const uint8_t bytes[4] = {0x00, 0x1f, 0xf8, 0x00};
    for (uint32_t i = 0; i < 4; ++i)
        cpu.write8(ptr + 2 * 2048 + i, bytes[i]);
    CHECK(!cpu.call(dll.exports().at("_grLfbUnlock@8"), {1, kBufferBack}, result));
    CHECK(dll.glide().lfbRead(kBufferBack, 0, 2, false) == 0xf800);
    CHECK(dll.glide().lfbRead(kBufferBack, 1, 2, false) == 0x001f);
}

// --- 21b: anti-aliasing --------------------------------------------------------------

Glide2Vertex vtx(float x, float y, float r = 255, float g = 255, float b = 255, float a = 255) {
    Glide2Vertex v;
    v.x = x;
    v.y = y;
    v.r = r;
    v.g = g;
    v.b = b;
    v.a = a;
    v.oow = 1;
    return v;
}

// The Programming Guide's recipe (Example 4.2, back to front): the alpha
// combine giving iterated alpha, blending by source alpha.
void aaSetup(Glide2 &g) {
    g.colorCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    g.alphaCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    g.alphaBlendFunction(kBlendSrcAlpha, kBlendOneMinusSrcAlpha, kBlendZero, kBlendZero);
    g.bufferClear(0, 0, 0);
}

int green(const Glide2 &g, int x, int y) {
    return g.lfbRead(kBufferBack, x, y, false) >> 5 & 63;
}

void testAntialiasing() {
    Glide2 g = openGlide2();
    aaSetup(g);
    // A white triangle with all its edges anti-aliased: inside as
    // grDrawTriangle draws it, a fringe of partial coverage outside, nothing
    // further out.
    g.aaDrawTriangle(vtx(10, 10), vtx(60, 12), vtx(20, 50), true, true, true);
    CHECK(green(g, 25, 20) == 63);
    // Just outside the long edge (60,12)-(20,50): partly covered.
    int partial = 0;
    for (int x = 30; x < 60; ++x) {
        const int v = green(g, x, 40);
        if (v > 0 && v < 63)
            ++partial;
    }
    CHECK(partial >= 1);
    CHECK(green(g, 70, 30) == 0 && green(g, 8, 8) == 0);
    // With no edge anti-aliased it's the plain triangle.
    Glide2 plain = openGlide2();
    Glide2 aa = openGlide2();
    aaSetup(plain);
    aaSetup(aa);
    plain.drawTriangle(vtx(10, 10), vtx(60, 12), vtx(20, 50));
    aa.aaDrawTriangle(vtx(10, 10), vtx(60, 12), vtx(20, 50), false, false, false);
    CHECK(std::memcmp(plain.pipeline().buffer(kBufferBack), aa.pipeline().buffer(kBufferBack), 640 * 480 * 2) == 0);
    // Only edge a-b (the top, y 10 to 12): the fringe is above it alone.
    aa.bufferClear(0, 0, 0);
    aa.aaDrawTriangle(vtx(10, 10), vtx(60, 12), vtx(20, 50), true, false, false);
    CHECK(green(aa, 30, 10) > 0); // above the top edge (at y ~10.8)
    CHECK(green(aa, 5, 30) == 0); // left of the c-a edge: not anti-aliased
    // The same with the vertices given the other way round (b and c
    // swapped): the a-c edge is then the one asked for.
    aa.bufferClear(0, 0, 0);
    aa.aaDrawTriangle(vtx(10, 10), vtx(20, 50), vtx(60, 12), false, false, true);
    CHECK(green(aa, 30, 10) > 0 && green(aa, 5, 30) == 0);

    // A point on a pixel's centre: that pixel only, fully.
    g.bufferClear(0, 0, 0);
    g.aaDrawPoint(vtx(100.5f, 100.5f));
    CHECK(green(g, 100, 100) == 63 && green(g, 101, 100) == 0 && green(g, 100, 101) == 0);
    // On a pixel corner: four pixels, a quarter each ("four pixels, each
    // blended according to the computed pixel coverage").
    g.aaDrawPoint(vtx(201, 201));
    for (int y = 200; y <= 201; ++y)
        for (int x = 200; x <= 201; ++x)
            CHECK(std::abs(green(g, x, y) - 16) <= 2);
    CHECK(green(g, 202, 201) == 0);
    // A horizontal line through pixel centres: its row covered, the rows
    // next to it not; between two rows' centres, half each ("fatter").
    g.aaDrawLine(vtx(300, 300.5f), vtx(340, 300.5f));
    CHECK(green(g, 320, 300) == 63 && green(g, 320, 299) == 0 && green(g, 320, 301) == 0);
    g.aaDrawLine(vtx(300, 320), vtx(340, 320));
    CHECK(std::abs(green(g, 320, 319) - 31) <= 1 && std::abs(green(g, 320, 320) - 31) <= 1);
    // A polygon: its outside edges anti-aliased, the inside ones not (no
    // fringe drawn over the polygon's own pixels).
    g.bufferClear(0, 0, 0);
    const Glide2Vertex quad[4] = {vtx(400, 100), vtx(450, 100), vtx(450, 150), vtx(400, 150)};
    g.aaDrawPolygon(4, quad);
    CHECK(green(g, 425, 125) == 63 && green(g, 420, 140) == 63);
    CHECK(std::abs(green(g, 425, 99) - 31) <= 1);

    // Glide 3: GR_AA_ORDERED makes grDrawTriangle anti-alias every edge.
    Glide3 g3x;
    g3x.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    g3x.core().ditherMode(kDitherDisable);
    aaSetup(g3x.core());
    g3x.vertexLayout(g3::kParamXy, 0, g3::kParamEnable);
    g3x.vertexLayout(g3::kParamPargb, 8, g3::kParamEnable);
    struct V {
        float x, y;
        uint32_t argb;
    } tri[3] = {{10, 10, 0xffffffffu}, {60, 10, 0xffffffffu}, {35, 50, 0xffffffffu}};
    auto bytes = [&](int i) { return reinterpret_cast<const uint8_t *>(&tri[i]); };
    g3x.drawTriangle(bytes(0), bytes(1), bytes(2));
    CHECK((g3x.core().lfbRead(kBufferBack, 35, 9, false) >> 5 & 63) == 0);
    g3x.enable(g3::kAaOrdered);
    g3x.drawTriangle(bytes(0), bytes(1), bytes(2));
    CHECK(std::abs((g3x.core().lfbRead(kBufferBack, 35, 9, false) >> 5 & 63) - 31) <= 1);
}

// --- 21a: the utility library -------------------------------------------------------

void testTextureManager() {
    Glide2 g = openGlide2();
    // Two mipmaps one after the other in TMU0 (256x256 565 with every
    // level, then 64x64 8-bit), the free memory going down by their sizes.
    const uint32_t avail = g.guTexMemQueryAvail(0);
    CHECK(avail == 4u << 20);
    const uint32_t a = g.guTexAllocateMemory(0, kLevelsBoth, 256, 256, kTexRgb565, kMipmapNearest, 8, 0, 3, kClampClamp,
                                             kClampWrap, kFilterBilinear, kFilterPoint, 0.5f, false);
    CHECK(a == 0);
    const uint32_t sizeA = g.texCalcMemRequired(8, 0, 3, kTexRgb565);
    CHECK(g.guTexMemQueryAvail(0) == avail - sizeA);
    const uint32_t b = g.guTexAllocateMemory(0, kLevelsBoth, 64, 64, kTexP8, kMipmapDisable, 2, 2, 3, kClampWrap,
                                             kClampWrap, kFilterPoint, kFilterPoint, 0, false);
    CHECK(b == 1 && g.guTexGetMipMapInfo(b)->start == sizeA);
    CHECK(g.guTexGetCurrentMipMap(0) == Glide2::kNullMipMap);
    // Making one current sets its attributes too.
    g.guTexSource(a);
    const TmuState &t = g.pipeline().state().tmus[0];
    CHECK(g.guTexGetCurrentMipMap(0) == a);
    CHECK(t.hasSource && t.start == 0 && t.clampS && !t.clampT && t.minFilter == kFilterBilinear);
    CHECK(t.mipmapMode == kMipmapNearest && t.lodBias == 0.5f);
    // A download lands at the mipmap's address: a 64x64 P8 level of 7s.
    std::vector<uint8_t> texels(64 * 64, 7);
    g.guTexDownloadMipMap(b, texels.data(), nullptr);
    CHECK(g.pipeline().tmuMemory(0)[sizeA] == 7 && g.pipeline().tmuMemory(0)[sizeA + 4095] == 7);
    // One level at a time: the level's bytes, so the caller's pointer
    // moves to the next.
    std::vector<uint8_t> level(128 * 128 * 2, 0x11);
    CHECK(g.guTexDownloadMipMapLevel(a, 1, level.data()) == 128 * 128 * 2);
    CHECK(g.guTexDownloadMipMapLevel(b, 0, level.data()) == 0); // not one of its levels
    // Changing attributes in place; a larger mipmap doesn't fit.
    CHECK(g.guTexChangeAttributes(b, -1, -1, -1, -1, -1, -1, -1, kClampClamp, -1, -1, -1));
    CHECK(g.guTexGetMipMapInfo(b)->clampS == kClampClamp && g.guTexGetMipMapInfo(b)->info.format == kTexP8);
    CHECK(!g.guTexChangeAttributes(b, -1, -1, kTexRgb565, -1, -1, -1, -1, -1, -1, -1, -1));
    // A mipmap that would cross the 2 MB line starts on it.
    const uint32_t c = g.guTexAllocateMemory(0, kLevelsBoth, 256, 256, kTexRgb565, kMipmapDisable, 0, 0, 3, kClampWrap,
                                             kClampWrap, kFilterPoint, kFilterPoint, 0, false);
    const uint32_t d = g.guTexAllocateMemory(0, kLevelsBoth, 256, 256, kTexRgb565, kMipmapDisable, 0, 0, 3, kClampWrap,
                                             kClampWrap, kFilterPoint, kFilterPoint, 0, false);
    CHECK(c != Glide2::kNullMipMap && d != Glide2::kNullMipMap);
    uint32_t last = d;
    for (int i = 0; i < 40 && last != Glide2::kNullMipMap; ++i) {
        const Glide2::MipMap *m = g.guTexGetMipMapInfo(last);
        CHECK(m->start / (2u << 20) == (m->start + m->size - 1) / (2u << 20));
        last = g.guTexAllocateMemory(0, kLevelsBoth, 256, 256, kTexRgb565, kMipmapDisable, 0, 0, 3, kClampWrap,
                                     kClampWrap, kFilterPoint, kFilterPoint, 0, false);
    }
    CHECK(last == Glide2::kNullMipMap); // 4 MB is full
    // A reset frees everything.
    g.guTexMemReset();
    CHECK(g.guTexMemQueryAvail(0) == avail && !g.guTexGetMipMapInfo(a));
    CHECK(g.guTexGetCurrentMipMap(0) == Glide2::kNullMipMap);
    CHECK(g.guTexAllocateMemory(5, 3, 8, 8, kTexRgb565, 0, 5, 5, 3, 0, 0, 0, 0, 0, false) == Glide2::kNullMipMap);
}

// A .3df file made here (not from any game): a 4x2 ARGB 4444 texture with
// its 2x1 and 1x1 levels.
std::string make3df() {
    std::string f = "3df v1.0\n# a test texture\ncolor format: argb4444\nlod range: 1 4\naspect ratio: 2 1\n";
    for (int i = 0; i < 4 * 2 + 2 + 1; ++i) {
        f += char(0xf0 | (i & 15)); // big-endian: the high byte first
        f += char(i);
    }
    return f;
}

void testUtilities() {
    // .3df files (the TexUS 2.01 guide's format).
    const std::string file = make3df();
    File3df f;
    std::string why;
    CHECK(parse3dfHeader(reinterpret_cast<const uint8_t *>(file.data()), file.size(), f, &why));
    CHECK(f.format == kTexArgb4444 && f.largeLodLog2 == 2 && f.smallLodLog2 == 0 && f.aspectLog2 == 1);
    CHECK(f.width == 4 && f.height == 2 && f.dataBytes == 22);
    std::vector<uint8_t> texels(f.dataBytes);
    CHECK(load3dfData(reinterpret_cast<const uint8_t *>(file.data()), file.size(), f, texels.data()));
    CHECK(texels[0] == 0 && texels[1] == 0xf0 && texels[2] == 1 && texels[3] == 0xf1);
    CHECK(!load3dfData(reinterpret_cast<const uint8_t *>(file.data()), file.size() - 1, f, texels.data()));
    const std::string notOne = "3dx v1.0\n";
    CHECK(!parse3dfHeader(reinterpret_cast<const uint8_t *>(notOne.data()), notOne.size(), f, &why));
    const std::string ncc = "3df v1.0\ncolor format: yiq\nlod range: 8 8\naspect ratio: 1 1\n";
    CHECK(!parse3dfHeader(reinterpret_cast<const uint8_t *>(ncc.data()), ncc.size(), f, &why));

    // RLE16: runs within each row, read back by grLfbWriteRegion.
    const uint16_t image[2 * 5] = {1, 1, 1, 2, 3, 4, 4, 4, 4, 4};
    const uint32_t bytes = encodeRle16(nullptr, image, 5, 2);
    CHECK(bytes == 4 * 4);
    std::vector<uint8_t> rle(bytes);
    encodeRle16(rle.data(), image, 5, 2);
    CHECK(rle[0] == 3 && rle[1] == 0 && rle[2] == 1 && rle[3] == 0);
    Glide2 g = openGlide2();
    CHECK(g.lfbWriteRegion(kBufferBack, 10, 10, kLfbRle16, 5, 2, 0, rle.data(), rle.size()));
    for (int i = 0; i < 10; ++i)
        CHECK(g.lfbRead(kBufferBack, 10 + i % 5, 10 + i / 5, false) == image[i]);
    CHECK(endianSwapWords(0x11223344u) == 0x33441122u && endianSwapBytes(0x1122) == 0x2211);

    // Through the binding: the texture manager, gu3dfGetInfo/gu3dfLoad with
    // a host file reader, guEncodeRLE16.
    reluva::x86::Cpu cpu(16u << 20);
    cpu.setStack(0x100000);
    reluva::x86::Arena arena(cpu, 0x200000, 0x800000);
    Glide2Dll dll(cpu, arena);
    dll.readFile = [&](const std::string &path, std::vector<uint8_t> &out) {
        if (path != "C:\\TEX\\TEST.3DF")
            return false;
        out.assign(file.begin(), file.end());
        return true;
    };
    uint32_t r = 0;
    auto call = [&](const char *name, const std::vector<uint32_t> &args) {
        r = 0xdeadbeef;
        CHECK(!cpu.call(dll.exports().at(name), args, r));
        return r;
    };
    call("_grSstWinOpen@28", {0, 7, 0, 0, 0, 2, 1});
    const uint32_t name = arena.alloc(32);
    cpu.writeString(name, "C:\\TEX\\TEST.3DF", 32);
    const uint32_t info = arena.alloc(1056);
    CHECK(call("_gu3dfGetInfo@8", {name, info}) == 1);
    CHECK(cpu.read32(info) == 4 && cpu.read32(info + 4) == 2);
    CHECK(cpu.read32(info + 8) == 8 && cpu.read32(info + 12) == 6);                       // GR_LOD_1, GR_LOD_4
    CHECK(cpu.read32(info + 16) == 2 && cpu.read32(info + 20) == uint32_t(kTexArgb4444)); // GR_ASPECT_2x1
    CHECK(cpu.read32(info + 1052) == 22);
    const uint32_t data = arena.alloc(22);
    cpu.write32(info + 1048, data);
    CHECK(call("_gu3dfLoad@8", {name, info}) == 1);
    CHECK(cpu.read8(data + 1) == 0xf0 && cpu.read8(data + 21) == 0xfa);
    cpu.writeString(name, "C:\\TEX\\NONE.3DF", 32);
    CHECK(call("_gu3dfGetInfo@8", {name, info}) == 0);
    // guTexAllocateMemory(tmu, mask, w, h, fmt, mm, small, large, aspect,
    // sclamp, tclamp, min, mag, bias, blend), then its info.
    float bias = 0;
    uint32_t biasBits = 0;
    std::memcpy(&biasBits, &bias, 4);
    const uint32_t id =
        call("_guTexAllocateMemory@60", {0, 3, 4, 2, uint32_t(kTexArgb4444), 0, 8, 6, 2, 1, 1, 0, 0, biasBits, 0});
    CHECK(id == 0);
    CHECK(call("_guTexDownloadMipMap@12", {id, data, 0}) == 0);
    call("_guTexSource@4", {id});
    CHECK(call("_guTexGetCurrentMipMap@4", {0}) == id);
    const uint32_t mi = call("_guTexGetMipMapInfo@4", {id});
    CHECK(mi && cpu.read32(mi + 4) == 1 && cpu.read32(mi + 8) == 4 && cpu.read32(mi + 24) == uint32_t(kTexArgb4444));
    CHECK(cpu.read32(mi + 40) == 1 && cpu.read32(mi + 76) == 0); // s clamp; tmu_base_address
    CHECK(dll.glide().pipeline().tmuMemory(0)[1] == 0xf0);
    CHECK(call("_guTexMemQueryAvail@4", {0}) == (4u << 20) - dll.glide().texCalcMemRequired(8, 6, 2, kTexArgb4444));
    CHECK(call("_guEndianSwapBytes@4", {0x1122}) == 0x2211);
    const uint32_t src = arena.alloc(20);
    const uint32_t dst = arena.alloc(64);
    for (uint32_t i = 0; i < 10; ++i)
        cpu.write16(src + 2 * i, image[i]);
    CHECK(call("_guEncodeRLE16@16", {0, src, 5, 2}) == 16);
    CHECK(call("_guEncodeRLE16@16", {dst, src, 5, 2}) == 16 && cpu.read16(dst + 4) == 1 && cpu.read16(dst + 6) == 2);
}

// --- 21d: the board generations ------------------------------------------------------

void testBoards() {
    Board b;
    CHECK(boardByName("voodoo1", b) && b.generation == Generation::Voodoo1 && b.tmus == 1);
    CHECK(boardByName("rush", b) && b.windowed && !b.palette);
    CHECK(boardByName("voodoo3", b) && b.uma && b.tmus == 2);
    CHECK(!boardByName("voodoo5", b) && !boardByName("voodoo1,x=1", b));
    CHECK(boardByName("voodoo1,tmu=4,tmurev=0", b) && b.tmuMemory == 4u << 20 && !b.palette);

    // Voodoo Graphics: 800x600 at most (Programming Guide table 3.3), not
    // GR_RESOLUTION_NONE; grSstQueryHardware says SST-1 with one TMU.
    Glide2 v1(voodoo1Board());
    CHECK(!v1.sstWinOpen(0, 12, 0, kColorArgb, 0, 2, 1)); // 1024x768
    CHECK(!v1.sstWinOpen(0, Glide2::kResolutionNone, 0, kColorArgb, 0, 2, 1));
    CHECK(v1.sstWinOpen(0, 8, 0, kColorArgb, 0, 2, 0)); // 800x600
    Glide2HwConfig hw;
    CHECK(v1.sstQueryHardware(hw) && hw.type == 0 && hw.nTexelfx == 1 && hw.tmuRamMb[0] == 2 && hw.tmuRamMb[1] == 0);
    CHECK(v1.texMaxAddress(0) == (2u << 20) - 8);
    // 2 MB of frame buffer (table 3.3): 800x600 double-buffered, but not
    // with a depth buffer; 640x480 with one.
    v1.sstWinClose();
    CHECK(v1.sstWinOpen(0, 8, 0, kColorArgb, 0, 2, 0));
    v1.sstWinClose();
    CHECK(!v1.sstWinOpen(0, 8, 0, kColorArgb, 0, 2, 1));
    CHECK(v1.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1));
    v1.sstWinClose();
    // A TMU of revision 0: no palette.
    Board rev0 = voodoo1Board();
    rev0.tmuRevision = 0;
    rev0.palette = false;
    Glide2 r0(rev0);
    r0.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    std::vector<uint32_t> pal(256, 0x00ff0000u);
    r0.texDownloadTable(0, kTablePalette, reinterpret_cast<const uint8_t *>(pal.data()));
    CHECK(r0.pipeline().palette(0)[5] == 0);

    // Voodoo Rush: windowed Glide, the window's size.
    Glide2 rush(voodooRushBoard());
    rush.windowSize = [](uint32_t hwnd, int &w, int &h) {
        w = 320;
        h = 200;
        return hwnd == 7;
    };
    CHECK(rush.sstWinOpen(7, Glide2::kResolutionNone, 0, kColorArgb, 0, 2, 1));
    CHECK(rush.sstScreenWidth() == 320 && rush.sstScreenHeight() == 200);
    rush.texDownloadTable(0, kTablePalette, reinterpret_cast<const uint8_t *>(pal.data()));
    CHECK(rush.pipeline().palette(0)[5] == 0);

    // Voodoo3: unified memory. Textures start after the open window's
    // buffers, aligned to 16; what TMU0 is given TMU1 reads too.
    Glide3 v3;
    CHECK(v3.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1));
    const uint32_t buffers = 640 * 480 * 2 * 3;
    CHECK(v3.core().texMinAddress(0) == (buffers + 15) / 16 * 16);
    CHECK(v3.core().texMaxAddress(0) == (16u << 20) - 16); // a 1x1 level's start, 16-byte aligned
    int32_t v[4] = {};
    CHECK(v3.get(g3::kTextureAlign, 4, v) == 4 && v[0] == 16);
    CHECK(v3.get(g3::kGammaTableEntries, 4, v) == 4 && v[0] == 256);
    CHECK(v3.get(g3::kMemoryTmu, 4, v) == 4 && v[0] == 0);
    const uint8_t texel[2] = {0x34, 0x12};
    const uint32_t at = v3.core().texMinAddress(0);
    v3.texDownloadMipMap(0, at, kLevelsBoth, {0, 0, 0, kTexRgb565}, texel);
    CHECK(v3.pipeline().tmuMemory(1)[at] == 0x34 && v3.pipeline().tmuMemory(1)[at + 1] == 0x12);
    // grLoadGammaTable: 256 entries on the Voodoo3, read through.
    std::vector<uint32_t> inv(256);
    for (uint32_t i = 0; i < 256; ++i)
        inv[i] = 255 - i;
    v3.pipeline().loadGammaTable(256, inv.data(), inv.data(), inv.data());
    v3.core().ditherMode(kDitherDisable);
    v3.core().bufferClear(0xffffffffu, 0, 0);
    v3.core().bufferSwap(0);
    reluva::Surface shown;
    v3.pipeline().present(shown);
    CHECK(shown.row(0)[0] == 0 && shown.row(0)[1] == 0);
    // Only the first entries given: the rest as the gamma value made them
    // (1.0: the identity), interpolated over the board's 32 on a Voodoo2.
    Glide2 v2g;
    v2g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    v2g.ditherMode(kDitherDisable);
    const uint32_t zero[4] = {0, 0, 0, 0};
    v2g.pipeline().loadGammaTable(4, zero, zero, zero);
    v2g.bufferClear(0xff808080u, 0, 0);
    v2g.bufferSwap(0);
    v2g.pipeline().present(shown);
    CHECK(std::abs(int(shown.row(0)[0]) - 132) <= 4);
    v2g.gammaCorrectionValue(1.0f); // a gamma value replaces the table
    v2g.pipeline().present(shown);
    CHECK(std::abs(int(shown.row(0)[0]) - 132) <= 4);
    // Glide 3's hardware names by generation.
    CHECK(Glide3(voodoo1Board()).getString(g3::kHardware) == "Voodoo Graphics");
    CHECK(Glide3(voodoo2Board()).get(g3::kMemoryUma, 4, v) == 4 && v[0] == 0);
}

// --- 22a: clip coordinates clipped against the view volume ---------------------------

void testClipCoordinates() {
    Glide3 g(voodoo2Board());
    g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    g.core().ditherMode(kDitherDisable);
    g.core().colorCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    g.core().bufferClear(0, 0, 0);
    g.coordinateSpace(g3::kClipCoords);
    struct V {
        float x, y, z, w, r, g, b;
    };
    g.vertexLayout(g3::kParamXy, 0, g3::kParamEnable);
    g.vertexLayout(g3::kParamZ, 8, g3::kParamEnable);
    g.vertexLayout(g3::kParamW, 12, g3::kParamEnable);
    g.vertexLayout(g3::kParamRgb, 16, g3::kParamEnable);
    auto at = [](const V &v) { return reinterpret_cast<const uint8_t *>(&v); };
    auto lit = [&](int x, int y) { return g.core().lfbRead(kBufferBack, x, y, false) != 0; };
    int count = 0;
    auto litPixels = [&] {
        count = 0;
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x)
                count += lit(x, y);
        return count;
    };
    // Inside the volume: drawn as before (the viewport: x = (x/w + 1) 320).
    const V a{-0.5f, -0.5f, 0, 1, 1, 1, 1};
    const V b{0.5f, -0.5f, 0, 1, 1, 1, 1};
    const V c{0, 0.5f, 0, 1, 1, 1, 1};
    g.drawTriangle(at(a), at(b), at(c));
    CHECK(lit(320, 240) && !lit(100, 100));
    const int inside = litPixels();
    CHECK(inside > 30000 && inside < 40000);
    // One vertex behind the eye (w < 0): only the part in front is drawn,
    // nothing wraps round through the division.
    g.core().bufferClear(0, 0, 0);
    const V behind{0, 3, 0, -1, 1, 1, 1};
    g.drawTriangle(at(a), at(b), at(behind));
    const int front = litPixels();
    CHECK(front > 0 && front < 640 * 480);
    CHECK(!lit(320, 100)); // above a-b (y/w -0.5): nothing
    // Entirely beyond the far plane (z > w): nothing.
    g.core().bufferClear(0, 0, 0);
    const V fa{-0.5f, -0.5f, 2, 1, 1, 1, 1};
    const V fb{0.5f, -0.5f, 2, 1, 1, 1, 1};
    const V fc{0, 0.5f, 2, 1, 1, 1, 1};
    g.drawTriangle(at(fa), at(fb), at(fc));
    CHECK(litPixels() == 0);
    // Half through the far plane: the near half only.
    const V ha{-0.5f, -0.5f, 0, 1, 1, 1, 1};
    const V hb{0.5f, -0.5f, 0, 1, 1, 1, 1};
    const V hc{0, 0.5f, 2, 1, 1, 1, 1};
    g.drawTriangle(at(ha), at(hb), at(hc));
    const int half = litPixels();
    CHECK(half > inside / 2 && half < inside);
    CHECK(lit(320, 125) && !lit(320, 355));
    // A line from in front of the eye to behind it: clipped at w = 0.
    g.core().bufferClear(0, 0, 0);
    const V l0{0, 0, 0, 1, 1, 1, 1};
    const V l1{0, 2, 0, -1, 1, 1, 1};
    g.drawLine(at(l0), at(l1));
    CHECK(litPixels() > 0 && litPixels() < 640);
    // A point outside: not drawn.
    const V out{2, 0, 0, 1, 1, 1, 1};
    g.core().bufferClear(0, 0, 0);
    g.drawPoint(at(out));
    CHECK(litPixels() == 0);
}

} // namespace

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.glide_more",
           {
               {"lfb formats", [](const std::filesystem::path &) { testLfbFormats(); }},
               {"lfb swizzle", [](const std::filesystem::path &) { testLfbSwizzle(); }},
               {"antialiasing", [](const std::filesystem::path &) { testAntialiasing(); }},
               {"texture manager", [](const std::filesystem::path &) { testTextureManager(); }},
               {"utilities", [](const std::filesystem::path &) { testUtilities(); }},
               {"boards", [](const std::filesystem::path &) { testBoards(); }},
               {"clip coordinates", [](const std::filesystem::path &) { testClipCoordinates(); }},
           });
