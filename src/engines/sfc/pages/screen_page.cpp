// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Screens page: sprites.q3's groups drawn from their
// objects.

#include "common/fs.h"
#include "engines/sfc/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cstdio>

namespace reluva::sfc {

namespace {

constexpr int kTop = 2 * font::kLineHeight + 6;
constexpr int kLargest = 1600; // the widest screens are laid out for 1600x1200

} // namespace

ScreenPage::ScreenPage(ViewerContext &ctx) : Page(ctx) {
    const auto path = fs::findFile(ctx.root, "Assets/Sprites/sprites.q3");
    if (!path || _sprites.open(*path))
        return;
    for (const auto &o : _sprites.objects())
        if (!o.children.empty())
            _screens.push_back(o.id);
}

void ScreenPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _screens.empty())
        return;
    const size_t n = _screens.size();
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
    case 'n':
        for (size_t i = _current + 1; i < n; ++i)
            if (!_sprites.object(_screens[i])->name.empty()) {
                _current = i;
                break;
            }
        break;
    case 'f':
        _fit = !_fit;
        break;
    default:
        return;
    }
    show();
}

void ScreenPage::show() {
    clear({20, 22, 28});
    if (_screens.empty())
        return;
    const SpriteArchive::Object &o = *_sprites.object(_screens[_current]);
    // Drawn from its origin on a surface the largest screen fits, then cut
    // to what was drawn.
    Surface drawn;
    drawn.create(kLargest, kLargest * 3 / 4, PixelFormat::kRGBA32);
    const Error err = _sprites.compose(o.id, drawn, 0, 0);
    int w = 1;
    int h = 1;
    for (int y = 0; y < drawn.h; ++y)
        for (int x = 0; x < drawn.w; ++x)
            if (drawn.row(y)[4 * x + 3]) {
                w = std::max(w, x + 1);
                h = std::max(h, y + 1);
            }
    char buf[200];
    std::snprintf(buf, sizeof buf, "screen %zu of %zu: object %u, %s, %zu children, %dx%d", _current + 1,
                  _screens.size(), o.id, o.name.empty() ? "unnamed" : o.name.c_str(), o.children.size(), w, h);
    text(4, 2, buf, {150, 200, 255});
    text(4, 2 + font::kLineHeight,
         "Left/Right, PgUp/PgDn, Home/End: screens  N: next named  F: fit  (the program's text isn't drawn)",
         {170, 170, 170});
    setTitle(o.name);
    if (err) {
        text(4, kTop + 4, err.message, {255, 120, 120});
        return;
    }
    const int areaW = kScreenW;
    const int areaH = kScreenH - kTop;
    int step = 1;
    if (_fit)
        while (w / step > areaW || h / step > areaH)
            ++step;
    const int sw = std::min(areaW, w / step);
    const int sh = std::min(areaH, h / step);
    const int x0 = (areaW - sw) / 2;
    const int y0 = kTop + (areaH - sh) / 2;
    for (int y = 0; y < sh; ++y) {
        const uint8_t *src = drawn.row(y * step);
        uint8_t *dst = _ctx.screen.row(y0 + y) + 4 * x0;
        for (int x = 0; x < sw; ++x, dst += 4) {
            const uint8_t *p = src + 4 * (x * step);
            if (p[3])
                std::copy(p, p + 4, dst);
            else // what nothing covers, in a checkerboard
                std::fill(dst, dst + 3, ((x / 8 + y / 8) & 1) ? 40 : 56);
        }
    }
}

} // namespace reluva::sfc
