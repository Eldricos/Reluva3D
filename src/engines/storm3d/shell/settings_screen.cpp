// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/settings_screen.h"

#include <algorithm>

namespace reluva::storm3d {

namespace {

// Where the game's frames have them (the pictures matched against them).
constexpr int kBottomY = 445;
constexpr int kPreviousX = 5;
constexpr int kPreviousWidth = 112;
constexpr int kBottomHeight = 30;
constexpr int kStartX = 123;
constexpr int kMainX = 522;
constexpr int kTitleY = 6;
// A slider's line: blue, a pixel high, 10 down and 27 in, at most 170
// long, a click on − or + moving it 17 (the frames before and after one).
constexpr int kLineX = 27;
constexpr int kLineY = 10;
constexpr int kLineMax = 170;
constexpr int kLineStep = 17;
constexpr uint8_t kLineColour[3] = {0, 0, 255};
// − and + are the slider's ends, each 27 across.
constexpr int kEndWidth = 27;
// Arial Bold as the game asks GDI for it, fitted to the frames' text.
constexpr float kText = 16;
constexpr float kListText = 12;
constexpr float kTitleText = 20;
constexpr Color kTextColour{0, 0, 0};
constexpr Color kLabelColour{255, 190, 0};
constexpr Color kTitleColour{255, 255, 255};
constexpr Color kPreviousColour{0, 0, 0};

bool inside(int x, int y, int rx, int ry, int w, int h) {
    return x >= rx && y >= ry && x < rx + w && y < ry + h;
}

// The controls' sizes: a switch, a slider, a list.
constexpr int kSizes[3][2] = {{126, 30}, {224, 20}, {324, 32}};

} // namespace

Error NavigationBar::load(const GameFiles &files) {
    if (Error err = loadPicture(files, "bitmaps/multiset/mpsstart1.bmp", _start))
        return err;
    if (Error err = loadPicture(files, "bitmaps/multiset/navmain1.bmp", _main))
        return err;
    return _previous.load(files, "bitmaps/multiset", "navback", kPreviousX, kBottomY);
}

bool NavigationBar::onPrevious(int x, int y) {
    return inside(x, y, kPreviousX, kBottomY, kPreviousWidth, kBottomHeight);
}

void NavigationBar::draw(Surface &screen, ShellFont &font, const std::string &previous, bool lit, bool pressed) const {
    drawPicture(screen, _start, kStartX, kBottomY, true);
    drawPicture(screen, _main, kMainX, kBottomY, true);
    _previous.draw(screen, lit, pressed, true);
    font.drawCentred(screen, _previous.x, _previous.y, _previous.looks[0].w, _previous.looks[0].h, previous, kText,
                     kPreviousColour);
}

const SettingsScreen::Layout &SettingsScreen::layoutOf(SettingsPage page) {
    using C = Control;
    // Labels centred over their controls, or in a column to the left of
    // the switches; their lines' tops 3 above their capitals' (4 at 20
    // pixels), as the frames have them.
    static const std::array<Layout, 3> kLayouts = {{
        {"graphics_settings",
         "previous",
         59,
         {
             {"display_settings", 20, 340, 62, kTitleText},
             {"performance_settings", 386, 128, 62, kTitleText},
             {"display_mode", 20, 340, 100, kText},
             {"aliased_fonts", 386, 126, 100, kText},
             {"display_driver", 20, 340, 150, kText},
             {"cinematic_window", 386, 128, 150, kText},
             {"ship_detail", 338, 224, 200, kText},
             {"unsupported", 20, 580, 380, kText},
         },
         {
             // What the program lists is the machine's own; Reluva's screen is 640 x 480.
             {C::kList, 20, 116, {"640x480x16"}, 0},
             {C::kList, 20, 166, {"Reluva"}, 0},
             {C::kSwitch, 386, 116, {"off", "on"}, 0},
             {C::kSwitch, 386, 166, {"off", "on"}, 1},
             {C::kSlider, 338, 216, {}, 168},
         }},
        {"sound_settings",
         "previous_menu",
         65,
         {
             {"music_volume", 208, 224, 64, kText},
             {"effects_volume", 208, 224, 114, kText},
             {"voice_volume", 208, 224, 164, kText},
             {"sound_channels", 100, 230, 220, kText},
             {"stereo_sound", 100, 230, 260, kText},
             {"unit_speech", 100, 230, 300, kText},
             {"alert_audio", 100, 230, 340, kText},
         },
         {
             {C::kSlider, 208, 80, {}, kLineMax},
             {C::kSlider, 208, 130, {}, kLineMax},
             {C::kSlider, 208, 180, {}, kLineMax},
             // The frame shows 8; the others are guessed.
             {C::kSwitch, 340, 214, {"4", "8", "16"}, 1},
             {C::kSwitch, 340, 254, {"off", "on"}, 1},
             {C::kSwitch, 340, 294, {"off", "on"}, 1},
             {C::kSwitch, 340, 334, {"off", "on"}, 1},
         }},
        {"game_settings",
         "previous",
         65,
         {
             {"game_difficulty", 100, 230, 70, kText},
             {"game_speed", 208, 224, 114, kText},
             {"mouse_scroll_speed", 208, 224, 164, kText},
             {"keyboard_scroll_speed", 208, 224, 214, kText},
             {"mouse_button_interface", 100, 230, 270, kText},
             {"mouse_camera_control", 100, 230, 320, kText},
             {"directors_cut", 100, 230, 370, kText},
         },
         {
             {C::kSwitch, 340, 64, {"easy", "medium", "hard"}, 1},
             {C::kSlider, 208, 130, {}, 84},
             {C::kSlider, 208, 180, {}, 80},
             {C::kSlider, 208, 230, {}, 28},
             {C::kSwitch, 340, 264, {"right_click", "left_click"}, 0},
             {C::kSwitch, 340, 314, {"off", "on"}, 1},
             {C::kSwitch, 340, 364, {"off", "on"}, 0},
         }},
    }};
    return kLayouts[size_t(page)];
}

int SettingsScreen::controlAt(SettingsPage page, int x, int y) {
    const std::vector<Control> &controls = layoutOf(page).controls;
    for (size_t i = 0; i < controls.size(); ++i) {
        const Control &c = controls[i];
        if (inside(x, y, c.x, c.y, kSizes[c.kind][0], kSizes[c.kind][1]))
            return int(i);
    }
    return -1;
}

int SettingsScreen::sliderAfterClick(int line, int x) {
    if (x < kEndWidth)
        return std::max(0, line - kLineStep);
    if (x >= kSizes[Control::kSlider][0] - kEndWidth)
        return std::min(kLineMax, line + kLineStep);
    return line;
}

Error SettingsScreen::load(const GameFiles &files) {
    if (Error err = loadPicture(files, "bitmaps/credit/creditsBG.bmp", _background))
        return err;
    if (Error err = _bar.load(files))
        return err;
    if (Error err = loadPicture(files, "bitmaps/holder/scrollbar.bmp", _slider))
        return err;
    for (size_t i = 0; i < 3; ++i) {
        const std::string n = std::to_string(i + 1) + ".bmp";
        if (Error err = loadPicture(files, "bitmaps/multiset/nav3but" + n, _switch[i]))
            return err;
        if (Error err = loadPicture(files, "bitmaps/wonlogin/wls324pd" + n, _list[i]))
            return err;
    }
    std::vector<uint8_t> data;
    if (Error err = files.read("label.map", data))
        return err;
    if (Error err = _text.parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size())))
        return Error::fail("label.map: " + err.message);
    _font.load();
    for (size_t p = 0; p < _values.size(); ++p)
        if (_values[p].empty())
            for (const Control &c : layoutOf(SettingsPage(p)).controls)
                _values[p].push_back(c.initial);
    return Error::success();
}

