// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/button.h"

#include "audio/decode.h"
#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/ui/widgets.h"

#include <algorithm>
#include <cctype>

namespace reluva::stawayteam {

namespace {

constexpr uint8_t kShortcutLine[3] = {63, 154, 255};

} // namespace

image::Image screenPicture(const Archive &data, const std::string &name) {
    std::vector<uint8_t> bytes;
    Frame frame;
    if (data.read("Cache/Interface/Screens/" + name + ".frm16", bytes) || readFrame(bytes.data(), bytes.size(), frame))
        return {};
    return frameImage(frame);
}

std::shared_ptr<audio::Sound> gameSound(const Archive &data, const std::string &name) {
    // The game's sounds are all Ogg Vorbis in DATA.dat, whatever their
    // names' ending in the program; the toolkit's ("UI/Button Action.wav")
    // are in Sounds/ itself.
    std::string file = name;
    const size_t dot = file.rfind('.');
    if (dot != std::string::npos)
        file.resize(dot);
    std::vector<uint8_t> bytes;
    if (data.read("Sounds/" + file + ".ogg", bytes)) {
        const size_t slash = file.rfind('/');
        if (slash == std::string::npos || data.read("Sounds/" + file.substr(slash + 1) + ".ogg", bytes))
            return nullptr;
    }
    auto sound = std::make_shared<audio::Sound>();
    if (audio::decodeSound(bytes, *sound))
        return nullptr;
    return sound;
}

// The game looks "<name>.wav" up (0x472d1e) in its list of every sound
// under Levels, an .ogg standing for a .wav (0x4f609d, 0x56dea0).
std::shared_ptr<audio::Sound> voiceOver(const Archive &data, const std::string &name) {
    const std::string want = normalizeArchiveName(name + ".ogg");
    for (const std::string &file : data.list()) {
        const std::string key = normalizeArchiveName(file);
        if (!key.starts_with(normalizeArchiveName("Levels/")) || key.size() < want.size() ||
            key.compare(key.size() - want.size(), want.size(), want) != 0 ||
            (key.size() > want.size() && key[key.size() - want.size() - 1] != '/'))
            continue;
        std::vector<uint8_t> bytes;
        auto sound = std::make_shared<audio::Sound>();
        if (!data.read(file, bytes) && !audio::decodeSound(bytes, *sound))
            return sound;
    }
    return nullptr;
}

Button::Button(const Archive &data, const std::string &folder, const std::string &name, int x, int y)
    : _x(x), _y(y), _default(screenPicture(data, folder + "/" + name + " Default")),
      _hover(screenPicture(data, folder + "/" + name + " Hover")),
      _press(screenPicture(data, folder + "/" + name + " Press")),
      _disable(screenPicture(data, folder + "/" + name + " Disable")) {
}

Button::Button(image::Image picture, int x, int y) : _x(x), _y(y), _default(std::move(picture)) {
}

void Button::setLabel(const std::string &label, const Font *font, int textX, int textY) {
    _label.clear();
    _shortcut = std::string::npos;
    // The & is left out, and the key after it kept (0x5ccdc0).
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] == '&' && _shortcut == std::string::npos && i + 1 < label.size())
            _shortcut = _label.size();
        else
            _label += label[i];
    }
    _font = font;
    _textX = textX;
    _textY = textY;
}

char Button::shortcut() const {
    if (_shortcut == std::string::npos)
        return 0;
    return char(std::tolower(static_cast<unsigned char>(_label[_shortcut])));
}

bool Button::contains(int x, int y) const {
    return x >= _x && y >= _y && x < _x + width() && y < _y + height();
}

const image::Image &Button::picture(int state) const {
    switch (state) {
    case 0:
        return _press.w ? _press : _default;
    case 1:
        return _hover.w ? _hover : _default;
    case 2:
        return _disable.w ? _disable : _default;
    default:
        return _default;
    }
}

