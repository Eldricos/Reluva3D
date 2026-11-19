// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/host/thrash_native.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::nfs {

namespace {

uint32_t from1555(uint16_t v) {
    const uint32_t r = (v >> 10) & 31;
    const uint32_t g = (v >> 5) & 31;
    const uint32_t b = v & 31;
    return ((v & 0x8000) ? 0xff000000u : 0u) | (r * 255 / 31) << 16 | (g * 255 / 31) << 8 | (b * 255 / 31);
}

uint32_t from4444(uint16_t v) {
    const uint32_t a = (v >> 12) & 15;
    const uint32_t r = (v >> 8) & 15;
    const uint32_t g = (v >> 4) & 15;
    const uint32_t b = v & 15;
    return (a * 17) << 24 | (r * 17) << 16 | (g * 17) << 8 | (b * 17);
}

uint32_t from565(uint16_t v) {
    const uint32_t r = (v >> 11) & 31;
    const uint32_t g = (v >> 5) & 63;
    const uint32_t b = v & 31;
    return 0xff000000u | (r * 255 / 31) << 16 | (g * 255 / 63) << 8 | (b * 255 / 31);
}

uint16_t to565(uint32_t r, uint32_t g, uint32_t b) {
    return uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

// Glide's 4x4 ordered dither into 5:6:5: each channel scaled to its bits
// plus a threshold from the Bayer matrix at the pixel, (2k + 1) / 32 of a
// step, then truncated, so a region keeps its colour on average where
// plain truncation loses up to a step (8 of 255 in red and blue). The
// hardware's own rounding isn't known here; this is the matrix's.
uint16_t to565Dithered(uint32_t r, uint32_t g, uint32_t b, int x, int y) {
    static constexpr uint32_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    const uint32_t t = (2 * kBayer[(y & 3) * 4 + (x & 3)] + 1) * 255;
    const uint32_t r5 = std::min(31u, (r * 31 * 32 + t) / (255 * 32));
    const uint32_t g6 = std::min(63u, (g * 63 * 32 + t) / (255 * 32));
    const uint32_t b5 = std::min(31u, (b * 31 * 32 + t) / (255 * 32));
    return uint16_t(r5 << 11 | g6 << 5 | b5);
}

// A texel's coordinate wrapped (repeat) or clamped to the edge.
int texelAt(int i, int size, bool wrap) {
    if (!wrap)
        return std::clamp(i, 0, size - 1);
    if ((size & (size - 1)) == 0)
        return i & (size - 1);
    return ((i % size) + size) % size;
}

} // namespace

uint32_t NativeThrash::samplePoint(const Texture &t, float fu, float fv, bool wrap) {
    int tx = int(fu);
    int ty = int(fv);
    tx -= fu < float(tx); // floor
    ty -= fv < float(ty);
    return t.rgba[size_t(texelAt(ty, t.height, wrap)) * size_t(t.width) + size_t(texelAt(tx, t.width, wrap))];
}

// Direct3D's bilinear filter: the four texels around the point, texel
// centres at half-integers, each channel weighted by distance (alpha too,
// so a colour-keyed edge fades rather than stepping).
uint32_t NativeThrash::sampleBilinear(const Texture &t, float fu, float fv, bool wrap) {
    const float su = fu - 0.5f;
    const float sv = fv - 0.5f;
    const float flu = std::floor(su);
    const float flv = std::floor(sv);
    const float ax = su - flu;
    const float ay = sv - flv;
    const int x0 = int(flu);
    const int y0 = int(flv);
    const int xa = texelAt(x0, t.width, wrap);
    const int xb = texelAt(x0 + 1, t.width, wrap);
    const size_t ra = size_t(texelAt(y0, t.height, wrap)) * size_t(t.width);
    const size_t rb = size_t(texelAt(y0 + 1, t.height, wrap)) * size_t(t.width);
    const uint32_t c00 = t.rgba[ra + size_t(xa)];
    const uint32_t c10 = t.rgba[ra + size_t(xb)];
    const uint32_t c01 = t.rgba[rb + size_t(xa)];
    const uint32_t c11 = t.rgba[rb + size_t(xb)];
    if (c00 == c10 && c00 == c01 && c00 == c11)
        return c00;
    const float w00 = (1 - ax) * (1 - ay);
    const float w10 = ax * (1 - ay);
    const float w01 = (1 - ax) * ay;
    const float w11 = ax * ay;
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float c = float((c00 >> shift) & 255) * w00 + float((c10 >> shift) & 255) * w10 +
                        float((c01 >> shift) & 255) * w01 + float((c11 >> shift) & 255) * w11;
        out |= uint32_t(std::clamp(int(c + 0.5f), 0, 255)) << shift;
    }
    return out;
}

