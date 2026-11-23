// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// GDI32 (and USER32's drawing), as Microsoft documents them: bitmaps in
// colour or monochrome, brushes and pens, and device contexts drawing on a
// bitmap or, for a window, on the screen (screenAccess: DirectX's primary
// surface while a program owns the display). Blits take any of the 256
// raster operations, converting between colour and monochrome as Windows
// does. Text is measured, by a stand-in font of 8 x 16 pixels a character,
// but not drawn.

#include "emulation/x86/win32.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace reluva::x86 {

namespace {

// Handles: GDI's stock objects (by GetStockObject's index), the device
// contexts of windows (by the window's low 16 bits), and what the program
// makes, counted up.
constexpr uint32_t kStockObjects = 0x28000;
constexpr uint32_t kDeviceContext = 0x30000;
constexpr uint32_t kGdiObjects = 0x38000;
// A memory device context's first bitmap: 1 x 1, monochrome. (No stock
// object has index 21.)
constexpr uint32_t kDefaultBitmap = kStockObjects + 21;
constexpr uint32_t kDesktopWindow = 0x0001000e;
constexpr uint32_t kWhiteBrush = 0;
constexpr uint32_t kNullBrush = 5;
constexpr uint32_t kWhitePen = 6;
constexpr uint32_t kBlackPen = 7;
constexpr uint32_t kNullPen = 8;
constexpr uint32_t kSystemFont = 13;
constexpr uint32_t kDefaultPalette = 15;
constexpr uint32_t kClrInvalid = 0xffffffffu;
constexpr int kCharWidth = 8;
constexpr int kCharHeight = 16;

// A COLORREF (0x00BBGGRR) as a pixel (0xRRGGBB), and back.
uint32_t pixelOf(uint32_t colour) {
    return (colour & 0xff) << 16 | (colour & 0xff00) | (colour >> 16 & 0xff);
}
uint32_t colourOf(uint32_t pixel) {
    return pixelOf(pixel);
}

// A ternary raster operation (its index, the code's third byte) on three
// pixels, bit by bit: bit (P << 2 | S << 1 | D) of the index is the result.
uint32_t rop3(uint8_t index, uint32_t p, uint32_t s, uint32_t d) {
    uint32_t r = 0;
    for (int i = 0; i < 8; ++i)
        if (index >> i & 1)
            r |= (i & 4 ? p : ~p) & (i & 2 ? s : ~s) & (i & 1 ? d : ~d);
    return r & 0xffffff;
}

/**
 * Decodes a DIB (BITMAPINFOHEADER, its colours or masks, then the rows of
 * `bits`) into `out`, top row first. Only scan lines [first, first +
 * count), counted from the DIB's first row, are in `bits`; others are 0.
 */
bool decodeDib(const uint8_t *info, size_t infoSize, const uint8_t *bits, size_t bitsSize, int first, int count,
               int &width, int &height, int &depth, std::vector<uint32_t> &out) {
    auto le16 = [&](size_t at) { return at + 2 <= infoSize ? uint32_t(info[at] | info[at + 1] << 8) : 0u; };
    auto le32 = [&](size_t at) { return le16(at) | le16(at + 2) << 16; };
    const uint32_t headerSize = le32(0);
    if (headerSize < 40 || headerSize > infoSize)
        return false;
    width = int32_t(le32(4));
    const int32_t rawHeight = int32_t(le32(8));
    depth = int(le16(14));
    const uint32_t compression = le32(16);
    const bool topDown = rawHeight < 0;
    height = topDown ? -rawHeight : rawHeight;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || (compression != 0 && compression != 3))
        return false;
    if (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 && depth != 32)
        return false;
    // The colours (RGBQUADs) of 1, 4 and 8 bits; BI_BITFIELDS' masks.
    std::vector<uint32_t> colours;
    if (depth <= 8) {
        const uint32_t used = le32(32) ? le32(32) : 1u << depth;
        for (uint32_t i = 0; i < used && headerSize + 4 * i + 4 <= infoSize; ++i)
            colours.push_back(le32(headerSize + 4 * i) & 0xffffff);
    }
    uint32_t masks[3] = {depth == 16 ? 0x7c00u : 0xff0000u, depth == 16 ? 0x03e0u : 0xff00u,
                         depth == 16 ? 0x001fu : 0xffu};
    if (compression == 3)
        for (int i = 0; i < 3; ++i)
            masks[i] = le32(headerSize + 4 * size_t(i));
    auto channel = [](uint32_t value, uint32_t mask) -> uint32_t {
        if (!mask)
            return 0;
        int shift = 0;
        while (!(mask >> shift & 1))
            ++shift;
        const uint32_t top = mask >> shift;
        return ((value & mask) >> shift) * 255 / top;
    };
    const size_t stride = (size_t(width) * size_t(depth) + 31) / 32 * 4;
    out.assign(size_t(width) * size_t(height), 0);
    for (int scan = std::max(first, 0); scan < first + count && scan < height; ++scan) {
        const size_t at = size_t(scan - first) * stride;
        if (at + stride > bitsSize)
            break;
        const uint8_t *row = bits + at;
        uint32_t *to = out.data() + size_t(topDown ? scan : height - 1 - scan) * size_t(width);
        for (int x = 0; x < width; ++x) {
            uint32_t px = 0;
            switch (depth) {
            case 1:
            case 4:
            case 8: {
                const size_t bit = size_t(x) * size_t(depth);
                const uint32_t index = (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
                px = index < colours.size() ? colours[index] : 0;
                break;
            }
            case 16: {
                const uint32_t v = uint32_t(row[2 * x] | row[2 * x + 1] << 8);
                px = channel(v, masks[0]) << 16 | channel(v, masks[1]) << 8 | channel(v, masks[2]);
                break;
            }
            case 24:
                px = uint32_t(row[3 * x + 2]) << 16 | uint32_t(row[3 * x + 1]) << 8 | row[3 * x];
                break;
            default: {
                const uint32_t v =
                    uint32_t(row[4 * x] | row[4 * x + 1] << 8 | row[4 * x + 2] << 16) | uint32_t(row[4 * x + 3]) << 24;
                px = compression == 3 ? channel(v, masks[0]) << 16 | channel(v, masks[1]) << 8 | channel(v, masks[2])
                                      : v & 0xffffff;
            }
            }
            to[x] = px;
        }
    }
    return true;
}

// The colours GetSysColor gives (COLOR_ indices), as Windows 98's
// standard scheme has them; a brush of `index + 1` is the colour's.
uint32_t systemColour(uint32_t index) {
    switch (index) {
    case 5:  // COLOR_WINDOW
    case 14: // COLOR_HIGHLIGHTTEXT
        return 0xffffff;
    case 6:  // COLOR_WINDOWFRAME
    case 7:  // COLOR_MENUTEXT
    case 8:  // COLOR_WINDOWTEXT
    case 18: // COLOR_BTNTEXT
        return 0x000000;
    case 1: // COLOR_BACKGROUND
        return 0x808000;
    case 13: // COLOR_HIGHLIGHT
    case 2:  // COLOR_ACTIVECAPTION
        return 0x800000;
    case 16: // COLOR_BTNSHADOW
        return 0x808080;
    default: // COLOR_BTNFACE, COLOR_MENU and the rest
        return 0xc0c0c0;
    }
}

} // namespace

