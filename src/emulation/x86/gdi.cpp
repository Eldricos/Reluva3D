// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// GDI32's objects and device contexts, and USER32's drawing, as Microsoft
// documents them (docs/common/x86/gdi.md).

#include "emulation/x86/gdi.h"

#include "common/text.h"
#include "graphics/builtin_fonts.h"
#include "graphics/truetype.h"

#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"
#include "graphics/font.h"
#include "image/bmp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace reluva::x86 {

namespace {

constexpr uint32_t kStock = 0x5a000000; // stock object i is kStock + i
// Windows' hinted Arial at the small sizes programs draw in is narrower than
// Liberation Sans drawn in the same cell: a line of LEGO Loco's, in Arial
// Bold with a cell of 16, is 183.5 pixels in a recording of the original,
// 195 in Liberation Sans at the cell's em of 14 and 180 at 13; drawn at
// this much of the cell, its em is 13 *(inferred)*.
constexpr float kArialWidth = 0.93f;
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
    // The fonts, as Windows 98's: OEM_FIXED_FONT, ANSI_FIXED_FONT and
    // SYSTEM_FIXED_FONT fixed; SYSTEM_FONT bold; the others plain.
    for (int i = 10; i < kStockObjects; ++i)
        if (i != 15) { // DEFAULT_PALETTE
            const bool fixed = i == 10 || i == 11 || i == 16;
            _objects[kStock + uint32_t(i)] =
                Object{Object::kFont, 0,    0, 0, 1, 16, i == 13 ? 700 : 400, false, fixed ? "Fixedsys" : "System", {},
                       true,          fixed};
        }
    addStandIns();
}

Gdi::~Gdi() = default;

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

