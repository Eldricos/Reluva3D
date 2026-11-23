// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/sprite_page.h"

#include "common/events.h"
#include "common/text.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

namespace reluva::storm3d {

namespace {

constexpr Color kBackground = {24, 24, 32};
constexpr Color kOutline = {255, 220, 80};
constexpr int kBox = 384; // the texture's and the enlarged frame's boxes, in pixels
constexpr const char *kSample = "Star Trek: Armada 0123456789 The quick brown fox jumps over the lazy dog";

// The colour a table's @skip=(r,g,b) makes see-through; false for "off" or none.
bool colourKey(const std::string &skip, Color &key) {
    int r = 0;
    int g = 0;
    int b = 0;
    if (std::sscanf(skip.c_str(), "(%d,%d,%d)", &r, &g, &b) != 3)
        return false;
    key = {uint8_t(r), uint8_t(g), uint8_t(b)};
    return true;
}

// Part of an image, `scale` screen pixels to a texel, its alpha over a
// checkerboard, clipped to the screen; pixels of the colour key, if
// given, see-through too.
void drawPart(Surface &s, const image::Image &img, int sx, int sy, int sw, int sh, int x0, int y0, double scale,
              const Color *key = nullptr) {
    const int w = int(sw * scale);
    const int h = int(sh * scale);
    for (int y = std::max(0, -y0); y < h && y0 + y < s.h; ++y) {
        const int ty = sy + int(y / scale);
        if (ty < 0 || ty >= img.h)
            continue;
        uint8_t *out = s.row(y0 + y);
        for (int x = std::max(0, -x0); x < w && x0 + x < s.w; ++x) {
            const int tx = sx + int(x / scale);
            if (tx < 0 || tx >= img.w)
                continue;
            const uint8_t *in = img.pixel(tx, ty);
            const uint8_t check = ((x / 8 + y / 8) % 2) ? 64 : 96;
            const bool keyed = key && in[0] == key->r && in[1] == key->g && in[2] == key->b;
            const int alpha = keyed ? 0 : in[3];
            uint8_t *p = out + size_t(x0 + x) * 4;
            for (int c = 0; c < 3; ++c)
                p[c] = uint8_t((in[c] * alpha + check * (255 - alpha)) / 255);
            p[3] = 255;
        }
    }
}

void outline(Surface &s, int x, int y, int w, int h) {
    fillRect(s, x, y, w, 1, kOutline);
    fillRect(s, x, y + h - 1, w, 1, kOutline);
    fillRect(s, x, y, 1, h, kOutline);
    fillRect(s, x + w - 1, y, 1, h, kOutline);
}

// A texture's scale to fit the box: whole numbers up to 4 when it's
// smaller, else shrunk.
double fitScale(int w, int h, int box) {
    if (w <= 0 || h <= 0)
        return 1;
    const double fit = std::min(double(box) / w, double(box) / h);
    return fit >= 1 ? std::min(4.0, std::floor(fit)) : fit;
}

} // namespace

void SpritePage::frameRect(const SpriteTable::Sprite &sprite, const SpriteTable::Animation *offset,
                           const SpriteTable::Animation *width, size_t frame, int texW, int texH, int &x, int &y,
                           int &w, int &h) {
    double u = sprite.u;
    double v = sprite.v;
    double sw = sprite.w;
    if (offset && frame < offset->keyframes.size() && offset->keyframes[frame].size() >= 2) {
        u += offset->keyframes[frame][0];
        v += offset->keyframes[frame][1];
    } else if (offset && offset->keyframes.empty() && int(frame) < offset->count) {
        // Keyframes the game lays out itself: the frames are cells the
        // sprite's size, along a row, down a column, or in a square
        // ("16 frame texture 'flipbook' animation", tex_anim.spr).
        if (offset->autoLayout == "row") {
            u += double(frame) * sprite.w;
        } else if (offset->autoLayout == "column") {
            v += double(frame) * sprite.h;
        } else if (offset->autoLayout == "square") {
            const int side = int(std::ceil(std::sqrt(double(offset->count))));
            u += double(int(frame) % side) * sprite.w;
            v += double(int(frame) / side) * sprite.h;
        }
    }
    if (width && frame < width->keyframes.size() && !width->keyframes[frame].empty())
        sw = width->keyframes[frame][0];
    // Measured against the reference size; a table that gives none is in
    // the texture's own pixels.
    const double kx = sprite.referenceWidth > 0 ? double(texW) / sprite.referenceWidth : 1;
    const double ky = sprite.referenceHeight > 0 ? double(texH) / sprite.referenceHeight : 1;
    x = int(u * kx);
    y = int(v * ky);
    w = int(sw * kx);
    h = int(sprite.h * ky);
}

SpritePage::SpritePage(System &sys, Surface &screen, const GameFiles &files)
    : _sys(sys), _screen(screen), _files(files) {
    const SpriteTable::Loader loader = [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> data;
        if (Error err = files.read("Sprites/" + std::string(name), data))
            return err;
        text.assign(data.begin(), data.end());
        return Error::success();
    };
    // sprites.spr with what it includes, then each table it doesn't.
    std::vector<std::string> names = {"sprites.spr"};
    for (const std::string &path : files.list())
        if (text::hasSuffixIgnoreCase(path, ".spr"))
            names.push_back(std::filesystem::path(path).filename().string());
    std::set<std::string> read;
    for (const std::string &name : names) {
        if (read.count(text::toLower(name)))
            continue;
        SpriteTable table;
        if (table.load(name, loader))
            continue;
        for (const std::string &t : table.tables())
            read.insert(text::toLower(t));
        for (size_t i = 0; i < table.sprites().size(); ++i)
            _list.push_back({_tables.size(), i});
        _tables.push_back(std::move(table));
        _tableNames.push_back(name);
    }
}

const SpriteTable::Animation *SpritePage::findAnimation(const std::string &name) const {
    for (const SpriteTable &t : _tables)
        if (const SpriteTable::Animation *a = t.animation(name))
            return a;
    return nullptr;
}

const image::Image *SpritePage::texture(const std::string &name) {
    auto [it, added] = _textures.try_emplace(text::toLower(name));
    if (added) {
        // The full-colour one first: the viewer shows the best there is.
        for (const char *folder : {"Textures/RGB/", "Textures/Index8/"}) {
            std::vector<uint8_t> data;
            if (!_files.read(folder + name + ".tga", data) && !image::decode(data, it->second))
                break;
            it->second = {};
        }
    }
    return it->second.w > 0 ? &it->second : nullptr;
}

void SpritePage::handleKey(uint32_t key) {
    const size_t before = _current;
    switch (key) {
    case key::kLeft:
        _current = stepIndex(_current, -1, _list.size());
        break;
    case key::kRight:
        _current = stepIndex(_current, 1, _list.size());
        break;
    case key::kPageUp:
        _current = stepIndex(_current, -10, _list.size());
        break;
    case key::kPageDown:
        _current = stepIndex(_current, 10, _list.size());
        break;
    case key::kHome:
        _current = 0;
        break;
    case key::kEnd:
        _current = _list.empty() ? 0 : _list.size() - 1;
        break;
    case key::kUp:
        _frame = _frame > 0 ? _frame - 1 : 0;
        break;
    case key::kDown:
        ++_frame; // held to the last keyframe in show()
        break;
    case 't': {
        const size_t table = _list[_current].table;
        size_t i = _current;
        while (i < _list.size() && _list[i].table == table)
            ++i;
        _current = i < _list.size() ? i : 0;
        break;
    }
    default:
        return;
    }
    if (_current != before)
        _frame = 0;
    show();
}

void SpritePage::show() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
    if (_list.empty())
        return;
    const Entry &e = _list[_current];
    const SpriteTable::Sprite &sprite = _tables[e.table].sprites()[e.sprite];
    // Its offset and width animations, the ones with keyframes to step.
    const SpriteTable::Animation *offset = nullptr;
    const SpriteTable::Animation *width = nullptr;
    for (const std::string &name : sprite.animations)
        if (const SpriteTable::Animation *a = findAnimation(name)) {
            if (a->kind == "offset")
                offset = a;
            else if (a->kind == "width")
                width = a;
        }
    const size_t frames = !offset ? 1
                                  : std::max<size_t>(1, offset->keyframes.empty() ? size_t(std::max(0, offset->count))
                                                                                  : offset->keyframes.size());
    _frame = std::min(_frame, frames - 1);

