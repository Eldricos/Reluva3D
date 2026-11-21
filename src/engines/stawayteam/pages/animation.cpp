// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/pages/animation.h"

#include "engines/stawayteam/pages/draw.h"

#include <algorithm>

namespace reluva::stawayteam {

void Animation::set(const SequenceData &seq, size_t first, size_t count, double fps) {
    _frames.clear();
    for (size_t i = first; i < first + count && i < seq.frames.size(); ++i) {
        Shown shown;
        if (const auto &f = seq.frames[i]) {
            shown.image = frameImage(*f);
            shown.originX = f->originX;
            shown.originY = f->originY;
        }
        _frames.push_back(std::move(shown));
    }
    _current = 0;
    // A sequence of 0 or no frames a second would never move.
    _fps = fps > 0 ? fps : 10;
    _lastMs = 0;
}

void Animation::step(int by) {
    _paused = true;
    if (_frames.empty())
        return;
    const auto n = int64_t(_frames.size());
    _current = size_t(((int64_t(_current) + by) % n + n) % n);
}

bool Animation::update(uint64_t nowMs) {
    if (_frames.size() < 2 || _paused) {
        _lastMs = nowMs;
        return false;
    }
    if (_lastMs == 0)
        _lastMs = nowMs;
    const auto frameMs = uint64_t(1000.0 / _fps);
    if (nowMs - _lastMs < frameMs)
        return false;
    _lastMs += frameMs;
    _current = (_current + 1) % _frames.size();
    return true;
}

void Animation::bounds(int &left, int &top, int &right, int &bottom) const {
    left = top = right = bottom = 0;
    bool any = false;
    for (const Shown &f : _frames) {
        if (f.image.w == 0)
            continue;
        const int l = -f.originX;
        const int t = -f.originY;
        left = any ? std::min(left, l) : l;
        top = any ? std::min(top, t) : t;
        right = any ? std::max(right, l + f.image.w) : l + f.image.w;
        bottom = any ? std::max(bottom, t + f.image.h) : t + f.image.h;
        any = true;
    }
}

void Animation::draw(Surface &s, int x, int y, int zoom, int backdrop) const {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bounds(left, top, right, bottom);
    drawBackdrop(s, x + left * zoom, y + top * zoom, (right - left) * zoom, (bottom - top) * zoom, backdrop);
    if (_current < _frames.size() && _frames[_current].image.w > 0) {
        const Shown &f = _frames[_current];
        // The origin is the point in the frame that goes where it's drawn.
        image::blit(s, enlarged(f.image, zoom), x - f.originX * zoom, y - f.originY * zoom);
    }
    // A small cross where the origin is.
    fill(s, x - 3, y, 7, 1, {255, 255, 0});
    fill(s, x, y - 3, 1, 7, {255, 255, 0});
}

} // namespace reluva::stawayteam
