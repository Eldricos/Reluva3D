// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Images page: sprites.q3's images, then the .bmp files.

#include "common/fs.h"
#include "common/stream.h"
#include "engines/sfc/pages/viewers.h"
#include "graphics/font.h"
#include "image/image.h"

#include <algorithm>
#include <cstdio>

namespace reluva::sfc {

namespace {

constexpr int kTop = 2 * font::kLineHeight + 6; // below the two lines of details

// The pixel's colour over the chosen background, by its alpha.
void blend(uint8_t *dst, const uint8_t *src, Color back) {
    const unsigned a = src[3];
    dst[0] = static_cast<uint8_t>((src[0] * a + back.r * (255 - a)) / 255);
    dst[1] = static_cast<uint8_t>((src[1] * a + back.g * (255 - a)) / 255);
    dst[2] = static_cast<uint8_t>((src[2] * a + back.b * (255 - a)) / 255);
    dst[3] = 255;
}

} // namespace

ImagePage::ImagePage(ViewerContext &ctx) : Page(ctx) {
    if (const auto path = fs::findFile(ctx.root, "Assets/Sprites/sprites.q3"))
        if (Error err = _sprites.open(*path))
            _spriteError = err.message;
    if (const auto assets = fs::findDirectory(ctx.root, "Assets"))
        for (const auto &p : filesWithExtension(*assets, ".bmp"))
            _bitmaps.push_back(assets->lexically_relative(ctx.root) / p);
}

void ImagePage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || count() == 0)
        return;
    const size_t n = count();
    const size_t sprites = _sprites.images().size();
    switch (ev.key) {
    case key::kLeft:
        _current = (_current + n - 1) % n;
        break;
    case key::kRight:
        _current = (_current + 1) % n;
        break;
    case key::kPageUp:
        _current = _current >= 10 ? _current - 10 : 0;
        break;
    case key::kPageDown:
        _current = std::min(n - 1, _current + 10);
        break;
    case key::kHome:
        _current = 0;
        break;
    case key::kEnd:
        _current = n - 1;
        break;
    case 's':
        _current = _current < sprites ? (sprites < n ? sprites : 0) : 0;
        break;
    case 'n':
        for (size_t i = _current + 1; i < sprites; ++i)
            if (!_sprites.images()[i].name.empty()) {
                _current = i;
                break;
            }
        break;
    case 'b':
        _background = (_background + 1) % 3;
        break;
    case 'z':
        _fit = !_fit;
        break;
    default:
        return;
    }
    show();
}

void ImagePage::show() {
    clear({20, 22, 28});
    if (count() == 0)
        return;
    const size_t sprites = _sprites.images().size();
    Surface image;
    std::string what;
    std::string error;
    if (_current < sprites) {
        const auto &img = _sprites.images()[_current];
        char buf[160];
        std::snprintf(buf, sizeof buf, "sprites.q3 image %zu of %zu: %ux%u, %u bits, hot spot %d,%d, at %u", _current,
                      sprites, img.width, img.height, img.bits, img.hotX, img.hotY, img.offset);
        what = std::string(buf) + (img.name.empty() ? "" : "  " + img.name);
        if (Error err = _sprites.decode(img, image))
            error = err.message;
    } else {
        const auto &rel = _bitmaps[_current - sprites];
        what = rel.generic_string() + "  (" + std::to_string(_current - sprites + 1) + " of " +
               std::to_string(_bitmaps.size()) + ")";
        std::vector<uint8_t> data;
        image::Image rgba;
        if (!readFile(_ctx.root / rel, data))
            error = "Can't read it";
        else if (Error err = image::decode(data, rgba))
            error = err.message;
        else {
            image.create(rgba.w, rgba.h, PixelFormat::kRGBA32);
            image.pixels = rgba.rgba;
        }
    }
    text(4, 2, what, {150, 200, 255});
    text(4, 2 + font::kLineHeight,
         "Left/Right, PgUp/PgDn, Home/End: images  S: sprites.q3 or the BMPs  N: next named  B: background  Z: fit",
         {170, 170, 170});
    if (!_spriteError.empty())
        text(4, kScreenH - font::kLineHeight - 2, _spriteError, {255, 120, 120});
    setTitle(what);
    if (!error.empty()) {
        text(4, kTop + 4, error, {255, 120, 120});
        return;
    }
    // Fitted, a big picture is shrunk by a whole factor to fit below the details.
    const int areaW = kScreenW;
    const int areaH = kScreenH - kTop;
    int step = 1;
    if (_fit)
        while (image.w / step > areaW || image.h / step > areaH)
            ++step;
    const int w = std::min(areaW, image.w / step);
    const int h = std::min(areaH, image.h / step);
    const int x0 = (areaW - w) / 2;
    const int y0 = kTop + (areaH - h) / 2;
    for (int y = 0; y < h; ++y) {
        uint8_t *dst = _ctx.screen.row(y0 + y) + 4 * x0;
        const uint8_t *src = image.row(y * step);
        for (int x = 0; x < w; ++x, dst += 4) {
            Color back{40, 42, 50};
            if (_background == 1)
                back = {200, 200, 200};
            else if (_background == 2)
                back = ((x / 8 + y / 8) & 1) ? Color{90, 90, 90} : Color{160, 160, 160};
            blend(dst, src + 4 * (x * step), back);
        }
    }
}

} // namespace reluva::sfc
