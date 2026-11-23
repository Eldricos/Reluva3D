// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/botf/formats/lexicon.h"
#include "engines/botf/formats/save_game.h"
#include "engines/botf/formats/window_layout.h"
#include "engines/botf/ui/assets.h"
#include "engines/botf/ui/galaxy_screen.h"
#include "image/image.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// What the galaxy screen's parts share: its widgets' ids, the places,
// colours and timings measured on the game's screenshots, and small
// helpers; for galaxy_screen.cpp, galaxy_map.cpp, galaxy_panel.cpp and
// galaxy_strip.cpp (docs/engines/botf/systems/galaxy-screen.md).

namespace reluva::botf::galaxy {

// ?maingal.wdf's widgets.
constexpr int32_t kMap = 1017;      // the map's place, 526x391
constexpr int32_t kTurnText = 1021; // "TURN 1"
constexpr int32_t kMilitary = 1004; // the map's mode, lit at the start
// The Map button opens the zoom's two buttons (?_zoom.wdf in place 1025,
// over the map's corner): Small Map zooms in, Large Map out.
constexpr int32_t kMapButton = 1002;
// Over the map, the sector under the pointer: its name in the turn's blue
// (with the claiming empire's adjective where one claims it)
// at the box's left, and the empire's scan strength there ending 6 pixels
// short of its right, yellow where it's above 0 and red where it isn't
// (two screenshots: "Unexplored Sector I6", -3; "Unclaimed Sector I1", 5).
constexpr int32_t kSectorText = 1081;
/** An empire's adjective, for the sector line: its lexicon entry, and the English. */
struct Adjective {
    int entry;
    const char *english;
};
constexpr Adjective kAdjectives[] = {{lex::kCardassian, "Cardassian"},
                                     {lex::kFederationAdjective, "Federation"},
                                     {lex::kFerengiAdjective, "Ferengi"},
                                     {lex::kKlingon, "Klingon"},
                                     {lex::kRomulan, "Romulan"}};
constexpr int kScanRightMargin = 6;
constexpr Color kScanColor{255, 255, 0};
constexpr Color kHiddenScanColor{255, 0, 0};
constexpr int32_t kZoomPlace = 1025;
constexpr int32_t kSmallMap = 1022;
constexpr int32_t kLargeMap = 1023;
// The task forces list's scroll bar and its arrows: not shown with the
// first turn's two task forces.
constexpr int32_t kScrollUp = 1038;
constexpr int32_t kScrollDown = 1039;
constexpr int32_t kScrollBar = 1040;
constexpr int32_t kPanel = 1020; // the empire's panel's place

// The panel's parts, one under another (measured on the screenshot): the
// empire's information, the other empires' emblems, the task forces' bar,
// and a row for each task force.
constexpr int kEmblemsY = 90;
constexpr int kTaskForcesY = 130;
constexpr int kFirstRowY = 147;
constexpr int kRowHeight = 40;
constexpr int32_t kInfoText = 1018;
constexpr int32_t kFirstEmblem = 1050; // 1050 + the empire's number; the player's has none
constexpr int32_t kRowIcon = 1036;
constexpr int32_t kRowText = 1037;
constexpr Color kLabelColor{247, 121, 33};
constexpr Color kFigureColor{173, 227, 115};
constexpr Color kTaskForceColor{99, 77, 181};
// The information's lines are 17 pixels apart, the font's 16 and one, and
// the figures end 6 pixels short of the text box's right edge.
constexpr int kLinePitch = 17;
constexpr int kFigureMargin = 6;
constexpr int kDilithiumGap = 6;
// result.lst's contents for a task force and a ship.
constexpr int kTaskForceResult = 4;
constexpr size_t kShipSpeedOffset = 268; // shiplist.sst's speed, a double
constexpr int kShipResult = 5;
// The task forces' bar: drawn here, as the game shows the background
// through its backing picture's black (hm_bktfi.tga, which has no magenta),
// and with hm_tab22.tga for its tab, not the layout's hm_tab20.tga (the
// screenshot's; hm_tab32 is the same picture).
constexpr int32_t kTaskForcesButton = 1028;
constexpr int32_t kTaskForcesTab = 1029;
constexpr const char *kTaskForcesTabPicture = "hm_tab22.tga";

// The map, measured on screenshots of small galaxies (13 by 10 sectors),
// zoomed out and in. Zoomed out, the sectors are 39 pixels square and the
// grid is in the middle of the map's place; that grid's square is the view
// both ways: zoomed in, what falls outside it isn't drawn, and the backdrop
// stays where it is.
/** How the map is drawn at a zoom: its sectors' size, where stars go, the pictures. */
struct Zoom {
    int sector; // pixels square
    // A star's place in its sector: so many pixels for each step of its
    // position on the small map, which has 5 a sector, plus an offset
    // (every star on the screenshots agrees).
    int starStep;
    int starOffset;
    const char *starSuffix;    // its picture: s-y-maps.tga, s-y-map.tga
    const char *stellarSuffix; // a stellar object's: nsmaps.tga, nsmap.tga
    // The empire's emblem for its task forces, after "i_" and the letter,
    // a pixel right of its sector's corner and one above or below it.
    const char *fleet;
    int fleetX;
    int fleetY;
};
constexpr Zoom kZooms[] = {{39, 7, -3, "maps.tga", "maps.tga", "i18.tga", 1, -1},
                           {61, 12, -8, "map.tga", "map.tga", "26.tga", 1, 1}};
constexpr int kOutSector = kZooms[0].sector;

constexpr Color kScannedGridColor{24, 73, 173}; // the dotted lines round what the scanners reach
constexpr Color kNameColor{0, 251, 255};        // the empire's own systems'
// Those of systems nobody holds; a Romulan system's in green (Romulus, in a
// zoomed-in screenshot); the other empires' are taken to be the first
// colour (guessed: none is seen).
constexpr Color kOtherNameColor{255, 186, 57};
constexpr Color kRomulanNameColor{0, 170, 16};
constexpr int kRomulans = 4;
constexpr Color kSelectedColor{247, 125, 0};
// The empires' claims on the sectors the player has seen, measured on the
// eleventh and thirty-third turns' screenshots: a dot on every other row
// from a sector's second, where (x + (y - 1) / 2 * shift) % period is
// phase, from the sector's corner, in the empire's colour.
/** An empire's claims' dots: their pattern and colour. */
struct Claim {
    int period;
    int shift;
    int phase;
    Color color;
};
constexpr Claim kClaims[] = {
    {4, 2, 3, {115, 8, 90}}, // Cardassians
    {3, 1, 0, {16, 8, 115}}, // Federation
    {4, 2, 3, {156, 89, 0}}, // Ferengi
    {3, 1, 0, {107, 8, 8}},  // Klingons
    {4, 2, 3, {8, 89, 8}},   // Romulans
};
// A sector two empires claim is striped instead, on every row, along
// x + y: every 10 pixels, five stripes in five colours, whoever claims it
// (the one seen, the Cardassians' and Romulans').
constexpr int kContestedPeriod = 10;
/** One of a contested sector's stripes: where in the period it is, and its colour. */
struct Stripe {
    int at;
    Color color;
};
constexpr Stripe kContestedStripes[] = {
    {1, {16, 8, 115}}, {3, {107, 8, 8}}, {5, {115, 113, 8}}, {7, {115, 8, 90}}, {9, {8, 89, 8}}};

// The stellar objects' pictures on the map, by type, before the zoom's
// ending (the neutron star's is seen; the others go by their files' names).
inline const char *stellarMapPicture(int type) {
    switch (type) {
    case 0:
        return "blkh";
    case 1:
        return "xp-";
    case 2:
        return "neb-";
    case 3:
        return "ns";
    case 10:
        return "worm";
    case 11:
        return "rpls";
    default:
        return nullptr;
    }
}

// Another's task force's emblem: an empire's letter's, a minor race's
// "m", a monster's "t" (seen; the others guessed).
inline std::string otherFleetPicture(int owner) {
    constexpr char kOwners[] = {'c', 'h', 'f', 'k', 'r'};
    constexpr int kRaces = 35; // race.rst's: beyond them, monsters
    const char letter = owner >= 0 && owner < 5 ? kOwners[owner] : owner < kRaces ? 'm' : 't';
    return std::string("i_") + letter + "13.tga";
}

// The ranges' borders, measured on the screenshot: round the empire's
// systems, a sector counting as within a range when the larger of its
// column and row distances, and half the smaller (rounded down), add up to
// no more than the range (which fits every sector on the first turn's
// screenshot, from Sol).
/** A range's border: how far, and its colour. */
struct Range {
    int sectors;
    Color color;
};
constexpr Range kRanges[] = {{1, {0, 255, 0}}, {2, {255, 255, 0}}, {5, {255, 0, 0}}};
constexpr Color kTurnColor{24, 101, 189};

// The strip under the map, for the selected sector: a system's planets
// and star, or a stellar object, each turning; nothing for an empty sector.
constexpr int32_t kStrip = 1014;
constexpr int32_t kStripArea = 1020;    // where the planets go
constexpr int32_t kStripTitle = 1100;   // "SYSTEM" and its name
constexpr int32_t kStripHeading = 1101; // the race living there, or "Description"
constexpr int32_t kStripLabels = 1102;
constexpr int32_t kStripValues = 1103; // a stellar object's description
// The planets, measured on three systems' screenshots (Sol, Babel and
// Manark): the star at the area's right end, then the planets leftward
// from the innermost, 16 pixels apart, or closer where that would take the
// outermost within 21 pixels of the area's left end (Sol's, 11 apart; the
// limit is guessed); all with their middles on the area's.
constexpr int kPlanetGap = 16;
constexpr int kPlanetsLeftMargin = 21;
// Frames: a planet's 24 in 4.75 seconds, a star's 36 in about 1.2, the
// neutron star's 8 in about 1.2 (counted in recordings).
constexpr uint64_t kPlanetFrameMillis = 198;
constexpr uint64_t kStarFrameMillis = GalaxyScreen::kFastestFrameMillis;
constexpr uint64_t kStellarFrameMillis = 150;
constexpr int kTitleNameX = 53; // the system's name, after "SYSTEM"
constexpr int kStripLinePitch = 17;
// The planets' classes' letters under them, by planet type (Sol's, Babel's
// and Manark's planets; type 4's isn't seen, guessed): blue for a gas giant,
// green for a planet that's terraformed, red for one that isn't.
constexpr char kPlanetClasses[] = {'P', 'J', 'G', 'B', 'L', 'O', 'M', 'Y'};
constexpr int kGasGiant = 3;
// The planet types' words, by type (lexicon.dic's, found by their text).
/** A planet type's word: its lexicon entry, and the English. */
struct TypeWord {
    int entry;
    const char *english;
};
constexpr TypeWord kPlanetTypeWords[] = {{37, "Arctic"},  {55, "Barren"},   {229, "Desert"},  {410, "Gas Giant"},
                                         {558, "Jungle"}, {878, "Oceanic"}, {1258, "Terran"}, {1414, "Volcanic"}};

// "HUMANS" as "Humans": capitals but the first letter's.
inline std::string titleCase(std::string s) {
    for (size_t i = 1; i < s.size(); ++i)
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] = char(s[i] - 'A' + 'a');
    return s;
}
// A planet's bonus, on its frame's top from its left edge: an energy bonus on a desert or volcanic
// planet (bie.tga), a food bonus on an oceanic or terran one (bif1.tga),
// as on every planet of Sol, Babel and Manark; and the system's dilithium
// at its star's top-left corner (bid.tga, Sol's). Jungle planets' food is
// taken to show too (guessed: none is seen).
constexpr const char *kEnergyIcon = "bie.tga";
constexpr const char *kFoodIcon = "bif1.tga";
constexpr const char *kDilithiumIcon = "bid.tga";
// A system an empire holds has its emblem (i_h46.tga for the Federation's)
// at the planets' place's top-left corner, at 64% of its brightness: each
// channel of Sol's, at the 16-bit screen's 5 or 6 bits, is its picture's
// times 0.64 rounded down. The other empires' are taken by their letters
// (guessed).
constexpr int kEmblemBrightness = 64;
inline const char *bonusIcon(const SavedPlanet &p) {
    if ((p.type == 2 || p.type == 7) && p.energyBonus > 0)
        return kEnergyIcon;
    if ((p.type == 4 || p.type == 5 || p.type == 6) && p.foodBonus > 0)
        return kFoodIcon;
    return nullptr;
}
constexpr Color kClassColors[] = {{0, 0, 255}, {0, 255, 0}, {255, 0, 0}};
// Stellar objects with a strip: their type, animation and the lexicon's
// entry with their description. The neutron star's is seen; the others go
// by their files' and entries' names (guessed).
/** A stellar object's strip: its type, animation and description's lexicon entry. */
struct StellarStrip {
    int type;
    const char *animation;
    int description;
};
constexpr StellarStrip kStellarStrips[] = {{0, "blkh.ani", lex::kBlackHole + 1},
                                           {1, "xp-.ani", lex::kXRayPulsar + 1},
                                           {3, "ns.ani", lex::kNeutronStar + 1},
                                           {10, "worm.ani", lex::kWormHole + 1},
                                           {11, "rpls.ani", lex::kRadioPulsar + 1}};
