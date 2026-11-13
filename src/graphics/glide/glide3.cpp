// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/glide/glide3.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace reluva::glide {

namespace {

float readFloat(const uint8_t* p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}

constexpr uint32_t kStateMagic = 0x33474c53; // "SLG3": a state buffer Reluva filled

// The last parameter grVertexLayout takes, and how many bytes each reads.
constexpr uint32_t kParams = 0x60;
int paramBytes(uint32_t param) {
    switch (param) {
    case g3::kParamXy: return 8;
    case g3::kParamRgb: return 12;
    default:
        if (param >= g3::kParamSt0 && param < g3::kParamSt0 + kMaxTmus)
            return 8;
        return 4;
    }
}

} // namespace

Glide3::Glide3(Board board) : board_(board), core_(board) {
    core_.logName = "glide3x";
    // Glide 3's defaults: window coordinates, nothing in the layout.
    layout_.space = g3::kWindowCoords;
}

// --- Start-up, the board ------------------------------------------------------------

uint32_t Glide3::get(uint32_t pname, uint32_t bytes, int32_t* out) {
    std::vector<int32_t> v;
    const Pipeline& p = core_.pipeline();
    const PixelStats& s = p.stats;
    switch (pname) {
    case g3::kBitsDepth: v = {16}; break;
    case g3::kBitsRgba: v = {5, 6, 5, 0}; break;
    case g3::kBitsGamma:
        // "If gamma correction is not available, grGet will fail, and the
        // params array will be unmodified" (Reference Manual, grGet): the
        // Voodoo Rush's (Board::gammaTableEntries 0).
        if (board_.gammaTableEntries == 0)
            return 0;
        v = {8};
        break;
    case g3::kFifoFullness: v = {0, 0}; break; // the FIFO empty: nothing waits
    case g3::kFogTableEntries: v = {board_.fogTableEntries}; break;
    case g3::kGammaTableEntries:
        // "Returns FXFALSE if it is not possible to manipulate gamma".
        if (board_.gammaTableEntries == 0)
            return 0;
        v = {board_.gammaTableEntries};
        break;
    case g3::kGlideStateSize: // the state buffer holds a number (glideGetState)
    case g3::kGlideVertexLayoutSize: v = {64}; break;
    case g3::kIsBusy: v = {0}; break;
    // "FXTRUE if LFB writes can go through the 3D pixel pipe": not on the
    // Voodoo Rush (Board::lfbPixelPipeline).
    case g3::kLfbPixelPipe: v = {board_.lfbPixelPipeline ? 1 : 0}; break;
    case g3::kMaxTextureSize: v = {256}; break;
    case g3::kMaxTextureAspectRatio: v = {3}; break;
    // Not a UMA configuration: frame buffer and textures apart (Reference
    // Manual, grGet: GR_MEMORY_UMA is then 0).
    // The Voodoo3 and Banshee: "The total number of bytes if a UMA
    // configuration", FB and TMU then 0.
    case g3::kMemoryFb: v = {board_.uma ? 0 : int32_t(board_.frameBufferMemory)}; break;
    case g3::kMemoryTmu: v = {board_.uma ? 0 : int32_t(board_.tmuMemory)}; break;
    case g3::kMemoryUma: v = {board_.uma ? int32_t(board_.tmuMemory) : 0}; break;
    case g3::kNumBoards: v = {1}; break;
    case g3::kNonPowerOfTwoTextures: v = {0}; break;
    case g3::kNumFb: v = {board_.sli ? 2 : 1}; break;
    case g3::kNumSwapHistoryBuffer: v = {0}; break;
    case g3::kNumTmu: v = {board_.tmus}; break;
    case g3::kPendingBufferSwaps: v = {0}; break;
    case g3::kRevisionFb: v = {board_.fbiRevision}; break;
    case g3::kRevisionTmu: v = {board_.tmuRevision}; break;
    case g3::kStatsLines: v = {0}; break;
    case g3::kStatsPoints: v = {0}; break;
    case g3::kStatsPixelsAfuncFail: v = {int32_t(s.alphaFail)}; break;
    case g3::kStatsPixelsChromaFail: v = {int32_t(s.chromaFail)}; break;
    case g3::kStatsPixelsDepthfuncFail: v = {int32_t(s.depthFail)}; break;
    case g3::kStatsPixelsIn: v = {int32_t(s.pixelsIn)}; break;
    case g3::kStatsPixelsOut: v = {int32_t(s.pixelsOut)}; break;
    case g3::kStatsTrianglesIn: v = {int32_t(s.trianglesIn)}; break;
    case g3::kStatsTrianglesOut: v = {int32_t(s.trianglesOut)}; break;
    case g3::kSupportsPassthru: v = {0}; break;
    case g3::kTextureAlign: v = {board_.textureAlign}; break;
    case g3::kVideoPosition: v = {int32_t(core_.sstVideoLine()), 0}; break;
    case g3::kViewport: v = {viewport_[0], viewport_[1], viewport_[2], viewport_[3]}; break;
    case g3::kWdepthMinMax:
    case g3::kZdepthMinMax: v = {0, 0xffff}; break;
    default:
        notImplemented("grGet " + std::to_string(pname), "a query Reluva doesn't answer: fails");
        return 0;
    }
    const uint32_t need = uint32_t(v.size() * 4);
    if (!out || bytes < need)
        return 0;
    std::copy(v.begin(), v.end(), out);
    return need;
}

