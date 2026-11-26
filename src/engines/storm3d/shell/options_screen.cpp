// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/options_screen.h"

namespace reluva::storm3d {

namespace {

// Where the game's frame has them (the pictures matched against it).
constexpr int kSettingsX = 158;
constexpr int kSettingsWidth = 324;
constexpr int kSettingsHeight = 32;
constexpr int kTitleX = 65;
constexpr int kTitleY = 6;
// Arial Bold at sizes the game asks GDI for, fitted to the frame's text:
// 16 pixels on the buttons, 20 for the title.
constexpr float kButtonText = 16;
constexpr float kTitleText = 20;
constexpr Color kButtonColour{0, 0, 0};
constexpr Color kTitleColour{255, 255, 255};

} // namespace

const std::vector<OptionsScreen::Button> &OptionsScreen::buttons(bool inGame) {
    static const std::vector<Button> kButtons = {
        {OptionsChoice::kGraphics, "graphics", 192},
        {OptionsChoice::kSound, "sound", 232},
        {OptionsChoice::kGame, "game", 272},
    };
    // A skirmish's, as the original's frame has them, 40 apart.
    static const std::vector<Button> kInGame = {
        {OptionsChoice::kSurrender, "surrender", 152}, {OptionsChoice::kGraphics, "graphics", 192},
        {OptionsChoice::kSound, "sound", 232},         {OptionsChoice::kGame, "game", 272},
        {OptionsChoice::kAbort, "abort", 312},         {OptionsChoice::kReturn, "return", 352},
    };
    return inGame ? kInGame : kButtons;
}

OptionsChoice OptionsScreen::choiceAt(int x, int y, bool inGame) {
    for (const Button &b : buttons(inGame))
        if (x >= kSettingsX && y >= b.y && x < kSettingsX + kSettingsWidth && y < b.y + kSettingsHeight)
            return b.choice;
    if (!inGame && NavigationBar::onPrevious(x, y))
        return OptionsChoice::kPreviousMenu;
    return OptionsChoice::kNone;
}

Error OptionsScreen::load(const GameFiles &files, bool inGame) {
    _inGame = inGame;
    _settings.assign(buttons(inGame).size(), ShellButton{});
    if (Error err = loadPicture(files, "bitmaps/wonlogin/wlsBG.bmp", _background))
        return err;
    if (Error err = _bar.load(files))
        return err;
    for (size_t i = 0; i < _settings.size(); ++i)
        if (Error err = _settings[i].load(files, "bitmaps/wonlogin", "wls324tg", kSettingsX, buttons(inGame)[i].y))
            return err;
    std::vector<uint8_t> data;
    if (Error err = files.read("label.map", data))
        return err;
    if (Error err = _text.parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size())))
        return Error::fail("label.map: " + err.message);
    _font.load();
    _hover = _pressed = OptionsChoice::kNone;
    return Error::success();
}

bool OptionsScreen::pointerAt(int x, int y) {
    const OptionsChoice now = choiceAt(x, y, _inGame);
    if (now == _hover)
        return false;
    _hover = now;
    return now != OptionsChoice::kNone;
}

void OptionsScreen::press(int x, int y) {
    _pressed = choiceAt(x, y, _inGame);
}

OptionsChoice OptionsScreen::release(int x, int y) {
    const OptionsChoice pressed = _pressed;
    _pressed = OptionsChoice::kNone;
    const OptionsChoice at = choiceAt(x, y, _inGame);
    return at == pressed ? at : OptionsChoice::kNone;
}

void OptionsScreen::draw(Surface &screen) {
    drawPicture(screen, _background, 0, 0, false);
    for (size_t i = 0; i < _settings.size(); ++i) {
        const ShellButton &b = _settings[i];
        const OptionsChoice choice = buttons(_inGame)[i].choice;
        b.draw(screen, _hover == choice, _pressed == choice, true);
        _font.drawCentred(screen, b.x, b.y, b.looks[0].w, b.looks[0].h,
                          _text.text("escape_menu", buttons(_inGame)[i].key), kButtonText, kButtonColour);
    }
    // The escape menu has no Previous Menu of its own: the words are the
    // ones every other screen's button has. A game's has none.
    _bar.draw(screen, _font, _inGame ? std::string() : _text.text("multiplayer_connection", "previous_menu"),
              _hover == OptionsChoice::kPreviousMenu, _pressed == OptionsChoice::kPreviousMenu);
    _font.draw(screen, kTitleX, kTitleY, _text.text("escape_menu", "title"), kTitleText, kTitleColour);
}

} // namespace reluva::storm3d
