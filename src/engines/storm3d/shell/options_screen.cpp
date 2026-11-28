// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/options_screen.h"

#include <string>
#include <utility>

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
// Abort Mission's box, as the original's frame has it: the picture at
// (140, 150), Yes (Previous Menu's plate) and No (the right plate's)
// along its foot, its words label.map's abort_mission_popup in the
// debriefing's gold, the tops of their lines 3 over their capitals.
constexpr int kBoxX = 140;
constexpr int kBoxY = 150;
constexpr int kBoxWidth = 359;
constexpr int kYesX = 163;
constexpr int kNoX = 357;
constexpr int kAskButtonY = 250;
constexpr int kWarningX = 232;
constexpr int kWarningY = 151;
constexpr int kMessageY = 195;
constexpr Color kAskColour{255, 190, 0};
constexpr float kAskButtonText = 12; // Yes and No smaller than the menu's words

} // namespace

const std::vector<OptionsScreen::Button> &OptionsScreen::buttons(OptionsMenu menu) {
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
    // A campaign mission's: Restate Objectives, Save Game and Load Game
    // above, no Surrender (0x5374fc-0x537880); Save Game left out during a
    // cut scene.
    static const std::vector<Button> kCampaign = {
        {OptionsChoice::kRestate, "restate", 72},     {OptionsChoice::kSaveGame, "save_game", 112},
        {OptionsChoice::kLoadGame, "load_game", 152}, {OptionsChoice::kGraphics, "graphics", 192},
        {OptionsChoice::kSound, "sound", 232},        {OptionsChoice::kGame, "game", 272},
        {OptionsChoice::kAbort, "abort", 312},        {OptionsChoice::kReturn, "return", 352},
    };
    static const std::vector<Button> kCampaignCutScene = [] {
        std::vector<Button> b = kCampaign;
        std::erase_if(b, [](const Button &x) { return x.choice == OptionsChoice::kSaveGame; });
        return b;
    }();
    switch (menu) {
    case OptionsMenu::kSkirmish:
        return kInGame;
    case OptionsMenu::kCampaign:
        return kCampaign;
    case OptionsMenu::kCampaignCutScene:
        return kCampaignCutScene;
    case OptionsMenu::kFrontEnd:
        break;
    }
    return kButtons;
}

OptionsChoice OptionsScreen::choiceAt(int x, int y, OptionsMenu menu) {
    const bool inGame = menu != OptionsMenu::kFrontEnd;
    for (const Button &b : buttons(menu))
        if (x >= kSettingsX && y >= b.y && x < kSettingsX + kSettingsWidth && y < b.y + kSettingsHeight)
            return b.choice;
    if (!inGame && NavigationBar::onPrevious(x, y))
        return OptionsChoice::kPreviousMenu;
    return OptionsChoice::kNone;
}

Error OptionsScreen::load(const GameFiles &files, OptionsMenu menu) {
    _menu = menu;
    _inGame = menu != OptionsMenu::kFrontEnd;
    const bool inGame = _inGame;
    _settings.assign(buttons(menu).size(), ShellButton{});
    if (Error err = loadPicture(files, "bitmaps/wonlogin/wlsBG.bmp", _background))
        return err;
    if (Error err = _bar.load(files))
        return err;
    for (size_t i = 0; i < _settings.size(); ++i)
        if (Error err = _settings[i].load(files, "bitmaps/wonlogin", "wls324tg", kSettingsX, buttons(menu)[i].y))
            return err;
    std::vector<uint8_t> data;
    if (Error err = files.read("label.map", data))
        return err;
    if (Error err = _text.parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size())))
        return Error::fail("label.map: " + err.message);
    if (inGame) {
        if (Error err = loadPicture(files, "bitmaps/misc/DBsmall2BG.bmp", _box))
            return err;
        if (Error err = _yes.load(files, "bitmaps/multiset", "navback", kYesX, kAskButtonY))
            return err;
        if (Error err = _no.load(files, "bitmaps/multiset", "navmain", kNoX, kAskButtonY))
            return err;
    }
    _font.load();
    _hover = _pressed = OptionsChoice::kNone;
    _asking = false;
    _askHover = _askPressed = 0;
    return Error::success();
}

