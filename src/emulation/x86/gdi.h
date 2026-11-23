// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace reluva::ttf {
class Font;
}

namespace reluva::x86 {

class Cpu;
class Win32Process;

/**
 * GDI32's objects and device contexts, and the drawing USER32 does with
 * them (docs/common/x86/gdi.md): brushes, pens, fonts and bitmaps; device
 * contexts drawing into pixels in the program's memory (a bitmap's, a
 * DirectDraw surface's, the screen's); filling, lines, ellipses, pixels,
 * stretched copies and text. Windows' fonts aren't Reluva's to have: text
 * in a proportional font is drawn in Liberation Sans, built in, and in a
 * fixed one in Reluva's own small font.
 */
class Gdi {
public:
    /** Pixels a device context draws into. */
    struct Target {
        uint32_t memory = 0; // in the process
        int width = 0;
        int height = 0;
        int pitch = 0;
        int bits = 8; // 8 (palette indices), 16, 24 or 32
        // 16 bits: the colour masks (5-6-5 if all are 0).
        uint32_t redMask = 0;
        uint32_t greenMask = 0;
        uint32_t blueMask = 0;
        // 8 bits: the colours of the indices, as 0x00RRGGBB.
        std::array<uint32_t, 256> palette{};
    };

    /** Registers GDI32's stand-ins, and USER32's that draw, with the process. */
    explicit Gdi(Win32Process &process);
    ~Gdi();

    /**
     * A device context drawing into `target` (a DirectDraw surface's, for
     * its GetDC), until releaseDc().
     */
    uint32_t createDc(const Target &target);
    /** Ends a device context createDc() made. */
    void releaseDc(uint32_t dc);

    /**
     * Where a window's device context (GetDC, BeginPaint) draws: the
     * screen's pixels as the engine has them (a DirectDraw primary
     * surface's), or nothing (drawing is then lost).
     */
    std::function<std::optional<Target>()> screen;

    /**
     * The desktop painted, as Windows paints it where windows have gone:
     * the part of `area` (left, top, width, height, on the screen) that no
     * window shown covers, in the desktop's colour (black). Nothing without
     * a screen.
     */
    void paintDesktop(const std::array<int, 4> &area);
    /**
     * A window's frame painted, as DefWindowProc paints it (WM_NCPAINT),
     * plainly: what lies outside its client area in the face colour (its
     * borders), its caption in the active caption's colour, without its
     * title or buttons, and its scroll bars. Nothing for a window without
     * a frame, or without a screen.
     * @param clip Only what's within it (left, top, width, height), if given.
     */
    void paintFrame(uint32_t window, const std::optional<std::array<int, 4>> &clip = std::nullopt);
    /** A device context for a window's client area, as GetDC makes, until releaseDc(). */
    uint32_t windowDc(uint32_t window);
    /**
     * An edit control's client area painted, as Windows' EDIT class paints
     * it: in the brush and text colour its parent gave (WM_CTLCOLOREDIT,
     * on `dc`), white and black if none; its text in its font from a small
     * margin, the selection (from `selStart` to `selEnd`, in characters)
     * highlighted and the caret at `selEnd` while it has the focus.
     * @param font The font it was given (WM_SETFONT); 0 for the system font.
     * @param brush The parent's answer; 0 for COLOR_WINDOW.
     */
    void paintEdit(uint32_t window, const std::string &text, int selStart, int selEnd, bool focused, uint32_t font,
                   uint32_t dc, uint32_t brush);
    /** A string's width in pixels in a font (0 for the system font). */
    int textWidth(uint32_t font, const std::string &s) const;

    /** A COLORREF (0x00BBGGRR) as the pixel value nearest it in a target's format. */
    static uint32_t pixelFor(const Target &t, uint32_t colorRef);
    /** A target's pixel as a COLORREF. */
    static uint32_t colorAt(Cpu &c, const Target &t, int x, int y);

private:
    struct Object {
        enum Kind { kBrush, kPen, kFont, kBitmap } kind = kBrush;
        uint32_t color = 0; // COLORREF: a brush's, a pen's
        int style = 0;      // a brush's (0 solid, 1 null, 2 hatched) or pen's (0 solid, 5 null)
        int hatch = 0;
        int width = 1;  // a pen's
        int height = 0; // a font's, as asked
        int weight = 400;
        bool italic = false;
        std::string face;
        Target bitmap; // a bitmap's pixels
        bool stock = false;
        bool fixed = false; // a font of fixed pitch, drawn in Reluva's own
    };
    struct Dc {
        std::optional<Target> target; // its own (a bitmap's, a surface's)
        bool window = false;          // draws on screen()
        uint32_t windowHandle = 0;    // a window DC's: its client area of the screen; 0 the whole screen
        uint32_t brush = 0;
        uint32_t pen = 0;
        uint32_t font = 0;
        uint32_t bitmap = 0; // a memory DC's selected bitmap
        uint32_t textColor = 0;
        uint32_t bkColor = 0xffffff;
        int bkMode = 2; // OPAQUE
        int x = 0;
        int y = 0;
    };
    std::optional<Target> targetOf(const Dc &dc) const;
    Dc *dc(uint32_t handle);
    uint32_t addObject(Object o);
    void addStandIns();
    void fill(Cpu &c, const Target &t, int left, int top, int right, int bottom, uint32_t colorRef);
    void line(Cpu &c, const Target &t, int x0, int y0, int x1, int y1, uint32_t colorRef);
    void plot(Cpu &c, const Target &t, int x, int y, uint32_t colorRef);
    int drawText(Cpu &c, Dc &d, const std::string &text, int left, int top, int right, int bottom, uint32_t format,
                 uint32_t rect);
    // Text in a font: what DrawTextA, TextOutA and the edit control draw with.
    const Object &fontObject(uint32_t handle) const;
    int textWidth(const Object &font, const std::string &s) const;
    int lineHeight(const Object &font) const;
    // A proportional font's TrueType stand-in (Liberation Sans, bold from
    // weight 600) and its size; null for a fixed one, drawn in Reluva's own.
    ttf::Font *proportional(const Object &font) const;
    float pixels(const Object &font) const;
    void drawString(Cpu &c, const Target &t, const Object &font, int x, int y, const std::string &s, uint32_t colorRef,
                    const std::array<int, 4> &clip);

    Win32Process &_process;
    std::map<uint32_t, Object> _objects;
    mutable std::unique_ptr<ttf::Font> _sans[2]; // regular, bold: loaded when first drawn with
    std::map<uint32_t, Dc> _dcs;
    uint32_t _nextObject = 0x5a000100;
    uint32_t _nextDc = 0x5d000100;
};

} // namespace reluva::x86
