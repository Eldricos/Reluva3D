// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectDraw's surfaces, palettes and clippers, as the DirectX 7
// documentation describes them. A surface's pixels are in the process's
// memory, rows `pitch` bytes apart, in its pixel format.

#include "common/x86/directx/internal.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace reluva::x86::dx {

namespace {

// DDSURFACEDESC(2) fields.
enum : uint32_t {
    kSdCaps = 0x1, kSdHeight = 0x2, kSdWidth = 0x4, kSdPitch = 0x8, kSdBackBufferCount = 0x20,
    kSdLpSurface = 0x800, kSdPixelFormat = 0x1000, kSdCkSrcBlt = 0x10000, kSdMipMapCount = 0x20000
};
// Blt flags.
enum : uint32_t {
    kBltColorFill = 0x400, kBltDepthFill = 0x02000000, kBltKeySrc = 0x8000, kBltKeySrcOverride = 0x10000
};
constexpr uint32_t kCkeySrcBlt = 0x8, kCkeyDestBlt = 0x2;

int shiftOf(uint32_t mask) {
    int s = 0;
    while (mask && !(mask & 1)) {
        mask >>= 1;
        ++s;
    }
    return s;
}
int widthOf(uint32_t mask) {
    int n = 0;
    for (mask >>= shiftOf(mask); mask & 1; mask >>= 1)
        ++n;
    return n;
}
// A channel's bits scaled to 0..255.
uint32_t channel(uint32_t value, uint32_t mask) {
    if (!mask)
        return 0;
    const int width = widthOf(mask);
    const uint32_t v = (value & mask) >> shiftOf(mask);
    return width >= 8 ? v >> (width - 8) : (v * 255 + ((1u << width) - 1) / 2) / ((1u << width) - 1);
}
uint32_t unchannel(uint32_t v8, uint32_t mask) {
    if (!mask)
        return 0;
    const int width = widthOf(mask);
    const uint32_t v = width >= 8 ? v8 << (width - 8) : v8 >> (8 - width);
    return (v << shiftOf(mask)) & mask;
}

} // namespace

PixelFormat PixelFormat::read(Cpu& c, uint32_t p) {
    PixelFormat f;
    f.flags = c.read32(p + 4);
    f.fourCC = c.read32(p + 8);
    f.bits = c.read32(p + 12);
    f.r = c.read32(p + 16);
    f.g = c.read32(p + 20);
    f.b = c.read32(p + 24);
    f.a = c.read32(p + 28);
    return f;
}

void PixelFormat::write(Cpu& c, uint32_t p) const {
    const uint32_t v[8] = {32, flags, fourCC, bits, r, g, b, a};
    for (uint32_t i = 0; i < 8; ++i)
        c.write32(p + 4 * i, v[i]);
}

uint32_t PixelFormat::toArgb(uint32_t value, const uint32_t* palette) const {
    if (flags & kPfPaletteIndexed8)
        return palette ? palette[value & 0xff] : 0xff000000u | (value & 0xff) * 0x010101u;
    if (flags & kPfLuminance) {
        const uint32_t l = channel(value, r);
        return ((flags & kPfAlphaPixels) ? channel(value, a) : 255) << 24 | l * 0x010101u;
    }
    const uint32_t alpha = (flags & kPfAlphaPixels) && a ? channel(value, a) : 255;
    return alpha << 24 | channel(value, r) << 16 | channel(value, g) << 8 | channel(value, b);
}

uint32_t PixelFormat::fromArgb(uint32_t argb) const {
    if (flags & kPfPaletteIndexed8)
        return argb & 0xff; // no search: a palettized target takes the blue as an index
    uint32_t v = unchannel(argb >> 16 & 0xff, r) | unchannel(argb >> 8 & 0xff, g) | unchannel(argb & 0xff, b);
    if ((flags & kPfAlphaPixels) && a)
        v |= unchannel(argb >> 24, a);
    return v;
}

