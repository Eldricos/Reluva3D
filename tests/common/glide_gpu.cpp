// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Glide stand-in's GPU backend (graphics/glide/gpu_renderer.h) against the
// software reference: the same scenes drawn by both through the Glide 2
// front end, the buffers compared. Gouraud shading through both dithers,
// textures (point, bilinear, mipmapped, palette, NCC, two TMUs), blending,
// chroma-key, alpha test, z and w buffering, table fog, clears in a clip
// window, lines, points, a lower-left origin, and linear frame buffer
// writes between draws; Glide 3's extensions where the board has them
// (mirrored textures, chroma range, the 6666 palette on a Voodoo3).
// Without a GPU (none in the build, or no device) the suite says so and
// passes.

#include "backends/graphics/sdl3/sdl_compute.h"
#include "graphics/glide/glide2.h"
#include "graphics/glide/gpu_renderer.h"
#include "tests/support/test.h"

#include <filesystem>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace reluva::glide;



namespace {

Glide2Vertex vtx(float x, float y, float r, float g, float b, float a = 255, float ooz = 0, float oow = 1, float s = 0,
                 float t = 0) {
    Glide2Vertex v;
    v.x = x;
    v.y = y;
    v.r = r;
    v.g = g;
    v.b = b;
    v.a = a;
    v.ooz = ooz;
    v.oow = oow;
    for (auto& m : v.tmuvtx) {
        m.sow = s * oow;
        m.tow = t * oow;
        m.oow = oow;
    }
    return v;
}

// Textures: a 64x64 565 checker with mipmaps, a palette one, an NCC one.
void loadTextures(Glide2& g) {
    std::vector<uint16_t> mip;
    for (int size = 64; size >= 1; size /= 2)
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                mip.push_back(uint16_t(((x / 4 + y / 4) & 1) ? 0xf800 | (size * 3) : 0x07e0 | (x * 31 / size)));
    Glide2TexInfo info{6, 2, 3, kTexRgb565}; // GR_LOD_1 to GR_LOD_64
    g.texDownloadMipMap(0, 0, kLevelsBoth, info, reinterpret_cast<const uint8_t*>(mip.data()));
    g.texDownloadMipMap(1, 0, kLevelsBoth, info, reinterpret_cast<const uint8_t*>(mip.data()));
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i)
        palette[i] = uint32_t(i) << 16 | uint32_t(255 - i) << 8 | uint32_t((i * 7) & 255);
    g.texDownloadTable(0, kTablePalette, reinterpret_cast<const uint8_t*>(palette));
    std::vector<uint8_t> idx(32 * 32);
    for (int i = 0; i < 32 * 32; ++i)
        idx[size_t(i)] = uint8_t(i * 13);
    Glide2TexInfo p8{3, 3, 3, kTexP8};
    g.texDownloadMipMap(0, 65536, kLevelsBoth, p8, idx.data());
    uint8_t ncc[64] = {};
    for (int i = 0; i < 16; ++i)
        ncc[i] = uint8_t(i * 16);
    const int16_t iq[24] = {10, -5, 0, 20, 0, -10, -20, 5, 3, 0, 0, 0, 0, 5, 30, -10, 10, 0, 7, 7, 7, -30, 0, 15};
    std::memcpy(ncc + 16, iq, 24);
    std::memcpy(ncc + 40, iq, 24);
    g.texDownloadTable(0, kTableNcc0, ncc);
    Glide2TexInfo yiq{3, 3, 3, kTexYiq422};
    g.texDownloadMipMap(0, 131072, kLevelsBoth, yiq, idx.data());
}