void NativeThrash::setTarget(uint32_t address, int pitch, int width, int height) {
    _target = address;
    _pitch = pitch;
    _width = width;
    _height = height;
}

void NativeThrash::talloc(uint32_t handle, int width, int height, int format) {
    Texture t;
    t.width = std::max(width, 1);
    t.height = std::max(height, 1);
    t.format = format;
    t.rgba.assign(size_t(t.width) * size_t(t.height), 0xffffffffu);
    _textures[handle] = std::move(t);
    _changed.insert(handle);
    if (handle == _textureHandle)
        _texture = &_textures[handle];
}

uint32_t NativeThrash::tupdate(uint32_t handle, uint32_t pixels, uint32_t palette) {
    auto it = _textures.find(handle);
    if (it == _textures.end())
        return handle;
    Texture &t = it->second;
    const size_t n = t.rgba.size();
    _changed.insert(handle);
    if (t.format == 2) {
        // 8-bit indices into a palette of 256 0x00RRGGBB colours, either
        // given on its own; index 255 is left out.
        if (pixels && _cpu.valid(pixels, uint32_t(n))) {
            const uint8_t *p = _cpu.memory().data() + pixels;
            t.indices.assign(p, p + n);
        }
        if (palette && _cpu.valid(palette, 1024)) {
            const uint8_t *p = _cpu.memory().data() + palette;
            for (int i = 0; i < 256; ++i)
                t.palette[size_t(i)] = 0xff000000u | uint32_t(p[4 * i] | p[4 * i + 1] << 8 | p[4 * i + 2] << 16);
        }
        if (t.indices.size() != n)
            return handle;
        t.transparent = false;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t index = t.indices[i];
            t.rgba[i] = index == 255 ? 0u : t.palette[index];
            t.transparent |= index == 255;
        }
        return handle;
    }
    if (!pixels)
        return t.format == 3 || t.format == 4 ? 0 : handle;
    // The other formats, all 16 bits a texel: 3 1:5:5:5 (the top bit set
    // where opaque), 4 5:6:5 (opaque), 7 4:4:4:4.
    if (!_cpu.valid(pixels, uint32_t(n * 2)))
        return handle;
    const uint8_t *p = _cpu.memory().data() + pixels;
    t.transparent = false;
    for (size_t i = 0; i < n; ++i) {
        const uint16_t v = uint16_t(p[2 * i] | p[2 * i + 1] << 8);
        uint32_t c;
        if (t.format == 3)
            c = from1555(v);
        else if (t.format == 4)
            c = from565(v);
        else if (t.format == 7)
            c = from4444(v);
        else {
            ++_unknownFormats[t.format];
            c = from565(v);
        }
        if ((c >> 24) != 0xff)
            t.transparent = true;
        t.rgba[i] = c;
    }
    return handle;
}

void NativeThrash::setState(uint32_t id, uint32_t value) {
    switch (id) {
    case 0x1: { // the texture: a handle, or 0 for none
        _textureHandle = value;
        auto it = _textures.find(value);
        _texture = it == _textures.end() ? nullptr : &it->second;
        break;
    }
    case 0x2:
        _cull = int(value);
        break;
    case 0x5: // d3da's setstate does nothing for it: its device never dithers
        if (!_direct3D)
            _dither = value != 0;
        break;
    case 0x6:
        _gouraud = value != 0;
        break;
    case 0x7: // only d3da's: 0 point-sampled, 1 bilinear (TEXTUREMAG and MIN), other values ignored
        if (_direct3D && value <= 1)
            _bilinear = value == 1;
        break;
    case 0xd:
        _wrap = value != 0;
        break;
    case 0x68:
        _blend = int(value & 3);
        break;
    case 0xa:
        _alphaBlend = value != 0;
        break;
    // Fog, as voodoo2a.dll has Glide do it: 0xe builds the table from its
    // value and turns table fog on, or (0) off; 0x15 turns it on (modes
    // 1, 2, 4 and 8, all the same) or off (0) with the table it has.
    case 0xe:
        _fogOn = value != 0;
        if (_fogOn) {
            _fogValue = float(value);
            for (int i = 0; i < 64; ++i) {
                const float w = std::pow(2.0f, 3.0f + float(i >> 2)) / float(8 - (i & 3));
                _fogTable[size_t(i)] = std::round(glideFog(w, _fogValue) * 255) / 255;
            }
        }
        break;
    case 0xf:
        _fogColour = value & 0xffffff;
        break;
    case 0x15:
        if (value == 0)
            _fogOn = false;
        else if (value == 1 || value == 2 || value == 4 || value == 8)
            _fogOn = _fogValue != 0;
        break;
    default:
        break;
    }
}