void SettingsScreen::open(SettingsPage page) {
    _page = page;
    _hover = _pressed = -1;
}

int SettingsScreen::targetAt(int x, int y) const {
    if (NavigationBar::onPrevious(x, y))
        return kPrevious;
    return controlAt(_page, x, y);
}

bool SettingsScreen::pointerAt(int x, int y) {
    const int now = targetAt(x, y);
    if (now == _hover)
        return false;
    _hover = now;
    return now != -1;
}

void SettingsScreen::press(int x, int y) {
    _pressed = targetAt(x, y);
}

bool SettingsScreen::release(int x, int y) {
    const int pressed = _pressed;
    _pressed = -1;
    const int at = targetAt(x, y);
    if (at != pressed || at == -1)
        return false;
    if (at == kPrevious)
        return true;
    const Control &c = layoutOf(_page).controls[size_t(at)];
    int &v = _values[size_t(_page)][size_t(at)];
    if (c.kind == Control::kSlider)
        v = sliderAfterClick(v, x - c.x);
    else
        v = (v + 1) % int(c.values.size());
    return false;
}

std::string SettingsScreen::textOf(const char *key) const {
    return _text.text(layoutOf(_page).section, key, key);
}

void SettingsScreen::draw(Surface &screen) {
    const Layout &layout = layoutOf(_page);
    drawPicture(screen, _background, 0, 0, false);
    _font.draw(screen, layout.titleX, kTitleY, textOf("title"), kTitleText, kTitleColour);
    for (const Label &l : layout.labels) {
        const std::string words = textOf(l.key);
        _font.draw(screen, l.x + (l.w - _font.width(words, l.pixels)) / 2, l.top, words, l.pixels, kLabelColour);
    }
    for (size_t i = 0; i < layout.controls.size(); ++i) {
        const Control &c = layout.controls[i];
        const int v = _values[size_t(_page)][i];
        if (c.kind == Control::kSlider) {
            drawPicture(screen, _slider, c.x, c.y, true);
            uint8_t *row = screen.row(c.y + kLineY);
            for (int x = c.x + kLineX; x < c.x + kLineX + v && x < screen.w; ++x) {
                uint8_t *p = row + size_t(x) * 4;
                p[0] = kLineColour[0], p[1] = kLineColour[1], p[2] = kLineColour[2], p[3] = 255;
            }
            continue;
        }
        const std::array<image::Image, 3> &looks = c.kind == Control::kSwitch ? _switch : _list;
        const image::Image &look = _pressed == int(i) ? looks[2] : _hover == int(i) ? looks[1] : looks[0];
        drawPicture(screen, look, c.x, c.y, true);
        _font.drawCentred(screen, c.x, c.y, look.w, look.h, textOf(c.values[size_t(v)]),
                          c.kind == Control::kList ? kListText : kText, kTextColour);
    }
    _bar.draw(screen, _font, textOf(layout.previous), _hover == kPrevious, _pressed == kPrevious);
}

} // namespace reluva::storm3d
