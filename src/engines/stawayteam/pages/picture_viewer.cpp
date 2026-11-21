// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/pages/picture_viewer.h"

#include "common/fs.h"
#include "engines/stawayteam/formats/frame.h"
#include "graphics/font.h"
#include "image/image.h"

#include <algorithm>
#include <cstdio>

namespace reluva::stawayteam {

namespace {

// The status line's two rows, at the bottom.
constexpr int kStatusHeight = 26;

void fill(Surface &s, int x, int y, int w, int h, Color c) {
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

std::string folderOf(const std::string &name) {
    const size_t slash = name.rfind('/');
    return slash == std::string::npos ? "" : name.substr(0, slash);
}

// The picture `zoom` times larger, each pixel a square.
image::Image enlarged(const image::Image &img, int zoom) {
    if (zoom == 1)
        return img;
    image::Image out;
    out.w = img.w * zoom;
    out.h = img.h * zoom;
    out.rgba.resize(size_t(out.w) * size_t(out.h) * 4);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x)
            std::copy_n(img.pixel(x / zoom, y / zoom), 4, out.pixel(x, y));
    return out;
}

} // namespace

PictureViewer::PictureViewer(System &sys, Surface &screen, const Archive &data)
    : _sys(sys), _screen(screen), _data(data) {
    for (std::string &name : data.list())
        if (fs::toLower(name).ends_with(".frm16"))
            _names.push_back(std::move(name));
    std::sort(_names.begin(), _names.end(),
              [](const std::string &a, const std::string &b) { return fs::toLower(a) < fs::toLower(b); });
}

void PictureViewer::activate() {
    show(_current);
}

void PictureViewer::handleKey(uint32_t key) {
    if (_names.empty())
        return;
    const size_t n = _names.size();
    size_t next = _current;
    if (key == key::kRight) {
        next = (_current + 1) % n;
    } else if (key == key::kLeft) {
        next = (_current + n - 1) % n;
    } else if (key == key::kPageDown) {
        next = std::min(_current + 10, n - 1);
    } else if (key == key::kPageUp) {
        next = _current >= 10 ? _current - 10 : 0;
    } else if (key == key::kHome) {
        next = 0;
    } else if (key == key::kEnd) {
        next = n - 1;
    } else if (key == 'f') {
        const std::string folder = folderOf(_names[_current]);
        next = _current;
        do
            next = (next + 1) % n;
        while (next != _current && folderOf(_names[next]) == folder);
    } else if (key == 'b') {
        _background = (_background + 1) % 3;
    } else if (key == 'z') {
        _zoom = (_zoom + 1) % 5;
    } else if (key == 'i') {
        _info = !_info;
    } else {
        return;
    }
    show(next);
}

void PictureViewer::show(size_t index) {
    fill(_screen, 0, 0, _screen.w, _screen.h, {40, 40, 48});
    if (_names.empty())
        return;
    _current = index;
    const std::string &name = _names[index];
    std::string status;
    std::vector<uint8_t> data;
    Frame frame;
    if (Error err = _data.read(name, data)) {
        status = err.message;
    } else if (Error err2 = readFrame(data.data(), data.size(), frame)) {
        status = err2.message;
    } else {
        int zoom = _zoom;
        if (zoom == 0) {
            zoom = 4;
            while (zoom > 1 && (frame.width * zoom > _screen.w || frame.height * zoom > _screen.h - kStatusHeight))
                --zoom;
        }
        const image::Image shown = enlarged(frameImage(frame), zoom);
        const int x = (_screen.w - shown.w) / 2;
        const int y = (_screen.h - kStatusHeight - shown.h) / 2;
        // Behind the picture only, so its edges show.
        if (_background == 1) {
            for (int ty = 0; ty < shown.h; ty += 8)
                for (int tx = 0; tx < shown.w; tx += 8) {
                    const uint8_t v = ((tx / 8 + ty / 8) % 2) ? 96 : 160;
                    fill(_screen, x + tx, y + ty, std::min(8, shown.w - tx), std::min(8, shown.h - ty), {v, v, v});
                }
        } else if (_background == 2) {
            fill(_screen, x, y, shown.w, shown.h, {255, 0, 255});
        }
        image::blit(_screen, shown, x, y);
        size_t blended = 0;
        for (uint8_t a : frame.alpha)
            blended += a != 0 && a != 255;
        char buf[160];
        std::snprintf(buf, sizeof buf, "%dx%d, origin %d,%d, flags 0x%x, %zu blended pixels, x%d", frame.width,
                      frame.height, frame.originX, frame.originY, frame.flags, blended, zoom);
        status = buf;
    }
    if (_info) {
        const std::string line = std::to_string(index + 1) + "/" + std::to_string(_names.size()) + "  " + name;
        fill(_screen, 0, _screen.h - kStatusHeight, _screen.w, kStatusHeight, {0, 0, 0});
        font::drawText(_screen, 4, _screen.h - 24, line, {230, 230, 230});
        font::drawText(_screen, 4, _screen.h - 12, status, {180, 180, 190});
    }
    _sys.setWindowTitle("Reluva - Star Trek: Away Team - pictures " + std::to_string(index + 1) + "/" +
                        std::to_string(_names.size()) +
                        " (Left/Right, F: next folder, B: background, Z: zoom, I: info, Esc: leave)");
}

} // namespace reluva::stawayteam