// A scene drawn through Glide 2, ending with the frame swapped to the front.
void scene(Glide2& g) {
    CHECK(g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1));
    loadTextures(g);
    g.bufferClear(0xff203040, 0, 0xffff);
    // Gouraud, both dithers and none.
    const int dithers[3] = {kDither4x4, kDither2x2, kDitherDisable};
    for (int i = 0; i < 3; ++i) {
        g.ditherMode(dithers[i]);
        const float x = 10.0f + 70.0f * float(i);
        g.drawTriangle(vtx(x, 10, 255, 0, 0), vtx(x + 60.3f, 12.7f, 0, 255, 0), vtx(x + 20.5f, 70.2f, 0, 0, 255));
    }
    g.ditherMode(kDither4x4);
    // Point, bilinear and mipmapped textures, perspective (w 1 to 4).
    Glide2TexInfo info{6, 2, 3, kTexRgb565};
    g.texSource(0, 0, kLevelsBoth, info);
    g.colorCombine(kFuncScaleOther, kFactorLocal, kLocalIterated, kOtherTexture, false);
    g.alphaCombine(kFuncScaleOther, kFactorLocal, kLocalIterated, kOtherTexture, false);
    auto quad = [&](float x0, float y0, float x1, float y1, float tiles) {
        g.drawTriangle(vtx(x0, y0, 255, 255, 255, 255, 0, 1, 0, 0), vtx(x1, y0, 200, 255, 255, 255, 0, 0.25f, 256 * tiles, 0),
                       vtx(x1, y1, 255, 200, 255, 255, 0, 0.25f, 256 * tiles, 256 * tiles));
        g.drawTriangle(vtx(x0, y0, 255, 255, 255, 255, 0, 1, 0, 0), vtx(x1, y1, 255, 200, 255, 255, 0, 0.25f, 256 * tiles, 256 * tiles),
                       vtx(x0, y1, 255, 255, 200, 255, 0, 1, 0, 256 * tiles));
    };
    quad(230, 10, 330, 90, 1);
    g.texFilterMode(0, kFilterBilinear, kFilterBilinear);
    quad(340, 10, 440, 90, 2);
    g.texMipMapMode(0, kMipmapNearest, false);
    quad(450, 10, 630, 120, 8);
    g.texMipMapMode(0, kMipmapNearestDither, false);
    g.texLodBiasValue(0, 0.5f);
    quad(10, 100, 200, 200, 12);
    g.texMipMapMode(0, kMipmapDisable, false);
    g.texLodBiasValue(0, 0);
    // Palette and NCC textures, clamped.
    g.texClampMode(0, kClampClamp, kClampClamp);
    g.texFilterMode(0, kFilterPoint, kFilterPoint);
    Glide2TexInfo p8{3, 3, 3, kTexP8};
    g.texSource(0, 65536, kLevelsBoth, p8);
    quad(210, 130, 300, 220, 1.5f);
    Glide2TexInfo yiq{3, 3, 3, kTexYiq422};
    g.texSource(0, 131072, kLevelsBoth, yiq);
    g.texFilterMode(0, kFilterBilinear, kFilterBilinear);
    quad(310, 130, 400, 220, 1);
    // Mirrored (the Voodoo2's TEXMIRROR), point and bilinear.
    g.texSource(0, 0, kLevelsBoth, info);
    g.texClampMode(0, kClampMirror, kClampMirror);
    g.texFilterMode(0, kFilterPoint, kFilterPoint);
    quad(510, 130, 560, 220, 2.5f);
    g.texFilterMode(0, kFilterBilinear, kFilterBilinear);
    quad(570, 130, 635, 220, 3.5f);
    g.texClampMode(0, kClampWrap, kClampWrap);
    // Two TMUs: TMU1's texel times TMU0's.
    g.texSource(0, 0, kLevelsBoth, info);
    g.texSource(1, 0, kLevelsBoth, info);
    g.texCombine(1, kFuncLocal, kFactorZero, kFuncLocal, kFactorZero, false, false);
    g.texCombine(0, kFuncScaleOther, kFactorLocal, kFuncScaleOther, kFactorLocalAlpha, false, false);
    quad(410, 130, 500, 220, 3);
    g.texCombine(0, kFuncLocal, kFactorZero, kFuncLocal, kFactorZero, false, false);
    // Blending, chroma-key, alpha test.
    g.alphaBlendFunction(kBlendSrcAlpha, kBlendOneMinusSrcAlpha, kBlendOne, kBlendZero);
    g.colorCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    g.alphaCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherIterated, false);
    g.drawTriangle(vtx(20, 20, 255, 255, 0, 40), vtx(600, 30, 0, 255, 255, 200), vtx(300, 230, 255, 0, 255, 120));
    g.alphaBlendFunction(kBlendOne, kBlendOne, kBlendOne, kBlendZero);
    g.drawTriangle(vtx(500, 200, 30, 20, 10), vtx(630, 220, 30, 20, 10), vtx(560, 300, 60, 50, 40));
    g.alphaBlendFunction(kBlendOne, kBlendZero, kBlendOne, kBlendZero);
    g.chromakeyMode(1);
    g.chromakeyValue(0xff0000ff);
    g.drawTriangle(vtx(20, 240, 0, 0, 255), vtx(200, 240, 0, 0, 255), vtx(100, 300, 255, 0, 0));
    // Chroma range: a gradient with a band of it dropped.
    g.chromaRangeMode(true);
    g.chromaRange(0xff200000, 0xffa0ffff, 0);
    g.drawTriangle(vtx(20, 310, 0, 0, 0), vtx(200, 310, 255, 0, 128), vtx(110, 330, 128, 255, 0));
    g.chromaRangeMode(false);
    g.chromakeyMode(0);
    g.alphaTestFunction(kCmpGreater);
    g.alphaTestReferenceValue(100);
    g.drawTriangle(vtx(220, 240, 0, 255, 0, 0), vtx(400, 240, 0, 255, 0, 255), vtx(300, 320, 0, 255, 0, 128));
    g.alphaTestFunction(kCmpAlways);
    // Z buffer, then w buffer with table fog.
    g.depthBufferMode(kDepthZBuffer);
    g.depthBufferFunction(kCmpGreater);
    g.depthMask(true);
    g.drawTriangle(vtx(420, 240, 255, 0, 0, 255, 1000), vtx(620, 250, 255, 0, 0, 255, 60000), vtx(500, 340, 255, 0, 0, 255, 30000));
    g.drawTriangle(vtx(420, 300, 0, 255, 0, 255, 50000), vtx(620, 260, 0, 255, 0, 255, 2000), vtx(520, 240, 0, 255, 0, 255, 30000));
    g.depthBufferMode(kDepthWBuffer);
    g.depthBufferFunction(kCmpLess);
    g.colorMask(false, false); // the depth buffer only
    g.bufferClear(0xff000000, 0, 0xffff);
    g.colorMask(true, true);
    uint8_t fog[64];
    for (int i = 0; i < 64; ++i)
        fog[i] = uint8_t(i * 4);
    g.fogTable(fog);
    g.fogColorValue(0xff8090a0);
    g.fogMode(kFogWithTable);
    g.drawTriangle(vtx(10, 330, 255, 255, 255, 255, 0, 1.0f / 2), vtx(400, 330, 255, 255, 255, 255, 0, 1.0f / 3000),
                   vtx(200, 470, 255, 255, 255, 255, 0, 1.0f / 40));
    g.fogMode(kFogWithIteratedZ);
    g.drawTriangle(vtx(410, 350, 255, 0, 0, 255, 0, 1.0f / 8), vtx(630, 350, 255, 0, 0, 255, 65000, 1.0f / 8),
                   vtx(500, 470, 255, 0, 0, 255, 30000, 1.0f / 8));
    g.fogMode(kFogDisable);
    g.depthBufferMode(kDepthDisable);
    // A clear in a clip window, lines and points.
    g.clipWindow(100, 400, 300, 450);
    g.bufferClear(0xff00ff00, 0, 0);
    g.clipWindow(0, 0, 640, 480);
    g.drawLine(vtx(5, 5, 255, 255, 255), vtx(630, 470, 0, 0, 0));
    g.drawLine(vtx(600, 5, 255, 255, 0), vtx(590, 470, 0, 255, 0));
    for (int i = 0; i < 50; ++i)
        g.drawPoint(vtx(float(i * 12) + 3.5f, 475.2f, 255, float(i * 5), 0));
    // A lower-left origin.
    g.sstOrigin(1);
    g.drawTriangle(vtx(300, 10, 0, 255, 255), vtx(380, 10, 0, 255, 255), vtx(340, 60, 255, 255, 0));
    g.sstOrigin(0);
    // Linear frame buffer writes between draws, then drawing over them.
    g.pipeline().sync();
    for (int x = 0; x < 100; ++x)
        g.pipeline().lfbWrite(kBufferBack, 250 + x, 250, kLfb565, 0xffff, false);
    g.alphaBlendFunction(kBlendSrcAlpha, kBlendOneMinusSrcAlpha, kBlendOne, kBlendZero);
    g.drawTriangle(vtx(240, 245, 0, 0, 0, 128), vtx(360, 245, 0, 0, 0, 128), vtx(300, 265, 0, 0, 0, 128));
    // Anti-aliased primitives: the coverage pixels, blended by alpha.
    g.aaDrawTriangle(vtx(30, 380, 255, 255, 255), vtx(90, 395.3f, 255, 128, 0), vtx(45.6f, 440, 0, 128, 255), true,
                     true, true);
    g.aaDrawLine(vtx(100.2f, 460, 255, 255, 0), vtx(200.7f, 430.4f, 0, 255, 255));
    g.aaDrawPoint(vtx(220.3f, 440.8f, 255, 255, 255));
    g.alphaBlendFunction(kBlendOne, kBlendZero, kBlendOne, kBlendZero);
    g.bufferSwap(0);
    // And a second frame into the other buffer.
    g.bufferClear(0xff102030, 0, 0);
    g.drawTriangle(vtx(0, 0, 255, 0, 0), vtx(639, 0, 0, 255, 0), vtx(0, 479, 0, 0, 255));
    g.bufferSwap(0);
}

