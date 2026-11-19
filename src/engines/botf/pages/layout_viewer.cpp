// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/pages/viewers.h"

#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cstdio>

namespace reluva::botf {

namespace {

void outline(Surface &s, int x, int y, int w, int h, Color c) {
    fillRect(s, x, y, w, 1, c);
    fillRect(s, x, y + h - 1, w, 1, c);
    fillRect(s, x, y, 1, h, c);
    fillRect(s, x + w - 1, y, 1, h, c);
}

Color typeColor(WidgetType t) {
    switch (t) {
    case WidgetType::kWindow:
        return {120, 120, 255};
    case WidgetType::kButton:
        return {80, 230, 80};
    case WidgetType::kMenu:
        return {255, 160, 60};
    case WidgetType::kSlider:
    case WidgetType::kScrollBar:
        return {230, 80, 230};
    case WidgetType::kListBox:
        return {80, 220, 230};
    case WidgetType::kPicture:
        return {200, 200, 200};
    case WidgetType::kTextBox:
        return {255, 240, 80};
    }
    return {255, 255, 255};
}

std::string hexColor(Color c) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

} // namespace

LayoutViewer::LayoutViewer(System &sys, Surface &screen, const Archive &archive)
    : _sys(sys), _screen(screen), _assets(archive) {
    for (std::string &n : archive.list())
        if (fs::toLower(n).ends_with(".wdf"))
            _names.push_back(std::move(n));
    std::sort(_names.begin(), _names.end(),
              [](const std::string &x, const std::string &y) { return fs::toLower(x) < fs::toLower(y); });
}

void LayoutViewer::activate() {
    if (!_names.empty() && _placed.empty() && _error.empty())
        load();
    show();
}

void LayoutViewer::load() {
    _error.clear();
    _placed.clear();
    _widget = 0;
    std::vector<uint8_t> data;
    if (Error err = _assets.archive().read(_names[_current], data))
        _error = err.message;
    else if (Error err2 = _layout.load(data))
        _error = "can't read: " + err2.message;
    else
        _placed = _layout.placed();
}

// The menu open where its widget is, tall enough for its items: the inside
// dark, the top and bottom pictures at the top and bottom, the left and
// right ones down the sides, then the items, one a line, the first in the
// second (highlight) font's colour.
void LayoutViewer::drawMenu(const WindowLayout::Placed &p, std::vector<std::string> &lines) {
    const Widget &w = *p.widget;
    const MenuFile *m = w.menuFile.empty() ? nullptr : _assets.menu(w.menuFile);
    if (!m) {
        lines.push_back("menu " + w.menuFile +
                        (_assets.why(w.menuFile).empty() ? "" : " (" + _assets.why(w.menuFile) + ")"));
        return;
    }
    const Font *f = nullptr;
    for (const WidgetFont &wf : w.fonts)
        if (!f && !wf.file.empty())
            f = _assets.font(wf.file);
    const int lineH = f ? f->lineHeight() : font::kLineHeight;
    const image::Image *left = nullptr;
    const image::Image *top = nullptr;
    const image::Image *right = nullptr;
    const image::Image *bottom = nullptr;
    for (const auto &[role, name] : w.pictures) {
        if (role == "left")
            left = _assets.picture(name);
        else if (role == "top")
            top = _assets.picture(name);
        else if (role == "right")
            right = _assets.picture(name);
        else if (role == "bottom")
            bottom = _assets.picture(name);
    }
    const int padTop = top ? top->h : 4;
    const int padBottom = bottom ? bottom->h : 4;
    const int padLeft = left ? left->w : 4;
    const int h = std::max(w.h, padTop + padBottom + int(m->items().size()) * lineH);
    const int x = p.x;
    const int y = std::min(p.y, _screen.h - h);
    fillRect(_screen, x, y, w.w, h, {0, 0, 0});
    if (left)
        for (int ty = y; ty < y + h; ty += left->h)
            blitClipped(_screen, *left, x, ty, x, y, w.w, h);
    if (right)
        for (int ty = y; ty < y + h; ty += right->h)
            blitClipped(_screen, *right, x + w.w - right->w, ty, x, y, w.w, h);
    if (top)
        blitClipped(_screen, *top, x, y, x, y, w.w, h);
    if (bottom)
        blitClipped(_screen, *bottom, x, y + h - bottom->h, x, y, w.w, h);
    const Color normal = w.fonts.size() > 0 && w.fonts[0].own ? w.fonts[0].color : Color{230, 230, 230};
    const Color hilite = w.fonts.size() > 1 && w.fonts[1].own ? w.fonts[1].color : Color{255, 220, 120};
    std::string list = w.menuFile + ":";
    for (size_t i = 0; i < m->items().size(); ++i) {
        const MenuItem &item = m->items()[i];
        const int ty = y + padTop + int(i) * lineH;
        if (ty + lineH <= y + h) {
            if (f)
                f->draw(_screen, x + padLeft + 2, ty, item.label, i == 0 ? hilite : normal);
            else
                font::drawText(_screen, x + padLeft + 2, ty, item.label, i == 0 ? hilite : normal);
        }
        const std::string entry =
            " " + std::to_string(item.id) + " " + item.label + (i + 1 < m->items().size() ? "," : "");
        if (list.size() + entry.size() > 125) { // the details strip's width in the built-in font
            lines.push_back(list);
            list = " ";
        }
        list += entry;
    }
    lines.push_back(list);
}

void LayoutViewer::handleKey(uint32_t key) {
    if (_names.empty())
        return;
    const size_t n = _names.size();
    if (key == key::kRight || key == key::kLeft || key == key::kPageDown || key == key::kPageUp) {
        if (key == key::kRight)
            _current = (_current + 1) % n;
        else if (key == key::kLeft)
            _current = (_current + n - 1) % n;
        else if (key == key::kPageDown)
            _current = std::min(_current + 10, n - 1);
        else
            _current = _current >= 10 ? _current - 10 : 0;
        load();
    } else if (key == key::kDown && !_placed.empty())
        _widget = (_widget + 1) % _placed.size();
    else if (key == key::kUp && !_placed.empty())
        _widget = (_widget + _placed.size() - 1) % _placed.size();
    else if (key == 'o')
        _outlines = !_outlines;
    else if (key == 's')
        _selected = !_selected;
    else if (key == 'i')
        _info = !_info;
    else
        return;
    show();
}

void LayoutViewer::show() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, {20, 20, 28});
    if (_names.empty())
        return;
    const std::string &file = _names[_current];

    // Each widget's background, then a button's normal (or selected)
    // picture, clipped to the widget, parents before children.
    for (const WindowLayout::Placed &p : _placed) {
        const Widget &w = *p.widget;
        if (const image::Image *img = w.background.empty() ? nullptr : _assets.picture(w.background))
            blitClipped(_screen, *img, p.x, p.y, p.x, p.y, w.w, w.h);
        if (w.type == WidgetType::kButton) {
            const char *want = _selected ? "selected" : "normal";
            for (const auto &[role, name] : w.pictures)
                if (role == want)
                    if (const image::Image *img = _assets.picture(name))
                        blitClipped(_screen, *img, p.x, p.y, p.x, p.y, w.w, w.h);
        }
    }
    if (_outlines)
        for (const WindowLayout::Placed &p : _placed)
            outline(_screen, p.x, p.y, p.widget->w, p.widget->h, typeColor(p.widget->type));
    if (!_placed.empty()) {
        const WindowLayout::Placed &p = _placed[_widget];
        outline(_screen, p.x - 1, p.y - 1, p.widget->w + 2, p.widget->h + 2, {255, 255, 255});
        outline(_screen, p.x - 2, p.y - 2, p.widget->w + 4, p.widget->h + 4, {0, 0, 0});
    }

    std::string head = std::to_string(_current + 1) + "/" + std::to_string(_names.size()) + "  " + file;
    if (!_error.empty())
        head += "  " + _error;
    std::vector<std::string> lines = {head};
    if (!_placed.empty()) {
        const WindowLayout::Placed &p = _placed[_widget];
        const Widget &w = *p.widget;
        lines[0] += "  kind " + std::to_string(_layout.kind()) + ", " + std::to_string(_placed.size()) + " widgets";
        lines.push_back(std::to_string(_widget + 1) + "/" + std::to_string(_placed.size()) + "  " +
                        std::string(p.depth ? std::string(size_t(p.depth) * 2, ' ') : "") + widgetTypeName(w.type) +
                        " " + std::to_string(w.id) + "  at " + std::to_string(w.x) + "," + std::to_string(w.y) +
                        " (screen " + std::to_string(p.x) + "," + std::to_string(p.y) + ")  size " +
                        std::to_string(w.w) + "x" + std::to_string(w.h));
        std::string pics;
        for (const auto &[role, name] : w.pictures) {
            pics += (pics.empty() ? "" : ", ") + role + " " + name;
            if (const std::string why = _assets.why(name); !why.empty())
                pics += " (" + why + ")";
        }
        if (!pics.empty())
            lines.push_back(pics);
        std::string more;
        if (w.type == WidgetType::kButton)
            more = "kind " + std::to_string(w.buttonKind) +
                   (w.repeats ? ", repeats " + std::to_string(w.repeatsPerSecond) + "/s" : "");
        if (w.type == WidgetType::kMenu || w.type == WidgetType::kTextBox)
            more = "text at " + std::to_string(w.textX) + "," + std::to_string(w.textY) + " size " +
                   std::to_string(w.textW) + "x" + std::to_string(w.textH);
        if (!w.menuFile.empty())
            more += "  menu " + w.menuFile;
        if (w.type == WidgetType::kSlider || w.type == WidgetType::kScrollBar)
            more = "values " + std::to_string(w.minValue) + "-" + std::to_string(w.maxValue) +
                   (w.units ? ", units " + std::to_string(w.units) : "");
        if (w.type == WidgetType::kListBox) {
            more = std::to_string(w.columns) + " columns:";
            for (int32_t i = 0; i < std::min(w.columns, int32_t(w.columnWidths.size())); ++i)
                more += " " + std::to_string(w.columnWidths[size_t(i)]);
        }
        for (const WidgetFont &f : w.fonts)
            more += "  font " + (f.file.empty() ? std::string("-") : f.file) + " " + hexColor(f.color) +
                    (f.own ? " (own)" : "");
        if (!w.unknown.empty()) {
            more += "  ?";
            for (int32_t v : w.unknown)
                more += " " + std::to_string(v);
        }
        if (!more.empty())
            lines.push_back(more);
    }
    if (!_placed.empty() && _placed[_widget].widget->type == WidgetType::kMenu)
        drawMenu(_placed[_widget], lines);
    if (_info) {
        const int h = int(lines.size()) * font::kLineHeight + 4;
        fillRect(_screen, 0, _screen.h - h, _screen.w, h, {0, 0, 0});
        for (size_t i = 0; i < lines.size(); ++i)
            font::drawText(_screen, 4, _screen.h - h + 2 + int(i) * font::kLineHeight, lines[i], {230, 230, 230});
    }
    _sys.setWindowTitle("Reluva - Birth of the Federation - layouts " + std::to_string(_current + 1) + "/" +
                        std::to_string(_names.size()) + ": " + file +
                        " (Left/Right, Up/Down: widget, O: outlines, S: selected, I: info, Tab: next page)");
}

} // namespace reluva::botf