Win32Process::DeviceContext *Win32Process::deviceContext(uint32_t handle) {
    if (auto it = _dcs.find(handle); it != _dcs.end())
        return &it->second;
    if (handle < kDeviceContext || handle >= kGdiObjects)
        return nullptr;
    // A window's, until ReleaseDC: its selection starts as the defaults.
    const uint32_t window = 0x10000 | (handle & 0xffff);
    if (window != kDesktopWindow && !_windows.count(window))
        return nullptr;
    DeviceContext &dc = _dcs[handle];
    dc.window = window;
    dc.brush = kStockObjects + kWhiteBrush;
    dc.pen = kStockObjects + kBlackPen;
    dc.font = kStockObjects + kSystemFont;
    return &dc;
}

bool Win32Process::monochrome(const DeviceContext &dc) const {
    if (dc.window)
        return false;
    auto it = _bitmaps.find(dc.bitmap);
    return it == _bitmaps.end() || it->second.bits == 1;
}

std::vector<uint32_t> Win32Process::readArea(const DeviceContext &dc, int x, int y, int width, int height) {
    std::vector<uint32_t> out(size_t(std::max(width, 0)) * size_t(std::max(height, 0)), 0);
    if (out.empty())
        return out;
    if (dc.window) {
        // The screen, at the window's client area (not clipped to it).
        if (dc.window != kDesktopWindow)
            if (auto area = clientArea(dc.window)) {
                x += (*area)[0];
                y += (*area)[1];
            }
        const int x0 = std::max(x, 0);
        const int y0 = std::max(y, 0);
        const int x1 = std::min(x + width, screenWidth);
        const int y1 = std::min(y + height, screenHeight);
        if (x0 >= x1 || y0 >= y1)
            return out;
        std::vector<uint32_t> part(size_t(x1 - x0) * size_t(y1 - y0));
        if (screenAccess) {
            screenAccess(x0, y0, x1 - x0, y1 - y0, part.data(), false);
        } else {
            _screen.resize(size_t(screenWidth) * size_t(screenHeight), 0);
            for (int r = y0; r < y1; ++r)
                std::copy_n(_screen.begin() + r * screenWidth + x0, x1 - x0, part.begin() + (r - y0) * (x1 - x0));
        }
        for (int r = y0; r < y1; ++r)
            std::copy_n(part.begin() + (r - y0) * (x1 - x0), x1 - x0,
                        out.begin() + size_t(r - y) * size_t(width) + size_t(x0 - x));
        return out;
    }
    auto it = _bitmaps.find(dc.bitmap);
    if (it == _bitmaps.end())
        return out;
    const Bitmap &b = it->second;
    for (int r = std::max(y, 0); r < std::min(y + height, b.height); ++r)
        for (int col = std::max(x, 0); col < std::min(x + width, b.width); ++col)
            out[size_t(r - y) * size_t(width) + size_t(col - x)] = b.pixels[size_t(r) * size_t(b.width) + size_t(col)];
    return out;
}

