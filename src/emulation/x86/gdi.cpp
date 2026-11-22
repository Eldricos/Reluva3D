// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// GDI32's objects and device contexts, and USER32's drawing, as Microsoft
// documents them (docs/common/x86/gdi.md).

#include "emulation/x86/gdi.h"

#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"
#include "graphics/font.h"
#include "image/bmp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace reluva::x86 {

namespace {

constexpr uint32_t kStock = 0x5a000000; // stock object i is kStock + i
constexpr int kStockObjects = 18;
constexpr uint32_t kSrcCopy = 0x00cc0020;

int maskShift(uint32_t mask) {
    int s = 0;
    while (mask && !(mask & 1)) {
        mask >>= 1;
        ++s;
    }
    return s;
}

int maskBits(uint32_t mask) {
    int n = 0;
    for (mask >>= maskShift(mask); mask & 1; mask >>= 1)
        ++n;
    return n;
}

void defaultMasks(const Gdi::Target &t, uint32_t &r, uint32_t &g, uint32_t &b) {
    r = t.redMask;
    g = t.greenMask;
    b = t.blueMask;
    if (!r && !g && !b) {
        r = 0xf800;
        g = 0x07e0;
        b = 0x001f;
    }
}

uint8_t red(uint32_t colorRef) {
    return uint8_t(colorRef);
}
uint8_t green(uint32_t colorRef) {
    return uint8_t(colorRef >> 8);
}
uint8_t blue(uint32_t colorRef) {
    return uint8_t(colorRef >> 16);
}

// USER32's system colours (GetSysColor's), Windows 98's standard scheme,
// as COLORREFs: what FillRect's brush (COLOR_x + 1) and DrawEdge use.
uint32_t sysColor(int index) {
    switch (index) {
    case 5:  // COLOR_WINDOW
    case 14: // COLOR_HIGHLIGHTTEXT
    case 20: // COLOR_3DHILIGHT
        return 0xffffff;
    case 6:  // COLOR_WINDOWFRAME
    case 7:  // COLOR_MENUTEXT
    case 8:  // COLOR_WINDOWTEXT
    case 18: // COLOR_BTNTEXT
    case 21: // COLOR_3DDKSHADOW
        return 0x000000;
    case 16: // COLOR_3DSHADOW
        return 0x808080;
    case 13: // COLOR_HIGHLIGHT
        return 0x800000;
    case 1: // COLOR_BACKGROUND
        return 0x808000;
    default: // COLOR_3DFACE and the rest
        return 0xc0c0c0;
    }
}

} // namespace

