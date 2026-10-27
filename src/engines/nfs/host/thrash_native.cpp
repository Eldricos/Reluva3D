// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/host/thrash_native.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::nfs {

namespace {

uint32_t from1555(uint16_t v) {
    const uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
    return ((v & 0x8000) ? 0xff000000u : 0u) | (r * 255 / 31) << 16 | (g * 255 / 31) << 8 | (b * 255 / 31);
}

uint32_t from4444(uint16_t v) {
    const uint32_t a = (v >> 12) & 15, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
    return (a * 17) << 24 | (r * 17) << 16 | (g * 17) << 8 | (b * 17);
}

uint32_t from565(uint16_t v) {
    const uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    return 0xff000000u | (r * 255 / 31) << 16 | (g * 255 / 63) << 8 | (b * 255 / 31);
}

uint16_t to565(uint32_t r, uint32_t g, uint32_t b) { return uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }

} // namespace

void NativeThrash::setTarget(uint32_t address, int pitch, int width, int height) {
    target_ = address;
    pitch_ = pitch;
    width_ = width;
    height_ = height;
}

void NativeThrash::talloc(uint32_t handle, int width, int height, int format) {
    Texture t;
    t.width = std::max(width, 1);
    t.height = std::max(height, 1);
    t.format = format;
    t.rgba.assign(size_t(t.width) * size_t(t.height), 0xffffffffu);
    textures_[handle] = std::move(t);
    if (handle == textureHandle_)
        texture_ = &textures_[handle];
}

void NativeThrash::tupdate(uint32_t handle, uint32_t pixels, uint32_t palette) {
    auto it = textures_.find(handle);
    if (it == textures_.end())
        return;
    Texture& t = it->second;
    const size_t n = t.rgba.size();
    if (t.format == 2) {
        // 8-bit indices into a palette of 256 0x00RRGGBB colours, either
        // given on its own; index 255 is left out.
        if (pixels && cpu_.valid(pixels, uint32_t(n))) {
            const uint8_t* p = cpu_.memory().data() + pixels;
            t.indices.assign(p, p + n);
        }
        if (palette && cpu_.valid(palette, 1024)) {
            const uint8_t* p = cpu_.memory().data() + palette;
            for (int i = 0; i < 256; ++i)
                t.palette[size_t(i)] = 0xff000000u | uint32_t(p[4 * i] | p[4 * i + 1] << 8 | p[4 * i + 2] << 16);
        }
        if (t.indices.size() != n)
            return;
        t.transparent = false;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t index = t.indices[i];
            t.rgba[i] = index == 255 ? 0u : t.palette[index];
            t.transparent |= index == 255;
        }
        return;
    }
    if (!pixels)
        return;
    // The other formats, all 16 bits a texel: 3 1:5:5:5 (the top bit set
    // where opaque), 4 5:6:5 (opaque), 7 4:4:4:4.
    if (!cpu_.valid(pixels, uint32_t(n * 2)))
        return;
    const uint8_t* p = cpu_.memory().data() + pixels;
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
            ++unknownFormats_[t.format];
            c = from565(v);
        }
        if ((c >> 24) != 0xff)
            t.transparent = true;
        t.rgba[i] = c;
    }
}

void NativeThrash::setState(uint32_t id, uint32_t value) {
    switch (id) {
    case 0x1: { // the texture: a handle, or 0 for none
        textureHandle_ = value;
        auto it = textures_.find(value);
        texture_ = it == textures_.end() ? nullptr : &it->second;
        break;
    }
    case 0x2: cull_ = int(value); break;
    case 0x6: gouraud_ = value != 0; break;
    case 0xd: wrap_ = value != 0; break;
    case 0x68: blend_ = int(value & 3); break;
    default: break;
    }
}

void NativeThrash::clip(int x0, int y0, int x1, int y1) {
    clipX0_ = x0;
    clipY0_ = y0;
    clipX1_ = x1;
    clipY1_ = y1;
}

NativeThrash::Vertex NativeThrash::vertex(uint32_t address) const {
    Vertex v{};
    if (cpu_.valid(address, 32))
        std::memcpy(&v, cpu_.memory().data() + address, 32);
    return v;
}

