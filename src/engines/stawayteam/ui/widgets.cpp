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

// The toolkit's greys (0x408430): its frames' light and dark sides.
constexpr uint8_t kFrameDark = 0x50;
constexpr uint8_t kFrameLight = 0xc8;

void plot(Surface &screen, int x, int y, uint8_t grey = kCross) {
    if (x < 0 || y < 0 || x >= screen.w || y >= screen.h)
        return;
    uint8_t *p = screen.row(y) + x * 4;
    p[0] = p[1] = p[2] = grey;
    p[3] = 255;
}

void fillGrey(Surface &screen, int left, int top, int right, int bottom, uint8_t grey) {
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            plot(screen, x, y, grey);
}

// A cross corner to corner of the square at (x, y), a pixel in (0x4e6270).
void cross(Surface &screen, int x, int y, int size) {
    for (int i = 1; i <= size - 2; ++i) {
        plot(screen, x + i, y + i);
        plot(screen, x + i, y + size - 1 - i);
    }
}

// The toolkit's spacings count in fifths of a font's baseline.
int fifth(const Font *font) {
    return font && font->loaded() ? font->baseline() / 5 : 0;
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

bool isEnter(const Event &ev) {
    return ev.type == EventType::kKeyDown && (ev.key == key::kReturn || ev.key == key::kKpEnter);
}

bool isEscape(const Event &ev) {
    return ev.type == EventType::kKeyDown && ev.key == key::kEscape;
}

bool isShortcut(const Event &ev, char shortcut) {
    const auto lower = [](uint32_t c) { return c < 0x80 ? uint32_t(std::tolower(int(c))) : c; };
    return ev.type == EventType::kKeyDown && shortcut && (ev.mods & keymod::kAlt) && !(ev.mods & keymod::kCtrl) &&
           lower(ev.key) == lower(static_cast<unsigned char>(shortcut));
}

PictureButton::PictureButton(image::Image picture, const std::string &label, const Font *font)
    : _picture(std::move(picture)), _font(font), _brightness(1.0f) {
    _label = withoutShortcut(label, _shortcut);
}

bool PictureButton::handleEvent(const Event &ev, int originX, int originY, audio::Mixer *mixer) {
    if (!_enabled)
        return false;
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
        chosen = isShortcut(ev, _shortcut);
        break;
    default:
        break;
    }
    return chosen && press(mixer);
}

bool PictureButton::press(audio::Mixer *mixer) {
    if (!_enabled)
        return false;
    if (mixer && _sound)
        mixer->play(_sound, audio::SoundType::kEffects, audio::kMaxVolume, 0, false);
    return true;
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
    // A disabled one's label at the picture's dimness (inferred).
    if (_font && _font->loaded())
        _font->draw(screen, x + kLabelX + in, y + kLabelY + in, _label, 255, _enabled ? 1.0f : kDim);
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
    // The box taken as square.
    if (_checked && !(_pressed && _hover))
        cross(screen, x, y, box);
    if (_font && _font->loaded())
        _font->draw(screen, x + box, y + 1, _label);
}

TextButton::TextButton(std::string label, const Font *font, int w, int h)
    : _label(std::move(label)), _font(font), _w(w), _h(h) {
}