uint32_t Gdi::pixelFor(const Target &t, uint32_t colorRef) {
    const uint8_t r = red(colorRef);
    const uint8_t g = green(colorRef);
    const uint8_t b = blue(colorRef);
    switch (t.bits) {
    case 8: {
        int best = 0;
        int bestDistance = INT32_MAX;
        for (int i = 0; i < 256; ++i) {
            const int dr = int((t.palette[size_t(i)] >> 16) & 0xff) - r;
            const int dg = int((t.palette[size_t(i)] >> 8) & 0xff) - g;
            const int db = int(t.palette[size_t(i)] & 0xff) - b;
            const int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (d < bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        return uint32_t(best);
    }
    case 16: {
        uint32_t rm;
        uint32_t gm;
        uint32_t bm;
        defaultMasks(t, rm, gm, bm);
        auto part = [](uint8_t v, uint32_t mask) {
            return (uint32_t(v) >> (8 - maskBits(mask)) << maskShift(mask)) & mask;
        };
        return part(r, rm) | part(g, gm) | part(b, bm);
    }
    default:
        return uint32_t(r) << 16 | uint32_t(g) << 8 | b;
    }
}

uint32_t Gdi::colorAt(Cpu &c, const Target &t, int x, int y) {
    const uint32_t at = t.memory + uint32_t(y * t.pitch);
    switch (t.bits) {
    case 8: {
        const uint32_t rgb = t.palette[c.read8(at + uint32_t(x))];
        return (rgb >> 16 & 0xff) | (rgb & 0xff00) | (rgb & 0xff) << 16;
    }
    case 16: {
        uint32_t rm;
        uint32_t gm;
        uint32_t bm;
        defaultMasks(t, rm, gm, bm);
        const uint32_t v = c.read16(at + uint32_t(x * 2));
        auto part = [v](uint32_t mask) {
            const int n = maskBits(mask);
            const uint32_t p = (v & mask) >> maskShift(mask);
            return n ? uint32_t(p * 255 / ((1u << n) - 1)) : 0u;
        };
        return part(rm) | part(gm) << 8 | part(bm) << 16;
    }
    case 24: {
        const uint32_t p = at + uint32_t(x * 3);
        return c.read8(p + 2) | uint32_t(c.read8(p + 1)) << 8 | uint32_t(c.read8(p)) << 16;
    }
    default: {
        const uint32_t v = c.read32(at + uint32_t(x * 4));
        return (v >> 16 & 0xff) | (v & 0xff00) | (v & 0xff) << 16;
    }
    }
}

Gdi::Gdi(Win32Process &process) : _process(process) {
    // The stock objects, by GetStockObject's index.
    const uint32_t brushes[] = {0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0x000000};
    for (int i = 0; i < 5; ++i)
        _objects[kStock + uint32_t(i)] = Object{Object::kBrush, brushes[i], 0, 0, 1, 0, 400, false, {}, {}, true};
    _objects[kStock + 5] = Object{Object::kBrush, 0, 1, 0, 1, 0, 400, false, {}, {}, true}; // NULL_BRUSH
    _objects[kStock + 6] = Object{Object::kPen, 0xffffff, 0, 0, 1, 0, 400, false, {}, {}, true};
    _objects[kStock + 7] = Object{Object::kPen, 0x000000, 0, 0, 1, 0, 400, false, {}, {}, true};
    _objects[kStock + 8] = Object{Object::kPen, 0, 5, 0, 1, 0, 400, false, {}, {}, true}; // NULL_PEN
    for (int i = 10; i < kStockObjects; ++i)
        if (i != 15) // DEFAULT_PALETTE
            _objects[kStock + uint32_t(i)] = Object{Object::kFont, 0, 0, 0, 1, 16, 400, false, "System", {}, true};
    addStandIns();
}

uint32_t Gdi::addObject(Object o) {
    const uint32_t h = _nextObject++;
    _objects[h] = std::move(o);
    return h;
}

Gdi::Dc *Gdi::dc(uint32_t handle) {
    const auto it = _dcs.find(handle);
    return it == _dcs.end() ? nullptr : &it->second;
}

uint32_t Gdi::createDc(const Target &target) {
    Dc d;
    d.target = target;
    d.brush = kStock;
    d.pen = kStock + 7;
    d.font = kStock + 13;
    const uint32_t h = _nextDc++;
    _dcs[h] = d;
    return h;
}

void Gdi::releaseDc(uint32_t handle) {
    _dcs.erase(handle);
}

std::optional<Gdi::Target> Gdi::targetOf(const Dc &d) const {
    if (d.window)
        return screen ? screen() : std::nullopt;
    return d.target;
}

void Gdi::plot(Cpu &c, const Target &t, int x, int y, uint32_t colorRef) {
    if (x < 0 || y < 0 || x >= t.width || y >= t.height)
        return;
    const uint32_t v = pixelFor(t, colorRef);
    const uint32_t at = t.memory + uint32_t(y * t.pitch);
    switch (t.bits) {
    case 8:
        c.write8(at + uint32_t(x), uint8_t(v));
        break;
    case 16:
        c.write16(at + uint32_t(x * 2), uint16_t(v));
        break;
    case 24:
        c.write8(at + uint32_t(x * 3), uint8_t(v));
        c.write8(at + uint32_t(x * 3 + 1), uint8_t(v >> 8));
        c.write8(at + uint32_t(x * 3 + 2), uint8_t(v >> 16));
        break;
    default:
        c.write32(at + uint32_t(x * 4), v);
        break;
    }
}

// Right and bottom are outside, as a RECT's are.
void Gdi::fill(Cpu &c, const Target &t, int left, int top, int right, int bottom, uint32_t colorRef) {
    left = std::max(left, 0);
    top = std::max(top, 0);
    right = std::min(right, t.width);
    bottom = std::min(bottom, t.height);
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            plot(c, t, x, y, colorRef);
}

// Bresenham's, the last point left out, as LineTo leaves it.
void Gdi::line(Cpu &c, const Target &t, int x0, int y0, int x1, int y1, uint32_t colorRef) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (x0 != x1 || y0 != y1) {
        plot(c, t, x0, y0, colorRef);
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// DrawTextA: lines broken at line feeds and (DT_WORDBREAK) to fit the
// width; each character the built-in font's advance, scaled to the font's
// height. Returns the text's height.
int Gdi::drawText(Cpu &c, Dc &d, const std::string &raw, int left, int top, int right, int bottom, uint32_t format,
                  uint32_t rect) {
    const Object &f = _objects.count(d.font) ? _objects[d.font] : _objects[kStock + 13];
    const int scale = std::max(1, int(std::lround(std::abs(f.height ? f.height : 16) / double(font::kLineHeight))));
    const int advance = font::kAdvance * scale;
    const int lineHeight = font::kLineHeight * scale;
    // '&' marks the next character (shown underlined, here plain), "&&" is '&'.
    std::string text;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '&' && !(format & 0x800)) {
            if (i + 1 < raw.size() && raw[i + 1] == '&')
                text += '&';
            ++i;
            if (i < raw.size() && raw[i] != '&')
                text += raw[i];
            continue;
        }
        text += raw[i];
    }
    const bool single = format & 0x20;
    const int width = right - left;
    std::vector<std::string> lines;
    std::string current;
    auto flush = [&] {
        lines.push_back(current);
        current.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (!single && ch == '\r')
            continue;
        if (!single && ch == '\n') {
            flush();
            continue;
        }
        current += ch;
        if ((format & 0x10) && !single && int(current.size()) * advance > width && current.size() > 1) {
            const size_t space = current.find_last_of(' ');
            if (space != std::string::npos && space > 0) {
                const std::string rest = current.substr(space + 1);
                current.erase(space);
                flush();
                current = rest;
            }
        }
    }
    if (!current.empty() || lines.empty())
        flush();
    const int height = lineHeight * int(lines.size());
    if (format & 0x400) { // DT_CALCRECT: the rectangle the text needs
        size_t longest = 0;
        for (const std::string &l : lines)
            longest = std::max(longest, l.size());
        c.write32(rect + 8, uint32_t(left + int(longest) * advance));
        c.write32(rect + 12, uint32_t(top + height));
        return height;
    }
    const auto target = targetOf(d);
    if (!target)
        return height;
    int y = top;
    if (single && (format & 0x4))
        y = top + (bottom - top - lineHeight) / 2;
    else if (single && (format & 0x8))
        y = bottom - lineHeight;
    for (const std::string &l : lines) {
        const int w = int(l.size()) * advance;
        int x = left;
        if (format & 0x1)
            x = left + (width - w) / 2;
        else if (format & 0x2)
            x = right - w;
        if (d.bkMode == 2)
            fill(c, *target, x, y, x + w, y + lineHeight, d.bkColor);
        // The glyphs, drawn white on black, give the pixels to colour.
        Surface glyphs;
        glyphs.create(std::max(1, w), lineHeight, PixelFormat::kRGBA32);
        font::drawText(glyphs, 0, 0, l, {255, 255, 255}, scale);
        for (int gy = 0; gy < glyphs.h; ++gy)
            for (int gx = 0; gx < glyphs.w; ++gx)
                if (glyphs.row(gy)[gx * 4] && x + gx < right && y + gy < bottom && x + gx >= left)
                    plot(c, *target, x + gx, y + gy, d.textColor);
        y += lineHeight;
    }
    return height;
}

void Gdi::addStandIns() {
    Win32Process &p = _process;
    const char *g = "GDI32.dll";
    const char *u = "USER32.dll";
    auto readRect = [](Cpu &c, uint32_t r, int &left, int &top, int &right, int &bottom) {
        left = int32_t(c.read32(r));
        top = int32_t(c.read32(r + 4));
        right = int32_t(c.read32(r + 8));
        bottom = int32_t(c.read32(r + 12));
    };
    // A brush's colour; FillRect also takes a system colour's index + 1.
    auto brushColor = [this](uint32_t brush, uint32_t &color) {
        if (brush > 0 && brush <= 31) {
            color = sysColor(int(brush) - 1);
            return true;
        }
        const auto it = _objects.find(brush);
        if (it == _objects.end() || it->second.kind != Object::kBrush || it->second.style == 1)
            return false;
        color = it->second.color;
        return true;
    };

    // Objects.
    p.provideStdcall(g, "GetStockObject", 1, [this](Cpu &c) {
        const uint32_t h = kStock + c.arg(0);
        return _objects.count(h) ? h : 0u;
    });
    p.provideStdcall(g, "CreateSolidBrush", 1, [this](Cpu &c) {
        Object o;
        o.color = c.arg(0) & 0xffffff;
        return addObject(o);
    });
    p.provideStdcall(g, "CreateHatchBrush", 2, [this](Cpu &c) {
        Object o;
        o.style = 2;
        o.hatch = int(c.arg(0));
        o.color = c.arg(1) & 0xffffff;
        return addObject(o);
    });
    p.provideStdcall(g, "CreatePen", 3, [this](Cpu &c) {
        Object o;
        o.kind = Object::kPen;
        o.style = int(c.arg(0) & 0xf);
        o.width = std::max(1, int(c.arg(1)));
        o.color = c.arg(2) & 0xffffff;
        return addObject(o);
    });
    // CreateFontA(height, width, escapement, orientation, weight, italic,
    // underline, strike out, character set, output precision, clipping
    // precision, quality, pitch and family, face name).
    p.provideStdcall(g, "CreateFontA", 14, [this](Cpu &c) {
        Object o;
        o.kind = Object::kFont;
        o.height = int(int32_t(c.arg(0)));
        o.weight = int(c.arg(4));
        o.italic = c.arg(5) != 0;
        o.face = c.arg(13) ? c.string(c.arg(13), 32) : "";
        return addObject(o);
    });
    p.provideStdcall(g, "DeleteObject", 1, [this](Cpu &c) {
        const auto it = _objects.find(c.arg(0));
        if (it == _objects.end())
            return 0u;
        if (!it->second.stock) {
            if (it->second.kind == Object::kBitmap)
                _process.memory().free(it->second.bitmap.memory);
            _objects.erase(it);
        }
        return 1u;
    });
    // GetObjectA(object, size, buffer): BITMAP (24 bytes), LOGPEN (16),
    // LOGBRUSH (12), LOGFONTA (60); with no buffer, the size.
    p.provideStdcall(g, "GetObjectA", 3, [this](Cpu &c) {
        const auto it = _objects.find(c.arg(0));
        if (it == _objects.end())
            return 0u;
        const Object &o = it->second;
        const uint32_t sizes[] = {12, 16, 60, 24};
        const uint32_t size = sizes[o.kind];
        const uint32_t buf = c.arg(2);
        if (!buf)
            return size;
        const uint32_t n = std::min(size, c.arg(1));
        std::vector<uint8_t> out(size, 0);
        auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&out[at], &v, 4); };
        auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&out[at], &v, 2); };
        switch (o.kind) {
        case Object::kBrush:
            put32(0, uint32_t(o.style));
            put32(4, o.color);
            put32(8, uint32_t(o.hatch));
            break;
        case Object::kPen:
            put32(0, uint32_t(o.style));
            put32(4, uint32_t(o.width));
            put32(12, o.color);
            break;
        case Object::kFont:
            put32(0, uint32_t(o.height));
            put32(16, uint32_t(o.weight));
            out[20] = o.italic;
            std::memcpy(&out[28], o.face.data(), std::min<size_t>(o.face.size(), 31));
            break;
        case Object::kBitmap:
            put32(4, uint32_t(o.bitmap.width));
            put32(8, uint32_t(o.bitmap.height));
            put32(12, uint32_t(o.bitmap.pitch));
            put16(16, 1);
            put16(18, uint16_t(o.bitmap.bits));
            break;
        }
        if (!c.valid(buf, n))
            return 0u;
        std::memcpy(c.memory().data() + buf, out.data(), n);
        return n;
    });

    // Device contexts.
    p.provideStdcall(g, "CreateCompatibleDC", 1, [this](Cpu &) {
        Dc d;
        d.brush = kStock;
        d.pen = kStock + 7;
        d.font = kStock + 13;
        const uint32_t h = _nextDc++;
        _dcs[h] = d;
        return h;
    });
    p.provideStdcall(g, "DeleteDC", 1, [this](Cpu &c) { return _dcs.erase(c.arg(0)) ? 1u : 0u; });
    // SelectObject(dc, object): the object of that kind selected before. A
    // bitmap becomes a memory DC's pixels.
    p.provideStdcall(g, "SelectObject", 2, [this](Cpu &c) {
        Dc *d = dc(c.arg(0));
        const auto it = _objects.find(c.arg(1));
        if (!d || it == _objects.end())
            return 0u;
        uint32_t *slot = nullptr;
        switch (it->second.kind) {
        case Object::kBrush:
            slot = &d->brush;
            break;
        case Object::kPen:
            slot = &d->pen;
            break;
        case Object::kFont:
            slot = &d->font;
            break;
        case Object::kBitmap:
            slot = &d->bitmap;
            d->target = it->second.bitmap;
            break;
        }
        const uint32_t before = *slot;
        *slot = c.arg(1);
        return before ? before : 1u;
    });
    p.provideStdcall(g, "SetTextColor", 2, [this](Cpu &c) {
        Dc *d = dc(c.arg(0));
        if (!d)
            return 0xffffffffu;
        const uint32_t before = d->textColor;
        d->textColor = c.arg(1) & 0xffffff;
        return before;
    });
    p.provideStdcall(g, "SetBkColor", 2, [this](Cpu &c) {
        Dc *d = dc(c.arg(0));
        if (!d)
            return 0xffffffffu;
        const uint32_t before = d->bkColor;
        d->bkColor = c.arg(1) & 0xffffff;
        return before;
    });
    p.provideStdcall(g, "SetBkMode", 2, [this](Cpu &c) {
        Dc *d = dc(c.arg(0));
        if (!d)
            return 0u;
        const int before = d->bkMode;
        d->bkMode = int(c.arg(1));
        return uint32_t(before);
    });

    // Drawing.
    p.provideStdcall(g, "MoveToEx", 4, [this](Cpu &c) {
        Dc *d = dc(c.arg(0));
        if (!d)
            return 0u;
        if (c.arg(3)) {
            c.write32(c.arg(3), uint32_t(d->x));
            c.write32(c.arg(3) + 4, uint32_t(d->y));
        }
        d->x = int32_t(c.arg(1));
        d->y = int32_t(c.arg(2));
        return 1u;
    });
    p.provideStdcall(g, "LineTo", 3, [this](Cpu &c) {
        Dc *d = dc(c.arg(0));
        if (!d)
            return 0u;
        const int x = int32_t(c.arg(1));
        const int y = int32_t(c.arg(2));
        const auto t = targetOf(*d);
        const Object &pen = _objects[d->pen];
        if (t && pen.style != 5)
            for (int w = 0; w < pen.width; ++w)
                line(c, *t, d->x + w, d->y, x + w, y, pen.color);
        d->x = x;
        d->y = y;
        return 1u;
    });
    p.provideStdcall(g, "SetPixel", 4, [this](Cpu &c) {
        Dc *d = dc(c.arg(0));
        const auto t = d ? targetOf(*d) : std::nullopt;
        const int x = int32_t(c.arg(1));
        const int y = int32_t(c.arg(2));
        if (!t || x < 0 || y < 0 || x >= t->width || y >= t->height)
            return 0xffffffffu;
        plot(c, *t, x, y, c.arg(3) & 0xffffff);
        return colorAt(c, *t, x, y);
    });
    // Ellipse(dc, left, top, right, bottom): filled with the brush, outlined with the pen.
    p.provideStdcall(g, "Ellipse", 5, [this, brushColor](Cpu &c) {
        Dc *d = dc(c.arg(0));
        const auto t = d ? targetOf(*d) : std::nullopt;
        if (!t)
            return d ? 1u : 0u;
        const int left = int32_t(c.arg(1));
        const int top = int32_t(c.arg(2));
        const int right = int32_t(c.arg(3));
        const int bottom = int32_t(c.arg(4));
        const double cx = (left + right - 1) / 2.0;
        const double cy = (top + bottom - 1) / 2.0;
        const double rx = (right - left) / 2.0;
        const double ry = (bottom - top) / 2.0;
        if (rx <= 0 || ry <= 0)
            return 1u;
        uint32_t fillColor = 0;
        const bool filled = brushColor(d->brush, fillColor);
        const Object &pen = _objects[d->pen];
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x) {
                const double nx = (x - cx) / rx;
                const double ny = (y - cy) / ry;
                const double r = nx * nx + ny * ny;
                if (r > 1.0)
                    continue;
                // The outline: within a pixel of the edge.
                const double inner = std::pow(1.0 - 1.0 / std::min(rx, ry), 2);
                if (pen.style != 5 && r >= inner)
                    plot(c, *t, x, y, pen.color);
                else if (filled)
                    plot(c, *t, x, y, fillColor);
            }
        return 1u;
    });
    // StretchBlt(dest, x, y, w, h, source, sx, sy, sw, sh, rop): the nearest
    // pixel, SRCCOPY's only; a negative size mirrors.
    p.provideStdcall(g, "StretchBlt", 11, [this](Cpu &c) {
        Dc *dd = dc(c.arg(0));
        Dc *sd = dc(c.arg(5));
        const auto to = dd ? targetOf(*dd) : std::nullopt;
        const auto from = sd ? targetOf(*sd) : std::nullopt;
        if (!dd || !sd)
            return 0u;
        if (!to || !from)
            return 1u;
        if (c.arg(10) != kSrcCopy && _process.output)
            _process.output("StretchBlt: raster operation " + std::to_string(c.arg(10)) + " drawn as SRCCOPY\n");
        const int x = int32_t(c.arg(1));
        const int y = int32_t(c.arg(2));
        const int w = int32_t(c.arg(3));
        const int h = int32_t(c.arg(4));
        const int sx = int32_t(c.arg(6));
        const int sy = int32_t(c.arg(7));
        const int sw = int32_t(c.arg(8));
        const int sh = int32_t(c.arg(9));
        if (!w || !h)
            return 1u;
        for (int j = 0; j < std::abs(h); ++j)
            for (int i = 0; i < std::abs(w); ++i) {
                const int fx = sx + int(int64_t(i) * sw / std::abs(w));
                const int fy = sy + int(int64_t(j) * sh / std::abs(h));
                if (fx < 0 || fy < 0 || fx >= from->width || fy >= from->height)
                    continue;
                plot(c, *to, w < 0 ? x - 1 - i : x + i, h < 0 ? y - 1 - j : y + j, colorAt(c, *from, fx, fy));
            }
        return 1u;
    });

    // USER32's drawing.
    p.provideStdcall(u, "GetDC", 1, [this](Cpu &) {
        Dc d;
        d.window = true;
        d.brush = kStock;
        d.pen = kStock + 7;
        d.font = kStock + 13;
        const uint32_t h = _nextDc++;
        _dcs[h] = d;
        return h;
    });
    p.provideStdcall(u, "ReleaseDC", 2, [this](Cpu &c) {
        const auto it = _dcs.find(c.arg(1));
        if (it == _dcs.end() || !it->second.window)
            return 0u;
        _dcs.erase(it);
        return 1u;
    });
    // BeginPaint(window, PAINTSTRUCT*): a window DC, the whole screen to paint.
    p.provideStdcall(u, "BeginPaint", 2, [this](Cpu &c) {
        Dc d;
        d.window = true;
        d.brush = kStock;
        d.pen = kStock + 7;
        d.font = kStock + 13;
        const uint32_t h = _nextDc++;
        _dcs[h] = d;
        const uint32_t ps = c.arg(1);
        if (c.valid(ps, 64)) {
            std::memset(c.memory().data() + ps, 0, 64);
            c.write32(ps, h);
            const auto t = screen ? screen() : std::nullopt;
            c.write32(ps + 16, uint32_t(t ? t->width : _process.screenWidth));
            c.write32(ps + 20, uint32_t(t ? t->height : _process.screenHeight));
        }
        return h;
    });
    p.provideStdcall(u, "EndPaint", 2, [this](Cpu &c) {
        if (c.valid(c.arg(1), 4))
            _dcs.erase(c.read32(c.arg(1)));
        return 1u;
    });
    p.provideStdcall(u, "FillRect", 3, [this, readRect, brushColor](Cpu &c) {
        Dc *d = dc(c.arg(0));
        const auto t = d ? targetOf(*d) : std::nullopt;
        uint32_t color = 0;
        if (!d || !brushColor(c.arg(2), color))
            return 0u;
        if (t) {
            int l;
            int tp;
            int r;
            int b;
            readRect(c, c.arg(1), l, tp, r, b);
            fill(c, *t, l, tp, r, b, color);
        }
        return 1u;
    });
    p.provideStdcall(u, "FrameRect", 3, [this, readRect, brushColor](Cpu &c) {
        Dc *d = dc(c.arg(0));
        const auto t = d ? targetOf(*d) : std::nullopt;
        uint32_t color = 0;
        if (!d || !brushColor(c.arg(2), color))
            return 0u;
        if (t) {
            int l;
            int tp;
            int r;
            int b;
            readRect(c, c.arg(1), l, tp, r, b);
            fill(c, *t, l, tp, r, tp + 1, color);
            fill(c, *t, l, b - 1, r, b, color);
            fill(c, *t, l, tp, l + 1, b, color);
            fill(c, *t, r - 1, tp, r, b, color);
        }
        return 1u;
    });
    // DrawEdge(dc, rect, edge, flags): raised or sunken, outer and inner
    // lines in the 3D colours; BF_MIDDLE fills, BF_ADJUST shrinks the rect.
    p.provideStdcall(u, "DrawEdge", 4, [this, readRect](Cpu &c) {
        Dc *d = dc(c.arg(0));
        if (!d)
            return 0u;
        const auto t = targetOf(*d);
        int l;
        int tp;
        int r;
        int b;
        readRect(c, c.arg(1), l, tp, r, b);
        const uint32_t edge = c.arg(2);
        const uint32_t flags = c.arg(3);
        auto band = [&](uint32_t topLeft, uint32_t bottomRight) {
            if (t) {
                if (flags & 2)
                    fill(c, *t, l, tp, r, tp + 1, topLeft);
                if (flags & 1)
                    fill(c, *t, l, tp, l + 1, b, topLeft);
                if (flags & 8)
                    fill(c, *t, l, b - 1, r, b, bottomRight);
                if (flags & 4)
                    fill(c, *t, r - 1, tp, r, b, bottomRight);
            }
            l += flags & 1 ? 1 : 0;
            tp += flags & 2 ? 1 : 0;
            r -= flags & 4 ? 1 : 0;
            b -= flags & 8 ? 1 : 0;
        };
        if (edge & 1) // BDR_RAISEDOUTER
            band(sysColor(15), sysColor(21));
        else if (edge & 2) // BDR_SUNKENOUTER
            band(sysColor(16), sysColor(20));
        if (edge & 4) // BDR_RAISEDINNER
            band(sysColor(20), sysColor(16));
        else if (edge & 8) // BDR_SUNKENINNER
            band(sysColor(21), sysColor(15));
        if ((flags & 0x800) && t)
            fill(c, *t, l, tp, r, b, sysColor(15));
        if (flags & 0x2000) {
            c.write32(c.arg(1), uint32_t(l));
            c.write32(c.arg(1) + 4, uint32_t(tp));
            c.write32(c.arg(1) + 8, uint32_t(r));
            c.write32(c.arg(1) + 12, uint32_t(b));
        }
        return 1u;
    });
    // DrawTextA(dc, text, count (-1: to its zero), rect, format).
    p.provideStdcall(u, "DrawTextA", 5, [this, readRect](Cpu &c) {
        Dc *d = dc(c.arg(0));
        if (!d)
            return 0u;
        const int32_t count = int32_t(c.arg(2));
        const std::string text = count < 0 ? c.string(c.arg(1)) : c.string(c.arg(1), uint32_t(count));
        int l;
        int t;
        int r;
        int b;
        readRect(c, c.arg(3), l, t, r, b);
        return uint32_t(drawText(c, *d, text, l, t, r, b, c.arg(4), c.arg(3)));
    });
    // LoadImageA(instance, name, type, width, height, flags): a bitmap
    // (IMAGE_BITMAP) from a file (LR_LOADFROMFILE), stretched to the size
    // asked if one is; others aren't loaded.
    p.provideStdcall(u, "LoadImageA", 6, [this](Cpu &c) -> uint32_t {
        const uint32_t type = c.arg(2);
        const uint32_t flags = c.arg(5);
        if (type != 0 || !(flags & 0x10) || c.arg(1) < 0x10000) {
            if (_process.output)
                _process.output("LoadImageA: only bitmaps from files are loaded\n");
            return 0;
        }
        const auto host =
            _process.mapPath ? _process.mapPath(_process.fullPath(c.string(c.arg(1))), false) : std::nullopt;
        std::ifstream in(host ? *host : std::filesystem::path(), std::ios::binary);
        if (!host || !in)
            return 0;
        const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        image::Bitmap bmp;
        if (image::decodeBmp(file, bmp))
            return 0;
        const int w = c.arg(3) ? int(c.arg(3)) : bmp.surface.w;
        const int h = c.arg(4) ? int(c.arg(4)) : bmp.surface.h;
        Object o;
        o.kind = Object::kBitmap;
        Target &t = o.bitmap;
        t.width = w;
        t.height = h;
        t.bits = bmp.indexed() ? 8 : 32;
        t.pitch = (w * t.bits / 8 + 3) & ~3;
        t.memory = _process.memory().alloc(uint32_t(t.pitch * h));
        if (!t.memory)
            return 0;
        if (bmp.indexed())
            for (int i = 0; i < 256; ++i) {
                uint8_t r;
                uint8_t g;
                uint8_t b;
                bmp.palette.getColor(i, r, g, b);
                t.palette[size_t(i)] = uint32_t(r) << 16 | uint32_t(g) << 8 | b;
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int fx = x * bmp.surface.w / w;
                const int fy = y * bmp.surface.h / h;
                const uint8_t *src = bmp.surface.row(fy) + fx * (bmp.indexed() ? 1 : 4);
                if (bmp.indexed())
                    c.write8(t.memory + uint32_t(y * t.pitch + x), src[0]);
                else
                    c.write32(t.memory + uint32_t(y * t.pitch + x * 4),
                              uint32_t(src[0]) << 16 | uint32_t(src[1]) << 8 | src[2]);
            }
        return addObject(o);
    });
}

} // namespace reluva::x86