constexpr Color kDescriptionColor{165, 121, 33};

// The empires' letters in their files' names, Cardassians to Romulans.
constexpr char kLetters[] = {'c', 'h', 'f', 'k', 'r'};

inline void put(Surface &s, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= s.w || y >= s.h)
        return;
    uint8_t *p = s.row(y) + size_t(x) * 4;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = 255;
}

// The map's and the strip's small pictures leave out their top-left
// pixel's colour: black, but in s-o-maps.tga, whose black the game draws
// (two stars on the screenshots).
inline void drawKeyed(Surface &s, const image::Image &img, int x, int y) {
    const uint8_t *key = img.pixel(0, 0);
    for (int py = 0; py < img.h; ++py)
        for (int px = 0; px < img.w; ++px) {
            const uint8_t *p = img.pixel(px, py);
            if (p[3] != 0 && (p[0] != key[0] || p[1] != key[1] || p[2] != key[2]))
                put(s, x + px, y + py, Color{p[0], p[1], p[2]});
        }
}

inline Error loadStrip(Assets &assets, const char *file, WindowLayout &layout) {
    std::vector<uint8_t> data;
    if (Error err = assets.archive().read(file, data))
        return err;
    if (Error err = layout.load(data))
        return Error::fail(std::string(file) + ": " + err.message);
    return Error::success();
}

inline const StellarStrip *stellarStrip(int type) {
    for (const StellarStrip &s : kStellarStrips)
        if (s.type == type)
            return &s;
    return nullptr;
}

} // namespace reluva::botf::galaxy