bool TextButton::handleEvent(const Event &ev, int originX, int originY) {
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

void TextButton::draw(Surface &screen, int originX, int originY, uint64_t now) {
    // Bright under the mouse or checked (0x5effa0, 0x5f0047), 2 a second.
    const float target = _hover || _checked ? 1.0f : kDim;
    const float step = _lastDraw ? float(now - _lastDraw) * kBrightPerMs : 0.0f;
    _lastDraw = now;
    _brightness = _brightness < target ? std::min(target, _brightness + step) : std::max(target, _brightness - step);
    if (!_font || !_font->loaded())
        return;
    const int in = _pressed && _hover ? 1 : 0;
    const int x = originX + _x + (_rightAligned ? _w - _textX - _font->width(_label) : _textX) + in;
    _font->draw(screen, x, originY + _y + _textY + in, _label, 255, _brightness);
}

void drawBevel(Surface &screen, int x, int y, int w, int h, int thickness, bool raised) {
    const uint8_t topLeft = raised ? kFrameLight : kFrameDark;
    const uint8_t bottomRight = raised ? kFrameDark : kFrameLight;
    int left = x;
    int top = y;
    int right = x + w;
    int bottom = y + h;
    for (int i = 0; i < thickness; ++i) {
        fillGrey(screen, left, top, right, top + 1, topLeft);
        fillGrey(screen, left, top + 1, left + 1, bottom, topLeft);
        fillGrey(screen, right - 1, top + 1, right, bottom - 1, bottomRight);
        fillGrey(screen, left + 1, bottom - 1, right, bottom, bottomRight);
        ++left;
        ++top;
        --right;
        --bottom;
    }
}

Slider::Slider(const Font *font, int maximum, image::Image thumbFill)
    : _font(font), _maximum(maximum), _thumbFill(std::move(thumbFill)) {
}

int Slider::height() const {
    // As high as the font's baseline, 4 fifths of it and 2 (0x4e4b82).
    return (_font && _font->loaded() ? _font->baseline() : 0) + 4 * fifth(_font) + 2;
}

void Slider::setValue(int value) {
    _value = std::clamp(value, 0, std::max(0, _maximum));
}

int Slider::valueAt(int mx) const {
    const int fb = _font && _font->loaded() ? _font->baseline() : 0;
    const int span = _w - fb - 1;
    if (span <= 0)
        return 0;
    // The thumb's middle under the mouse, rounded to the nearest value.
    return ((mx - (fb + 1) / 2) * (_maximum - 1) + span / 2) / span;
}

bool Slider::handleEvent(const Event &ev, int originX, int originY) {
    const int mx = ev.mouseX - originX - _x;
    const int my = ev.mouseY - originY - _y;
    const int before = _value;
    if (!_enabled)
        return false;
    const int last = std::max(0, _maximum - 1);
    // A twentieth of its range and a half, at least 1 (0x4e4b59).
    const int step = std::max(1, (_maximum + 10) / 20);
    const bool over = mx >= 0 && my >= 0 && mx < _w && my < height();
    switch (ev.type) {
    case EventType::kMouseDown:
        if (ev.button == MouseButton::kLeft && over) {
            _dragging = true;
            _focused = true;
            _value = std::clamp(valueAt(mx), 0, last);
        }
        break;
    case EventType::kMouseMove:
        if (_dragging)
            _value = std::clamp(valueAt(mx), 0, last);
        break;
    case EventType::kMouseWheel:
        // Up a step, down a step (0x4e4f70).
        if (over && ev.wheelY > 0)
            _value = std::min(last, _value + step);
        else if (over && ev.wheelY < 0)
            _value = std::max(0, _value - step);
        break;
    case EventType::kKeyDown:
        // The program's keys 5, 14, 11 and 12 (0x4e4fa0), from these (0x5fa470).
        if (!_focused)
            break;
        if (ev.key == key::kHome)
            _value = 0;
        else if (ev.key == key::kEnd)
            _value = last;
        else if (ev.key == key::kLeft)
            _value = std::max(0, _value - step);
        else if (ev.key == key::kRight)
            _value = std::min(last, _value + step);
        break;
    case EventType::kMouseUp:
        if (ev.button == MouseButton::kLeft)
            _dragging = false;
        break;
    default:
        break;
    }
    return _value != before;
}

void Slider::draw(Surface &screen, int originX, int originY) const {
    const int x = originX + _x;
    const int y = originY + _y;
    const int u = fifth(_font);
    const int fb = _font && _font->loaded() ? _font->baseline() : 0;
    // The track, sunken, and a groove along its middle (0x4e4bd0).
    drawBevel(screen, x, y + 2 * u + 1, _w, height() - 4 * u - 2, u, false);
    fillGrey(screen, x + u, y + height() / 2, x + _w - u + 1, y + height() / 2 + 1, kFrameDark);
    fillGrey(screen, x + u, y + height() / 2 + 1, x + _w - u + 1, y + height() / 2 + 2, kFrameLight);
    // The thumb, raised, at its place for the value (0x4e4d24).
    int pos = 0;
    if (_maximum > 1)
        pos = ((_w - fb - 1) * _value + (_maximum - 1) / 2) / (_maximum - 1);
    const int thumbW = fb + 1;
    if (_thumbFill.w > 0 && _thumbFill.h > 0)
        for (int ty = 0; ty < height(); ++ty) {
            const int sy = y + ty;
            if (sy < 0 || sy >= screen.h)
                continue;
            for (int tx = 0; tx < thumbW; ++tx) {
                const int sx = x + pos + tx;
                if (sx < 0 || sx >= screen.w)
                    continue;
                // The texture tiled from the screen's corner (inferred).
                const uint8_t *src = _thumbFill.pixel(sx % _thumbFill.w, sy % _thumbFill.h);
                uint8_t *dst = screen.row(sy) + sx * 4;
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 255;
            }
        }
    drawBevel(screen, x + pos, y, thumbW, height(), u, true);
}

CheckBox::CheckBox(const std::string &label, const Font *font) : _font(font) {
    _label = withoutShortcut(label, _shortcut);
}

int CheckBox::box() const {
    // The font's baseline and 2 (0x4e6053).
    return (_font && _font->loaded() ? _font->baseline() : 0) + 2;
}

int CheckBox::width() const {
    return box() + (_font && _font->loaded() ? _font->width(_label) : 0) + 1;
}

int CheckBox::height() const {
    return box();
}

bool CheckBox::handleEvent(const Event &ev, int originX, int originY) {
    const int mx = ev.mouseX - originX;
    const int my = ev.mouseY - originY;
    bool clicked = false;
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
            clicked = _pressed && _hover;
            _pressed = false;
        }
        break;
    case EventType::kKeyDown:
        clicked = isShortcut(ev, _shortcut);
        break;
    default:
        break;
    }
    if (clicked)
        _checked = !_checked;
    return clicked;
}

void CheckBox::draw(Surface &screen, int originX, int originY) const {
    const int x = originX + _x;
    const int y = originY + _y;
    const int size = box();
    // Its label first, after the box, a pixel down (0x4e62e0).
    if (_font && _font->loaded())
        _font->draw(screen, x + size, y + 1, _label);
    // Held down on or checked, sunken; otherwise raised (0x4e6150, 0x4e61f0).
    const bool down = (_pressed && _hover) || _checked;
    drawBevel(screen, x, y, size, size, fifth(_font), !down);
    if (_checked && !(_pressed && _hover))
        cross(screen, x, y, size);
}

} // namespace reluva::stawayteam
