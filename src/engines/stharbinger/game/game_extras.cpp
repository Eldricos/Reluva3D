// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The credits (DEEP.EXE 0x22424, scroller 0x2275D) and the deck map (0x24299).

#include "engines/stharbinger/game/game.h"

#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

#include <algorithm>

namespace reluva::stharbinger {

namespace {

// The credits window and its text.
const Rect kCreditsWindow(258, 24, 638, 456);
const int kCreditsLinePitch = 24;
const int kCreditsStep = 4;           // pixels per scroll step
const int kCreditsStepsPerScroll = 2; // one scroll step every 40 ms
const uint8_t kHeadingColor = 0xFD;
const uint8_t kTextColor = 7;
const uint8_t kCreditsShadow = 0xFE;
const uint32_t kCreditsFadeMillis = 500; // 10 steps of 50 ms

// Deck maps: raw 8-bit images (maps\*.dat), the view shown, and where each
// location is (DEEP.EXE tables 0x54BF4 / 0x54D20).
struct DeckMap {
    const char *file;
    int w;
    int h;
    int viewW;
    int viewH;
};
const DeckMap kDeckMaps[4] = {
    {"MAPS/OPS.DAT", 301, 309, 240, 320},
    {"MAPS/LOWER.DAT", 393, 767, 240, 320},
    {"MAPS/UPPER.DAT", 469, 930, 240, 320},
    {"MAPS/HABITAT.DAT", 1101, 883, 320, 240},
};

struct MapLocation {
    int location;
    int x;
    int y;
};
const MapLocation kMapLocations[] = {
    {0, 36, 150},    {1, 58, 175},    {2, 40, 218},    {3, 72, 217},    {4, 120, 234},   {5, 180, 236},
    {6, 227, 217},   {7, 244, 158},   {8, 207, 110},   {9, 145, 94},    {10, 81, 107},   {20, 277, 72},
    {21, 227, 123},  {22, 198, 150},  {23, 176, 180},  {24, 158, 210},  {25, 138, 246},  {26, 110, 316},
    {27, 101, 395},  {28, 111, 472},  {29, 141, 534},  {30, 144, 571},  {31, 175, 601},  {32, 208, 623},
    {33, 266, 648},  {34, 272, 683},  {35, 90, 618},   {36, 304, 479},  {37, 254, 406},  {38, 223, 322},
    {39, 300, 278},  {40, 239, 265},  {65, 284, 148},  {66, 229, 189},  {67, 194, 222},  {68, 159, 265},
    {69, 125, 333},  {70, 108, 381},  {71, 91, 429},   {72, 95, 505},   {73, 107, 550},  {74, 121, 597},
    {75, 137, 640},  {76, 160, 680},  {77, 189, 715},  {78, 224, 747},  {79, 264, 777},  {80, 328, 571},
    {81, 257, 514},  {82, 258, 403},  {83, 325, 369},  {84, 375, 296},  {100, 129, 755}, {101, 123, 695},
    {102, 118, 633}, {103, 113, 570}, {104, 104, 509}, {105, 101, 453}, {106, 157, 447}, {107, 213, 441},
    {108, 272, 438}, {109, 330, 439}, {110, 388, 444}, {111, 452, 452}, {112, 444, 512}, {113, 436, 565},
    {114, 427, 619}, {115, 497, 630}, {116, 582, 651}, {117, 572, 696}, {118, 562, 741}, {119, 550, 790},
    {120, 488, 777}, {121, 448, 735}, {122, 592, 605}, {123, 603, 560}, {124, 654, 573}, {125, 746, 596},
    {126, 835, 629}, {127, 857, 574}, {128, 877, 522}, {129, 900, 465}, {130, 923, 408}, {131, 947, 350},
    {132, 969, 291}, {133, 998, 228}, {134, 941, 204}, {135, 884, 182}, {136, 831, 155}, {137, 769, 131},
    {138, 211, 401}, {139, 265, 358}, {140, 305, 359}, {141, 346, 359}, {142, 377, 286},
};

} // namespace

// ---- Credits ------------------------------------------------------------------

void Game::startCredits(bool withBlank, bool fromMenu) {
    if (!_creditsLoaded) {
        _creditsLoaded = true;
        _creditLines = _resources.loadCredits();
        auto s = _resources.openFile("ART/HAN.FNT");
        if (!s || !_creditsFont.load(*s))
            log::warning("art\\han.fnt not found: the credits have no text");
    }
    _credits = CreditsScreen();
    _credits.active = true;
    _credits.fromMenu = fromMenu;
    bool loaded = false;
    if (!_resources.loadBackground("CREDITS", _credits.background, _credits.palette, loaded))
        draw::create(_credits.background, kScreenWidth, kScreenHeight, 0);
    if (withBlank && _resources.loadBackground("BLANKCRD", _credits.blank, _credits.blankPalette, loaded))
        _credits.phase = CreditsScreen::kBlank;
    _credits.phaseStart = _millis;
    if (!fromMenu)
        _thread.wait = kWaitCredits;
    if (_system->getMixer())
        _system->getMixer()->stopAll();
    drawCredits();
}

void Game::drawCredits() {
    Surface &dst = _screen;
    if (_credits.phase == CreditsScreen::kBlank) {
        draw::blit(dst, _credits.blank, 0, 0);
        return;
    }
    draw::blit(dst, _credits.background, 0, 0);
    if (_creditsFont.empty())
        return;
    // The lines come up from the bottom of the window, centred in it; headings
    // (marked *) in their own colour.
    Surface window;
    draw::create(window, kCreditsWindow.width(), kCreditsWindow.height());
    for (int y = 0; y < window.h; y++)
        for (int x = 0; x < window.w; x++)
            *draw::at(window, x, y) = *draw::at(_credits.background, kCreditsWindow.left + x, kCreditsWindow.top + y);
    for (int i = 0; i < int(_creditLines.size()); i++) {
        int y = kCreditsWindow.height() - _credits.offset + i * kCreditsLinePitch;
        if (y < -kCreditsLinePitch || y >= window.h)
            continue;
        std::string text = _creditLines[i];
        bool heading = !text.empty() && text[0] == '*';
        if (heading)
            text.erase(0, 1);
        int x = (640 - _creditsFont.getStringWidth(text)) / 2 + 128 - kCreditsWindow.left;
        _creditsFont.drawString(window, x, y, text, heading ? kHeadingColor : kTextColor, kCreditsShadow);
    }
    draw::blit(dst, window, kCreditsWindow.left, kCreditsWindow.top);
}

void Game::updateCredits() {
    uint32_t elapsed = _millis - _credits.phaseStart;
    int level = int(std::min<uint32_t>(elapsed, kCreditsFadeMillis) * 256 / kCreditsFadeMillis);
    switch (_credits.phase) {
    case CreditsScreen::kBlank:
        _palette = _credits.blankPalette;
        _fadeLevel = 256 - level;
        applyPalette();
        if (elapsed >= kCreditsFadeMillis) {
            _credits.phase = CreditsScreen::kFadeIn;
            _credits.phaseStart = _millis;
        }
        break;
    case CreditsScreen::kFadeIn:
        _palette = _credits.palette;
        _fadeLevel = level;
        applyPalette();
        if (elapsed >= kCreditsFadeMillis) {
            _credits.phase = CreditsScreen::kScroll;
            _credits.phaseStart = _millis;
        }
        break;
    case CreditsScreen::kScroll:
        if (_stepCount % kCreditsStepsPerScroll == 0)
            _credits.offset += kCreditsStep;
        // Done when the last line has left the window.
        if (_credits.offset > kCreditsWindow.height() + int(_creditLines.size()) * kCreditsLinePitch) {
            _credits.phase = CreditsScreen::kFadeOut;
            _credits.phaseStart = _millis;
        }
        break;
    case CreditsScreen::kFadeOut:
        _fadeLevel = 256 - level;
        applyPalette();
        if (elapsed >= kCreditsFadeMillis)
            finishCredits();
        break;
    }
    if (_credits.active)
        drawCredits();
}

void Game::finishCredits() {
    bool fromMenu = _credits.fromMenu;
    _credits = CreditsScreen();
    _fadeLevel = 256;
    if (fromMenu) {
        drawMenuScreen();
    } else {
        _palette = Palette();
        applyPalette();
        draw::fill(_screen, 0);
        draw::create(_background, kScreenWidth, kScreenHeight);
        _workBufferStale = true;
        _thread.wait = kWaitNone;
    }
}

// ---- The deck map --------------------------------------------------------------

bool Game::openDeckMap() {
    int location = _mapLocation;
    const MapLocation *where = nullptr;
    for (const MapLocation &l : kMapLocations)
        if (l.location == location)
            where = &l;
    if (!where)
        return false;
    const DeckMap &map = kDeckMaps[location < 20 ? 0 : location < 65 ? 1 : location < 100 ? 2 : location < 150 ? 3 : 0];
    auto s = _resources.openFile(map.file);
    if (!s) {
        log::warning("%s not found", map.file);
        return false;
    }
    std::vector<uint8_t> pixels = s->readAll();
    if (pixels.size() < size_t(map.w) * map.h)
        return false;

    // The view around the marker, at the bottom left of the screen.
    int vw = std::min(map.viewW, map.w);
    int vh = std::min(map.viewH, map.h);
    int left = std::clamp(where->x - vw / 2, 0, map.w - vw);
    int top = std::clamp(where->y - vh / 2, 0, map.h - vh);
    int sx = 0;
    int sy = kScreenHeight - vh;
    uint8_t grey = uint8_t(_palette.findBestColor(162, 162, 162));
    draw::create(_deckMapFrame, kScreenWidth, kScreenHeight);
    draw::blit(_deckMapFrame, _cleanScreen, 0, 0);
    for (int y = 0; y < vh; y++) {
        for (int x = 0; x < vw; x++) {
            uint8_t p = pixels[size_t(top + y) * map.w + left + x];
            *draw::at(_deckMapFrame, sx + x, sy + y) = p == 1 ? grey : p;
        }
    }
    // A grey band at the top and the marker: an X of 4-pixel strokes in the
    // cycling colours 2 and 3.
    draw::fillRect(_deckMapFrame, sx, sy, vw, 6, grey);
    int mx = sx + where->x - left;
    int my = sy + where->y - top;
    for (int t = 0; t < 12; t++) {
        uint8_t c = (t & 1) ? 3 : 2;
        for (int k = 0; k < 4; k++) {
            int px = mx - 6 + t;
            for (int py : {my - 6 + t + k - 1, my + 5 - t + k - 1})
                if (px >= sx && px < sx + vw && py >= sy && py < sy + vh)
                    *draw::at(_deckMapFrame, px, py) = c;
        }
    }
    _deckMap = true;
    log::debug(1, "Deck map %s, location %d", map.file, location);
    return true;
}

void Game::pressKey(uint32_t key) {
    InputEvent event;
    event.type = InputEvent::kKeyDown;
    event.key = key;
    handleEvent(event);
}

} // namespace reluva::stharbinger
