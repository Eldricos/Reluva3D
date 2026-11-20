// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/layout_screen.h"

#include "common/text.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace reluva::botf {

namespace {

Error loadLayout(Assets &assets, const char *name, WindowLayout &layout) {
    std::vector<uint8_t> data;
    if (Error err = assets.archive().read(name, data))
        return err;
    if (Error err = layout.load(data))
        return Error::fail(std::string(name) + ": " + err.message);
    return Error::success();
}

// How bright the game draws a button that can't be used, and the whole
// main menu behind a panel, in percent: measured from a recording, where the
// main menu's three buttons that can't be used yet are about 35% as bright
// as the rest would be, and behind the game setup screen every button is
// about 28% as bright as it is on the menu, those three 80% of their own.
constexpr int kDisabledBrightness = 35;
constexpr int kBehindPanelBrightness = kDisabledBrightness * 80 / 100;

// A button's "normal" picture, or its "selected" one, at `brightness`
// percent.
void drawButton(Assets &assets, Surface &screen, const Widget &w, int x, int y, bool selected, int brightness) {
    for (const auto &[role, name] : w.pictures)
        if (role == (selected ? "selected" : "normal"))
            if (const image::Image *img = brightness < 100 ? assets.dimmed(name, brightness) : assets.picture(name))
                blitClipped(screen, *img, x, y, x, y, w.w, w.h);
}

} // namespace

std::string upperAscii(std::string s) {
    for (char &c : s)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    return s;
}

Error LayoutScreen::loadLayouts(const char *file, std::initializer_list<int32_t> needed, bool overMenu) {
    _menu = WindowLayout{};
    if (overMenu)
        if (Error err = loadLayout(_assets, "opening.wdf", _menu))
            return err;
    if (Error err = loadLayout(_assets, file, _layout))
        return err;
    _widgets.clear();
    _panels.clear();
    for (const WindowLayout::Placed &p : _layout.placed())
        _widgets[p.widget->id] = {p.widget, p.x, p.y};
    for (int32_t id : needed)
        if (!find(id))
            return Error::fail(std::string(file) + " has no widget " + std::to_string(id));
    return Error::success();
}

Error LayoutScreen::addPanel(const char *file, int32_t place, std::initializer_list<int32_t> needed, int &panel) {
    const Placed *at = find(place);
    if (!at)
        return Error::fail("no widget " + std::to_string(place) + " to put " + file + " in");
    auto p = std::make_unique<Panel>();
    if (Error err = loadLayout(_assets, file, p->layout))
        return err;
    p->x = at->x;
    p->y = at->y;
    panel = int(_panels.size());
    // Not its window, whose id (0) others share: drawn as the panel's.
    for (const WindowLayout::Placed &w : p->layout.placed())
        if (w.depth > 0)
            _widgets[w.widget->id] = {w.widget, p->x + w.x, p->y + w.y, panel};
    _panels.push_back(std::move(p));
    for (int32_t id : needed)
        if (!find(id))
            return Error::fail(std::string(file) + " has no widget " + std::to_string(id));
    return Error::success();
}

void LayoutScreen::showPanel(int panel, bool shown) {
    if (panel >= 0 && panel < int(_panels.size()))
        _panels[size_t(panel)]->shown = shown;
}

bool LayoutScreen::panelShown(int panel) const {
    return panel >= 0 && panel < int(_panels.size()) && _panels[size_t(panel)]->shown;
}

const LayoutScreen::Placed *LayoutScreen::find(int32_t id) const {
    auto it = _widgets.find(id);
    return it == _widgets.end() ? nullptr : &it->second;
}

int32_t LayoutScreen::buttonAt(int x, int y) const {
    for (const auto &[id, p] : _widgets)
        if (p.widget->type == WidgetType::kButton && !_hidden.count(id) && !_disabled.count(id) &&
            (p.panel < 0 || panelShown(p.panel)) && p.contains(x, y))
            return id;
    return 0;
}

void LayoutScreen::trackPointer(const Event &event) {
    if (event.type == EventType::kMouseMove || event.type == EventType::kMouseDown || event.type == EventType::kMouseUp)
        _pointed = buttonAt(event.mouseX, event.mouseY);
}

void LayoutScreen::drawLayouts(Surface &screen, const std::set<int32_t> &selected) const {
    auto draw = [&](const WindowLayout &layout, int dx, int dy, bool menu) {
        for (const WindowLayout::Placed &lp : layout.placed()) {
            const Widget &w = *lp.widget;
            const int x = dx + lp.x;
            const int y = dy + lp.y;
            if (!menu && _hidden.count(w.id))
                continue;
            if (const image::Image *img = w.background.empty() ? nullptr : _assets.picture(w.background))
                blitClipped(screen, *img, x, y, x, y, w.w, w.h);
            if (w.type != WidgetType::kButton)
                continue;
            if (menu) {
                drawButton(_assets, screen, w, x, y, false, kBehindPanelBrightness);
                continue;
            }
            const bool disabled = _disabled.count(w.id) > 0;
            const bool lit =
                !disabled && (w.id == _pressed || selected.count(w.id) || (_highlightPointed && w.id == _pointed));
            drawButton(_assets, screen, w, x, y, lit, disabled ? kDisabledBrightness : 100);
        }
    };
    draw(_menu, 0, 0, true);
    draw(_layout, 0, 0, false);
    for (const auto &panel : _panels)
        if (panel->shown)
            draw(panel->layout, panel->x, panel->y, false);
}

