// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/main_menu.h"

#include "engines/storm3d/shell/shell_draw.h"
#include "graphics/font.h"

#include <algorithm>

namespace reluva::storm3d {

namespace {

// The version the menu writes in its corner, as the game does (in GDI's
// "System" font, bold; Reluva's own stands in), and its colour as drawn.
constexpr const char *kVersion = "v1.0";
constexpr int kVersionX = 605;
constexpr int kVersionY = 467;
constexpr Color kVersionColour{181, 0, 0};

} // namespace

const std::vector<MainMenu::Button> &MainMenu::buttons() {
    // Where the game's frame of the menu has them (matched against the
    // bitmaps and the movies' frames, to the pixel).
    static const std::vector<Button> kButtons = {
        {MenuChoice::kSinglePlayer, "MSsingl", 22, 10, "Single", 0, 45},
        {MenuChoice::kMultiplayer, "MSmulti", 488, 10, "Multi", 353, 32},
        {MenuChoice::kOptions, "MSoption", 22, 452, "Const", 0, 285},
        // The ship flying off is Quit's: the game lit Quit under it, and
        // a click there ended the program.
        {MenuChoice::kCredits, "MScredit", 513, 433, nullptr, 0, 0},
        {MenuChoice::kQuit, "MSquit", 544, 415, "Credit", 408, 217},
        {MenuChoice::kReplayIntro, "MSintro", 439, 451, nullptr, 0, 0},
    };
    return kButtons;
}

int MainMenu::buttonAt(int x, int y, const std::vector<std::array<int, 4>> &sizes) {
    const std::vector<Button> &all = buttons();
    auto inside = [x, y](int rx, int ry, int w, int h) { return x >= rx && y >= ry && x < rx + w && y < ry + h; };
    // The labels first: Credits' runs over Quit's panel's bottom edge.
    for (size_t i = 0; i < all.size() && i < sizes.size(); ++i)
        if (inside(all[i].labelX, all[i].labelY, sizes[i][0], sizes[i][1]))
            return int(i);
    for (size_t i = 0; i < all.size() && i < sizes.size(); ++i)
        if (all[i].movie && inside(all[i].panelX, all[i].panelY, sizes[i][2], sizes[i][3]))
            return int(i);
    return -1;
}

Error MainMenu::load(const GameFiles &files, uint64_t millis) {
    if (Error err = loadPicture(files, "bitmaps/main/mainbk.bmp", _background))
        return err;
    _shown.clear();
    _shown.resize(buttons().size());
    for (size_t i = 0; i < buttons().size(); ++i) {
        const Button &b = buttons()[i];
        for (size_t look = 0; look < 3; ++look)
            if (Error err = loadPicture(files, "bitmaps/ms/" + std::string(b.label) + std::to_string(look + 1) + ".bmp",
                                        _shown[i].looks[look]))
                return err;
        if (!b.movie)
            continue;
        const std::string movie = "animations/" + std::string(b.movie);
        if (!_shown[i].rest.open(files, movie + "01.bik"))
            _shown[i].rest.restart(millis);
        if (!_shown[i].lit.open(files, movie + "02.bik"))
            _shown[i].lit.restart(millis);
    }
    _hover = _pressed = -1;
    return Error::success();
}

std::vector<std::array<int, 4>> MainMenu::rectangles() const {
    std::vector<std::array<int, 4>> sizes;
    for (const Shown &s : _shown)
        sizes.push_back(
            {s.looks[0].w, s.looks[0].h, s.rest.isOpen() ? s.rest.width() : 0, s.rest.isOpen() ? s.rest.height() : 0});
    return sizes;
}

bool MainMenu::pointerAt(int x, int y, uint64_t millis) {
    const int now = buttonAt(x, y, rectangles());
    if (now == _hover)
        return false;
    // The panel under the pointer plays its second movie from its start
    // (inferred: its frames match it, the others' their first).
    if (now >= 0 && _shown[size_t(now)].lit.isOpen())
        _shown[size_t(now)].lit.restart(millis);
    if (_hover >= 0 && _shown[size_t(_hover)].rest.isOpen())
        _shown[size_t(_hover)].rest.restart(millis);
    _hover = now;
    return now >= 0;
}

void MainMenu::press(int x, int y) {
    _pressed = buttonAt(x, y, rectangles());
}

MenuChoice MainMenu::release(int x, int y) {
    const int at = buttonAt(x, y, rectangles());
    const int pressed = _pressed;
    _pressed = -1;
    return at >= 0 && at == pressed ? buttons()[size_t(at)].choice : MenuChoice::kNone;
}

void MainMenu::draw(Surface &screen, uint64_t millis) {
    if (screen.format != PixelFormat::kRGBA32)
        return;
    drawPicture(screen, _background, 0, 0, false);
    for (size_t i = 0; i < _shown.size(); ++i) {
        const Button &b = buttons()[i];
        PanelMovie &movie = int(i) == _hover && _shown[i].lit.isOpen() ? _shown[i].lit : _shown[i].rest;
        if (b.movie && movie.isOpen() && movie.update(millis) && !movie.rgba().empty())
            drawPixels(screen, movie.rgba().data(), movie.width(), movie.height(), b.panelX, b.panelY, false);
    }
    for (size_t i = 0; i < _shown.size(); ++i) {
        const Button &b = buttons()[i];
        const size_t look = int(i) == _pressed && int(i) == _hover ? 2 : int(i) == _hover ? 1 : 0;
        const image::Image &label = _shown[i].looks[look];
        drawPicture(screen, label, b.labelX, b.labelY, true);
    }
    font::drawText(screen, kVersionX, kVersionY, kVersion, kVersionColour);
}

} // namespace reluva::storm3d