ArgbConverter::ArgbConverter(const PixelFormat& format, const uint32_t* palette)
    : format_(format), palette_(palette) {
    if (format.flags & (kPfPaletteIndexed8 | kPfLuminance)) {
        general_ = true;
        return;
    }
    const bool alpha = (format.flags & kPfAlphaPixels) && format.a;
    if (!alpha)
        alpha_ = 0xff000000u;
    const std::pair<uint32_t, uint32_t> masks[4] = {{format.r, 16}, {format.g, 8}, {format.b, 0}, {format.a, 24}};
    for (int i = 0; i < (alpha ? 4 : 3); ++i) {
        const uint32_t mask = masks[i].first;
        if (!mask)
            continue;
        Channel c;
        const int width = widthOf(mask);
        c.shift = uint32_t(shiftOf(mask));
        c.mask = mask >> c.shift;
        c.drop = width > 8 ? uint32_t(width - 8) : 0;
        c.to = masks[i].second;
        const uint32_t values = 1u << std::min(width, 8);
        for (uint32_t v = 0; v < values; ++v) // as channel() scales them
            c.table[v] = uint8_t(channel((v << c.drop) << c.shift, mask));
        channels_.push_back(c);
    }
}

void ArgbConverter::row(const uint8_t* bytes, int count, uint32_t* out) const {
    switch (format_.bytes()) {
    case 1:
        for (int i = 0; i < count; ++i)
            out[i] = (*this)(bytes[i]);
        break;
    case 2:
        for (int i = 0; i < count; ++i)
            out[i] = (*this)(uint32_t(bytes[2 * i] | bytes[2 * i + 1] << 8));
        break;
    case 3:
        for (int i = 0; i < count; ++i)
            out[i] = (*this)(uint32_t(bytes[3 * i] | bytes[3 * i + 1] << 8 | bytes[3 * i + 2] << 16));
        break;
    default:
        for (int i = 0; i < count; ++i) {
            uint32_t v;
            std::memcpy(&v, bytes + 4 * i, 4);
            out[i] = (*this)(v);
        }
        break;
    }
}

PixelFormat rgbFormat(int bits) {
    PixelFormat f;
    f.flags = kPfRgb;
    f.bits = uint32_t(bits);
    switch (bits) {
    case 8: f.flags |= kPfPaletteIndexed8; break;
    case 16: f.r = 0xf800, f.g = 0x07e0, f.b = 0x001f; break;
    default: f.r = 0xff0000, f.g = 0x00ff00, f.b = 0x0000ff; break;
    }
    return f;
}

PixelFormat zFormat(int bits) {
    PixelFormat f;
    f.flags = kPfZBuffer;
    f.bits = uint32_t(bits);
    f.g = bits >= 32 ? 0xffffffffu : (1u << bits) - 1; // dwZBitMask
    return f;
}

std::vector<PixelFormat> textureFormats() {
    auto argb = [](uint32_t bits, uint32_t a, uint32_t r, uint32_t g, uint32_t b) {
        PixelFormat f;
        f.flags = kPfRgb | (a ? uint32_t(kPfAlphaPixels) : 0u);
        f.bits = bits, f.a = a, f.r = r, f.g = g, f.b = b;
        return f;
    };
    return {argb(16, 0, 0xf800, 0x07e0, 0x001f), argb(16, 0x8000, 0x7c00, 0x03e0, 0x001f),
            argb(16, 0xf000, 0x0f00, 0x00f0, 0x000f), argb(16, 0, 0x7c00, 0x03e0, 0x001f),
            argb(32, 0xff000000u, 0xff0000, 0xff00, 0xff), argb(32, 0, 0xff0000, 0xff00, 0xff)};
}

Rect Rect::read(Cpu& c, uint32_t p) {
    return {int32_t(c.read32(p)), int32_t(c.read32(p + 4)), int32_t(c.read32(p + 8)), int32_t(c.read32(p + 12))};
}

// --- Surfaces

Surface::~Surface() {
    if (s && d.memory) {
        s->process.memory().free(d.memory);
        if (d.caps[0] & kCapsVideoMemory)
            s->videoUsed -= std::min(s->videoUsed, uint32_t(d.pitch * d.height));
    }
}

