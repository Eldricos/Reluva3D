// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// GDI32's text and bitmaps on device contexts that draw into something of
// the host's (addDeviceContext: a DirectDraw surface's GetDC), as
// Microsoft documents them: fonts, text in Reluva's own font (its size
// the font's), a bitmap stretched onto it; and palettes, kept as handles,
// as no system palette is shown. On another device context (a window's)
// they draw nothing.

#include "emulation/x86/win32.h"

#include "graphics/font.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace reluva::x86 {

namespace {

constexpr uint32_t kTransparent = 1; // SetBkMode's TRANSPARENT
constexpr uint32_t kEtoOpaque = 0x2;
constexpr uint32_t kEtoClipped = 0x4;
constexpr uint32_t kSrcCopy = 0xcc0020;
constexpr uint32_t kDefaultPalette = 0x2800f; // GetStockObject(DEFAULT_PALETTE)

// A COLORREF (0x00BBGGRR) as 0x00RRGGBB.
uint32_t rgbOf(uint32_t colorRef) {
    return (colorRef & 0xff) << 16 | (colorRef & 0xff00) | (colorRef >> 16 & 0xff);
}

} // namespace

uint32_t Win32Process::addDeviceContext(DrawTarget target) {
    const uint32_t dc = _nextDeviceContext++;
    _deviceContexts[dc].target = std::move(target);
    return dc;
}

void Win32Process::removeDeviceContext(uint32_t dc) {
    _deviceContexts.erase(dc);
}

