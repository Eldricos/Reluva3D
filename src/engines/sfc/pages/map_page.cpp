// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Maps page: a campaign map's hexes, coloured by a field.

#include "common/stream.h"
#include "engines/sfc/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace reluva::sfc {

namespace {

constexpr int kTop = 2 * font::kLineHeight + 8;
constexpr int kLegendLines = 4;
constexpr int kFields = 5;
const char *const kFieldNames[kFields] = {"empire", "terrain", "planet", "base", "economic value"};

// The hexes stand in columns, flat side up, every other column half a
// hex lower, as the game's map shows them (which columns are lower is
// guessed).
struct Layout {
    double side = 1; // a hex's side, in pixels
    int x0 = 0;      // the first column's left
    int y0 = 0;      // the first row's top
};

Layout layoutFor(const MetaverseMap &map) {
    const int areaW = kScreenW - 8;
    const int areaH = kScreenH - kTop - kLegendLines * font::kLineHeight - 8;
    const double root3 = std::sqrt(3.0);
    // Columns are 1.5 sides apart and two wide at the last; rows root 3
    // sides apart, and half a row more for the lowered columns.
    const double byWidth = areaW / (1.5 * map.width + 0.5);
    const double byHeight = areaH / (root3 * (map.height + 0.5));
    Layout l;
    l.side = std::max(2.0, std::min(byWidth, byHeight));
    l.x0 = 4 + int((areaW - l.side * (1.5 * map.width + 0.5)) / 2);
    l.y0 = kTop;
    return l;
}

void centre(const Layout &l, uint32_t col, uint32_t row, double &cx, double &cy) {
    const double h = std::sqrt(3.0) * l.side;
    cx = l.x0 + l.side + 1.5 * l.side * col;
    cy = l.y0 + h / 2 + h * row + ((col & 1) ? h / 2 : 0);
}

Color valueColor(int field, int32_t value) {
    static const Color kColors[] = {
        {60, 60, 70},    {90, 140, 255}, {230, 70, 60},   {80, 220, 110},  {200, 140, 60}, {240, 220, 80},
        {170, 110, 240}, {80, 220, 220}, {240, 120, 230}, {150, 150, 150}, {120, 200, 60}, {255, 170, 120},
    };
    if (field == 4) { // economic: dark to bright by value
        const int v = std::clamp(value * 12, 0, 200);
        return {uint8_t(30 + v / 4), uint8_t(40 + v), uint8_t(40 + v / 3)};
    }
    if (value <= 0)
        return kColors[0];
    return kColors[1 + (value - 1) % 11];
}

int32_t fieldOf(const MetaverseMap::Cell &c, int field) {
    switch (field) {
    case 0:
        return c.region;
    case 1:
        return c.terrain;
    case 2:
        return c.planet;
    case 3:
        return c.base;
    default:
        return c.economic;
    }
}

const std::vector<std::string> *namesOf(const MetaverseMap &map, int field) {
    switch (field) {
    case 0:
        return &map.regions;
    case 1:
        return &map.terrains;
    case 2:
        return &map.planets;
    case 3:
        return &map.bases;
    default:
        return nullptr;
    }
}

} // namespace

MapPage::MapPage(ViewerContext &ctx) : Page(ctx) {
    _files = filesWithExtension(ctx.root, ".mvm");
}

void MapPage::activate() {
    load();
    show();
}

void MapPage::load() {
    _error.clear();
    _hover = -1;
    _map = MetaverseMap{};
    std::vector<uint8_t> data;
    if (!readFile(_ctx.root / _files[_current], data))
        _error = "Can't read it";
    else if (Error err = _map.read(data))
        _error = err.message;
}

int MapPage::hexAt(int x, int y) const {
    if (_map.cells.empty())
        return -1;
    const Layout l = layoutFor(_map);
    // The nearest centre, if the point is inside its hex's circle.
    int best = -1;
    double bestD = l.side * l.side * 0.75;
    for (uint32_t row = 0; row < _map.height; ++row)
        for (uint32_t col = 0; col < _map.width; ++col) {
            double cx = 0;
            double cy = 0;
            centre(l, col, row, cx, cy);
            const double d = (cx - x) * (cx - x) + (cy - y) * (cy - y);
            if (d < bestD) {
                bestD = d;
                best = int(row * _map.width + col);
            }
        }
    return best;
}