void NativeThrash::setDirect3D(bool on) {
    _direct3D = on;
    // d3da's device set-up (0x60004137-0x600042af): TEXTUREMAG and
    // TEXTUREMIN LINEAR, DITHERENABLE off. voodoo2a.dll's own set-up after
    // setvideomode asks for bilinear too (0x6000208f), so either way the
    // filter starts bilinear unless held point-sampled.
    _bilinear = !_pointSampled;
    _dither = !on;
}

void NativeThrash::setPointSampled(bool on) {
    _pointSampled = on;
    _bilinear = !on;
}

float glideFog(float w, float value) {
    if (value <= 0)
        return 0;
    // Density 1 / value; the table's last entry is for w = 2^18 / 5, and
    // the curve is scaled to reach 1 there.
    const float d = 1.0f / value;
    const float f = (1 - std::exp(-d * w)) / (1 - std::exp(-d * 52428.8f));
    return std::clamp(f, 0.0f, 1.0f);
}

// Glide picks the entries either side of w, w's exponent giving four (at
// 2^k times 1, 8/7, 8/6 and 8/5) and its mantissa where between, and
// interpolates; below entry 0's w (1) it's entry 0, past the last the last.
float NativeThrash::fogAt(float w) const {
    if (!(w > 1))
        return _fogTable[0];
    int e = 0;
    const float s = std::frexp(w, &e) * 2; // w = s * 2^(e - 1), s in [1, 2)
    const int k = e - 1;
    if (k >= 16)
        return _fogTable[63];
    static constexpr float kAt[5] = {1.0f, 8.0f / 7, 8.0f / 6, 8.0f / 5, 2.0f};
    const int j = s < kAt[1] ? 0 : s < kAt[2] ? 1 : s < kAt[3] ? 2 : 3;
    const int i = 4 * k + j;
    const float lo = _fogTable[size_t(i)];
    const float hi = _fogTable[size_t(std::min(i + 1, 63))];
    return lo + (hi - lo) * (s - kAt[j]) / (kAt[j + 1] - kAt[j]);
}

void NativeThrash::clip(int x0, int y0, int x1, int y1) {
    _clipX0 = x0;
    _clipY0 = y0;
    _clipX1 = x1;
    _clipY1 = y1;
}

NativeThrash::Vertex NativeThrash::vertex(uint32_t address) const {
    Vertex v{};
    if (_cpu.valid(address, 32))
        std::memcpy(&v, _cpu.memory().data() + address, 32);
    return v;
}

void NativeThrash::plot(int x, int y, uint32_t argb) {
    put(_cpu.memory().data() + _target + size_t(y) * size_t(_pitch) + size_t(x) * 2, argb, x, y);
}

void NativeThrash::put(uint8_t *p, uint32_t argb, int x, int y) {
    uint32_t r = (argb >> 16) & 255;
    uint32_t g = (argb >> 8) & 255;
    uint32_t b = argb & 255;
    const uint32_t a = argb >> 24;
    // State 0x68's other modes apply while blending is on (state 0xa), to
    // opaque pixels too: an added pixel of alpha 255 is added in full, as
    // the GPU's (source alpha, one) adds it and softtria's additive spans
    // do (thrash.md, Blend modes). Otherwise what's translucent is blended
    // over what's drawn, and what's opaque replaces it.
    const int mode = _alphaBlend ? _blend : 0;
    if (a < 255 || mode != 0) {
        const uint16_t d = uint16_t(p[0] | p[1] << 8);
        const uint32_t dr = ((d >> 11) & 31) * 255 / 31;
        const uint32_t dg = ((d >> 5) & 63) * 255 / 63;
        const uint32_t db = (d & 31) * 255 / 31;
        switch (mode) {
        case 1: // additive
            r = std::min(255u, dr + r * a / 255);
            g = std::min(255u, dg + g * a / 255);
            b = std::min(255u, db + b * a / 255);
            break;
        case 2: // darken by alpha
            r = dr * (255 - a) / 255;
            g = dg * (255 - a) / 255;
            b = db * (255 - a) / 255;
            break;
        case 3: // multiply
            r = dr * r / 255;
            g = dg * g / 255;
            b = db * b / 255;
            break;
        default: // alpha
            r = (r * a + dr * (255 - a)) / 255;
            g = (g * a + dg * (255 - a)) / 255;
            b = (b * a + db * (255 - a)) / 255;
            break;
        }
    }
    const uint16_t v = _dither ? to565Dithered(r, g, b, x, y) : to565(r, g, b);
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    ++_pixels;
}

