// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/saved_games_screen.h"

#include <algorithm>

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
constexpr float kText = 16;
constexpr Color kWhite{255, 255, 255};
constexpr Color kBlack{0, 0, 0};
// The description's box and its text (0x53eac0): the picture at (100,
// 385), the edit control at (107, 392).
constexpr int kBoxX = 100;
constexpr int kBoxY = 385;
constexpr int kEditX = 107;
constexpr int kEditY = 392;
// The overwrite question: as Abort Mission's box is drawn
// (options_screen.cpp), its words label.map's save_over.
constexpr int kAskX = 140;
constexpr int kAskY = 150;
constexpr int kAskWidth = 359;
constexpr int kYesX = 163;
constexpr int kNoX = 357;
constexpr int kAskButtonY = 250;
constexpr int kWarningX = 232;
constexpr int kWarningY = 151;
constexpr int kMessageY = 195;
constexpr Color kAskColour{255, 190, 0};
constexpr float kAskButtonText = 12;

} // namespace

SavedGamesScreen::Hit SavedGamesScreen::hitAt(int x, int y) const {
    if (_asking)
        return {_yes.contains(x, y) ? kYes : _no.contains(x, y) ? kNo : kNone, 0};
    if (NavigationBar::onPrevious(x, y))
        return {kPrevious, 0};
    if (NavigationBar::onStart(x, y))
        return {kAct, 0};
    if (NavigationBar::onMain(x, y))
        return {kDelete, 0};
    if (_up.contains(x, y))
        return {kUp, 0};
    if (_down.contains(x, y))
        return {kDown, 0};
    if (x >= kRowX && x < kRowX + kRowWidth && y >= kRowY && y < kRowY + int(rows()) * kRowHeight) {
        const int row = (y - kRowY) / kRowHeight;
        if (_first + size_t(row) < _entries.size())
            return {kRow, row};
    }
    return {};
}

Error SavedGamesScreen::load(const GameFiles &files, bool saving, const std::string &map) {
    _saving = saving;
    if (Error err = loadPicture(files, "bitmaps/misc/LoadBG.bmp", _background))
        return err;
    if (Error err = _up.load(files, "bitmaps/multiset", "scrollup", kScrollX, kUpY))
        return err;
    if (Error err = _down.load(files, "bitmaps/multiset", "scrolldn", kScrollX, kDownY))
        return err;
    if (Error err = loadPicture(files, "bitmaps/multiset/scrolltb1.bmp", _thumb))
        return err;
    for (size_t i = 0; i < _row.size(); ++i)
        if (Error err = loadPicture(files, "bitmaps/multiset/mpsstart" + std::to_string(i + 1) + ".bmp", _row[i]))
            return err;
    if (saving) {
        if (Error err = loadPicture(files, "bitmaps/wonlogin/wlstext.bmp", _box))
            return err;
        if (Error err = loadPicture(files, "bitmaps/misc/DBsmall2BG.bmp", _askBox))
            return err;
        if (Error err = _yes.load(files, "bitmaps/multiset", "navback", kYesX, kAskButtonY))
            return err;
        if (Error err = _no.load(files, "bitmaps/multiset", "navmain", kNoX, kAskButtonY))
            return err;
    }
    if (Error err = _bar.load(files))
        return err;
    std::vector<uint8_t> data;
    if (Error err = files.read("label.map", data))
        return err;
    if (Error err = _text.parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size())))
        return Error::fail("label.map: " + err.message);
    _font.load();
    _description = map.empty() ? std::string() : _text.text("mission_select", map, map).substr(0, kDescriptionLength);
    _hover = _pressed = Hit{};
    _asking = false;
    _chosen = -1;
    _first = 0;
    return Error::success();
}

void SavedGamesScreen::setEntries(std::vector<Entry> entries) {
    _entries = std::move(entries);
    _chosen = -1;
    _first = std::min(_first, _entries.empty() ? 0 : _entries.size() - 1);
}

bool SavedGamesScreen::pointerAt(int x, int y) {
    const Hit now = hitAt(x, y);
    if (now == _hover)
        return false;
    _hover = now;
    return now.target != kNone;
}

void SavedGamesScreen::press(int x, int y) {
    _pressed = hitAt(x, y);
}

