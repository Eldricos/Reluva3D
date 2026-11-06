// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/platform.h"

#include "common/text.h"

#include <algorithm>
#include <cstring>
#include <system_error>

namespace reluva::stgenerations {

Platform::Platform(System& system, audio::Mixer* mixer, std::filesystem::path saveDir)
    : system_(system), mixer_(mixer), saveDir_(std::move(saveDir)) {}

void Platform::initSize(int width, int height, Canvas::Format format) {
    if (graphics_ && screen_.w == width && screen_.h == height && screen_.format() == format)
        return;
    const PixelFormat pf = format == Canvas::kClut8 ? PixelFormat::CLUT8 : PixelFormat::RGBA32;
    system_.initGraphics(width, height, pf);
    graphics_ = true;
    screen_.create(width, height, format);
    frame_.create(width, height, pf);
    // A new screen starts black: the palette the game set is its again.
    if (format == Canvas::kClut8) {
        const auto colors = palette_.colors();
        system_.setPalette(colors.data(), 0, 256);
    }
}

void Platform::setPalette(const uint8_t* colors, unsigned start, unsigned num) {
    for (unsigned i = 0; i < num && start + i < 256; i++)
        palette_.setColor(int(start + i), colors[i * 3], colors[i * 3 + 1], colors[i * 3 + 2]);
    const auto all = palette_.colors();
    if (start < 256)
        system_.setPalette(all.data() + start, int(start), int(std::min(num, 256 - start)));
    // The pointer is coloured from the palette, as the original's was.
    if (cursorSet_)
        refreshCursor();
}

void Platform::grabPalette(uint8_t* colors, unsigned start, unsigned num) const {
    for (unsigned i = 0; i < num && start + i < 256; i++)
        palette_.getColor(int(start + i), colors[i * 3], colors[i * 3 + 1], colors[i * 3 + 2]);
}

void Platform::copyRectToScreen(const void* buf, int pitch, int x, int y, int w, int h) {
    screen_.copyRectToSurface(buf, pitch, x, y, w, h);
}

void Platform::updateScreen() {
    if (!graphics_)
        return;
    if (screen_.isClut8()) {
        for (int y = 0; y < screen_.h; y++)
            std::copy_n(static_cast<const uint8_t*>(screen_.getBasePtr(0, y)), screen_.w, frame_.row(y));
    } else {
        // 0xAARRGGBB to R, G, B, A bytes, shown opaque.
        for (int y = 0; y < screen_.h; y++) {
            const uint8_t* src = static_cast<const uint8_t*>(screen_.getBasePtr(0, y));
            uint8_t* dst = frame_.row(y);
            for (int x = 0; x < screen_.w; x++) {
                uint32_t c;
                std::memcpy(&c, src + x * 4, 4);
                dst[x * 4] = uint8_t(c >> 16);
                dst[x * 4 + 1] = uint8_t(c >> 8);
                dst[x * 4 + 2] = uint8_t(c);
                dst[x * 4 + 3] = 255;
            }
        }
    }
    system_.updateScreen(frame_);
}

void Platform::setWindowCaption(const std::string& caption) { system_.setWindowTitle(caption); }

bool Platform::showMouse(bool visible) {
    const bool was = mouseVisible_;
    mouseVisible_ = visible;
    system_.setMouseCursorVisible(visible);
    return was;
}

void Platform::setMouseCursor(const void* buf, unsigned w, unsigned h, int hotspotX, int hotspotY, uint32_t keycolor) {
    cursorImage_.create(int(w), int(h), Canvas::kClut8);
    cursorImage_.copyRectToSurface(buf, int(w), 0, 0, int(w), int(h));
    cursorHotX_ = hotspotX;
    cursorHotY_ = hotspotY;
    cursorKey_ = keycolor;
    cursorSet_ = true;
    refreshCursor();
}

void Platform::refreshCursor() {
    if (cursorImage_.empty()) {
        system_.setMouseCursor(nullptr);
        return;
    }
    const Cursor c = cursor::fromIndexed(static_cast<const uint8_t*>(cursorImage_.getPixels()), cursorImage_.w,
                                         cursorImage_.h, cursorImage_.pitch, palette_.data(), int(cursorKey_ & 0xFF),
                                         cursorHotX_, cursorHotY_);
    system_.setMouseCursor(&c);
}

void Platform::setTextInput(bool on) {
    if (on == textInput_)
        return;
    textInput_ = on;
    system_.setTextInput(on);
}

Point Platform::clampToScreen(int x, int y) const {
    // In the screen, as the original's window gave it (in the borders, or
    // dragged out of the window, at its edge).
    const int w = screen_.w > 0 ? screen_.w : 640, h = screen_.h > 0 ? screen_.h : 480;
    return Point(std::clamp(x, 0, w - 1), std::clamp(y, 0, h - 1));
}

bool Platform::translate(const reluva::Event& in, Event& out) {
    out = Event{};
    out.mouse = mouse_;
    auto flags = [](uint16_t mods) {
        return uint8_t(((mods & keymod::Ctrl) ? kKbdCtrl : 0) | ((mods & keymod::Alt) ? kKbdAlt : 0) |
                       ((mods & keymod::Shift) ? kKbdShift : 0));
    };
    switch (in.type) {
    case reluva::EventType::Quit:
        out.type = kEventQuit;
        quit_ = true;
        return true;
    case reluva::EventType::ReturnToLauncher:
        out.type = kEventReturnToLauncher;
        quit_ = true;
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
        case key::Backspace: out.kbd.ascii = 8; break;
        case key::Tab: out.kbd.ascii = 9; break;
        case key::Return:
        case key::KpEnter: out.kbd.ascii = 13; break;
        case key::Escape: out.kbd.ascii = 27; break;
        default: break;
        }
        modifiers_ = out.kbd.flags;
        return true;
    case reluva::EventType::TextInput:
        // A character typed, while text input is on: printable ASCII only,
        // as the original's names were.
        if (!textInput_ || in.text[0] < 32 || in.text[0] > 126 || in.text[1] != 0)
            return false;
        out.type = kEventKeydown;
        out.kbd.keycode = key::Unknown;
        out.kbd.ascii = uint16_t(in.text[0]);
        out.kbd.flags = uint8_t(modifiers_);
        return true;
    case reluva::EventType::MouseMove:
        mouse_ = clampToScreen(in.mouseX, in.mouseY);
        out.type = kEventMousemove;
        out.mouse = mouse_;
        return true;
    case reluva::EventType::MouseDown:
    case reluva::EventType::MouseUp: {
        const bool down = in.type == reluva::EventType::MouseDown;
        mouse_ = clampToScreen(in.mouseX, in.mouseY);
        out.mouse = mouse_;
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
        default: return false;
        }
        buttons_ = down ? buttons_ | bit : buttons_ & ~bit;
        return true;
    }
    case reluva::EventType::MouseWheel:
        if (in.wheelY == 0)
            return false;
        out.type = in.wheelY > 0 ? kEventWheelup : kEventWheeldown;
        return true;
    case reluva::EventType::FocusGained: out.type = kEventFocusGained; return true;
    case reluva::EventType::FocusLost: out.type = kEventFocusLost; return true;
    default: return false;
    }
}