void Button::draw(Surface &screen, int originX, int originY, bool hover, bool pressed, uint64_t now) const {
    // Under the mouse, pressed or checked lights it; away, only checked
    // (0x4e8140).
    const int state = !_enabled ? 2 : hover ? (pressed || _checked ? 0 : 1) : (_checked ? 0 : 3);
    // The label's brightness goes to 1, or to 0 for the states 2 and 3,
    // and a new state's picture fades in over the old one's, which fades
    // out, both at the fade rate (0x5ccfc0, 0x5cd290).
    const float target = state == 2 || state == 3 ? 0.0f : 1.0f;
    if (_state < 0) {
        _state = state;
        _lit = target;
    } else if (state != _state) {
        _previous = _state;
        _state = state;
        _pictureFade = 1.0f - _pictureFade;
    }
    const float step = now && _lastDraw ? float(now - _lastDraw) / 1000.0f * _fadeRate : 1.0f;
    _lastDraw = now;
    _pictureFade = std::min(1.0f, _pictureFade + step);
    _lit = _lit < target ? std::min(target, _lit + step) : std::max(target, _lit - step);

    const int x = originX + _x;
    const int y = originY + _y;
    if (_pictureFade < 1.0f && _previous >= 0) {
        image::blit(screen, dimmed(picture(_previous), toolkitScale(1.0f - _pictureFade)), x, y);
        image::blit(screen, dimmed(picture(_state), toolkitScale(_pictureFade)), x, y);
    } else {
        image::blit(screen, picture(_state), x, y);
    }
    if (!_font || !_font->loaded())
        return;
    const int labelX =
        _rightAligned ? x + width() - _textX - _font->width(_label) : x + (width() - _font->width(_label)) / 2 - _textX;
    // The label's drawn 0.66 bright, up to 1 as it's lit (0x5cd0fe).
    const float brightness = 0.66f + 0.34f * _lit;
    _font->draw(screen, labelX, y + _textY, _label, 255, toolkitScale(brightness));
    // An enabled button's shortcut is underlined, whether or not Alt is
    // held: a row under the baseline, kept inside the button, 1 in from
    // each side of the character, in the toolkit's light blue (0x5cd148,
    // its colour 0x408511).
    if (!_enabled || _shortcut >= _label.size())
        return;
    const int left = labelX + _font->width(_label.substr(0, _shortcut)) + 1;
    const int right = left + _font->width(_label.substr(_shortcut, 1)) - 2;
    const int lineY = y + std::min(_textY + _font->baseline() + 1, height() - 1);
    if (lineY < 0 || lineY >= screen.h)
        return;
    // Its colour's alpha is the label's brightness (0x5cd1c4).
    const int alpha = std::clamp(int(brightness * 255.0f), 0, 255);
    for (int px = std::max(0, left); px < std::min(right, screen.w); ++px) {
        uint8_t *p = screen.row(lineY) + px * 4;
        for (int c = 0; c < 3; ++c)
            p[c] = uint8_t((kShortcutLine[c] * alpha + p[c] * (255 - alpha) + 127) / 255);
        p[3] = 255;
    }
}

int ButtonGroup::buttonAt(int x, int y) const {
    for (size_t i = 0; i < _buttons.size(); ++i)
        if (_buttons[i].enabled() && _buttons[i].contains(x - _originX, y - _originY))
            return int(i);
    return -1;
}

int ButtonGroup::handleEvent(const Event &ev, audio::Mixer *mixer) {
    int chosen = -1;
    switch (ev.type) {
    case EventType::kMouseMove:
        _hover = buttonAt(ev.mouseX, ev.mouseY);
        break;
    case EventType::kMouseDown:
        if (ev.button == MouseButton::kLeft) {
            _pressed = _hover = buttonAt(ev.mouseX, ev.mouseY);
            // Pressed by the mouse, it takes the focus (0x408dd3).
            if (_focus >= 0 && _pressed >= 0 && _buttons[size_t(_pressed)].enabled())
                _focus = _pressed;
        }
        break;
    case EventType::kMouseUp:
        if (ev.button == MouseButton::kLeft) {
            _hover = buttonAt(ev.mouseX, ev.mouseY);
            if (_pressed >= 0 && _pressed == _hover)
                chosen = _pressed;
            _pressed = -1;
        }
        break;
    case EventType::kKeyDown:
        if (_focus >= 0 && !_buttons.empty()) {
            const bool shift = (ev.mods & keymod::kShift) != 0;
            const int count = int(_buttons.size());
            if (ev.key == key::kDown || ev.key == key::kRight || (ev.key == key::kTab && !shift))
                _focus = (_focus + 1) % count;
            else if (ev.key == key::kUp || ev.key == key::kLeft || (ev.key == key::kTab && shift))
                _focus = (_focus + count - 1) % count;
        }
        for (size_t i = 0; i < _buttons.size(); ++i)
            if (_buttons[i].enabled() && isShortcut(ev, _buttons[i].shortcut()))
                chosen = int(i);
        break;
    default:
        break;
    }
    return chosen >= 0 ? press(size_t(chosen), mixer) : -1;
}

int ButtonGroup::pressFocused(size_t fallback, audio::Mixer *mixer) {
    if (_focus >= 0 && _buttons[size_t(_focus)].enabled())
        return press(size_t(_focus), mixer);
    return press(fallback, mixer);
}

int ButtonGroup::press(size_t i, audio::Mixer *mixer) {
    if (!_buttons[i].enabled())
        return -1;
    if (mixer && _buttons[i].sound())
        mixer->play(_buttons[i].sound(), audio::SoundType::kEffects, audio::kMaxVolume, 0, false);
    return int(i);
}

void ButtonGroup::draw(Surface &screen, uint64_t now) const {
    for (size_t i = 0; i < _buttons.size(); ++i)
        _buttons[i].draw(screen, _originX, _originY, int(i) == _hover, int(i) == _pressed && _hover == _pressed, now);
}

void ButtonGroup::setFadeRate(float perSecond) {
    for (Button &button : _buttons)
        button.setFadeRate(perSecond);
}

} // namespace reluva::stawayteam