std::string Glide3::getString(uint32_t pname) const {
    switch (pname) {
    case g3::kExtension: {
        // A space-separated list of the board's extensions, or "a single
        // space" if none (Reference Manual, grGetString). TEXCHROMA and
        // FOGCOORD aren't offered: no board here has texture chroma-range
        // or fog coordinate registers in its specification.
        std::string list;
        if (board_.extensions & kExtChromaRange)
            list += " CHROMARANGE";
        if (board_.extensions & kExtTexMirror)
            list += " TEXMIRROR";
        if (board_.extensions & kExtPalette6666)
            list += " PALETTE6666";
        return list.empty() ? " " : list.substr(1);
    }
    case g3::kHardware:
        // The names NFS III's Voodoo3 patch DLL tells boards apart by (its
        // strings at 0x600115f0-0x60011624, compared at 0x60001730).
        switch (board_.generation) {
        case Generation::Voodoo1: return "Voodoo Graphics";
        case Generation::VoodooRush: return "Voodoo Rush";
        case Generation::Voodoo2: return "Voodoo2";
        case Generation::Banshee: return "Voodoo Banshee";
        case Generation::Voodoo3: return "Voodoo3 (tm)";
        }
        return "Voodoo2";
    case g3::kRenderer: return "Glide";
    case g3::kVendor: return "3Dfx Interactive";
    // At least 3.10, the version the patch DLL takes as current (0x600115e8).
    case g3::kVersion: return "3.10";
    default: return "";
    }
}

bool Glide3::offersProc(const std::string& name) const {
    // CHROMARANGE's two procedures (Reference Manual, grGetProcAddress);
    // TEXMIRROR and PALETTE6666 add modes, not procedures.
    if (name == "grChromaRangeModeExt" || name == "grChromaRangeExt")
        return (board_.extensions & kExtChromaRange) != 0;
    return false;
}

std::vector<Glide3Resolution> Glide3::queryResolutions(const Glide3Resolution& q) const {
    std::vector<Glide3Resolution> out;
    auto matches = [](int want, int have) { return uint32_t(want) == g3::kQueryAny || want == have; };
    for (int res = 0; res < 24; ++res) {
        int w = 0, h = 0;
        if (!Glide2::resolutionSize(res, w, h) || w > board_.maxWidth || h > board_.maxHeight || !matches(q.resolution, res))
            continue;
        // One refresh rate each (the board shows what it's asked for).
        const int refresh = uint32_t(q.refresh) == g3::kQueryAny ? 0 : q.refresh;
        for (int buffers = 2; buffers <= 3; ++buffers)
            for (int aux = 0; aux <= 1; ++aux) {
                if (!matches(q.numColorBuffers, buffers) || !matches(q.numAuxBuffers, aux))
                    continue;
                const uint64_t need = uint64_t(w) * uint64_t(h) * 2u * uint64_t(buffers + aux);
                if (need > uint64_t(board_.frameBufferMemory) * (board_.sli ? 2u : 1u))
                    continue;
                out.push_back({res, refresh, buffers, aux});
            }
    }
    return out;
}

uint32_t Glide3::sstWinOpen(uint32_t hwnd, int resolution, int refresh, int colorFormat, int origin, int colorBuffers,
                            int auxBuffers) {
    if (!core_.sstWinOpen(hwnd, resolution, refresh, colorFormat, origin, colorBuffers, auxBuffers))
        return 0;
    viewport_[0] = viewport_[1] = 0;
    viewport_[2] = core_.pipeline().width();
    viewport_[3] = core_.pipeline().height();
    lastMode_ = 0xffffffffu;
    mipmapAsked_.fill(kMipmapDisable);
    context_ = context_ + 1 ? context_ + 1 : 1;
    return context_;
}

