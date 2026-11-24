// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// GDI32's device contexts, fonts and text, bitmaps, DIB sections, blits and
// palettes, as Microsoft's API documentation describes them. Text is drawn
// in Reluva's built-in bitmap font, scaled to the font's height: the shapes
// aren't the font's. Bitmaps are of 8, 16, 24 or 32 bits; a window's device
// context draws nowhere, and reads as black.

#include "emulation/x86/win32.h"

#include "graphics/font.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace reluva::x86 {

namespace {

constexpr uint32_t kTransparent = 1; // SetBkMode's
constexpr uint32_t kBiRgb = 0;
constexpr uint32_t kBiBitFields = 3;
constexpr uint32_t kDibPalColors = 1; // a colour table of indices into the palette selected
constexpr uint32_t kPcReserved = 0x01;
constexpr uint32_t kClrInvalid = 0xffffffffu;
// GetStockObject(DEFAULT_PALETTE), as win32_user.cpp hands stock objects out.
constexpr uint32_t kDefaultPalette = 0x28000 + 15;
// The bitmap a memory device context starts with (1 by 1 and monochrome on
// Windows): nothing to draw into here.
constexpr uint32_t kDefaultBitmap = 0x2ff00;

// The built-in font's scale for a font whose cell is `height` pixels high.
int scaleFor(int height) {
    return std::max(1, (height + font::kLineHeight / 2) / font::kLineHeight);
}

// The bytes of a row of a DIB: whole DWORDs.
int strideOf(int width, int bits) {
    return (width * bits + 31) / 32 * 4;
}

// A COLORREF's channels (red, green, blue) packed by masks, each scaled to
// its mask's width; and unpacked.
uint32_t pack(const std::array<uint32_t, 3> &masks, uint32_t ref) {
    uint32_t v = 0;
    for (int i = 0; i < 3; ++i) {
        const uint32_t m = masks[size_t(i)];
        if (!m)
            continue;
        const int shift = std::countr_zero(m);
        const uint32_t top = m >> shift;
        const uint32_t channel = (ref >> (8 * i)) & 0xff;
        v |= ((channel * top + 127) / 255) << shift & m;
    }
    return v;
}

uint32_t unpack(const std::array<uint32_t, 3> &masks, uint32_t pixel) {
    uint32_t ref = 0;
    for (int i = 0; i < 3; ++i) {
        const uint32_t m = masks[size_t(i)];
        if (!m)
            continue;
        const int shift = std::countr_zero(m);
        const uint64_t top = m >> shift;
        const uint64_t v = (pixel & m) >> shift;
        ref |= uint32_t((v * 255 + top / 2) / top) << (8 * i);
    }
    return ref;
}

// The entry of `colors` (COLORREFs, or PALETTEENTRYs) nearest a COLORREF.
uint32_t nearest(const std::vector<uint32_t> &colors, uint32_t ref) {
    uint32_t best = 0;
    int bestDistance = 1 << 30;
    for (size_t i = 0; i < colors.size(); ++i) {
        int distance = 0;
        for (int k = 0; k < 3; ++k) {
            const int d = int((colors[i] >> (8 * k)) & 0xff) - int((ref >> (8 * k)) & 0xff);
            distance += d * d;
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            best = uint32_t(i);
        }
    }
    return best;
}

uint32_t readPixel(const Cpu &c, const Win32Process::GdiBitmap &b, int x, int y) {
    const uint32_t bytes = uint32_t(b.bits / 8);
    const uint32_t at = b.memory + uint32_t(y * b.pitch) + uint32_t(x) * bytes;
    uint32_t v = 0;
    for (uint32_t i = 0; i < bytes; ++i)
        v |= uint32_t(c.read8(at + i)) << (8 * i);
    return v;
}

void writePixel(Cpu &c, const Win32Process::GdiBitmap &b, int x, int y, uint32_t v) {
    const uint32_t bytes = uint32_t(b.bits / 8);
    const uint32_t at = b.memory + uint32_t(y * b.pitch) + uint32_t(x) * bytes;
    for (uint32_t i = 0; i < bytes; ++i)
        c.write8(at + i, uint8_t(v >> (8 * i)));
}

// Whether pixels can be copied from one bitmap to the other as they are:
// the same depth, and the same colours for the values tried.
bool sameFormat(const Win32Process::GdiBitmap &from, const Win32Process::GdiBitmap &to) {
    if (from.bits != to.bits)
        return false;
    if (!from.colorOf || !to.pixelOf)
        return true;
    std::vector<uint32_t> tried;
    if (from.bits == 8) {
        for (uint32_t i = 0; i < 256; ++i)
            tried.push_back(i);
    } else {
        tried = {0, 0x1f, 0x3e0, 0x7c00, 0x7e0, 0xf800, 0xff, 0xff00, 0xff0000, 0xffffff};
    }
    const uint32_t mask = from.bits == 32 ? 0xffffffu : (1u << from.bits) - 1;
    return std::all_of(tried.begin(), tried.end(), [&](uint32_t v) {
        v &= mask;
        return to.pixelOf(from.colorOf(v)) == v;
    });
}

// Ternary raster operations (the second byte of a ROP code: a truth table
// indexed by pattern 4, source 2, destination 1) that don't use the
// pattern, there being no brushes.
bool usesPattern(uint8_t op) {
    return (op >> 4) != (op & 0xf);
}

uint32_t rasterOp(uint8_t op, uint32_t source, uint32_t destination) {
    uint32_t r = 0;
    for (int i = 0; i < 4; ++i)
        if (op >> i & 1)
            r |= (i & 2 ? source : ~source) & (i & 1 ? destination : ~destination);
    return r;
}

// Copies the source rectangle (sx, sy, sw, sh) to the destination's,
// scaled to it by the nearest pixel, and mirrored where the signs of the
// widths or of the heights differ, as StretchBlt does; no source reads as
// black. False for an operation that needs a pattern.
bool blit(Cpu &c, const Win32Process::GdiBitmap &to, int dx, int dy, int dw, int dh,
          const Win32Process::GdiBitmap *from, int sx, int sy, int sw, int sh, uint32_t rop) {
    const uint8_t op = uint8_t(rop >> 16);
    if (usesPattern(op))
        return false;
    bool flipX = false;
    bool flipY = false;
    if (dw < 0) {
        dx += dw;
        dw = -dw;
        flipX = !flipX;
    }
    if (dh < 0) {
        dy += dh;
        dh = -dh;
        flipY = !flipY;
    }
    if (sw < 0) {
        sx += sw;
        sw = -sw;
        flipX = !flipX;
    }
    if (sh < 0) {
        sy += sh;
        sh = -sh;
        flipY = !flipY;
    }
    if (!dw || !dh || !to.bits)
        return true;
    const uint32_t mask = to.bits == 32 ? 0xffffffffu : (1u << to.bits) - 1;
    const uint32_t black = to.pixelOf ? to.pixelOf(0) : 0;
    const uint32_t white = to.pixelOf ? to.pixelOf(0xffffff) : mask;
    const bool raw = from && sameFormat(*from, to);
    std::unordered_map<uint32_t, uint32_t> converted;
    // A bitmap copied onto itself is read whole before it's written.
    std::vector<uint32_t> before;
    if (from && from->memory == to.memory) {
        before.reserve(size_t(from->width) * size_t(from->height));
        for (int y = 0; y < from->height; ++y)
            for (int x = 0; x < from->width; ++x)
                before.push_back(readPixel(c, *from, x, y));
    }
    auto sourceValue = [&](int x, int y) {
        if (!from || x < 0 || y < 0 || x >= from->width || y >= from->height)
            return black;
        const uint32_t v =
            before.empty() ? readPixel(c, *from, x, y) : before[size_t(y) * size_t(from->width) + size_t(x)];
        if (raw || !from->colorOf || !to.pixelOf)
            return v;
        auto [it, added] = converted.try_emplace(v, 0);
        if (added)
            it->second = to.pixelOf(from->colorOf(v));
        return it->second;
    };
    for (int j = 0; j < dh; ++j) {
        const int ty = dy + j;
        if (ty < 0 || ty >= to.height)
            continue;
        const int y = sy + int(int64_t(flipY ? dh - 1 - j : j) * sh / dh);
        for (int i = 0; i < dw; ++i) {
            const int tx = dx + i;
            if (tx < 0 || tx >= to.width)
                continue;
            uint32_t v;
            if (op == 0x00) {
                v = black;
            } else if (op == 0xff) {
                v = white;
            } else {
                const int x = sx + int(int64_t(flipX ? dw - 1 - i : i) * sw / dw);
                const uint32_t s = sw && sh ? sourceValue(x, y) : black;
                v = op == 0xcc ? s : rasterOp(op, s, readPixel(c, to, tx, ty)) & mask;
            }
            writePixel(c, to, tx, ty, v);
        }
    }
    return true;
}

// A BITMAPINFOHEADER's fields that say how its pixels are laid out.
struct DibHeader {
    uint32_t size = 0;
    int width = 0;
    int height = 0; // negative: top down
    int bits = 0;
    uint32_t compression = 0;
    uint32_t colorsUsed = 0;
};

DibHeader readDibHeader(const Cpu &c, uint32_t p) {
    DibHeader h;
    h.size = c.read32(p);
    h.width = int32_t(c.read32(p + 4));
    h.height = int32_t(c.read32(p + 8));
    h.bits = c.read16(p + 14);
    h.compression = c.read32(p + 16);
    h.colorsUsed = c.read32(p + 32);
    return h;
}

// A DIB's masks: BI_BITFIELDS's, after a BITMAPINFOHEADER (or in a later
// header, at the same place); otherwise 5-5-5 for 16 bits, 8-8-8 above.
std::array<uint32_t, 3> dibMasks(const Cpu &c, uint32_t info, const DibHeader &h) {
    if (h.compression == kBiBitFields)
        return {c.read32(info + 40), c.read32(info + 44), c.read32(info + 48)};
    if (h.bits == 16)
        return {0x7c00, 0x03e0, 0x001f};
    return {0xff0000, 0xff00, 0xff};
}

bool supportedDib(const DibHeader &h) {
    const bool depth = h.bits == 8 || h.bits == 16 || h.bits == 24 || h.bits == 32;
    const bool layout = h.compression == kBiRgb || (h.compression == kBiBitFields && h.bits != 8 && h.bits != 24);
    return depth && layout && h.width > 0 && h.height != 0 && h.width <= 0x8000 && std::abs(h.height) <= 0x8000;
}

} // namespace