uint32_t Gdi::windowDc(uint32_t window) {
    Dc d;
    d.window = true;
    d.windowHandle = window;
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

// A window's DC draws in its client area of the screen: the target is
// that part of the screen's pixels. (Where the area is off the screen's
// left or top, its drawing starts at the screen's edge instead; no program
// has needed one there.)
std::optional<Gdi::Target> Gdi::targetOf(const Dc &d) const {
    if (!d.window)
        return d.target;
    std::optional<Target> t = screen ? screen() : std::nullopt;
    if (!t || !d.windowHandle)
        return t;
    const auto area = _process.clientArea(d.windowHandle);
    if (!area)
        return t;
    const int left = std::clamp((*area)[0], 0, t->width);
    const int top = std::clamp((*area)[1], 0, t->height);
    const int right = std::clamp((*area)[0] + (*area)[2], left, t->width);
    const int bottom = std::clamp((*area)[1] + (*area)[3], top, t->height);
    t->memory += uint32_t(top * t->pitch + left * t->bits / 8);
    t->width = right - left;
    t->height = bottom - top;
    return t;
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

// Each scheme's colours by index (COLOR_SCROLLBAR, 0, to
// COLOR_MENUBAR, 30), as COLORREFs: Windows 98's standard scheme, and
// Windows XP's blue one.
uint32_t Gdi::sysColor(int index) const {
    static constexpr std::array<uint32_t, 31> kWindows98 = {
        0xc0c0c0, 0x808000, 0x800000, 0x808080, 0xc0c0c0, 0xffffff, 0x000000, 0x000000, 0x000000, 0xffffff, 0xc0c0c0,
        0xc0c0c0, 0x808080, 0x800000, 0xffffff, 0xc0c0c0, 0x808080, 0x808080, 0x000000, 0xc0c0c0, 0xffffff, 0x000000,
        0xc0c0c0, 0x000000, 0xe1ffff, 0xc0c0c0, 0x800000, 0xd08410, 0xc0c0c0, 0x800000, 0xc0c0c0};
    static constexpr std::array<uint32_t, 31> kWindowsXp = {
        0xc8d0d4, 0x984e00, 0xe35400, 0xdf967a, 0xffffff, 0xffffff, 0x000000, 0x000000, 0x000000, 0xffffff, 0xc8d0d4,
        0xc8d0d4, 0x808080, 0xc56a31, 0xffffff, 0xd8e9ec, 0x99a8ac, 0x99a8ac, 0x000000, 0xf8e4d8, 0xffffff, 0x646f71,
        0xe2eff1, 0x000000, 0xe1ffff, 0xb5b5b5, 0x800000, 0xff953d, 0xebb99d, 0xc56a31, 0xd8e9ec};
    const auto &scheme = colours == ColourScheme::kWindowsXp ? kWindowsXp : kWindows98;
    return index >= 0 && index < int(scheme.size()) ? scheme[size_t(index)] : scheme[15];
}

void Gdi::paintDesktop(const std::array<int, 4> &area) {
    const auto t = screen ? screen() : std::nullopt;
    if (!t)
        return;
    // The windows shown on top (a child lies within its parent).
    std::vector<std::array<int, 4>> covered;
    for (const uint32_t h : _process.windows()) {
        const Win32Process::Window *w = _process.window(h);
        if (w && !(w->style & 0x40000000) && _process.shown(h)) // WS_CHILD
            if (const auto a = _process.windowArea(h))
                covered.push_back(*a);
    }
    Cpu &c = _process.cpu();
    for (int y = std::max(area[1], 0); y < std::min(area[1] + area[3], t->height); ++y)
        for (int x = std::max(area[0], 0); x < std::min(area[0] + area[2], t->width); ++x) {
            const bool under = std::any_of(covered.begin(), covered.end(), [&](const std::array<int, 4> &a) {
                return x >= a[0] && x < a[0] + a[2] && y >= a[1] && y < a[1] + a[3];
            });
            if (!under)
                plot(c, *t, x, y, sysColor(1)); // COLOR_BACKGROUND
        }
}

void Gdi::paintFrame(uint32_t window, const std::optional<std::array<int, 4>> &clip) {
    const auto t = screen ? screen() : std::nullopt;
    const Win32Process::Window *w = _process.window(window);
    const auto whole = _process.windowArea(window);
    const auto inside = _process.clientArea(window);
    if (!t || !w || !whole || !inside || *whole == *inside)
        return;
    Cpu &c = _process.cpu();
    auto put = [&](int x, int y, uint32_t colorRef) {
        if (!clip || (x >= (*clip)[0] && x < (*clip)[0] + (*clip)[2] && y >= (*clip)[1] && y < (*clip)[1] + (*clip)[3]))
            plot(c, *t, x, y, colorRef);
    };
    const auto [wx, wy, ww, wh] = *whole;
    // The border's own width, without the sunken edge inside it.
    const int sunken = (w->exStyle & 0x200) ? 2 : (w->exStyle & 0x20000) ? 1 : 0;
    const int edge = (*inside)[0] - wx - sunken;
    for (int y = wy; y < wy + wh; ++y)
        for (int x = wx; x < wx + ww; ++x)
            if (!(x >= (*inside)[0] && x < (*inside)[0] + (*inside)[2] && y >= (*inside)[1] &&
                  y < (*inside)[1] + (*inside)[3]))
                put(x, y, sysColor(15)); // COLOR_3DFACE
    // A border of 2 pixels or more is raised, as a button is: light above
    // and left, dark below and right, in two rings; a border of 1 is a line.
    auto ring = [&](int inset, uint32_t lightColour, uint32_t darkColour) {
        const int l = wx + inset;
        const int tp = wy + inset;
        const int r = wx + ww - 1 - inset;
        const int b = wy + wh - 1 - inset;
        for (int x = l; x <= r; ++x)
            put(x, tp, lightColour), put(x, b, darkColour);
        for (int y = tp; y <= b; ++y)
            put(l, y, lightColour), put(r, y, darkColour);
    };
    if (edge >= 2) {
        ring(0, sysColor(22), sysColor(21)); // COLOR_3DLIGHT, COLOR_3DDKSHADOW
        ring(1, sysColor(20), sysColor(16)); // COLOR_3DHILIGHT, COLOR_3DSHADOW
    } else if (edge == 1) {
        ring(0, sysColor(6), sysColor(6)); // COLOR_WINDOWFRAME
    }
    paintCaption(c, *t, window, clip);
    // A sunken edge round the client area and its scroll bars: two rings
    // (WS_EX_CLIENTEDGE), or one (WS_EX_STATICEDGE), dark above and left.
    const int rings = sunken;
    {
        int left = (*inside)[0];
        int top = (*inside)[1];
        int right = left + (*inside)[2] + ((w->style & 0x00200000) ? 16 : 0);
        int bottom = top + (*inside)[3] + ((w->style & 0x00100000) ? 16 : 0);
        const uint32_t darks[2] = {sysColor(21), sysColor(16)}; // inner ring, outer
        const uint32_t lights[2] = {sysColor(22), sysColor(20)};
        for (int r = 0; r < rings; ++r) {
            --left, --top, ++right, ++bottom;
            const int i = rings == 1 ? 1 : r;
            for (int x = left; x < right; ++x)
                put(x, top, darks[i]), put(x, bottom - 1, lights[i]);
            for (int y = top; y < bottom; ++y)
                put(left, y, darks[i]), put(right - 1, y, lights[i]);
        }
    }
    // Its scroll bars, as Windows 98 draws them: a pale track between two
    // arrow buttons, and the thumb a raised button where the position is.
    for (int bar = 0; bar < 2; ++bar) {
        const auto area = _process.scrollBarArea(window, bar);
        if (!area)
            continue;
        const auto [bx, by, bw, bh] = *area;
        // The track: white dotted with COLOR_SCROLLBAR, drawn as their mean.
        uint32_t track = 0;
        for (int shift = 0; shift < 24; shift += 8)
            track |= ((0xffu + ((sysColor(0) >> shift) & 0xff) + 1) / 2) << shift;
        for (int y = by; y < by + bh; ++y)
            for (int x = bx; x < bx + bw; ++x)
                put(x, y, track);
        const int size = bar ? bw : bh;
        const int length = bar ? bh : bw;
        // A raised square button `size` long, `from` the bar's start.
        auto button = [&](int from) {
            const int x0 = bar ? bx : bx + from;
            const int y0 = bar ? by + from : by;
            for (int y = y0; y < y0 + size; ++y)
                for (int x = x0; x < x0 + size; ++x) {
                    const bool light = x == x0 || y == y0;
                    const bool dark = x == x0 + size - 1 || y == y0 + size - 1;
                    put(x, y, dark ? sysColor(21) : light ? sysColor(20) : sysColor(15));
                }
        };
        // An arrow's triangle in a button, pointing back (-1) or on (+1).
        auto arrow = [&](int from, int way) {
            button(from);
            const int mid = size / 2;
            for (int i = 0; i < 4; ++i)
                for (int j = -i; j <= i; ++j) {
                    const int along = from + mid + way * (2 - i); // the tip first
                    put(bar ? bx + mid + j : bx + along, bar ? by + along : by + mid + j, sysColor(18));
                }
        };
        arrow(0, -1);
        arrow(length - size, 1);
        if (const int thumb = _process.scrollThumb(window, bar); thumb >= 0)
            button(thumb);
    }
}

// A caption as Windows 98 paints an active window's (WM_NCPAINT): its
// bar shaded from COLOR_ACTIVECAPTION to COLOR_GRADIENTACTIVECAPTION, the
// title in bold in COLOR_CAPTIONTEXT, and its buttons. A window's icon
// isn't drawn: its picture isn't read.
void Gdi::paintCaption(Cpu &c, const Target &t, uint32_t window, const std::optional<std::array<int, 4>> &clip) {
    const Win32Process::Window *w = _process.window(window);
    const auto bar = _process.captionArea(window);
    if (!w || !bar)
        return;
    const auto [x, y, width, height] = *bar;
    auto put = [&](int px, int py, uint32_t colorRef) {
        if (!clip ||
            (px >= (*clip)[0] && px < (*clip)[0] + (*clip)[2] && py >= (*clip)[1] && py < (*clip)[1] + (*clip)[3]))
            plot(c, t, px, py, colorRef);
    };
    const uint32_t from = sysColor(2);
    const uint32_t to = sysColor(27);
    for (int i = 0; i < width; ++i) {
        uint32_t shade = 0;
        for (int shift = 0; shift < 24; shift += 8) {
            const int a = int((from >> shift) & 0xff);
            const int b = int((to >> shift) & 0xff);
            shade |= uint32_t(a + (b - a) * i / std::max(1, width - 1)) << shift;
        }
        for (int j = 0; j < height; ++j)
            put(x + i, y + j, shade);
    }
    // The buttons, raised, each with its glyph in COLOR_BTNTEXT.
    int titleRight = x + width - 2;
    for (int button = 0; button < 3; ++button) {
        const auto a = _process.captionButtonArea(window, button);
        if (!a)
            continue;
        const auto [bx, by, bw, bh] = *a;
        titleRight = std::min(titleRight, bx - 2);
        for (int j = 0; j < bh; ++j)
            for (int i = 0; i < bw; ++i) {
                const bool outerDark = i == bw - 1 || j == bh - 1;
                const bool innerDark = i == bw - 2 || j == bh - 2;
                const bool outerLight = i == 0 || j == 0;
                const bool innerLight = i == 1 || j == 1;
                put(bx + i, by + j,
                    outerDark    ? sysColor(21)
                    : outerLight ? sysColor(20)
                    : innerDark  ? sysColor(16)
                    : innerLight ? sysColor(22)
                                 : sysColor(15));
            }
        const uint32_t ink = sysColor(18);
        const int gx = bx + 4;
        const int gy = by + 3;
        auto box = [&](int left, int top, int boxWidth, int boxHeight) { // a window, its top two pixels thick
            for (int i = 0; i < boxWidth; ++i)
                put(left + i, top, ink), put(left + i, top + 1, ink), put(left + i, top + boxHeight - 1, ink);
            for (int j = 0; j < boxHeight; ++j)
                put(left, top + j, ink), put(left + boxWidth - 1, top + j, ink);
        };
        if (button == 0) { // close: a cross, two pixels thick
            for (int k = 0; k < 7; ++k) {
                put(gx + k, gy + k, ink), put(gx + k + 1, gy + k, ink);
                put(gx + 6 - k, gy + k, ink), put(gx + 7 - k, gy + k, ink);
            }
        } else if (button == 1 && (w->style & 0x01000000)) { // restore (WS_MAXIMIZE): two windows
            box(gx + 2, gy - 1, 6, 5);
            box(gx, gy + 2, 6, 6);
        } else if (button == 1) { // maximize
            box(gx, gy, 9, 8);
        } else { // minimize: a bar at the foot
            for (int i = 0; i < 6; ++i)
                put(gx + i, gy + 6, ink), put(gx + i, gy + 7, ink);
        }
    }
    // The title: 8 points of a bold sans serif, the caption font.
    Object font;
    font.kind = Object::kFont;
    font.height = -11;
    font.weight = 700;
    font.face = "MS Sans Serif";
    std::array<int, 4> area{x + 2, y, titleRight, y + height};
    if (clip)
        area = {std::max(area[0], (*clip)[0]), std::max(area[1], (*clip)[1]),
                std::min(area[2], (*clip)[0] + (*clip)[2]), std::min(area[3], (*clip)[1] + (*clip)[3])};
    drawString(c, t, font, x + 3, y + (height - lineHeight(font)) / 2, w->title, sysColor(9), area);
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

// The built-in font, scaled to a font's height (whole times).
namespace {
int fontScale(int height) {
    return std::max(1, int(std::lround(std::abs(height ? height : 16) / double(font::kLineHeight))));
}
} // namespace

const Gdi::Object &Gdi::fontObject(uint32_t handle) const {
    const auto it = _objects.find(handle);
    if (it != _objects.end() && it->second.kind == Object::kFont)
        return it->second;
    return _objects.at(kStock + 13); // SYSTEM_FONT
}

ttf::Font *Gdi::proportional(const Object &f) const {
    if (f.fixed)
        return nullptr;
    const int bold = f.weight >= 600 ? 1 : 0;
    std::unique_ptr<ttf::Font> &font = _sans[bold];
    if (!font) {
        font = std::make_unique<ttf::Font>();
        std::vector<uint8_t> own = arial ? arial(bold != 0) : std::vector<uint8_t>();
        _isArial[bold] = !own.empty() && !font->load(std::move(own));
        if (!_isArial[bold] && font->load(ttf::liberationSans(bold != 0)))
            return nullptr;
    }
    return font->loaded() ? font.get() : nullptr;
}

// A font's height: above 0 its cell, the ascent and descent; below 0 its
// em, without the internal leading (as LOGFONT has them); 0 Windows'
// default, a cell of 16. The player's own Arial is drawn so, hinted as
// Windows hints it; Liberation Sans a little smaller, as Windows' Arial is
// narrower at small sizes (kArialWidth).
float Gdi::pixels(const Object &f) const {
    ttf::Font *font = proportional(f);
    if (!font)
        return float(std::abs(f.height ? f.height : 16));
    const float size = f.height < 0 ? font->sizeForPoints(float(-f.height) * 0.75f) : float(f.height ? f.height : 16);
    return _isArial[f.weight >= 600 ? 1 : 0] ? size : size * kArialWidth;
}

int Gdi::textWidth(const Object &f, const std::string &s) const {
    if (ttf::Font *font = proportional(f))
        return font->width(text::windows1252ToUtf8(s), pixels(f));
    return int(s.size()) * font::kAdvance * fontScale(f.height);
}

int Gdi::textWidth(uint32_t font, const std::string &s) const {
    return textWidth(fontObject(font), s);
}

int Gdi::lineHeight(const Object &f) const {
    if (proportional(f)) // the cell asked for
        return f.height > 0 ? f.height : f.height < 0 ? int(std::ceil(pixels(f) - 0.01f)) : 16;
    return font::kLineHeight * fontScale(f.height);
}

// The glyphs, drawn white on black, give the pixels to colour: without
// smoothing, as Windows 98 draws text, those half covered or more.
void Gdi::drawString(Cpu &c, const Target &t, const Object &f, int x, int y, const std::string &s, uint32_t colorRef,
                     const std::array<int, 4> &clip) {
    const int w = textWidth(f, s);
    if (w <= 0)
        return;
    Surface glyphs;
    glyphs.create(w, lineHeight(f), PixelFormat::kRGBA32);
    if (ttf::Font *font = proportional(f))
        font->draw(glyphs, 0, 0, text::windows1252ToUtf8(s), pixels(f), {255, 255, 255});
    else
        font::drawText(glyphs, 0, 0, s, {255, 255, 255}, fontScale(f.height));
    for (int gy = 0; gy < glyphs.h; ++gy)
        for (int gx = 0; gx < glyphs.w; ++gx) {
            const int px = x + gx;
            const int py = y + gy;
            if (glyphs.row(gy)[gx * 4] >= 128 && px >= clip[0] && px < clip[2] && py >= clip[1] && py < clip[3])
                plot(c, t, px, py, colorRef);
        }
}

// DrawTextA: lines broken at line feeds and (DT_WORDBREAK) to fit the
// width. Returns the text's height.
int Gdi::drawText(Cpu &c, Dc &d, const std::string &raw, int left, int top, int right, int bottom, uint32_t format,
                  uint32_t rect) {
    const Object &f = fontObject(d.font);
    const int lh = lineHeight(f);
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
        if ((format & 0x10) && !single && textWidth(f, current) > width && current.size() > 1) {
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
    const int height = lh * int(lines.size());
    if (format & 0x400) { // DT_CALCRECT: the rectangle the text needs
        int widest = 0;
        for (const std::string &l : lines)
            widest = std::max(widest, textWidth(f, l));
        c.write32(rect + 8, uint32_t(left + widest));
        c.write32(rect + 12, uint32_t(top + height));
        return height;
    }
    const auto target = targetOf(d);
    if (!target)
        return height;
    int y = top;
    if (single && (format & 0x4))
        y = top + (bottom - top - lh) / 2;
    else if (single && (format & 0x8))
        y = bottom - lh;
    for (const std::string &l : lines) {
        const int w = textWidth(f, l);
        int x = left;
        if (format & 0x1)
            x = left + (width - w) / 2;
        else if (format & 0x2)
            x = right - w;
        if (d.bkMode == 2)
            fill(c, *target, x, y, x + w, y + lh, d.bkColor);
        drawString(c, *target, f, x, y, l, d.textColor, {left, top, right, bottom});
        y += lh;
    }
    return height;
}

// The parent's brush and text colour, else white (COLOR_WINDOW) and black;
// the text from a margin of 1 pixel, the selection in the highlight's
// colours (COLOR_HIGHLIGHT, COLOR_HIGHLIGHTTEXT) and a caret of a pixel's
// width while focused, as Windows 98's.
void Gdi::paintEdit(uint32_t window, const std::string &text, int selStart, int selEnd, bool focused, uint32_t font,
                    uint32_t dcHandle, uint32_t brush) {
    const auto t = screen ? screen() : std::nullopt;
    const auto area = _process.clientArea(window);
    if (!t || !area || !_process.shown(window))
        return;
    Cpu &c = _process.cpu();
    const auto [ax, ay, aw, ah] = *area;
    const std::array<int, 4> clip{ax, ay, ax + aw, ay + ah};
    uint32_t back = 0xffffff;
    bool filled = true;
    if (brush) {
        const auto b = _objects.find(brush);
        filled = b != _objects.end() && b->second.kind == Object::kBrush && b->second.style != 1; // not BS_NULL
        if (filled)
            back = b->second.color;
    }
    const Dc *d = brush ? dc(dcHandle) : nullptr;
    const uint32_t ink = d ? d->textColor : 0x000000;
    if (filled)
        fill(c, *t, ax, ay, ax + aw, ay + ah, back);
    const Object &f = fontObject(font);
    const int x = ax + 1;
    const int y = ay + std::max(0, (ah - lineHeight(f)) / 2);
    const int n = int(text.size());
    const int from = std::clamp(std::min(selStart, selEnd), 0, n);
    const int to = std::clamp(std::max(selStart, selEnd), 0, n);
    const std::string before = text.substr(0, size_t(from));
    const std::string chosen = text.substr(size_t(from), size_t(to - from));
    const std::string after = text.substr(size_t(to));
    drawString(c, *t, f, x, y, before, ink, clip);
    const int chosenX = x + textWidth(f, before);
    if (focused && !chosen.empty())
        fill(c, *t, chosenX, y, chosenX + textWidth(f, chosen), y + lineHeight(f), sysColor(13)); // COLOR_HIGHLIGHT
    drawString(c, *t, f, chosenX, y, chosen, focused ? sysColor(14) : ink, clip);
    drawString(c, *t, f, chosenX + textWidth(f, chosen), y, after, ink, clip);
    if (focused && from == to) {
        const int caretX = x + textWidth(f, text.substr(0, size_t(std::clamp(selEnd, 0, n))));
        fill(c, *t, caretX, y, caretX + 1, y + lineHeight(f), ink);
    }
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
        // FIXED_PITCH (1), or a face Windows has only of fixed pitch.
        std::string face = o.face;
        for (char &ch : face)
            ch = char(std::tolower(static_cast<unsigned char>(ch)));
        o.fixed = (c.arg(12) & 3) == 1 || face == "courier" || face == "courier new" || face == "fixedsys" ||
                  face == "terminal" || face == "lucida console";
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
    p.provideStdcall(u, "GetDC", 1, [this](Cpu &c) { return windowDc(c.arg(0)); });
    p.provideStdcall(u, "ReleaseDC", 2, [this](Cpu &c) {
        const auto it = _dcs.find(c.arg(1));
        if (it == _dcs.end() || !it->second.window)
            return 0u;
        _dcs.erase(it);
        return 1u;
    });
    // BeginPaint(window, PAINTSTRUCT*): the window's DC, its whole client area to paint.
    p.provideStdcall(u, "BeginPaint", 2, [this](Cpu &c) {
        Dc d;
        d.window = true;
        d.windowHandle = c.arg(0);
        d.brush = kStock;
        d.pen = kStock + 7;
        d.font = kStock + 13;
        const uint32_t h = _nextDc++;
        _dcs[h] = d;
        const uint32_t ps = c.arg(1);
        if (c.valid(ps, 64)) {
            std::memset(c.memory().data() + ps, 0, 64);
            c.write32(ps, h);
            const auto area = _process.clientArea(c.arg(0));
            c.write32(ps + 16, uint32_t(area ? (*area)[2] : _process.screenWidth));
            c.write32(ps + 20, uint32_t(area ? (*area)[3] : _process.screenHeight));
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