uint32_t Surface::pixel(int x, int y) const {
    const uint32_t at = d.memory + uint32_t(y * d.pitch) + uint32_t(x) * d.format.bytes();
    switch (d.format.bytes()) {
    case 1: return s->cpu.read8(at);
    case 2: return s->cpu.read16(at);
    case 3: return s->cpu.read16(at) | uint32_t(s->cpu.read8(at + 2)) << 16;
    default: return s->cpu.read32(at);
    }
}

void Surface::setPixel(int x, int y, uint32_t v) {
    const uint32_t at = d.memory + uint32_t(y * d.pitch) + uint32_t(x) * d.format.bytes();
    switch (d.format.bytes()) {
    case 1: s->cpu.write8(at, uint8_t(v)); break;
    case 2: s->cpu.write16(at, uint16_t(v)); break;
    case 3:
        s->cpu.write16(at, uint16_t(v));
        s->cpu.write8(at + 2, uint8_t(v >> 16));
        break;
    default: s->cpu.write32(at, v); break;
    }
}

const uint32_t* Surface::paletteArgb() const { return palette ? palette->argb.data() : nullptr; }

uint32_t Surface::argb(int x, int y) const { return d.format.toArgb(pixel(x, y), paletteArgb()); }

const ComInterface* Surface::query(const std::string& iid) {
    Interfaces& i = s->interfaces;
    for (ComInterface* f : {&i.surface7, &i.surface4, &i.surface3, &i.surface2, &i.surface})
        if (iid == f->iid)
            return f;
    return nullptr;
}

namespace {

bool isDesc2(Cpu& c, uint32_t p) { return c.read32(p) == 124; }

// Fills a DDSURFACEDESC or DDSURFACEDESC2 (by its dwSize) for `s`.
void writeDesc(Cpu& c, uint32_t p, const Surface& s, uint32_t surfaceAddress) {
    const uint32_t size = isDesc2(c, p) ? 124 : 108;
    std::memset(c.memory().data() + p + 4, 0, size - 4);
    c.write32(p, size);
    uint32_t flags = kSdCaps | kSdHeight | kSdWidth | kSdPitch | kSdPixelFormat;
    c.write32(p + 8, uint32_t(s.d.height));
    c.write32(p + 12, uint32_t(s.d.width));
    c.write32(p + 16, uint32_t(s.d.pitch));
    if (surfaceAddress) {
        flags |= kSdLpSurface;
        c.write32(p + 36, surfaceAddress);
    }
    if (s.hasSrcKey) {
        flags |= kSdCkSrcBlt;
        c.write32(p + 64, s.srcKeyLow);
        c.write32(p + 68, s.srcKeyHigh);
    }
    s.d.format.write(c, p + 72);
    c.write32(p + 104, s.d.caps[0]);
    if (size == 124)
        for (uint32_t i = 1; i < 4; ++i)
            c.write32(p + 104 + 4 * i, s.d.caps[i]);
    c.write32(p + 4, flags);
}

} // namespace

