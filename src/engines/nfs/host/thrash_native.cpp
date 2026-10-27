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
    changed_.insert(handle);
    if (handle == textureHandle_)
        texture_ = &textures_[handle];
}

uint32_t NativeThrash::tupdate(uint32_t handle, uint32_t pixels, uint32_t palette) {
    auto it = textures_.find(handle);
    if (it == textures_.end())
        return handle;
    Texture& t = it->second;
    const size_t n = t.rgba.size();
    changed_.insert(handle);
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
    if (!cpu_.valid(pixels, uint32_t(n * 2)))
        return handle;
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
    return handle;
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
    case 0xa: alphaBlend_ = value != 0; break;
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
    put(cpu_.memory().data() + target_ + size_t(y) * size_t(pitch_) + size_t(x) * 2, argb);
}

void NativeThrash::put(uint8_t* p, uint32_t argb) {
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
    if (!cpu_.valid(target_ + uint32_t(y0) * uint32_t(pitch_), uint32_t((y1 - y0) * pitch_)))
        return;
    const float inv = 1.0f / area;
    // Barycentric weights l0 (a's), l1 (b's), l2 = 1 - l0 - l1, and how
    // they change a pixel across and down.
    const float l0dx = (b.y - c.y) * inv, l0dy = (c.x - b.x) * inv;
    const float l1dx = (c.y - a.y) * inv, l1dy = (a.x - c.x) * inv;
    // Values interpolated across the triangle (linear on screen): 1/w, u/w,
    // v/w, then the colour's channels.
    enum { W, U, V, R, G, B, A, kValues };
    float at0[kValues], at1[kValues], at2[kValues];
    auto load = [](float* to, const Vertex& v) {
        to[W] = v.rhw;
        to[U] = v.u * v.rhw;
        to[V] = v.v * v.rhw;
        to[R] = float((v.diffuse >> 16) & 255);
        to[G] = float((v.diffuse >> 8) & 255);
        to[B] = float(v.diffuse & 255);
        to[A] = float(v.diffuse >> 24);
    };
    load(at0, a);
    load(at1, b);
    load(at2, c);
    if (!gouraud_) // flat: the first vertex's colour throughout
        for (int i = R; i < kValues; ++i)
            at1[i] = at2[i] = at0[i];
    float dx[kValues], dy[kValues], base[kValues];
    const float px0 = float(x0) + 0.5f, py0 = float(y0) + 0.5f;
    const float l0 = ((b.x - px0) * (c.y - py0) - (c.x - px0) * (b.y - py0)) * inv;
    const float l1 = ((c.x - px0) * (a.y - py0) - (a.x - px0) * (c.y - py0)) * inv;
    for (int i = 0; i < kValues; ++i) {
        const float d0 = at0[i] - at2[i], d1 = at1[i] - at2[i];
        dx[i] = l0dx * d0 + l1dx * d1;
        dy[i] = l0dy * d0 + l1dy * d1;
        base[i] = at2[i] + l0 * d0 + l1 * d1;
    }
    const Texture* tex = texture_;
    const int tw = tex ? tex->width : 1, th = tex ? tex->height : 1;
    const bool pow2 = (tw & (tw - 1)) == 0 && (th & (th - 1)) == 0;
    const float ftw = float(tw), fth = float(th);
    const float l2dx = -l0dx - l1dx;
    uint8_t* const memory = cpu_.memory().data();
    for (int y = y0; y < y1; ++y) {
        const float rowOff = float(y - y0);
        const float r0 = l0 + l0dy * rowOff, r1 = l1 + l1dy * rowOff, r2 = 1.0f - r0 - r1;
        // The span where all three weights are at least 0: each weight is
        // linear along the row.
        float kmin = 0, kmax = float(x1 - x0 - 1);
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
        const int xs = x0 + int(kmin), xe = x0 + int(kmax);
        float v[kValues];
        const float colOff = float(xs - x0);
        for (int i = 0; i < kValues; ++i)
            v[i] = base[i] + dy[i] * rowOff + dx[i] * colOff;
        uint8_t* p = memory + target_ + size_t(y) * size_t(pitch_) + size_t(xs) * 2;
        for (int x = xs; x <= xe; ++x, p += 2) {
            float r = v[R], g = v[G], bl = v[B], al = v[A];
            bool draw = true;
            if (tex) {
                const float w = 1.0f / v[W];
                const float fu = v[U] * w * ftw, fv = v[V] * w * fth;
                int tx = int(fu), ty = int(fv);
                tx -= fu < float(tx); // floor
                ty -= fv < float(ty);
                if (wrap_) {
                    if (pow2) {
                        tx &= tw - 1;
                        ty &= th - 1;
                    } else {
                        tx = ((tx % tw) + tw) % tw;
                        ty = ((ty % th) + th) % th;
                    }
                } else {
                    tx = std::clamp(tx, 0, tw - 1);
                    ty = std::clamp(ty, 0, th - 1);
                }
                const uint32_t t = tex->rgba[size_t(ty) * size_t(tw) + size_t(tx)];
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
            if (draw)
                put(p, uint32_t(std::clamp(al, 0.0f, 255.0f)) << 24 | uint32_t(std::clamp(r, 0.0f, 255.0f)) << 16 |
                           uint32_t(std::clamp(g, 0.0f, 255.0f)) << 8 | uint32_t(std::clamp(bl, 0.0f, 255.0f)));
            for (int i = 0; i < kValues; ++i)
                v[i] += dx[i];
        }
    }
}

ThrashFrame::Batch& NativeThrash::batch() {
    ThrashFrame::Batch b;
    b.texture = texture_ ? textureHandle_ : 0;
    b.wrap = wrap_;
    b.blend = alphaBlend_;
    b.blendMode = blend_;
    b.cull = cull_;
    b.clip[0] = std::max(clipX0_, 0);
    b.clip[1] = std::max(clipY0_, 0);
    b.clip[2] = std::min(clipX1_, frame_.width);
    b.clip[3] = std::min(clipY1_, frame_.height);
    b.first = uint32_t(frame_.vertices.size());
    if (!frame_.batches.empty()) {
        const ThrashFrame::Batch& last = frame_.batches.back();
        if (last.texture == b.texture && last.wrap == b.wrap && last.blend == b.blend &&
            last.blendMode == b.blendMode && last.cull == b.cull && std::equal(b.clip, b.clip + 4, last.clip))
            return frame_.batches.back();
    }
    frame_.batches.push_back(b);
    return frame_.batches.back();
}

void NativeThrash::record(const Vertex& a, const Vertex& b, const Vertex& c) {
    ThrashFrame::Batch& to = batch();
    // Flat shading: the first vertex's colour throughout.
    const uint32_t flat = a.diffuse;
    for (const Vertex* v : {&a, &b, &c})
        frame_.vertices.push_back({v->x, v->y, v->z, v->rhw, gouraud_ ? v->diffuse : flat, v->u, v->v});
    to.count += 3;
}

void NativeThrash::fill(int x0, int y0, int x1, int y1, uint32_t argb) {
    const Texture* texture = texture_;
    const bool blend = alphaBlend_;
    const int cull = cull_;
    texture_ = nullptr;
    alphaBlend_ = false;
    cull_ = 0;
    const auto corner = [&](int x, int y) { return Vertex{float(x), float(y), 0, 1, argb | 0xff000000u, 0, 0, 0}; };
    record(corner(x0, y0), corner(x1, y0), corner(x1, y1));
    record(corner(x0, y0), corner(x1, y1), corner(x0, y1));
    texture_ = texture;
    alphaBlend_ = blend;
    cull_ = cull;
}

ThrashFrame NativeThrash::takeFrame() {
    ThrashFrame out = std::move(frame_);
    frame_ = ThrashFrame{};
    frame_.width = out.width;
    frame_.height = out.height;
    out.resetTextures = resetTextures_;
    resetTextures_ = false;
    for (uint32_t handle : changed_)
        if (auto it = textures_.find(handle); it != textures_.end())
            out.textures.push_back({handle, it->second.width, it->second.height, it->second.rgba});
    changed_.clear();
    return out;
}

void NativeThrash::triangle(uint32_t a, uint32_t b, uint32_t c) {
    if (recording_)
        record(vertex(a), vertex(b), vertex(c));
    else
        rasterize(vertex(a), vertex(b), vertex(c));
}

void NativeThrash::quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const Vertex va = vertex(a), vb = vertex(b), vc = vertex(c), vd = vertex(d);
    if (recording_) {
        record(va, vb, vc);
        record(va, vc, vd);
        return;
    }
    rasterize(va, vb, vc);
    rasterize(va, vc, vd);
}