void NativeThrash::rasterize(const Vertex &a, const Vertex &b, const Vertex &c) {
    if (!_target)
        return;
    // Screen space, y down: the signed area is positive for triangles that
    // turn clockwise on screen.
    const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (area == 0 || !std::isfinite(area))
        return;
    if ((_cull == 1 && area < 0) || (_cull == 2 && area > 0))
        return;
    const int x0 = std::max({int(std::floor(std::min({a.x, b.x, c.x}))), _clipX0, 0});
    const int x1 = std::min({int(std::ceil(std::max({a.x, b.x, c.x}))), _clipX1, _width});
    const int y0 = std::max({int(std::floor(std::min({a.y, b.y, c.y}))), _clipY0, 0});
    const int y1 = std::min({int(std::ceil(std::max({a.y, b.y, c.y}))), _clipY1, _height});
    if (x0 >= x1 || y0 >= y1)
        return;
    if (!_cpu.valid(_target + uint32_t(y0) * uint32_t(_pitch), uint32_t((y1 - y0) * _pitch)))
        return;
    const float inv = 1.0f / area;
    // Barycentric weights l0 (a's), l1 (b's), l2 = 1 - l0 - l1, and how
    // they change a pixel across and down.
    const float l0dx = (b.y - c.y) * inv;
    const float l0dy = (c.x - b.x) * inv;
    const float l1dx = (c.y - a.y) * inv;
    const float l1dy = (a.x - c.x) * inv;
    // Values interpolated across the triangle (linear on screen): 1/w, u/w,
    // v/w, then the colour's channels.
    enum { kW, kU, kV, kR, kG, kB, kA, kValues };
    float at0[kValues];
    float at1[kValues];
    float at2[kValues];
    auto load = [](float *to, const Vertex &v) {
        to[kW] = v.rhw;
        to[kU] = v.u * v.rhw;
        to[kV] = v.v * v.rhw;
        to[kR] = float((v.diffuse >> 16) & 255);
        to[kG] = float((v.diffuse >> 8) & 255);
        to[kB] = float(v.diffuse & 255);
        to[kA] = float(v.diffuse >> 24);
    };
    load(at0, a);
    load(at1, b);
    load(at2, c);
    if (!_gouraud) // flat: the first vertex's colour throughout
        for (int i = kR; i < kValues; ++i)
            at1[i] = at2[i] = at0[i];
    float dx[kValues];
    float dy[kValues];
    float base[kValues];
    const float px0 = float(x0) + 0.5f;
    const float py0 = float(y0) + 0.5f;
    const float l0 = ((b.x - px0) * (c.y - py0) - (c.x - px0) * (b.y - py0)) * inv;
    const float l1 = ((c.x - px0) * (a.y - py0) - (a.x - px0) * (c.y - py0)) * inv;
    for (int i = 0; i < kValues; ++i) {
        const float d0 = at0[i] - at2[i];
        const float d1 = at1[i] - at2[i];
        dx[i] = l0dx * d0 + l1dx * d1;
        dy[i] = l0dy * d0 + l1dy * d1;
        base[i] = at2[i] + l0 * d0 + l1 * d1;
    }
    const Texture *tex = _texture;
    const int tw = tex ? tex->width : 1;
    const int th = tex ? tex->height : 1;
    const float ftw = float(tw);
    const float fth = float(th);
    const bool bilinear = _bilinear;
    const float l2dx = -l0dx - l1dx;
    uint8_t *const memory = _cpu.memory().data();
    for (int y = y0; y < y1; ++y) {
        const float rowOff = float(y - y0);
        const float r0 = l0 + l0dy * rowOff;
        const float r1 = l1 + l1dy * rowOff;
        const float r2 = 1.0f - r0 - r1;
        // The span where all three weights are at least 0: each weight is
        // linear along the row.
        float kmin = 0;
        float kmax = float(x1 - x0 - 1);
        bool empty = false;
        auto bound = [&](float v, float slope) {
            if (slope > 0)
                kmin = std::max(kmin, std::ceil(-v / slope - 1e-4f));
            else if (slope < 0)
                kmax = std::min(kmax, std::floor(v / -slope + 1e-4f));
            else if (v < 0)
                empty = true;
        };
        bound(r0, l0dx);
        bound(r1, l1dx);
        bound(r2, l2dx);
        if (empty || kmin > kmax)
            continue;
        const int xs = x0 + int(kmin);
        const int xe = x0 + int(kmax);
        float v[kValues];
        const float colOff = float(xs - x0);
        for (int i = 0; i < kValues; ++i)
            v[i] = base[i] + dy[i] * rowOff + dx[i] * colOff;
        uint8_t *p = memory + _target + size_t(y) * size_t(_pitch) + size_t(xs) * 2;
        for (int x = xs; x <= xe; ++x, p += 2) {
            float r = v[kR];
            float g = v[kG];
            float bl = v[kB];
            float al = v[kA];
            bool draw = true;
            if (tex) {
                const float w = 1.0f / v[kW];
                const float fu = v[kU] * w * ftw;
                const float fv = v[kV] * w * fth;
                const uint32_t t = bilinear ? sampleBilinear(*tex, fu, fv, _wrap) : samplePoint(*tex, fu, fv, _wrap);
                const uint32_t ta = t >> 24;
                if (ta == 0)
                    draw = false; // left out, as the colour key / alpha test does
                else {
                    r = r * float((t >> 16) & 255) * (1.0f / 255);
                    g = g * float((t >> 8) & 255) * (1.0f / 255);
                    bl = bl * float(t & 255) * (1.0f / 255);
                    al = al * float(ta) * (1.0f / 255);
                }
            }
            if (draw && _fogOn) {
                const float f = fogAt(1.0f / v[kW]);
                r += (float((_fogColour >> 16) & 255) - r) * f;
                g += (float((_fogColour >> 8) & 255) - g) * f;
                bl += (float(_fogColour & 255) - bl) * f;
            }
            if (draw)
                put(p,
                    uint32_t(std::clamp(al, 0.0f, 255.0f)) << 24 | uint32_t(std::clamp(r, 0.0f, 255.0f)) << 16 |
                        uint32_t(std::clamp(g, 0.0f, 255.0f)) << 8 | uint32_t(std::clamp(bl, 0.0f, 255.0f)),
                    x, y);
            for (int i = 0; i < kValues; ++i)
                v[i] += dx[i];
        }
    }
}