void Glide3::sstWinClose(uint32_t) { core_.sstWinClose(); }

bool Glide3::selectContext(uint32_t context) { return context && context == context_; }

void Glide3::enable(uint32_t mode) {
    if (mode < enabled_.size())
        enabled_[mode] = true;
    if (mode == g3::kAllowMipmapDither)
        for (int i = 0; i < kMaxTmus; ++i)
            texMipMapMode(i, mipmapAsked_[size_t(i)], lodBlend_[size_t(i)]);
    else if (mode == g3::kVideoSmoothing)
        notImplemented("grEnable(GR_VIDEO_SMOOTHING)", "the video filter isn't modelled");
}

void Glide3::disable(uint32_t mode) {
    if (mode < enabled_.size())
        enabled_[mode] = false;
    if (mode == g3::kAllowMipmapDither)
        for (int i = 0; i < kMaxTmus; ++i)
            texMipMapMode(i, mipmapAsked_[size_t(i)], lodBlend_[size_t(i)]);
}

// --- Vertices --------------------------------------------------------------------------

void Glide3::coordinateSpace(uint32_t mode) { layout_.space = mode; }

void Glide3::viewport(int x, int y, int width, int height) {
    viewport_[0] = x;
    viewport_[1] = y;
    viewport_[2] = width;
    viewport_[3] = height;
}

void Glide3::depthRange(float nearValue, float farValue) {
    depthNear_ = nearValue;
    depthFar_ = farValue;
}

void Glide3::vertexLayout(uint32_t param, int32_t offset, uint32_t mode) {
    if (param >= kParams) {
        notImplemented("grVertexLayout " + std::to_string(param), "an unknown parameter");
        return;
    }
    layout_.enabled[param] = mode == g3::kParamEnable;
    if (layout_.enabled[param])
        layout_.offset[param] = offset;
}

int Glide3::layoutMin() const {
    int lo = 0;
    for (uint32_t p = 0; p < kParams; ++p)
        if (layout_.enabled[p])
            lo = std::min(lo, layout_.offset[p]);
    return lo;
}

int Glide3::layoutMax() const {
    int hi = 0;
    for (uint32_t p = 0; p < kParams; ++p)
        if (layout_.enabled[p])
            hi = std::max(hi, layout_.offset[p] + paramBytes(p));
    return hi;
}

Vertex Glide3::readVertex(const uint8_t* at) {
    const Layout& l = layout_;
    auto has = [&](uint32_t p) { return l.enabled[p]; };
    auto f = [&](uint32_t p, int k = 0) { return readFloat(at + l.offset[p] + 4 * k); };
    Vertex& v = last_;
    if (has(g3::kParamXy)) {
        v.x = f(g3::kParamXy);
        v.y = f(g3::kParamXy, 1);
    }
    if (has(g3::kParamPargb)) {
        uint32_t c;
        std::memcpy(&c, at + l.offset[g3::kParamPargb], 4);
        v.a = float(c >> 24);
        v.r = float(c >> 16 & 255);
        v.g = float(c >> 8 & 255);
        v.b = float(c & 255);
    }
    const bool clip = l.space == g3::kClipCoords;
    const float colorScale = clip ? 255.0f : 1.0f; // clip coordinates give colours 0-1
    if (has(g3::kParamRgb)) {
        v.r = f(g3::kParamRgb) * colorScale;
        v.g = f(g3::kParamRgb, 1) * colorScale;
        v.b = f(g3::kParamRgb, 2) * colorScale;
    }
    if (has(g3::kParamA))
        v.a = f(g3::kParamA) * colorScale;
    // Texture coordinates: TMU n's own s and t, else TMU0's (Glide 2's
    // default, Reluva's reading for a layout that gives only ST0); its own
    // q, else the vertex's.
    float s[kMaxTmus], t[kMaxTmus], q[kMaxTmus];
    bool hasQ[kMaxTmus];
    for (uint32_t i = 0; i < uint32_t(kMaxTmus); ++i) {
        const uint32_t st = g3::kParamSt0 + i, qi = g3::kParamQ0 + i;
        if (has(st)) {
            s[i] = f(st);
            t[i] = f(st, 1);
        } else if (i > 0) {
            s[i] = s[0];
            t[i] = t[0];
        } else {
            s[i] = v.tmu[0].sow;
            t[i] = v.tmu[0].tow;
        }
        hasQ[i] = has(qi);
        q[i] = hasQ[i] ? f(qi) : 0;
    }
    if (!clip) {
        // Window coordinates: the application has divided by w (Reference
        // Manual, grCoordinateSpace): z is 1/z (0-65535), q 1/w, s and t
        // already s/w and t/w (0-256 a repeat).
        if (has(g3::kParamZ))
            v.ooz = f(g3::kParamZ);
        if (has(g3::kParamQ))
            v.oow = f(g3::kParamQ);
        for (int i = 0; i < kMaxTmus; ++i) {
            v.tmu[size_t(i)].sow = s[i];
            v.tmu[size_t(i)].tow = t[i];
            v.tmu[size_t(i)].oow = hasQ[i] ? q[i] : v.oow;
        }
        return v;
    }
    // Clip coordinates: kept as given here (clipVertex_), then clipped
    // and divided by w (project).
    ClipVertex& c = clip_;
    c.window = false;
    c.v = v;
    c.x = v.x;
    c.y = v.y;
    c.w = has(g3::kParamW) ? f(g3::kParamW) : 1.0f;
    c.hasZ = has(g3::kParamZ);
    c.z = c.hasZ ? f(g3::kParamZ) : 0.0f;
    c.q = has(g3::kParamQ) ? f(g3::kParamQ) : 1.0f;
    for (int i = 0; i < kMaxTmus; ++i) {
        c.s[i] = s[i];
        c.t[i] = t[i];
        c.qi[i] = hasQ[i] ? q[i] : 1.0f;
    }
    return project(c);
}

