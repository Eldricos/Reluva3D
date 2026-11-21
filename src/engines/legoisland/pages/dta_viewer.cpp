// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's page for the worlds' *INF.DTA animation lists
// (docs/engines/legoisland/formats/dta.md).

#include "engines/legoisland/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace reluva::legoisland {

namespace {

constexpr int kListW = 230;
constexpr int kRowH = font::kLineHeight + 1;
constexpr Color kBackground = {40, 40, 48};
constexpr Color kText = {230, 230, 230};
constexpr Color kDim = {150, 150, 160};
constexpr Color kSelected = {70, 90, 140};
constexpr Color kError = {255, 120, 120};
constexpr Color kMapBack = {28, 30, 36};
constexpr Color kOtherCircle = {80, 84, 100};
constexpr Color kCircle = {150, 200, 255};

// The models' kinds' colours on the map: models, characters, copies, plants.
constexpr Color kKindColors[] = {{230, 230, 230}, {240, 200, 60},  {255, 120, 120},
                                 {120, 220, 120}, {255, 150, 220}, {120, 200, 255}};

void fillRect(Surface &s, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w);
    const int y1 = std::min(y + h, s.h);
    for (int py = y0; py < y1; ++py) {
        uint8_t *p = s.row(py) + size_t(x0) * 4;
        for (int px = x0; px < x1; ++px, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

void dot(Surface &s, int x, int y, int r, Color c) {
    fillRect(s, x - r, y - r, 2 * r + 1, 2 * r + 1, c);
}

// A circle's outline, as dots.
void circle(Surface &s, int cx, int cy, int r, Color c, int clipX, int clipY, int clipW, int clipH) {
    const int steps = std::max(24, r * 4);
    for (int i = 0; i < steps; i++) {
        const double a = 6.283185307 * i / steps;
        const int x = cx + int(std::lround(r * std::cos(a)));
        const int y = cy + int(std::lround(r * std::sin(a)));
        if (x >= clipX && y >= clipY && x < clipX + clipW && y < clipY + clipH)
            fillRect(s, x, y, 1, 1, c);
    }
}

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

} // namespace

DtaPage::DtaPage(Surface &screen, std::vector<std::filesystem::path> files)
    : _screen(screen), _files(std::move(files)) {
}

void DtaPage::activate() {
    if (_dta.animations.empty() && _error.empty() && !_files.empty())
        openFile(_file);
    redraw();
}

void DtaPage::openFile(size_t index) {
    _file = index;
    _current = 0;
    _error.clear();
    if (Error e = _dta.read(_files[index])) {
        _error = e.message;
        _dta = DtaFile();
    }
}

void DtaPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _files.empty())
        return;
    const size_t n = _dta.animations.size();
    switch (ev.key) {
    case key::kLeft:
    case key::kRight:
        openFile((_file + (ev.key == key::kRight ? 1 : _files.size() - 1)) % _files.size());
        break;
    case key::kUp:
        _current = _current > 0 ? _current - 1 : 0;
        break;
    case key::kDown:
        _current = n ? std::min(_current + 1, n - 1) : 0;
        break;
    case key::kPageUp:
        _current = _current > 20 ? _current - 20 : 0;
        break;
    case key::kPageDown:
        _current = n ? std::min(_current + 20, n - 1) : 0;
        break;
    case key::kHome:
        _current = 0;
        break;
    case key::kEnd:
        _current = n ? n - 1 : 0;
        break;
    default:
        return;
    }
    redraw();
}

