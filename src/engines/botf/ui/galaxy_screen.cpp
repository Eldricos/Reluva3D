// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/galaxy_screen.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace reluva::botf {

namespace {

// ?maingal.wdf's widgets.
constexpr int32_t kMap = 1017;      // the map's place, 526x391
constexpr int32_t kTurnText = 1021; // "TURN 1"
constexpr int32_t kMilitary = 1004; // the map's mode, lit at the start
// The task forces list's scroll bar and its arrows: not shown with the
// first turn's two task forces.
constexpr int32_t kScrollUp = 1038;
constexpr int32_t kScrollDown = 1039;
constexpr int32_t kScrollBar = 1040;

// The map, measured on a screenshot of a small galaxy (13 by 10 sectors):
// sectors 39 pixels square, the grid in the middle of the map's place, the
// backdrop drawn from the grid's corner.
constexpr int kSector = 39;
// A star's place in its sector: 7 pixels for each step of its position on
// the small map, which has 5 a sector (three stars agree).
constexpr int kStarStep = 7;
constexpr int kStarOffset = -3;

constexpr Color kGridColor{0, 0, 8};            // the lines outside the scanners' range
constexpr Color kScannedGridColor{24, 73, 173}; // dotted, inside it
constexpr Color kNameColor{0, 251, 255};
constexpr Color kTurnColor{24, 101, 189};

// The empires' letters in their files' names, Cardassians to Romulans.
constexpr char kLetters[] = {'c', 'h', 'f', 'k', 'r'};

void put(Surface &s, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= s.w || y >= s.h)
        return;
    uint8_t *p = s.row(y) + size_t(x) * 4;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = 255;
}

} // namespace

Error GalaxyScreen::load(int empire) {
    _empire = std::clamp(empire, 0, 4);
    _letter = kLetters[_empire];
    const std::string layout = std::string(1, _letter) + "maingal.wdf";
    _hidden = {kScrollUp, kScrollDown, kScrollBar};
    return loadLayouts(layout.c_str(), {kMap, kTurnText, kMilitary}, false);
}

void GalaxyScreen::open(SavedGalaxy galaxy, std::vector<SavedTaskForce> taskForces, int empire, int turn) {
    _galaxy = std::move(galaxy);
    _taskForces = std::move(taskForces);
    _empire = std::clamp(empire, 0, 4);
    _turn = turn;
    // What the player's scanners reach: a sector around each of its
    // systems and task forces (inferred from the first turn's screenshot,
    // which shows the 3 by 3 sectors around Sol).
    _scanned.clear();
    auto around = [&](int cx, int cy) {
        for (int y = cy - 1; y <= cy + 1; ++y)
            for (int x = cx - 1; x <= cx + 1; ++x)
                if (x >= 0 && y >= 0 && x < _galaxy.width && y < _galaxy.height)
                    _scanned.insert({x, y});
    };
    for (int y = 0; y < _galaxy.height; ++y)
        for (int x = 0; x < _galaxy.width; ++x)
            if (const int s = _galaxy.at(x, y).system; s >= 0 && _galaxy.systems[size_t(s)].controllingRace == _empire)
                around(x, y);
    for (const SavedTaskForce &t : _taskForces)
        if (t.owner == _empire)
            around(t.x, t.y);
}

void GalaxyScreen::draw(Surface &screen) const {
    drawLayouts(screen, {kMilitary});
    drawMap(screen);
    drawText(screen, kTurnText, upperAscii(word(lex::kTurn, "Turn")) + " " + std::to_string(_turn + 1), kTurnColor);
}

void GalaxyScreen::drawMap(Surface &screen) const {
    const Placed *map = find(kMap);
    if (!map || _galaxy.width <= 0)
        return;
    const int gx = map->x + (map->widget->w - _galaxy.width * kSector) / 2;
    const int gy = map->y + (map->widget->h - _galaxy.height * kSector) / 2;
    const int right = map->x + map->widget->w;
    const int bottom = map->y + map->widget->h;
    if (const image::Image *backdrop = _assets.picture(std::string(1, _letter) + "m_glmap.tga"))
        blitClipped(screen, *backdrop, gx, gy, map->x, map->y, map->widget->w, map->widget->h);
    // The scanners' sectors are the backdrop darkened to a fifth.
    for (const auto &[sx, sy] : _scanned)
        for (int y = gy + sy * kSector; y < gy + (sy + 1) * kSector && y < bottom; ++y)
            for (int x = gx + sx * kSector; x < gx + (sx + 1) * kSector && x < right; ++x) {
                uint8_t *p = screen.row(y) + size_t(x) * 4;
                for (int c = 0; c < 3; ++c)
                    p[c] = uint8_t(p[c] / 5);
            }
    // The grid: solid lines, dotted where the scanners reach.
    for (int row = 0; row <= _galaxy.height; ++row)
        for (int x = gx; x < gx + _galaxy.width * kSector && x < right; ++x) {
            const int col = (x - gx) / kSector;
            const bool lit = scanned(col, row) || scanned(col, row - 1);
            if (!lit)
                put(screen, x, gy + row * kSector, kGridColor);
            else if ((x - gx) % 2 == 0)
                put(screen, x, gy + row * kSector, kScannedGridColor);
        }
    for (int col = 0; col <= _galaxy.width; ++col)
        for (int y = gy; y < gy + _galaxy.height * kSector && y < bottom; ++y) {
            const int row = (y - gy) / kSector;
            const bool lit = scanned(col, row) || scanned(col - 1, row);
            if (!lit)
                put(screen, gx + col * kSector, y, kGridColor);
            else if ((y - gy) % 2 == 0)
                put(screen, gx + col * kSector, y, kScannedGridColor);
        }
    // The stars the scanners see; the names of those the empire has
    // explored under them (the first turn shows Sol's, not its neighbours').
    const Font *font = _assets.font("human9.fnt");
    for (const auto &[sx, sy] : _scanned) {
        const int s = _galaxy.at(sx, sy).system;
        if (s < 0)
            continue;
        const SavedSystem &sys = _galaxy.systems[size_t(s)];
        const image::Image *star = _assets.picture(sys.starAnimation + "maps.tga");
        if (!star)
            continue;
        const int x = gx + sx * kSector + kStarStep * (sys.mapX - 5 * sx) + kStarOffset;
        const int y = gy + sy * kSector + kStarStep * (sys.mapY - 5 * sy) + kStarOffset;
        // Black is the star's background.
        for (int py = 0; py < star->h; ++py)
            for (int px = 0; px < star->w; ++px) {
                const uint8_t *p = star->pixel(px, py);
                if (p[0] | p[1] | p[2])
                    put(screen, x + px, y + py, Color{p[0], p[1], p[2]});
            }
        if (font && (_galaxy.at(sx, sy).explored >> _empire & 1))
            font->drawInColor(screen, x + star->w / 2 - font->measure(sys.name) / 2, y + star->h, sys.name, kNameColor);
    }
}

} // namespace reluva::botf