SavedGamesChoice SavedGamesScreen::release(int x, int y) {
    const Hit pressed = _pressed;
    _pressed = Hit{};
    if (!(hitAt(x, y) == pressed))
        return SavedGamesChoice::kNone;
    switch (pressed.target) {
    case kPrevious:
        return SavedGamesChoice::kPrevious;
    case kUp:
        _first = _first > 0 ? _first - 1 : 0;
        return SavedGamesChoice::kNone;
    case kDown:
        if (_first + rows() < _entries.size())
            ++_first;
        return SavedGamesChoice::kNone;
    case kRow:
        _chosen = int(_first) + pressed.row;
        return SavedGamesChoice::kNone;
    case kAct:
        // Saving over a game chosen asks first (save_over); loading wants one.
        if (_saving && _chosen >= 0) {
            _asking = true;
            return SavedGamesChoice::kNone;
        }
        if (_saving)
            return SavedGamesChoice::kSave;
        return _chosen >= 0 ? SavedGamesChoice::kLoad : SavedGamesChoice::kNone;
    case kDelete:
        return _chosen >= 0 ? SavedGamesChoice::kDelete : SavedGamesChoice::kNone;
    case kYes:
        _asking = false;
        return SavedGamesChoice::kSave;
    case kNo:
        _asking = false;
        return SavedGamesChoice::kNone;
    default:
        return SavedGamesChoice::kNone;
    }
}

void SavedGamesScreen::type(std::string_view text) {
    if (!_saving || _asking)
        return;
    for (char c : text)
        if (c >= ' ' && c <= '~' && _description.size() < kDescriptionLength)
            _description.push_back(c);
}

void SavedGamesScreen::erase() {
    if (_saving && !_asking && !_description.empty())
        _description.pop_back();
}

void SavedGamesScreen::draw(Surface &screen) {
    const char *section = _saving ? "save_game" : "load_game";
    drawPicture(screen, _background, 0, 0, false);
    _font.draw(screen, kTitleX, kTitleY, _text.text(section, "title"), kTitleText, kWhite);
    _up.draw(screen, _hover.target == kUp, _pressed.target == kUp, true);
    drawPicture(screen, _thumb, kScrollX, kThumbY, true);
    _down.draw(screen, _hover.target == kDown, _pressed.target == kDown, true);
    // Each row a button saying the save's description; the one chosen pressed.
    for (size_t r = 0; r < rows() && _first + r < _entries.size(); ++r) {
        const size_t i = _first + r;
        const bool lit = _hover.target == kRow && _hover.row == int(r);
        const image::Image &look = int(i) == _chosen ? _row[2] : lit ? _row[1] : _row[0];
        const int y = kRowY + int(r) * kRowHeight;
        drawPicture(screen, look, kRowX, y, false);
        _font.drawCentred(screen, kRowX, y, look.w, look.h, _entries[i].description, kText, kBlack);
    }
    if (_saving) {
        drawPicture(screen, _box, kBoxX, kBoxY, true);
        _font.draw(screen, kEditX, kEditY, _description, kText, kWhite);
    }
    _bar.draw(screen, _font, _text.text(section, "previous_menu"), _hover.target == kPrevious,
              _pressed.target == kPrevious, _text.text(section, _saving ? "save_game_text" : "load_game_text"),
              _text.text(section, "delete_game"));
    if (!_asking)
        return;
    static constexpr const char *kSection = "save_over";
    drawPicture(screen, _askBox, kAskX, kAskY, false);
    _font.draw(screen, kWarningX, kWarningY, _text.text(kSection, "warning"), kText, kAskColour);
    const std::string message = _text.text(kSection, "message");
    _font.draw(screen, kAskX + (kAskWidth - _font.width(message, kText)) / 2, kMessageY, message, kText, kAskColour);
    _yes.draw(screen, _hover.target == kYes, _pressed.target == kYes, true);
    _no.draw(screen, _hover.target == kNo, _pressed.target == kNo, true);
    _font.drawCentred(screen, _yes.x, _yes.y, _yes.looks[0].w, _yes.looks[0].h, _text.text(kSection, "ok_button"),
                      kAskButtonText, kBlack);
    _font.drawCentred(screen, _no.x, _no.y, _no.looks[0].w, _no.looks[0].h, _text.text(kSection, "cancel_button"),
                      kAskButtonText, kBlack);
}

} // namespace reluva::storm3d
