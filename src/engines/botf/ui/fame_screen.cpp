// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/fame_screen.h"

#include <vector>

namespace reluva::botf {

namespace {

// fame.wdf's widgets: Return, and the rows, top down, each an empty picture
// widget where a row (fameitem.wdf) goes.
constexpr int32_t kReturn = 9950;
constexpr int32_t kFirstRow = 9951;
// fameitem.wdf's text boxes, from the row's corner.
constexpr int32_t kName = 9962;
constexpr int32_t kDescription = 9963;
constexpr int32_t kScore = 9964;

// The rows' text, measured from a screenshot of the game: not their fonts'
// colours.
constexpr Color kTextColor{24, 101, 189};

// Each row's frame is its empire's: h?_fm1.tga, with the empire's letter.
// Matched to the screenshot's rows by their descriptions: h is the
// Federation and f the Ferengi.
constexpr char kEmpireLetters[] = {'c', 'h', 'f', 'k', 'r'};

} // namespace

Error FameScreen::load() {
    if (Error err = loadLayouts("fame.wdf", {kReturn, kFirstRow, kFirstRow + kRows - 1}, false))
        return err;
    std::vector<uint8_t> data;
    if (Error err = _assets.archive().read("fameitem.wdf", data))
        return err;
    if (Error err = _item.load(data))
        return Error::fail("fameitem.wdf: " + err.message);
    return Error::success();
}

bool FameScreen::handle(const Event &e) {
    if (e.type == EventType::kKeyDown && (e.key == key::kReturn || e.key == key::kKpEnter || e.key == key::kEscape))
        return true;
    if (e.button != MouseButton::kLeft)
        return false;
    if (e.type == EventType::kMouseDown) {
        _pressed = buttonAt(e.mouseX, e.mouseY);
        return false;
    }
    if (e.type != EventType::kMouseUp)
        return false;
    // A button acts when the mouse comes up on it, as Windows buttons do.
    const bool left = _pressed == kReturn && buttonAt(e.mouseX, e.mouseY) == kReturn;
    _pressed = 0;
    return left;
}

void FameScreen::draw(Surface &screen) const {
    drawLayouts(screen);
    for (size_t i = 0; i < _entries.size() && i < size_t(kRows); ++i) {
        const Placed *row = find(kFirstRow + int32_t(i));
        if (!row)
            continue;
        const FameEntry &entry = _entries[i];
        const int empire = entry.empire >= 0 && entry.empire < 5 ? entry.empire : 0;
        const std::string frame = std::string("h") + kEmpireLetters[empire] + "_fm1.tga";
        if (const image::Image *img = _assets.picture(frame))
            blitClipped(screen, *img, row->x, row->y, row->x, row->y, row->widget->w, row->widget->h);
        for (const WindowLayout::Placed &p : _item.placed()) {
            const Widget &w = *p.widget;
            std::string text;
            if (w.id == kName)
                text = entry.name;
            else if (w.id == kDescription)
                text = entry.description;
            else if (w.id == kScore)
                text = std::to_string(entry.score);
            else
                continue;
            drawTextAt(screen, w, row->x + p.x, row->y + p.y, text, kTextColor);
        }
    }
}

} // namespace reluva::botf