void Win32Process::addGdiStandIns() {
    const char *g = "GDI32.dll";
    auto context = [this](uint32_t dc) {
        const auto it = _deviceContexts.find(dc);
        return it == _deviceContexts.end() ? nullptr : &it->second;
    };
    // The built-in font's scale for a device context's font: its height
    // over the font's own line (or character) height, at least 1.
    auto scaleOf = [this](const DeviceContext &d) {
        const auto it = _gdiObjects.find(d.font);
        if (it == _gdiObjects.end() || !it->second.height)
            return 1;
        const int h = it->second.height;
        return std::max(1, h < 0 ? (-h + 4) / 9 : (h + 5) / font::kLineHeight);
    };

    // CreateFontA(height, width, escapement, orientation, weight, italic,
    // underline, strike out, charset, precisions, quality, pitch, face):
    // only its height matters here.
    stdcall(g, "CreateFontA", 14, [this](Cpu &c) {
        const uint32_t handle = _nextGdiObject++;
        _gdiObjects[handle] = {true, int(int32_t(c.arg(0)))};
        return handle;
    });
    stdcall(g, "CreatePalette", 1, [this](Cpu &) {
        const uint32_t handle = _nextGdiObject++;
        _gdiObjects[handle] = {};
        return handle;
    });
    stdcall(g, "DeleteObject", 1, [this](Cpu &c) { return _gdiObjects.erase(c.arg(0)) ? 1u : 0u; });
    // SelectObject(dc, object): the one of its kind it replaces.
    stdcall(g, "SelectObject", 2, [this, context](Cpu &c) {
        DeviceContext *d = context(c.arg(0));
        const auto it = _gdiObjects.find(c.arg(1));
        if (!d || it == _gdiObjects.end() || !it->second.font)
            return 0u;
        return std::exchange(d->font, c.arg(1));
    });
    stdcall(g, "SelectPalette", 3, [context](Cpu &c) {
        DeviceContext *d = context(c.arg(0));
        if (!d)
            return kDefaultPalette;
        const uint32_t was = std::exchange(d->palette, c.arg(1));
        return was ? was : kDefaultPalette;
    });
    stdcall(g, "RealizePalette", 1, [](Cpu &) { return 0u; }); // no entries changed the system's
    stdcall(g, "SetTextColor", 2, [context](Cpu &c) {
        DeviceContext *d = context(c.arg(0));
        return d ? std::exchange(d->textColor, c.arg(1)) : 0u;
    });
    stdcall(g, "SetBkColor", 2, [context](Cpu &c) {
        DeviceContext *d = context(c.arg(0));
        return d ? std::exchange(d->backColor, c.arg(1)) : 0xffffffu;
    });
    stdcall(g, "SetBkMode", 2, [context](Cpu &c) {
        DeviceContext *d = context(c.arg(0));
        return d ? std::exchange(d->backMode, c.arg(1)) : 2u;
    });
    // GetTextExtentPoint(32)A(dc, text, length, SIZE*): its width and height.
    auto extent = [context, scaleOf](Cpu &c) {
        const uint32_t size = c.arg(3);
        if (!c.valid(size, 8))
            return 0u;
        const DeviceContext *d = context(c.arg(0));
        const int scale = d ? scaleOf(*d) : 1;
        c.write32(size, uint32_t(int(c.arg(2)) * font::kAdvance * scale));
        c.write32(size + 4, uint32_t(font::kLineHeight * scale));
        return 1u;
    };
    stdcall(g, "GetTextExtentPoint32A", 4, extent);
    stdcall(g, "GetTextExtentPointA", 4, extent);
    // ExtTextOutA(dc, x, y, options, RECT*, text, length, spacing): the
    // rectangle filled with the background colour (ETO_OPAQUE), and the
    // text in the text colour, on its background unless it's transparent,
    // clipped to the rectangle if asked (ETO_CLIPPED).
    stdcall(g, "ExtTextOutA", 8, [context, scaleOf](Cpu &c) {
        DeviceContext *d = context(c.arg(0));
        if (!d)
            return 1u;
        const DrawTarget &t = d->target;
        const int x = int(int32_t(c.arg(1)));
        const int y = int(int32_t(c.arg(2)));
        const uint32_t options = c.arg(3);
        int clip[4] = {0, 0, t.width, t.height};
        const bool hasRect = c.arg(4) && c.valid(c.arg(4), 16);
        int rect[4] = {};
        if (hasRect)
            for (uint32_t i = 0; i < 4; ++i)
                rect[i] = int(int32_t(c.read32(c.arg(4) + 4 * i)));
        auto put = [&](int px, int py, uint32_t rgb) {
            if (px >= clip[0] && py >= clip[1] && px < clip[2] && py < clip[3])
                t.put(px, py, rgb);
        };
        if (hasRect && (options & kEtoOpaque))
            for (int py = rect[1]; py < rect[3]; ++py)
                for (int px = rect[0]; px < rect[2]; ++px)
                    put(px, py, rgbOf(d->backColor));
        if (hasRect && (options & kEtoClipped)) {
            clip[0] = std::max(clip[0], rect[0]);
            clip[1] = std::max(clip[1], rect[1]);
            clip[2] = std::min(clip[2], rect[2]);
            clip[3] = std::min(clip[3], rect[3]);
        }
        const std::string text = c.valid(c.arg(5), c.arg(6)) ? c.string(c.arg(5), c.arg(6)) : std::string();
        const int scale = scaleOf(*d);
        Surface picture;
        picture.create(std::max(1, int(text.size()) * font::kAdvance * scale), font::kLineHeight * scale,
                       PixelFormat::kRGBA32);
        font::drawText(picture, 0, 0, text, Color{255, 255, 255}, scale);
        for (int py = 0; py < picture.h; ++py)
            for (int px = 0; px < picture.w; ++px) {
                const bool ink = picture.row(py)[px * 4 + 3] != 0;
                if (ink)
                    put(x + px, y + py, rgbOf(d->textColor));
                else if (d->backMode != kTransparent)
                    put(x + px, y + py, rgbOf(d->backColor));
            }
        return 1u;
    });
    // StretchDIBits(dc, x, y, width, height, from x, from y, from width,
    // from height, bits, BITMAPINFO*, usage, raster operation): a
    // device-independent bitmap (8 bits with its colours, 16 as 555, 24 or
    // 32, bottom-up unless its height is negative) copied onto it,
    // stretched; only SRCCOPY. Returns the lines copied.
    stdcall(g, "StretchDIBits", 13, [context](Cpu &c) -> uint32_t {
        const int fromHeight = int(int32_t(c.arg(8)));
        DeviceContext *d = context(c.arg(0));
        const uint32_t info = c.arg(10);
        if (!d || c.arg(12) != kSrcCopy || !c.valid(info, 40))
            return uint32_t(fromHeight);
        const int width = int(int32_t(c.read32(info + 4)));
        const int rawHeight = int(int32_t(c.read32(info + 8)));
        const int height = std::abs(rawHeight);
        const int bits = c.read16(info + 14);
        const uint32_t colours = info + c.read32(info);
        if (width <= 0 || height <= 0 || (bits != 8 && bits != 16 && bits != 24 && bits != 32))
            return 0u;
        const uint32_t stride = uint32_t((width * bits / 8 + 3) & ~3);
        if (!c.valid(c.arg(9), stride * uint32_t(height)))
            return 0u;
        const int toX = int(int32_t(c.arg(1)));
        const int toY = int(int32_t(c.arg(2)));
        const int toWidth = int(int32_t(c.arg(3)));
        const int toHeight = int(int32_t(c.arg(4)));
        const int fromX = int(int32_t(c.arg(5)));
        const int fromY = int(int32_t(c.arg(6)));
        const int fromWidth = int(int32_t(c.arg(7)));
        if (!toWidth || !toHeight)
            return uint32_t(fromHeight);
        auto source = [&](int sx, int sy) -> uint32_t { // 0x00RRGGBB, sy from the top
            const int row = rawHeight > 0 ? height - 1 - sy : sy;
            const uint32_t at = c.arg(9) + uint32_t(row) * stride;
            switch (bits) {
            case 8:
                return c.read32(colours + 4u * c.read8(at + uint32_t(sx))) & 0xffffff; // RGBQUAD: B, G, R
            case 16: {
                const uint32_t v = c.read16(at + 2u * uint32_t(sx));
                return (v >> 10 & 31) * 255 / 31 << 16 | (v >> 5 & 31) * 255 / 31 << 8 | (v & 31) * 255 / 31;
            }
            case 24:
                return c.read8(at + 3u * uint32_t(sx)) | c.read8(at + 3u * uint32_t(sx) + 1) << 8 |
                       c.read8(at + 3u * uint32_t(sx) + 2) << 16;
            default:
                return c.read32(at + 4u * uint32_t(sx)) & 0xffffff;
            }
        };
        const DrawTarget &t = d->target;
        for (int py = 0; py < std::abs(toHeight); ++py)
            for (int px = 0; px < std::abs(toWidth); ++px) {
                const int sx = fromX + px * fromWidth / std::abs(toWidth);
                const int sy = fromY + py * fromHeight / std::abs(toHeight);
                const int dx = toX + (toWidth < 0 ? -px : px);
                const int dy = toY + (toHeight < 0 ? -py : py);
                if (sx < 0 || sy < 0 || sx >= width || sy >= height || dx < 0 || dy < 0 || dx >= t.width ||
                    dy >= t.height)
                    continue;
                t.put(dx, dy, source(sx, sy));
            }
        return uint32_t(fromHeight);
    });
}

} // namespace reluva::x86
