// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/layout_screen.h"

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
    for (const WindowLayout::Placed &p : _layout.placed())
        _widgets[p.widget->id] = {p.widget, p.x, p.y};
    for (int32_t id : needed)
        if (!find(id))
            return Error::fail(std::string(file) + " has no widget " + std::to_string(id));
    return Error::success();
}

const LayoutScreen::Placed *LayoutScreen::find(int32_t id) const {
    auto it = _widgets.find(id);
    return it == _widgets.end() ? nullptr : &it->second;
}

int32_t LayoutScreen::buttonAt(int x, int y) const {
    for (const auto &[id, p] : _widgets)
        if (p.widget->type == WidgetType::kButton && !_hidden.count(id) && !_disabled.count(id) && p.contains(x, y))
            return id;
    return 0;
}

void LayoutScreen::trackPointer(const Event &event) {
    if (event.type == EventType::kMouseMove || event.type == EventType::kMouseDown || event.type == EventType::kMouseUp)
        _pointed = buttonAt(event.mouseX, event.mouseY);
}

void LayoutScreen::drawLayouts(Surface &screen, const std::set<int32_t> &selected) const {
    for (const WindowLayout *layout : {&_menu, &_layout})
        for (const WindowLayout::Placed &lp : layout->placed()) {
            const Widget &w = *lp.widget;
            if (layout == &_layout && _hidden.count(w.id))
                continue;
            if (const image::Image *img = w.background.empty() ? nullptr : _assets.picture(w.background))
                blitClipped(screen, *img, lp.x, lp.y, lp.x, lp.y, w.w, w.h);
            if (w.type != WidgetType::kButton)
                continue;
            if (layout == &_menu) {
                drawButton(_assets, screen, w, lp.x, lp.y, false, kBehindPanelBrightness);
                continue;
            }
            const bool disabled = _disabled.count(w.id) > 0;
            const bool lit =
                !disabled && (w.id == _pressed || selected.count(w.id) || (_highlightPointed && w.id == _pointed));
            drawButton(_assets, screen, w, lp.x, lp.y, lit, disabled ? kDisabledBrightness : 100);
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
    if (lineGap < 0) {
        // An odd pixel left over goes above: the options screen's title.
        line(text, boxY + w.textY + (w.textH - f->glyphHeight() + 1) / 2);
        return;
    }
    int y = boxY + w.textY;
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