Win32Process::GdiBitmapObject Win32Process::makeBitmap(int width, int height, int bits, bool bottomUp,
                                                       std::array<uint32_t, 3> masks, std::vector<uint32_t> colors,
                                                       uint32_t memory) {
    GdiBitmapObject o;
    const int stride = strideOf(width, bits);
    if (!memory) {
        o.block = _arena->alloc(uint32_t(stride) * uint32_t(height));
        memory = o.block;
    }
    GdiBitmap &b = o.bitmap;
    b.memory = bottomUp ? memory + uint32_t(stride) * uint32_t(height - 1) : memory;
    b.width = width;
    b.height = height;
    b.pitch = bottomUp ? -stride : stride;
    b.bits = bits;
    if (bits == 8) {
        colors.resize(256);
        o.colors = std::make_shared<std::vector<uint32_t>>(std::move(colors));
        b.pixelOf = [table = o.colors](uint32_t ref) { return nearest(*table, ref); };
        b.colorOf = [table = o.colors](uint32_t v) { return (*table)[v & 0xff] & 0xffffff; };
    } else {
        b.pixelOf = [masks](uint32_t ref) { return pack(masks, ref); };
        b.colorOf = [masks](uint32_t v) { return unpack(masks, v); };
    }
    return o;
}

Win32Process::DeviceContext &Win32Process::deviceContext(uint32_t dc) {
    DeviceContext &d = _deviceContexts[dc];
    // A window's: a surface the size of its client area, made anew when
    // the window's size has changed. The desktop's (GetDC(0)) has none.
    if (dc > kWindowDeviceContexts && dc <= kWindowDeviceContexts + 0xffff) {
        const uint32_t window = 0x10000 | (dc & 0xffff);
        const auto area = clientArea(window);
        if (!area || (*area)[2] <= 0 || (*area)[3] <= 0)
            return d;
        auto it = _windowSurfaces.find(window);
        if (it == _windowSurfaces.end() || it->second.bitmap.width != (*area)[2] ||
            it->second.bitmap.height != (*area)[3]) {
            if (it != _windowSurfaces.end())
                _arena->free(it->second.block);
            it = _windowSurfaces
                     .insert_or_assign(window,
                                       makeBitmap((*area)[2], (*area)[3], 32, false, {0xff0000, 0xff00, 0xff}, {}))
                     .first;
            std::memset(_cpu.memory().data() + it->second.block, 0, size_t((*area)[2]) * size_t((*area)[3]) * 4);
        }
        d.target = it->second.bitmap;
    }
    return d;
}

