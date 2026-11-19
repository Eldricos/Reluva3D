// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/platform.h"

#include "graphics/palette.h"

#include <system_error>

namespace reluva::stharbinger {

Platform::Platform(System &system, audio::Mixer *mixer, std::filesystem::path saveDir)
    : _system(system), _mixer(mixer), _saveDir(std::move(saveDir)) {
}

void Platform::initGraphics(int width, int height) {
    if (!_graphics || _screen.w != width || _screen.h != height) {
        _system.initGraphics(width, height, PixelFormat::kCLUT8);
        draw::create(_screen, width, height);
        _graphics = true;
    }
}

void Platform::setPalette(const uint8_t *rgb, int start, int count) {
    const auto colors = toColors(rgb, count);
    _system.setPalette(colors.data(), start, count);
}

void Platform::copyRectToScreen(const Surface &screen) {
    if (screen.w == _screen.w && screen.h == _screen.h)
        _screen.pixels = screen.pixels;
}

void Platform::updateScreen() {
    if (_graphics)
        _system.updateScreen(_screen);
}

void Platform::setWindowTitle(const std::string &title) {
    _system.setWindowTitle(title);
}

void Platform::setMouseCursorVisible(bool visible) {
    _system.setMouseCursorVisible(visible);
}

void Platform::setTextInput(bool on) {
    _system.setTextInput(on);
}

bool Platform::pollEvent(InputEvent &out) {
    Event ev;
    while (_system.pollEvent(ev)) {
        out = InputEvent();
        out.mouse = {ev.mouseX, ev.mouseY};
        switch (ev.type) {
        case EventType::kQuit:
        case EventType::kReturnToLauncher:
            _quit = true;
            out.type = InputEvent::kQuit;
            return true;
        case EventType::kKeyDown:
            out.type = InputEvent::kKeyDown;
            out.key = ev.key;
            out.mods = ev.mods;
            return true;
        case EventType::kTextInput:
            // One printable ASCII character, as the original's names take.
            if (ev.text[0] >= 32 && ev.text[0] < 127 && ev.text[1] == 0) {
                out.type = InputEvent::kKeyDown;
                out.ascii = ev.text[0];
                return true;
            }
            break;
        case EventType::kMouseMove:
            out.type = InputEvent::kMouseMove;
            return true;
        case EventType::kMouseDown:
            if (ev.button == MouseButton::kLeft || ev.button == MouseButton::kRight) {
                out.type = ev.button == MouseButton::kLeft ? InputEvent::kLeftDown : InputEvent::kRightDown;
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

uint32_t Platform::getMillis() const {
    return uint32_t(_system.getMillis());
}

void Platform::delayMillis(uint32_t ms) {
    _system.delayMillis(ms);
}

std::string Platform::getSavePath() {
    std::error_code ec;
    std::filesystem::create_directories(_saveDir, ec);
    std::string path = _saveDir.string();
    if (!path.empty() && path.back() != '/' && path.back() != '\\')
        path += std::filesystem::path::preferred_separator;
    return path;
}

} // namespace reluva::stharbinger