// On a Voodoo3: a P_8 texture through the PALETTE6666 palette (its alpha
// blended), and the same after a plain palette.
void scene6666(Glide2& g) {
    CHECK(g.sstWinOpen(0, 7, 0, kColorArgb, 0, 2, 1));
    g.bufferClear(0xff406080, 0, 0);
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i)
        palette[i] = uint32_t(i) << 24 | uint32_t(255 - i) << 16 | uint32_t(i * 3 & 255) << 8 | uint32_t(i * 5 & 255);
    std::vector<uint8_t> idx(32 * 32);
    for (int i = 0; i < 32 * 32; ++i)
        idx[size_t(i)] = uint8_t(i * 7);
    const uint32_t at = g.texMinAddress(0);
    Glide2TexInfo p8{3, 3, 3, kTexP8};
    g.texDownloadMipMap(0, at, kLevelsBoth, p8, idx.data());
    g.texSource(0, at, kLevelsBoth, p8);
    g.texDownloadTable(0, kTablePalette6666, reinterpret_cast<const uint8_t*>(palette));
    g.colorCombine(kFuncScaleOther, kFactorOne, kLocalIterated, kOtherTexture, false);
    g.alphaCombine(kFuncScaleOther, kFactorOne, kLocalIterated, kOtherTexture, false);
    g.alphaBlendFunction(kBlendSrcAlpha, kBlendOneMinusSrcAlpha, kBlendOne, kBlendZero);
    for (int filter = 0; filter < 2; ++filter) {
        g.texFilterMode(0, filter, filter);
        const float x = 20.0f + 300.0f * float(filter);
        g.drawTriangle(vtx(x, 20, 255, 255, 255, 255, 0, 1, 0, 0), vtx(x + 280, 30, 255, 255, 255, 255, 0, 1, 512, 0),
                       vtx(x + 100, 300, 255, 255, 255, 255, 0, 1, 64, 512));
    }
    g.texDownloadTable(0, kTablePalette, reinterpret_cast<const uint8_t*>(palette));
    g.drawTriangle(vtx(50, 320, 255, 255, 255, 255, 0, 1, 0, 0), vtx(600, 330, 255, 255, 255, 255, 0, 1, 256, 0),
                   vtx(300, 470, 255, 255, 255, 255, 0, 1, 0, 256));
    g.bufferSwap(0);
}

