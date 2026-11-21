// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/settings.h"

#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/ui/button.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

// The window's layout, as 0x4863b0 builds it with the toolkit (see the
// docs): buttons at 69 across; the first 41 down, the second 5 under it;
// the labels 4 apart, the first 8 under the second button; the sliders'
// right edge 127 in from the window's, then 8 further; the check box 4
// under the last label, and 4 more; and at the bottom, CONTINUE 29 up,
// each button above it 4 higher.
constexpr int kButtonX = 0x45;
constexpr int kFirstButtonY = 5 + 0x24;
constexpr int kButtonGap = 5;
constexpr int kLabelGap = 4;
constexpr int kSliderRightInset = 200 - 4 - 69;
constexpr int kSliderShift = 8;
constexpr int kContinueBottomGap = 0x1d;
constexpr int kStackGap = 4;
// The sliders run 0 to 99 (0x48663b).
constexpr int kSliderMaximum = 100;

// A slider's 0-99 for `value` between `low` and `high` (0x486b9e), and back (0x486f04).
int toSlider(float value, float low, float high) {
    return std::clamp(int((value - low) * 100.0f / (high - low)), 0, kSliderMaximum - 1);
}

float fromSlider(int slider, float low, float high) {
    return low + (high - low) * float(slider) * 0.01f;
}

image::Image cachePicture(const Archive &data, const std::string &name) {
    std::vector<uint8_t> bytes;
    Frame frame;
    if (data.read("Cache/" + name + ".frm16", bytes) || readFrame(bytes.data(), bytes.size(), frame))
        return {};
    return frameImage(frame);
}

} // namespace

SettingsWindow::SettingsWindow(const Archive &data, GameOptions &options, int screenW, int screenH)
    : _options(options), _background(screenPicture(data, "Configure Menu/Background")),
      _keyboard(screenPicture(data, "Configure Menu/Button Background"), "CONFIGURE &KEYBOARD", &_buttonFont),
      _sound(screenPicture(data, "Configure Menu/Button Background"), "CONFIGURE &SOUND", &_buttonFont),
      _labels{"Brightness:", "Scroll Speed:", "Scroll Accel:"},
      _sliders{Slider(&_font, kSliderMaximum, cachePicture(data, "Interface/Window Texture")),
               Slider(&_font, kSliderMaximum, cachePicture(data, "Interface/Window Texture")),
               Slider(&_font, kSliderMaximum, cachePicture(data, "Interface/Window Texture"))},
      _pause("&Pause Game When Hit Off-screen", &_font),
      _continue(screenPicture(data, "Configure Menu/Button Background"), "&CONTINUE", &_buttonFont),
      _reset(screenPicture(data, "Configure Menu/Button Background"), "RESET &DEFAULTS", &_buttonFont),
      _difficulty(screenPicture(data, "Configure Menu/Button Background"), "DI&FFICULTY SETTING", &_buttonFont),
      _x((screenW - _background.w) / 2), _y((screenH - _background.h) / 2) {
    // Both fonts named by the program (0x4863e7, 0x486505).
    (void)_buttonFont.load(data, "Placar Condensed");
    (void)_font.load(data, "Trek Small");
    const int w = _background.w;
    const int h = _background.h;
    const auto sound = gameSound(data, "UI/Button Action.wav");
    for (PictureButton *button : {&_keyboard, &_sound, &_continue, &_reset, &_difficulty})
        button->setSound(sound);

    _keyboard.setPosition(kButtonX, kFirstButtonY);
    _sound.setPosition(kButtonX, _keyboard.y() + _keyboard.height() + kButtonGap);
    // A label is as high as its font's baseline and 2, its text a pixel
    // left of and below its corner (right-justified in its own width).
    _labelX = kButtonX;
    const int labelH = _font.loaded() ? _font.baseline() + 2 : 0;
    int y = _sound.y() + _sound.height() + kLabelGap + kLabelGap;
    int shortest = w;
    for (size_t i = 0; i < _labels.size(); ++i) {
        _labelY[i] = y;
        y += labelH + kLabelGap;
        shortest = std::min(shortest, _font.loaded() ? _font.width(_labels[i]) : 0);
    }
    // Every slider as wide as the widest, which is for the shortest label,
    // their right edges lined up (0x486773).
    const int sliderW = w - shortest - 200;
    const int sliderRight = w - kSliderRightInset + kSliderShift;
    for (size_t i = 0; i < _sliders.size(); ++i) {
        _sliders[i].setWidth(sliderW);
        _sliders[i].setPosition(sliderRight - sliderW, _labelY[i]);
    }
    _pause.setPosition(kButtonX, _labelY[2] + labelH + kLabelGap + kLabelGap);

    _continue.setPosition(kButtonX, h - _continue.height() - kContinueBottomGap);
    _reset.setPosition(kButtonX, _continue.y() - _reset.height() - kStackGap);
    _difficulty.setPosition(kButtonX, _reset.y() - _difficulty.height() - kStackGap);
    showOptions();
}

