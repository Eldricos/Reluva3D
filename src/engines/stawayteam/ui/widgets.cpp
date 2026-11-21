// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/widgets.h"

#include <algorithm>
#include <cctype>

namespace reluva::stawayteam {

namespace {

// A picture button's label, from its left and top (0x5eff58: the 20 and
// -2 the button sets, and 1); pressed, a pixel further each way.
constexpr int kLabelX = 21;
constexpr int kLabelY = -1;
// Its picture's brightness: from 1 it goes to 0.4, and to 1 again under
// the mouse, 2 a second (0x5efe60, 0x5f0056).
constexpr float kDim = 0.4f;
constexpr float kBrightPerMs = 0.002f;
// The cross over a checked toggle's box (0x4e6270), in the toolkit's
// colour for it (0x4084e4).
constexpr uint8_t kCross = 0xba;

// `label` without its first `&`, and the key after it, in lower case.
std::string withoutShortcut(const std::string &label, char &shortcut) {
    shortcut = 0;
    std::string out;
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] == '&' && !shortcut && i + 1 < label.size())
            shortcut = char(std::tolower(static_cast<unsigned char>(label[i + 1])));
        else
            out += label[i];
    }
    return out;
}

void plot(Surface &screen, int x, int y) {
    if (x < 0 || y < 0 || x >= screen.w || y >= screen.h)
        return;
    uint8_t *p = screen.row(y) + x * 4;
    p[0] = p[1] = p[2] = kCross;
    p[3] = 255;
}

} // namespace

image::Image dimmed(const image::Image &picture, float brightness) {
    image::Image out = picture;
    const int scale = std::clamp(int(brightness * 256.0f), 0, 256);
    for (size_t i = 0; i < out.rgba.size(); i += 4)
        for (size_t c = 0; c < 3; ++c)
            out.rgba[i + c] = uint8_t(out.rgba[i + c] * scale / 256);
    return out;
}

PictureButton::PictureButton(image::Image picture, const std::string &label, const Font *font)
    : _picture(std::move(picture)), _font(font), _brightness(1.0f) {
    _label = withoutShortcut(label, _shortcut);
}

bool PictureButton::handleEvent(const Event &ev, int originX, int originY, audio::Mixer *mixer) {
    bool chosen = false;
    const int mx = ev.mouseX - originX;
    const int my = ev.mouseY - originY;
    switch (ev.type) {
    case EventType::kMouseMove:
        _hover = contains(mx, my);
        break;
    case EventType::kMouseDown:
        if (ev.button == MouseButton::kLeft)
            _pressed = _hover = contains(mx, my);
        break;
    case EventType::kMouseUp:
        if (ev.button == MouseButton::kLeft) {
            _hover = contains(mx, my);
            chosen = _pressed && _hover;
            _pressed = false;
        }
        break;
    case EventType::kKeyDown:
        chosen = !(ev.mods & (keymod::kCtrl | keymod::kAlt)) && _shortcut &&
                 ev.key == uint32_t(static_cast<unsigned char>(_shortcut));
        break;
    default:
        break;
    }
    if (chosen && mixer && _sound)
        mixer->play(_sound, audio::SoundType::kEffects, audio::kMaxVolume, 0, false);
    return chosen;
}

void PictureButton::draw(Surface &screen, int originX, int originY, uint64_t now) {
    const float target = _hover ? 1.0f : kDim;
    const float step = _lastDraw ? float(now - _lastDraw) * kBrightPerMs : 0.0f;
    _lastDraw = now;
    _brightness = _brightness < target ? std::min(target, _brightness + step) : std::max(target, _brightness - step);
    const int x = originX + _x;
    const int y = originY + _y;
    image::blit(screen, dimmed(_picture, _brightness), x, y);
    const int in = _pressed && _hover ? 1 : 0;
    if (_font && _font->loaded())
        _font->draw(screen, x + kLabelX + in, y + kLabelY + in, _label);
}

Toggle::Toggle(image::Image normal, image::Image press, std::string label, const Font *font)
    : _normal(std::move(normal)), _press(std::move(press)), _label(std::move(label)), _font(font) {
}

int Toggle::width() const {
    const int box = std::max(_normal.w, _press.w);
    return box + (_font && _font->loaded() ? _font->width(_label) : 0) + 1;
}

int Toggle::height() const {
    return std::max(_normal.h, _press.h);
}

bool Toggle::handleEvent(const Event &ev, int originX, int originY) {
    const int mx = ev.mouseX - originX;
    const int my = ev.mouseY - originY;
    switch (ev.type) {
    case EventType::kMouseMove:
        _hover = contains(mx, my);
        return false;
    case EventType::kMouseDown:
        if (ev.button == MouseButton::kLeft)
            _pressed = _hover = contains(mx, my);
        return false;
    case EventType::kMouseUp:
        if (ev.button == MouseButton::kLeft) {
            _hover = contains(mx, my);
            const bool chosen = _pressed && _hover;
            _pressed = false;
            return chosen;
        }
        return false;
    default:
        return false;
    }
}

void Toggle::draw(Surface &screen, int originX, int originY) const {
    const int x = originX + _x;
    const int y = originY + _y;
    // Held down on, or checked: its Press picture (0x4e63c1, 0x4e63db).
    image::blit(screen, (_pressed && _hover) || _checked ? _press : _normal, x, y);
    const int box = std::max(_normal.w, _press.w);
    if (_checked && !(_pressed && _hover)) {
        // A cross corner to corner, a pixel in, the box taken as square.
        for (int i = 1; i <= box - 2; ++i) {
            plot(screen, x + i, y + i);
            plot(screen, x + i, y + box - 1 - i);
        }
    }
    if (_font && _font->loaded())
        _font->draw(screen, x + box, y + 1, _label);
}

} // namespace reluva::stawayteam