void Win32Process::drawnOn(uint32_t dc) {
    if (!windowDrawn || dc <= kWindowDeviceContexts || dc > kWindowDeviceContexts + 0xffff)
        return;
    const uint32_t window = 0x10000 | (dc & 0xffff);
    auto it = _windowSurfaces.find(window);
    if (it == _windowSurfaces.end())
        return;
    const GdiBitmap &b = it->second.bitmap;
    windowDrawn(window, reinterpret_cast<const uint32_t *>(_cpu.memory().data() + b.memory), b.width, b.height);
}

uint32_t Win32Process::createDeviceContext(GdiBitmap bitmap) {
    const uint32_t dc = _nextGdiObject++;
    _deviceContexts[dc].target = std::move(bitmap);
    return dc;
}

void Win32Process::deleteDeviceContext(uint32_t dc) {
    _deviceContexts.erase(dc);
}

void Win32Process::addGdiStandIns() {
    const char *g = "GDI32.dll";

    // CreateFontA(height, width, escapement, orientation, weight, italic,
    // underline, strike out, charset, precision, clip precision, quality,
    // pitch and family, face): a negative height is the characters', a
    // positive one the cell's; 0 is a default.
    stdcall(g, "CreateFontA", 14, [this](Cpu &c) {
        const int32_t h = int32_t(c.arg(0));
        GdiFont f;
        f.height = h == 0 ? 16 : h < 0 ? -h * 5 / 4 : h;
        f.weight = int(c.arg(4));
        f.face = c.arg(13) ? c.string(c.arg(13)) : std::string();
        const uint32_t handle = _nextGdiObject++;
        _fonts[handle] = f;
        return handle;
    });
    // SelectObject(dc, object): fonts are kept; the font selected before is
    // returned. A bitmap goes into a memory device context only, which then
    // draws into it; the one before is returned (0 for any other context).
    // Other objects are taken and the same handed back.
    stdcall(g, "SelectObject", 2, [this](Cpu &c) {
        DeviceContext &dc = deviceContext(c.arg(0));
        const uint32_t object = c.arg(1);
        if (_fonts.count(object)) {
            const uint32_t was = dc.font;
            dc.font = object;
            return was ? was : object;
        }
        auto bitmap = _bitmaps.find(object);
        if (bitmap != _bitmaps.end() || object == kDefaultBitmap) {
            if (!dc.memory)
                return 0u;
            const uint32_t was = dc.bitmap ? dc.bitmap : kDefaultBitmap;
            dc.bitmap = object == kDefaultBitmap ? 0 : object;
            if (bitmap != _bitmaps.end())
                dc.target = bitmap->second.bitmap;
            else
                dc.target.reset();
            return was;
        }
        return object;
    });
    stdcall(g, "DeleteObject", 1, [this](Cpu &c) {
        const uint32_t object = c.arg(0);
        _fonts.erase(object);
        _palettes.erase(object);
        if (auto it = _bitmaps.find(object); it != _bitmaps.end()) {
            if (it->second.block)
                _arena->free(it->second.block);
            _bitmaps.erase(it);
        }
        return 1u;
    });
    stdcall(g, "SetTextColor", 2, [this](Cpu &c) {
        DeviceContext &dc = deviceContext(c.arg(0));
        return std::exchange(dc.textColor, c.arg(1) & 0xffffff);
    });
    stdcall(g, "SetBkColor", 2, [this](Cpu &c) {
        DeviceContext &dc = deviceContext(c.arg(0));
        return std::exchange(dc.backColor, c.arg(1) & 0xffffff);
    });
    stdcall(g, "SetBkMode", 2, [this](Cpu &c) {
        DeviceContext &dc = deviceContext(c.arg(0));
        return std::exchange(dc.backMode, c.arg(1));
    });
    stdcall(g, "GdiFlush", 0, [](Cpu &) { return 1u; });

    auto fontScale = [this](const DeviceContext &dc) {
        auto it = _fonts.find(dc.font);
        return scaleFor(it == _fonts.end() ? 16 : it->second.height);
    };
    // GetTextExtentPoint32A(dc, text, count, SIZE*)
    stdcall(g, "GetTextExtentPoint32A", 4, [this, fontScale](Cpu &c) {
        const int scale = fontScale(deviceContext(c.arg(0)));
        if (!c.valid(c.arg(3), 8))
            return 0u;
        c.write32(c.arg(3), c.arg(2) * uint32_t(font::kAdvance * scale));
        c.write32(c.arg(3) + 4, uint32_t(font::kLineHeight * scale));
        return 1u;
    });
    // TextOutA(dc, x, y, text, count): with the opaque background mode, the
    // text's box is filled with the background colour first.
    stdcall(g, "TextOutA", 5, [this, fontScale](Cpu &c) {
        DeviceContext &dc = deviceContext(c.arg(0));
        const uint32_t count = std::min(c.arg(4), 4096u);
        if (!dc.target || !c.valid(c.arg(3), count))
            return 1u;
        const GdiBitmap &b = *dc.target;
        const int scale = fontScale(dc);
        std::string text(count, '\0');
        std::memcpy(text.data(), c.memory().data() + c.arg(3), count);
        Surface glyphs;
        glyphs.create(int(count) * font::kAdvance * scale, font::kLineHeight * scale, PixelFormat::kRGBA32);
        font::drawText(glyphs, 0, 0, text, {255, 255, 255}, scale);
        const int x0 = int32_t(c.arg(1));
        const int y0 = int32_t(c.arg(2));
        const uint32_t ink = b.pixelOf(dc.textColor);
        const uint32_t paper = b.pixelOf(dc.backColor);
        const int bytes = b.bits / 8;
        for (int y = 0; y < glyphs.h; ++y) {
            const int ty = y0 + y;
            if (ty < 0 || ty >= b.height)
                continue;
            for (int x = 0; x < glyphs.w; ++x) {
                const int tx = x0 + x;
                if (tx < 0 || tx >= b.width)
                    continue;
                const bool set = glyphs.row(y)[x * 4 + 3] != 0;
                if (!set && dc.backMode == kTransparent)
                    continue;
                const uint32_t at = b.memory + uint32_t(ty * b.pitch + tx * bytes);
                if (!c.valid(at, uint32_t(bytes)))
                    continue;
                const uint32_t v = set ? ink : paper;
                for (int i = 0; i < bytes; ++i)
                    c.write8(at + uint32_t(i), uint8_t(v >> (8 * i)));
            }
        }
        return 1u;
    });

    // Memory device contexts, and bitmaps.

    auto paletteOf = [this](uint32_t dc) -> const std::vector<uint32_t> * {
        auto it = _deviceContexts.find(dc);
        if (it == _deviceContexts.end())
            return nullptr;
        auto p = _palettes.find(it->second.palette);
        return p == _palettes.end() ? nullptr : &p->second;
    };
    // The colours a device context's new 8-bit bitmap takes: its palette's,
    // else those of what it draws into, else greys.
    auto deviceColors = [this, paletteOf](uint32_t dc) {
        std::vector<uint32_t> colors(256);
        const DeviceContext &d = deviceContext(dc);
        for (uint32_t i = 0; i < 256; ++i)
            colors[i] = i | i << 8 | i << 16;
        if (const std::vector<uint32_t> *palette = paletteOf(dc)) {
            for (size_t i = 0; i < colors.size() && i < palette->size(); ++i)
                colors[i] = (*palette)[i] & 0xffffff;
        } else if (d.target && d.target->bits == 8 && d.target->colorOf) {
            for (uint32_t i = 0; i < 256; ++i)
                colors[i] = d.target->colorOf(i);
        }
        return colors;
    };
    // A DIB's colour table, after its header: RGBQUADs (blue, green, red),
    // or with DIB_PAL_COLORS 16-bit indices into the context's palette.
    auto dibColors = [paletteOf](Cpu &c, uint32_t info, const DibHeader &h, uint32_t usage, uint32_t dc) {
        std::vector<uint32_t> colors;
        if (h.bits != 8)
            return colors;
        const uint32_t count = h.colorsUsed && h.colorsUsed < 256 ? h.colorsUsed : 256;
        const uint32_t table = info + h.size;
        const std::vector<uint32_t> *palette = paletteOf(dc);
        for (uint32_t i = 0; i < count; ++i) {
            if (usage == kDibPalColors) {
                const uint32_t index = c.read16(table + 2 * i);
                colors.push_back(palette && index < palette->size() ? (*palette)[index] & 0xffffff : 0);
            } else {
                const uint32_t q = c.read32(table + 4 * i);
                colors.push_back((q >> 16 & 0xff) | (q & 0xff00) | (q & 0xff) << 16);
            }
        }
        return colors;
    };
    auto keep = [this](GdiBitmapObject o) {
        const uint32_t handle = _nextGdiObject++;
        _bitmaps[handle] = std::move(o);
        return handle;
    };
    constexpr int64_t kLargestBitmap = 64 << 20; // bytes

    // CreateCompatibleDC(dc): a memory device context, drawing into the
    // bitmap selected into it.
    stdcall(g, "CreateCompatibleDC", 1, [this](Cpu &) {
        const uint32_t dc = _nextGdiObject++;
        _deviceContexts[dc].memory = true;
        return dc;
    });
    stdcall(g, "DeleteDC", 1, [this](Cpu &c) { return _deviceContexts.erase(c.arg(0)) ? 1u : 0u; });
    // CreateCompatibleBitmap(dc, width, height): of the depth and colours
    // of what the context draws into, else of the desktop's depth (where
    // Windows would make a memory context's monochrome, after the bitmap it
    // starts with); 16 bits are 5-6-5.
    stdcall(g, "CreateCompatibleBitmap", 3, [this, deviceColors, keep](Cpu &c) {
        const DeviceContext &dc = deviceContext(c.arg(0));
        const int width = std::max(1, int(int32_t(c.arg(1))));
        const int height = std::max(1, int(int32_t(c.arg(2))));
        int bits = dc.target ? dc.target->bits : screenBits;
        if (bits != 8 && bits != 16 && bits != 24)
            bits = 32;
        if (int64_t(strideOf(width, bits)) * height > kLargestBitmap)
            return 0u;
        const std::array<uint32_t, 3> masks = bits == 16 ? std::array<uint32_t, 3>{0xf800, 0x07e0, 0x001f}
                                                         : std::array<uint32_t, 3>{0xff0000, 0xff00, 0xff};
        GdiBitmapObject o = makeBitmap(width, height, bits, false, masks, deviceColors(c.arg(0)));
        if (!o.block)
            return 0u;
        if (bits != 8 && dc.target && dc.target->pixelOf && dc.target->colorOf) {
            o.bitmap.pixelOf = dc.target->pixelOf;
            o.bitmap.colorOf = dc.target->colorOf;
        }
        return keep(std::move(o));
    });
    // CreateDIBSection(dc, info, usage, bits, section, offset): a DIB of 8,
    // 16, 24 or 32 bits, BI_RGB or BI_BITFIELDS, in memory of its own, its
    // address written to `bits`. A file mapping given is ignored: the
    // pixels aren't shared with it.
    stdcall(g, "CreateDIBSection", 6, [this, dibColors, keep](Cpu &c) {
        const uint32_t info = c.arg(1);
        const uint32_t bitsOut = c.arg(3);
        if (bitsOut)
            c.write32(bitsOut, 0);
        if (!c.valid(info, 40))
            return 0u;
        const DibHeader h = readDibHeader(c, info);
        const int height = std::abs(h.height);
        if (!supportedDib(h) || int64_t(strideOf(h.width, h.bits)) * height > kLargestBitmap)
            return 0u;
        GdiBitmapObject o = makeBitmap(h.width, height, h.bits, h.height > 0, dibMasks(c, info, h),
                                       dibColors(c, info, h, c.arg(2), c.arg(0)));
        if (!o.block)
            return 0u;
        o.dibSection = true;
        if (bitsOut)
            c.write32(bitsOut, o.block);
        return keep(std::move(o));
    });
    // GetDIBits(dc, bitmap, first, lines, bits, info, usage): with no
    // `bits`, the bitmap's own format (if the header's bit count is 0) or
    // the image's size and colour table; else its scan lines from `first`
    // (the bottom one first, for a bottom-up DIB) in the header's format.
    // 8 bits only from an 8-bit bitmap: colours aren't reduced to a table.
    stdcall(g, "GetDIBits", 7, [this](Cpu &c) {
        auto it = _bitmaps.find(c.arg(1));
        const uint32_t info = c.arg(5);
        const uint32_t out = c.arg(4);
        if (it == _bitmaps.end() || !c.valid(info, 52))
            return 0u;
        const GdiBitmap &b = it->second.bitmap;
        DibHeader h = readDibHeader(c, info);
        auto writeTable = [&] {
            if (b.bits != 8 || h.bits != 8 || !b.colorOf)
                return;
            for (uint32_t i = 0; i < 256; ++i) {
                if (c.arg(6) == kDibPalColors) {
                    c.write16(info + h.size + 2 * i, uint16_t(i));
                } else {
                    const uint32_t ref = b.colorOf(i);
                    c.write32(info + h.size + 4 * i, (ref >> 16 & 0xff) | (ref & 0xff00) | (ref & 0xff) << 16);
                }
            }
        };
        if (!out && h.bits == 0) {
            const std::array<uint32_t, 3> masks = {b.pixelOf(0xff), b.pixelOf(0xff00), b.pixelOf(0xff0000)};
            const bool fields = (b.bits == 16 && masks != std::array<uint32_t, 3>{0x7c00, 0x03e0, 0x001f}) ||
                                (b.bits == 32 && masks != std::array<uint32_t, 3>{0xff0000, 0xff00, 0xff});
            c.write32(info + 4, uint32_t(b.width));
            c.write32(info + 8, uint32_t(b.height));
            c.write16(info + 12, 1);
            c.write16(info + 14, uint16_t(b.bits));
            c.write32(info + 16, fields ? kBiBitFields : kBiRgb);
            c.write32(info + 20, uint32_t(strideOf(b.width, b.bits) * b.height));
            c.write32(info + 32, 0);
            if (fields)
                for (uint32_t i = 0; i < 3; ++i)
                    c.write32(info + 40 + 4 * i, masks[i]);
            return 1u;
        }
        h.width = b.width;
        if (!supportedDib(h) || (h.bits == 8 && b.bits != 8))
            return 0u;
        if (!out) {
            c.write32(info + 20, uint32_t(strideOf(b.width, h.bits) * b.height));
            writeTable();
            return 1u;
        }
        const std::array<uint32_t, 3> masks = dibMasks(c, info, h);
        const uint32_t stride = uint32_t(strideOf(b.width, h.bits));
        const uint32_t bytes = uint32_t(h.bits / 8);
        uint32_t copied = 0;
        for (; copied < c.arg(3); ++copied) {
            const uint32_t scan = c.arg(2) + copied;
            const uint32_t row = out + copied * stride;
            if (scan >= uint32_t(b.height) || !c.valid(row, stride))
                break;
            const int y = h.height > 0 ? b.height - 1 - int(scan) : int(scan);
            for (int x = 0; x < b.width; ++x) {
                uint32_t v = readPixel(c, b, x, y);
                if (h.bits != 8)
                    v = pack(masks, b.colorOf ? b.colorOf(v) : v);
                for (uint32_t i = 0; i < bytes; ++i)
                    c.write8(row + uint32_t(x) * bytes + i, uint8_t(v >> (8 * i)));
            }
        }
        writeTable();
        return copied;
    });

    // Blits. Scaling takes the nearest pixel, as COLORONCOLOR does, whatever
    // the stretch mode; raster operations that use a brush's pattern fail.

    // What a device context draws into, to read from: a window's own
    // surface for a window's.
    auto sourceOf = [this](uint32_t dc) -> const GdiBitmap * {
        if (!_deviceContexts.count(dc) && (dc <= kWindowDeviceContexts || dc > kWindowDeviceContexts + 0xffff))
            return nullptr;
        const DeviceContext &d = deviceContext(dc);
        return d.target ? &*d.target : nullptr;
    };
    // BitBlt(dc, x, y, width, height, source dc, source x, source y, rop)
    stdcall(g, "BitBlt", 9, [this, sourceOf](Cpu &c) {
        const DeviceContext &dc = deviceContext(c.arg(0));
        const uint32_t rop = c.arg(8);
        if (!dc.target)
            return usesPattern(uint8_t(rop >> 16)) ? 0u : 1u;
        const int w = int32_t(c.arg(3));
        const int h = int32_t(c.arg(4));
        if (!blit(c, *dc.target, int32_t(c.arg(1)), int32_t(c.arg(2)), w, h, sourceOf(c.arg(5)), int32_t(c.arg(6)),
                  int32_t(c.arg(7)), w, h, rop))
            return 0u;
        drawnOn(c.arg(0));
        return 1u;
    });
    // StretchBlt(dc, x, y, width, height, source dc, x, y, width, height, rop)
    stdcall(g, "StretchBlt", 11, [this, sourceOf](Cpu &c) {
        const DeviceContext &dc = deviceContext(c.arg(0));
        const uint32_t rop = c.arg(10);
        if (!dc.target)
            return usesPattern(uint8_t(rop >> 16)) ? 0u : 1u;
        if (!blit(c, *dc.target, int32_t(c.arg(1)), int32_t(c.arg(2)), int32_t(c.arg(3)), int32_t(c.arg(4)),
                  sourceOf(c.arg(5)), int32_t(c.arg(6)), int32_t(c.arg(7)), int32_t(c.arg(8)), int32_t(c.arg(9)), rop))
            return 0u;
        drawnOn(c.arg(0));
        return 1u;
    });
    // StretchDIBits(dc, x, y, width, height, source x, y, width, height,
    // bits, info, usage, rop): the scan lines copied. A bottom-up DIB's
    // source rectangle is measured from its lower left corner.
    stdcall(g, "StretchDIBits", 13, [this, dibColors](Cpu &c) {
        const uint32_t pixels = c.arg(9);
        const uint32_t info = c.arg(10);
        if (!c.valid(info, 40))
            return 0u;
        const DibHeader h = readDibHeader(c, info);
        const int height = std::abs(h.height);
        if (!supportedDib(h) || !c.valid(pixels, uint32_t(strideOf(h.width, h.bits) * height)))
            return 0u;
        const GdiBitmapObject source = makeBitmap(h.width, height, h.bits, h.height > 0, dibMasks(c, info, h),
                                                  dibColors(c, info, h, c.arg(11), c.arg(0)), pixels);
        int sy = int32_t(c.arg(6));
        const int sh = int32_t(c.arg(8));
        if (h.height > 0)
            sy = height - sy - sh;
        const DeviceContext &dc = deviceContext(c.arg(0));
        if (dc.target) {
            if (!blit(c, *dc.target, int32_t(c.arg(1)), int32_t(c.arg(2)), int32_t(c.arg(3)), int32_t(c.arg(4)),
                      &source.bitmap, int32_t(c.arg(5)), sy, int32_t(c.arg(7)), sh, c.arg(12)))
                return 0u;
            drawnOn(c.arg(0));
        } else if (usesPattern(uint8_t(c.arg(12) >> 16))) {
            return 0u;
        }
        return uint32_t(std::abs(sh));
    });
    // SetStretchBltMode(dc, mode): kept, the mode before returned; 0 for
    // one that isn't BLACKONWHITE (1) to HALFTONE (4).
    stdcall(g, "SetStretchBltMode", 2, [this](Cpu &c) {
        if (c.arg(1) < 1 || c.arg(1) > 4)
            return 0u;
        return std::exchange(deviceContext(c.arg(0)).stretchMode, c.arg(1));
    });

    // Palettes.

    // CreatePalette(LOGPALETTE: version, count, then PALETTEENTRYs)
    stdcall(g, "CreatePalette", 1, [this](Cpu &c) {
        const uint32_t p = c.arg(0);
        const uint32_t count = c.read16(p + 2);
        if (!count || !c.valid(p, 4 + 4 * count))
            return 0u;
        std::vector<uint32_t> entries(count);
        for (uint32_t i = 0; i < count; ++i)
            entries[i] = c.read32(p + 4 + 4 * i);
        const uint32_t handle = _nextGdiObject++;
        _palettes[handle] = std::move(entries);
        return handle;
    });
    // SelectPalette(dc, palette, background): the palette before.
    stdcall(g, "SelectPalette", 3, [this](Cpu &c) {
        const uint32_t palette = c.arg(1);
        if (palette != kDefaultPalette && !_palettes.count(palette))
            return 0u;
        DeviceContext &dc = deviceContext(c.arg(0));
        const uint32_t was = dc.palette ? dc.palette : kDefaultPalette;
        dc.palette = palette == kDefaultPalette ? 0 : palette;
        return was;
    });
    // RealizePalette(dc): the entries mapped into the system's palette, all
    // of them on a desktop with a palette, none on one without.
    stdcall(g, "RealizePalette", 1, [this, paletteOf](Cpu &c) {
        const std::vector<uint32_t> *palette = paletteOf(c.arg(0));
        return screenBits <= 8 && palette ? uint32_t(palette->size()) : 0u;
    });
    // GetNearestPaletteIndex(palette, colour)
    stdcall(g, "GetNearestPaletteIndex", 2, [this](Cpu &c) {
        auto it = _palettes.find(c.arg(0));
        return it == _palettes.end() ? kClrInvalid : nearest(it->second, c.arg(1));
    });
    // GetSystemPaletteUse(dc): SYSPAL_STATIC, Windows' twenty colours kept;
    // SYSPAL_ERROR on a desktop without a palette.
    stdcall(g, "GetSystemPaletteUse", 1, [this](Cpu &) { return screenBits <= 8 ? 1u : 0u; });
    // AnimatePalette(palette, first, count, entries): only the entries made
    // PC_RESERVED change.
    stdcall(g, "AnimatePalette", 4, [this](Cpu &c) {
        auto it = _palettes.find(c.arg(0));
        if (it == _palettes.end())
            return 0u;
        std::vector<uint32_t> &entries = it->second;
        for (uint32_t i = 0; i < c.arg(2) && c.arg(1) + i < entries.size(); ++i)
            if (entries[c.arg(1) + i] >> 24 & kPcReserved)
                entries[c.arg(1) + i] = c.read32(c.arg(3) + 4 * i);
        return 1u;
    });
    // SetDIBColorTable(dc, first, count, RGBQUADs): the colours of the
    // 8-bit DIB section selected into the context; the entries set.
    stdcall(g, "SetDIBColorTable", 4, [this](Cpu &c) {
        auto dc = _deviceContexts.find(c.arg(0));
        if (dc == _deviceContexts.end())
            return 0u;
        auto it = _bitmaps.find(dc->second.bitmap);
        if (it == _bitmaps.end() || !it->second.dibSection || !it->second.colors)
            return 0u;
        std::vector<uint32_t> &colors = *it->second.colors;
        uint32_t n = 0;
        for (; n < c.arg(2) && c.arg(1) + n < colors.size(); ++n) {
            const uint32_t q = c.read32(c.arg(3) + 4 * n);
            colors[c.arg(1) + n] = (q >> 16 & 0xff) | (q & 0xff00) | (q & 0xff) << 16;
        }
        return n;
    });
}

} // namespace reluva::x86
