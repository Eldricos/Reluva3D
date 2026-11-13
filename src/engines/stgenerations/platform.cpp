// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/platform.h"

#include "common/text.h"

#include <algorithm>
#include <cstring>
#include <system_error>

namespace reluva::stgenerations {

Platform::Platform(System &system, audio::Mixer *mixer, std::filesystem::path saveDir)
    : _system(system), _mixer(mixer), _saveDir(std::move(saveDir)) {
}

void Platform::initSize(int width, int height, Canvas::Format format) {
    if (_graphics && _screen.w == width && _screen.h == height && _screen.format() == format)
        return;
    const PixelFormat pf = format == Canvas::kClut8 ? PixelFormat::kCLUT8 : PixelFormat::kRGBA32;
    _system.initGraphics(width, height, pf);
    _graphics = true;
    _screen.create(width, height, format);
    _frame.create(width, height, pf);
    // A new screen starts black: the palette the game set is its again.
    if (format == Canvas::kClut8) {
        const auto colors = _palette.colors();
        _system.setPalette(colors.data(), 0, 256);
    }
}

void Platform::setPalette(const uint8_t *colors, unsigned start, unsigned num) {
    for (unsigned i = 0; i < num && start + i < 256; i++)
        _palette.setColor(int(start + i), colors[i * 3], colors[i * 3 + 1], colors[i * 3 + 2]);
    const auto all = _palette.colors();
    if (start < 256)
        _system.setPalette(all.data() + start, int(start), int(std::min(num, 256 - start)));
    // The pointer is coloured from the palette, as the original's was.
    if (_cursorSet)
        refreshCursor();
}

void Platform::grabPalette(uint8_t *colors, unsigned start, unsigned num) const {
    for (unsigned i = 0; i < num && start + i < 256; i++)
        _palette.getColor(int(start + i), colors[i * 3], colors[i * 3 + 1], colors[i * 3 + 2]);
}

void Platform::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
    _screen.copyRectToSurface(buf, pitch, x, y, w, h);
}

void Platform::updateScreen() {
    if (!_graphics)
        return;
    if (_screen.isClut8()) {
        for (int y = 0; y < _screen.h; y++)
            std::copy_n(static_cast<const uint8_t *>(_screen.getBasePtr(0, y)), _screen.w, _frame.row(y));
    } else {
        // 0xAARRGGBB to R, G, B, A bytes, shown opaque.
        for (int y = 0; y < _screen.h; y++) {
            const uint8_t *src = static_cast<const uint8_t *>(_screen.getBasePtr(0, y));
            uint8_t *dst = _frame.row(y);
            for (int x = 0; x < _screen.w; x++) {
                uint32_t c;
                std::memcpy(&c, src + x * 4, 4);
                dst[x * 4] = uint8_t(c >> 16);
                dst[x * 4 + 1] = uint8_t(c >> 8);
                dst[x * 4 + 2] = uint8_t(c);
                dst[x * 4 + 3] = 255;
            }
        }
    }
    _system.updateScreen(_frame);
}

void Platform::setWindowCaption(const std::string &caption) {
    _system.setWindowTitle(caption);
}

bool Platform::showMouse(bool visible) {
    const bool was = _mouseVisible;
    _mouseVisible = visible;
    _system.setMouseCursorVisible(visible);
    return was;
}

void Platform::setMouseCursor(const void *buf, unsigned w, unsigned h, int hotspotX, int hotspotY, uint32_t keycolor) {
    _cursorImage.create(int(w), int(h), Canvas::kClut8);
    _cursorImage.copyRectToSurface(buf, int(w), 0, 0, int(w), int(h));
    _cursorHotX = hotspotX;
    _cursorHotY = hotspotY;
    _cursorKey = keycolor;
    _cursorSet = true;
    refreshCursor();
}

void Platform::refreshCursor() {
    if (_cursorImage.empty()) {
        _system.setMouseCursor(nullptr);
        return;
    }
    const Cursor c =
        cursor::fromIndexed(static_cast<const uint8_t *>(_cursorImage.getPixels()), _cursorImage.w, _cursorImage.h,
                            _cursorImage.pitch, _palette.data(), int(_cursorKey & 0xFF), _cursorHotX, _cursorHotY);
    _system.setMouseCursor(&c);
}

void Platform::setTextInput(bool on) {
    if (on == _textInput)
        return;
    _textInput = on;
    _system.setTextInput(on);
}

Point Platform::clampToScreen(int x, int y) const {
    // In the screen, as the original's window gave it (in the borders, or
    // dragged out of the window, at its edge).
    const int w = _screen.w > 0 ? _screen.w : 640;
    const int h = _screen.h > 0 ? _screen.h : 480;
    return Point(std::clamp(x, 0, w - 1), std::clamp(y, 0, h - 1));
}

