// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/sound_options.h"

#include "engines/stawayteam/formats/frame.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

// The labels' texts (0x645728...); the first three's `&` is drawn as it
// is by a label, so it's left out here as the game shows them.
constexpr std::array<const char *, 6> kLabels = {"Master Volume", "Sfx Volume", "Music Volume",
                                                 "Voiceovers",    "Stereo",     "Sound Mixing"};
constexpr std::array<const char *, 6> kPairLabels = {"ON", "OFF", "STEREO", "MONO", "FASTER", "BETTER"};
// The volume sliders run to 3333 (0x56eb12), a volume being the slider less
// that in hundredths of a decibel; effects' 0.5 to 1 (0x56eca0).
constexpr int kSliderMaximum = 0xd05;
constexpr float kEffectsScale = 6666.0f;
// The layout, as 0x56e8f0 builds it with the toolkit: the window's
// spacing 5 (0x56e985); the first label at 100 across (0x56ea96); the
// sliders 125 wide (0x56f121); the paired buttons at the sliders' right,
// 4 apart (0x56f2f2); CONTINUE at 69 across, 29 above the bottom.
constexpr int kSpacing = 5;
constexpr int kFirstLabelX = 100;
constexpr int kSliderWidth = 125;
constexpr int kPairGap = 4;
constexpr int kButtonX = 0x45;
constexpr int kContinueBottomGap = 0x1d;
// A label's border: 2 fifths of its font's baseline (0x56e97e).
constexpr int kBorderFifths = 2;

enum ButtonIndex : size_t { kMasterMute, kEffectsMute, kMusicMute, kOn, kOff, kStereo, kMono, kFaster, kBetter };

image::Image cachePicture(const Archive &data, const std::string &name) {
    std::vector<uint8_t> bytes;
    Frame frame;
    if (data.read("Cache/" + name + ".frm16", bytes) || readFrame(bytes.data(), bytes.size(), frame))
        return {};
    return frameImage(frame);
}

} // namespace

SoundOptionsWindow::SoundOptionsWindow(const Archive &data, GameOptions &options, int screenW, int screenH)
    : _options(options), _background(screenPicture(data, "Sound Options Dialog/Background")),
      _sliders{Slider(&_font, kSliderMaximum, cachePicture(data, "Interface/Window Texture")),
               Slider(&_font, kSliderMaximum, cachePicture(data, "Interface/Window Texture")),
               Slider(&_font, kSliderMaximum, cachePicture(data, "Interface/Window Texture"))},
      _continue(screenPicture(data, "Sound Options Dialog/Button Background"), "&CONTINUE", &_buttonFont),
      _x((screenW - _background.w) / 2), _y((screenH - _background.h) / 2) {
    // The fonts the program names (0x56e966, 0x56ea39, 0x56f1d6).
    (void)_font.load(data, "Trek Small");
    (void)_pairFont.load(data, "Small Blue");
    (void)_buttonFont.load(data, "Placar Condensed");
    const int border = _font.loaded() ? kBorderFifths * (_font.baseline() / 5) : 0;
    const int labelH = (_font.loaded() ? _font.baseline() : 0) + 2 * border + 2;
    auto labelWidth = [&](const char *text) { return (_font.loaded() ? _font.width(text) : 0) + 2 * border; };

    // Six rows: the first label at 100 across, in the middle of the
    // window for rows of its height and 8 (0x56eaa3); each next one under
    // the one before, lined up on its right (layout 9).
    _labelY0 = (_background.h - 6 * (labelH + 8)) / 2;
    const int labelsRight = kFirstLabelX + labelWidth(kLabels[0]);
    for (size_t i = 0; i < kLabels.size(); ++i) {
        _labels[i] = kLabels[i];
        _labelX[i] = labelsRight - labelWidth(kLabels[i]);
    }
    auto rowY = [&](size_t row) { return _labelY0 + int(row) * (labelH + kSpacing); };
    // Each volume's mute button and slider after its label (layout 14).
    const auto sound = gameSound(data, "UI/Button Action.wav");
    int sliderRight = 0;
    for (size_t i = 0; i < 3; ++i) {
        Button mute(data, "Sound Options Dialog", "Mute", labelsRight + kSpacing, rowY(i));
        const int sliderX = mute.x() + mute.width() + kSpacing;
        _buttons.add(std::move(mute));
        _sliders[i].setWidth(kSliderWidth);
        _sliders[i].setPosition(sliderX, rowY(i));
        sliderRight = sliderX + kSliderWidth;
    }
    // The paired buttons: the second at the sliders' right, the first 4
    // before it, their labels centred on them (0x56f2b2).
    for (size_t row = 3; row < 6; ++row) {
        const image::Image picture = screenPicture(data, "Sound Options Dialog/Button Default");
        const int secondX = sliderRight - picture.w;
        Button first(data, "Sound Options Dialog", "Button", secondX - picture.w - kPairGap, rowY(row));
        Button second(data, "Sound Options Dialog", "Button", secondX, rowY(row));
        const int textY = (picture.h - (_pairFont.loaded() ? _pairFont.height() : 0)) / 2;
        first.setLabel(kPairLabels[(row - 3) * 2], &_pairFont, 0, textY);
        second.setLabel(kPairLabels[(row - 3) * 2 + 1], &_pairFont, 0, textY);
        first.setSound(sound);
        second.setSound(sound);
        _buttons.add(std::move(first));
        _buttons.add(std::move(second));
    }
    _buttons.setOrigin(_x, _y);
    _continue.setPosition(kButtonX, _background.h - _continue.height() - kContinueBottomGap);
    _continue.setSound(sound);
    showOptions();
}

