// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/archive.h"
#include "engines/legoloco/pages/viewers.h"

#include <algorithm>
#include <string>

namespace reluva::legoloco {

namespace {

// How long a frame-set delay of 1 lasts: one tick of the game's timer,
// which it sets to 28 ms (0x406e54). That a delay counts these ticks is a
// guess.
constexpr uint64_t kTickMs = 28;

void drawBackground(Surface &s, int bottom) {
    for (int y = 0; y < bottom; y += 16)
        for (int x = 0; x < s.w; x += 16) {
            const uint8_t v = ((x + y) / 16) % 2 ? 40 : 52;
            fillRect(s, x, y, 16, 16, {v, v, uint8_t(v + 6)});
        }
}

void outline(Surface &s, int x, int y, int w, int h, Color c) {
    fillRect(s, x, y, w, 1, c);
    fillRect(s, x, y + h - 1, w, 1, c);
    fillRect(s, x, y, 1, h, c);
    fillRect(s, x + w - 1, y, 1, h, c);
}

} // namespace

PicturePage::PicturePage(System &sys, Surface &screen, const GameFiles &files)
    : _sys(sys), _screen(screen), _files(files) {
    const auto &entries = files.resources().entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string name = normalizeArchiveName(entries[i].name);
        if (name.ends_with(".bmp") || name.ends_with(".but"))
            _entries.push_back(i);
    }
}

void PicturePage::load() {
    _error.clear();
    _playing = false;
    if (_entries.empty())
        return;
    if (Error err = loadPicture(_files, _entries[_current], _picture))
        _error = err.message;
    startSet(0);
    _playing = false;
    draw();
}

void PicturePage::startSet(size_t set) {
    _set = set;
    const auto &sets = _picture.definition.frameSets;
    _frame = set < sets.size() ? sets[set].start : 0;
    _nextStepMs = _sys.getMillis() + (set < sets.size() ? sets[set].delay : 1) * kTickMs;
}

// The next frame of the set; at its end, the set it names next, or a stop.
void PicturePage::step() {
    const auto &sets = _picture.definition.frameSets;
    if (_set >= sets.size()) {
        _playing = false;
        return;
    }
    const FrameSet &s = sets[_set];
    if (_frame != s.end) {
        _frame += s.end > s.start ? 1 : -1;
        _nextStepMs += s.delay * kTickMs;
        return;
    }
    if (s.next >= 0 && size_t(s.next) < sets.size())
        startSet(size_t(s.next));
    else
        _playing = false;
}

void PicturePage::update(uint64_t nowMs) {
    if (!_playing || nowMs < _nextStepMs)
        return;
    while (_playing && nowMs >= _nextStepMs)
        step();
    draw();
}

void PicturePage::handleKey(uint32_t key) {
    const size_t n = _entries.size();
    const size_t old = _current;
    const auto &sets = _picture.definition.frameSets;
    switch (key) {
    case key::kRight:
        _current = (_current + 1) % n;
        break;
    case key::kLeft:
        _current = (_current + n - 1) % n;
        break;
    case key::kPageDown:
        _current = std::min(n - 1, _current + 20);
        break;
    case key::kPageUp:
        _current = _current >= 20 ? _current - 20 : 0;
        break;
    case key::kHome:
        _current = 0;
        break;
    case key::kEnd:
        _current = n - 1;
        break;
    case key::kDown:
        if (!sets.empty())
            startSet((_set + 1) % sets.size());
        break;
    case key::kUp:
        if (!sets.empty())
            startSet((_set + sets.size() - 1) % sets.size());
        break;
    case key::kSpace:
        _playing = !_playing && !sets.empty();
        if (_playing)
            startSet(_set);
        break;
    case '.':
        _frame = (_frame + 1) % _picture.frames;
        _playing = false;
        break;
    case ',':
        _frame = (_frame + _picture.frames - 1) % _picture.frames;
        _playing = false;
        break;
    case 'w':
        _whole = !_whole;
        break;
    case 'z':
        _zoom = _zoom == 0 ? 1 : (_zoom == 8 ? 0 : _zoom * 2);
        break;
    case 'k':
        _transparent = !_transparent;
        break;
    default:
        return;
    }
    if (_current != old)
        load();
    else
        draw();
}

