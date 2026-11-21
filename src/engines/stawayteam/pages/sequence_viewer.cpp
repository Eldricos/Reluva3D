// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/pages/sequence_viewer.h"

#include "engines/stawayteam/pages/draw.h"

#include <algorithm>
#include <cstdio>

namespace reluva::stawayteam {

namespace {

// How many times larger frames of these bounds can be drawn, up to 4.
int fittingZoom(int left, int top, int right, int bottom, int w, int h) {
    int zoom = 4;
    while (zoom > 1 && ((right - left) * zoom > w - 20 || (bottom - top) * zoom > h - 20))
        --zoom;
    return zoom;
}

} // namespace

SequenceViewer::SequenceViewer(System &sys, Surface &screen, const Archive &data)
    : _sys(sys), _screen(screen), _data(data), _files(data, [](const std::string &name) {
          return name.ends_with(".seq16") && !name.starts_with("cache/fonts/");
      }) {
}

void SequenceViewer::activate() {
    if (_object.object.className.empty() && _error.empty())
        load();
    draw();
}

void SequenceViewer::load() {
    _error.clear();
    _animation.clear();
    std::vector<uint8_t> bytes;
    if (Error err = _data.read(_files.current(), bytes)) {
        _error = err.message;
        return;
    }
    if (Error err = readBinaryObject(bytes.data(), bytes.size(), "", _object)) {
        _error = err.message;
        return;
    }
    if (_object.sequence)
        _animation.set(*_object.sequence, 0, _object.sequence->frames.size(),
                       _object.object.number("Frames Per Second").value_or(10));
}

void SequenceViewer::handleKey(uint32_t key) {
    if (key == ' ') {
        _animation.togglePause();
    } else if (key == ',') {
        _animation.step(-1);
    } else if (key == '.') {
        _animation.step(1);
    } else if (key == 'b') {
        _backdrop = (_backdrop + 1) % 3;
    } else if (key == 'z') {
        _zoom = (_zoom + 1) % 5;
    } else if (_files.handleKey(key)) {
        load();
    } else {
        return;
    }
    draw();
}

void SequenceViewer::update(uint64_t nowMs) {
    if (_animation.update(nowMs))
        draw();
}

void SequenceViewer::draw() {
    clearPage(_screen);
    std::string status = _error;
    if (_error.empty() && _object.sequence) {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        _animation.bounds(left, top, right, bottom);
        const int height = _screen.h - kStatusHeight;
        const int zoom = _zoom ? _zoom : fittingZoom(left, top, right, bottom, _screen.w, height);
        const int x = _screen.w / 2 - (left + right) * zoom / 2;
        const int y = height / 2 - (top + bottom) * zoom / 2;
        _animation.draw(_screen, x, y, zoom, _backdrop);
        const std::string *blit = _object.object.value("Blit Type");
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s, frame %zu/%zu%s, %s a second, %s, bounds %d,%d to %d,%d, x%d",
                      _object.object.className.c_str(), _animation.current() + 1, _animation.size(),
                      _animation.paused() ? " (paused)" : "",
                      _object.object.value("Frames Per Second") ? _object.object.value("Frames Per Second")->c_str()
                                                                : "?",
                      blit ? blit->c_str() : "", _object.sequence->box[0], _object.sequence->box[1],
                      _object.sequence->box[2], _object.sequence->box[3], zoom);
        status = buf;
    }
    drawStatus(_screen, _files.position(), status);
    _sys.setWindowTitle("Reluva - Star Trek: Away Team - sequences (Left/Right, F: next folder, Space: pause, ,/.: "
                        "frame, B: background, Z: zoom, Tab: next page)");
}

} // namespace reluva::stawayteam