void NativeThrash::plot(int x, int y, uint32_t argb) {
    uint8_t* p = cpu_.memory().data() + target_ + size_t(y) * size_t(pitch_) + size_t(x) * 2;
    uint32_t r = (argb >> 16) & 255, g = (argb >> 8) & 255, b = argb & 255;
    const uint32_t a = argb >> 24;
    if (a < 255) {
        const uint16_t d = uint16_t(p[0] | p[1] << 8);
        const uint32_t dr = ((d >> 11) & 31) * 255 / 31, dg = ((d >> 5) & 63) * 255 / 63, db = (d & 31) * 255 / 31;
        switch (blend_) {
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
    const uint16_t v = to565(r, g, b);
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    ++pixels_;
}

void NativeThrash::rasterize(const Vertex& a, const Vertex& b, const Vertex& c) {
    if (!target_)
        return;
    // Screen space, y down: the signed area is positive for triangles that
    // turn clockwise on screen.
    const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (area == 0 || !std::isfinite(area))
        return;
    if ((cull_ == 1 && area < 0) || (cull_ == 2 && area > 0))
        return;
    const int x0 = std::max({int(std::floor(std::min({a.x, b.x, c.x}))), clipX0_, 0});
    const int x1 = std::min({int(std::ceil(std::max({a.x, b.x, c.x}))), clipX1_, width_});
    const int y0 = std::max({int(std::floor(std::min({a.y, b.y, c.y}))), clipY0_, 0});
    const int y1 = std::min({int(std::ceil(std::max({a.y, b.y, c.y}))), clipY1_, height_});
    if (x0 >= x1 || y0 >= y1)
        return;
    const float inv = 1.0f / area;
    const Texture* tex = texture_;
    // Per-vertex values interpolated across: 1/w, u/w, v/w, and colour.
    auto channel = [](uint32_t c, int shift) { return float((c >> shift) & 255); };
    const float wa = a.rhw, wb = b.rhw, wc = c.rhw;
    for (int y = y0; y < y1; ++y) {
        const float py = float(y) + 0.5f;
        for (int x = x0; x < x1; ++x) {
            const float px = float(x) + 0.5f;
            // Barycentric weights; pixels on an edge go to one side only.
            float l0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) * inv;
            float l1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) * inv;
            float l2 = 1.0f - l0 - l1;
            if (l0 < 0 || l1 < 0 || l2 < 0)
                continue;
            uint32_t colour;
            const uint32_t d0 = a.diffuse;
            float r, g, bl, al;
            if (gouraud_) {
                r = l0 * channel(a.diffuse, 16) + l1 * channel(b.diffuse, 16) + l2 * channel(c.diffuse, 16);
                g = l0 * channel(a.diffuse, 8) + l1 * channel(b.diffuse, 8) + l2 * channel(c.diffuse, 8);
                bl = l0 * channel(a.diffuse, 0) + l1 * channel(b.diffuse, 0) + l2 * channel(c.diffuse, 0);
                al = l0 * channel(a.diffuse, 24) + l1 * channel(b.diffuse, 24) + l2 * channel(c.diffuse, 24);
            } else {
                r = channel(d0, 16);
                g = channel(d0, 8);
                bl = channel(d0, 0);
                al = channel(d0, 24);
            }
            if (tex) {
                const float w = l0 * wa + l1 * wb + l2 * wc;
                float u = (l0 * a.u * wa + l1 * b.u * wb + l2 * c.u * wc) / w;
                float v = (l0 * a.v * wa + l1 * b.v * wb + l2 * c.v * wc) / w;
                int tx = int(std::floor(u * float(tex->width)));
                int ty = int(std::floor(v * float(tex->height)));
                if (wrap_) {
                    tx = ((tx % tex->width) + tex->width) % tex->width;
                    ty = ((ty % tex->height) + tex->height) % tex->height;
                } else {
                    tx = std::clamp(tx, 0, tex->width - 1);
                    ty = std::clamp(ty, 0, tex->height - 1);
                }
                const uint32_t t = tex->rgba[size_t(ty) * size_t(tex->width) + size_t(tx)];
                const uint32_t ta = t >> 24;
                if (ta == 0)
                    continue; // left out, as the colour key / alpha test does
                r = r * channel(t, 16) / 255;
                g = g * channel(t, 8) / 255;
                bl = bl * channel(t, 0) / 255;
                al = al * float(ta) / 255;
            }
            colour = uint32_t(std::clamp(al, 0.0f, 255.0f)) << 24 | uint32_t(std::clamp(r, 0.0f, 255.0f)) << 16 |
                     uint32_t(std::clamp(g, 0.0f, 255.0f)) << 8 | uint32_t(std::clamp(bl, 0.0f, 255.0f));
            plot(x, y, colour);
        }
    }
}

void NativeThrash::triangle(uint32_t a, uint32_t b, uint32_t c) { rasterize(vertex(a), vertex(b), vertex(c)); }

void NativeThrash::quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const Vertex va = vertex(a), vb = vertex(b), vc = vertex(c), vd = vertex(d);
    rasterize(va, vb, vc);
    rasterize(va, vc, vd);
}

void NativeThrash::line(uint32_t a, uint32_t b) {
    if (!target_)
        return;
    const Vertex va = vertex(a), vb = vertex(b);
    const float dx = vb.x - va.x, dy = vb.y - va.y;
    const int steps = int(std::max(std::fabs(dx), std::fabs(dy))) + 1;
    if (steps > 4096)
        return;
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const int x = int(va.x + dx * t), y = int(va.y + dy * t);
        if (x >= std::max(clipX0_, 0) && x < std::min(clipX1_, width_) && y >= std::max(clipY0_, 0) &&
            y < std::min(clipY1_, height_))
            plot(x, y, va.diffuse | 0xff000000u);
    }
}

void NativeThrash::point(uint32_t a) {
    if (!target_)
        return;
    const Vertex v = vertex(a);
    const int x = int(v.x), y = int(v.y);
    if (x >= std::max(clipX0_, 0) && x < std::min(clipX1_, width_) && y >= std::max(clipY0_, 0) &&
        y < std::min(clipY1_, height_))
        plot(x, y, v.diffuse | 0xff000000u);
}

} // namespace reluva::nfs