bool Platform::translate(const reluva::Event &in, Event &out) {
    out = Event{};
    out.mouse = _mouse;
    auto flags = [](uint16_t mods) {
        return uint8_t(((mods & keymod::Ctrl) ? kKbdCtrl : 0) | ((mods & keymod::Alt) ? kKbdAlt : 0) |
                       ((mods & keymod::Shift) ? kKbdShift : 0));
    };
    switch (in.type) {
    case reluva::EventType::Quit:
        out.type = kEventQuit;
        _quit = true;
        return true;
    case reluva::EventType::ReturnToLauncher:
        out.type = kEventReturnToLauncher;
        _quit = true;
        return true;
    case reluva::EventType::KeyDown:
    case reluva::EventType::KeyUp:
        if (in.key == key::Unknown)
            return false;
        out.type = in.type == reluva::EventType::KeyDown ? kEventKeydown : kEventKeyup;
        out.kbdRepeat = in.repeat;
        out.kbd.keycode = in.key;
        out.kbd.flags = flags(in.mods);
        switch (in.key) {
        case key::Backspace:
            out.kbd.ascii = 8;
            break;
        case key::Tab:
            out.kbd.ascii = 9;
            break;
        case key::Return:
        case key::KpEnter:
            out.kbd.ascii = 13;
            break;
        case key::Escape:
            out.kbd.ascii = 27;
            break;
        default:
            break;
        }
        _modifiers = out.kbd.flags;
        return true;
    case reluva::EventType::TextInput:
        // A character typed, while text input is on: printable ASCII only,
        // as the original's names were.
        if (!_textInput || in.text[0] < 32 || in.text[0] > 126 || in.text[1] != 0)
            return false;
        out.type = kEventKeydown;
        out.kbd.keycode = key::Unknown;
        out.kbd.ascii = uint16_t(in.text[0]);
        out.kbd.flags = uint8_t(_modifiers);
        return true;
    case reluva::EventType::MouseMove:
        _mouse = clampToScreen(in.mouseX, in.mouseY);
        out.type = kEventMousemove;
        out.mouse = _mouse;
        return true;
    case reluva::EventType::MouseDown:
    case reluva::EventType::MouseUp: {
        const bool down = in.type == reluva::EventType::MouseDown;
        _mouse = clampToScreen(in.mouseX, in.mouseY);
        out.mouse = _mouse;
        int bit = 0;
        switch (in.button) {
        case MouseButton::Left:
            out.type = down ? kEventLbuttondown : kEventLbuttonup;
            bit = 1;
            break;
        case MouseButton::Right:
            out.type = down ? kEventRbuttondown : kEventRbuttonup;
            bit = 2;
            break;
        case MouseButton::Middle:
            out.type = down ? kEventMbuttondown : kEventMbuttonup;
            bit = 4;
            break;
        default:
            return false;
        }
        _buttons = down ? _buttons | bit : _buttons & ~bit;
        return true;
    }
    case reluva::EventType::MouseWheel:
        if (in.wheelY == 0)
            return false;
        out.type = in.wheelY > 0 ? kEventWheelup : kEventWheeldown;
        return true;
    case reluva::EventType::FocusGained:
        out.type = kEventFocusGained;
        return true;
    case reluva::EventType::FocusLost:
        out.type = kEventFocusLost;
        return true;
    default:
        return false;
    }
}

bool Platform::pollEvent(Event &event) {
    if (!_queue.empty()) {
        event = _queue.front();
        _queue.pop_front();
        return true;
    }
    reluva::Event in;
    while (_system.pollEvent(in))
        if (translate(in, event))
            return true;
    return false;
}

uint32_t Platform::getMillis() const {
    return uint32_t(_system.getMillis());
}

void Platform::delayMillis(uint32_t ms) {
    _system.delayMillis(ms);
}

std::unique_ptr<SeekableReadStream> Platform::openForLoading(const std::string &name) const {
    std::vector<uint8_t> data;
    if (_saveDir.empty() || !readFile(_saveDir / name, data))
        return nullptr;
    return std::make_unique<MemoryReadStream>(std::move(data));
}

bool Platform::saveFile(const std::string &name, const std::vector<uint8_t> &data) {
    if (_saveDir.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(_saveDir, ec);
    // Written beside it first, so a failed write leaves the old one.
    const std::filesystem::path path = _saveDir / name;
    const std::filesystem::path part = _saveDir / (name + ".part");
    if (!writeFile(part, data))
        return false;
    std::filesystem::rename(part, path, ec);
    return !ec;
}

bool Platform::removeFile(const std::string &name) {
    std::error_code ec;
    return !_saveDir.empty() && std::filesystem::remove(_saveDir / name, ec);
}

bool Platform::fileExists(const std::string &name) const {
    std::error_code ec;
    return !_saveDir.empty() && std::filesystem::is_regular_file(_saveDir / name, ec);
}

std::vector<std::string> Platform::listFiles(const std::string &pattern) const {
    std::vector<std::string> out;
    std::error_code ec;
    if (_saveDir.empty() || !std::filesystem::is_directory(_saveDir, ec))
        return out;
    for (const auto &entry : std::filesystem::directory_iterator(_saveDir, ec)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file(ec) && text::matchGlob(name, pattern))
            out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace reluva::stgenerations