std::optional<uint32_t> Surface::invoke(const ComInterface& iface, int index, Cpu& c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    // What reads or writes pixels (or what they're made of) waits for the
    // draws still drawing.
    if (m == "Lock" || m == "Unlock" || m == "Blt" || m == "BltFast" || m == "BltBatch" || m == "Flip" ||
        m == "GetDC" || m == "ReleaseDC" || m == "Restore" || m == "SetPalette" || m == "SetColorKey" ||
        m == "UpdateOverlay" || m == "AddAttachedSurface" || m == "DeleteAttachedSurface")
        s->finishDraws();
    ComHost& com = s->com;
    // The interface this surface's own pointers are handed out as.
    auto pointerTo = [&](const std::shared_ptr<Surface>& other) { return com.pointer(other, iface); };

    if (m == "GetSurfaceDesc") {
        if (!c.valid(c.arg(1), 108))
            return kDDErrInvalidParams;
        writeDesc(c, c.arg(1), *this, 0);
        return kDDOk;
    }
    if (m == "GetPixelFormat") {
        if (!c.valid(c.arg(1), 32))
            return kDDErrInvalidParams;
        d.format.write(c, c.arg(1));
        return kDDOk;
    }
    if (m == "GetCaps") {
        const uint32_t p = c.arg(1);
        c.write32(p, d.caps[0]);
        if (&iface == &s->interfaces.surface7 || &iface == &s->interfaces.surface4)
            for (uint32_t i = 1; i < 4; ++i)
                c.write32(p + 4 * i, d.caps[i]);
        return kDDOk;
    }
    // Lock(rect, desc, flags, event): the pixels' address (at the rectangle's
    // corner) and pitch in the description.
    if (m == "Lock") {
        const uint32_t rect = c.arg(1), desc = c.arg(2);
        if (!c.valid(desc, 108))
            return kDDErrInvalidParams;
        uint32_t at = d.memory;
        if (rect) {
            const Rect r = Rect::read(c, rect);
            if (r.left < 0 || r.top < 0 || r.right > d.width || r.bottom > d.height || r.left > r.right ||
                r.top > r.bottom)
                return kDDErrInvalidRect;
            at += uint32_t(r.top * d.pitch) + uint32_t(r.left) * d.format.bytes();
        }
        ++locks;
        writeDesc(c, desc, *this, at);
        return kDDOk;
    }
    if (m == "Unlock") {
        if (locks > 0)
            --locks;
        touched();
        if (d.caps[0] & kCapsPrimarySurface)
            s->present();
        return kDDOk;
    }
    if (m == "IsLost" || m == "Restore" || m == "PageLock" || m == "PageUnlock")
        return kDDOk;
    if (m == "GetAttachedSurface") {
        const uint32_t caps = c.read32(c.arg(1)), out = c.arg(2);
        for (const auto& a : attached)
            if ((a->d.caps[0] & caps) == caps) {
                ++a->refs;
                c.write32(out, pointerTo(a));
                return kDDOk;
            }
        c.write32(out, 0);
        return kDDErrNotFound;
    }
    if (m == "AddAttachedSurface") {
        std::shared_ptr<Surface> other = com.shared<Surface>(c.arg(1));
        if (!other)
            return kDDErrInvalidObject;
        if (std::find(attached.begin(), attached.end(), other) == attached.end()) {
            ++other->refs;
            attached.push_back(other);
        }
        return kDDOk;
    }
    if (m == "DeleteAttachedSurface") {
        auto* other = com.as<Surface>(c.arg(2));
        attached.erase(std::remove_if(attached.begin(), attached.end(),
                                      [&](const auto& a) { return !other || a.get() == other; }),
                       attached.end());
        return kDDOk;
    }
    if (m == "SetColorKey") {
        const uint32_t flags = c.arg(1), key = c.arg(2);
        const bool on = key != 0;
        const uint32_t lo = on ? c.read32(key) : 0, hi = on ? c.read32(key + 4) : 0;
        if (flags & kCkeySrcBlt) {
            hasSrcKey = on;
            srcKeyLow = lo, srcKeyHigh = hi;
        }
        if (flags & kCkeyDestBlt) {
            hasDestKey = on;
            destKeyLow = lo, destKeyHigh = hi;
        }
        touched();
        return kDDOk;
    }
    if (m == "GetColorKey") {
        const uint32_t flags = c.arg(1), key = c.arg(2);
        if ((flags & kCkeySrcBlt) && hasSrcKey) {
            c.write32(key, srcKeyLow);
            c.write32(key + 4, srcKeyHigh);
            return kDDOk;
        }
        if ((flags & kCkeyDestBlt) && hasDestKey) {
            c.write32(key, destKeyLow);
            c.write32(key + 4, destKeyHigh);
            return kDDOk;
        }
        return kDDErrNoColorKey;
    }
    if (m == "SetPalette") {
        palette = com.shared<Palette>(c.arg(1));
        touched();
        return kDDOk;
    }
    if (m == "GetPalette") {
        if (!palette)
            return kDDErrNoPalette;
        ++palette->refs;
        c.write32(c.arg(1), com.pointer(palette, s->interfaces.palette));
        return kDDOk;
    }
    if (m == "SetClipper") {
        clipper = com.shared<Clipper>(c.arg(1));
        return kDDOk;
    }
    if (m == "Flip")
        return flip(c, iface);
    if (m == "Blt")
        return blt(c);
    if (m == "BltFast")
        return bltFast(c);
    if (m == "GetBltStatus" || m == "GetFlipStatus")
        return kDDOk;
    if (m == "SetPriority") {
        priority = c.arg(1);
        return kDDOk;
    }
    if (m == "GetPriority") {
        c.write32(c.arg(1), priority);
        return kDDOk;
    }
    if (m == "SetLOD") {
        lod = c.arg(1);
        return kDDOk;
    }
    if (m == "GetLOD") {
        c.write32(c.arg(1), lod);
        return kDDOk;
    }
    if (m == "GetUniquenessValue") {
        c.write32(c.arg(1), uint32_t(d.version));
        return kDDOk;
    }
    if (m == "ChangeUniquenessValue") {
        touched();
        return kDDOk;
    }
    if (m == "GetDDInterface") {
        ++s->directDraw->refs;
        c.write32(c.arg(1), com.pointer(s->directDraw, s->interfaces.directDraw7));
        return kDDOk;
    }
    return std::nullopt;
}

