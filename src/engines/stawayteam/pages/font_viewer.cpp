// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/pages/font_viewer.h"

#include "engines/stawayteam/pages/draw.h"

#include <algorithm>
#include <cstdio>

namespace reluva::stawayteam {

namespace {

constexpr int kColumns = 16;
// Between characters in the sample line: what the game puts there isn't
// known yet.
constexpr int kGap = 1;
const char *const kSample = "The Away Team beams down: 0123456789 ?!.,";

} // namespace

FontViewer::FontViewer(System &sys, Surface &screen, const Archive &data)
    : _sys(sys), _screen(screen), _data(data), _files(data, [](const std::string &name) {
          return name.ends_with(".seq16") && name.starts_with("cache/fonts/");
      }) {
}

void FontViewer::activate() {
    if (_font.object.className.empty() && _error.empty())
        load();
    draw();
}

void FontViewer::load() {
    _error.clear();
    _scroll = 0;
    std::vector<uint8_t> bytes;
    if (Error err = _data.read(_files.current(), bytes)) {
        _error = err.message;
        return;
    }
    if (Error err = readBinaryObject(bytes.data(), bytes.size(), "", _font))
        _error = err.message;
    else if (!_font.sequence)
        _error = "not a sequence";
}

void FontViewer::handleKey(uint32_t key) {
    if (key == key::kDown) {
        ++_scroll;
    } else if (key == key::kUp) {
        _scroll = std::max(0, _scroll - 1);
    } else if (key == 'b') {
        _backdrop = (_backdrop + 1) % 3;
    } else if (_files.handleKey(key)) {
        load();
    } else {
        return;
    }
    draw();
}

void FontViewer::draw() {
    clearPage(_screen);
    std::string status = _error;
    if (_error.empty()) {
        const auto &frames = _font.sequence->frames;
        const long first = _font.object.integer("First ASCII").value_or(0);
        int cellW = 1;
        int cellH = 1;
        for (const auto &f : frames)
            if (f) {
                cellW = std::max(cellW, f->width);
                cellH = std::max(cellH, f->height);
            }
        cellW += 4;
        cellH += 4;
        // The line of text, under the grid's top, then the grid.
        const int height = _screen.h - kStatusHeight;
        int x = 8;
        const int lineY = 6;
        for (const char *c = kSample; *c; ++c) {
            const long i = long(static_cast<unsigned char>(*c)) - first;
            if (i < 0 || size_t(i) >= frames.size() || !frames[size_t(i)])
                continue;
            const Frame &f = *frames[size_t(i)];
            if (x + f.width > _screen.w)
                break;
            drawBackdrop(_screen, x, lineY, f.width, f.height, _backdrop);
            image::blit(_screen, frameImage(f), x, lineY);
            x += f.width + kGap;
        }
        const int gridY = lineY + cellH + 10;
        const int rows = int((frames.size() + kColumns - 1) / kColumns);
        const int visible = std::max(1, (height - gridY) / cellH);
        _scroll = std::min(_scroll, std::max(0, rows - visible));
        for (int r = 0; r < visible && r + _scroll < rows; ++r)
            for (int c = 0; c < kColumns; ++c) {
                const size_t i = size_t(r + _scroll) * kColumns + size_t(c);
                if (i >= frames.size() || !frames[i])
                    continue;
                const int cx = 8 + c * std::max(cellW, 24);
                const int cy = gridY + r * cellH;
                drawBackdrop(_screen, cx, cy, frames[i]->width, frames[i]->height, _backdrop);
                image::blit(_screen, frameImage(*frames[i]), cx, cy);
            }
        // CFont's first property of its own has no name: shown as it is,
        // not yet known (formats/binary-object.md).
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "characters %ld to %ld, %zu frames, fixed width %s, its first number %s; rows %d-%d of %d", first,
                      _font.object.integer("Last ASCII").value_or(0), frames.size(),
                      _font.object.value("Fixed Width") ? _font.object.value("Fixed Width")->c_str() : "?",
                      _font.object.fields.size() > 10 ? _font.object.fields[10].value.c_str() : "?", _scroll + 1,
                      std::min(rows, _scroll + visible), rows);
        status = buf;
    }
    drawStatus(_screen, _files.position(), status);
    _sys.setWindowTitle("Reluva - Star Trek: Away Team - fonts (Left/Right, Up/Down: scroll, B: background, Tab: "
                        "next page)");
}

} // namespace reluva::stawayteam