void DtaPage::redraw() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
    if (_files.empty())
        return;
    font::drawText(_screen, 4, 4,
                   std::to_string(_file + 1) + "/" + std::to_string(_files.size()) + "  " +
                       _files[_file].filename().string() + ": " + std::to_string(_dta.animations.size()) +
                       " animations (Left/Right: another world, Tab: next page)",
                   kDim);
    const int top = 4 + 2 * kRowH;
    if (!_error.empty())
        font::drawText(_screen, 4, top, _error, kError);

    // The list, the current row kept in sight.
    const int rows = (_screen.h - top - 4) / kRowH;
    const size_t first = _current > size_t(rows / 2) ? _current - size_t(rows / 2) : 0;
    for (int i = 0; i < rows && first + size_t(i) < _dta.animations.size(); i++) {
        const DtaAnimation &a = _dta.animations[first + size_t(i)];
        const int y = top + i * kRowH;
        if (first + size_t(i) == _current)
            fillRect(_screen, 0, y - 1, kListW, kRowH, kSelected);
        font::drawText(_screen, 4, y, std::to_string(a.id) + " " + a.name + "  " + std::to_string(a.models.size()),
                       kText);
    }
    if (_dta.animations.empty())
        return;

    // The selected one's details.
    const DtaAnimation &a = _dta.animations[_current];
    const int x0 = kListW + 12;
    int y = top;
    auto line = [&](const std::string &s, Color c) {
        font::drawText(_screen, x0, y, s, c);
        y += kRowH;
    };
    // OBSTINF.DTA's ids are 100 more than their objects' (formats/dta.md).
    line(a.name + ": the SI object " + a.name + "_RunAnim; the list's id " + std::to_string(a.id), kText);
    char buf[200];
    std::snprintf(buf, sizeof buf, "value %s; flags %u %u %#04x %u; plays within %s of (%s, %s, %s)",
                  a.spot == 0xFFFF ? "none" : std::to_string(a.spot).c_str(), a.flags[0], a.flags[1], a.flags[2],
                  a.flags[3], number(a.radius).c_str(), number(a.center[0]).c_str(), number(a.center[1]).c_str(),
                  number(a.center[2]).c_str());
    line(buf, kText);
    static const char *const kKinds[] = {"?", "model", "character", "copy", "copy (4)", "plant"};
    const int detailRows = std::min<int>(int(a.models.size()), 18);
    for (int i = 0; i < detailRows; i++) {
        const DtaModel &m = a.models[size_t(i)];
        std::snprintf(buf, sizeof buf, "  %-12s %-9s at (%s, %s, %s)%s", m.name.c_str(),
                      kKinds[std::min<int>(m.kind, 5)], number(m.location[0]).c_str(), number(m.location[1]).c_str(),
                      number(m.location[2]).c_str(), m.flag ? "" : ", flag 0");
        line(buf, kKindColors[std::min<int>(m.kind, 5)]);
    }
    if (int(a.models.size()) > detailRows)
        line("  ... and " + std::to_string(a.models.size() - size_t(detailRows)) + " more", kDim);
    y += kRowH / 2;
    drawMap(x0, y, _screen.w - x0 - 8, _screen.h - y - 8);
}

// From above: x to the right, z up the page; every animation's circle,
// the selected one's bright, and its models' starting points.
void DtaPage::drawMap(int x, int y, int w, int h) {
    if (w < 40 || h < 40)
        return;
    fillRect(_screen, x, y, w, h, kMapBack);
    float lo[2] = {1e30f, 1e30f};
    float hi[2] = {-1e30f, -1e30f};
    auto grow = [&](float px, float pz, float r) {
        lo[0] = std::min(lo[0], px - r);
        hi[0] = std::max(hi[0], px + r);
        lo[1] = std::min(lo[1], pz - r);
        hi[1] = std::max(hi[1], pz + r);
    };
    for (const DtaAnimation &a : _dta.animations) {
        if (a.radius > 0)
            grow(a.center[0], a.center[2], a.radius);
        for (const DtaModel &m : a.models)
            grow(m.location[0], m.location[2], 0);
    }
    if (lo[0] > hi[0])
        return;
    const float span = std::max({hi[0] - lo[0], hi[1] - lo[1], 1.0f});
    const float scale = float(std::min(w, h) - 16) / span;
    auto toScreen = [&](float px, float pz, int &sx, int &sy) {
        sx = x + 8 + int((px - lo[0]) * scale);
        sy = y + h - 8 - int((pz - lo[1]) * scale);
    };
    for (size_t pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < _dta.animations.size(); i++) {
            const DtaAnimation &a = _dta.animations[i];
            if ((i == _current) != (pass == 1) || a.radius <= 0)
                continue;
            int sx = 0;
            int sy = 0;
            toScreen(a.center[0], a.center[2], sx, sy);
            circle(_screen, sx, sy, std::max(2, int(a.radius * scale)), pass ? kCircle : kOtherCircle, x, y, w, h);
        }
    for (const DtaModel &m : _dta.animations[_current].models) {
        int sx = 0;
        int sy = 0;
        toScreen(m.location[0], m.location[2], sx, sy);
        dot(_screen, sx, sy, 2, kKindColors[std::min<int>(m.kind, 5)]);
    }
    font::drawText(_screen, x + 4, y + 4, "from above: x across, z up", kDim);
}

} // namespace reluva::legoisland
