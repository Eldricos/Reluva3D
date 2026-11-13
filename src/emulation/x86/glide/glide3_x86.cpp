// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/glide/glide3_x86.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace reluva::glide {

using x86::Cpu;

Glide3Dll::Glide3Dll(Cpu& cpu, x86::Arena& memory, Board board)
    : GlideDll(cpu, memory, "glide3x.dll"), glide_(board) {
    setCore(glide_.core());
    addAll();
}

const uint8_t* Glide3Dll::vertex(uint32_t address) const {
    const int lo = glide_.layoutMin(), hi = glide_.layoutMax();
    const uint32_t first = address + uint32_t(lo);
    if (!guest(first, uint32_t(hi - lo)))
        return nullptr;
    return cpu_.memory().data() + address;
}

Glide3TexInfo Glide3Dll::texInfo(uint32_t address) const {
    // GrTexInfo: smallLodLog2, largeLodLog2, aspectRatioLog2, format, data.
    Glide3TexInfo t;
    if (guest(address, 20)) {
        t.smallLodLog2 = int(cpu_.read32(address));
        t.largeLodLog2 = int(cpu_.read32(address + 4));
        t.aspectRatioLog2 = int(int32_t(cpu_.read32(address + 8)));
        t.format = int(cpu_.read32(address + 12));
    }
    return t;
}

uint32_t Glide3Dll::guestString(const std::string& s) {
    auto it = strings_.find(s);
    if (it != strings_.end())
        return it->second;
    const uint32_t at = memory_.alloc(uint32_t(s.size() + 1));
    if (!at)
        return 0;
    cpu_.writeString(at, s, uint32_t(s.size() + 1));
    strings_[s] = at;
    return at;
}