Vertex Glide3::project(const ClipVertex& c) const {
    if (c.window)
        return c.v;
    // Divided by w and put through the viewport (grCoordinateSpace,
    // grViewport, grDepthRange): x = (x/w + 1) width/2 + x0, z from
    // [-1, 1] to the depth range. A vertex with w of 0 (only reached when
    // the view volume's test can't apply) is left at the origin.
    const float oow = c.w != 0 ? 1.0f / c.w : 0.0f;
    const float xs = c.x * oow, ys = c.y * oow;
    Vertex out = c.v;
    out.x = (xs + 1) * float(viewport_[2]) / 2 + float(viewport_[0]);
    out.y = (ys + 1) * float(viewport_[3]) / 2 + float(viewport_[1]);
    if (c.hasZ) {
        const float zs = c.z * oow;
        out.ooz = ((zs + 1) / 2 * (depthFar_ - depthNear_) + depthNear_) * 65535.0f;
    }
    out.oow = oow * c.q;
    for (int i = 0; i < kMaxTmus; ++i) {
        // s and t 0-1 a repeat, whatever the size (Reference Manual).
        const float qq = c.qi[i] * oow;
        out.tmu[size_t(i)].sow = c.s[i] * 256.0f * qq;
        out.tmu[size_t(i)].tow = c.t[i] * 256.0f * qq;
        out.tmu[size_t(i)].oow = qq;
    }
    return out;
}

// --- Clipping against the view volume ----------------------------------------------
// The view volume of clip coordinates: -w <= x, y <= w, and -w <= z <= w
// when z is given ("If z buffering is enabled, z should be in the range
// [-w..+w]"; x/w and y/w are "normalized device coordinates", Glide 3.0
// Programming Guide, Coordinate Spaces). The manuals leave geometric
// clipping to the application ("proper geometric clipping should be done by
// the application", grClipWindow); Reluva clips what's outside so that
// nothing behind the eye (w <= 0) is divided through. Every parameter is
// interpolated linearly in clip space, where the hardware's perspective-
// correct iteration has them linear too.

namespace {

// Plane k's signed distance (inside >= 0): w + x, w - x, w + y, w - y, and
// for z, w + z, w - z.
float planeDistance(const Glide3::ClipVertex& c, int k) {
    switch (k) {
    case 0: return c.w + c.x;
    case 1: return c.w - c.x;
    case 2: return c.w + c.y;
    case 3: return c.w - c.y;
    case 4: return c.w + c.z;
    default: return c.w - c.z;
    }
}

Glide3::ClipVertex lerp(const Glide3::ClipVertex& a, const Glide3::ClipVertex& b, float t) {
    auto m = [t](float x, float y) { return x + (y - x) * t; };
    Glide3::ClipVertex o = a;
    o.x = m(a.x, b.x);
    o.y = m(a.y, b.y);
    o.z = m(a.z, b.z);
    o.w = m(a.w, b.w);
    o.q = m(a.q, b.q);
    o.v.r = m(a.v.r, b.v.r);
    o.v.g = m(a.v.g, b.v.g);
    o.v.b = m(a.v.b, b.v.b);
    o.v.a = m(a.v.a, b.v.a);
    for (int i = 0; i < kMaxTmus; ++i) {
        o.s[i] = m(a.s[i], b.s[i]);
        o.t[i] = m(a.t[i], b.t[i]);
        o.qi[i] = m(a.qi[i], b.qi[i]);
    }
    return o;
}

int planes(const Glide3::ClipVertex& c) { return c.hasZ ? 6 : 4; }

bool insideVolume(const Glide3::ClipVertex& c) {
    if (c.window)
        return true;
    if (!(c.w > 0))
        return false;
    for (int k = 0; k < planes(c); ++k)
        if (planeDistance(c, k) < 0)
            return false;
    return true;
}

} // namespace

