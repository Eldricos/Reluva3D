// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/pages/model_viewer.h"

#include "engines/stawayteam/pages/draw.h"

#include <algorithm>
#include <cstdio>

namespace reluva::stawayteam {

ModelViewer::ModelViewer(System &sys, Surface &screen, const Archive &data)
    : _sys(sys), _screen(screen), _data(data),
      _files(data, [](const std::string &name) { return name.ends_with(".mdl16"); }) {
}

void ModelViewer::activate() {
    if (_model.object.className.empty() && _error.empty())
        load();
    draw();
}

void ModelViewer::load() {
    _error.clear();
    _sequences.clear();
    _sequence = 0;
    _rotation = 0;
    _animation.clear();
    std::vector<uint8_t> bytes;
    if (Error err = _data.read(_files.current(), bytes)) {
        _error = err.message;
        return;
    }
    if (Error err = readBinaryObject(bytes.data(), bytes.size(), "CModel", _model)) {
        _error = err.message;
        return;
    }
    for (size_t i = 0; i < _model.sequences.size(); ++i)
        if (_model.sequences[i].sequence && _model.sequences[i].sequence->sequence)
            _sequences.push_back(i);
    // Start on the model's own default, where it has one.
    const std::string *start = _model.object.value("Default Sequence");
    for (size_t i = 0; start && i < _sequences.size(); ++i)
        if (_model.sequences[_sequences[i]].name == *start)
            _sequence = i;
    choose();
}

// The frames of the sequence and direction chosen: a rotation sequence
// keeps each direction's frames together, one direction after another.
void ModelViewer::choose() {
    _animation.clear();
    if (_sequences.empty())
        return;
    const BinaryObject &seq = *_model.sequences[_sequences[_sequence]].sequence;
    const size_t frames = seq.sequence->frames.size();
    _rotations = int(std::max(1L, seq.object.integer("Num Rotations").value_or(1)));
    if (frames % size_t(_rotations) != 0)
        _rotations = 1; // the game refuses such a sequence (string at 0x6444b4)
    _rotation %= _rotations;
    const size_t each = frames / size_t(_rotations);
    _animation.set(*seq.sequence, size_t(_rotation) * each, each, seq.object.number("Frames Per Second").value_or(10));
}

void ModelViewer::handleKey(uint32_t key) {
    const size_t n = _sequences.size();
    if (key == key::kDown && n) {
        _sequence = (_sequence + 1) % n;
        choose();
    } else if (key == key::kUp && n) {
        _sequence = (_sequence + n - 1) % n;
        choose();
    } else if (key == '.') {
        _rotation = (_rotation + 1) % _rotations;
        choose();
    } else if (key == ',') {
        _rotation = (_rotation + _rotations - 1) % _rotations;
        choose();
    } else if (key == ' ') {
        _animation.togglePause();
    } else if (key == '[') {
        _animation.step(-1);
    } else if (key == ']') {
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

void ModelViewer::update(uint64_t nowMs) {
    if (_animation.update(nowMs))
        draw();
}

void ModelViewer::draw() {
    clearPage(_screen);
    std::string status = _error;
    if (_error.empty()) {
        const std::string *name = _model.object.value("Legacy Name");
        const std::string *line = _model.object.value("Description Line");
        status = (name ? *name : std::string()) + (line && !line->empty() ? " - " + *line : "");
        if (_sequences.empty()) {
            status += ": no sequences";
        } else {
            int left = 0;
            int top = 0;
            int right = 0;
            int bottom = 0;
            _animation.bounds(left, top, right, bottom);
            const int height = _screen.h - kStatusHeight;
            int zoom = _zoom;
            if (zoom == 0) {
                zoom = 4;
                while (zoom > 1 && ((right - left) * zoom > _screen.w - 20 || (bottom - top) * zoom > height - 20))
                    --zoom;
            }
            _animation.draw(_screen, _screen.w / 2 - (left + right) * zoom / 2, height / 2 - (top + bottom) * zoom / 2,
                            zoom, _backdrop);
            const BinaryObject &seq = *_model.sequences[_sequences[_sequence]].sequence;
            char buf[200];
            std::snprintf(
                buf, sizeof buf, ": \"%s\" (%zu/%zu), facing %d/%d, frame %zu/%zu%s, %s a second, x%d",
                _model.sequences[_sequences[_sequence]].name.c_str(), _sequence + 1, _sequences.size(), _rotation + 1,
                _rotations, _animation.current() + 1, _animation.size(), _animation.paused() ? " (paused)" : "",
                seq.object.value("Frames Per Second") ? seq.object.value("Frames Per Second")->c_str() : "?", zoom);
            status += buf;
        }
    }
    drawStatus(_screen, _files.position(), status);
    _sys.setWindowTitle("Reluva - Star Trek: Away Team - models (Left/Right, F: next folder, Up/Down: sequence, ,/.: "
                        "turn, Space: pause, [/]: frame, B: background, Z: zoom, Tab: next page)");
}

} // namespace reluva::stawayteam
