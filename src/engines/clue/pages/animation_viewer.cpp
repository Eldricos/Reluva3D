// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/pages/viewers.h"

#include <algorithm>

namespace reluva::clue {

namespace {

constexpr int kInfoH = 26;

} // namespace

AnimationViewer::AnimationViewer(System &sys, Surface &screen, const GameData &data)
    : _sys(sys), _screen(screen), _data(data) {
    for (const auto &path : _data.withExtension(".a")) {
        const DataDrive *d = _data.drive(path.stem().string(), 'A');
        if (d && !d->entries().empty()) {
            _files.push_back(path);
            _total += d->entries().size();
        }
    }
}

void AnimationViewer::activate() {
    select();
    _frameStart = _sys.getMillis();
}

void AnimationViewer::select() {
    _current = nullptr;
    _frame = 0;
    _layer = -1;
    _dirty = true;
    if (_files.empty())
        return;
    const std::string drive = _files[size_t(_file)].stem().string();
    const DataDrive *d = _data.drive(drive, 'A');
    _name = drive + ":" + d->entries()[size_t(_anim)].name;
    _current = _data.animation(_name);
    _error = _current ? std::string() : _data.lastError();
    _sys.setWindowTitle(std::string(gameName(_data.language())) + " animations: " + _name);
}

void AnimationViewer::handleKey(uint32_t key) {
    if (_files.empty())
        return;
    const int n = int(_data.drive(_files[size_t(_file)].stem().string(), 'A')->entries().size());
    const size_t frames = _current ? _current->frameCount() : 0;
    switch (key) {
    case key::kRight:
        _anim = (_anim + 1) % n;
        select();
        break;
    case key::kLeft:
        _anim = (_anim + n - 1) % n;
        select();
        break;
    case key::kDown:
        _file = (_file + 1) % int(_files.size());
        _anim = 0;
        select();
        break;
    case key::kUp:
        _file = (_file + int(_files.size()) - 1) % int(_files.size());
        _anim = 0;
        select();
        break;
    case key::kSpace:
        _paused = !_paused;
        break;
    case '.':
        _paused = true;
        if (frames)
            _frame = (_frame + 1) % frames;
        break;
    case ',':
        _paused = true;
        if (frames)
            _frame = (_frame + frames - 1) % frames;
        break;
    case 'z':
        _zoom = _zoom == 1 ? 2 : 1;
        break;
    case 'a':
        _showAnchor = !_showAnchor;
        break;
    case 'l':
        if (_current && !_current->layers.empty())
            _layer = _layer + 1 >= int(_current->layers.size()) ? -1 : _layer + 1;
        break;
    default:
        return;
    }
    _frameStart = _sys.getMillis();
    _dirty = true;
}

void AnimationViewer::update(uint64_t nowMs) {
    if (_current && !_paused && _current->frameCount() > 1) {
        const int fps = _current->framesPerSecond ? _current->framesPerSecond : 10;
        if (nowMs - _frameStart >= uint64_t(1000 / fps)) {
            _frame = (_frame + 1) % _current->frameCount();
            _frameStart = nowMs;
            _dirty = true;
        }
    }
    if (_dirty)
        draw();
}

void AnimationViewer::draw() {
    _dirty = false;
    const int areaH = kScreenH - kInfoH;
    drawCheckerboard(_screen, 0, areaH);
    if (_files.empty()) {
        drawInfo(_screen, "No animations found");
        return;
    }
    if (!_current) {
        drawInfo(_screen, _name, _error);
        return;
    }
    const Animation &a = *_current;
    const int z = (a.width * 2 <= kScreenW && a.height * 2 <= areaH) ? _zoom : 1;
    // Isometric animations hang from their anchor point (a character's
    // feet), put low in the middle; the others are centred, among them the
    // short ISOA form's (the interface's, fonts), whose anchors are mostly 0.
    const bool hangs = a.isometric && !a.shortForm && !a.frames.empty();
    int ax = kScreenW / 2;
    int ay = areaH / 2;
    int left = ax - a.width * z / 2;
    int top = ay - a.height * z / 2;
    if (hangs) {
        ay = std::min(areaH - 8, areaH / 2 + a.height * z / 2);
        left = ax + a.frames[_frame].anchorX * z;
        top = ay + a.frames[_frame].anchorY * z;
    }
    std::string missing;
    for (size_t l = 0; l < a.layers.size(); ++l) {
        if (_layer >= 0 && int(l) != _layer)
            continue;
        if (_frame >= a.layers[l].size())
            continue;
        const Animation::Part &part = a.layers[l][_frame];
        if (part.image >= a.images.size())
            continue;
        const Image *img = _data.image(a.images[part.image]);
        if (!img) {
            missing = _data.lastError();
            continue;
        }
        drawImage(_screen, img->pixels, left + (part.x + img->offsetX) * z, top + (part.y + img->offsetY) * z, z);
    }
    if (_showAnchor && hangs) {
        fillRect(_screen, ax - 6, ay, 13, 1, {255, 64, 64});
        fillRect(_screen, ax, ay - 6, 1, 13, {255, 64, 64});
    }
    std::string line2 = "frame " + std::to_string(_frame + 1) + "/" + std::to_string(a.frameCount());
    if (!a.frames.empty() && a.frames[_frame].solid)
        line2 += " box " + std::to_string(a.frames[_frame].sizeX) + "x" + std::to_string(a.frames[_frame].sizeY) + "x" +
                 std::to_string(a.frames[_frame].height);
    line2 += "  " + std::to_string(a.width) + "x" + std::to_string(a.height) + "  " + std::to_string(a.images.size()) +
             " images, " + std::to_string(a.layers.size()) + " layer(s)";
    if (_layer >= 0)
        line2 += " showing " + std::to_string(_layer + 1);
    if (a.isometric)
        line2 += a.shortForm ? "  ISOA (short)" : "  ISOA";
    if (_paused)
        line2 += "  paused";
    if (!missing.empty())
        line2 += "  " + missing;
    const DataDrive *d = _data.drive(_files[size_t(_file)].stem().string(), 'A');
    drawInfo(_screen,
             _name + "   " + std::to_string(_anim + 1) + "/" + std::to_string(d->entries().size()) + " (file " +
                 std::to_string(_file + 1) + "/" + std::to_string(_files.size()) + ")",
             line2);
}

} // namespace reluva::clue