void Win32Process::writeArea(const DeviceContext &dc, int x, int y, int width, int height,
                             const std::vector<uint32_t> &pixels) {
    if (width <= 0 || height <= 0)
        return;
    if (dc.window) {
        if (dc.window != kDesktopWindow)
            if (auto area = clientArea(dc.window)) {
                x += (*area)[0];
                y += (*area)[1];
            }
        const int x0 = std::max(x, 0);
        const int y0 = std::max(y, 0);
        const int x1 = std::min(x + width, screenWidth);
        const int y1 = std::min(y + height, screenHeight);
        if (x0 >= x1 || y0 >= y1)
            return;
        std::vector<uint32_t> part(size_t(x1 - x0) * size_t(y1 - y0));
        for (int r = y0; r < y1; ++r)
            std::copy_n(pixels.begin() + size_t(r - y) * size_t(width) + size_t(x0 - x), x1 - x0,
                        part.begin() + (r - y0) * (x1 - x0));
        if (screenAccess) {
            screenAccess(x0, y0, x1 - x0, y1 - y0, part.data(), true);
        } else {
            _screen.resize(size_t(screenWidth) * size_t(screenHeight), 0);
            for (int r = y0; r < y1; ++r)
                std::copy_n(part.begin() + (r - y0) * (x1 - x0), x1 - x0, _screen.begin() + r * screenWidth + x0);
        }
        return;
    }
    auto it = _bitmaps.find(dc.bitmap);
    if (it == _bitmaps.end())
        return;
    Bitmap &b = it->second;
    for (int r = std::max(y, 0); r < std::min(y + height, b.height); ++r)
        for (int col = std::max(x, 0); col < std::min(x + width, b.width); ++col) {
            uint32_t px = pixels[size_t(r - y) * size_t(width) + size_t(col - x)];
            if (b.bits == 1)
                px = px ? 0xffffff : 0;
            b.pixels[size_t(r) * size_t(b.width) + size_t(col)] = px;
        }
}

