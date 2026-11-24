// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/settings_screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
// long.
constexpr int kLineX = 27;
constexpr int kLineY = 10;
constexpr int kLineMax = 170;
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
    using P = Preferences;
    const std::vector<Choice> onOff = {{"off", 0}, {"on", 1}};
    auto toggle = [&onOff](int x, int y, P::Line line, size_t field) {
        return C{C::kSwitch, x, y, line, field, onOff, 0, 0, 0, 0, false};
    };
    // Ranges, steps and lengths from the preferences the game wrote and
    // its frames after clicks: a volume 17 pixels a unit, game speed 28,
    // detail 42, mouse scroll 80 (it keeps tenths), keyboard scroll 28 for 6.
    auto slider = [](int x, int y, P::Line line, size_t field, double min, double max, double step, double pixels) {
        return C{C::kSlider, x, y, line, field, {}, min, max, step, pixels, false};
    };
    auto list = [](int x, int y, P::Line line) { return C{C::kList, x, y, line, 0, {}, 0, 0, 0, 0, true}; };
    std::vector<Choice> channels;
    static const char *const kDigits[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
    for (int i = 0; i < 8; ++i)
        channels.push_back({kDigits[i], double(i + 1)});
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
             // What the lists offer is the machine's own; Reluva's screen is 640 x 480.
             list(20, 116, P::kGraphics),
             list(20, 166, P::kDriver),
             toggle(386, 116, P::kGraphics, 10),
             // A click on it changed nothing in the frames or the file.
             C{C::kSwitch, 386, 166, P::kGraphics, 9, onOff, 0, 0, 0, 0, true},
             slider(338, 216, P::kGraphics, 5, 0, 4, 1, 42),
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
             slider(208, 80, P::kSound, 0, 0, 10, 1, 17),
             slider(208, 130, P::kSound, 1, 0, 10, 1, 17),
             slider(208, 180, P::kSound, 2, 0, 10, 1, 17),
             C{C::kSwitch, 340, 214, P::kSound, 3, channels, 0, 0, 0, 0, false},
             toggle(340, 254, P::kSound, 4),
             toggle(340, 294, P::kSound, 5),
             toggle(340, 334, P::kSound, 6),
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
             C{C::kSwitch, 340, 64, P::kGame, 0, {{"easy", 1}, {"medium", 2}, {"hard", 3}}, 0, 0, 0, 0, false},
             slider(208, 130, P::kGame, 1, 1, 6, 1, 28),
             slider(208, 180, P::kGame, 2, 0, 2, 0.1, 80),
             slider(208, 230, P::kGame, 3, 0, 36, 4, 28.0 / 6),
             C{C::kSwitch, 340, 264, P::kGame, 4, {{"right_click", 1}, {"left_click", 0}}, 0, 0, 0, 0, false},
             toggle(340, 314, P::kGame, 5),
             toggle(340, 364, P::kGame, 6),
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

double SettingsScreen::sliderAfterClick(const Control &slider, double value, int x) {
    double v = value;
    if (x < kEndWidth)
        v -= slider.step;
    else if (x >= kSizes[Control::kSlider][0] - kEndWidth)
        v += slider.step;
    // Whole tenths, as the file keeps them, not a sum's rounding.
    v = std::round(v * 1000) / 1000;
    return std::clamp(v, slider.min, slider.max);
}

int SettingsScreen::lineOf(const Control &slider, double value) {
    return std::clamp(int(value * slider.pixels + 1e-6), 0, kLineMax);
}

Error SettingsScreen::load(const GameFiles &files, Preferences *preferences) {
    _preferences = preferences;
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
    if (c.fixed || !_preferences)
        return false;
    const double v = _preferences->number(c.line, c.field);
    if (c.kind == Control::kSlider) {
        _preferences->setNumber(c.line, c.field, sliderAfterClick(c, v, x - c.x));
    } else {
        // The next choice after the one kept; the first if it's none of them.
        size_t i = 0;
        while (i < c.choices.size() && c.choices[i].stored != v)
            ++i;
        _preferences->setNumber(c.line, c.field, c.choices[(i + 1) % c.choices.size()].stored);
    }
    return false;
}

std::string SettingsScreen::textOf(const char *key) const {
    return _text.text(layoutOf(_page).section, key, key);
}

std::string SettingsScreen::shown(const Control &c) const {
    if (!_preferences)
        return {};
    if (c.kind == Control::kList) {
        if (c.line == Preferences::kDriver)
            return _preferences->line(Preferences::kDriver);
        // The mode: width, height and depth, the graphics line's 7th to 9th.
        char mode[40];
        std::snprintf(mode, sizeof mode, "%dx%dx%d", int(_preferences->number(c.line, 6)),
                      int(_preferences->number(c.line, 7)), int(_preferences->number(c.line, 8)));
        return mode;
    }
    const double v = _preferences->number(c.line, c.field);
    for (const Choice &choice : c.choices)
        if (choice.stored == v)
            return textOf(choice.key);
    return textOf(c.choices.front().key);
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
        if (c.kind == Control::kSlider) {
            drawPicture(screen, _slider, c.x, c.y, true);
            const int length = _preferences ? lineOf(c, _preferences->number(c.line, c.field)) : 0;
            uint8_t *row = screen.row(c.y + kLineY);
            for (int x = c.x + kLineX; x < c.x + kLineX + length && x < screen.w; ++x) {
                uint8_t *p = row + size_t(x) * 4;
                p[0] = kLineColour[0], p[1] = kLineColour[1], p[2] = kLineColour[2], p[3] = 255;
            }
            continue;
        }
        const std::array<image::Image, 3> &looks = c.kind == Control::kSwitch ? _switch : _list;
        const image::Image &look = _pressed == int(i) ? looks[2] : _hover == int(i) ? looks[1] : looks[0];
        drawPicture(screen, look, c.x, c.y, true);
        _font.drawCentred(screen, c.x, c.y, look.w, look.h, shown(c), c.kind == Control::kList ? kListText : kText,
                          kTextColour);
    }
    _bar.draw(screen, _font, textOf(layout.previous), _hover == kPrevious, _pressed == kPrevious);
}

} // namespace reluva::storm3d
