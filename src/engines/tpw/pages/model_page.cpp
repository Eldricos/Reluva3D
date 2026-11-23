// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's page for the game's models, drawn in software.

#include "common/events.h"
#include "common/text.h"
#include "engines/tpw/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr Color kBackground = {44, 52, 66};
constexpr Color kLabel = {230, 230, 230};
constexpr Color kDim = {150, 160, 175};
constexpr Color kError = {255, 120, 110};
constexpr int kTop = 24;
constexpr float kTurn = 0.2f;     // radians a key
constexpr uint64_t kFrameMs = 40; // how often a playing animation is drawn again

} // namespace

ModelPage::ModelPage(ViewerContext &context) : Page(context) {
    // Models and those that can't be read, to show why; not animations or headers alone.
    for (const std::string &name : _context.namesEndingIn({".md2"})) {
        std::vector<uint8_t> data;
        Model model;
        if (_context.files.read(name, data) || model.parse(data) || model.kind() == Model::Kind::kModel)
            _names.push_back(name);
    }
    if (!_names.empty())
        selected();
}

std::string ModelPage::current() const {
    return _names.empty() ? std::string() : _names[_index];
}

void ModelPage::selected() {
    _error.clear();
    _view.animation = -1;
    _view.frame = 0;
    if (Error err = _loaded.load(_context.files, _names[_index]))
        _error = err.message;
}

void ModelPage::handleKey(uint32_t key) {
    if (key == 'a')
        _view.yaw -= kTurn;
    else if (key == 'd')
        _view.yaw += kTurn;
    else if (key == 'w')
        _view.pitch = std::min(1.5f, _view.pitch + kTurn);
    else if (key == 's')
        _view.pitch = std::max(-1.5f, _view.pitch - kTurn);
    else if (key == 'z')
        _view.distance = std::max(0.6f, _view.distance / 1.25f);
    else if (key == 'x')
        _view.distance = std::min(20.0f, _view.distance * 1.25f);
    else if (key == 't')
        _view.textured = !_view.textured;
    else if (key == 'n') {
        // The next animation, then none again.
        _view.animation = _view.animation + 1 < int(_loaded.animations.size()) ? _view.animation + 1 : -1;
        _view.frame = 0;
        _lastTick = 0;
    } else if (key == 'h')
        _view.heights = !_view.heights;
    else
        Page::handleKey(key);
}

bool ModelPage::tick(uint64_t nowMs) {
    if (_view.animation < 0)
        return false;
    if (_lastTick == 0)
        _lastTick = nowMs;
    const uint64_t elapsed = nowMs - _lastTick;
    if (elapsed < kFrameMs)
        return false;
    _lastTick = nowMs;
    // Round and round, its length in frames.
    const float length = float(std::max(1u, _loaded.animations[size_t(_view.animation)].frames()));
    _view.frame = std::fmod(_view.frame + float(elapsed) * Animation::kFramesPerSecond / 1000, length);
    return true;
}

void ModelPage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    if (_error.empty())
        drawModel(screen, _rasterizer, _loaded, _view);
    size_t faces = 0;
    for (const Model::Mesh &mesh : _loaded.model.meshes())
        faces += mesh.faces.size();
    drawLabel(screen, 4, 4,
              text::format("%zu/%zu  %s  %zu meshes, %zu faces, %zu/%zu textures", _index + 1, _names.size(),
                           current().c_str(), _loaded.model.meshes().size(), faces, _loaded.texturesFound,
                           _loaded.textures.size()),
              kLabel);
    if (!_error.empty())
        drawLabel(screen, 4, kTop + 4, _error, kError);
    else if (_view.animation >= 0)
        drawLabel(screen, 4, kTop,
                  text::format("%s, frame %.0f of %u", _loaded.animationNames[size_t(_view.animation)].c_str(),
                               double(_view.frame), _loaded.animations[size_t(_view.animation)].frames()),
                  kLabel);
    else if (!_loaded.animations.empty())
        drawLabel(screen, 4, kTop, text::format("%zu animations", _loaded.animations.size()), kDim);
    drawLabel(screen, 4, screen.h - font::kLineHeight - 2,
              std::string("A/D turn  W/S tilt  Z/X nearer, further  T textures ") + (_view.textured ? "off" : "on") +
                  (_loaded.hasHeights ? std::string("  H heights ") + (_view.heights ? "off" : "on") : "") +
                  (_loaded.animations.empty() ? "" : "  N animation"),
              kDim);
}

} // namespace reluva::tpw