void Glide3::clipTriangle(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, bool aa) {
    if (aa) {
        clipAaPolygon({a, b, c}, {true, true, true});
        return;
    }
    if (insideVolume(a) && insideVolume(b) && insideVolume(c)) {
        core_.triangle(project(a), project(b), project(c));
        return;
    }
    // Sutherland-Hodgman against each plane, w > 0 first.
    std::vector<ClipVertex> poly = {a, b, c}, next;
    const int n = planes(a);
    for (int k = -1; k < n && !poly.empty(); ++k) {
        next.clear();
        for (size_t i = 0; i < poly.size(); ++i) {
            const ClipVertex& p = poly[i];
            const ClipVertex& q = poly[(i + 1) % poly.size()];
            constexpr float kNearW = 1e-6f;
            const float dp = k < 0 ? p.w - kNearW : planeDistance(p, k);
            const float dq = k < 0 ? q.w - kNearW : planeDistance(q, k);
            if (dp >= 0)
                next.push_back(p);
            if ((dp >= 0) != (dq >= 0))
                next.push_back(lerp(p, q, dp / (dp - dq)));
        }
        poly.swap(next);
    }
    if (poly.size() < 3)
        return;
    std::vector<Vertex> out;
    for (const ClipVertex& v : poly)
        out.push_back(project(v));
    for (size_t i = 1; i + 1 < out.size(); ++i)
        core_.triangle(out[0], out[i], out[i + 1]);
}

void Glide3::clipAaPolygon(std::vector<ClipVertex> poly, std::vector<bool> edges) {
    if (poly.size() < 3 || edges.size() != poly.size())
        return;
    bool inside = true;
    for (const ClipVertex& v : poly)
        inside = inside && insideVolume(v);
    if (!inside) {
        // Sutherland-Hodgman as clipTriangle, each edge keeping its flag:
        // a piece of an edge has the edge's, and the edge a plane cuts
        // across the polygon has a fringe as well. The documents' own
        // clipped anti-aliased triangle does so ("All edges of the clipped
        // triangle will be anti-aliased", Glide 2.4 Programming Guide,
        // guAADrawTriangleWithClip, drawn "with
        // grAADrawPolygonVertexList", whose "exterior polygon edges are
        // anti-aliased"); for the view volume that is Reluva's choice.
        std::vector<ClipVertex> next;
        std::vector<bool> nextEdges;
        const int n = planes(poly[0]);
        for (int k = -1; k < n && !poly.empty(); ++k) {
            next.clear();
            nextEdges.clear();
            for (size_t i = 0; i < poly.size(); ++i) {
                const ClipVertex& p = poly[i];
                const ClipVertex& q = poly[(i + 1) % poly.size()];
                constexpr float kNearW = 1e-6f;
                const float dp = k < 0 ? p.w - kNearW : planeDistance(p, k);
                const float dq = k < 0 ? q.w - kNearW : planeDistance(q, k);
                if (dp >= 0) {
                    next.push_back(p);
                    nextEdges.push_back(edges[i]);
                    if (dq < 0) { // leaving: then along the plane
                        next.push_back(lerp(p, q, dp / (dp - dq)));
                        nextEdges.push_back(true);
                    }
                } else if (dq >= 0) { // entering: the rest of this edge
                    next.push_back(lerp(p, q, dp / (dp - dq)));
                    nextEdges.push_back(edges[i]);
                }
            }
            poly.swap(next);
            edges.swap(nextEdges);
        }
        if (poly.size() < 3)
            return;
    }
    std::vector<Vertex> out;
    for (const ClipVertex& v : poly)
        out.push_back(project(v));
    if (out.size() == 3) {
        core_.aaTriangle(out[0], out[1], out[2], edges[0], edges[1], edges[2]);
        return;
    }
    std::unique_ptr<bool[]> flags(new bool[edges.size()]);
    for (size_t i = 0; i < edges.size(); ++i)
        flags[i] = edges[i];
    core_.aaPolygon(int(out.size()), out.data(), flags.get());
}

