// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Glide stand-in's core (graphics/glide): the dither, texture formats and
// memory layout, the combine units, the fog table, depth, chroma-key, the
// fill rule and the linear frame buffer; the Glide 2 front end and the
// x86 binding's exports.

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
#include <set>
#include <string>
#include <vector>

namespace {

using namespace reluva::glide;

namespace {

void testDither() {
    // Off: truncation.
    CHECK(to565(255, 255, 255, 0, 0, kDitherDisable) == 0xffff);
    CHECK(to565(7, 3, 7, 0, 0, kDitherDisable) == 0);
    CHECK(to565(8, 4, 8, 0, 0, kDitherDisable) == (1 << 11 | 1 << 5 | 1));
    // On: full white and black stay; a level between two steps averages
    // to itself over the matrix.
    for (int mode : {kDither4x4, kDither2x2}) {
        CHECK(to565(255, 255, 255, 1, 2, mode) == 0xffff);
        CHECK(to565(0, 0, 0, 3, 1, mode) == 0);
        const int n = mode == kDither4x4 ? 4 : 2;
        for (int level : {20, 100, 133, 200}) {
            double sum = 0;
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                    sum += double(to565(level, 0, 0, x, y, mode) >> 11) * 255.0 / 31.0;
            CHECK(std::fabs(sum / (n * n) - level) < 255.0 / 31.0 / (n * n) + 0.5);
        }
    }
    // The 4x4 matrix's thresholds differ across the block.
    std::set<uint16_t> seen;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            seen.insert(to565(100, 100, 100, x, y, kDither4x4));
    CHECK(seen.size() > 1);
    // 5:6:5 widens by repeating the top bits.
    const Rgba c = from565(0xf81f);
    CHECK(c.r == 255 && c.g == 0 && c.b == 255);
    CHECK(from565(0x0821).r == 8 && from565(0x0821).g == 4 && from565(0x0821).b == 8);
}

void testFormats() {
    uint32_t palette[256] = {};
    palette[7] = 0x00123456;
    palette[200] = 0xff0000ff; // the top byte isn't the palette's
    NccTable ncc;
    for (int i = 0; i < 16; ++i)
        ncc.y[i] = uint8_t(i * 16);
    ncc.i[1][0] = 10;
    ncc.q[2][2] = -20;
    CHECK(decodeTexel(kTexRgb565, 0xf800, palette, ncc) == 0xffff0000u);
    CHECK(decodeTexel(kTexRgb565, 0x07e0, palette, ncc) == 0xff00ff00u);
    CHECK(decodeTexel(kTexRgb565, 0x0841, palette, ncc) == 0xff080808u);
    CHECK(decodeTexel(kTexArgb1555, 0x7c00, palette, ncc) == 0x00ff0000u);
    CHECK(decodeTexel(kTexArgb1555, 0x801f, palette, ncc) == 0xff0000ffu);
    CHECK(decodeTexel(kTexArgb4444, 0x8f37, palette, ncc) == 0x88ff3377u);
    CHECK(decodeTexel(kTexP8, 7, palette, ncc) == 0xff123456u);
    CHECK(decodeTexel(kTexP8, 200, palette, ncc) == 0xff0000ffu);
    CHECK(decodeTexel(kTexAp88, 0x8007, palette, ncc) == 0x80123456u);
    CHECK(decodeTexel(kTexRgb332, 0xe0, palette, ncc) == 0xffff0000u);
    CHECK(decodeTexel(kTexRgb332, 0x03, palette, ncc) == 0xff0000ffu);
    CHECK(decodeTexel(kTexRgb332, 0x24, palette, ncc) == 0xff242400u); // r 1 -> 0x24, g 1 -> 0x24
    CHECK(decodeTexel(kTexArgb8332, 0x40e0, palette, ncc) == 0x40ff0000u);
    CHECK(decodeTexel(kTexAlpha8, 0x5a, palette, ncc) == 0x5a5a5a5au);
    CHECK(decodeTexel(kTexIntensity8, 0x5a, palette, ncc) == 0xff5a5a5au);
    CHECK(decodeTexel(kTexAlphaIntensity44, 0x3c, palette, ncc) == 0x33ccccccu);
    CHECK(decodeTexel(kTexAlphaIntensity88, 0x7f10, palette, ncc) == 0x7f101010u);
    // YIQ: Y[5] = 80, I[1] adds 10 to red, Q[2] takes 20 from blue.
    CHECK(decodeTexel(kTexYiq422, 0x5 << 4 | 1 << 2 | 2, palette, ncc) == 0xff5a503cu);
    CHECK(decodeTexel(kTexAyiq8422, 0x8000 | 0x5 << 4 | 1 << 2 | 2, palette, ncc) == 0x805a503cu);
    CHECK(texelBytes(kTexP8) == 1 && texelBytes(kTexArgb4444) == 2);
}

void testLayout() {
    int w = 0;
    int h = 0;
    levelSize(8, 3, w, h); // 8x1 at 256: 256x32
    CHECK(w == 256 && h == 32);
    levelSize(2, -2, w, h); // 1x4 at 4: 1x4
    CHECK(w == 1 && h == 4);
    levelSize(1, 3, w, h);
    CHECK(w == 2 && h == 1);
    // The Voodoo2 specification's table, in 8-byte units for 16-bit texels.
    CHECK(levelBytes(8, 0, kTexRgb565) == (1u << 14) * 8);
    CHECK(levelBytes(8, 3, kTexRgb565) == (1u << 11) * 8);
    CHECK(levelBytes(3, 3, kTexRgb565) == 4u * 8);        // 8x1: 2^2
    CHECK(levelBytes(2, 2, kTexRgb565) == 2u * 8);        // 4x1: 2^1
    CHECK(levelBytes(1, 0, kTexRgb565) == 1u * 8);        // 2x2
    CHECK(levelBytes(0, 0, kTexRgb565) == 1u * 8);        // 1x1
    CHECK(levelBytes(5, 0, kTexRgb565) == (1u << 8) * 8); // 32x32
    // Its first example: 16-bit 1:1 levels 128 and 64. Its second and third
    // (8-bit 8:1 levels 16-1) count 8x1 and 4x1 levels as 2 and 1 units
    // where its table has 4 and 2; Reluva follows the table (8 + 4 + 2 + 1
    // + 1 half units, rounded up; even levels 8 + 2 + 1).
    CHECK(textureBytes({6, 7, 0, kTexRgb565}, kLevelsBoth) == 0x1400u * 8);
    CHECK(textureBytes({0, 4, 3, kTexP8}, kLevelsBoth) == 8u * 8);
    CHECK(textureBytes({0, 4, 3, kTexP8}, kLevelsEven) == 6u * 8);
    // Levels follow each other; with only odd levels the even ones take no room.
    const TextureInfo t{0, 8, 0, kTexRgb565};
    CHECK(levelOffset(t, kLevelsBoth, 8) == 0);
    CHECK(levelOffset(t, kLevelsBoth, 7) == levelBytes(8, 0, kTexRgb565));
    CHECK(levelOffset(t, kLevelsOdd, 5) == levelBytes(7, 0, kTexRgb565));
}

void testCombine() {
    CHECK(scaleBy(200, 255) == 200);
    CHECK(scaleBy(200, 0) == 0);
    CHECK(scaleBy(128, 128) == 64);
    CHECK(scaleBy(-100, 255) == -100);
    // Texture times vertex colour (voodoo2a.dll's settexture: SCALE_OTHER,
    // factor LOCAL, local iterated, other texture).
    PixelCombine p;
    p.color = {kFuncScaleOther, kFactorLocal, false};
    p.alpha = {kFuncScaleOther, kFactorLocal, false};
    p.colorLocal = kLocalIterated;
    p.colorOther = kOtherTexture;
    p.alphaLocal = kLocalIterated;
    p.alphaOther = kOtherTexture;
    Rgba other;
    Rgba out = combinePixel(p, {255, 128, 0, 255}, {255, 128, 200, 128}, {}, 0, &other);
    CHECK(out.r == 255 && out.g == 64 && out.b == 0 && out.a == 128);
    CHECK(other.r == 255 && other.b == 200);
    // Blend: f * other + (1 - f) * local, with f the texture's alpha.
    p.color = {kFuncScaleOtherMinusLocalAddLocal, kFactorTextureAlpha, false};
    out = combinePixel(p, {0, 0, 0, 255}, {255, 255, 255, 255}, {}, 0, nullptr);
    CHECK(out.r == 255);
    // Invert, and the local alpha added.
    p.color = {kFuncZero, kFactorZero, true};
    CHECK(combinePixel(p, {}, {}, {}, 0, nullptr).g == 255);
    p.color = {kFuncLocalAlpha, kFactorZero, false};
    CHECK(combinePixel(p, {1, 2, 3, 77}, {}, {}, 0, nullptr).b == 77);
    // Constant colour and depth as the alpha's local.
    p.alpha = {kFuncLocal, kFactorZero, false};
    p.alphaLocal = kLocalDepth;
    CHECK(combinePixel(p, {}, {}, {}, 0x5c, nullptr).a == 0x5c);
    // The texture unit: modulate with the TMU above.
    out = combineTexture({kFuncScaleOther, kFactorLocal, false}, {kFuncScaleOther, kFactorLocalAlpha, false},
                         {255, 0, 128, 255}, {100, 100, 100, 50}, 0, 0);
    CHECK(out.r == 100 && out.g == 0 && out.b == 50 && out.a == 50);
}

void testFogAndDepth() {
    // The w float: 1/w of 1 or more nearest, below 2^-16 farthest, growing
    // with w.
    CHECK(floatDepth(1.0) == 0);
    CHECK(floatDepth(2.0) == 0);
    CHECK(floatDepth(1.0 / 70000.0) == 0xffff);
    uint16_t last = 0;
    for (double w = 1.01; w < 60000; w *= 1.07) {
        const uint16_t d = floatDepth(1.0 / w);
        CHECK(d >= last);
        last = d;
    }
    // Table entries fall on Glide's w for each index (guFogTableIndexToW).
    uint8_t table[64];
    for (int i = 0; i < 64; ++i)
        table[i] = uint8_t(i * 4);
    CHECK(fogTableFactor(table, floatDepth(1.0)) == 0);
    CHECK(std::abs(fogTableFactor(table, floatDepth(1.0 / 2.0)) - 16) <= 1);  // w 2: entry 4
    CHECK(std::abs(fogTableFactor(table, floatDepth(1.0 / 16.0)) - 64) <= 1); // w 16: entry 16
    CHECK(std::abs(fogTableFactor(table, floatDepth(7.0 / 8.0)) - 4) <= 1);   // w 8/7: entry 1
    // Half way between entries 4 and 5 (w 2 and 16/7).
    const int mid = fogTableFactor(table, floatDepth(1.0 / 2.12));
    CHECK(mid > 16 && mid < 20);
    // The fog unit.
    const Rgba fogged = applyFog({0, 100, 200, 77}, {255, 255, 255, 0}, 255, kFogWithTable);
    CHECK(fogged.r == 255 && fogged.g == 255 && fogged.b == 255 && fogged.a == 77);
    CHECK(applyFog({0, 100, 200, 77}, {255, 255, 255, 0}, 0, kFogWithTable).g == 100);
    CHECK(applyFog({100, 100, 100, 0}, {255, 255, 255, 0}, 255, kFogWithTable | kFogMult2).r == 255);
    CHECK(applyFog({100, 100, 100, 0}, {255, 255, 255, 0}, 255, kFogWithTable | kFogAdd2).r == 0);
    CHECK(compare(kCmpLess, 1, 2) && !compare(kCmpLess, 2, 2) && compare(kCmpGequal, 2, 2) &&
          !compare(kCmpNever, 0, 0));
    // Blending: source alpha over.
    const Rgba blended = blendPixel({255, 0, 0, 128}, {0, 0, 255, 255}, {}, kBlendSrcAlpha, kBlendOneMinusSrcAlpha,
                                    kBlendOne, kBlendZero);
    CHECK(std::abs(blended.r - 128) <= 1 && std::abs(blended.b - 127) <= 1);
    // DST_COLOR as the source factor: a multiply.
    CHECK(blendPixel({128, 0, 0, 255}, {128, 0, 0, 255}, {}, kBlendSrcColor, kBlendZero, kBlendOne, kBlendZero).r ==
          64);
}

Vertex vtx(float x, float y, float r = 255, float g = 255, float b = 255, float a = 255) {
    Vertex v;
    v.x = x;
    v.y = y;
    v.r = r;
    v.g = g;
    v.b = b;
    v.a = a;
    v.oow = 1;
    for (auto &t : v.tmu)
        t.oow = 1;
    return v;
}

// A pipeline drawing iterated colour, undithered.
Pipeline flatPipeline() {
    Pipeline p;
    p.open(64, 32, 2, 1);
    State &s = p.state();
    s.color = {{kFuncLocal, kFactorZero, false}, kLocalIterated, kOtherIterated};
    s.alpha = {{kFuncLocal, kFactorZero, false}, kLocalIterated, kOtherIterated};
    s.dither = kDitherDisable;
    return p;
}

void testRasterizer() {
    // Two triangles sharing an edge, added together: each pixel drawn once.
    Pipeline p = flatPipeline();
    State &s = p.state();
    s.blendRgbSrc = kBlendOne;
    s.blendRgbDst = kBlendOne;
    p.clear(0, 0, 0);
    p.triangle(vtx(3, 2, 0, 0, 40), vtx(40, 5, 0, 0, 40), vtx(10, 30, 0, 0, 40));
    p.triangle(vtx(40, 5, 0, 0, 40), vtx(50, 31, 0, 0, 40), vtx(10, 30, 0, 0, 40));
    const uint16_t *back = p.buffer(kBufferBack);
    int once = 0;
    int twice = 0;
    for (int i = 0; i < 64 * 32; ++i) {
        const int b = from565(back[i]).b;
        once += b > 30 && b < 50;
        twice += b > 70;
    }
    CHECK(once > 300);
    CHECK(twice == 0);
    // A pixel centre on a left or top edge is in, on a right or bottom edge
    // out: a 4x4 square from (2,2) to (6,6) covers exactly 16 pixels.
    Pipeline q = flatPipeline();
    q.clear(0, 0, 0);
    q.triangle(vtx(2, 2), vtx(6, 2), vtx(6, 6));
    q.triangle(vtx(2, 2), vtx(6, 6), vtx(2, 6));
    int lit = 0;
    for (int i = 0; i < 64 * 32; ++i)
        lit += q.buffer(kBufferBack)[i] != 0;
    CHECK(lit == 16);
    CHECK(q.buffer(kBufferBack)[2 * 64 + 2] == 0xffff && q.buffer(kBufferBack)[5 * 64 + 5] == 0xffff);
    CHECK(q.buffer(kBufferBack)[6 * 64 + 6] == 0);
    // Culling by the sign of the area: clockwise on screen is positive.
    q.state().cull = kCullPositive;
    q.clear(0, 0, 0);
    q.triangle(vtx(2, 2), vtx(6, 2), vtx(6, 6));
    CHECK(q.buffer(kBufferBack)[2 * 64 + 4] == 0);
    q.state().cull = kCullNegative;
    q.triangle(vtx(2, 2), vtx(6, 2), vtx(6, 6));
    CHECK(q.buffer(kBufferBack)[2 * 64 + 4] == 0xffff);
    // The clip window.
    q.state().cull = kCullDisable;
    q.clear(0, 0, 0);
    q.state().clip[0] = 4;
    q.triangle(vtx(0, 0), vtx(20, 0), vtx(0, 20));
    CHECK(q.buffer(kBufferBack)[1 * 64 + 3] == 0 && q.buffer(kBufferBack)[1 * 64 + 4] == 0xffff);
}

void testDepthAndChroma() {
    Pipeline p = flatPipeline();
    State &s = p.state();
    s.depthMode = kDepthZBuffer;
    s.depthFunc = kCmpLess;
    s.depthMask = true;
    p.clear(0, 0, 0xffff);
    Vertex a = vtx(0, 0, 255, 0, 0);
    Vertex b = vtx(30, 0, 255, 0, 0);
    Vertex c = vtx(0, 30, 255, 0, 0);
    a.ooz = b.ooz = c.ooz = 1000;
    p.triangle(a, b, c);
    // Behind: hidden.
    Vertex d = vtx(0, 0, 0, 0, 255);
    Vertex e = vtx(30, 0, 0, 0, 255);
    Vertex f = vtx(0, 30, 0, 0, 255);
    d.ooz = e.ooz = f.ooz = 2000;
    p.triangle(d, e, f);
    CHECK(from565(p.buffer(kBufferBack)[5 * 64 + 5]).r == 255);
    CHECK(p.buffer(kBufferAux)[5 * 64 + 5] == 1000);
    // In front: drawn.
    d.ooz = e.ooz = f.ooz = 500;
    p.triangle(d, e, f);
    CHECK(from565(p.buffer(kBufferBack)[5 * 64 + 5]).b == 255);
    // Compare to bias: the bias is compared, the true depth written.
    s.depthMode = kDepthZCompareToBias;
    s.depthBias = 0;
    s.depthFunc = kCmpNotEqual;
    d.ooz = e.ooz = f.ooz = 3000;
    p.triangle(d, e, f);
    CHECK(p.buffer(kBufferAux)[5 * 64 + 5] == 3000);
    // W buffer: the float of 1/w.
    s.depthMode = kDepthWBuffer;
    s.depthFunc = kCmpAlways;
    d.oow = e.oow = f.oow = 0.25f;
    p.triangle(d, e, f);
    CHECK(p.buffer(kBufferAux)[5 * 64 + 5] == floatDepth(0.25));
    // Chroma-key compares the colour combine's "other" colour.
    Pipeline k = flatPipeline();
    k.state().color.other = kOtherIterated;
    k.state().color.settings = {kFuncScaleOther, kFactorOne, false};
    k.state().chromakey = true;
    k.state().chromakeyColor = 0x00ff00ff;
    k.clear(0, 0, 0);
    k.triangle(vtx(0, 0, 255, 0, 255), vtx(30, 0, 255, 0, 255), vtx(0, 30, 255, 0, 255));
    CHECK(k.buffer(kBufferBack)[2 * 64 + 2] == 0);
    CHECK(k.stats.chromaFail > 0);
    k.triangle(vtx(0, 0, 255, 0, 254), vtx(30, 0, 255, 0, 254), vtx(0, 30, 255, 0, 254));
    CHECK(k.buffer(kBufferBack)[2 * 64 + 2] != 0);
    // Alpha test: greater than the reference.
    Pipeline t = flatPipeline();
    t.state().alphaTest = kCmpGreater;
    t.state().alphaRef = 0x10;
    t.clear(0, 0, 0);
    t.triangle(vtx(0, 0, 255, 255, 255, 16), vtx(30, 0, 255, 255, 255, 16), vtx(0, 30, 255, 255, 255, 16));
    CHECK(t.buffer(kBufferBack)[2 * 64 + 2] == 0);
    t.triangle(vtx(0, 0, 255, 255, 255, 17), vtx(30, 0, 255, 255, 255, 17), vtx(0, 30, 255, 255, 255, 17));
    CHECK(t.buffer(kBufferBack)[2 * 64 + 2] == 0xffff);
}

void testTableFog() {
    Pipeline p = flatPipeline();
    State &s = p.state();
    s.fogMode = kFogWithTable;
    s.fogColor = 0x0000ff;
    for (int i = 0; i < 64; ++i)
        s.fogTable[size_t(i)] = i < 32 ? 0 : 255;
    p.clear(0, 0, 0);
    Vertex a = vtx(0, 0, 255, 0, 0);
    Vertex b = vtx(30, 0, 255, 0, 0);
    Vertex c = vtx(0, 30, 255, 0, 0);
    a.oow = b.oow = c.oow = 1.0f; // near: no fog
    p.triangle(a, b, c);
    CHECK(from565(p.buffer(kBufferBack)[3 * 64 + 3]).r == 255);
    a.oow = b.oow = c.oow = 1.0f / 50000.0f; // far: all fog
    p.triangle(a, b, c);
    const Rgba far = from565(p.buffer(kBufferBack)[3 * 64 + 3]);
    CHECK(far.r == 0 && far.b == 255);
}

void testTextures() {
    // An 8x8 RGB 565 texture of two colours, columns 0-3 red, 4-7 blue,
    // through the Glide 2 front end.
    Glide2 g;
    CHECK(!g.sstWinOpen(0, 0xff, 0, kColorArgb, 0, 2, 1)); // GR_RESOLUTION_NONE fails on a Voodoo2
    CHECK(g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1));     // 640x480
    CHECK(g.sstScreenWidth() == 640 && g.sstScreenHeight() == 480);
    std::vector<uint16_t> texels(64);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            texels[size_t(y * 8 + x)] = x < 4 ? 0xf800 : 0x001f;
    Glide2TexInfo info{5, 5, 3, kTexRgb565}; // GR_LOD_8, GR_ASPECT_1x1
    CHECK(g.texCalcMemRequired(5, 5, 3, kTexRgb565) == 128);
    const uint32_t at = g.texMinAddress(0) + 256;
    g.texDownloadMipMap(0, at, kLevelsBoth, info, reinterpret_cast<const uint8_t *>(texels.data()));
    g.texSource(0, at, kLevelsBoth, info);
    g.texCombine(0, kFuncLocal, kFactorZero, kFuncLocal, kFactorZero, false, false);
    g.colorCombine(kFuncScaleOther, kFactorOne, kLocalIterated, kOtherTexture, false);
    g.alphaCombine(kFuncScaleOther, kFactorOne, kLocalIterated, kOtherTexture, false);
    g.ditherMode(kDitherDisable);
    g.bufferClear(0, 0, 0);
    // A quad from (0,0) to (64,64), s and t 0-256 across (w 1).
    auto gv = [](float x, float y, float s, float t) {
        Glide2Vertex v;
        v.x = x;
        v.y = y;
        v.oow = 1;
        v.tmuvtx[0].sow = s;
        v.tmuvtx[0].tow = t;
        v.tmuvtx[0].oow = 1;
        return v;
    };
    g.drawTriangle(gv(0, 0, 0, 0), gv(64, 0, 256, 0), gv(64, 64, 256, 256));
    g.drawTriangle(gv(0, 0, 0, 0), gv(64, 64, 256, 256), gv(0, 64, 0, 256));
    const uint16_t *back = g.pipeline().buffer(kBufferBack);
    CHECK(back[10 * 640 + 5] == 0xf800);
    CHECK(back[10 * 640 + 40] == 0x001f);
    // Point sampling: the texel boundary at s = 128, pixel 32.
    CHECK(back[10 * 640 + 31] == 0xf800 && back[10 * 640 + 32] == 0x001f);
    // Bilinear: the boundary blends.
    g.texFilterMode(0, kFilterBilinear, kFilterBilinear);
    g.drawTriangle(gv(0, 0, 0, 0), gv(64, 0, 256, 0), gv(64, 64, 256, 256));
    const Rgba mid = from565(back[2 * 640 + 32]);
    CHECK(mid.r > 0 && mid.b > 0);
    // Clamp: past the edge stays the edge texel; wrap: comes round.
    g.texFilterMode(0, kFilterPoint, kFilterPoint);
    g.texClampMode(0, kClampClamp, kClampClamp);
    g.drawTriangle(gv(100, 0, 0, 0), gv(164, 0, 512, 0), gv(164, 64, 512, 256));
    CHECK(back[2 * 640 + 160] == 0x001f);
    g.texClampMode(0, kClampWrap, kClampWrap);
    g.drawTriangle(gv(100, 0, 0, 0), gv(164, 0, 512, 0), gv(164, 64, 512, 256));
    CHECK(back[2 * 640 + 135] == 0xf800); // s about 280: wrapped to the red half
    // A palette texture.
    uint32_t palette[256] = {};
    palette[3] = 0x0000ff00;
    g.texDownloadTable(0, kTablePalette, reinterpret_cast<const uint8_t *>(palette));
    std::vector<uint8_t> indices(64, 3);
    Glide2TexInfo p8{5, 5, 3, kTexP8};
    g.texDownloadMipMap(0, 4096, kLevelsBoth, p8, indices.data());
    g.texSource(0, 4096, kLevelsBoth, p8);
    g.drawTriangle(gv(200, 0, 0, 0), gv(264, 0, 256, 0), gv(264, 64, 256, 256));
    CHECK(back[2 * 640 + 250] == 0x07e0);
}

void testLfb() {
    Glide2 g;
    g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    g.ditherMode(kDitherDisable);
    Glide2LfbLock lock;
    CHECK(g.lfbLock(1, kBufferBack, kLfbAny, 0, false, lock));
    CHECK(lock.writeMode == kLfb565 && lock.strideBytes == 2048);
    g.lfbWrite(lock, 10, 20, 0x1234);
    CHECK(g.lfbRead(kBufferBack, 10, 20, false) == 0x1234);
    g.lfbUnlock(1, kBufferBack);
    // Lower-left origin: row 0 is the bottom.
    CHECK(g.lfbLock(1, kBufferBack, kLfb565, 1, false, lock));
    g.lfbWrite(lock, 0, 0, 0xffff);
    CHECK(g.pipeline().buffer(kBufferBack)[479 * 640] == 0xffff);
    // 32-bit ARGB, truncated with dithering off.
    CHECK(g.lfbLock(1, kBufferBack, kLfb8888, 0, false, lock));
    CHECK(lock.bytesPerPixel == 4 && lock.strideBytes == 4096);
    g.lfbWrite(lock, 1, 1, 0xff10ff08);
    CHECK(g.lfbRead(kBufferBack, 1, 1, false) == (2 << 11 | 63 << 5 | 1));
    // The auxiliary buffer through ZA16.
    CHECK(!g.lfbLock(1, kBufferAux, kLfb565, 0, false, lock));
    CHECK(g.lfbLock(1, kBufferAux, kLfbAny, 0, false, lock) && lock.writeMode == kLfbZA16);
    g.lfbWrite(lock, 3, 3, 0xbeef);
    CHECK(g.pipeline().buffer(kBufferAux)[3 * 640 + 3] == 0xbeef);
    // Regions.
    const uint16_t region[4] = {0x0001, 0x0002, 0x0003, 0x0004};
    CHECK(g.lfbWriteRegion(kBufferFront, 100, 100, kLfb565, 2, 2, 4, reinterpret_cast<const uint8_t *>(region)));
    uint16_t back[4] = {};
    CHECK(g.lfbReadRegion(kBufferFront, 100, 100, 2, 2, 4, reinterpret_cast<uint8_t *>(back)));
    CHECK(std::memcmp(back, region, 8) == 0);
    // Through the pixel pipeline: blended over what's there.
    g.alphaBlendFunction(kBlendOne, kBlendOne, kBlendOne, kBlendZero);
    g.colorCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    CHECK(g.lfbLock(1, kBufferBack, kLfb565, 0, true, lock));
    g.pipeline().buffer(kBufferBack)[50 * 640 + 50] = 0x0001;
    g.lfbWrite(lock, 50, 50, 0x0800);
    CHECK(g.lfbRead(kBufferBack, 50, 50, false) == 0x0801);
    // Swapping: the back buffer becomes the front.
    g.alphaBlendFunction(kBlendOne, kBlendZero, kBlendOne, kBlendZero);
    g.lfbWrite(lock, 7, 7, 0x4321);
    g.bufferSwap(1);
    CHECK(g.lfbRead(kBufferFront, 7, 7, false) == 0x4321);
    // Drawing into the front buffer, or writing it through a lock, is
    // noted (a host shows it at once); the back buffer isn't.
    g.takeFrontChanged();
    g.bufferClear(0, 0, 0);
    CHECK(!g.takeFrontChanged());
    g.renderBuffer(kBufferFront);
    g.bufferClear(0, 0, 0);
    CHECK(g.takeFrontChanged() && !g.takeFrontChanged());
    g.renderBuffer(kBufferBack);
    CHECK(g.lfbLock(1, kBufferFront, kLfb565, 0, false, lock));
    g.lfbWrite(lock, 7, 7, 0x4321);
    g.lfbUnlock(1, kBufferFront);
    CHECK(g.takeFrontChanged());
    // Presented with gamma 1: widened 5:6:5.
    reluva::Surface out;
    g.pipeline().present(out);
    CHECK(out.w == 640 && out.row(7)[7 * 4 + 3] == 255);
}

void testBinding() {
    // Every Glide function NFS III's voodoo2a.dll looks up (its names, read
    // from the DLL) is exported under its stdcall name.
    reluva::x86::Cpu cpu(16u << 20);
    cpu.setStack(0x100000);
    reluva::x86::Arena arena(cpu, 0x200000, 0x800000);
    Glide2Dll dll(cpu, arena);
    const char *wanted[] = {
        "_grAADrawLine@8",
        "_grAADrawTriangle@24",
        "_grAlphaBlendFunction@16",
        "_grAlphaCombine@20",
        "_grAlphaTestFunction@4",
        "_grAlphaTestReferenceValue@4",
        "_grBufferClear@12",
        "_grBufferNumPending@0",
        "_grBufferSwap@4",
        "_grChromakeyMode@4",
        "_grChromakeyValue@4",
        "_grClipWindow@16",
        "_grColorCombine@20",
        "_grCullMode@4",
        "_grDepthBiasLevel@4",
        "_grDepthBufferFunction@4",
        "_grDepthBufferMode@4",
        "_grDepthMask@4",
        "_grDitherMode@4",
        "_grDrawLine@8",
        "_grDrawPoint@4",
        "_grDrawTriangle@12",
        "_grFogColorValue@4",
        "_grFogMode@4",
        "_grFogTable@4",
        "_grGammaCorrectionValue@4",
        "_grGlideGetVersion@4",
        "_grGlideInit@0",
        "_grGlideShutdown@0",
        "_grHints@8",
        "_grLfbLock@24",
        "_grLfbReadRegion@28",
        "_grLfbUnlock@8",
        "_grLfbWriteRegion@32",
        "_grRenderBuffer@4",
        "_grSstControl@4",
        "_grSstIdle@0",
        "_grSstIsBusy@0",
        "_grSstQueryBoards@4",
        "_grSstQueryHardware@4",
        "_grSstScreenHeight@0",
        "_grSstScreenWidth@0",
        "_grSstSelect@4",
        "_grSstStatus@0",
        "_grSstVRetraceOn@0",
        "_grSstWinClose@0",
        "_grSstWinOpen@28",
        "_grTexCalcMemRequired@16",
        "_grTexClampMode@12",
        "_grTexCombine@28",
        "_grTexCombineFunction@8",
        "_grTexDownloadMipMap@16",
        "_grTexDownloadTable@12",
        "_grTexFilterMode@12",
        "_grTexLodBiasValue@8",
        "_grTexMaxAddress@4",
        "_grTexMinAddress@4",
        "_grTexMipMapMode@12",
        "_grTexSource@16",
    };
    for (const char *name : wanted)
        CHECK(dll.exports().count(name) == 1);
    CHECK(dll.exports().size() > 100);
    CHECK(!dll.stubs().empty());
    // A call through the interpreter: grSstWinOpen, then grSstScreenWidth.
    uint32_t result = 0;
    CHECK(!cpu.call(dll.exports().at("_grSstWinOpen@28"), {0, 7, 0, 0, 0, 2, 1}, result) && result == 1);
    CHECK(!cpu.call(dll.exports().at("_grSstScreenWidth@0"), {}, result) && result == 640);
    // grGlideGetVersion writes a string Glide 2.4x callers accept.
    const uint32_t buffer = arena.alloc(80);
    CHECK(!cpu.call(dll.exports().at("_grGlideGetVersion@4"), {buffer}, result));
    CHECK(cpu.string(buffer).rfind("Glide Version 2.4", 0) == 0);
    // A lock gives a pointer into guest memory; what's written there reaches
    // the buffer when it's unlocked.
    const uint32_t info = arena.alloc(20);
    cpu.write32(info, 20);
    CHECK(!cpu.call(dll.exports().at("_grLfbLock@24"), {1, kBufferBack, kLfbAny, 0, 0, info}, result) && result == 1);
    const uint32_t ptr = cpu.read32(info + 4);
    const uint32_t stride = cpu.read32(info + 8);
    CHECK(ptr != 0 && stride == 2048);
    cpu.write16(ptr + 3 * stride + 2 * 9, 0xabcd);
    CHECK(!cpu.call(dll.exports().at("_grLfbUnlock@8"), {1, kBufferBack}, result));
    CHECK(dll.glide().lfbRead(kBufferBack, 9, 3, false) == 0xabcd);
    // A stub returns 0 and is counted.
    CHECK(!cpu.call(dll.exports().at("_grSplash@20"), {0, 0, 0, 0, 0}, result) && result == 0);
    CHECK(dll.calls().at("grSplash") == 1);
}

void testGlide3() {
    Glide3 g; // a Voodoo3
    // grGet and grGetString: what NFS III's Voodoo3 patch DLL asks.
    int32_t v[4] = {};
    CHECK(g.get(g3::kNumBoards, 4, v) == 4 && v[0] == 1);
    CHECK(g.get(g3::kFogTableEntries, 4, v) == 4 && v[0] == 64);
    CHECK(g.get(g3::kMemoryUma, 4, v) == 4 && v[0] == 16 << 20); // unified memory
    CHECK(g.get(g3::kMemoryFb, 4, v) == 4 && v[0] == 0);
    CHECK(g.get(g3::kNumTmu, 4, v) == 4 && v[0] == 2);
    CHECK(g.get(g3::kBitsRgba, 4, v) == 0); // too small a buffer
    CHECK(g.get(g3::kBitsRgba, 16, v) == 16 && v[1] == 6);
    CHECK(g.getString(g3::kHardware) == "Voodoo3 (tm)");
    CHECK(g.getString(g3::kVersion) >= "3.10");
    // grQueryResolutions: 640x480 with two buffers and a depth buffer fits.
    Glide3Resolution q{7, int(g3::kQueryAny), int(g3::kQueryAny), 1};
    const auto list = g.queryResolutions(q);
    CHECK(!list.empty() && list.front().resolution == 7 && list.front().numAuxBuffers == 1);
    CHECK(g.queryResolutions({int(g3::kQueryAny), int(g3::kQueryAny), int(g3::kQueryAny), int(g3::kQueryAny)}).size() >
          list.size());
    // Opened, a context comes back.
    const uint32_t context = g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1);
    CHECK(context != 0 && g.selectContext(context));
    CHECK(g.get(g3::kViewport, 16, v) == 16 && v[2] == 640 && v[3] == 480);
    // The patch DLL's layout: window coordinates, xy at 0, z at 8, q at 12,
    // packed ARGB at 16, st0 at 24 (thrash.md).
    g.coordinateSpace(g3::kWindowCoords);
    g.vertexLayout(g3::kParamXy, 0, g3::kParamEnable);
    g.vertexLayout(g3::kParamZ, 8, g3::kParamEnable);
    g.vertexLayout(g3::kParamQ, 12, g3::kParamEnable);
    g.vertexLayout(g3::kParamPargb, 16, g3::kParamEnable);
    g.vertexLayout(g3::kParamSt0, 24, g3::kParamEnable);
    CHECK(g.layoutMin() == 0 && g.layoutMax() == 32);
    struct GameVertex {
        float x;
        float y;
        float z;
        float q;
        uint32_t argb;
        uint32_t pad;
        float s;
        float t;
    };
    static_assert(sizeof(GameVertex) == 32);
    auto gv = [](float x, float y, uint32_t argb) { return GameVertex{x, y, 0, 1, argb, 0, 0, 0}; };
    g.core().colorCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    g.core().ditherMode(kDitherDisable);
    g.core().bufferClear(0, 0, 0);
    const GameVertex a = gv(10, 10, 0xffff0000);
    const GameVertex b = gv(50, 10, 0xffff0000);
    const GameVertex c = gv(10, 50, 0xffff0000);
    g.drawTriangle(reinterpret_cast<const uint8_t *>(&a), reinterpret_cast<const uint8_t *>(&b),
                   reinterpret_cast<const uint8_t *>(&c));
    const uint16_t *back = g.pipeline().buffer(kBufferBack);
    CHECK(back[20 * 640 + 20] == 0xf800);
    CHECK(back[45 * 640 + 45] == 0);
    // A strip of four vertices: two triangles, both drawn whatever the
    // culling, as their winding is kept.
    g.core().cullMode(kCullNegative);
    const GameVertex s0 = gv(100, 100, 0xff00ff00);
    const GameVertex s1 = gv(100, 140, 0xff00ff00);
    const GameVertex s2 = gv(140, 100, 0xff00ff00);
    const GameVertex s3 = gv(140, 140, 0xff00ff00);
    g.drawVertexArray(g3::kTriangleStrip,
                      {reinterpret_cast<const uint8_t *>(&s0), reinterpret_cast<const uint8_t *>(&s1),
                       reinterpret_cast<const uint8_t *>(&s2), reinterpret_cast<const uint8_t *>(&s3)});
    const bool firstDrawn = back[110 * 640 + 105] == 0x07e0;
    const bool secondDrawn = back[135 * 640 + 135] == 0x07e0;
    CHECK(firstDrawn == secondDrawn);
    g.core().cullMode(kCullDisable);
    // Log2 LODs: a 256x256 565 texture takes 128 KB and a 1x1 level 8 bytes.
    CHECK(g.texCalcMemRequired(8, 8, 0, kTexRgb565) == 131072);
    CHECK(g.texCalcMemRequired(0, 0, 0, kTexRgb565) == 8);
    // One palette for every TMU.
    uint32_t palette[256] = {};
    palette[1] = 0x123456;
    g.texDownloadTablePartial(kTablePalette, reinterpret_cast<const uint8_t *>(palette), 0, 255);
    CHECK(g.pipeline().palette(0)[1] == 0x123456 && g.pipeline().palette(1)[1] == 0x123456);
    // Fog modes renumbered: Glide 3's table on q is the core's table fog.
    g.fogMode(g3::kFogWithTableOnQ);
    CHECK(g.pipeline().state().fogMode == kFogWithTable);
    g.fogMode(g3::kFogWithIteratedAlphaExt);
    CHECK(g.pipeline().state().fogMode == kFogWithIteratedAlpha);
    // Mipmap dithering only once allowed.
    g.texMipMapMode(0, kMipmapNearestDither, false);
    CHECK(g.pipeline().state().tmus[0].mipmapMode == kMipmapNearest);
    g.enable(g3::kAllowMipmapDither);
    CHECK(g.pipeline().state().tmus[0].mipmapMode == kMipmapNearestDither);
    // Gamma per channel.
    g.gammaCorrectionRgb(1.0f, 1.3f, 2.0f);
    CHECK(g.pipeline().state().gamma[2] == 2.0f);
    // Clip coordinates through the viewport.
    g.coordinateSpace(g3::kClipCoords);
    g.vertexLayout(g3::kParamW, 12, g3::kParamEnable);
    g.vertexLayout(g3::kParamQ, 0, g3::kParamDisable);
    const GameVertex cv{0.5f, -0.5f, 0, 2, 0xffffffff, 0, 0, 0}; // w 2: (0.25, -0.25)
    const Vertex out = g.readVertex(reinterpret_cast<const uint8_t *>(&cv));
    CHECK(std::fabs(out.x - 400) < 0.01f && std::fabs(out.y - 180) < 0.01f && std::fabs(out.oow - 0.5f) < 1e-6f);
}