void SoundOptionsWindow::showOptions() {
    _sliders[0].setValue(_options.masterVolume + kSliderMaximum);
    _sliders[1].setValue(int((_options.effectsVolume - 0.5f) * kEffectsScale));
    _sliders[2].setValue(_options.musicVolume + kSliderMaximum);
    const std::array<bool, 3> muted = {_options.masterMute, _options.effectsMute, _options.musicMute};
    for (size_t i = 0; i < 3; ++i) {
        _buttons.button(i).setChecked(muted[i]);
        _sliders[i].setEnabled(!muted[i]);
    }
    _buttons.button(kOn).setChecked(_options.playVoiceovers);
    _buttons.button(kOff).setChecked(!_options.playVoiceovers);
    _buttons.button(kStereo).setChecked(_options.stereo);
    _buttons.button(kMono).setChecked(!_options.stereo);
    _buttons.button(kFaster).setChecked(_options.mixingChannels != GameOptions::kMixingBetter);
    _buttons.button(kBetter).setChecked(_options.mixingChannels == GameOptions::kMixingBetter);
}

bool SoundOptionsWindow::handleEvent(const Event &ev, audio::Mixer *mixer) {
    // CONTINUE is the window's default and cancel button (0x486ad0).
    if ((isEnter(ev) || isEscape(ev)) && _continue.press(mixer))
        return true;
    if (_continue.handleEvent(ev, _x, _y, mixer))
        return true;
    if (ev.type == EventType::kMouseDown)
        for (Slider &slider : _sliders)
            slider.setFocused(false);
    bool changed = false;
    if (_sliders[0].handleEvent(ev, _x, _y)) {
        _options.masterVolume = _sliders[0].value() - kSliderMaximum;
        changed = true;
    }
    if (_sliders[1].handleEvent(ev, _x, _y)) {
        // 0x56f9cd
        _options.effectsVolume = (float(_sliders[1].value()) / float(kSliderMaximum) + 1.0f) * 0.5f;
        changed = true;
    }
    if (_sliders[2].handleEvent(ev, _x, _y)) {
        _options.musicVolume = _sliders[2].value() - kSliderMaximum;
        changed = true;
    }
    // A mute button turns its volume off or back on, and its slider with it
    // (0x56f733); the paired buttons set their option (0x56f572...).
    const int chosen = _buttons.handleEvent(ev, mixer);
    switch (chosen) {
    case kMasterMute:
        _options.masterMute = !_options.masterMute;
        break;
    case kEffectsMute:
        _options.effectsMute = !_options.effectsMute;
        break;
    case kMusicMute:
        _options.musicMute = !_options.musicMute;
        break;
    case kOn:
        _options.playVoiceovers = true;
        break;
    case kOff:
        _options.playVoiceovers = false;
        break;
    case kStereo:
        _options.stereo = true;
        break;
    case kMono:
        _options.stereo = false;
        break;
    case kFaster:
        _options.mixingChannels = GameOptions::kMixingFaster;
        break;
    case kBetter:
        _options.mixingChannels = GameOptions::kMixingBetter;
        break;
    default:
        break;
    }
    changed = changed || chosen >= 0;
    if (changed) {
        showOptions();
        if (mixer)
            _options.applySound(*mixer);
        _options.save();
    }
    return false;
}

void SoundOptionsWindow::draw(Surface &screen, uint64_t now) {
    image::blit(screen, _background, _x, _y);
    const int border = _font.loaded() ? kBorderFifths * (_font.baseline() / 5) : 0;
    const int labelH = (_font.loaded() ? _font.baseline() : 0) + 2 * border + 2;
    for (size_t i = 0; i < _labels.size(); ++i)
        if (_font.loaded())
            // A label's text at its border across and a pixel more down (0x4e66f2).
            _font.draw(screen, _x + _labelX[i] + border, _y + _labelY0 + int(i) * (labelH + kSpacing) + border + 1,
                       _labels[i]);
    for (const Slider &slider : _sliders)
        slider.draw(screen, _x, _y);
    _buttons.draw(screen);
    _continue.draw(screen, _x, _y, now);
}

} // namespace reluva::stawayteam
