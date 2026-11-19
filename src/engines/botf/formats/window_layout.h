// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The .wdf window layouts in stbof.res (docs/engines/botf/formats/wdf.md): one per
// screen or dialog, a tree of windows holding buttons, menus, sliders,
// scroll bars, list boxes, pictures and text boxes, each with its place, its
// size and the pictures it's drawn with.

namespace reluva::botf {

enum class WidgetType : uint32_t {
    kWindow = 1,
    kButton = 2,
    kMenu = 5,
    kSlider = 7,
    kScrollBar = 8,
    kListBox = 12,
    kPicture = 13,
    kTextBox = 14,
};

const char *widgetTypeName(WidgetType type);

/** How a menu, list box or text box draws its text. */
struct WidgetFont {
    enum class Align { kLeft, kRight, kCenter };

    Color color;                  // stored red, blue, green (not confirmed)
    std::array<int32_t, 7> other; // flags: [0] right-aligned, [1] centred; [2], [3] not known
    Align align() const { return other[0] ? Align::kRight : other[1] ? Align::kCenter : Align::kLeft; }
    bool own = false; // use this font rather than the game's default
    std::string file; // a .fnt in stbof.res
};

/** One widget of a layout, with what its type adds; windows hold their widgets. */
struct Widget {
    WidgetType type = WidgetType::kWindow;
    int32_t id = 0;
    /** x and y from the parent window's corner */
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;
    std::string background; // a .tga, or empty for none

    /**
     * Every picture the widget names, with what it's for ("background",
     * "normal", "selected" ...), in the file's order; "none" left out.
     */
    std::vector<std::pair<std::string, std::string>> pictures;

    /** Buttons */
    uint8_t buttonKind = 0;
    bool repeats = false;
    int32_t repeatsPerSecond = 0;
    /**
     * Text boxes: where the text goes, inside the widget. Menus: four values
     * in the same place, 0, 0 and the widget's x and y again
     */
    int32_t textX = 0;
    int32_t textY = 0;
    int32_t textW = 0;
    int32_t textH = 0;
    /**
     * Menus, list boxes, text boxes (list boxes and menus also have a second
     * font, for the selected line, presumably)
     */
    std::vector<WidgetFont> fonts;
    std::string menuFile; // a .mnu in stbof.res
    /** Sliders and scroll bars */
    int32_t minValue = 0;
    int32_t maxValue = 0;
    int32_t units = 0;
    /** List boxes */
    int32_t columns = 0;
    std::array<int32_t, 10> columnWidths{};
    /** Values not worked out, in the file's order */
    std::vector<int32_t> unknown;

    /** Windows: the widgets inside, in the file's order (grouped by type) */
    std::vector<Widget> children;
};

/** A .wdf file: its tree of widgets, from the screen's window down. */
class WindowLayout {
public:
    Error load(const std::vector<uint8_t> &data);

    uint32_t kind() const { return _kind; } // the file's first u32
    size_t used() const { return _used; }   // bytes read; the files end there
    const Widget &root() const { return _root; }
    /**
     * Every widget, the root included, depth first; with each one's corner
     * on the screen (the sum of its own and its windows' positions).
     */
    struct Placed {
        const Widget *widget;
        int x;
        int y;
        int depth;
    };
    std::vector<Placed> placed() const;

private:
    uint32_t _kind = 0;
    size_t _used = 0;
    Widget _root;
};

} // namespace reluva::botf
