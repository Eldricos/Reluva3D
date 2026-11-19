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

void drawButton(Assets &assets, Surface &screen, const Widget &w, int x, int y, bool selected) {
    for (const auto &[role, name] : w.pictures)
        if (role == (selected ? "selected" : "normal"))
            if (const image::Image *img = assets.picture(name))
                blitClipped(screen, *img, x, y, x, y, w.w, w.h);
}

} // namespace

std::string upperAscii(std::string s) {
    for (char &c : s)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    return s;
}

Error LayoutScreen::loadLayouts(const char *file, std::initializer_list<int32_t> needed) {
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
        if (p.widget->type == WidgetType::kButton && !_hidden.count(id) && p.contains(x, y))
            return id;
    return 0;
}

void LayoutScreen::drawLayouts(Surface &screen, const std::set<int32_t> &selected) const {
    for (const WindowLayout *layout : {&_menu, &_layout})
        for (const WindowLayout::Placed &lp : layout->placed()) {
            const Widget &w = *lp.widget;
            if (layout == &_layout && _hidden.count(w.id))
                continue;
            if (const image::Image *img = w.background.empty() ? nullptr : _assets.picture(w.background))
                blitClipped(screen, *img, lp.x, lp.y, lp.x, lp.y, w.w, w.h);
            if (w.type == WidgetType::kButton)
                drawButton(_assets, screen, w, lp.x, lp.y,
                           layout == &_layout && (w.id == _pressed || selected.count(w.id)));
        }
}

void LayoutScreen::drawText(Surface &screen, int32_t textBox, const std::string &text, Color color, int lineGap) const {
    const Placed *p = find(textBox);
    if (!p || p->widget->fonts.empty())
        return;
    const Widget &w = *p->widget;
    const WidgetFont &wf = w.fonts[0];
    const Font *f = _assets.font(wf.file);
    if (!f)
        return;
    auto line = [&](const std::string &s, int y) {
        int x = p->x + w.textX;
        if (wf.align() == WidgetFont::Align::kCenter)
            x += (w.textW - f->measure(s)) / 2;
        else if (wf.align() == WidgetFont::Align::kRight)
            x += w.textW - f->measure(s);
        f->drawInColor(screen, x, y, s, color);
    };
    if (lineGap < 0) {
        line(text, p->y + w.textY + (w.textH - f->glyphHeight()) / 2);
        return;
    }
    // A word fits when it and a space after it do (the game's breaks, in the
    // empire selection screen's descriptions).
    int y = p->y + w.textY;
    for (const std::string &s : f->wrap(text, w.textW - f->spaceAdvance())) {
        if (y + f->glyphHeight() > p->y + w.textY + w.textH)
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

std::string LayoutScreen::word(int entry, const char *english) const {
    const std::string &w = _lexicon[size_t(entry)];
    return w.empty() ? english : w;
}

} // namespace reluva::botf
