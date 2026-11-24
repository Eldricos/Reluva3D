// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/sign_painter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace reluva::tpw {

namespace {

// The fonts' archive stands for a folder of its name.
constexpr const char *kFontFolder = "fonts/";
// What a line may take of the canvas's width, so its letters don't touch the edges.
constexpr int kMargin = 8;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool endsWith(const std::string &s, const std::string &end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

// A model's texture by its name, whatever its case and type: "sign1".
size_t textureNamed(const Model &model, const std::string &stem) {
    for (size_t i = 0; i < model.textures().size(); ++i) {
        const std::string name = lower(model.textures()[i].name);
        if (name.compare(0, name.find('.'), stem) == 0)
            return i;
    }
    return SIZE_MAX;
}

} // namespace

// Windows finds a font by its name, from those installed: the game
// installs its own, the files of fonts.wad.
ttf::Font *SignPainter::font(const SearchSet &files, const std::string &face, const std::string &file) {
    if (!_fontsRead) {
        _fontsRead = true;
        for (const std::string &name : files.list()) {
            const std::string l = lower(name);
            if (l.compare(0, std::string(kFontFolder).size(), kFontFolder) != 0 || !endsWith(l, ".ttf"))
                continue;
            std::vector<uint8_t> data;
            auto f = std::make_unique<ttf::Font>();
            if (files.read(name, data) || f->load(std::move(data)))
                continue;
            const std::string family = lower(f->familyName());
            if (!family.empty())
                _fonts.emplace(family, std::move(f));
        }
    }
    auto found = _fonts.find(lower(face));
    if (found != _fonts.end())
        return found->second.get();
    // A name none has: Windows would choose the nearest; the sign's own file is nearer.
    std::vector<uint8_t> data;
    auto f = std::make_unique<ttf::Font>();
    if (file.empty() || files.read(kFontFolder + file, data) || f->load(std::move(data)))
        return nullptr;
    return _fonts.emplace(lower(face), std::move(f)).first->second.get();
}

image::Image SignPainter::paint(const SearchSet &files, const SignStyle &style,
                                const std::array<std::string, 2> &lines) {
    image::Image canvas;
    canvas.w = kCanvasWidth;
    canvas.h = SignStyle::kCanvasHeight;
    canvas.rgba.assign(size_t(canvas.w) * size_t(canvas.h) * 4, 0);
    for (size_t k = 0; k < lines.size(); ++k) {
        const SignStyle::Line &line = style.lines()[k];
        if (lines[k].empty() || line.height <= 0 || line.fill.w <= 0)
            continue;
        ttf::Font *f = font(files, line.face, style.fontFile());
        if (!f)
            continue;
        // The height is the em's, in pixels, as a Windows font's negative height.
        const float pixels = f->sizeForPoints(float(line.height) * 72.0f / 96.0f);
        const int natural = f->width(lines[k], pixels);
        const int high = f->lineHeight(pixels);
        if (natural <= 0 || high <= 0)
            continue;
        Surface letters;
        letters.create(natural, high, PixelFormat::kRGBA32);
        f->draw(letters, 0, 0, lines[k], pixels, Color{255, 255, 255});

        // Widened to the font description's width, as Windows makes its
        // letters' average that, then stretched as the style has it, and
        // narrowed to fit, centred.
        float wide = float(natural) * float(std::max(1, line.stretch)) / 100.0f;
        const float average = f->averageWidth(pixels);
        if (line.width > 0 && average > 0)
            wide *= float(line.width) / average;
        wide = std::min(wide, float(canvas.w - 2 * kMargin));
        const int left = int(std::lround((float(canvas.w) - wide) / 2));
        const int width = std::max(1, int(std::lround(wide)));
        for (int y = 0; y < high; ++y) {
            const int cy = line.top + y;
            if (cy < 0 || cy >= canvas.h)
                continue;
            const int fy = std::min(line.fill.h - 1, y * line.fill.h / high);
            for (int x = 0; x < width; ++x) {
                const int cx = left + x;
                if (cx < 0 || cx >= canvas.w)
                    continue;
                const int sx = std::min(natural - 1, x * natural / width);
                const uint8_t cover = letters.row(y)[sx * 4 + 3];
                if (cover == 0)
                    continue;
                const uint8_t *fill = line.fill.pixel(std::min(line.fill.w - 1, x * line.fill.w / width), fy);
                uint8_t *to = canvas.pixel(cx, cy);
                // Over the other line where they cross.
                const int keep = 255 - cover;
                for (int c = 0; c < 3; ++c)
                    to[c] = uint8_t((fill[c] * cover + to[c] * keep) / 255);
                to[3] = uint8_t(cover + to[3] * keep / 255);
            }
        }
    }
    return canvas;
}

bool SignPainter::letter(const SearchSet &files, const std::string &modelPath, const std::array<std::string, 2> &lines,
                         LoadedModel &model) {
    const size_t dot = modelPath.rfind('.');
    std::vector<uint8_t> data;
    SignStyle style;
    if (dot == std::string::npos || files.read(modelPath.substr(0, dot) + ".sgn", data) || style.parse(data))
        return false;
    const std::array<size_t, 2> textures{textureNamed(model.model, "sign1"), textureNamed(model.model, "sign2")};
    if (textures[0] >= model.textures.size() && textures[1] >= model.textures.size())
        return false;
    const image::Image canvas = paint(files, style, lines);
    const int half = canvas.w / 2;
    for (size_t side = 0; side < textures.size(); ++side) {
        if (textures[side] >= model.textures.size())
            continue;
        image::Image &out = model.textures[textures[side]];
        out.w = half;
        out.h = canvas.h;
        out.rgba.resize(size_t(half) * size_t(canvas.h) * 4);
        for (int y = 0; y < canvas.h; ++y)
            std::copy_n(canvas.pixel(int(side) * half, y), size_t(half) * 4, out.pixel(0, y));
    }
    return true;
}

} // namespace reluva::tpw