    std::string about = sprite.texture + " not found";
    if (const image::Image *tex = texture(sprite.texture)) {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        frameRect(sprite, offset, width, _frame, tex->w, tex->h, x, y, w, h);
        Color keyColour;
        const Color *key = colourKey(sprite.skip, keyColour) ? &keyColour : nullptr;
        const double scale = fitScale(tex->w, tex->h, kBox);
        drawPart(_screen, *tex, 0, 0, tex->w, tex->h, 8, 8, scale, key);
        outline(_screen, 8 + int(x * scale) - 1, 8 + int(y * scale) - 1, int(w * scale) + 2, int(h * scale) + 2);
        if (w > 0 && h > 0) {
            const double big = std::max(1.0, std::floor(std::min(double(kBox) / w, double(kBox) / h)));
            drawPart(_screen, *tex, x, y, w, h, 16 + kBox, 8, std::min(big, 16.0), key);
        }
        about = text::format("%s %dx%d, at %d,%d %dx%d", sprite.texture.c_str(), tex->w, tex->h, x, y, w, h);

        // A font: a character a keyframe, from the space up, written out.
        if (offset && width && offset->duration == 0 && offset->keyframes.size() > 95) {
            int pen = 8;
            for (const char *c = kSample; *c; ++c) {
                const size_t glyph = size_t(uint8_t(*c)) - 32;
                frameRect(sprite, offset, width, glyph, tex->w, tex->h, x, y, w, h);
                drawPart(_screen, *tex, x, y, w, h, pen, 8 + kBox + 24, 2, key);
                pen += 2 * w;
            }
        }
    }
    const std::string line =
        text::format("%zu/%zu  %s: %s  %s  frame %zu/%zu%s%s", _current + 1, _list.size(), _tableNames[e.table].c_str(),
                     sprite.name.c_str(), about.c_str(), _frame + 1, frames,
                     sprite.material.empty() ? "" : ("  material " + sprite.material).c_str(),
                     sprite.animations.empty() ? "" : ("  anim " + sprite.animations.front()).c_str());
    drawStatus(_screen, line);
    _sys.setWindowTitle("Reluva viewer - Sprites - " + line);
}

} // namespace reluva::storm3d
