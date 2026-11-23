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

// The game looks its sounds up by name (0x56dea0).
std::shared_ptr<audio::Sound> voiceOver(const Archive &data, const std::string &name) {
    const std::string want = normalizeArchiveName(name + ".ogg");
    for (const std::string &file : data.list()) {
        const std::string key = normalizeArchiveName(file);
        if (!key.starts_with(normalizeArchiveName("Levels/Voice Overs/")) || key.size() < want.size() ||
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

void Button::draw(Surface &screen, int originX, int originY, bool hover, bool pressed) const {
    const image::Image &picture = !_enabled && _disable.w             ? _disable
                                  : (pressed || _checked) && _press.w ? _press
                                  : hover && _hover.w                 ? _hover
                                                                      : _default;
    const int x = originX + _x;
    const int y = originY + _y;
    image::blit(screen, picture, x, y);
    if (!_font || !_font->loaded())
        return;
    const int labelX =
        _rightAligned ? x + width() - _textX - _font->width(_label) : x + (width() - _font->width(_label)) / 2 - _textX;
    _font->draw(screen, labelX, y + _textY, _label);
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
    for (int px = std::max(0, left); px < std::min(right, screen.w); ++px) {
        uint8_t *p = screen.row(lineY) + px * 4;
        p[0] = kShortcutLine[0];
        p[1] = kShortcutLine[1];
        p[2] = kShortcutLine[2];
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
        if (ev.button == MouseButton::kLeft)
            _pressed = _hover = buttonAt(ev.mouseX, ev.mouseY);
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
        for (size_t i = 0; i < _buttons.size(); ++i)
            if (_buttons[i].enabled() && isShortcut(ev, _buttons[i].shortcut()))
                chosen = int(i);
        break;
    default:
        break;
    }
    return chosen >= 0 ? press(size_t(chosen), mixer) : -1;
}

int ButtonGroup::press(size_t i, audio::Mixer *mixer) {
    if (!_buttons[i].enabled())
        return -1;
    if (mixer && _buttons[i].sound())
        mixer->play(_buttons[i].sound(), audio::SoundType::kEffects, audio::kMaxVolume, 0, false);
    return int(i);
}

void ButtonGroup::draw(Surface &screen) const {
    for (size_t i = 0; i < _buttons.size(); ++i)
        _buttons[i].draw(screen, _originX, _originY, int(i) == _hover, int(i) == _pressed && _hover == _pressed);
}

} // namespace reluva::stawayteam
