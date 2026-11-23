// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's page for the game's models, drawn in software.

#include "common/events.h"
#include "common/text.h"
#include "engines/tpw/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>

namespace reluva::tpw {

namespace {

constexpr Color kBackground = {44, 52, 66};
constexpr Color kLabel = {230, 230, 230};
constexpr Color kDim = {150, 160, 175};
constexpr Color kError = {255, 120, 110};
constexpr int kTop = 24;
constexpr float kTurn = 0.2f; // radians a key

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
    else
        Page::handleKey(key);
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
    drawLabel(screen, 4, screen.h - font::kLineHeight - 2,
              std::string("A/D turn  W/S tilt  Z/X nearer, further  T textures ") + (_view.textured ? "off" : "on"),
              kDim);
}

} // namespace reluva::tpw