// The largest of 1, 2, 4 and 8 at which w x h fits in the space.
static int fittedZoom(int w, int h, int spaceW, int spaceH) {
    int zoom = 8;
    while (zoom > 1 && (w * zoom > spaceW || h * zoom > spaceH))
        zoom /= 2;
    return zoom;
}

void PicturePage::draw() {
    constexpr int kStatusH = 40;
    drawBackground(_screen, kScreenH);
    const auto &entry = _files.resources().entries()[_entries[_current]];
    const std::string position = "[" + std::to_string(_current + 1) + "/" + std::to_string(_entries.size()) + "]";
    if (!_error.empty()) {
        drawStatus(_screen, position + " " + entry.name, _error);
        return;
    }
    const Surface &bmp = _picture.bitmap.surface;
    const auto &sets = _picture.definition.frameSets;
    const FrameSet *set = _set < sets.size() ? &sets[_set] : nullptr;
    const bool flip = set && set->flipX && !_whole;
    if (_whole) {
        const int scale = _zoom ? _zoom : fittedZoom(bmp.w, bmp.h, kScreenW - 16, kScreenH - kStatusH - 16);
        Picture whole = _picture;
        whole.frames = 1;
        whole.frameW = bmp.w;
        drawFrame(_screen, whole, 0, 8, 8, scale, _transparent);
        for (int f = 1; f < _picture.frames; ++f)
            fillRect(_screen, 8 + f * _picture.frameW * scale, 8, 1, bmp.h * scale, {255, 255, 0});
    } else {
        // The frame large, and below it the picture's strip with the frame marked, if it fits.
        const int zoom = _zoom ? _zoom : fittedZoom(_picture.frameW, bmp.h, kScreenW - 16, (kScreenH - kStatusH) / 2);
        const int fw = _picture.frameW * zoom;
        const int fh = bmp.h * zoom;
        drawFrame(_screen, _picture, _frame, (kScreenW - fw) / 2, 16, zoom, _transparent, flip);
        outline(_screen, (kScreenW - fw) / 2 - 1, 15, fw + 2, fh + 2, {90, 90, 110});
        const int stripY = 16 + fh + 16;
        if (bmp.w <= kScreenW - 16 && stripY + bmp.h < kScreenH - kStatusH) {
            Picture whole = _picture;
            whole.frames = 1;
            whole.frameW = bmp.w;
            drawFrame(_screen, whole, 0, 8, stripY, 1, _transparent);
            outline(_screen, 8 + _frame * _picture.frameW, stripY, _picture.frameW, bmp.h, {255, 255, 0});
        }
    }
    std::string line1 = position + " " + entry.name + "  " + std::to_string(bmp.w) + "x" + std::to_string(bmp.h);
    if (_picture.hasDefinition)
        line1 += "  " + std::to_string(_picture.frames) + " frames of " + std::to_string(_picture.frameW) + "  frame " +
                 std::to_string(_frame);
    else
        line1 += "  no definition";
    std::string line2;
    if (set)
        line2 = "set " + std::to_string(_set + 1) + "/" + std::to_string(sets.size()) + " " + set->name + ": " +
                std::to_string(set->start) + "-" + std::to_string(set->end) + " delay " + std::to_string(set->delay) +
                " next " + std::to_string(set->next) + (set->flipX ? " flipped" : "") + (_playing ? "  playing" : "");
    drawStatus(_screen, line1, line2,
               "Left/Right picture  Up/Down set  Space play  ,/. frame  W whole  Z zoom  K transparent  Tab page");
}

} // namespace reluva::legoloco
