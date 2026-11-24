// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/multiplayer_screen.h"

namespace reluva::storm3d {

namespace {

// Where the game's frame has them (the pictures matched against it).
constexpr int kButtonX = 158;
constexpr int kButtonWidth = 324;
constexpr int kButtonHeight = 32;
constexpr int kBoxX = 158;
constexpr int kBoxY = 96;
constexpr int kLabelX = 158;
constexpr int kLabelY = 78;
constexpr int kTitleX = 64;
constexpr int kTitleY = 6;
// The name's place in its box: the frame's is empty, so a guess.
constexpr int kNameInset = 8;
constexpr float kText = 16;
constexpr float kTitleText = 20;
constexpr Color kButtonColour{0, 0, 0};
constexpr Color kWhite{255, 255, 255};
constexpr const char *kSection = "multiplayer_connection";

} // namespace

const std::vector<MultiplayerScreen::Button> &MultiplayerScreen::buttons() {
    static const std::vector<Button> kButtons = {
        {ConnectionChoice::kInternet, "internet", 162},
        {ConnectionChoice::kLan, "lan_tcpip", 228},
        {ConnectionChoice::kInstantAction, "instant_action", 294},
    };
    return kButtons;
}

ConnectionChoice MultiplayerScreen::choiceAt(int x, int y) {
    for (const Button &b : buttons())
        if (x >= kButtonX && y >= b.y && x < kButtonX + kButtonWidth && y < b.y + kButtonHeight)
            return b.choice;
    return NavigationBar::onPrevious(x, y) ? ConnectionChoice::kPreviousMenu : ConnectionChoice::kNone;
}

std::string MultiplayerScreen::typed(std::string_view name, std::string_view text) {
    std::string out(name);
    for (char c : text)
        if (c >= ' ' && c <= '~' && out.size() < kNameLength)
            out.push_back(c);
    return out;
}

Error MultiplayerScreen::load(const GameFiles &files) {
    if (Error err = loadPicture(files, "bitmaps/wonlogin/wlsBG.bmp", _background))
        return err;
    if (Error err = loadPicture(files, "bitmaps/wonlogin/wlstext.bmp", _box))
        return err;
    for (size_t i = 0; i < _ways.size(); ++i)
        if (Error err = _ways[i].load(files, "bitmaps/wonlogin", "wls324tg", kButtonX, buttons()[i].y))
            return err;
    if (Error err = _bar.load(files))
        return err;
    std::vector<uint8_t> data;
    if (Error err = files.read("label.map", data))
        return err;
    if (Error err = _text.parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size())))
        return Error::fail("label.map: " + err.message);
    _font.load();
    _hover = _pressed = ConnectionChoice::kNone;
    return Error::success();
}

bool MultiplayerScreen::pointerAt(int x, int y) {
    const ConnectionChoice now = choiceAt(x, y);
    if (now == _hover)
        return false;
    _hover = now;
    return now != ConnectionChoice::kNone;
}

void MultiplayerScreen::press(int x, int y) {
    _pressed = choiceAt(x, y);
}

ConnectionChoice MultiplayerScreen::release(int x, int y) {
    const ConnectionChoice pressed = _pressed;
    _pressed = ConnectionChoice::kNone;
    const ConnectionChoice at = choiceAt(x, y);
    return at == pressed ? at : ConnectionChoice::kNone;
}

void MultiplayerScreen::draw(Surface &screen) {
    drawPicture(screen, _background, 0, 0, false);
    _font.draw(screen, kTitleX, kTitleY, _text.text(kSection, "title"), kTitleText, kWhite);
    _font.draw(screen, kLabelX, kLabelY, _text.text(kSection, "enter_name"), kText, kWhite);
    drawPicture(screen, _box, kBoxX, kBoxY, true);
    _font.draw(screen, kBoxX + kNameInset, kBoxY + (_box.h - int(kText)) / 2, _name, kText, kWhite);
    for (size_t i = 0; i < _ways.size(); ++i) {
        const ShellButton &b = _ways[i];
        const ConnectionChoice choice = buttons()[i].choice;
        // Drawn as the Options screen's buttons are: the original's, under
        // the emulator, stayed disabled (second look, grey words).
        b.draw(screen, _hover == choice, _pressed == choice, true);
        _font.drawCentred(screen, b.x, b.y, b.looks[0].w, b.looks[0].h, _text.text(kSection, buttons()[i].key), kText,
                          kButtonColour);
    }
    _bar.draw(screen, _font, _text.text(kSection, "previous_menu"), _hover == ConnectionChoice::kPreviousMenu,
              _pressed == ConnectionChoice::kPreviousMenu);
}

} // namespace reluva::storm3d