ThrashFrame::Batch &NativeThrash::batch() {
    ThrashFrame::Batch b;
    b.texture = _texture ? _textureHandle : 0;
    b.wrap = _wrap;
    b.blend = _alphaBlend;
    b.blendMode = _blend;
    b.cull = _cull;
    b.filter = _direct3D ? int(_bilinear) : -1;
    b.fog = _fogOn ? _fogValue : 0;
    b.fogColour = _fogOn ? _fogColour : 0;
    b.clip[0] = std::max(_clipX0, 0);
    b.clip[1] = std::max(_clipY0, 0);
    b.clip[2] = std::min(_clipX1, _frame.width);
    b.clip[3] = std::min(_clipY1, _frame.height);
    b.first = uint32_t(_frame.vertices.size());
    if (!_frame.batches.empty()) {
        const ThrashFrame::Batch &last = _frame.batches.back();
        if (last.texture == b.texture && last.wrap == b.wrap && last.blend == b.blend &&
            last.blendMode == b.blendMode && last.cull == b.cull && last.filter == b.filter && last.fog == b.fog &&
            last.fogColour == b.fogColour && std::equal(b.clip, b.clip + 4, last.clip))
            return _frame.batches.back();
    }
    _frame.batches.push_back(b);
    return _frame.batches.back();
}

