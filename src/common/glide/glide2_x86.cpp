// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/glide/glide2_x86.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::glide {

using x86::Cpu;

Glide2Dll::Glide2Dll(Cpu& cpu, x86::Arena& memory, Board board)
    : GlideDll(cpu, memory, "glide2x.dll"), glide_(board) {
    setCore(glide_);
    glide_.logName = "glide2x";
    addAll();
}

Glide2Vertex Glide2Dll::vertex(uint32_t address) const {
    Glide2Vertex v;
    if (const uint8_t* p = guest(address, sizeof v))
        std::memcpy(&v, p, sizeof v);
    return v;
}

Glide2TexInfo Glide2Dll::texInfo(uint32_t address) const {
    // GrTexInfo: smallLod, largeLod, aspectRatio, format, then data.
    Glide2TexInfo t;
    if (guest(address, 20)) {
        t.smallLod = int(cpu_.read32(address));
        t.largeLod = int(cpu_.read32(address + 4));
        t.aspectRatio = int(cpu_.read32(address + 8));
        t.format = int(cpu_.read32(address + 12));
    }
    return t;
}

void Glide2Dll::addAll() {
    Glide2& g = glide_;
    auto v = [this](Cpu& c, int i) { return vertex(c.arg(i)); };
    auto b = [](Cpu& c, int i) { return c.arg(i) != 0; };
    auto n = [](Cpu& c, int i) { return int(c.arg(i)); };

    // --- Start-up, the board, shut-down -----------------------------------------
    add("grGlideInit", 0, [&g](Cpu&) { g.glideInit(); return 0u; });
    add("grGlideShutdown", 0, [&g](Cpu&) { g.glideShutdown(); return 0u; });
    add("grGlideGetVersion", 4, [&g](Cpu& c) {
        c.writeString(c.arg(0), g.glideGetVersion(), 80);
        return 0u;
    });
    add("grSstQueryBoards", 4, [this, &g](Cpu& c) {
        Glide2HwConfig hw;
        g.sstQueryBoards(hw);
        if (guest(c.arg(0), 4))
            c.write32(c.arg(0), uint32_t(hw.numSst));
        return 1u;
    });
    add("grSstQueryHardware", 4, [this, &g](Cpu& c) {
        // GrHwConfiguration: num_sst, then per board its type and the
        // GrVoodooConfig_t union (fbRam, fbiRev, nTexelfx, sliDetect,
        // tmuConfig[2] of tmuRev, tmuRam): 36 bytes a board.
        Glide2HwConfig hw;
        g.sstQueryHardware(hw);
        const uint32_t p = c.arg(0);
        if (!guest(p, 40))
            return 0u;
        c.write32(p, uint32_t(hw.numSst));
        c.write32(p + 4, uint32_t(hw.type));
        c.write32(p + 8, uint32_t(hw.fbRamMb));
        c.write32(p + 12, uint32_t(hw.fbiRev));
        c.write32(p + 16, uint32_t(hw.nTexelfx));
        c.write32(p + 20, hw.sliDetect ? 1u : 0u);
        for (uint32_t i = 0; i < 2; ++i) {
            c.write32(p + 24 + 8 * i, uint32_t(hw.tmuRev[i]));
            c.write32(p + 28 + 8 * i, uint32_t(hw.tmuRamMb[i]));
        }
        return 1u;
    });
    add("grSstSelect", 4, [&g, n](Cpu& c) { g.sstSelect(n(c, 0)); return 0u; });
    add("grSstWinOpen", 28, [&g, n](Cpu& c) {
        return g.sstWinOpen(c.arg(0), n(c, 1), n(c, 2), n(c, 3), n(c, 4), n(c, 5), n(c, 6)) ? 1u : 0u;
    });
    add("grSstWinClose", 0, [&g](Cpu&) { g.sstWinClose(); return 0u; });
    add("grSstControl", 4, [&g](Cpu& c) { return g.sstControl(c.arg(0)) ? 1u : 0u; });
    add("grSstControlMode", 4, [&g](Cpu& c) { return g.sstControl(c.arg(0)) ? 1u : 0u; });
    add("grSstScreenWidth", 0, [&g](Cpu&) { return g.sstScreenWidth(); });
    add("grSstScreenHeight", 0, [&g](Cpu&) { return g.sstScreenHeight(); });
    add("grSstStatus", 0, [&g](Cpu&) { return g.sstStatus(); });
    add("grSstIdle", 0, [&g](Cpu&) { g.pipeline().sync(); return 0u; });
    add("grSstIsBusy", 0, [](Cpu&) { return 0u; });
    add("grSstVRetraceOn", 0, [&g](Cpu&) { return g.sstVRetraceOn() ? 1u : 0u; });
    add("grSstVideoLine", 0, [&g](Cpu&) { return g.sstVideoLine(); });
    add("grSstOrigin", 4, [&g, n](Cpu& c) { g.sstOrigin(n(c, 0)); return 0u; });
    add("grSstPerfStats", 4, [this, &g](Cpu& c) {
        uint32_t s[5];
        g.sstPerfStats(s);
        if (guest(c.arg(0), 20))
            for (uint32_t i = 0; i < 5; ++i)
                c.write32(c.arg(0) + 4 * i, s[i]);
        return 0u;
    });
    add("grSstResetPerfStats", 0, [&g](Cpu&) { g.sstResetPerfStats(); return 0u; });
    add("grTriStats", 8, [this, &g](Cpu& c) {
        uint32_t processed = 0, drawn = 0;
        g.triStats(processed, drawn);
        if (guest(c.arg(0), 4))
            c.write32(c.arg(0), processed);
        if (guest(c.arg(1), 4))
            c.write32(c.arg(1), drawn);
        return 0u;
    });
    add("grResetTriStats", 0, [&g](Cpu&) { g.resetTriStats(); return 0u; });
    add("grErrorSetCallback", 4, [](Cpu&) { return 0u; }); // the release build never calls it (Reference Manual)
    add("grHints", 8, [&g, n](Cpu& c) { g.hints(n(c, 0), c.arg(1)); return 0u; });
    add("grGlideGetState", 4, [this, &g](Cpu& c) {
        if (guest(c.arg(0), 8)) {
            c.write32(c.arg(0), g.glideGetState());
            c.write32(c.arg(0) + 4, 0x52474c53);
        }
        return 0u;
    });
    add("grGlideSetState", 4, [this, &g](Cpu& c) {
        if (guest(c.arg(0), 8))
            g.glideSetState(c.read32(c.arg(0)));
        return 0u;
    });
    add("grGlideShamelessPlug", 4, [](Cpu&) { return 0u; });
    add("grCheckForRoom", 4, [](Cpu&) { return 0u; });

    // --- Drawing -------------------------------------------------------------------
    add("grDrawTriangle", 12, [&g, v](Cpu& c) { g.drawTriangle(v(c, 0), v(c, 1), v(c, 2)); return 0u; });
    add("grDrawLine", 8, [&g, v](Cpu& c) { g.drawLine(v(c, 0), v(c, 1)); return 0u; });
    add("grDrawPoint", 4, [&g, v](Cpu& c) { g.drawPoint(v(c, 0)); return 0u; });
    // Anti-aliased primitives drawn without the coverage alpha.
    add("grAADrawTriangle", 24, [&g, v](Cpu& c) {
        g.notImplemented("grAADrawTriangle", "drawn without edge anti-aliasing");
        g.drawTriangle(v(c, 0), v(c, 1), v(c, 2));
        return 0u;
    });
    add("grAADrawLine", 8, [&g, v](Cpu& c) {
        g.notImplemented("grAADrawLine", "drawn without anti-aliasing");
        g.drawLine(v(c, 0), v(c, 1));
        return 0u;
    });
    add("grAADrawPoint", 4, [&g, v](Cpu& c) {
        g.notImplemented("grAADrawPoint", "drawn without anti-aliasing");
        g.drawPoint(v(c, 0));
        return 0u;
    });
    // Polygons: an index list into a vertex array, or the array itself.
    auto polygon = [this, &g](Cpu& c, bool indexed, bool aa) {
        const int count = std::clamp(int(c.arg(0)), 0, 4096);
        std::vector<Glide2Vertex> list;
        for (int i = 0; i < count; ++i) {
            const uint32_t index = indexed ? c.read32(c.arg(1) + 4 * uint32_t(i)) : uint32_t(i);
            list.push_back(vertex(c.arg(indexed ? 2 : 1) + index * uint32_t(sizeof(Glide2Vertex))));
        }
        if (aa)
            g.notImplemented("grAADrawPolygon", "drawn without edge anti-aliasing");
        g.drawPolygon(count, list.data());
        return 0u;
    };
    add("grDrawPolygon", 12, [polygon](Cpu& c) { return polygon(c, true, false); });
    add("grDrawPlanarPolygon", 12, [polygon](Cpu& c) { return polygon(c, true, false); });
    add("grDrawPolygonVertexList", 8, [polygon](Cpu& c) { return polygon(c, false, false); });
    add("grDrawPlanarPolygonVertexList", 8, [polygon](Cpu& c) { return polygon(c, false, false); });
    add("grAADrawPolygon", 12, [polygon](Cpu& c) { return polygon(c, true, true); });
    add("grAADrawPolygonVertexList", 8, [polygon](Cpu& c) { return polygon(c, false, true); });
    // Clipping is the clip window's, per pixel, so these draw as is.
    add("guDrawTriangleWithClip", 12, [&g, v](Cpu& c) { g.drawTriangle(v(c, 0), v(c, 1), v(c, 2)); return 0u; });
    add("guAADrawTriangleWithClip", 12, [&g, v](Cpu& c) { g.drawTriangle(v(c, 0), v(c, 1), v(c, 2)); return 0u; });
    add("guDrawPolygonVertexListWithClip", 8, [polygon](Cpu& c) { return polygon(c, false, false); });
    add("grBufferClear", 12, [&g](Cpu& c) {
        g.bufferClear(c.arg(0), int(c.arg(1) & 0xff), int(c.arg(2) & 0xffff));
        return 0u;
    });
    add("grBufferSwap", 4, [&g, n](Cpu& c) { g.bufferSwap(n(c, 0)); return 0u; });
    add("grBufferNumPending", 0, [&g](Cpu&) { return uint32_t(g.bufferNumPending()); });
    add("grRenderBuffer", 4, [&g, n](Cpu& c) { g.renderBuffer(n(c, 0)); return 0u; });

    // --- State ---------------------------------------------------------------------
    add("grColorCombine", 20, [&g, n, b](Cpu& c) { g.colorCombine(n(c, 0), n(c, 1), n(c, 2), n(c, 3), b(c, 4)); return 0u; });
    add("grAlphaCombine", 20, [&g, n, b](Cpu& c) { g.alphaCombine(n(c, 0), n(c, 1), n(c, 2), n(c, 3), b(c, 4)); return 0u; });
    add("grAlphaControlsITRGBLighting", 4, [&g, b](Cpu& c) { g.alphaControlsITRGBLighting(b(c, 0)); return 0u; });
    add("grAlphaBlendFunction", 16, [&g, n](Cpu& c) { g.alphaBlendFunction(n(c, 0), n(c, 1), n(c, 2), n(c, 3)); return 0u; });
    add("grAlphaTestFunction", 4, [&g, n](Cpu& c) { g.alphaTestFunction(n(c, 0)); return 0u; });
    add("grAlphaTestReferenceValue", 4, [&g](Cpu& c) { g.alphaTestReferenceValue(int(c.arg(0) & 0xff)); return 0u; });
    add("grChromakeyMode", 4, [&g, n](Cpu& c) { g.chromakeyMode(n(c, 0)); return 0u; });
    add("grChromakeyValue", 4, [&g](Cpu& c) { g.chromakeyValue(c.arg(0)); return 0u; });
    add("grClipWindow", 16, [&g](Cpu& c) { g.clipWindow(c.arg(0), c.arg(1), c.arg(2), c.arg(3)); return 0u; });
    add("grColorMask", 8, [&g, b](Cpu& c) { g.colorMask(b(c, 0), b(c, 1)); return 0u; });
    add("grConstantColorValue", 4, [&g](Cpu& c) { g.constantColorValue(c.arg(0)); return 0u; });
    add("grConstantColorValue4", 16, [&g](Cpu& c) {
        g.constantColorValue4(argFloat(c, 0), argFloat(c, 1), argFloat(c, 2), argFloat(c, 3));
        return 0u;
    });
    add("grCullMode", 4, [&g, n](Cpu& c) { g.cullMode(n(c, 0)); return 0u; });
    add("grDepthBiasLevel", 4, [&g](Cpu& c) { g.depthBiasLevel(int(int16_t(c.arg(0) & 0xffff))); return 0u; });
    add("grDepthBufferFunction", 4, [&g, n](Cpu& c) { g.depthBufferFunction(n(c, 0)); return 0u; });
    add("grDepthBufferMode", 4, [&g, n](Cpu& c) { g.depthBufferMode(n(c, 0)); return 0u; });
    add("grDepthMask", 4, [&g, b](Cpu& c) { g.depthMask(b(c, 0)); return 0u; });
    add("grDisableAllEffects", 0, [&g](Cpu&) { g.disableAllEffects(); return 0u; });
    add("grDitherMode", 4, [&g, n](Cpu& c) { g.ditherMode(n(c, 0)); return 0u; });
    add("grFogColorValue", 4, [&g](Cpu& c) { g.fogColorValue(c.arg(0)); return 0u; });
    add("grFogMode", 4, [&g, n](Cpu& c) { g.fogMode(n(c, 0)); return 0u; });
    add("grFogTable", 4, [this, &g](Cpu& c) {
        if (const uint8_t* t = guest(c.arg(0), 64))
            g.fogTable(t);
        return 0u;
    });
    add("grGammaCorrectionValue", 4, [&g](Cpu& c) { g.gammaCorrectionValue(argFloat(c, 0)); return 0u; });
    add("grLfbConstantAlpha", 4, [&g](Cpu& c) { g.lfbConstantAlpha(int(c.arg(0) & 0xff)); return 0u; });
    add("grLfbConstantDepth", 4, [&g](Cpu& c) { g.lfbConstantDepth(int(c.arg(0) & 0xffff)); return 0u; });
    add("guColorCombineFunction", 4, [&g, n](Cpu& c) { g.guColorCombineFunction(n(c, 0)); return 0u; });
    add("guAlphaSource", 4, [&g, n](Cpu& c) { g.guAlphaSource(n(c, 0)); return 0u; });

    addFogUtilities();

    // --- Textures ------------------------------------------------------------------
    add("grTexMinAddress", 4, [&g, n](Cpu& c) { return g.texMinAddress(n(c, 0)); });
    add("grTexMaxAddress", 4, [&g, n](Cpu& c) { return g.texMaxAddress(n(c, 0)); });
    add("grTexCalcMemRequired", 16, [&g, n](Cpu& c) { return g.texCalcMemRequired(n(c, 0), n(c, 1), n(c, 2), n(c, 3)); });
    add("grTexTextureMemRequired", 8, [this, &g, n](Cpu& c) { return g.texTextureMemRequired(n(c, 0), texInfo(c.arg(1))); });
    add("grTexSource", 16, [this, &g, n](Cpu& c) {
        g.texSource(n(c, 0), c.arg(1), n(c, 2), texInfo(c.arg(3)));
        return 0u;
    });
    add("grTexDownloadMipMap", 16, [this, &g, n](Cpu& c) {
        const Glide2TexInfo info = texInfo(c.arg(3));
        const uint32_t data = guest(c.arg(3), 20) ? c.read32(c.arg(3) + 16) : 0;
        if (const uint8_t* p = guest(data, Glide2::mipmapDataBytes(info)))
            g.texDownloadMipMap(n(c, 0), c.arg(1), n(c, 2), info, p);
        return 0u;
    });
    add("grTexDownloadMipMapLevel", 32, [this, &g, n](Cpu& c) {
        if (const uint8_t* p = guest(c.arg(7), Glide2::levelDataBytes(n(c, 2), n(c, 4), n(c, 5))))
            g.texDownloadMipMapLevel(n(c, 0), c.arg(1), n(c, 2), n(c, 3), n(c, 4), n(c, 5), n(c, 6), p);
        return 0u;
    });
    add("grTexDownloadMipMapLevelPartial", 40, [this, &g, n](Cpu& c) {
        // The data is the rows from the first asked for to the last.
        const int first = n(c, 8), last = n(c, 9);
        const uint32_t level = Glide2::levelDataBytes(n(c, 2), n(c, 4), n(c, 5));
        int w = 0, h = 0;
        levelSize(8 - std::clamp(n(c, 2), 0, 8), 3 - std::clamp(n(c, 4), 0, 6), w, h);
        const uint32_t rows = uint32_t(std::clamp(last, 0, h - 1) - std::clamp(first, 0, h - 1) + 1);
        if (const uint8_t* p = guest(c.arg(7), std::min(level, rows * (level / uint32_t(h)))))
            g.texDownloadMipMapLevelPartial(n(c, 0), c.arg(1), n(c, 2), n(c, 3), n(c, 4), n(c, 5), n(c, 6), p, first,
                                            last);
        return 0u;
    });
    add("grTexDownloadTable", 12, [this, &g, n](Cpu& c) {
        const int type = n(c, 1);
        if (const uint8_t* p = guest(c.arg(2), type == kTablePalette ? 1024u : 64u))
            g.texDownloadTable(n(c, 0), type, p);
        return 0u;
    });
    add("grTexDownloadTablePartial", 20, [this, &g, n](Cpu& c) {
        const int type = n(c, 1);
        if (const uint8_t* p = guest(c.arg(2), type == kTablePalette ? 1024u : 64u))
            g.texDownloadTablePartial(n(c, 0), type, p, n(c, 3), n(c, 4));
        return 0u;
    });
    add("grTexNCCTable", 8, [&g, n](Cpu& c) { g.texNCCTable(n(c, 0), n(c, 1)); return 0u; });
    add("grTexClampMode", 12, [&g, n](Cpu& c) { g.texClampMode(n(c, 0), n(c, 1), n(c, 2)); return 0u; });
    add("grTexCombine", 28, [&g, n, b](Cpu& c) {
        g.texCombine(n(c, 0), n(c, 1), n(c, 2), n(c, 3), n(c, 4), b(c, 5), b(c, 6));
        return 0u;
    });
    add("grTexCombineFunction", 8, [&g, n](Cpu& c) { g.texCombineFunction(n(c, 0), n(c, 1)); return 0u; });
    add("guTexCombineFunction", 8, [&g, n](Cpu& c) { g.texCombineFunction(n(c, 0), n(c, 1)); return 0u; });
    add("grTexDetailControl", 16, [&g, n](Cpu& c) {
        g.texDetailControl(n(c, 0), n(c, 1), int(c.arg(2) & 0xff), argFloat(c, 3));
        return 0u;
    });
    add("grTexFilterMode", 12, [&g, n](Cpu& c) { g.texFilterMode(n(c, 0), n(c, 1), n(c, 2)); return 0u; });
    add("grTexLodBiasValue", 8, [&g, n](Cpu& c) { g.texLodBiasValue(n(c, 0), argFloat(c, 1)); return 0u; });
    add("grTexMipMapMode", 12, [&g, n, b](Cpu& c) { g.texMipMapMode(n(c, 0), n(c, 1), b(c, 2)); return 0u; });
    add("grTexMultibase", 8, [&g, n, b](Cpu& c) { g.texMultibase(n(c, 0), b(c, 1)); return 0u; });
    add("grTexMultibaseAddress", 20, [this, &g, n](Cpu& c) {
        g.texMultibaseAddress(n(c, 0), n(c, 1), c.arg(2), n(c, 3), texInfo(c.arg(4)));
        return 0u;
    });

    // --- The linear frame buffer ---------------------------------------------------
    add("grLfbLock", 24, [this](Cpu& c) {
        return lfbLock(c.arg(0), c.arg(1), c.arg(2), c.arg(3), c.arg(4) != 0, c.arg(5));
    });
    add("grLfbUnlock", 8, [this](Cpu& c) { return lfbUnlock(c.arg(0), c.arg(1)); });
    add("grLfbWriteRegion", 32, [this, &g, n](Cpu& c) {
        const uint32_t width = c.arg(4), height = c.arg(5);
        const int stride = int(c.arg(6)), format = n(c, 3);
        const uint32_t bpp = format == kLfb565 || format == kLfb555 || format == kLfb1555 || format == kLfbZA16 ? 2 : 4;
        // RLE16's length isn't known before it's read: as much as there is.
        const uint32_t bytes = format == kLfbRle16 ? 4u
                               : height ? uint32_t(std::abs(stride)) * (height - 1) + width * bpp
                                        : 0;
        const uint8_t* p = guest(c.arg(7), std::min<uint32_t>(bytes, 64u << 20));
        if (!p || stride < 0)
            return 0u;
        return g.lfbWriteRegion(n(c, 0), c.arg(1), c.arg(2), format, width, height, stride, p,
                                cpu_.memory().size() - c.arg(7))
                   ? 1u
                   : 0u;
    });
    add("grLfbReadRegion", 28, [this, &g, n](Cpu& c) {
        const uint32_t width = c.arg(3), height = c.arg(4), stride = c.arg(5);
        if (!height || !guest(c.arg(6), stride * (height - 1) + width * 2))
            return 0u;
        return g.lfbReadRegion(n(c, 0), c.arg(1), c.arg(2), width, height, stride, cpu_.memory().data() + c.arg(6))
                   ? 1u
                   : 0u;
    });
    add("grLfbWriteColorFormat", 4, [&g](Cpu& c) {
        if (c.arg(0) != kColorArgb)
            g.notImplemented("grLfbWriteColorFormat", "only the window's own colour format is used");
        return 0u;
    });
    add("grLfbWriteColorSwizzle", 8, [&g](Cpu& c) {
        if (c.arg(0) || c.arg(1))
            g.notImplemented("grLfbWriteColorSwizzle", "byte and word swaps not carried out");
        return 0u;
    });

    // --- Not stood in for yet ------------------------------------------------------
    // The utility library's texture manager and file loaders, the splash
    // screen and run-length texture downloads: they log once and fail.
    stub("guTexAllocateMemory", 60, 0xffffffffu); // GR_NULL_MIPMAP_HANDLE
    stub("guTexChangeAttributes", 48);
    stub("guTexDownloadMipMap", 12);
    stub("guTexDownloadMipMapLevel", 12);
    stub("guTexGetCurrentMipMap", 4, 0xffffffffu);
    stub("guTexGetMipMapInfo", 4);
    stub("guTexMemQueryAvail", 4);
    stub("guTexMemReset", 0);
    stub("guTexSource", 4);
    stub("guTexCreateColorMipMap", 0);
    stub("guEncodeRLE16", 16);
    stub("guEndianSwapWords", 4);
    stub("guEndianSwapBytes", 4);
    stub("gu3dfGetInfo", 8);
    stub("gu3dfLoad", 8);
    stub("grSplash", 20);
    stub("ConvertAndDownloadRle", 64);
}

} // namespace reluva::glide
