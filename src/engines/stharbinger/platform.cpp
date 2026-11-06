// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/platform.h"

#include "common/palette.h"

#include <system_error>

namespace reluva::stharbinger {

Platform::Platform(System& system, audio::Mixer* mixer, std::filesystem::path saveDir)
    : system_(system), mixer_(mixer), saveDir_(std::move(saveDir)) {}

void Platform::initGraphics(int width, int height) {
    if (!graphics_ || screen_.w != width || screen_.h != height) {
        system_.initGraphics(width, height, PixelFormat::CLUT8);
        draw::create(screen_, width, height);
        graphics_ = true;
    }
}

void Platform::setPalette(const uint8_t* rgb, int start, int count) {
    const auto colors = toColors(rgb, count);
    system_.setPalette(colors.data(), start, count);
}

void Platform::copyRectToScreen(const Surface& screen) {
    if (screen.w == screen_.w && screen.h == screen_.h)
        screen_.pixels = screen.pixels;
}

void Platform::updateScreen() {
    if (graphics_)
        system_.updateScreen(screen_);
}

void Platform::setWindowTitle(const std::string& title) { system_.setWindowTitle(title); }

void Platform::setMouseCursorVisible(bool visible) { system_.setMouseCursorVisible(visible); }

void Platform::setTextInput(bool on) { system_.setTextInput(on); }

bool Platform::pollEvent(InputEvent& out) {
    Event ev;
    while (system_.pollEvent(ev)) {
        out = InputEvent();
        out.mouse = {ev.mouseX, ev.mouseY};
        switch (ev.type) {
        case EventType::Quit:
        case EventType::ReturnToLauncher:
            quit_ = true;
            out.type = InputEvent::kQuit;
            return true;
        case EventType::KeyDown:
            out.type = InputEvent::kKeyDown;
            out.key = ev.key;
            out.mods = ev.mods;
            return true;
        case EventType::TextInput:
            // One printable ASCII character, as the original's names take.
            if (ev.text[0] >= 32 && ev.text[0] < 127 && ev.text[1] == 0) {
                out.type = InputEvent::kKeyDown;
                out.ascii = ev.text[0];
                return true;
            }
            break;
        case EventType::MouseMove: out.type = InputEvent::kMouseMove; return true;
        case EventType::MouseDown:
            if (ev.button == MouseButton::Left || ev.button == MouseButton::Right) {
                out.type = ev.button == MouseButton::Left ? InputEvent::kLeftDown : InputEvent::kRightDown;
                return true;
            }
            break;
        default: break;
        }
    }
    return false;
}

uint32_t Platform::getMillis() const { return uint32_t(system_.getMillis()); }

void Platform::delayMillis(uint32_t ms) { system_.delayMillis(ms); }

std::string Platform::getSavePath() {
    std::error_code ec;
    std::filesystem::create_directories(saveDir_, ec);
    std::string path = saveDir_.string();
    if (!path.empty() && path.back() != '/' && path.back() != '\\')
        path += std::filesystem::path::preferred_separator;
    return path;
}

} // namespace reluva::stharbinger