// --- Blits and flips

void Surface::fill(Rect r, uint32_t value) {
    r.left = std::max(r.left, 0), r.top = std::max(r.top, 0);
    r.right = std::min(r.right, d.width), r.bottom = std::min(r.bottom, d.height);
    if (r.left >= r.right || r.top >= r.bottom)
        return;
    const uint32_t bytes = d.format.bytes();
    uint8_t* mem = s->cpu.memory().data();
    for (int y = r.top; y < r.bottom; ++y) {
        uint8_t* row = mem + d.memory + uint32_t(y * d.pitch) + uint32_t(r.left) * bytes;
        for (int x = r.left; x < r.right; ++x, row += bytes)
            for (uint32_t b = 0; b < bytes; ++b)
                row[b] = uint8_t(value >> (8 * b));
    }
    touched();
}

void Surface::copy(const Surface& src, Rect from, Rect to, bool keyed, uint32_t keyLow, uint32_t keyHigh) {
    if (from.width() <= 0 || from.height() <= 0 || to.width() <= 0 || to.height() <= 0)
        return;
    const bool same = src.d.format == d.format && !keyed && from.width() == to.width() &&
                      from.height() == to.height() && !(d.format.flags & kPfPaletteIndexed8 && src.palette != palette);
    uint8_t* mem = s->cpu.memory().data();
    const uint32_t bytes = d.format.bytes();
    // Clipped to both surfaces, keeping the scale.
    const double sx = double(from.width()) / to.width(), sy = double(from.height()) / to.height();
    const int x0 = std::max(to.left, 0), x1 = std::min(to.right, d.width);
    const int y0 = std::max(to.top, 0), y1 = std::min(to.bottom, d.height);
    if (same && sx == 1 && sy == 1) {
        const int dx = from.left - to.left, dy = from.top - to.top;
        const int cx0 = std::max(x0, -dx), cx1 = std::min(x1, src.d.width - dx);
        const int cy0 = std::max(y0, -dy), cy1 = std::min(y1, src.d.height - dy);
        if (cx0 >= cx1)
            return;
        // Rows in the order that copies a surface onto itself correctly.
        const bool down = &src != this || dy >= 0;
        for (int i = 0; i < cy1 - cy0; ++i) {
            const int y = down ? cy0 + i : cy1 - 1 - i;
            std::memmove(mem + d.memory + uint32_t(y * d.pitch) + uint32_t(cx0) * bytes,
                         mem + src.d.memory + uint32_t((y + dy) * src.d.pitch) + uint32_t(cx0 + dx) * bytes,
                         size_t(cx1 - cx0) * bytes);
        }
        touched();
        return;
    }
    const uint32_t* pal = src.paletteArgb();
    for (int y = y0; y < y1; ++y) {
        const int syi = from.top + int((y - to.top + 0.5) * sy);
        if (syi < 0 || syi >= src.d.height)
            continue;
        for (int x = x0; x < x1; ++x) {
            const int sxi = from.left + int((x - to.left + 0.5) * sx);
            if (sxi < 0 || sxi >= src.d.width)
                continue;
            const uint32_t v = src.pixel(sxi, syi);
            if (keyed && v >= keyLow && v <= keyHigh)
                continue;
            setPixel(x, y, src.d.format == d.format ? v : d.format.fromArgb(src.d.format.toArgb(v, pal)));
        }
    }
    touched();
}