void SettingsWindow::showOptions() {
    _sliders[0].setValue(int((_options.brightness - GameOptions::kBrightnessMin) * 200.0f));
    _sliders[1].setValue(toSlider(_options.scrollSpeed, GameOptions::kScrollSpeedMin, GameOptions::kScrollSpeedMax));
    _sliders[2].setValue(toSlider(_options.scrollAccel, GameOptions::kScrollAccelMin, GameOptions::kScrollAccelMax));
    _pause.setChecked(_options.pauseWhenHit);
}

SettingsWindow::Choice SettingsWindow::handleEvent(const Event &ev, audio::Mixer *mixer) {
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape)
        return Choice::kContinue;
    bool changed = false;
    if (_sliders[0].handleEvent(ev, _x, _y)) {
        // Gamma 1 to 1.5 (0x486e5f).
        _options.brightness = std::clamp(GameOptions::kBrightnessMin + float(_sliders[0].value()) * 0.5f * 0.01f,
                                         GameOptions::kBrightnessMin, GameOptions::kBrightnessMax);
        changed = true;
    }
    if (_sliders[1].handleEvent(ev, _x, _y)) {
        _options.scrollSpeed =
            fromSlider(_sliders[1].value(), GameOptions::kScrollSpeedMin, GameOptions::kScrollSpeedMax);
        changed = true;
    }
    if (_sliders[2].handleEvent(ev, _x, _y)) {
        _options.scrollAccel =
            fromSlider(_sliders[2].value(), GameOptions::kScrollAccelMin, GameOptions::kScrollAccelMax);
        changed = true;
    }
    if (_pause.handleEvent(ev, _x, _y)) {
        _options.pauseWhenHit = _pause.checked();
        changed = true;
    }
    // Neither the keyboard's nor the sound's settings are there yet.
    (void)_keyboard.handleEvent(ev, _x, _y, mixer);
    (void)_sound.handleEvent(ev, _x, _y, mixer);
    Choice choice = Choice::kNone;
    if (_reset.handleEvent(ev, _x, _y, mixer)) {
        // The program's defaults, and pausing when hit (0x486d32).
        _options.scrollSpeed = GameOptions::kScrollSpeedDefault;
        _options.scrollAccel = GameOptions::kScrollAccelDefault;
        _options.brightness = GameOptions::kBrightnessMin;
        _options.pauseWhenHit = true;
        showOptions();
        changed = true;
    }
    if (_difficulty.handleEvent(ev, _x, _y, mixer))
        choice = Choice::kDifficulty;
    if (_continue.handleEvent(ev, _x, _y, mixer))
        choice = Choice::kContinue;
    if (changed)
        _options.save();
    return choice;
}

void SettingsWindow::draw(Surface &screen, uint64_t now) {
    image::blit(screen, _background, _x, _y);
    _keyboard.draw(screen, _x, _y, now);
    _sound.draw(screen, _x, _y, now);
    for (size_t i = 0; i < _labels.size(); ++i) {
        if (_font.loaded())
            _font.draw(screen, _x + _labelX - 1, _y + _labelY[i] + 1, _labels[i]);
        _sliders[i].draw(screen, _x, _y);
    }
    _pause.draw(screen, _x, _y);
    _difficulty.draw(screen, _x, _y, now);
    _reset.draw(screen, _x, _y, now);
    _continue.draw(screen, _x, _y, now);
}

} // namespace reluva::stawayteam