void NativeThrash::record(const Vertex &a, const Vertex &b, const Vertex &c) {
    ThrashFrame::Batch &to = batch();
    // Flat shading: the first vertex's colour throughout.
    const uint32_t flat = a.diffuse;
    for (const Vertex *v : {&a, &b, &c})
        _frame.vertices.push_back({v->x, v->y, v->z, v->rhw, _gouraud ? v->diffuse : flat, v->u, v->v});
    to.count += 3;
}

void NativeThrash::fill(int x0, int y0, int x1, int y1, uint32_t argb) {
    const Texture *texture = _texture;
    const bool blend = _alphaBlend;
    const int cull = _cull;
    const bool fog = _fogOn;
    _texture = nullptr;
    _alphaBlend = false;
    _cull = 0;
    _fogOn = false;
    const auto corner = [&](int x, int y) { return Vertex{float(x), float(y), 0, 1, argb | 0xff000000u, 0, 0, 0}; };
    record(corner(x0, y0), corner(x1, y0), corner(x1, y1));
    record(corner(x0, y0), corner(x1, y1), corner(x0, y1));
    _texture = texture;
    _alphaBlend = blend;
    _cull = cull;
    _fogOn = fog;
}

ThrashFrame NativeThrash::takeFrame() {
    ThrashFrame out = std::move(_frame);
    _frame = ThrashFrame{};
    _frame.width = out.width;
    _frame.height = out.height;
    out.resetTextures = _resetTextures;
    _resetTextures = false;
    for (uint32_t handle : _changed)
        if (auto it = _textures.find(handle); it != _textures.end())
            out.textures.push_back({handle, it->second.width, it->second.height, it->second.rgba});
    _changed.clear();
    return out;
}

void NativeThrash::triangle(uint32_t a, uint32_t b, uint32_t c) {
    if (_recording)
        record(vertex(a), vertex(b), vertex(c));
    if (!_recording || _alsoRasterize)
        rasterize(vertex(a), vertex(b), vertex(c));
}

void NativeThrash::quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const Vertex va = vertex(a);
    const Vertex vb = vertex(b);
    const Vertex vc = vertex(c);
    const Vertex vd = vertex(d);
    if (_recording) {
        record(va, vb, vc);
        record(va, vc, vd);
        if (!_alsoRasterize)
            return;
    }
    rasterize(va, vb, vc);
    rasterize(va, vc, vd);
}

void NativeThrash::line(uint32_t a, uint32_t b) {
    const Vertex va = vertex(a);
    const Vertex vb = vertex(b);
    if (_recording) {
        // A quad a pixel wide, across the line, in its first vertex's colour.
        const float dx = vb.x - va.x;
        const float dy = vb.y - va.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0)
            return;
        const float nx = -dy / length * 0.5f;
        const float ny = dx / length * 0.5f;
        const Texture *texture = _texture;
        _texture = nullptr;
        auto at = [&](const Vertex &v, float s) {
            Vertex o = v;
            o.x += nx * s;
            o.y += ny * s;
            o.diffuse = va.diffuse | 0xff000000u;
            return o;
        };
        const int cull = _cull;
        _cull = 0;
        record(at(va, 1), at(vb, 1), at(vb, -1));
        record(at(va, 1), at(vb, -1), at(va, -1));
        _cull = cull;
        _texture = texture;
        if (!_alsoRasterize)
            return;
    }
    if (!_target)
        return;
    const float dx = vb.x - va.x;
    const float dy = vb.y - va.y;
    const int steps = int(std::max(std::fabs(dx), std::fabs(dy))) + 1;
    if (steps > 4096)
        return;
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const int x = int(va.x + dx * t);
        const int y = int(va.y + dy * t);
        if (x >= std::max(_clipX0, 0) && x < std::min(_clipX1, _width) && y >= std::max(_clipY0, 0) &&
            y < std::min(_clipY1, _height))
            plot(x, y, va.diffuse | 0xff000000u);
    }
}

void NativeThrash::point(uint32_t a) {
    const Vertex v = vertex(a);
    if (_recording) {
        const int x = int(v.x);
        const int y = int(v.y);
        fill(x, y, x + 1, y + 1, v.diffuse);
        if (!_alsoRasterize)
            return;
    }
    if (!_target)
        return;
    const int x = int(v.x);
    const int y = int(v.y);
    if (x >= std::max(_clipX0, 0) && x < std::min(_clipX1, _width) && y >= std::max(_clipY0, 0) &&
        y < std::min(_clipY1, _height))
        plot(x, y, v.diffuse | 0xff000000u);
}

} // namespace reluva::nfs