void MapPage::handleEvent(const Event &ev) {
    if (_files.empty())
        return;
    if (ev.type == EventType::kMouseMove) {
        const int hex = hexAt(ev.mouseX, ev.mouseY);
        if (hex != _hover) {
            _hover = hex;
            show();
        }
        return;
    }
    if (ev.type != EventType::kKeyDown)
        return;
    const size_t n = _files.size();
    switch (ev.key) {
    case key::kLeft:
        _current = (_current + n - 1) % n;
        load();
        break;
    case key::kRight:
        _current = (_current + 1) % n;
        load();
        break;
    case 'f':
        _field = (_field + 1) % kFields;
        break;
    default:
        return;
    }
    show();
}

void MapPage::show() {
    clear({20, 22, 28});
    if (_files.empty())
        return;
    const auto &rel = _files[_current];
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s  (%zu of %zu): %ux%u hexes, by %s", rel.generic_string().c_str(), _current + 1,
                  _files.size(), _map.width, _map.height, kFieldNames[_field]);
    text(4, 2, buf, {150, 200, 255});
    text(4, 2 + font::kLineHeight, "Left/Right: maps  F: the field shown  the mouse over a hex: its fields",
         {170, 170, 170});
    setTitle(rel.filename().string());
    if (!_error.empty()) {
        text(4, kTop, _error, {255, 120, 120});
        return;
    }
    const Layout l = layoutFor(_map);
    const double half = std::sqrt(3.0) * l.side / 2;
    for (uint32_t row = 0; row < _map.height; ++row)
        for (uint32_t col = 0; col < _map.width; ++col) {
            const int index = int(row * _map.width + col);
            const auto &cell = _map.cells[size_t(index)];
            Color c = valueColor(_field, fieldOf(cell, _field));
            if (index == _hover)
                c = {255, 255, 255};
            double cx = 0;
            double cy = 0;
            centre(l, col, row, cx, cy);
            // Row by row, a flat-topped hex less a pixel's border.
            for (int dy = int(-half) + 1; dy < int(half); ++dy) {
                const double w = l.side - std::abs(dy) / std::sqrt(3.0) - 1;
                fill(int(cx - w), int(cy) + dy, int(2 * w), 1, c);
            }
        }
    // The legend: the field's names and their colours, or the hex's fields.
    int y = kScreenH - kLegendLines * font::kLineHeight - 2;
    if (_hover >= 0) {
        const auto &cell = _map.cells[size_t(_hover)];
        const auto name = [](const std::vector<std::string> &names, int32_t i) {
            return i >= 0 && size_t(i) < names.size() ? names[size_t(i)] : std::to_string(i);
        };
        std::snprintf(buf, sizeof buf, "hex %d,%d: %s, %s, %s, %s", _hover % int(_map.width), _hover / int(_map.width),
                      name(_map.regions, cell.region).c_str(), name(_map.terrains, cell.terrain).c_str(),
                      name(_map.planets, cell.planet).c_str(), name(_map.bases, cell.base).c_str());
        text(4, y, buf);
        std::snprintf(buf, sizeof buf, "economic %d, impedence %g, strength %d", cell.economic, double(cell.impedence),
                      cell.strength);
        text(4, y + font::kLineHeight, buf);
        return;
    }
    const auto *names = namesOf(_map, _field);
    if (!names) {
        text(4, y, "brighter: more economic value", {170, 170, 170});
        return;
    }
    int x = 4;
    for (size_t i = 0; i < names->size(); ++i) {
        const std::string &label = (*names)[i];
        const int w = 14 + int(label.size() + 2) * font::kAdvance;
        if (x + w > kScreenW) {
            x = 4;
            y += font::kLineHeight;
        }
        fill(x, y + 2, 10, font::kLineHeight - 4, valueColor(_field, int32_t(i)));
        text(x + 14, y, label);
        x += w;
    }
}

} // namespace reluva::sfc
