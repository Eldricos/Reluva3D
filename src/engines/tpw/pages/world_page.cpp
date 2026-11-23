// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's page for the worlds' terrain, drawn in software.

#include "common/events.h"
#include "common/fs.h"
#include "common/text.h"
#include "engines/tpw/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>

namespace reluva::tpw {

namespace {

constexpr Color kBackground = {120, 160, 210};
constexpr Color kLabel = {255, 255, 255};
constexpr Color kError = {255, 120, 110};
constexpr int kTop = 24;
constexpr float kTurn = 0.2f; // radians a key
constexpr const char *kSurroundings = "/terrain/base.md2";

} // namespace

WorldPage::WorldPage(ViewerContext &context) : Page(context) {
    for (const std::string &name : _context.files.list()) {
        const std::string lower = fs::toLower(name);
        const size_t end = lower.size() - std::string(kSurroundings).size();
        if (lower.size() > std::string(kSurroundings).size() && lower.compare(0, 7, "levels/") == 0 &&
            lower.compare(end, std::string::npos, kSurroundings) == 0 && name.find('/', 7) == end)
            _folders.push_back(name.substr(0, end));
    }
    if (!_folders.empty())
        selected();
}

std::string WorldPage::current() const {
    return _folders.empty() ? std::string() : _folders[_index];
}

void WorldPage::selected() {
    _error.clear();
    if (Error err = _world.load(_context.files, _folders[_index]))
        _error = err.message;
}

void WorldPage::handleKey(uint32_t key) {
    if (key == 'a')
        _view.yaw -= kTurn;
    else if (key == 'd')
        _view.yaw += kTurn;
    else if (key == 'w')
        _view.pitch = std::min(1.5f, _view.pitch + kTurn);
    else if (key == 's')
        _view.pitch = std::max(0.0f, _view.pitch - kTurn);
    else if (key == 'z')
        _view.distance = std::max(100.0f, _view.distance / 1.25f);
    else if (key == 'x')
        _view.distance = std::min(5000.0f, _view.distance * 1.25f);
    else if (key == 't')
        _view.textured = !_view.textured;
    else if (key == 'g')
        _view.ground = !_view.ground;
    else
        Page::handleKey(key);
}

void WorldPage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    if (_error.empty())
        drawWorld(screen, _rasterizer, _world, _view);
    std::string status = text::format("%zu/%zu  %s", _index + 1, _folders.size(), current().c_str());
    if (_world.hasLand)
        status += text::format("  ground %dx%d cells", _world.land.width(), _world.land.depth());
    if (_world.hasAttributes)
        status += text::format(", map %dx%d", _world.attributes.width(), _world.attributes.height());
    drawLabel(screen, 4, 4, status, kLabel);
    if (!_error.empty())
        drawLabel(screen, 4, kTop + 4, _error, kError);
    drawLabel(screen, 4, screen.h - font::kLineHeight - 2,
              std::string("A/D turn  W/S tilt  Z/X nearer, further  T textures ") + (_view.textured ? "off" : "on") +
                  "  G ground " + (_view.ground ? "off" : "on"),
              kLabel);
}

} // namespace reluva::tpw