void LayoutScreen::typeInto(std::string &text, int32_t textBox, const char *utf8, size_t maxCharacters) const {
    const Placed *box = find(textBox);
    const Font *font = box && !box->widget->fonts.empty() ? _assets.font(box->widget->fonts[0].file) : nullptr;
    if (!font)
        return;
    auto characters = [](std::string_view s) {
        size_t n = 0;
        for (size_t i = 0; i < s.size(); ++n)
            text::nextUtf8(s, i);
        return n;
    };
    const std::string_view typed(utf8);
    for (size_t i = 0; i < typed.size();) {
        const size_t start = i;
        const uint32_t c = text::nextUtf8(typed, i);
        if (c != ' ' && !font->glyph(char32_t(c)))
            continue;
        std::string longer = text + std::string(typed.substr(start, i - start));
        if (characters(longer) <= maxCharacters && font->measure(longer) <= box->widget->textW)
            text = std::move(longer);
    }
}

void LayoutScreen::eraseLast(std::string &text) {
    size_t last = 0;
    for (size_t i = 0; i < text.size();) {
        last = i;
        text::nextUtf8(text, i);
    }
    text.resize(last);
}

void LayoutScreen::drawCursor(Surface &screen, int32_t textBox, const std::string &text) const {
    const Placed *p = find(textBox);
    const Font *font = p && !p->widget->fonts.empty() ? _assets.font(p->widget->fonts[0].file) : nullptr;
    if (!font)
        return;
    const Widget &w = *p->widget;
    constexpr int kCursorWidth = 8;
    // After the text's last glyph, from the glyphs' top (drawTextAt's).
    const int x0 = p->x + w.textX + font->measure(text);
    const int y0 = p->y + w.textY + (w.textH - font->lineHeight()) / 2 + 1;
    for (int y = std::max(y0, 0); y < std::min(y0 + font->glyphHeight(), screen.h); ++y)
        for (int x = std::max(x0, 0); x < std::min(x0 + kCursorWidth, screen.w); ++x) {
            uint8_t *px = screen.row(y) + size_t(x) * 4;
            px[0] = px[1] = px[2] = px[3] = 255;
        }
}

void LayoutScreen::drawText(Surface &screen, int32_t textBox, const std::string &text, Color color, int lineGap) const {
    if (const Placed *p = find(textBox))
        drawTextAt(screen, *p->widget, p->x, p->y, text, color, lineGap);
}

void LayoutScreen::drawTextAt(Surface &screen, const Widget &w, int boxX, int boxY, const std::string &text,
                              Color color, int lineGap) const {
    if (w.fonts.empty())
        return;
    const WidgetFont &wf = w.fonts[0];
    const Font *f = _assets.font(wf.file);
    if (!f)
        return;
    auto line = [&](const std::string &s, int y) {
        int x = boxX + w.textX;
        if (wf.align() == WidgetFont::Align::kCenter)
            x += (w.textW - f->measure(s)) / 2;
        else if (wf.align() == WidgetFont::Align::kRight)
            x += w.textW - f->measure(s);
        f->drawInColor(screen, x, y, s, color);
    };
    // The game draws a line of text as its glyphs' whole cells, the frame's
    // row above them included: the glyphs start a pixel below the line's
    // top (measured on screenshots, single lines and the empire screen's
    // wrapped ones alike).
    if (lineGap < 0) {
        line(text, boxY + w.textY + (w.textH - f->lineHeight()) / 2 + 1);
        return;
    }
    int y = boxY + w.textY + 1;
    for (const std::string &s : f->wrap(text, w.textW)) {
        if (y + f->glyphHeight() > boxY + w.textY + w.textH)
            break;
        line(s, y);
        y += f->lineHeight() + lineGap;
    }
}

void LayoutScreen::drawPicture(Surface &screen, int32_t widget, const std::string &file) const {
    if (const Placed *p = find(widget))
        if (const image::Image *img = _assets.picture(file))
            blitClipped(screen, *img, p->x, p->y, p->x, p->y, p->widget->w, p->widget->h);
}

Color LayoutScreen::fontColor(int32_t textBox) const {
    const Placed *p = find(textBox);
    return p && !p->widget->fonts.empty() ? p->widget->fonts[0].color : Color{};
}

std::string LayoutScreen::word(int entry, const char *english) const {
    const std::string &w = _lexicon[size_t(entry)];
    return w.empty() ? english : w;
}

} // namespace reluva::botf
