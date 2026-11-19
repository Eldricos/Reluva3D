// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/pages/viewers.h"

#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>

namespace reluva::botf {

namespace {

constexpr const char *kSamples[] = {
    "The quick brown fox jumps over the lazy dog.",
    "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG! 1234567890",
    "`-=\\[];',./ ~!@#$%^&*()_+|{}:\"<>?",
    "Ça, élève! Où est la Fédération? Größe ¿Qué? ¡Sí! ® © ™ … — –",
};

} // namespace

FontViewer::FontViewer(System &sys, Surface &screen, const Archive &archive)
    : _sys(sys), _screen(screen), _archive(archive) {
    for (std::string &n : archive.list())
        if (fs::toLower(n).ends_with(".fnt"))
            _names.push_back(std::move(n));
    std::sort(_names.begin(), _names.end(),
              [](const std::string &x, const std::string &y) { return fs::toLower(x) < fs::toLower(y); });
}

void FontViewer::activate() {
    show();
}

void FontViewer::handleKey(uint32_t key) {
    if (_names.empty())
        return;
    if (key == key::kRight)
        _current = (_current + 1) % _names.size();
    else if (key == key::kLeft)
        _current = (_current + _names.size() - 1) % _names.size();
    else if (key == '+' || key == '=')
        _scale = std::min(_scale + 1, 4);
    else if (key == '-')
        _scale = std::max(_scale - 1, 1);
    else
        return;
    show();
}

void FontViewer::show() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, {24, 24, 32});
    if (_names.empty())
        return;
    const std::string &name = _names[_current];
    std::string status;
    std::vector<uint8_t> data;
    Font font;
    if (Error err = _archive.read(name, data))
        status = err.message;
    else if (Error err2 = font.load(data))
        status = "can't read: " + err2.message;
    else {
        // The glyph sheet as stored, frames and all, at twice its size if
        // it fits; then the samples.
        const image::Image &sheet = font.sheet();
        const int zoom = sheet.w * 2 <= _screen.w - 16 && sheet.h * 2 <= 300 ? 2 : 1;
        for (int y = 0; y < sheet.h; ++y)
            for (int x = 0; x < sheet.w; ++x) {
                const uint8_t *p = sheet.pixel(x, y);
                fillRect(_screen, 8 + x * zoom, 24 + y * zoom, zoom, zoom, {p[0], p[1], p[2]});
            }
        int y = 24 + sheet.h * zoom + 12;
        for (const char *sample : kSamples) {
            font.draw(_screen, 8, y, sample, std::nullopt, _scale);
            y += font.lineHeight() * _scale + 4;
        }
        status = "line height " + std::to_string(font.lineHeight()) + ", space " + std::to_string(font.spaceAdvance()) +
                 ", sheet " + std::to_string(sheet.w) + "x" + std::to_string(sheet.h) + ", samples at " +
                 std::to_string(_scale) + "x";
    }
    font::drawText(_screen, 8, 6,
                   std::to_string(_current + 1) + "/" + std::to_string(_names.size()) + "  " + name + "  " + status,
                   {230, 230, 230});
    font::drawText(_screen, 8, _screen.h - 16, "Left/Right: font  +/-: sample size  Tab: next page", {140, 140, 160});
    _sys.setWindowTitle("Reluva - Birth of the Federation - fonts " + std::to_string(_current + 1) + "/" +
                        std::to_string(_names.size()) + ": " + name);
}

} // namespace reluva::botf