void testGlide3Binding() {
    reluva::x86::Cpu cpu(16u << 20);
    cpu.setStack(0x100000);
    reluva::x86::Arena arena(cpu, 0x200000, 0x800000);
    Glide3Dll dll(cpu, arena);
    CHECK(dll.dllName() == "glide3x.dll");
    // Every Glide 3 function the CD's Voodoo3 patch DLL looks up (its names,
    // read from the DLL).
    const char *wanted[] = {
        "_grAADrawTriangle@24",
        "_grAlphaBlendFunction@16",
        "_grAlphaCombine@20",
        "_grAlphaTestFunction@4",
        "_grAlphaTestReferenceValue@4",
        "_grBufferClear@12",
        "_grBufferSwap@4",
        "_grChromakeyMode@4",
        "_grChromakeyValue@4",
        "_grClipWindow@16",
        "_grColorCombine@20",
        "_grCoordinateSpace@4",
        "_grCullMode@4",
        "_grDepthBiasLevel@4",
        "_grDepthBufferFunction@4",
        "_grDepthBufferMode@4",
        "_grDepthMask@4",
        "_grDisable@4",
        "_grDitherMode@4",
        "_grDrawLine@8",
        "_grDrawPoint@4",
        "_grDrawTriangle@12",
        "_grEnable@4",
        "_grErrorSetCallback@4",
        "_grFinish@0",
        "_grFogColorValue@4",
        "_grFogMode@4",
        "_grFogTable@4",
        "_grGet@12",
        "_grGetString@4",
        "_grGlideInit@0",
        "_grGlideShutdown@0",
        "_grLfbLock@24",
        "_grLfbReadRegion@28",
        "_grLfbUnlock@8",
        "_grLfbWriteRegion@36",
        "_grQueryResolutions@8",
        "_grRenderBuffer@4",
        "_grSstSelect@4",
        "_grSstWinClose@4",
        "_grSstWinOpen@28",
        "_grTexCalcMemRequired@16",
        "_grTexClampMode@12",
        "_grTexCombine@28",
        "_grTexDownloadMipMap@16",
        "_grTexDownloadMipMapLevelPartial@40",
        "_grTexDownloadTable@8",
        "_grTexFilterMode@12",
        "_grTexLodBiasValue@8",
        "_grTexMaxAddress@4",
        "_grTexMinAddress@4",
        "_grTexMipMapMode@12",
        "_grTexSource@16",
        "_grVertexLayout@12",
        "_guGammaCorrectionRGB@12",
    };
    for (const char *name : wanted)
        CHECK(dll.exports().count(name) == 1);
    CHECK(dll.exports().count("_grDrawVertexArray@12") == 1 &&
          dll.exports().count("_grDrawVertexArrayContiguous@16") == 1);
    uint32_t result = 0;
    // grGet through the interpreter.
    const uint32_t buf = arena.alloc(16);
    CHECK(!cpu.call(dll.exports().at("_grGet@12"), {g3::kFogTableEntries, 4, buf}, result) && result == 4);
    CHECK(cpu.read32(buf) == 64);
    // grGetString gives a string in guest memory.
    CHECK(!cpu.call(dll.exports().at("_grGetString@4"), {g3::kHardware}, result) && result != 0);
    CHECK(cpu.string(result) == "Voodoo3 (tm)");
    // Open, a layout, a triangle from guest vertices.
    CHECK(!cpu.call(dll.exports().at("_grSstWinOpen@28"), {0, 7, 0, 0, 0, 2, 1}, result) && result != 0);
    CHECK(!cpu.call(dll.exports().at("_grVertexLayout@12"), {g3::kParamXy, 0, 1}, result));
    CHECK(!cpu.call(dll.exports().at("_grVertexLayout@12"), {g3::kParamPargb, 8, 1}, result));
    CHECK(!cpu.call(dll.exports().at("_grColorCombine@20"),
                    {kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, 0}, result));
    CHECK(!cpu.call(dll.exports().at("_grDitherMode@4"), {kDitherDisable}, result));
    const uint32_t verts = arena.alloc(36);
    const float xy[6] = {10, 10, 50, 10, 10, 50};
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t fx;
        uint32_t fy;
        std::memcpy(&fx, &xy[2 * i], 4);
        std::memcpy(&fy, &xy[2 * i + 1], 4);
        cpu.write32(verts + 12 * i, fx);
        cpu.write32(verts + 12 * i + 4, fy);
        cpu.write32(verts + 12 * i + 8, 0xff0000ff);
    }
    CHECK(!cpu.call(dll.exports().at("_grDrawTriangle@12"), {verts, verts + 12, verts + 24}, result));
    CHECK(dll.glide().pipeline().buffer(kBufferBack)[20 * 640 + 20] == 0x001f);
    // grLfbWriteRegion's Glide 3 form: the pixel pipeline flag before the
    // stride.
    const uint32_t px = arena.alloc(8);
    cpu.write16(px, 0x07e0);
    cpu.write16(px + 2, 0x07e0);
    CHECK(!cpu.call(dll.exports().at("_grLfbWriteRegion@36"), {kBufferBack, 5, 6, kLfb565, 2, 1, 0, 4, px}, result) &&
          result == 1);
    CHECK(dll.glide().pipeline().buffer(kBufferBack)[6 * 640 + 6] == 0x07e0);
    CHECK(dll.calls().at("grDrawTriangle") == 1);
    CHECK(dll.modes().at("grDitherMode").at(kDitherDisable) == 1);
}

} // namespace

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.glide", {
                                    {"dither", [](const std::filesystem::path &) { testDither(); }},
                                    {"formats", [](const std::filesystem::path &) { testFormats(); }},
                                    {"layout", [](const std::filesystem::path &) { testLayout(); }},
                                    {"combine", [](const std::filesystem::path &) { testCombine(); }},
                                    {"fog and depth", [](const std::filesystem::path &) { testFogAndDepth(); }},
                                    {"rasterizer", [](const std::filesystem::path &) { testRasterizer(); }},
                                    {"depth and chroma", [](const std::filesystem::path &) { testDepthAndChroma(); }},
                                    {"table fog", [](const std::filesystem::path &) { testTableFog(); }},
                                    {"textures", [](const std::filesystem::path &) { testTextures(); }},
                                    {"lfb", [](const std::filesystem::path &) { testLfb(); }},
                                    {"binding", [](const std::filesystem::path &) { testBinding(); }},
                                    {"glide3", [](const std::filesystem::path &) { testGlide3(); }},
                                    {"glide3 binding", [](const std::filesystem::path &) { testGlide3Binding(); }},
                                });