void Glide3Dll::addAll() {
    Glide3& g = glide_;
    Glide2& c2 = glide_.core();
    auto b = [](Cpu& c, int i) { return c.arg(i) != 0; };
    auto n = [](Cpu& c, int i) { return int(c.arg(i)); };

    // --- Start-up, the board, shut-down -----------------------------------------------
    add("grGlideInit", 0, [&c2](Cpu&) { c2.glideInit(); return 0u; });
    add("grGlideShutdown", 0, [&c2](Cpu&) { c2.glideShutdown(); return 0u; });
    add("grGet", 12, [this, &g](Cpu& c) {
        const uint32_t bytes = std::min<uint32_t>(c.arg(1), 64);
        int32_t out[16] = {};
        if (!guest(c.arg(2), bytes))
            return 0u;
        const uint32_t written = g.get(c.arg(0), bytes, out);
        for (uint32_t i = 0; i < written / 4; ++i)
            c.write32(c.arg(2) + 4 * i, uint32_t(out[i]));
        return written;
    });
    add("grGetString", 4, [this, &g](Cpu& c) { return guestString(g.getString(c.arg(0))); });
    add("grQueryResolutions", 8, [this, &g](Cpu& c) {
        // GrResolution: resolution, refresh, numColorBuffers, numAuxBuffers.
        if (!guest(c.arg(0), 16))
            return 0u;
        Glide3Resolution q;
        q.resolution = int(c.read32(c.arg(0)));
        q.refresh = int(c.read32(c.arg(0) + 4));
        q.numColorBuffers = int(c.read32(c.arg(0) + 8));
        q.numAuxBuffers = int(c.read32(c.arg(0) + 12));
        const std::vector<Glide3Resolution> list = g.queryResolutions(q);
        const uint32_t bytes = uint32_t(list.size() * 16);
        if (c.arg(1) && guest(c.arg(1), bytes))
            for (uint32_t i = 0; i < list.size(); ++i) {
                c.write32(c.arg(1) + 16 * i, uint32_t(list[i].resolution));
                c.write32(c.arg(1) + 16 * i + 4, uint32_t(list[i].refresh));
                c.write32(c.arg(1) + 16 * i + 8, uint32_t(list[i].numColorBuffers));
                c.write32(c.arg(1) + 16 * i + 12, uint32_t(list[i].numAuxBuffers));
            }
        return bytes;
    });
    add("grReset", 4, [&g](Cpu& c) {
        PixelStats& s = g.pipeline().stats;
        switch (c.arg(0)) {
        case g3::kStatsPixels: s.pixelsIn = s.chromaFail = s.depthFail = s.alphaFail = s.pixelsOut = 0; return 1u;
        case g3::kStatsTriangles: s.trianglesIn = s.trianglesOut = 0; return 1u;
        case g3::kStatsLines:
        case g3::kStatsPoints: return 1u;
        case g3::kVertexParameter: g.setLayout({}); return 1u;
        default: return 0u;
        }
    });
    // The extensions' procedures (stdcall, as Glide's FX_CALL), handed out
    // by name where the board has them (Reference Manual,
    // grGetProcAddress), else 0.
    procs_["grChromaRangeModeExt"] = addProc("grChromaRangeModeExt", 4, [&c2](Cpu& c) {
        c2.chromaRangeMode(c.arg(0) == 1); // GR_CHROMARANGE_ENABLE_EXT
        return 0u;
    });
    procs_["grChromaRangeExt"] = addProc("grChromaRangeExt", 12, [&c2, n](Cpu& c) {
        c2.chromaRange(c.arg(0), c.arg(1), n(c, 2));
        return 0u;
    });
    add("grGetProcAddress", 4, [this, &g](Cpu& c) {
        const std::string name = c.string(c.arg(0), 64);
        auto it = procs_.find(name);
        return it != procs_.end() && g.offersProc(name) ? it->second : 0u;
    });
    add("grSstSelect", 4, [&c2, n](Cpu& c) { c2.sstSelect(n(c, 0)); return 0u; });
    add("grSstWinOpen", 28, [&g, n](Cpu& c) {
        return g.sstWinOpen(c.arg(0), n(c, 1), n(c, 2), n(c, 3), n(c, 4), n(c, 5), n(c, 6));
    });
    add("grSstWinClose", 4, [&g](Cpu& c) { g.sstWinClose(c.arg(0)); return 1u; });
    add("grSelectContext", 4, [&g](Cpu& c) { return g.selectContext(c.arg(0)) ? 1u : 0u; });
    add("grSstOrigin", 4, [&c2, n](Cpu& c) { c2.sstOrigin(n(c, 0)); return 0u; });
    add("grFinish", 0, [&g](Cpu&) { g.pipeline().sync(); return 0u; });
    add("grFlush", 0, [](Cpu&) { return 0u; });
    add("grErrorSetCallback", 4, [](Cpu&) { return 0u; }); // never called back: nothing fails that way
    add("grEnable", 4, [&g](Cpu& c) { g.enable(c.arg(0)); return 0u; });
    add("grDisable", 4, [&g](Cpu& c) { g.disable(c.arg(0)); return 0u; });
    add("grCoordinateSpace", 4, [&g](Cpu& c) { g.coordinateSpace(c.arg(0)); return 0u; });
    add("grViewport", 16, [&g, n](Cpu& c) { g.viewport(n(c, 0), n(c, 1), n(c, 2), n(c, 3)); return 0u; });
    add("grDepthRange", 8, [&g](Cpu& c) { g.depthRange(argFloat(c, 0), argFloat(c, 1)); return 0u; });
    add("grGlideGetState", 4, [this, &g](Cpu& c) {
        if (guest(c.arg(0), 8)) {
            c.write32(c.arg(0), g.glideGetState());
            c.write32(c.arg(0) + 4, 0x33474c53);
        }
        return 0u;
    });
    add("grGlideSetState", 4, [this, &g](Cpu& c) {
        if (guest(c.arg(0), 8))
            g.glideSetState(c.read32(c.arg(0)));
        return 0u;
    });
    // The vertex layout saved whole into the caller's buffer (64 bytes,
    // grGet(GR_GLIDE_VERTEXLAYOUT_SIZE)): each parameter's offset, 0 when
    // disabled... too many for 64 bytes, so as for the state, a number.
    add("grGlideGetVertexLayout", 4, [this, &g](Cpu& c) {
        if (guest(c.arg(0), 8)) {
            const uint32_t token = uint32_t(layouts_.size() + 1);
            layouts_.push_back(g.layout());
            c.write32(c.arg(0), token);
            c.write32(c.arg(0) + 4, 0x4c474c53);
        }
        return 0u;
    });
    add("grGlideSetVertexLayout", 4, [this, &g](Cpu& c) {
        if (guest(c.arg(0), 8) && c.read32(c.arg(0) + 4) == 0x4c474c53) {
            const uint32_t token = c.read32(c.arg(0));
            if (token >= 1 && token <= layouts_.size())
                g.setLayout(layouts_[token - 1]);
        }
        return 0u;
    });

    // --- Drawing -------------------------------------------------------------------
    add("grVertexLayout", 12, [&g](Cpu& c) { g.vertexLayout(c.arg(0), int32_t(c.arg(1)), c.arg(2)); return 0u; });
    add("grDrawTriangle", 12, [this, &g](Cpu& c) {
        const uint8_t *a = vertex(c.arg(0)), *bb = vertex(c.arg(1)), *cc = vertex(c.arg(2));
        if (a && bb && cc)
            g.drawTriangle(a, bb, cc);
        return 0u;
    });
    add("grDrawLine", 8, [this, &g](Cpu& c) {
        const uint8_t *a = vertex(c.arg(0)), *bb = vertex(c.arg(1));
        if (a && bb)
            g.drawLine(a, bb);
        return 0u;
    });
    add("grDrawPoint", 4, [this, &g](Cpu& c) {
        if (const uint8_t* a = vertex(c.arg(0)))
            g.drawPoint(a);
        return 0u;
    });
    add("grAADrawTriangle", 24, [this, &g](Cpu& c) {
        const uint8_t *a = vertex(c.arg(0)), *bb = vertex(c.arg(1)), *cc = vertex(c.arg(2));
        if (a && bb && cc)
            g.aaDrawTriangle(a, bb, cc, c.arg(3) != 0, c.arg(4) != 0, c.arg(5) != 0);
        return 0u;
    });
    add("grDrawVertexArray", 12, [this, &g](Cpu& c) {
        const uint32_t count = std::min<uint32_t>(c.arg(1), 65536);
        if (!guest(c.arg(2), count * 4))
            return 0u;
        std::vector<const uint8_t*> list;
        for (uint32_t i = 0; i < count; ++i)
            if (const uint8_t* p = vertex(c.read32(c.arg(2) + 4 * i)))
                list.push_back(p);
        if (list.size() == count)
            g.drawVertexArray(c.arg(0), list);
        return 0u;
    });
    add("grDrawVertexArrayContiguous", 16, [this, &g](Cpu& c) {
        const uint32_t count = std::min<uint32_t>(c.arg(1), 65536), stride = c.arg(3);
        std::vector<const uint8_t*> list;
        for (uint32_t i = 0; i < count; ++i)
            if (const uint8_t* p = vertex(c.arg(2) + i * stride))
                list.push_back(p);
        if (list.size() == count)
            g.drawVertexArray(c.arg(0), list);
        return 0u;
    });
    add("grBufferClear", 12, [&c2](Cpu& c) {
        c2.bufferClear(c.arg(0), int(c.arg(1) & 0xff), int(c.arg(2) & 0xffff));
        return 0u;
    });
    add("grBufferSwap", 4, [&c2, n](Cpu& c) { c2.bufferSwap(n(c, 0)); return 0u; });
    add("grRenderBuffer", 4, [&c2, n](Cpu& c) { c2.renderBuffer(n(c, 0)); return 0u; });

    // --- State ---------------------------------------------------------------------
    add("grColorCombine", 20, [&c2, n, b](Cpu& c) { c2.colorCombine(n(c, 0), n(c, 1), n(c, 2), n(c, 3), b(c, 4)); return 0u; });
    add("grAlphaCombine", 20, [&c2, n, b](Cpu& c) { c2.alphaCombine(n(c, 0), n(c, 1), n(c, 2), n(c, 3), b(c, 4)); return 0u; });
    add("grAlphaControlsITRGBLighting", 4, [&c2, b](Cpu& c) { c2.alphaControlsITRGBLighting(b(c, 0)); return 0u; });
    add("grAlphaBlendFunction", 16, [&c2, n](Cpu& c) { c2.alphaBlendFunction(n(c, 0), n(c, 1), n(c, 2), n(c, 3)); return 0u; });
    add("grAlphaTestFunction", 4, [&c2, n](Cpu& c) { c2.alphaTestFunction(n(c, 0)); return 0u; });
    add("grAlphaTestReferenceValue", 4, [&c2](Cpu& c) { c2.alphaTestReferenceValue(int(c.arg(0) & 0xff)); return 0u; });
    add("grChromakeyMode", 4, [&c2, n](Cpu& c) { c2.chromakeyMode(n(c, 0)); return 0u; });
    add("grChromakeyValue", 4, [&c2](Cpu& c) { c2.chromakeyValue(c.arg(0)); return 0u; });
    add("grClipWindow", 16, [&c2](Cpu& c) { c2.clipWindow(c.arg(0), c.arg(1), c.arg(2), c.arg(3)); return 0u; });
    add("grColorMask", 8, [&c2, b](Cpu& c) { c2.colorMask(b(c, 0), b(c, 1)); return 0u; });
    add("grConstantColorValue", 4, [&c2](Cpu& c) { c2.constantColorValue(c.arg(0)); return 0u; });
    add("grCullMode", 4, [&c2, n](Cpu& c) { c2.cullMode(n(c, 0)); return 0u; });
    add("grDepthBiasLevel", 4, [&c2](Cpu& c) { c2.depthBiasLevel(int(int16_t(c.arg(0) & 0xffff))); return 0u; });
    add("grDepthBufferFunction", 4, [&c2, n](Cpu& c) { c2.depthBufferFunction(n(c, 0)); return 0u; });
    add("grDepthBufferMode", 4, [&c2, n](Cpu& c) { c2.depthBufferMode(n(c, 0)); return 0u; });
    add("grDepthMask", 4, [&c2, b](Cpu& c) { c2.depthMask(b(c, 0)); return 0u; });
    add("grDisableAllEffects", 0, [&c2](Cpu&) { c2.disableAllEffects(); return 0u; });
    add("grDitherMode", 4, [&c2, n](Cpu& c) { c2.ditherMode(n(c, 0)); return 0u; });
    add("grFogColorValue", 4, [&c2](Cpu& c) { c2.fogColorValue(c.arg(0)); return 0u; });
    add("grFogMode", 4, [&g, n](Cpu& c) { g.fogMode(n(c, 0)); return 0u; });
    add("grFogTable", 4, [this, &g](Cpu& c) {
        if (const uint8_t* t = guest(c.arg(0), uint32_t(std::max(g.board().fogTableEntries, 1))))
            g.fogTable(t);
        return 0u;
    });
    add("guGammaCorrectionRGB", 12, [&g](Cpu& c) {
        g.gammaCorrectionRgb(argFloat(c, 0), argFloat(c, 1), argFloat(c, 2));
        return 0u;
    });
    add("grLfbConstantAlpha", 4, [&c2](Cpu& c) { c2.lfbConstantAlpha(int(c.arg(0) & 0xff)); return 0u; });
    add("grLfbConstantDepth", 4, [&c2](Cpu& c) { c2.lfbConstantDepth(int(c.arg(0) & 0xffff)); return 0u; });
    addFogUtilities();

    // --- Textures ------------------------------------------------------------------
    add("grTexMinAddress", 4, [&c2, n](Cpu& c) { return c2.texMinAddress(n(c, 0)); });
    add("grTexMaxAddress", 4, [&c2, n](Cpu& c) { return c2.texMaxAddress(n(c, 0)); });
    add("grTexCalcMemRequired", 16, [&g, n](Cpu& c) { return g.texCalcMemRequired(n(c, 0), n(c, 1), n(c, 2), n(c, 3)); });
    add("grTexTextureMemRequired", 8, [this, &g, n](Cpu& c) { return g.texTextureMemRequired(n(c, 0), texInfo(c.arg(1))); });
    add("grTexSource", 16, [this, &g, n](Cpu& c) {
        g.texSource(n(c, 0), c.arg(1), n(c, 2), texInfo(c.arg(3)));
        return 0u;
    });
    add("grTexDownloadMipMap", 16, [this, &g, n](Cpu& c) {
        const Glide3TexInfo info = texInfo(c.arg(3));
        const uint32_t data = guest(c.arg(3), 20) ? c.read32(c.arg(3) + 16) : 0;
        if (const uint8_t* p = guest(data, Glide3::mipmapDataBytes(info)))
            g.texDownloadMipMap(n(c, 0), c.arg(1), n(c, 2), info, p);
        return 0u;
    });
    auto levelPartial = [this, &g, n](Cpu& c, int first, int last) {
        // The data is the rows from the first asked for to the last.
        const uint32_t level = Glide3::levelDataBytes(n(c, 2), n(c, 4), n(c, 5));
        int w = 0, h = 0;
        levelSize(std::clamp(n(c, 2), 0, 8), std::clamp(n(c, 4), -3, 3), w, h);
        const uint32_t rows = uint32_t(std::clamp(last, 0, h - 1) - std::clamp(first, 0, h - 1) + 1);
        if (const uint8_t* p = guest(c.arg(7), std::min(level, rows * (level / uint32_t(h)))))
            g.texDownloadMipMapLevelPartial(n(c, 0), c.arg(1), n(c, 2), n(c, 3), n(c, 4), n(c, 5), n(c, 6), p, first,
                                            last);
        return 0u;
    };
    add("grTexDownloadMipMapLevel", 32, [levelPartial](Cpu& c) { return levelPartial(c, 0, 255); });
    add("grTexDownloadMipMapLevelPartial", 40, [levelPartial, n](Cpu& c) { return levelPartial(c, n(c, 8), n(c, 9)); });
    add("grTexDownloadTable", 8, [this, &g, n](Cpu& c) {
        const int type = n(c, 0);
        if (const uint8_t* p = guest(c.arg(1), type == kTablePalette ? 1024u : 64u))
            g.texDownloadTablePartial(type, p, 0, 255);
        return 0u;
    });
    add("grTexDownloadTablePartial", 16, [this, &g, n](Cpu& c) {
        const int type = n(c, 0);
        if (const uint8_t* p = guest(c.arg(1), type == kTablePalette ? 1024u : 64u))
            g.texDownloadTablePartial(type, p, n(c, 2), n(c, 3));
        return 0u;
    });
    add("grTexNCCTable", 4, [&g, n](Cpu& c) { g.texNCCTable(n(c, 0)); return 0u; });
    // GR_TEXTURECLAMP_MIRROR_EXT (2) where the board has TEXMIRROR (the
    // core wraps it elsewhere).
    add("grTexClampMode", 12, [&c2, n](Cpu& c) { c2.texClampMode(n(c, 0), n(c, 1), n(c, 2)); return 0u; });
    add("grTexCombine", 28, [&c2, n, b](Cpu& c) {
        c2.texCombine(n(c, 0), n(c, 1), n(c, 2), n(c, 3), n(c, 4), b(c, 5), b(c, 6));
        return 0u;
    });
    add("grTexDetailControl", 16, [&c2, n](Cpu& c) {
        c2.texDetailControl(n(c, 0), n(c, 1), int(c.arg(2) & 0xff), argFloat(c, 3));
        return 0u;
    });
    add("grTexFilterMode", 12, [&c2, n](Cpu& c) { c2.texFilterMode(n(c, 0), n(c, 1), n(c, 2)); return 0u; });
    add("grTexLodBiasValue", 8, [&c2, n](Cpu& c) { c2.texLodBiasValue(n(c, 0), argFloat(c, 1)); return 0u; });
    add("grTexMipMapMode", 12, [&g, n, b](Cpu& c) { g.texMipMapMode(n(c, 0), n(c, 1), b(c, 2)); return 0u; });
    add("grTexMultibase", 8, [&c2, n, b](Cpu& c) { c2.texMultibase(n(c, 0), b(c, 1)); return 0u; });
    add("grTexMultibaseAddress", 20, [&g, n](Cpu& c) { g.texMultibaseAddress(n(c, 0), n(c, 1), c.arg(2)); return 0u; });

    // --- The linear frame buffer ---------------------------------------------------
    add("grLfbLock", 24, [this](Cpu& c) {
        return lfbLock(c.arg(0), c.arg(1), c.arg(2), c.arg(3), c.arg(4) != 0, c.arg(5));
    });
    add("grLfbUnlock", 8, [this](Cpu& c) { return lfbUnlock(c.arg(0), c.arg(1)); });
    // Glide 3 adds the pixel pipeline flag before the stride (Reference
    // Manual, grLfbWriteRegion).
    add("grLfbWriteRegion", 36, [this, &c2, n](Cpu& c) {
        const uint32_t width = c.arg(4), height = c.arg(5);
        const bool pipeline = c.arg(6) != 0;
        const int stride = int(c.arg(7)), format = n(c, 3);
        const uint32_t bpp = format == kLfb565 || format == kLfb555 || format == kLfb1555 || format == kLfbZA16 ? 2 : 4;
        const uint32_t bytes = format == kLfbRle16 ? 4u
                               : height ? uint32_t(std::abs(stride)) * (height - 1) + width * bpp
                                        : 0;
        const uint8_t* p = guest(c.arg(8), std::min<uint32_t>(bytes, 64u << 20));
        if (!p || stride < 0)
            return 0u;
        return c2.lfbWriteRegion(n(c, 0), c.arg(1), c.arg(2), format, width, height, stride, p,
                                 cpu_.memory().size() - c.arg(8), pipeline)
                   ? 1u
                   : 0u;
    });
    add("grLfbReadRegion", 28, [this, &c2, n](Cpu& c) {
        const uint32_t width = c.arg(3), height = c.arg(4), stride = c.arg(5);
        if (!height || !guest(c.arg(6), stride * (height - 1) + width * 2))
            return 0u;
        return c2.lfbReadRegion(n(c, 0), c.arg(1), c.arg(2), width, height, stride, cpu_.memory().data() + c.arg(6))
                   ? 1u
                   : 0u;
    });
    add("grLfbWriteColorFormat", 4, [&c2](Cpu& c) { c2.lfbWriteColorFormat(int(c.arg(0))); return 0u; });
    add("grLfbWriteColorSwizzle", 8, [&c2](Cpu& c) {
        c2.lfbWriteColorSwizzle(c.arg(0) != 0, c.arg(1) != 0);
        return 0u;
    });

    // --- Not stood in for yet: they log once and fail ----------------------------
    add("grLoadGammaTable", 16, [this, &g](Cpu& c) {
        const int n = int(std::min<uint32_t>(c.arg(0), 4096));
        const uint8_t *r = guest(c.arg(1), uint32_t(n) * 4), *gg = guest(c.arg(2), uint32_t(n) * 4),
                      *b = guest(c.arg(3), uint32_t(n) * 4);
        if (n > 0 && r && gg && b) {
            const size_t count = size_t(n);
            std::vector<uint32_t> rv(count), gv(count), bv(count);
            std::memcpy(rv.data(), r, size_t(n) * 4);
            std::memcpy(gv.data(), gg, size_t(n) * 4);
            std::memcpy(bv.data(), b, size_t(n) * 4);
            g.pipeline().loadGammaTable(n, rv.data(), gv.data(), bv.data());
        }
        return 0u;
    });
    stub("grSplash", 20);
    add3dfLoaders(true);
}

} // namespace reluva::glide
