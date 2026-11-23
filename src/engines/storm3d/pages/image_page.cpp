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
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
    drawFitted(_screen, img, _alpha, area);
    const std::string line = std::to_string(_current + 1) + "/" + std::to_string(_names.size()) + "  " + name + "  " +
                             what + (_alpha ? "  alpha" : "");
    if (_status)
        drawStatus(_screen, line);
    _sys.setWindowTitle("Reluva viewer - Images - " + line);
}

} // namespace reluva::storm3d