bool Platform::pollEvent(Event& event) {
    if (!queue_.empty()) {
        event = queue_.front();
        queue_.pop_front();
        return true;
    }
    reluva::Event in;
    while (system_.pollEvent(in))
        if (translate(in, event))
            return true;
    return false;
}

uint32_t Platform::getMillis() const { return uint32_t(system_.getMillis()); }

void Platform::delayMillis(uint32_t ms) { system_.delayMillis(ms); }

std::unique_ptr<SeekableReadStream> Platform::openForLoading(const std::string& name) const {
    std::vector<uint8_t> data;
    if (saveDir_.empty() || !readFile(saveDir_ / name, data))
        return nullptr;
    return std::make_unique<MemoryReadStream>(std::move(data));
}

bool Platform::saveFile(const std::string& name, const std::vector<uint8_t>& data) {
    if (saveDir_.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(saveDir_, ec);
    // Written beside it first, so a failed write leaves the old one.
    const std::filesystem::path path = saveDir_ / name, part = saveDir_ / (name + ".part");
    if (!writeFile(part, data))
        return false;
    std::filesystem::rename(part, path, ec);
    return !ec;
}

bool Platform::removeFile(const std::string& name) {
    std::error_code ec;
    return !saveDir_.empty() && std::filesystem::remove(saveDir_ / name, ec);
}

bool Platform::fileExists(const std::string& name) const {
    std::error_code ec;
    return !saveDir_.empty() && std::filesystem::is_regular_file(saveDir_ / name, ec);
}

std::vector<std::string> Platform::listFiles(const std::string& pattern) const {
    std::vector<std::string> out;
    std::error_code ec;
    if (saveDir_.empty() || !std::filesystem::is_directory(saveDir_, ec))
        return out;
    for (const auto& entry : std::filesystem::directory_iterator(saveDir_, ec)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file(ec) && text::matchGlob(name, pattern))
            out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace reluva::stgenerations