void NativeThrash::line(uint32_t a, uint32_t b) {
    const Vertex va = vertex(a), vb = vertex(b);
    if (recording_) {
        // A quad a pixel wide, across the line, in its first vertex's colour.
        const float dx = vb.x - va.x, dy = vb.y - va.y, length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0)
            return;
        const float nx = -dy / length * 0.5f, ny = dx / length * 0.5f;
        const Texture* texture = texture_;
        texture_ = nullptr;
        auto at = [&](const Vertex& v, float s) {
            Vertex o = v;
            o.x += nx * s;
            o.y += ny * s;
            o.diffuse = va.diffuse | 0xff000000u;
            return o;
        };
        const int cull = cull_;
        cull_ = 0;
        record(at(va, 1), at(vb, 1), at(vb, -1));
        record(at(va, 1), at(vb, -1), at(va, -1));
        cull_ = cull;
        texture_ = texture;
        return;
    }
    if (!target_)
        return;
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
    const Vertex v = vertex(a);
    if (recording_) {
        const int x = int(v.x), y = int(v.y);
        fill(x, y, x + 1, y + 1, v.diffuse);
        return;
    }
    if (!target_)
        return;
    const int x = int(v.x), y = int(v.y);
    if (x >= std::max(clipX0_, 0) && x < std::min(clipX1_, width_) && y >= std::max(clipY0_, 0) &&
        y < std::min(clipY1_, height_))
        plot(x, y, v.diffuse | 0xff000000u);
}

} // namespace reluva::nfs