void Glide3::clipLine(const ClipVertex& a, const ClipVertex& b, bool aa) {
    ClipVertex p = a, q = b;
    float t0 = 0, t1 = 1;
    const int n = planes(a);
    for (int k = -1; k < n; ++k) {
        const float dp = k < 0 ? a.w - 1e-6f : planeDistance(a, k);
        const float dq = k < 0 ? b.w - 1e-6f : planeDistance(b, k);
        if (dp < 0 && dq < 0)
            return;
        if (dp < 0)
            t0 = std::max(t0, dp / (dp - dq));
        else if (dq < 0)
            t1 = std::min(t1, dp / (dp - dq));
    }
    if (t0 > t1)
        return;
    p = lerp(a, b, t0);
    q = lerp(a, b, t1);
    if (aa)
        core_.aaLine(project(p), project(q));
    else
        core_.line(project(p), project(q));
}

void Glide3::clipPoint(const ClipVertex& a, bool aa) {
    if (!insideVolume(a))
        return;
    if (aa)
        core_.aaPoint(project(a));
    else
        core_.point(project(a));
}

// --- Drawing ---------------------------------------------------------------------------

namespace {
// Each TMU's own s, t and 1/w, which the layouts always give (the pipeline
// reads them as Glide 2's stw hints say).
constexpr uint32_t kOwnStw = kStwWDiffTmu0 | kStwStDiffTmu1 | kStwWDiffTmu1 | kStwStDiffTmu2 | kStwWDiffTmu2;
} // namespace

Glide3::ClipVertex Glide3::readClip(const uint8_t* at) {
    const Vertex v = readVertex(at);
    if (layout_.space == g3::kClipCoords)
        return clip_;
    ClipVertex c; // window coordinates: drawn as they are
    c.window = true;
    c.v = v;
    return c;
}

void Glide3::drawTriangle(const uint8_t* a, const uint8_t* b, const uint8_t* c) {
    pipeline().state().stwHints = kOwnStw;
    const ClipVertex va = readClip(a), vb = readClip(b), vc = readClip(c);
    clipTriangle(va, vb, vc, enabled(g3::kAaOrdered));
    lastMode_ = 0xffffffffu;
}

void Glide3::aaDrawTriangle(const uint8_t* a, const uint8_t* b, const uint8_t* c, bool ab, bool bc, bool ca) {
    pipeline().state().stwHints = kOwnStw;
    const ClipVertex va = readClip(a), vb = readClip(b), vc = readClip(c);
    clipAaPolygon({va, vb, vc}, {ab, bc, ca});
    lastMode_ = 0xffffffffu;
}

void Glide3::drawLine(const uint8_t* a, const uint8_t* b) {
    pipeline().state().stwHints = kOwnStw;
    const ClipVertex va = readClip(a), vb = readClip(b);
    clipLine(va, vb, enabled(g3::kAaOrdered));
    lastMode_ = 0xffffffffu;
}

void Glide3::drawPoint(const uint8_t* a) {
    pipeline().state().stwHints = kOwnStw;
    clipPoint(readClip(a), enabled(g3::kAaOrdered));
    lastMode_ = 0xffffffffu;
}

