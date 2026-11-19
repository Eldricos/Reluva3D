// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/pages/viewers.h"

#include "graphics/font.h"
#include "common/fs.h"
#include "image/image.h"

#include <algorithm>
#include <filesystem>

namespace reluva::botf {

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

ImageViewer::ImageViewer(System &sys, Surface &screen, const std::vector<const Archive *> &archives)
    : _sys(sys), _screen(screen) {
    for (const Archive *a : archives) {
        std::vector<std::string> names = a->list();
        std::sort(names.begin(), names.end(),
                  [](const std::string &x, const std::string &y) { return fs::toLower(x) < fs::toLower(y); });
        for (std::string &n : names)
            if (fs::toLower(n).ends_with(".tga"))
                _images.push_back({a, std::move(n)});
    }
}

void ImageViewer::activate() {
    show(_current);
}

void ImageViewer::handleKey(uint32_t key) {
    if (_images.empty())
        return;
    const size_t n = _images.size();
    size_t next = _current;
    if (key == key::kRight)
        next = (_current + 1) % n;
    else if (key == key::kLeft)
        next = (_current + n - 1) % n;
    else if (key == key::kPageDown)
        next = std::min(_current + 10, n - 1);
    else if (key == key::kPageUp)
        next = _current >= 10 ? _current - 10 : 0;
    else if (key == key::kHome)
        next = 0;
    else if (key == key::kEnd)
        next = n - 1;
    else if (key == 'k') {
        _key = !_key;
        next = _current;
    } else if (key == 'i') {
        _info = !_info;
        next = _current;
    } else if (key == 's') {
        // The first image of the next archive, wrapping round.
        next = _current;
        do
            next = (next + 1) % n;
        while (next != _current && _images[next].archive == _images[_current].archive);
        while (next > 0 && _images[next - 1].archive == _images[next].archive)
            --next;
    } else
        return;
    show(next);
}

void ImageViewer::show(size_t index) {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, {40, 40, 48});
    if (_images.empty())
        return;
    _current = index;
    const ImageRef &ref = _images[index];
    std::string status;
    std::vector<uint8_t> data;
    image::Image img;
    if (Error err = ref.archive->read(ref.name, data))
        status = err.message;
    else if (Error err2 = image::decode(data, img))
        status = "can't decode: " + err2.message;
    else {
        const size_t keyed = _key ? applyColorKey(img) : 0;
        // Images up to the screen's size (the full-screen 800x600 ones
        // included) are shown at their own size, in the middle; larger ones
        // at half or a third of it.
        int scale = 1;
        while (img.w / scale > _screen.w || img.h / scale > _screen.h)
            ++scale;
        image::Image shown = img;
        if (scale > 1) {
            shown.w = img.w / scale;
            shown.h = img.h / scale;
            shown.rgba.resize(size_t(shown.w) * shown.h * 4);
            for (int y = 0; y < shown.h; ++y)
                for (int x = 0; x < shown.w; ++x)
                    std::copy_n(img.pixel(x * scale, y * scale), 4, shown.pixel(x, y));
        }
        image::blit(_screen, shown, (_screen.w - shown.w) / 2, (_screen.h - shown.h) / 2);
        status = std::to_string(img.w) + "x" + std::to_string(img.h) +
                 (scale > 1 ? " (shown at 1/" + std::to_string(scale) + ")" : "");
        if (_key)
            status += "  colour key: " + std::to_string(keyed) + " pixels transparent";
    }
    const std::string line = std::to_string(index + 1) + "/" + std::to_string(_images.size()) + "  " +
                             std::filesystem::path(ref.archive->describe()).filename().string() + ": " + ref.name +
                             "  " + status;
    // The status line goes over the bottom of the picture; I hides it.
    if (_info) {
        fillRect(_screen, 0, _screen.h - 14, _screen.w, 14, {0, 0, 0});
        font::drawText(_screen, 4, _screen.h - 12, line, {230, 230, 230});
    }
    _sys.setWindowTitle("Reluva - Birth of the Federation - images " + std::to_string(index + 1) + "/" +
                        std::to_string(_images.size()) + ": " + ref.name +
                        " (Left/Right, S: next archive, K: colour key, I: info, Tab: next page)");
}

} // namespace reluva::botf
