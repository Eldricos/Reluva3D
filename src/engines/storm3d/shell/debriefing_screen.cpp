// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/debriefing_screen.h"

#include <utility>

namespace reluva::storm3d {

namespace {

// Where the original's frame has its words (measured on it): the title as
// the Options screen's; a column each 48 across from 215; a row each 40
// down, its name's two lines centred on 108.
constexpr int kTitleX = 65;
constexpr int kTitleY = 6;
constexpr int kFirstColumn = 215;
constexpr int kColumnStep = 48;
constexpr int kNameX = 108;
constexpr int kHeadingY = 64; // the tops of their lines: the capitals 3 lower
constexpr int kLabelY = 80;
constexpr int kRowY = 100;
constexpr int kRowStep = 40;
constexpr int kLineStep = 16;
constexpr int kNumberY = 108;
// Arial Bold, at sizes fitted to the frame's words' widths: 16 pixels for
// the headings, names and numbers, 12 for the columns' words.
constexpr float kTitleText = 20;
constexpr float kText = 16;
constexpr float kLabelText = 12;
constexpr Color kTitleColour{255, 255, 255};
constexpr Color kHeadingColour{255, 190, 0};
// The bottom bar's plates, as every screen's: Continue on the middle one,
// Quit To Windows on the right.
constexpr int kBottomY = 445;
constexpr int kContinueX = 123;
constexpr int kContinueWidth = 394;
constexpr int kQuitX = 522;
constexpr int kQuitWidth = 112;
constexpr int kBottomHeight = 30;

bool inside(int x, int y, int left, int top, int w, int h) {
    return x >= left && y >= top && x < left + w && y < top + h;
}

} // namespace

DebriefingChoice DebriefingScreen::choiceAt(int x, int y) {
    if (inside(x, y, kContinueX, kBottomY, kContinueWidth, kBottomHeight))
        return DebriefingChoice::kContinue;
    if (inside(x, y, kQuitX, kBottomY, kQuitWidth, kBottomHeight))
        return DebriefingChoice::kQuit;
    return DebriefingChoice::kNone;
}

Error DebriefingScreen::load(const GameFiles &files) {
    if (Error err = loadPicture(files, "bitmaps/credit/creditsBG.bmp", _background))
        return err;
    if (Error err = _bar.load(files))
        return err;
    std::vector<uint8_t> data;
    if (Error err = files.read("label.map", data))
        return err;
    if (Error err = _text.parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size())))
        return Error::fail("label.map: " + err.message);
    _font.load();
    _hover = _pressed = DebriefingChoice::kNone;
    return Error::success();
}

bool DebriefingScreen::pointerAt(int x, int y) {
    const DebriefingChoice now = choiceAt(x, y);
    if (now == _hover)
        return false;
    _hover = now;
    return now != DebriefingChoice::kNone;
}

DebriefingChoice DebriefingScreen::release(int x, int y) {
    const DebriefingChoice pressed = std::exchange(_pressed, DebriefingChoice::kNone);
    return choiceAt(x, y) == pressed ? pressed : DebriefingChoice::kNone;
}

void DebriefingScreen::draw(Surface &screen) {
    static constexpr const char *kSection = "multiplayer_debriefing";
    drawPicture(screen, _background, 0, 0, false);
    _font.draw(screen, kTitleX, kTitleY, _text.text(kSection, "title"), kTitleText, kTitleColour);
    auto centred = [&](int x, int y, const std::string &words, float size, Color colour) {
        _font.draw(screen, x - _font.width(words, size) / 2, y, words, size, colour);
    };
    // Each heading over its two columns, each column's word under it.
    static constexpr const char *kHeadings[4] = {"dilithium", "created", "lost", "destroyed"};
    for (int h = 0; h < 4; ++h) {
        const int left = kFirstColumn + 2 * h * kColumnStep;
        centred(left + kColumnStep / 2, kHeadingY, _text.text(kSection, kHeadings[h]), kText, kHeadingColour);
        centred(left, kLabelY, _text.text(kSection, h == 0 ? "collected" : "ships"), kLabelText, kHeadingColour);
        centred(left + kColumnStep, kLabelY, _text.text(kSection, h == 0 ? "spent" : "stations"), kLabelText,
                kHeadingColour);
    }
    for (size_t r = 0; r < _rows.size(); ++r) {
        const Row &row = _rows[r];
        const int top = kRowY + int(r) * kRowStep;
        centred(kNameX, top, row.name, kText, row.colour);
        centred(kNameX, top + kLineStep, row.side, kText, row.colour);
        for (size_t c = 0; c < row.numbers.size(); ++c)
            centred(kFirstColumn + int(c) * kColumnStep, kNumberY + int(r) * kRowStep, std::to_string(row.numbers[c]),
                    kText, row.colour);
    }
    // "Quit To Windows" is wider than its plate, and runs over its edges,
    // as the original's does.
    // Lit under the pointer, as the original's Continue was.
    auto look = [this](DebriefingChoice c) { return _pressed == c ? 2 : _hover == c ? 1 : 0; };
    _bar.drawPlates(screen, _font, _text.text(kSection, "continue"), _text.text(kSection, "quit"), {0, 0, 0},
                    look(DebriefingChoice::kContinue), look(DebriefingChoice::kQuit));
}

} // namespace reluva::storm3d
