// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/load_game_screen.h"

namespace reluva::storm3d {

namespace {

// Where the game's frame has them (the pictures matched against it).
constexpr int kScrollX = 614;
constexpr int kUpY = 53;
constexpr int kThumbY = 74;
constexpr int kDownY = 396;
constexpr int kTitleX = 64;
constexpr int kTitleY = 6;
constexpr float kTitleText = 20;
constexpr Color kWhite{255, 255, 255};
constexpr const char *kSection = "load_game";

} // namespace

LoadGameScreen::Target LoadGameScreen::targetAt(int x, int y) {
    if (NavigationBar::onPrevious(x, y))
        return kPrevious;
    if (NavigationBar::onStart(x, y))
        return kLoad;
    if (NavigationBar::onMain(x, y))
        return kDelete;
    return kNone;
}

Error LoadGameScreen::load(const GameFiles &files) {
    if (Error err = loadPicture(files, "bitmaps/misc/LoadBG.bmp", _background))
        return err;
    if (Error err = _up.load(files, "bitmaps/multiset", "scrollup", kScrollX, kUpY))
        return err;
    if (Error err = _down.load(files, "bitmaps/multiset", "scrolldn", kScrollX, kDownY))
        return err;
    if (Error err = loadPicture(files, "bitmaps/multiset/scrolltb1.bmp", _thumb))
        return err;
    if (Error err = _bar.load(files))
        return err;
    std::vector<uint8_t> data;
    if (Error err = files.read("label.map", data))
        return err;
    if (Error err = _text.parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size())))
        return Error::fail("label.map: " + err.message);
    _font.load();
    _hover = _pressed = kNone;
    return Error::success();
}

bool LoadGameScreen::pointerAt(int x, int y) {
    const Target now = targetAt(x, y);
    if (now == _hover)
        return false;
    _hover = now;
    return now != kNone;
}

void LoadGameScreen::press(int x, int y) {
    _pressed = targetAt(x, y);
}

bool LoadGameScreen::release(int x, int y) {
    const Target pressed = _pressed;
    _pressed = kNone;
    // With no saved games, Load Game and Delete Game have nothing to act on.
    return pressed == kPrevious && targetAt(x, y) == kPrevious;
}

void LoadGameScreen::draw(Surface &screen) {
    drawPicture(screen, _background, 0, 0, false);
    _font.draw(screen, kTitleX, kTitleY, _text.text(kSection, "title"), kTitleText, kWhite);
    _up.draw(screen, false, false, true);
    drawPicture(screen, _thumb, kScrollX, kThumbY, true);
    _down.draw(screen, false, false, true);
    _bar.draw(screen, _font, _text.text(kSection, "previous_menu"), _hover == kPrevious, _pressed == kPrevious,
              _text.text(kSection, "load_game_text"), _text.text(kSection, "delete_game"));
}

} // namespace reluva::storm3d
