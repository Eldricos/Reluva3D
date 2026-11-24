// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/archive.h"
#include "engines/legoloco/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace reluva::legoloco {

namespace {

constexpr int kRowHeight = 12;
constexpr int kCell = 12;

// A grid's cells as coloured squares, value by value, layer by layer
// side by side; returns its height.
int drawGrid(Surface &s, const char *title, const Occupancy &grid, int x, int y) {
    static const Color kColors[] = {{40, 40, 50},  {90, 160, 90},  {200, 170, 60}, {80, 140, 210},
                                    {200, 90, 90}, {160, 90, 200}, {90, 200, 200}, {220, 220, 220}};
    font::drawText(s, x, y, title, {255, 220, 120});
    y += kRowHeight + 2;
    for (int layer = 0; layer < grid.layers; ++layer) {
        const int lx = x + layer * (grid.columns * kCell + 12);
        for (int row = 0; row < grid.rows; ++row)
            for (int column = 0; column < grid.columns; ++column) {
                const int v = grid.at(layer, row, column);
                fillRect(s, lx + column * kCell, y + row * kCell, kCell - 1, kCell - 1, kColors[size_t(v) % 8]);
                font::drawText(s, lx + column * kCell + 3, y + row * kCell + 2, std::to_string(v),
                               v ? Color{0, 0, 0} : Color{120, 120, 140});
            }
    }
    return kRowHeight + 2 + grid.rows * kCell + 8;
}

} // namespace

DefinitionPage::DefinitionPage(System &sys, Surface &screen, const GameFiles &files)
    : _sys(sys), _screen(screen), _files(files) {
    const auto &entries = files.resources().entries();
    for (size_t i = 0; i < entries.size(); ++i)
        if (normalizeArchiveName(entries[i].name).ends_with(".dat"))
            _entries.push_back(i);
}

void DefinitionPage::load() {
    _error.clear();
    _lines.clear();
    _scroll = 0;
    _hasPicture = false;
    if (_entries.empty())
        return;
    const ResourceFile &rf = _files.resources();
    const std::string &name = rf.entries()[_entries[_current]].name;
    std::vector<uint8_t> data;
    Error err = rf.readEntry(_entries[_current], data);
    if (!err)
        err = parseObjectDefinition(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()),
                                    _definition);
    if (err) {
        _error = err.message;
    } else {
        for (size_t p = 0; p < _definition.parts.size(); ++p) {
            _lines.push_back("part " + std::to_string(p + 1));
            for (const auto &line : _definition.parts[p]) {
                std::string text = "  " + line.keyword;
                for (const auto &v : line.values)
                    text += " " + v;
                _lines.push_back(text);
            }
        }
        if (!_definition.frameSets.empty()) {
            _lines.push_back(std::to_string(_definition.totalFrames) + " frames, cursor frame sets " +
                             std::to_string(_definition.cursorFrameSets[0]) + " " +
                             std::to_string(_definition.cursorFrameSets[1]));
            _lines.push_back("  set             start   end delay split restart  next  sound  replay volume flip");
            for (const auto &s : _definition.frameSets) {
                char buf[160];
                std::snprintf(buf, sizeof buf, "  %-14s %6u %5u %5u %5u %7d %5d %6d %7d %6u %4u", s.name.c_str(),
                              s.start, s.end, s.delay, s.split, s.restartDelay, s.next, s.sound, s.replayDelay,
                              s.soundCategory, s.flipX);
                _lines.push_back(buf);
            }
        }
    }
    // The picture beside it, if there is one: its first frame.
    const int picture = rf.find(name.substr(0, name.size() - 4) + ".bmp");
    if (picture >= 0 && !loadPicture(_files, size_t(picture), _picture))
        _hasPicture = true;
    draw();
}

void DefinitionPage::handleKey(uint32_t key) {
    const size_t n = _entries.size();
    const size_t old = _current;
    if (key == key::kRight)
        _current = (_current + 1) % n;
    else if (key == key::kLeft)
        _current = (_current + n - 1) % n;
    else if (key == key::kPageDown)
        _current = std::min(n - 1, _current + 20);
    else if (key == key::kPageUp)
        _current = _current >= 20 ? _current - 20 : 0;
    else if (key == key::kHome)
        _current = 0;
    else if (key == key::kEnd)
        _current = n - 1;
    else if (key == key::kDown && _scroll + 1 < _lines.size())
        ++_scroll;
    else if (key == key::kUp && _scroll > 0)
        --_scroll;
    else
        return;
    if (_current != old)
        load();
    else
        draw();
}

void DefinitionPage::draw() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, {20, 24, 36});
    if (_entries.empty())
        return;
    const std::string &name = _files.resources().entries()[_entries[_current]].name;
    font::drawText(_screen, 8, 8, name, {255, 220, 120});
    if (!_error.empty())
        font::drawText(_screen, 8, 24, _error, {255, 120, 120});

    // The text on the left; the grids and the picture on the right.
    const int bottom = kScreenH - 30;
    for (size_t i = _scroll; i < _lines.size(); ++i) {
        const int y = 28 + int(i - _scroll) * kRowHeight;
        if (y + kRowHeight > bottom)
            break;
        font::drawText(_screen, 8, y, _lines[i], {220, 220, 220});
    }
    int y = 28;
    const int x = kScreenW / 2 + 40;
    if (_definition.physical)
        y += drawGrid(_screen, "physical occupancy (layers left to right)", *_definition.physical, x, y);
    if (_definition.bitmap)
        y += drawGrid(_screen, "bitmap occupancy", *_definition.bitmap, x, y);
    if (_hasPicture && y + _picture.bitmap.surface.h * 2 < bottom)
        drawFrame(_screen, _picture, 0, x, y + 8, 2, true);
    drawStatus(_screen, "[" + std::to_string(_current + 1) + "/" + std::to_string(_entries.size()) +
                            "]  Left/Right definition  Up/Down scroll  Tab page");
}

} // namespace reluva::legoloco