// With GR_AA_ORDERED on, triangles, lines, points and polygons are
// anti-aliased ("grDrawTriangle will anti-aliased all three edges of the
// triangle"; grDrawLine, grDrawPoint likewise; grDrawVertexArray's
// GR_POLYGON for grAADrawPolygon), strips and fans not ("This mode applies
// to all primitives except strips and fans"; Glide 3.0 Reference Manual,
// grEnable, grDrawTriangle, grDrawVertexArray).
void Glide3::drawVertexArray(uint32_t mode, const std::vector<const uint8_t*>& pointers) {
    pipeline().state().stwHints = kOwnStw;
    std::vector<ClipVertex> v;
    v.reserve(pointers.size());
    for (const uint8_t* p : pointers)
        v.push_back(readClip(p));
    const size_t n = v.size();
    const bool aa = enabled(g3::kAaOrdered);
    switch (mode) {
    case g3::kPoints:
        for (const ClipVertex& p : v)
            clipPoint(p, aa);
        break;
    case g3::kLineStrip:
        for (size_t i = 0; i + 1 < n; ++i)
            clipLine(v[i], v[i + 1], aa);
        break;
    case g3::kLines:
        for (size_t i = 0; i + 1 < n; i += 2)
            clipLine(v[i], v[i + 1], aa);
        break;
    case g3::kTriangles:
        for (size_t i = 0; i + 2 < n; i += 3)
            clipTriangle(v[i], v[i + 1], v[i + 2], aa);
        break;
    case g3::kPolygon: {
        // A fan; with GR_AA_ORDERED its outside edges anti-aliased (the
        // polygon clipped whole, so that only its outside edges have them).
        if (aa) {
            clipAaPolygon(v, std::vector<bool>(n, true));
        } else {
            for (size_t i = 1; i + 1 < n; ++i)
                clipTriangle(v[0], v[i], v[i + 1], false);
        }
        break;
    }
    case g3::kTriangleFan:
    case g3::kTriangleFanContinue: {
        // A continued fan keeps its first vertex and the last one drawn.
        std::vector<ClipVertex> all;
        if (mode == g3::kTriangleFanContinue && lastMode_ == g3::kTriangleFan)
            all = strip_;
        all.insert(all.end(), v.begin(), v.end());
        for (size_t i = 1; i + 1 < all.size(); ++i)
            clipTriangle(all[0], all[i], all[i + 1], false);
        if (all.size() >= 2)
            strip_ = {all.front(), all.back()};
        lastMode_ = g3::kTriangleFan;
        return;
    }
    case g3::kTriangleStrip:
    case g3::kTriangleStripContinue: {
        // Every other triangle turned round, so that all keep the strip's
        // winding; a continued strip goes on from the last two vertices
        // with the parity it had.
        std::vector<ClipVertex> all;
        size_t parity = 0;
        if (mode == g3::kTriangleStripContinue && lastMode_ == g3::kTriangleStrip && strip_.size() == 2) {
            all = strip_;
            parity = stripParity_;
        }
        all.insert(all.end(), v.begin(), v.end());
        for (size_t i = 0; i + 2 < all.size(); ++i) {
            if ((i + parity) & 1)
                clipTriangle(all[i + 1], all[i], all[i + 2], false);
            else
                clipTriangle(all[i], all[i + 1], all[i + 2], false);
        }
        if (all.size() >= 2) {
            stripParity_ = (parity + all.size() - 2) & 1;
            strip_ = {all[all.size() - 2], all.back()};
        }
        lastMode_ = g3::kTriangleStrip;
        return;
    }
    default: notImplemented("grDrawVertexArray mode " + std::to_string(mode), "an unknown mode"); break;
    }
    lastMode_ = 0xffffffffu;
}

// --- State ------------------------------------------------------------------------------

void Glide3::fogMode(int mode) {
    const int extra = mode & (kFogMult2 | kFogAdd2);
    switch (mode & 0xff) {
    case 0: core_.fogMode(kFogDisable); return;
    case g3::kFogWithTableOnQ: core_.fogMode(kFogWithTable | extra); return;
    case g3::kFogWithIteratedZ: core_.fogMode(kFogWithIteratedZ | extra); return;
    case g3::kFogWithIteratedAlphaExt: core_.fogMode(kFogWithIteratedAlpha | extra); return;
    case g3::kFogWithTableOnFogCoordExt:
        notImplemented("grFogMode(GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT)", "the extension isn't offered: the table on q");
        core_.fogMode(kFogWithTable | extra);
        return;
    default: notImplemented("grFogMode " + std::to_string(mode), "an unknown mode"); return;
    }
}

void Glide3::fogTable(const uint8_t* table) {
    const int n = std::max(board_.fogTableEntries, 1);
    if (n == 64) {
        core_.fogTable(table);
        return;
    }
    // A table of another size: entry i of the core's 64 from the board's
    // entry at the same place along it (Reluva's choice; the Voodoo2 and
    // Voodoo3 have 64).
    uint8_t t[64];
    for (int i = 0; i < 64; ++i)
        t[i] = table[std::min(n - 1, i * n / 64)];
    core_.fogTable(t);
}

void Glide3::gammaCorrectionRgb(float red, float green, float blue) {
    auto ok = [](float g) { return g > 0 && g <= 20; };
    if (ok(red) && ok(green) && ok(blue)) {
        pipeline().state().gamma = {red, green, blue};
        pipeline().setGammaValue();
    }
}

