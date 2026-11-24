// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/shell_draw.h"

#include "graphics/font.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace reluva::storm3d {

Error loadPicture(const GameFiles &files, const std::string &name, image::Image &out) {
    std::vector<uint8_t> data;
    if (Error err = files.read(name, data))
        return err;
    if (Error err = image::decode(data, out))
        return Error::fail(name + ": " + err.message);
    return Error::success();
}

void drawPixels(Surface &screen, const uint8_t *rgba, int w, int h, int x, int y, bool keyed) {
    if (screen.format != PixelFormat::kRGBA32 || !rgba)
        return;
    for (int row = 0; row < h; ++row) {
        const int sy = y + row;
        if (sy < 0 || sy >= screen.h)
            continue;
        for (int col = 0; col < w; ++col) {
            const int sx = x + col;
            if (sx < 0 || sx >= screen.w)
                continue;
            const uint8_t *from = rgba + (size_t(row) * size_t(w) + size_t(col)) * 4;
            if (keyed && from[0] == 255 && from[1] == 0 && from[2] == 255)
                continue;
            uint8_t *to = screen.row(sy) + size_t(sx) * 4;
            std::memcpy(to, from, 3);
            to[3] = 255;
        }
    }
}

Error ShellButton::load(const GameFiles &files, const std::string &folder, const std::string &name, int atX, int atY) {
    x = atX;
    y = atY;
    for (size_t i = 0; i < looks.size(); ++i)
        if (Error err = loadPicture(files, folder + "/" + name + std::to_string(i + 1) + ".bmp", looks[i]))
            return err;
    return Error::success();
}

void ShellButton::draw(Surface &screen, bool lit, bool pressed, bool keyed) const {
    drawPicture(screen, looks[pressed && lit ? 2 : lit ? 1 : 0], x, y, keyed);
}

void ShellFont::load() {
    if (_tried)
        return;
    _tried = true;
    std::vector<std::filesystem::path> places;
    for (const char *var : {"WINDIR", "SystemRoot"})
        if (const char *dir = std::getenv(var))
            places.push_back(std::filesystem::path(dir) / "Fonts" / "arialbd.ttf");
    for (const char *dir : {"/System/Library/Fonts/Supplemental", "/Library/Fonts"})
        places.push_back(std::filesystem::path(dir) / "Arial Bold.ttf");
    for (const char *dir : {"/usr/share/fonts/truetype/msttcorefonts", "/usr/share/fonts/msttcore"})
        places.push_back(std::filesystem::path(dir) / "Arial_Bold.ttf");
    places.push_back("/usr/share/fonts/TTF/arialbd.ttf");
    for (const char *dir : {"/usr/share/fonts/truetype/liberation", "/usr/share/fonts/truetype/liberation2",
                            "/usr/share/fonts/liberation-sans", "/usr/share/fonts/liberation", "/usr/share/fonts/TTF",
                            "/usr/local/share/fonts"})
        places.push_back(std::filesystem::path(dir) / "LiberationSans-Bold.ttf");
    for (const std::filesystem::path &place : places) {
        std::ifstream in(place, std::ios::binary);
        if (!in)
            continue;
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!_font.load(std::move(data)))
            return;
    }
}

int ShellFont::width(std::string_view utf8, float pixels) {
    load();
    return _font.loaded() ? _font.width(utf8, pixels) : int(font::length(utf8)) * font::kAdvance;
}

void ShellFont::draw(Surface &screen, int x, int y, std::string_view utf8, float pixels, Color colour) {
    load();
    if (_font.loaded())
        _font.draw(screen, x, y, utf8, pixels, colour);
    else
        font::drawText(screen, x, y, utf8, colour);
}

void ShellFont::drawCentred(Surface &screen, int x, int y, int w, int h, std::string_view utf8, float pixels,
                            Color colour) {
    load();
    // A line as tall as the font's size, as the emulated GDI that drew the
    // game's frames centres it (DT_VCENTER).
    const int lineH = _font.loaded() ? int(std::ceil(pixels)) : font::kLineHeight;
    draw(screen, x + (w - width(utf8, pixels)) / 2, y + (h - lineH) / 2, utf8, pixels, colour);
}

} // namespace reluva::storm3d
