// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/button.h"

#include "audio/decode.h"
#include "engines/stawayteam/formats/frame.h"

#include <cctype>

namespace reluva::stawayteam {

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

Button::Button(const Archive &data, const std::string &folder, const std::string &name, int x, int y)
    : _x(x), _y(y), _default(screenPicture(data, folder + "/" + name + " Default")),
      _hover(screenPicture(data, folder + "/" + name + " Hover")),
      _press(screenPicture(data, folder + "/" + name + " Press")) {
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
    const image::Image &picture = (pressed || _checked) && _press.w ? _press : hover && _hover.w ? _hover : _default;
    const int x = originX + _x;
    const int y = originY + _y;
    image::blit(screen, picture, x, y);
    if (_font && _font->loaded())
        _font->draw(screen, x + (width() - _font->width(_label)) / 2 - _textX, y + _textY, _label);
}

int ButtonGroup::buttonAt(int x, int y) const {
    for (size_t i = 0; i < _buttons.size(); ++i)
        if (_buttons[i].contains(x - _originX, y - _originY))
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
        if (ev.mods & (keymod::kCtrl | keymod::kAlt))
            break;
        for (size_t i = 0; i < _buttons.size(); ++i)
            if (_buttons[i].shortcut() && uint32_t(static_cast<unsigned char>(_buttons[i].shortcut())) == ev.key)
                chosen = int(i);
        break;
    default:
        break;
    }
    if (chosen >= 0 && mixer && _buttons[size_t(chosen)].sound())
        mixer->play(_buttons[size_t(chosen)].sound(), audio::SoundType::kEffects, audio::kMaxVolume, 0, false);
    return chosen;
}

void ButtonGroup::draw(Surface &screen) const {
    for (size_t i = 0; i < _buttons.size(); ++i)
        _buttons[i].draw(screen, _originX, _originY, int(i) == _hover, int(i) == _pressed && _hover == _pressed);
}

} // namespace reluva::stawayteam