void Glide3::texMipMapMode(int tmu, int mode, bool lodBlend) {
    if (tmu < 0 || tmu >= kMaxTmus)
        return;
    mipmapAsked_[size_t(tmu)] = mode;
    lodBlend_[size_t(tmu)] = lodBlend;
    // GR_MIPMAP_NEAREST_DITHER only once grEnable(GR_ALLOW_MIPMAP_DITHER)
    // allows it (Reference Manual, grTexMipMapMode): else nearest.
    if (mode == kMipmapNearestDither && !enabled(g3::kAllowMipmapDither))
        mode = kMipmapNearest;
    core_.texMipMapMode(tmu, mode, lodBlend);
}

// --- Textures -------------------------------------------------------------------------

Glide2TexInfo Glide3::toGlide2(const Glide3TexInfo& info) {
    // Glide 2's numbers count down from GR_LOD_256 and GR_ASPECT_8x1.
    Glide2TexInfo t;
    t.smallLod = 8 - std::clamp(info.smallLodLog2, 0, 8);
    t.largeLod = 8 - std::clamp(info.largeLodLog2, 0, 8);
    t.aspectRatio = 3 - std::clamp(info.aspectRatioLog2, -3, 3);
    t.format = info.format;
    return t;
}

uint32_t Glide3::texCalcMemRequired(int smallLodLog2, int largeLodLog2, int aspectLog2, int format) const {
    const Glide2TexInfo t = toGlide2({smallLodLog2, largeLodLog2, aspectLog2, format});
    return const_cast<Glide2&>(core_).texCalcMemRequired(t.smallLod, t.largeLod, t.aspectRatio, t.format);
}

uint32_t Glide3::texTextureMemRequired(int evenOdd, const Glide3TexInfo& info) const {
    return const_cast<Glide2&>(core_).texTextureMemRequired(evenOdd, toGlide2(info));
}

uint32_t Glide3::mipmapDataBytes(const Glide3TexInfo& info) { return Glide2::mipmapDataBytes(toGlide2(info)); }

uint32_t Glide3::levelDataBytes(int lodLog2, int aspectLog2, int format) {
    return Glide2::levelDataBytes(8 - std::clamp(lodLog2, 0, 8), 3 - std::clamp(aspectLog2, -3, 3), format);
}

void Glide3::texSource(int tmu, uint32_t start, int evenOdd, const Glide3TexInfo& info) {
    core_.texSource(tmu, start, evenOdd, toGlide2(info));
}

void Glide3::texDownloadMipMap(int tmu, uint32_t start, int evenOdd, const Glide3TexInfo& info, const uint8_t* data) {
    core_.texDownloadMipMap(tmu, start, evenOdd, toGlide2(info), data);
}

void Glide3::texDownloadMipMapLevelPartial(int tmu, uint32_t start, int thisLodLog2, int largeLodLog2, int aspectLog2,
                                           int format, int evenOdd, const uint8_t* data, int firstRow, int lastRow) {
    core_.texDownloadMipMapLevelPartial(tmu, start, 8 - std::clamp(thisLodLog2, 0, 8),
                                        8 - std::clamp(largeLodLog2, 0, 8), 3 - std::clamp(aspectLog2, -3, 3),
                                        format, evenOdd, data, firstRow, lastRow);
}

void Glide3::texDownloadTablePartial(int type, const uint8_t* data, int first, int last) {
    // GR_TEXTABLE_PALETTE_6666_EXT too, where the board has it (the core
    // says if not).
    for (int i = 0; i < board_.tmus; ++i)
        core_.texDownloadTablePartial(i, type, data, first, last);
}

void Glide3::texNCCTable(int table) {
    for (int i = 0; i < kMaxTmus; ++i)
        core_.texNCCTable(i, table);
}

void Glide3::texMultibaseAddress(int tmu, int range, uint32_t start) {
    // Glide 3 numbers the ranges from the small end.
    static constexpr int kGlide2Range[4] = {3, 2, 1, 0};
    if (range >= 0 && range < 4)
        core_.texMultibaseAddress(tmu, kGlide2Range[range], start, kLevelsBoth, {});
}

uint32_t Glide3::glideGetState() {
    const uint32_t token = core_.glideGetState();
    saved_[token] = {layout_, enabled_};
    while (saved_.size() > 64)
        saved_.erase(saved_.begin());
    return token ^ kStateMagic;
}

void Glide3::glideSetState(uint32_t token) {
    const uint32_t t = token ^ kStateMagic;
    auto it = saved_.find(t);
    if (it == saved_.end()) {
        notImplemented("grGlideSetState", "a state buffer Reluva didn't fill: ignored");
        return;
    }
    core_.glideSetState(t);
    layout_ = it->second.layout;
    enabled_ = it->second.enabled;
}

} // namespace reluva::glide
