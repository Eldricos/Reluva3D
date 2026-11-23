// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// GDI32's device contexts, fonts and text, as Microsoft's API documentation
// describes them. Text is drawn in Reluva's built-in bitmap font, scaled to
// the font's height: the shapes aren't the font's.

#include "emulation/x86/win32.h"

#include "graphics/font.h"

#include <algorithm>
#include <cstring>

namespace reluva::x86 {

namespace {

constexpr uint32_t kTransparent = 1; // SetBkMode's

// The built-in font's scale for a font whose cell is `height` pixels high.
int scaleFor(int height) {
    return std::max(1, (height + font::kLineHeight / 2) / font::kLineHeight);
}

} // namespace

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
    // returned. Other objects are taken and the same handed back.
    stdcall(g, "SelectObject", 2, [this](Cpu &c) {
        DeviceContext &dc = deviceContext(c.arg(0));
        const uint32_t object = c.arg(1);
        if (_fonts.count(object)) {
            const uint32_t was = dc.font;
            dc.font = object;
            return was ? was : object;
        }
        return object;
    });
    stdcall(g, "DeleteObject", 1, [this](Cpu &c) {
        _fonts.erase(c.arg(0));
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
}

} // namespace reluva::x86