void Win32Process::addGdiStandIns() {
    const char *u = "USER32.dll";
    const char *g = "GDI32.dll";
    _bitmaps[kDefaultBitmap] = {1, 1, 1, {0}};

    // Device contexts. A window's is its handle's low 16 bits after
    // kDeviceContext (the desktop's for none), drawing on the screen at its
    // client area; ReleaseDC forgets what was selected into it.
    stdcall(u, "GetDC", 1, [](Cpu &c) { return kDeviceContext + ((c.arg(0) ? c.arg(0) : kDesktopWindow) & 0xffff); });
    stdcall(u, "ReleaseDC", 2, [this](Cpu &c) {
        _dcs.erase(c.arg(1));
        return 1u;
    });
    // BeginPaint(window, PAINTSTRUCT*): its device context, and the whole
    // client area to paint; the window is then painted.
    stdcall(u, "BeginPaint", 2, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        const uint32_t ps = c.arg(1);
        const auto area = clientArea(c.arg(0));
        if (it == _windows.end() || !area || !c.valid(ps, 64))
            return 0u;
        it->second.invalid = false;
        const uint32_t dc = kDeviceContext + (c.arg(0) & 0xffff);
        c.write32(ps, dc);
        c.write32(ps + 4, 0); // fErase: the background is left alone
        c.write32(ps + 8, 0);
        c.write32(ps + 12, 0);
        c.write32(ps + 16, uint32_t((*area)[2]));
        c.write32(ps + 20, uint32_t((*area)[3]));
        return dc;
    });
    stdcall(u, "EndPaint", 2, [](Cpu &) { return 1u; });
    stdcall(g, "CreateCompatibleDC", 1, [this](Cpu &) {
        const uint32_t handle = kGdiObjects + _nextGdiObject++;
        DeviceContext &dc = _dcs[handle];
        dc.bitmap = kDefaultBitmap;
        dc.brush = kStockObjects + kWhiteBrush;
        dc.pen = kStockObjects + kBlackPen;
        dc.font = kStockObjects + kSystemFont;
        return handle;
    });
    stdcall(g, "DeleteDC", 1, [this](Cpu &c) { return _dcs.erase(c.arg(0)) ? 1u : 0u; });
    // GetDeviceCaps(dc, index): the desktop's.
    stdcall(g, "GetDeviceCaps", 2, [this](Cpu &c) {
        switch (c.arg(1)) {
        case 8:
            return uint32_t(screenWidth); // HORZRES
        case 10:
            return uint32_t(screenHeight); // VERTRES
        case 12:
            return uint32_t(screenBits); // BITSPIXEL
        case 14:
            return 1u; // PLANES
        case 24:
            return screenBits <= 8 ? 256u : ~0u; // NUMCOLORS: -1 above 8 bits
        case 38:
            return screenBits <= 8 ? 0x100u : 0u; // RASTERCAPS: RC_PALETTE
        case 88:
        case 90:
            return 96u; // LOGPIXELSX, LOGPIXELSY
        case 104:
            return screenBits <= 8 ? 256u : 0u; // SIZEPALETTE
        case 116:
            return 60u; // VREFRESH
        default:
            return 0u;
        }
    });
    // GetSystemPaletteEntries: none unless the desktop has a palette, when
    // they're greys.
    stdcall(g, "GetSystemPaletteEntries", 4, [this](Cpu &c) {
        const uint32_t start = c.arg(1);
        const uint32_t count = c.arg(2);
        const uint32_t out = c.arg(3);
        if (screenBits > 8)
            return 0u;
        if (!out)
            return 256u;
        uint32_t n = 0;
        for (; n < count && start + n < 256 && c.valid(out + 4 * n, 4); ++n) {
            const uint32_t v = start + n;
            c.write32(out + 4 * n, v | v << 8 | v << 16);
        }
        return n;
    });

    // Objects. Palettes are kept as handles only: the screen has no palette
    // above 8 bits.
    stdcall(g, "GetStockObject", 1, [](Cpu &c) { return c.arg(0) <= 17 ? kStockObjects + c.arg(0) : 0u; });
    // CreateCompatibleBitmap(dc, width, height): monochrome for a memory
    // device context that still has a monochrome bitmap, as Windows makes
    // it; the screen's colours otherwise.
    stdcall(g, "CreateCompatibleBitmap", 3, [this](Cpu &c) {
        const DeviceContext *dc = deviceContext(c.arg(0));
        const int width = int32_t(c.arg(1));
        const int height = int32_t(c.arg(2));
        if (!dc || width < 0 || height < 0 || int64_t(width) * height > (64 << 20))
            return 0u;
        const uint32_t handle = kGdiObjects + _nextGdiObject++;
        if (width == 0 || height == 0)
            _bitmaps[handle] = {1, 1, 1, {0}};
        else
            _bitmaps[handle] = {width, height, monochrome(*dc) ? 1 : screenBits,
                                std::vector<uint32_t>(size_t(width) * size_t(height), 0)};
        return handle;
    });
    // CreateBitmap(width, height, planes, bits, data): monochrome with one
    // plane of 1 bit, its rows a multiple of 16 bits; 32-bit rows read as
    // BGRX; others start black.
    stdcall(g, "CreateBitmap", 5, [this](Cpu &c) {
        const int width = int32_t(c.arg(0));
        const int height = int32_t(c.arg(1));
        const uint32_t bits = c.arg(2) * c.arg(3);
        if (width <= 0 || height <= 0 || int64_t(width) * height > (64 << 20))
            return 0u;
        Bitmap b{width, height, int(bits == 1 ? 1 : bits), std::vector<uint32_t>(size_t(width) * size_t(height), 0)};
        if (const uint32_t data = c.arg(4)) {
            const uint32_t stride = bits == 1 ? uint32_t(width + 15) / 16 * 2 : uint32_t(width) * 4;
            if ((bits == 1 || bits == 32) && c.valid(data, stride * uint32_t(height)))
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x) {
                        const uint32_t row = data + uint32_t(y) * stride;
                        b.pixels[size_t(y) * size_t(width) + size_t(x)] =
                            bits == 1 ? (c.read8(row + uint32_t(x) / 8) >> (7 - x % 8) & 1 ? 0xffffffu : 0u)
                                      : c.read32(row + 4 * uint32_t(x)) & 0xffffff;
                    }
        }
        const uint32_t handle = kGdiObjects + _nextGdiObject++;
        _bitmaps[handle] = std::move(b);
        return handle;
    });
    stdcall(g, "CreateSolidBrush", 1, [this](Cpu &c) {
        const uint32_t handle = kGdiObjects + _nextGdiObject++;
        _brushes[handle].colour = c.arg(0);
        return handle;
    });
    // CreatePatternBrush(bitmap): the brush keeps a copy of the bitmap.
    stdcall(g, "CreatePatternBrush", 1, [this](Cpu &c) {
        auto it = _bitmaps.find(c.arg(0));
        if (it == _bitmaps.end())
            return 0u;
        const uint32_t handle = kGdiObjects + _nextGdiObject++;
        Brush &b = _brushes[handle];
        b.patternWidth = it->second.width;
        b.patternHeight = it->second.height;
        b.pattern = it->second.pixels;
        return handle;
    });
    stdcall(g, "CreatePen", 3, [this](Cpu &c) {
        const uint32_t handle = kGdiObjects + _nextGdiObject++;
        _pens[handle] = {c.arg(2), c.arg(0) == 5}; // PS_NULL
        return handle;
    });
    stdcall(g, "CreateFontIndirectA", 1, [this](Cpu &) {
        const uint32_t handle = kGdiObjects + _nextGdiObject++;
        _fonts.insert(handle);
        return handle;
    });
    stdcall(g, "CreatePalette", 1, [this](Cpu &) {
        const uint32_t handle = kGdiObjects + _nextGdiObject++;
        _palettes.insert(handle);
        return handle;
    });
    // Stock objects can't be deleted, but saying so is no failure.
    stdcall(g, "DeleteObject", 1, [this](Cpu &c) {
        const uint32_t h = c.arg(0);
        if (h >= kStockObjects && h < kDeviceContext)
            return 1u;
        return _bitmaps.erase(h) + _brushes.erase(h) + _pens.erase(h) + _fonts.erase(h) + _palettes.erase(h) ? 1u : 0u;
    });
    // SelectObject(dc, object): what was selected of its kind before.
    stdcall(g, "SelectObject", 2, [this](Cpu &c) {
        DeviceContext *dc = deviceContext(c.arg(0));
        const uint32_t h = c.arg(1);
        if (!dc)
            return 0u;
        const uint32_t stock = h - kStockObjects;
        if (_bitmaps.count(h))
            return dc->window ? 0u : std::exchange(dc->bitmap, h);
        if (_brushes.count(h) || stock <= kNullBrush)
            return std::exchange(dc->brush, h);
        if (_pens.count(h) || (stock >= kWhitePen && stock <= kNullPen))
            return std::exchange(dc->pen, h);
        if (_fonts.count(h) || (stock >= 10 && stock <= 17 && stock != kDefaultPalette))
            return std::exchange(dc->font, h);
        return 0u;
    });
    stdcall(g, "SelectPalette", 3, [](Cpu &) { return kStockObjects + kDefaultPalette; });
    stdcall(g, "RealizePalette", 1, [](Cpu &) { return 0u; });
    // GetObjectA(handle, size, out): a BITMAP, or a brush's LOGBRUSH.
    stdcall(g, "GetObjectA", 3, [this](Cpu &c) {
        const uint32_t out = c.arg(2);
        if (auto it = _bitmaps.find(c.arg(0)); it != _bitmaps.end()) {
            if (!out)
                return 24u;
            if (c.arg(1) < 24 || !c.valid(out, 24))
                return 0u;
            const Bitmap &b = it->second;
            c.write32(out, 0);                                                 // bmType
            c.write32(out + 4, uint32_t(b.width));                             // bmWidth
            c.write32(out + 8, uint32_t(b.height));                            // bmHeight
            c.write32(out + 12, uint32_t(((b.width * b.bits + 15) / 16) * 2)); // bmWidthBytes, even
            c.write16(out + 16, 1);                                            // bmPlanes
            c.write16(out + 18, uint16_t(b.bits));                             // bmBitsPixel
            c.write32(out + 20, 0);                                            // bmBits
            return 24u;
        }
        if (auto it = _brushes.find(c.arg(0)); it != _brushes.end()) {
            if (c.arg(1) < 12 || !c.valid(out, 12))
                return 0u;
            c.write32(out, it->second.pattern.empty() ? 0u : 3u); // BS_SOLID, BS_PATTERN
            c.write32(out + 4, it->second.colour);
            c.write32(out + 8, 0);
            return 12u;
        }
        return 0u;
    });

    // The colours and mode return what they replace.
    auto dcField = [this](uint32_t handle, uint32_t DeviceContext::*field, uint32_t value) {
        DeviceContext *dc = deviceContext(handle);
        return dc ? std::exchange(dc->*field, value) : kClrInvalid;
    };
    stdcall(g, "SetTextColor", 2,
            [dcField](Cpu &c) { return dcField(c.arg(0), &DeviceContext::textColour, c.arg(1)); });
    stdcall(g, "SetBkColor", 2, [dcField](Cpu &c) { return dcField(c.arg(0), &DeviceContext::backColour, c.arg(1)); });
    stdcall(g, "SetBkMode", 2, [dcField](Cpu &c) {
        const uint32_t old = dcField(c.arg(0), &DeviceContext::backMode, c.arg(1));
        return old == kClrInvalid ? 0u : old;
    });

    // Drawing. The pattern is the selected brush, tiled from the device
    // context's origin; for a monochrome destination, light colours are
    // white.
    auto brushAt = [this](const DeviceContext &dc, uint32_t brush, int x, int y) -> uint32_t {
        uint32_t px = 0;
        if (brush - kStockObjects <= kNullBrush) {
            static const uint32_t kStock[] = {0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0, 0};
            px = kStock[brush - kStockObjects];
        } else if (brush > 0 && brush <= 31) {
            px = systemColour(brush - 1);
        } else if (auto it = _brushes.find(brush); it != _brushes.end()) {
            const Brush &b = it->second;
            px = b.pattern.empty() ? pixelOf(b.colour)
                                   : b.pattern[size_t(((y % b.patternHeight) + b.patternHeight) % b.patternHeight) *
                                                   size_t(b.patternWidth) +
                                               size_t(((x % b.patternWidth) + b.patternWidth) % b.patternWidth)];
        }
        if (monochrome(dc)) {
            const uint32_t light = (px >> 16 & 0xff) + (px >> 8 & 0xff) + (px & 0xff);
            px = light >= 3 * 128 ? 0xffffff : 0;
        }
        return px;
    };
    // A raster operation over a rectangle: the source (if it uses one),
    // converted between colour and monochrome as Windows does (monochrome
    // to colour: 0 is the destination's text colour, 1 its background;
    // colour to monochrome: the source's background colour is 1).
    auto blit = [this, brushAt](uint32_t dest, int x, int y, int width, int height, uint32_t src, int sx, int sy,
                                uint32_t rop, uint32_t brush) {
        DeviceContext *d = deviceContext(dest);
        if (!d || width <= 0 || height <= 0)
            return false;
        const uint8_t index = uint8_t(rop >> 16);
        const bool usesSource = ((index >> 2) ^ index) & 0x33;
        const bool usesDest = ((index >> 1) ^ index) & 0x55;
        const bool usesPattern = ((index >> 4) ^ index) & 0x0f;
        std::vector<uint32_t> s;
        if (usesSource) {
            DeviceContext *sdc = deviceContext(src);
            if (!sdc)
                return false;
            s = readArea(*sdc, sx, sy, width, height);
            const bool fromMono = monochrome(*sdc);
            const bool toMono = monochrome(*d);
            if (fromMono && !toMono)
                for (uint32_t &px : s)
                    px = px ? pixelOf(d->backColour) : pixelOf(d->textColour);
            else if (!fromMono && toMono)
                for (uint32_t &px : s)
                    px = px == pixelOf(sdc->backColour) ? 0xffffff : 0;
        }
        std::vector<uint32_t> out =
            usesDest ? readArea(*d, x, y, width, height) : std::vector<uint32_t>(size_t(width) * size_t(height), 0);
        for (int r = 0; r < height; ++r)
            for (int col = 0; col < width; ++col) {
                const size_t i = size_t(r) * size_t(width) + size_t(col);
                const uint32_t p = usesPattern ? brushAt(*d, brush, x + col, y + r) : 0;
                out[i] = rop3(index, p, usesSource ? s[i] : 0, out[i]);
            }
        writeArea(*d, x, y, width, height, out);
        return true;
    };
    stdcall(g, "BitBlt", 9, [this, blit](Cpu &c) {
        const DeviceContext *d = deviceContext(c.arg(0));
        return d && blit(c.arg(0), int32_t(c.arg(1)), int32_t(c.arg(2)), int32_t(c.arg(3)), int32_t(c.arg(4)), c.arg(5),
                         int32_t(c.arg(6)), int32_t(c.arg(7)), c.arg(8), d->brush)
                   ? 1u
                   : 0u;
    });
    stdcall(g, "PatBlt", 6, [this, blit](Cpu &c) {
        const DeviceContext *d = deviceContext(c.arg(0));
        return d && blit(c.arg(0), int32_t(c.arg(1)), int32_t(c.arg(2)), int32_t(c.arg(3)), int32_t(c.arg(4)), 0, 0, 0,
                         c.arg(5), d->brush)
                   ? 1u
                   : 0u;
    });
    // FillRect(dc, rect, brush): a brush, or a system colour's index + 1.
    stdcall(u, "FillRect", 3, [blit](Cpu &c) {
        const uint32_t r = c.arg(1);
        if (!c.valid(r, 16))
            return 0u;
        const int left = int32_t(c.read32(r));
        const int top = int32_t(c.read32(r + 4));
        return blit(c.arg(0), left, top, int32_t(c.read32(r + 8)) - left, int32_t(c.read32(r + 12)) - top, 0, 0, 0,
                    0xf00021, c.arg(2)) // PATCOPY
                   ? 1u
                   : 0u;
    });
    // SetDIBitsToDevice(dc, x, y, width, height, sx, sy, first scan, scans,
    // bits, BITMAPINFO, usage): `scans` rows of a DIB, from `first scan`;
    // (sx, sy) is from the DIB's lower left for a bottom-up one, as
    // documented. DIB_PAL_COLORS isn't stood in for.
    stdcall(g, "SetDIBitsToDevice", 12, [this](Cpu &c) {
        DeviceContext *dc = deviceContext(c.arg(0));
        const uint32_t info = c.arg(10);
        if (!dc || !c.valid(info, 40) || c.arg(11) != 0)
            return 0u;
        const uint32_t headerSize = c.read32(info);
        const uint32_t infoSize = headerSize + 256 * 4;
        std::vector<uint8_t> header(infoSize, 0);
        for (uint32_t i = 0; i < infoSize && c.valid(info + i, 1); ++i)
            header[i] = c.read8(info + i);
        const int dibWidth = int32_t(c.read32(info + 4));
        const int depth = c.read16(info + 14);
        const int first = int32_t(c.arg(7));
        const int scans = int32_t(c.arg(8));
        const uint32_t stride = (uint32_t(std::max(dibWidth, 0)) * uint32_t(depth) + 31) / 32 * 4;
        const uint32_t size = stride * uint32_t(std::max(scans, 0));
        if (!c.valid(c.arg(9), size))
            return 0u;
        const uint8_t *bits = c.memory().data() + c.arg(9);
        int w = 0;
        int h = 0;
        int d = 0;
        std::vector<uint32_t> image;
        if (!decodeDib(header.data(), header.size(), bits, size, first, scans, w, h, d, image))
            return 0u;
        const bool topDown = int32_t(c.read32(info + 8)) < 0;
        const int x = int32_t(c.arg(1));
        const int y = int32_t(c.arg(2));
        const int width = int32_t(c.arg(3));
        const int height = int32_t(c.arg(4));
        const int sx = int32_t(c.arg(5));
        const int sy = int32_t(c.arg(6));
        if (width <= 0 || height <= 0)
            return 0u;
        std::vector<uint32_t> out = readArea(*dc, x, y, width, height);
        for (int r = 0; r < height; ++r) {
            const int row = topDown ? sy + r : h - sy - height + r;
            if (row < 0 || row >= h)
                continue;
            for (int col = 0; col < width; ++col)
                if (sx + col >= 0 && sx + col < w)
                    out[size_t(r) * size_t(width) + size_t(col)] = image[size_t(row) * size_t(w) + size_t(sx + col)];
        }
        writeArea(*dc, x, y, width, height, out);
        return uint32_t(scans);
    });
    stdcall(g, "SetPixel", 4, [this](Cpu &c) {
        DeviceContext *dc = deviceContext(c.arg(0));
        if (!dc)
            return kClrInvalid;
        writeArea(*dc, int32_t(c.arg(1)), int32_t(c.arg(2)), 1, 1, {pixelOf(c.arg(3))});
        return colourOf(readArea(*dc, int32_t(c.arg(1)), int32_t(c.arg(2)), 1, 1)[0]);
    });
    stdcall(g, "GetPixel", 3, [this](Cpu &c) {
        DeviceContext *dc = deviceContext(c.arg(0));
        return dc ? colourOf(readArea(*dc, int32_t(c.arg(1)), int32_t(c.arg(2)), 1, 1)[0]) : kClrInvalid;
    });
    // Polyline(dc, points, n): lines in the pen's colour, each without its
    // last point, as GDI draws them.
    stdcall(g, "Polyline", 3, [this](Cpu &c) {
        DeviceContext *dc = deviceContext(c.arg(0));
        const uint32_t n = c.arg(2);
        if (!dc || n < 2 || !c.valid(c.arg(1), 8 * n))
            return 0u;
        uint32_t colour = 0;
        if (auto it = _pens.find(dc->pen); it != _pens.end()) {
            if (it->second.none)
                return 1u;
            colour = pixelOf(it->second.colour);
        } else if (dc->pen == kStockObjects + kNullPen) {
            return 1u;
        } else if (dc->pen == kStockObjects + kWhitePen) {
            colour = 0xffffff;
        }
        for (uint32_t i = 0; i + 1 < n; ++i) {
            int x = int32_t(c.read32(c.arg(1) + 8 * i));
            int y = int32_t(c.read32(c.arg(1) + 8 * i + 4));
            const int x1 = int32_t(c.read32(c.arg(1) + 8 * i + 8));
            const int y1 = int32_t(c.read32(c.arg(1) + 8 * i + 12));
            const int dx = std::abs(x1 - x);
            const int dy = -std::abs(y1 - y);
            const int stepX = x < x1 ? 1 : -1;
            const int stepY = y < y1 ? 1 : -1;
            int error = dx + dy;
            while (x != x1 || y != y1) {
                writeArea(*dc, x, y, 1, 1, {colour});
                const int twice = 2 * error;
                if (twice >= dy) {
                    error += dy;
                    x += stepX;
                }
                if (twice <= dx) {
                    error += dx;
                    y += stepY;
                }
            }
        }
        return 1u;
    });

    // Bitmaps from files (LoadImageA with LR_LOADFROMFILE, 0x10) and from a
    // module's resources (RT_BITMAP: a DIB without its file header). Other
    // images are handles only.
    auto bitmapFrom = [this](const std::vector<uint8_t> &dib) -> uint32_t {
        if (dib.size() < 40)
            return 0;
        const uint32_t headerSize = uint32_t(dib[0] | dib[1] << 8);
        const int depth = dib[14] | dib[15] << 8;
        const uint32_t used = uint32_t(dib[32] | dib[33] << 8);
        const uint32_t compression = dib[16];
        const uint32_t table = depth <= 8 ? 4 * (used ? used : 1u << depth) : compression == 3 ? 12 : 0;
        if (headerSize + table > dib.size())
            return 0;
        Bitmap b;
        if (!decodeDib(dib.data(), headerSize + table, dib.data() + headerSize + table, dib.size() - headerSize - table,
                       0, 1 << 30, b.width, b.height, b.bits, b.pixels))
            return 0;
        const uint32_t handle = kGdiObjects + _nextGdiObject++;
        _bitmaps[handle] = std::move(b);
        return handle;
    };
    auto resourceBitmap = [this, bitmapFrom](Cpu &c, uint32_t instance, uint32_t name) -> uint32_t {
        const auto found = findResource(instance, 2, name);
        if (!found || !c.valid(found->first, found->second))
            return 0;
        const uint8_t *at = c.memory().data() + found->first;
        return bitmapFrom(std::vector<uint8_t>(at, at + found->second));
    };
    stdcall(u, "LoadImageA", 6, [this, bitmapFrom, resourceBitmap](Cpu &c) {
        if (c.arg(2) != 0) // IMAGE_BITMAP
            return kGdiObjects + _nextGdiObject++;
        if (!(c.arg(5) & 0x10))
            return resourceBitmap(c, c.arg(0), c.arg(1));
        const std::string full = fullPath(c.string(c.arg(1)));
        const auto host = mapPath ? mapPath(full, false) : std::nullopt;
        std::FILE *f = host ? std::fopen(host->string().c_str(), "rb") : nullptr;
        if (fileOpened)
            fileOpened(full, f != nullptr);
        if (!f)
            return 0u;
        std::vector<uint8_t> file;
        uint8_t chunk[4096];
        for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;)
            file.insert(file.end(), chunk, chunk + n);
        std::fclose(f);
        if (file.size() < 14 || file[0] != 'B' || file[1] != 'M')
            return 0u;
        // The bits are where the file header says, after the colours.
        const uint32_t offset = uint32_t(file[10] | file[11] << 8 | file[12] << 16 | uint32_t(file[13]) << 24);
        if (offset < 14 + 40 || offset > file.size())
            return 0u;
        Bitmap b;
        if (!decodeDib(file.data() + 14, offset - 14, file.data() + offset, file.size() - offset, 0, 1 << 30, b.width,
                       b.height, b.bits, b.pixels))
            return 0u;
        const uint32_t handle = kGdiObjects + _nextGdiObject++;
        _bitmaps[handle] = std::move(b);
        return handle;
    });
    stdcall(u, "LoadBitmapA", 2, [resourceBitmap](Cpu &c) { return resourceBitmap(c, c.arg(0), c.arg(1)); });

    // Text: measured by the stand-in font, 8 x 16 pixels a character, and
    // not drawn.
    stdcall(g, "TextOutA", 5, [](Cpu &) { return 1u; });
    stdcall(g, "GetTextExtentPoint32A", 4, [](Cpu &c) {
        if (!c.valid(c.arg(3), 8))
            return 0u;
        c.write32(c.arg(3), c.arg(2) * kCharWidth);
        c.write32(c.arg(3) + 4, kCharHeight);
        return 1u;
    });
    // GetTextExtentExPointA(dc, text, n, room, fit*, ends*, size*): how many
    // characters fit in `room` pixels, and where each ends.
    stdcall(g, "GetTextExtentExPointA", 7, [](Cpu &c) {
        const uint32_t n = c.arg(2);
        if (c.arg(4))
            c.write32(c.arg(4), std::min<uint32_t>(n, int32_t(c.arg(3)) > 0 ? c.arg(3) / kCharWidth : 0));
        if (c.arg(5) && c.valid(c.arg(5), 4 * n))
            for (uint32_t i = 0; i < n; ++i)
                c.write32(c.arg(5) + 4 * i, kCharWidth * (i + 1));
        if (c.arg(6) && c.valid(c.arg(6), 8)) {
            c.write32(c.arg(6), kCharWidth * n);
            c.write32(c.arg(6) + 4, kCharHeight);
        }
        return 1u;
    });
    // DrawTextA(dc, text, n, rect, format) and DrawTextExA (with margins):
    // the height of one line.
    stdcall(u, "DrawTextA", 5, [](Cpu &) { return uint32_t(kCharHeight); });
    stdcall(u, "DrawTextExA", 6, [](Cpu &) { return uint32_t(kCharHeight); });

    // Coordinates: only MM_TEXT (1), device units, no origin moved.
    stdcall(g, "GetMapMode", 1, [](Cpu &) { return 1u; });
    stdcall(g, "SetMapMode", 2, [](Cpu &) { return 1u; });
    stdcall(g, "DPtoLP", 3, [](Cpu &) { return 1u; });
    // GetDCOrgEx: a window's client area on the screen; a memory one's 0, 0.
    stdcall(g, "GetDCOrgEx", 2, [this](Cpu &c) {
        DeviceContext *dc = deviceContext(c.arg(0));
        if (!dc || !c.valid(c.arg(1), 8))
            return 0u;
        std::optional<std::array<int, 4>> area;
        if (dc->window && dc->window != kDesktopWindow)
            area = clientArea(dc->window);
        c.write32(c.arg(1), area ? uint32_t((*area)[0]) : 0u);
        c.write32(c.arg(1) + 4, area ? uint32_t((*area)[1]) : 0u);
        return 1u;
    });
    // GetClipBox: a bitmap's size, or a window's client area, as a simple
    // region (2).
    stdcall(g, "GetClipBox", 2, [this](Cpu &c) {
        DeviceContext *dc = deviceContext(c.arg(0));
        if (!dc || !c.valid(c.arg(1), 16))
            return 0u;
        int width = screenWidth;
        int height = screenHeight;
        if (!dc->window) {
            auto it = _bitmaps.find(dc->bitmap);
            width = it != _bitmaps.end() ? it->second.width : 1;
            height = it != _bitmaps.end() ? it->second.height : 1;
        } else if (dc->window != kDesktopWindow) {
            if (auto area = clientArea(dc->window)) {
                width = (*area)[2];
                height = (*area)[3];
            }
        }
        c.write32(c.arg(1), 0);
        c.write32(c.arg(1) + 4, 0);
        c.write32(c.arg(1) + 8, uint32_t(width));
        c.write32(c.arg(1) + 12, uint32_t(height));
        return 2u;
    });
}

} // namespace reluva::x86
