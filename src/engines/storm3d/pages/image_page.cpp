// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/image_page.h"

#include "common/archive.h"
#include "common/events.h"
#include "common/text.h"
#include "graphics/font.h"
#include "image/image.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace reluva::storm3d {

namespace {

constexpr Color kBackground = {32, 32, 40};

// The folder, the name without its extension, and the copy's level: 0 for
// the texture itself, 1-4 for its smaller copies, and after those any
// other extension (Fpod.6 and Fpod.56 are TGAs too).
std::tuple<std::string, std::string, int, std::string> sortKey(const std::string &name) {
    const std::string n = normalizeArchiveName(name);
    const size_t slash = n.rfind('/');
    const std::string folder = slash == std::string::npos ? "" : n.substr(0, slash);
    const std::string file = slash == std::string::npos ? n : n.substr(slash + 1);
    const size_t dot = file.rfind('.');
    const std::string stem = dot == std::string::npos ? file : file.substr(0, dot);
    const std::string ext = dot == std::string::npos ? "" : file.substr(dot + 1);
    int level = 9;
    if (ext.empty() || ext == "tga" || ext == "bmp")
        level = 0;
    else if (ext.size() == 1 && ext[0] >= '1' && ext[0] <= '4')
        level = ext[0] - '0';
    return {folder, stem, level, ext};
}

// The image drawn as large as fits, up to 4 times its size, centred above
// the status line; over a checkerboard when its alpha is shown.
void drawFitted(Surface &screen, const image::Image &img, bool alpha, int areaHeight) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    if (img.w <= 0 || img.h <= 0)
        return;
    const double fit = std::min(double(screen.w) / img.w, double(areaHeight) / img.h);
    const double scale = fit >= 1 ? std::min(4.0, std::floor(fit)) : fit;
    const int w = std::max(1, int(img.w * scale));
    const int h = std::max(1, int(img.h * scale));
    const int x0 = (screen.w - w) / 2;
    const int y0 = (areaHeight - h) / 2;
    for (int y = 0; y < h; ++y) {
        uint8_t *out = screen.row(y0 + y) + size_t(x0) * 4;
        const int sy = std::min(img.h - 1, int(y / scale));
        for (int x = 0; x < w; ++x, out += 4) {
            const uint8_t *in = img.pixel(std::min(img.w - 1, int(x / scale)), sy);
            if (!alpha) {
                out[0] = in[0];
                out[1] = in[1];
                out[2] = in[2];
            } else {
                const uint8_t check = ((x / 8 + y / 8) % 2) ? 96 : 160;
                for (int c = 0; c < 3; ++c)
                    out[c] = uint8_t((in[c] * in[3] + check * (255 - in[3])) / 255);
            }
            out[3] = 255;
        }
    }
}

} // namespace

bool ImagePage::listedBefore(const std::string &a, const std::string &b) {
    return sortKey(a) < sortKey(b);
}

bool ImagePage::isPicture(const std::string &name) {
    const std::string n = normalizeArchiveName(name);
    if (text::hasSuffixIgnoreCase(n, ".tga") || text::hasSuffixIgnoreCase(n, ".bmp"))
        return true;
    // Everything else in Textures/ is a TGA under another name, but for a
    // palette and Paint Shop Pro's browse file.
    return n.rfind("textures/", 0) == 0 && !text::hasSuffixIgnoreCase(n, ".pal") &&
           !text::hasSuffixIgnoreCase(n, ".jbf");
}

ImagePage::ImagePage(System &sys, Surface &screen, const GameFiles &files) : _sys(sys), _screen(screen), _files(files) {
    for (const std::string &name : files.list())
        if (isPicture(name))
            _names.push_back(name);
    std::sort(_names.begin(), _names.end(), listedBefore);
}

void ImagePage::handleKey(uint32_t key) {
    switch (key) {
    case key::kLeft:
        _current = stepIndex(_current, -1, _names.size());
        break;
    case key::kRight:
        _current = stepIndex(_current, 1, _names.size());
        break;
    case key::kPageUp:
        _current = stepIndex(_current, -10, _names.size());
        break;
    case key::kPageDown:
        _current = stepIndex(_current, 10, _names.size());
        break;
    case key::kHome:
        _current = 0;
        break;
    case key::kEnd:
        _current = _names.empty() ? 0 : _names.size() - 1;
        break;
    case 'f': {
        const std::string folder = std::get<0>(sortKey(_names[_current]));
        size_t i = _current;
        while (i < _names.size() && std::get<0>(sortKey(_names[i])) == folder)
            ++i;
        _current = i < _names.size() ? i : 0;
        break;
    }
    case 'a':
        _alpha = !_alpha;
        break;
    case 'i':
        _status = !_status;
        break;
    default:
        return;
    }
    show();
}

void ImagePage::show() {
    if (_names.empty())
        return;
    const std::string &name = _names[_current];
    std::vector<uint8_t> data;
    image::Image img;
    std::string what;
    if (Error err = _files.read(name, data)) {
        what = err.message;
    } else if (Error err2 = image::decode(data, img)) {
        what = "not read: " + err2.message;
        img = {};
    } else {
        what = std::to_string(img.w) + "x" + std::to_string(img.h);
    }
    const int area = _status ? _screen.h - font::kLineHeight - 4 : _screen.h;
    drawFitted(_screen, img, _alpha, area);
    const std::string line = std::to_string(_current + 1) + "/" + std::to_string(_names.size()) + "  " + name + "  " +
                             what + (_alpha ? "  alpha" : "");
    if (_status)
        drawStatus(_screen, line);
    _sys.setWindowTitle("Reluva viewer - Images - " + line);
}

} // namespace reluva::storm3d