struct Frames {
    std::vector<uint16_t> front, back, aux;
};

Frames draw(std::unique_ptr<Renderer> renderer, bool voodoo3 = false) {
    Glide2 g(voodoo3 ? voodoo3Board() : voodoo2Board());
    if (renderer)
        g.pipeline().setRenderer(std::move(renderer));
    if (voodoo3)
        scene6666(g);
    else
        scene(g);
    g.pipeline().sync();
    const size_t n = 640 * 480;
    Frames f;
    f.front.assign(g.pipeline().buffer(kBufferFront), g.pipeline().buffer(kBufferFront) + n);
    f.back.assign(g.pipeline().buffer(kBufferBack), g.pipeline().buffer(kBufferBack) + n);
    f.aux.assign(g.pipeline().buffer(kBufferAux), g.pipeline().buffer(kBufferAux) + n);
    return f;
}

int differing(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b, int& maxChannel) {
    int n = 0;
    maxChannel = 0;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) {
            ++n;
            const Rgba x = from565(a[i]), y = from565(b[i]);
            maxChannel = std::max({maxChannel, std::abs(x.r - y.r), std::abs(x.g - y.g), std::abs(x.b - y.b)});
        }
    return n;
}

} // namespace

static void runAll() {
    std::string error;
    auto gpu = reluva::makeSdlCompute(error);
    std::unique_ptr<Renderer> renderer = gpu ? makeGpuRenderer(std::move(gpu), error) : nullptr;
    if (!renderer) {
        std::printf("glide_gpu: no GPU here (%s); skipped\n", error.c_str());
        return;
    }
    const Frames sw = draw(nullptr), hw = draw(std::move(renderer));
    int maxFront = 0, maxBack = 0, maxAux = 0;
    const int front = differing(sw.front, hw.front, maxFront), back = differing(sw.back, hw.back, maxBack);
    const int aux = differing(sw.aux, hw.aux, maxAux);
    std::printf("glide_gpu: pixels differing from the software reference: frame 1 %d (at most %d a channel), frame "
                "2 %d (%d), aux %d\n",
                back, maxBack, front, maxFront, aux);
    // The same set-up and integer pixel arithmetic: only the per-pixel
    // floating-point evaluation of the planes (the GPU's at each pixel, the
    // software's stepped along the row) can differ, at a few pixels.
    CHECK(back < 640 * 480 / 200);
    CHECK(front < 640 * 480 / 1000);
    CHECK(aux < 640 * 480 / 200);
    // The comparing renderer finds the same differences at each swap.
    std::vector<FrameComparison> seen;
    auto gpu2 = reluva::makeSdlCompute(error);
    draw(makeComparingRenderer(makeGpuRenderer(std::move(gpu2), error),
                               [&](const FrameComparison& f) { seen.push_back(f); }));
    CHECK(seen.size() == 2);
    if (seen.size() == 2) {
        std::printf("glide_gpu: compared at each swap: %llu and %llu pixels differing, mean %.4f and %.4f of 255\n",
                    (unsigned long long)seen[0].pixelsDiffering, (unsigned long long)seen[1].pixelsDiffering,
                    seen[0].meanDifference, seen[1].meanDifference);
        CHECK(int(seen[0].pixelsDiffering) == back && int(seen[1].pixelsDiffering) == front);
        CHECK(seen[0].meanDifference < 0.05 && seen[1].meanDifference < 0.01);
    }
    // The Voodoo3's 6666 palette, on both.
    auto gpu3 = reluva::makeSdlCompute(error);
    const Frames sw3 = draw(nullptr, true), hw3 = draw(makeGpuRenderer(std::move(gpu3), error), true);
    int max3 = 0;
    const int diff3 = differing(sw3.front, hw3.front, max3);
    std::printf("glide_gpu: the 6666 palette's frame: %d pixels differing (at most %d a channel)\n", diff3, max3);
    CHECK(diff3 < 640 * 480 / 1000);
    int drawn3 = 0;
    for (uint16_t v : sw3.front)
        drawn3 += v != sw3.front[639];
    CHECK(drawn3 > 30000);
    // Something was drawn: most of the frame isn't the clear colour.
    int drawn = 0;
    for (uint16_t v : hw.back)
        drawn += v != hw.back[639];
    std::printf("glide_gpu: %d pixels drawn over the clear\n", drawn);
    CHECK(drawn > 50000);
    // To look at them: RELUVA_GLIDE_GPU_DUMP=folder writes both frames as PPM.
    if (const char* dir = std::getenv("RELUVA_GLIDE_GPU_DUMP")) {
        auto ppm = [&](const std::vector<uint16_t>& px, const char* name) {
            const std::string path = std::string(dir) + "/" + name;
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fprintf(f, "P6 640 480 255\n");
                for (uint16_t v : px) {
                    const Rgba c = from565(v);
                    const uint8_t rgb[3] = {uint8_t(c.r), uint8_t(c.g), uint8_t(c.b)};
                    std::fwrite(rgb, 1, 3, f);
                }
                std::fclose(f);
            }
        };
        ppm(sw.back, "software.ppm");
        ppm(hw.back, "gpu.ppm");
    }
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.glide_gpu", {
    {"all", [](const std::filesystem::path&) { runAll(); }},
});