// Blt(destRect, src, srcRect, flags, DDBLTFX*): a colour (or depth) fill,
// or a copy, stretched to fit and keyed by the source's colour key if
// asked.
uint32_t Surface::blt(Cpu& c) {
    const uint32_t destRect = c.arg(1), srcPtr = c.arg(2), srcRect = c.arg(3), flags = c.arg(4), fx = c.arg(5);
    const Rect to = destRect ? Rect::read(c, destRect) : bounds();
    if (flags & (kBltColorFill | kBltDepthFill)) {
        if (!c.valid(fx, 84))
            return kDDErrInvalidParams;
        fill(to, c.read32(fx + 80));
    } else {
        Surface* src = s->com.as<Surface>(srcPtr);
        if (!src)
            return kDDErrInvalidObject;
        const Rect from = srcRect ? Rect::read(c, srcRect) : src->bounds();
        bool keyed = false;
        uint32_t lo = 0, hi = 0;
        if (flags & kBltKeySrcOverride) {
            keyed = true;
            lo = c.read32(fx + 92), hi = c.read32(fx + 96);
        } else if ((flags & kBltKeySrc) && src->hasSrcKey) {
            keyed = true;
            lo = src->srcKeyLow, hi = src->srcKeyHigh;
        }
        copy(*src, from, to, keyed, lo, hi);
    }
    if (d.caps[0] & kCapsPrimarySurface)
        s->present();
    return kDDOk;
}

// BltFast(x, y, src, srcRect, flags): an unstretched copy; flag 1 keys it
// by the source's colour key.
uint32_t Surface::bltFast(Cpu& c) {
    const int x = int32_t(c.arg(1)), y = int32_t(c.arg(2));
    Surface* src = s->com.as<Surface>(c.arg(3));
    if (!src)
        return kDDErrInvalidObject;
    const Rect from = c.arg(4) ? Rect::read(c, c.arg(4)) : src->bounds();
    const bool keyed = (c.arg(5) & 1) && src->hasSrcKey;
    copy(*src, from, {x, y, x + from.width(), y + from.height()}, keyed, src->srcKeyLow, src->srcKeyHigh);
    if (d.caps[0] & kCapsPrimarySurface)
        s->present();
    return kDDOk;
}

// Flip: each buffer of the chain takes the next one's pixels, the last
// the front's, so the back buffer drawn becomes what's shown.
uint32_t Surface::flip(Cpu&, const ComInterface&) {
    if (chain.size() < 2)
        return kDDErrNotFlippable;
    const uint32_t first = chain[0]->d.memory;
    for (size_t i = 0; i + 1 < chain.size(); ++i)
        chain[i]->d.memory = chain[i + 1]->d.memory;
    chain.back()->d.memory = first;
    for (Surface* b : chain)
        b->touched();
    if (d.caps[0] & kCapsPrimarySurface)
        s->present();
    return kDDOk;
}

// --- Palettes and clippers

const ComInterface* Palette::query(const std::string& iid) {
    return iid == s->interfaces.palette.iid ? &s->interfaces.palette : nullptr;
}