int OptionsScreen::askAt(int x, int y) const {
    return _yes.contains(x, y) ? 1 : _no.contains(x, y) ? 2 : 0;
}

bool OptionsScreen::pointerAt(int x, int y) {
    if (_asking) {
        const int now = askAt(x, y);
        if (now == _askHover)
            return false;
        _askHover = now;
        return now != 0;
    }
    const OptionsChoice now = choiceAt(x, y, _menu);
    if (now == _hover)
        return false;
    _hover = now;
    return now != OptionsChoice::kNone;
}

void OptionsScreen::press(int x, int y) {
    if (_asking)
        _askPressed = askAt(x, y);
    else
        _pressed = choiceAt(x, y, _menu);
}

OptionsChoice OptionsScreen::release(int x, int y) {
    if (_asking) {
        // Yes aborts; No puts the box away, back to the menu.
        const int pressed = std::exchange(_askPressed, 0);
        const int at = askAt(x, y);
        if (at != pressed || at == 0)
            return OptionsChoice::kNone;
        _asking = false;
        _askHover = 0;
        return at == 1 ? OptionsChoice::kAbort : OptionsChoice::kNone;
    }
    const OptionsChoice pressed = _pressed;
    _pressed = OptionsChoice::kNone;
    const OptionsChoice at = choiceAt(x, y, _menu);
    if (at != pressed)
        return OptionsChoice::kNone;
    if (at == OptionsChoice::kAbort) {
        _asking = true;
        return OptionsChoice::kNone;
    }
    return at;
}

void OptionsScreen::draw(Surface &screen) {
    drawPicture(screen, _background, 0, 0, false);
    for (size_t i = 0; i < _settings.size(); ++i) {
        const ShellButton &b = _settings[i];
        const OptionsChoice choice = buttons(_menu)[i].choice;
        // Abort Mission stays pressed while its box asks.
        b.draw(screen, _hover == choice, _pressed == choice || (_asking && choice == OptionsChoice::kAbort), true);
        _font.drawCentred(screen, b.x, b.y, b.looks[0].w, b.looks[0].h,
                          _text.text("escape_menu", buttons(_menu)[i].key), kButtonText, kButtonColour);
    }
    // The escape menu has no Previous Menu of its own: the words are the
    // ones every other screen's button has. A game's has none.
    _bar.draw(screen, _font, _inGame ? std::string() : _text.text("multiplayer_connection", "previous_menu"),
              _hover == OptionsChoice::kPreviousMenu, _pressed == OptionsChoice::kPreviousMenu);
    _font.draw(screen, kTitleX, kTitleY, _text.text("escape_menu", "title"), kTitleText, kTitleColour);
    if (!_asking)
        return;
    static constexpr const char *kSection = "abort_mission_popup";
    drawPicture(screen, _box, kBoxX, kBoxY, false);
    _font.draw(screen, kWarningX, kWarningY, _text.text(kSection, "warning"), kButtonText, kAskColour);
    const std::string message = _text.text(kSection, "message");
    _font.draw(screen, kBoxX + (kBoxWidth - _font.width(message, kButtonText)) / 2, kMessageY, message, kButtonText,
               kAskColour);
    _yes.draw(screen, _askHover == 1, _askPressed == 1, true);
    _no.draw(screen, _askHover == 2, _askPressed == 2, true);
    _font.drawCentred(screen, _yes.x, _yes.y, _yes.looks[0].w, _yes.looks[0].h, _text.text(kSection, "ok_button"),
                      kAskButtonText, kButtonColour);
    _font.drawCentred(screen, _no.x, _no.y, _no.looks[0].w, _no.looks[0].h, _text.text(kSection, "cancel_button"),
                      kAskButtonText, kButtonColour);
}

} // namespace reluva::storm3d