std::optional<uint32_t> Palette::invoke(const ComInterface& iface, int index, Cpu& c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "SetEntries")
        s->finishDraws();
    if (m == "GetCaps") {
        c.write32(c.arg(1), caps);
        return kDDOk;
    }
    // GetEntries/SetEntries(flags, start, count, PALETTEENTRY*): r, g, b, flags.
    if (m == "GetEntries" || m == "SetEntries") {
        const uint32_t start = c.arg(2), count = c.arg(3), p = c.arg(4);
        if (start + count > 256 || !c.valid(p, count * 4))
            return kDDErrInvalidParams;
        for (uint32_t i = 0; i < count; ++i) {
            if (m == "SetEntries") {
                const uint32_t e = c.read32(p + 4 * i);
                argb[start + i] = 0xff000000u | (e & 0xff) << 16 | (e & 0xff00) | (e >> 16 & 0xff);
            } else {
                const uint32_t v = argb[start + i];
                c.write32(p + 4 * i, (v >> 16 & 0xff) | (v & 0xff00) | (v & 0xff) << 16);
            }
        }
        if (m == "SetEntries")
            if (auto primary = s->primary.lock(); primary && primary->palette.get() == this) {
                primary->touched();
                s->present();
            }
        return kDDOk;
    }
    return std::nullopt;
}

const ComInterface* Clipper::query(const std::string& iid) {
    return iid == s->interfaces.clipper.iid ? &s->interfaces.clipper : nullptr;
}

std::optional<uint32_t> Clipper::invoke(const ComInterface& iface, int index, Cpu& c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "SetHWnd") {
        window = c.arg(2);
        return kDDOk;
    }
    if (m == "GetHWnd") {
        c.write32(c.arg(1), window);
        return kDDOk;
    }
    if (m == "IsClipListChanged") {
        c.write32(c.arg(1), 0);
        return kDDOk;
    }
    if (m == "Initialize")
        return kDDOk;
    return std::nullopt;
}

} // namespace reluva::x86::dx

namespace reluva::x86 {

uint32_t DirectX::State::allocate(dx::SurfaceData& d) {
    const uint32_t bytes = d.format.bytes();
    d.pitch = int((uint32_t(d.width) * bytes + 7) & ~7u);
    d.memory = process.memory().alloc(uint32_t(d.pitch * std::max(d.height, 1)), 16);
    if (d.memory && (d.caps[0] & dx::kCapsVideoMemory))
        videoUsed += uint32_t(d.pitch * d.height);
    return d.memory;
}

std::shared_ptr<dx::Surface> DirectX::State::makeSurface(int w, int h, const dx::PixelFormat& format,
                                                         const std::array<uint32_t, 4>& caps) {
    auto surface = std::make_shared<dx::Surface>();
    surface->s = this;
    surface->d.width = w, surface->d.height = h;
    surface->d.format = format;
    surface->d.caps = caps;
    if (!allocate(surface->d))
        return nullptr;
    return surface;
}

// The frame is the primary surface; with a window (not fullscreen), the
// window's client area of it.
void DirectX::State::present() {
    finishDraws();
    auto p = primary.lock();
    if (!p)
        return;
    int x0 = 0, y0 = 0, w = p->d.width, h = p->d.height;
    if (!exclusive && window)
        if (auto area = process.clientArea(window)) {
            x0 = (*area)[0], y0 = (*area)[1];
            w = (*area)[2], h = (*area)[3];
        }
    frame.width = w, frame.height = h;
    frame.pixels.assign(size_t(w) * size_t(h), 0xff000000u);
    // The part of the window on the surface, a row at a time.
    const int from = std::max(0, -x0), to = std::min(w, p->d.width - x0);
    const dx::ArgbConverter convert(p->d.format, p->paletteArgb());
    const uint32_t bytes = p->d.format.bytes();
    for (int y = std::max(0, -y0); y < std::min(h, p->d.height - y0) && from < to; ++y) {
        const uint32_t at = p->d.memory + uint32_t((y0 + y) * p->d.pitch) + uint32_t(x0 + from) * bytes;
        if (!cpu.valid(at, uint32_t(to - from) * bytes))
            continue;
        convert.row(cpu.memory().data() + at, to - from, frame.pixels.data() + size_t(y) * size_t(w) + size_t(from));
    }
    ++frame.number;
    if (owner.presented)
        owner.presented(frame);
}

} // namespace reluva::x86
