// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/galaxy_screen.h"

#include "engines/botf/formats/planet_populations.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <set>
#include <string>

namespace reluva::botf {

namespace {

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
struct Stripe {
    int at;
    Color color;
};
constexpr Stripe kContestedStripes[] = {
    {1, {16, 8, 115}}, {3, {107, 8, 8}}, {5, {115, 113, 8}}, {7, {115, 8, 90}}, {9, {8, 89, 8}}};

// The stellar objects' pictures on the map, by type, before the zoom's
// ending (the neutron star's is seen; the others go by their files' names).
const char *stellarMapPicture(int type) {
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
std::string otherFleetPicture(int owner) {
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
struct TypeWord {
    int entry;
    const char *english;
};
constexpr TypeWord kPlanetTypeWords[] = {{37, "Arctic"},  {55, "Barren"},   {229, "Desert"},  {410, "Gas Giant"},
                                         {558, "Jungle"}, {878, "Oceanic"}, {1258, "Terran"}, {1414, "Volcanic"}};

// "HUMANS" as "Humans": capitals but the first letter's.
std::string titleCase(std::string s) {
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
const char *bonusIcon(const SavedPlanet &p) {
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

void put(Surface &s, int x, int y, Color c) {
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
void drawKeyed(Surface &s, const image::Image &img, int x, int y) {
    const uint8_t *key = img.pixel(0, 0);
    for (int py = 0; py < img.h; ++py)
        for (int px = 0; px < img.w; ++px) {
            const uint8_t *p = img.pixel(px, py);
            if (p[3] != 0 && (p[0] != key[0] || p[1] != key[1] || p[2] != key[2]))
                put(s, x + px, y + py, Color{p[0], p[1], p[2]});
        }
}

Error loadStrip(Assets &assets, const char *file, WindowLayout &layout) {
    std::vector<uint8_t> data;
    if (Error err = assets.archive().read(file, data))
        return err;
    if (Error err = layout.load(data))
        return Error::fail(std::string(file) + ": " + err.message);
    return Error::success();
}

const StellarStrip *stellarStrip(int type) {
    for (const StellarStrip &s : kStellarStrips)
        if (s.type == type)
            return &s;
    return nullptr;
}

} // namespace

Error GalaxyScreen::load(int empire) {
    _empire = std::clamp(empire, 0, 4);
    _letter = kLetters[_empire];
    const std::string l(1, _letter);
    _hidden = {kScrollUp, kScrollDown, kScrollBar, kTaskForcesTab};
    if (Error err = loadLayouts((l + "maingal.wdf").c_str(),
                                {kMap, kTurnText, kMilitary, kPanel, kStrip, kMapButton, kZoomPlace}, false))
        return err;
    // The strips are drawn here, not as panels: their ids are the main
    // layout's too.
    const Placed *strip = find(kStrip);
    if (Error err = loadStrip(_assets, (l + "_sysmap.wdf").c_str(), _systemLayout))
        return err;
    if (Error err = loadStrip(_assets, (l + "_starmp.wdf").c_str(), _stellarLayout))
        return err;
    for (auto [layout, placed] :
         {std::pair{&_systemLayout, &_systemStrip}, std::pair{&_stellarLayout, &_stellarStrip}}) {
        *placed = layout->placed();
        for (WindowLayout::Placed &w : *placed) {
            w.x += strip->x;
            w.y += strip->y;
        }
    }
    for (int32_t id : {kStripArea, kStripTitle, kStripHeading, kStripLabels, kStripValues})
        if (!stripWidget(_systemStrip, id))
            return Error::fail(l + "_sysmap.wdf has no widget " + std::to_string(id));
    for (int32_t id : {kStripArea, kStripTitle, kStripHeading, kStripValues})
        if (!stripWidget(_stellarStrip, id))
            return Error::fail(l + "_starmp.wdf has no widget " + std::to_string(id));
    if (Error err = addPanel((l + "infobox.wdf").c_str(), kPanel, {kInfoText}, _infoPanel))
        return err;
    if (Error err = addPanel((l + "_eicon.wdf").c_str(), kPanel, {}, _emblemsPanel, 0, kEmblemsY))
        return err;
    if (Error err = addPanel((l + "inf_tf.wdf").c_str(), kPanel, {}, _taskForcesPanel, 0, kTaskForcesY))
        return err;
    if (Error err = addPanel((l + "_tfinfo.wdf").c_str(), kPanel, {kRowIcon, kRowText}, _taskForceRow, 0, kFirstRowY))
        return err;
    // The task forces' bar's backing picture is its window's.
    _taskForcesBack = _panels[size_t(_taskForcesPanel)]->layout.root().background;
    if (Error err = addPanel((l + "_zoom.wdf").c_str(), kZoomPlace, {kSmallMap, kLargeMap}, _zoomPanel))
        return err;
    drawPanelOver(_zoomPanel);
    showPanel(_infoPanel, true);
    showPanel(_emblemsPanel, true);
    // The ships' class names and the races' names; without the tables, the
    // task forces' rows and the strip have none.
    std::vector<uint8_t> data;
    for (const TableLayout &t : tableLayouts()) {
        if (std::string(t.file) == "shiplist.sst" && !_assets.archive().read("shiplist.sst", data))
            (void)_shipClasses.load(t, data);
        if (std::string(t.file) == "race.rst" && !_assets.archive().read("race.rst", data))
            (void)_races.load(t, data);
    }
    // The planets' maximum populations; without the file, 0.
    if (!_assets.archive().read("planspac.bin", data))
        (void)_planetPopulations.load(data);
    return Error::success();
}

void GalaxyScreen::open(GameState state) {
    _state = std::move(state);
    _empire = _state.player();
    // The ranges are round the empire's systems; the first of them is
    // selected at the start (Sol, the only one on the first turn).
    _rangeCentres.clear();
    for (int y = 0; y < game().galaxy.height; ++y)
        for (int x = 0; x < game().galaxy.width; ++x)
            if (const int sys = game().galaxy.at(x, y).system;
                sys >= 0 && game().galaxy.systems[size_t(sys)].controllingRace == _empire)
                _rangeCentres.push_back({x, y});
    // And round its outposts (the thirty-fourth turn's green ranges are round
    // Sol and two of the Federation's outposts).
    for (const SavedOutpost &o : game().galaxy.outposts)
        if (o.owner == _empire && game().galaxy.width > 0)
            _rangeCentres.push_back({o.sector % game().galaxy.width, o.sector / game().galaxy.width});
    if (_rangeCentres.empty())
        select(-1, -1);
    else
        select(_rangeCentres.front().first, _rangeCentres.front().second);
}

void GalaxyScreen::select(int x, int y) {
    const SavedGalaxy &g = game().galaxy;
    const bool on = x >= 0 && y >= 0 && x < g.width && y < g.height;
    _selected = on ? std::pair{x, y} : std::pair{-1, -1};
    _pointedPlanet = -1;
    _planets.clear();
    _sun = {};
    if (!on)
        return;
    auto read = [&](const std::string &file, Animation &a) {
        std::vector<uint8_t> data;
        // Without its file, a planet is left out.
        if (_assets.archive().read(file, data) || readAnimation(data, a))
            a = {};
    };
    if (const int s = g.at(x, y).system; s >= 0) {
        const SavedSystem &sys = g.systems[size_t(s)];
        for (const SavedPlanet &p : sys.planets)
            read(p.animation, _planets.emplace_back());
        read(sys.starAnimation + ".ani", _sun);
    } else if (const int o = g.at(x, y).stellarObject; o >= 0) {
        if (const StellarStrip *strip = stellarStrip(g.stellarObjects[size_t(o)].type))
            read(strip->animation, _sun);
    }
}

bool GalaxyScreen::scanned(int x, int y) const {
    const SavedGalaxy &g = game().galaxy;
    return x >= 0 && y >= 0 && x < g.width && y < g.height && g.at(x, y).scanStrength[size_t(_empire)] > 0;
}

bool GalaxyScreen::revealed(int x, int y) const {
    const SavedGalaxy &g = game().galaxy;
    if (x < 0 || y < 0 || x >= g.width || y >= g.height)
        return false;
    const SavedSector &s = g.at(x, y);
    return ((s.revealed | s.explored) >> _empire & 1) != 0;
}

bool GalaxyScreen::view(int &x0, int &y0, int &x1, int &y1) const {
    const Placed *map = find(kMap);
    if (!map || game().galaxy.width <= 0)
        return false;
    // The zoomed-out grid, lines included, within the map's place.
    const int gx = map->x + (map->widget->w - game().galaxy.width * kOutSector) / 2;
    const int gy = map->y + (map->widget->h - game().galaxy.height * kOutSector) / 2;
    x0 = std::max(gx, map->x);
    y0 = std::max(gy, map->y);
    x1 = std::min(gx + game().galaxy.width * kOutSector, map->x + map->widget->w - 1);
    y1 = std::min(gy + game().galaxy.height * kOutSector, map->y + map->widget->h - 1);
    return true;
}

bool GalaxyScreen::gridCorner(int &gx, int &gy) const {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!view(x0, y0, x1, y1))
        return false;
    const SavedGalaxy &g = game().galaxy;
    const int sector = kZooms[_zoom].sector;
    // Zoomed out, the grid is the view. Zoomed in, the corner of the sector
    // selected when zooming in is at the view's middle (Sol's on the
    // screenshot, at 511), the grid kept over the whole view (there, at the
    // galaxy's top); selecting another doesn't move it (a screenshot with
    // Babel selected has Sol's place).
    if (_zoom == 0) {
        const Placed *map = find(kMap);
        gx = map->x + (map->widget->w - g.width * sector) / 2;
        gy = map->y + (map->widget->h - g.height * sector) / 2;
        return true;
    }
    const int sx = std::max(_zoomCentre.first, 0);
    const int sy = std::max(_zoomCentre.second, 0);
    auto place = [&](int middle, int s, int first, int last, int sectors) {
        const int at = middle - s * sector;
        return std::clamp(at, std::min(first, last - sectors * sector), first);
    };
    gx = place((x0 + x1) / 2, sx, x0, x1, g.width);
    gy = place((y0 + y1) / 2, sy, y0, y1, g.height);
    return true;
}

void GalaxyScreen::setZoomedIn(bool in) {
    if (in && _zoom == 0)
        _zoomCentre = _selected;
    _zoom = in ? 1 : 0;
}

std::pair<int, int> GalaxyScreen::sectorAt(int x, int y) const {
    int gx = 0;
    int gy = 0;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!view(x0, y0, x1, y1) || !gridCorner(gx, gy) || x < x0 || y < y0 || x > x1 || y > y1 || x < gx || y < gy)
        return {-1, -1};
    const int sector = kZooms[_zoom].sector;
    const int sx = (x - gx) / sector;
    const int sy = (y - gy) / sector;
    if (sx >= game().galaxy.width || sy >= game().galaxy.height)
        return {-1, -1};
    return {sx, sy};
}

bool GalaxyScreen::click(int x, int y) {
    const auto [sx, sy] = sectorAt(x, y);
    if (sx < 0)
        return false;
    select(sx, sy);
    return true;
}

bool GalaxyScreen::handle(const Event &e) {
    // The button under the pointer pulses, as the main menu's (the Map
    // button and the zoom's in a recording; the others taken alike), but
    // for the lit ones.
    trackPointer(e);
    const int32_t pointedButton = buttonAt(e.mouseX, e.mouseY);
    const bool lit = pointedButton == kMilitary || pointedButton == (_zoom == 1 ? kSmallMap : kLargeMap);
    if (e.type == EventType::kMouseMove || e.type == EventType::kMouseDown)
        setPulsing(lit ? 0 : pointedButton);
    // The sector under the pointer, named over the map.
    bool changed = false;
    if (e.type == EventType::kMouseMove || e.type == EventType::kMouseDown) {
        const std::pair<int, int> under = sectorAt(e.mouseX, e.mouseY);
        const int planet = planetAt(e.mouseX, e.mouseY);
        changed = under != _pointed || planet != _pointedPlanet;
        _pointed = under;
        _pointedPlanet = planet;
    }
    if (e.type != EventType::kMouseDown || e.button != MouseButton::kLeft)
        return changed;
    const bool zoomOpen = panelShown(_zoomPanel);
    switch (buttonAt(e.mouseX, e.mouseY)) {
    case kMapButton:
        showPanel(_zoomPanel, !zoomOpen);
        return true;
    case kSmallMap:
    case kLargeMap:
        setZoomedIn(buttonAt(e.mouseX, e.mouseY) == kSmallMap);
        showPanel(_zoomPanel, false);
        return true;
    default:
        break;
    }
    // A click elsewhere closes the zoom's buttons (guessed).
    showPanel(_zoomPanel, false);
    return click(e.mouseX, e.mouseY) || zoomOpen || changed;
}

int GalaxyScreen::eventCount() const {
    return _state.eventCount(_empire);
}

void GalaxyScreen::drawSectorLine(Surface &screen) const {
    const auto [sx, sy] = _pointed;
    const Placed *box = find(kSectorText);
    if (sx < 0 || !box)
        return;
    const SavedSector &here = game().galaxy.at(sx, sy);
    // Its name: its column's letter, small, and its row from 1 ("i6").
    const std::string name = char('a' + sx) + std::to_string(sy + 1);
    int claims = 0;
    for (uint16_t c = here.claims; c != 0; c &= uint16_t(c - 1))
        ++claims;
    std::string text;
    if (!(here.explored >> _empire & 1))
        text = word(lex::kUnexploredSector, "Unexplored Sector %s");
    else if (claims == 0)
        text = word(lex::kUnclaimedSector, "Unclaimed Sector %s");
    else if (claims > 1)
        text = word(lex::kContestedSector, "Contested Sector %s");
    else
        // The claiming empire's adjective: "Romulan Sector d1", "Cardassian
        // Sector c6".
        for (int e = 0; e < int(std::size(kAdjectives)); ++e)
            if (here.claims >> e & 1)
                text = word(kAdjectives[e].entry, kAdjectives[e].english) + " " + word(lex::kSector, "Sector %s");
    if (const size_t at = text.find("%s"); at != std::string::npos)
        text.replace(at, 2, name);
    drawText(screen, kSectorText, text, kTurnColor);
    // The empire's scan strength there, ending 6 pixels short of the box's right.
    const int strength = here.scanStrength[size_t(_empire)];
    std::string scan = word(lex::kScanStrength, "Scan Strength %d");
    if (const size_t at = scan.find("%d"); at != std::string::npos)
        scan.replace(at, 2, std::to_string(strength));
    const Widget &w = *box->widget;
    if (const Font *font = _assets.font(w.fonts.empty() ? "" : w.fonts[0].file))
        drawTextAt(screen, w, box->x + w.w - kScanRightMargin - font->measure(scan) - w.textX, box->y, scan,
                   strength > 0 ? kScanColor : kHiddenScanColor);
}

void GalaxyScreen::draw(Surface &screen) const {
    // The zoom's buttons: the current zoom's lit (a recording: Large Map
    // zoomed out, Small Map zoomed in); the Map button isn't lit while
    // they're open.
    const std::set<int32_t> lit{kMilitary, _zoom == 1 ? kSmallMap : kLargeMap};
    drawLayouts(screen, lit);
    drawMap(screen);
    drawPanelsOver(screen, lit);
    drawSectorLine(screen);
    drawPanel(screen);
    drawStrip(screen);
    drawText(screen, kTurnText, upperAscii(word(lex::kTurn, "Turn")) + " " + std::to_string(game().summary.turn + 1),
             kTurnColor);
}

void GalaxyScreen::drawIcon(Surface &screen, int x, int y, const Widget &w, const std::string &file) const {
    if (const image::Image *img = _assets.picture(file))
        image::blit(screen, *img, x + (w.w - img->w) / 2, y);
}

void GalaxyScreen::drawTaskForcesBar(Surface &screen) const {
    const Placed *button = find(kTaskForcesButton);
    const Placed *panel = find(kPanel);
    if (!button || !panel)
        return;
    const int x = panel->x;
    const int y = panel->y + kTaskForcesY;
    const std::string &back = _taskForcesBack;
    if (const image::Image *img = back.empty() ? nullptr : _assets.picture(back))
        for (int py = 0; py < img->h; ++py)
            for (int px = 0; px < img->w; ++px) {
                const uint8_t *p = img->pixel(px, py);
                if ((p[0] | p[1] | p[2]) && p[3])
                    put(screen, x + px, y + py, Color{p[0], p[1], p[2]});
            }
    for (const auto &[what, file] : button->widget->pictures)
        if (what == "normal")
            drawPicture(screen, kTaskForcesButton, file);
    drawPicture(screen, kTaskForcesTab, kTaskForcesTabPicture);
}

void GalaxyScreen::drawPanel(Surface &screen) const {
    const SavedEmpire &e = game().empires[size_t(_empire)];
    // The empire's information: a label at the left of each line, its
    // figure at the right; dilithium and events share the last line.
    if (const Placed *box = find(kInfoText))
        if (const Font *font = _assets.font(box->widget->fonts.empty() ? "" : box->widget->fonts[0].file)) {
            const int right = box->x + box->widget->w - kFigureMargin;
            auto line = [&](int i, const std::string &left, Color leftColor, const std::string &figure,
                            Color figureColor) {
                const int y = box->y + 1 + i * kLinePitch;
                font->drawInColor(screen, box->x, y, left, leftColor);
                font->drawInColor(screen, right - font->measure(figure), y, figure, figureColor);
            };
            auto withSign = [](int v) { return (v >= 0 ? "+" : "") + std::to_string(v); };
            line(0, word(lex::kTotalCredits, "TOTAL CREDITS"), kLabelColor, std::to_string(e.credits), kFigureColor);
            line(1, word(lex::kCreditsPerTurn, "CREDITS PER TURN"), kLabelColor, withSign(e.creditsPerTurn),
                 kFigureColor);
            // The dilithium's figure first, in the figures' colour (but 0 in
            // the words', unsigned), and its word 6 pixels after it (so
            // after "+1" and "0" on screenshots); then how many of the last
            // turn's results the empire has, in the figures' colour, or "No
            // Events" in the words'.
            const int events = eventCount();
            if (events == 0)
                line(2, "", kLabelColor, word(lex::kNoEvents, "No Events"), kLabelColor);
            else
                line(2, "", kLabelColor,
                     std::to_string(events) + " " +
                         (events == 1 ? word(lex::kEvent, "Event") : word(lex::kEvents, "Events")),
                     kFigureColor);
            const std::string dilithium = e.dilithiumPerTurn == 0 ? "0" : withSign(e.dilithiumPerTurn);
            font->drawInColor(screen, box->x, box->y + 1 + 2 * kLinePitch, dilithium,
                              e.dilithiumPerTurn == 0 ? kLabelColor : kFigureColor);
            font->drawInColor(screen, box->x + font->measure(dilithium) + kDilithiumGap, box->y + 1 + 2 * kLinePitch,
                              upperAscii(word(lex::kDilithium, "Dilithium")), kLabelColor);
        }
    drawTaskForcesBar(screen);
    // The other empires' emblems, each in its place.
    for (int other = 0; other < 5; ++other)
        if (const Placed *p = other == _empire ? nullptr : find(kFirstEmblem + other))
            drawIcon(screen, p->x, p->y, *p->widget, std::string("i_") + kLetters[other] + "26.tga");
    // A row for each of the empire's outposts in the selected sector, then
    // each task force there (the thirty-first turn's lists the one of four
    // at Deneb, the seventeenth's a Klingon scout with the Federation's):
    // its emblem across its place, and two lines.
    const Placed *icon = find(kRowIcon);
    const Placed *text = find(kRowText);
    const Placed *panel = find(kPanel);
    if (!icon || !text || !panel || _selected.first < 0)
        return;
    const Font *font = _assets.font(text->widget->fonts.empty() ? "" : text->widget->fonts[0].file);
    int row = 0;
    auto drawRow = [&](const std::string &emblem, const std::string &first, const std::string &second) {
        const int dy = row++ * kRowHeight;
        if (icon->y + dy >= panel->y + panel->widget->h)
            return;
        drawIcon(screen, icon->x, icon->y + dy, *icon->widget, emblem);
        if (!font)
            return;
        font->drawInColor(screen, text->x, text->y + dy + 1, first, kTaskForceColor);
        font->drawInColor(screen, text->x, text->y + dy + 1 + kLinePitch, second, kTaskForceColor);
    };
    // An outpost: its emblem (i_ho30.tga), "Outpost" and "OPERATIONAL" (one
    // seen; one being built shows how far it's got, not read yet).
    for (const SavedOutpost *o : _state.outpostsAt(_selected.first, _selected.second))
        if (o->owner == _empire)
            drawRow(std::string("i_") + _letter + "o30.tga", word(lex::kOutpost, "Outpost"),
                    word(lex::kOperational, "OPERATIONAL"));
    // A task force: the empire's own, its mission and, where it's headed
    // elsewhere, how long it'll take ("ENGAGE eta 2 TURNS"), then its first
    // ship's class; another empire's, its first ship's class alone, in the
    // same colour (guessed: the one seen is selected, its line orange).
    for (const SavedTaskForce *tf : _state.taskForcesAt(_selected.first, _selected.second)) {
        const SavedTaskForce &t = *tf;
        if (t.owner < 0 || t.owner >= int(std::size(kLetters)))
            continue;
        std::string ship;
        if (const SavedShip *first = t.shipIds.empty() ? nullptr : _state.ship(t.shipIds.front());
            first && size_t(first->shipClass) < _shipClasses.count())
            ship = _shipClasses.name(size_t(first->shipClass));
        const std::string emblem = std::string("i_") + kLetters[t.owner] + "26.tga";
        if (t.owner != _empire) {
            drawRow(emblem, ship, "");
            continue;
        }
        std::string orders = missionWord(t.mission);
        if (const int turns = etaTurns(t); turns > 0) {
            orders += (orders.empty() ? "" : " ") + word(lex::kEta, "eta") + " " + std::to_string(turns) + " " +
                      (turns == 1 ? std::string("TURN") : word(lex::kTurns, "TURNS"));
        }
        drawRow(emblem, orders, ship);
    }
}

int GalaxyScreen::etaTurns(const SavedTaskForce &t) const {
    // The sectors to its destination, the larger of the two distances, over
    // its slowest ship's speed (shiplist.sst's), rounded up: 2 for a science
    // vessel (speed 1) two rows away, the one seen.
    if (t.destinationX < 0 || (t.destinationX == t.x && t.destinationY == t.y))
        return 0;
    double speed = 0;
    for (const int id : t.shipIds)
        if (const SavedShip *s = _state.ship(id); s && size_t(s->shipClass) < _shipClasses.count()) {
            double v = 0;
            std::memcpy(&v, _shipClasses.record(size_t(s->shipClass)) + kShipSpeedOffset, sizeof v);
            speed = speed == 0 ? v : std::min(speed, v);
        }
    if (speed <= 0)
        return 0;
    const int sectors = std::max(std::abs(t.destinationX - t.x), std::abs(t.destinationY - t.y));
    return int(std::ceil(sectors / speed));
}

void GalaxyScreen::drawRanges(Surface &screen, int gx, int gy) const {
    // Each range's border: a line along every sector edge between a sector
    // in it and one out of it (or the grid's edge), the shorter ranges over
    // the longer.
    const SavedGalaxy &g = game().galaxy;
    const int sector = kZooms[_zoom].sector;
    for (size_t r = std::size(kRanges); r-- > 0;) {
        auto in = [&](int x, int y) {
            // Not into the sectors of an empire whose territory it has no
            // access to (the thirty-fourth turn's stop at the Cardassians'
            // and Romulans', with whom the Federation has non-aggression
            // treaties, and run into the Klingons' and Ferengi's).
            if (!_state.mayEnter(_empire, x, y))
                return false;
            for (const auto &[cx, cy] : _rangeCentres) {
                const int dx = std::abs(x - cx);
                const int dy = std::abs(y - cy);
                if (std::max(dx, dy) + std::min(dx, dy) / 2 <= kRanges[r].sectors)
                    return true;
            }
            return false;
        };
        for (int y = 0; y <= g.height; ++y)
            for (int x = 0; x <= g.width; ++x) {
                // Along the galaxy's top edge, a pixel lower: inside the
                // grid's line (both screenshots with a range reaching it).
                if (in(x, y) != in(x, y - 1) && x < g.width)
                    for (int k = 0; k <= sector; ++k)
                        put(screen, gx + x * sector + k, gy + y * sector + (y == 0 ? 1 : 0), kRanges[r].color);
                if (in(x, y) != in(x - 1, y) && y < g.height)
                    for (int k = 0; k <= sector; ++k)
                        put(screen, gx + x * sector, gy + y * sector + k, kRanges[r].color);
            }
    }
}

std::string GalaxyScreen::missionWord(int mission) const {
    // GWTForce's missions (published) and lexicon.dic's words for them:
    // AVOID and ENGAGE seen; none for a plain move.
    struct Mission {
        int mission;
        int entry;
        const char *english;
    };
    static constexpr Mission kMissions[] = {
        {1, 663, "TERRAFORM PLANET"}, {2, 656, "COLONIZE SYSTEM"}, {3, 661, "RAID"},
        {5, lex::kAvoid, "AVOID"},    {6, lex::kEngage, "ENGAGE"}, {7, 653, "BUILD OUTPOST"},
        {8, 654, "BUILD STARBASE"},   {9, 664, "TRAIN CREW"},      {12, 662, "SCRAP"},
        {13, 657, "ENTER WORMHOLE"},  {14, 651, "ATTACK SYSTEM"}};
    for (const Mission &m : kMissions)
        if (m.mission == mission)
            return word(m.entry, m.english);
    return {};
}

void GalaxyScreen::drawMap(Surface &screen) const {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    int gx = 0;
    int gy = 0;
    if (!view(x0, y0, x1, y1) || !gridCorner(gx, gy))
        return;
    // Drawn on a surface the view's size, which cuts off what falls outside
    // it, over what the screen has there.
    Surface view;
    view.create(x1 - x0 + 1, y1 - y0 + 1, PixelFormat::kRGBA32);
    for (int y = 0; y < view.h; ++y)
        std::copy_n(screen.row(y0 + y) + size_t(x0) * 4, size_t(view.w) * 4, view.row(y));
    drawGrid(view, gx - x0, gy - y0);
    for (int y = 0; y < view.h; ++y)
        std::copy_n(view.row(y), size_t(view.w) * 4, screen.row(y0 + y) + size_t(x0) * 4);
}

void GalaxyScreen::drawClaims(Surface &screen, uint16_t claims, int left, int top, int sector) const {
    std::vector<int> claimants;
    for (int e = 0; e < int(std::size(kClaims)); ++e)
        if (claims >> e & 1)
            claimants.push_back(e);
    if (claimants.size() == 1) {
        const Claim &c = kClaims[claimants.front()];
        for (int y = 1; y < sector; y += 2)
            for (int x = 1; x < sector; ++x)
                if ((x + (y - 1) / 2 * c.shift) % c.period == c.phase)
                    put(screen, left + x, top + y, c.color);
    } else if (claimants.size() > 1) {
        for (int y = 1; y < sector; ++y)
            for (int x = 1; x < sector; ++x)
                for (const Stripe &stripe : kContestedStripes)
                    if ((x + y) % kContestedPeriod == stripe.at)
                        put(screen, left + x, top + y, stripe.color);
    }
}

void GalaxyScreen::drawGrid(Surface &screen, int gx, int gy) const {
    const Zoom &z = kZooms[_zoom];
    const int sector = z.sector;
    const int width = game().galaxy.width * sector;
    const int height = game().galaxy.height * sector;
    const SavedGalaxy &g = game().galaxy;
    // The backdrop, from the view's corner whichever the zoom, only inside
    // the sectors the empire hasn't seen: the grid's lines and the sectors
    // seen show the screen's background (hm_scrn.tga), as on the
    // screenshots, every pixel of them.
    const image::Image *backdrop = _assets.picture(std::string(1, _letter) + "m_glmap.tga");
    for (int sy = 0; sy < g.height; ++sy)
        for (int sx = 0; sx < g.width; ++sx) {
            const int left = gx + sx * sector;
            const int top = gy + sy * sector;
            if (!revealed(sx, sy)) {
                if (!backdrop)
                    continue;
                for (int y = std::max(top + 1, 0); y < std::min({top + sector, screen.h, backdrop->h}); ++y)
                    for (int x = std::max(left + 1, 0); x < std::min({left + sector, screen.w, backdrop->w}); ++x) {
                        const uint8_t *p = backdrop->pixel(x, y);
                        put(screen, x, y, Color{p[0], p[1], p[2]});
                    }
                continue;
            }
            drawClaims(screen, g.at(sx, sy).claims, left, top, sector);
        }
    // The lines next to a sector the scanners reach now are dotted: every
    // other pixel from each sector's corner.
    for (int row = 0; row <= g.height; ++row)
        for (int x = std::max(gx, 0); x < std::min(gx + width, screen.w); ++x)
            if ((x - gx) % sector % 2 == 0 && (scanned((x - gx) / sector, row) || scanned((x - gx) / sector, row - 1)))
                put(screen, x, gy + row * sector, kScannedGridColor);
    for (int col = 0; col <= g.width; ++col)
        for (int y = std::max(gy, 0); y < std::min(gy + height, screen.h); ++y)
            if ((y - gy) % sector % 2 == 0 && (scanned(col, (y - gy) / sector) || scanned(col - 1, (y - gy) / sector)))
                put(screen, gx + col * sector, y, kScannedGridColor);
    // The stars and other objects the empire has seen, with the names of
    // those it has explored under them (the first turn shows Sol's, not its
    // neighbours'; the eleventh, every explored sector's). An object's
    // picture is centred where a star's would be.
    const Font *font = _assets.font("human9.fnt");
    const image::Image *starSized = _assets.picture(std::string("s-y-") + z.starSuffix);
    for (int sy = 0; sy < g.height; ++sy)
        for (int sx = 0; sx < g.width; ++sx) {
            if (!revealed(sx, sy))
                continue;
            const SavedSector &here = g.at(sx, sy);
            const image::Image *picture = nullptr;
            std::string name;
            int mapX = 0;
            int mapY = 0;
            bool own = false;
            int owner = -1;
            if (here.system >= 0) {
                const SavedSystem &sys = g.systems[size_t(here.system)];
                picture = _assets.picture(sys.starAnimation + z.starSuffix);
                name = sys.name;
                mapX = sys.mapX;
                mapY = sys.mapY;
                own = sys.controllingRace == _empire;
                owner = sys.controllingRace;
            } else if (here.stellarObject >= 0) {
                const SavedStellarObject &o = g.stellarObjects[size_t(here.stellarObject)];
                if (const char *file = stellarMapPicture(o.type))
                    picture = _assets.picture(file + std::string(z.stellarSuffix));
                name = o.name;
                mapX = o.mapX;
                mapY = o.mapY;
            }
            if (!picture)
                continue;
            const int slot = starSized ? starSized->w : picture->w;
            int x = gx + sx * sector + z.starStep * (mapX - 5 * sx) + z.starOffset + (slot - picture->w) / 2;
            int y = gy + sy * sector + z.starStep * (mapY - 5 * sy) + z.starOffset + (slot - picture->h) / 2;
            drawKeyed(screen, *picture, x, y);
            if (!font || !(here.explored >> _empire & 1))
                continue;
            // Centred on the picture less a pixel, rounding down: every name
            // on both zooms' screenshots, of odd widths and even.
            const int spare = picture->w - 1 - font->measure(name);
            const int nameX = x + (spare - (spare < 0 && spare % 2 != 0 ? 1 : 0)) / 2;
            font->drawInColor(screen, nameX, y + picture->h, name,
                              own                  ? kNameColor
                              : owner == kRomulans ? kRomulanNameColor
                                                   : kOtherNameColor);
        }
    // The selected system: an orange square just inside its sector's lines,
    // under the ranges (at the galaxy's top edge, the range covers its top).
    if (_selected.first >= 0)
        for (int k = 1; k < sector; ++k) {
            const int x0 = gx + _selected.first * sector;
            const int y0 = gy + _selected.second * sector;
            put(screen, x0 + k, y0 + 1, kSelectedColor);
            put(screen, x0 + k, y0 + sector - 1, kSelectedColor);
            put(screen, x0 + 1, y0 + k, kSelectedColor);
            put(screen, x0 + sector - 1, y0 + k, kSelectedColor);
        }
    drawRanges(screen, gx, gy);
    // The empire's outposts: its outpost emblem (i_ho11.tga) a pixel right
    // of the sector's bottom-left corner, at its foot (the Federation's two
    // at the thirty-fourth turn); the other empires' aren't shown.
    if (const image::Image *icon = _assets.picture(std::string("i_") + _letter + "o11.tga"))
        for (const SavedOutpost &o : g.outposts)
            if (o.owner == _empire && g.width > 0)
                image::blit(screen, *icon, gx + o.sector % g.width * sector + 1,
                            gy + o.sector / g.width * sector + sector - icon->h);
    // Where the empire's task forces are ordered to: a marker in the middle
    // of the sector.
    if (const image::Image *mark = _assets.picture("movs.tga"))
        for (const SavedTaskForce &t : game().taskForces)
            if (t.owner == _empire && t.destinationX >= 0 && (t.destinationX != t.x || t.destinationY != t.y))
                image::blit(screen, *mark, gx + t.destinationX * sector + (sector - mark->w) / 2,
                            gy + t.destinationY * sector + (sector - mark->h) / 2);
    // Task forces: the empire's emblem at their sector's corner, one for the
    // sector however many there are; the others' where its scanners reach
    // them, their small emblem a pixel right of and below the corner.
    std::set<std::pair<int, int>> drawn;
    for (const SavedTaskForce &t : game().taskForces) {
        if (!drawn.insert({t.x, t.y}).second)
            continue;
        if (t.owner == _empire) {
            if (const image::Image *icon = _assets.picture(std::string("i_") + _letter + z.fleet))
                image::blit(screen, *icon, gx + t.x * sector + z.fleetX, gy + t.y * sector + z.fleetY);
        } else if (scanned(t.x, t.y)) {
            if (const image::Image *icon = _assets.picture(otherFleetPicture(t.owner)))
                image::blit(screen, *icon, gx + t.x * sector + 1, gy + t.y * sector + 1);
        }
    }
}

const WindowLayout::Placed *GalaxyScreen::stripWidget(const std::vector<WindowLayout::Placed> &strip,
                                                      int32_t id) const {
    for (const WindowLayout::Placed &w : strip)
        if (w.depth > 0 && w.widget->id == id)
            return &w;
    return nullptr;
}

void GalaxyScreen::drawFrame(Surface &screen, const Animation &a, uint64_t frameMillis, int cx, int cy) const {
    if (a.frames.empty())
        return;
    const image::Image &f = a.frames[size_t(_now / frameMillis % a.frames.size())];
    image::blit(screen, f, cx - f.w / 2, cy - f.h / 2);
}

void GalaxyScreen::drawStrip(Surface &screen) const {
    if (_selected.first < 0)
        return;
    const SavedSector &sector = game().galaxy.at(_selected.first, _selected.second);
    if (sector.system >= 0)
        drawSystemStrip(screen, game().galaxy.systems[size_t(sector.system)]);
    else if (sector.stellarObject >= 0 && stellarStrip(game().galaxy.stellarObjects[size_t(sector.stellarObject)].type))
        drawStellarStrip(screen, game().galaxy.stellarObjects[size_t(sector.stellarObject)]);
}

std::vector<int> GalaxyScreen::planetCentres(int &sunCx, int &cy) const {
    // The star at the planets' place's right end, then the planets leftward
    // from it.
    const WindowLayout::Placed &area = *stripWidget(_systemStrip, kStripArea);
    cy = area.y + area.widget->h / 2;
    const int sunW = _sun.frameWidth;
    sunCx = area.x + area.widget->w - sunW + sunW / 2;
    auto centres = [&](int gap) {
        std::vector<int> out;
        int cx = sunCx;
        int w = sunW;
        for (const Animation &p : _planets) {
            cx -= (w + p.frameWidth) / 2 + gap;
            w = p.frameWidth;
            out.push_back(cx);
        }
        return out;
    };
    int gap = kPlanetGap;
    std::vector<int> xs = centres(gap);
    while (gap > 0 && !xs.empty() && xs.back() - _planets.back().frameWidth / 2 < area.x + kPlanetsLeftMargin)
        xs = centres(--gap);
    return xs;
}

int GalaxyScreen::planetAt(int x, int y) const {
    if (_selected.first < 0 || game().galaxy.at(_selected.first, _selected.second).system < 0)
        return -1;
    int sunCx = 0;
    int cy = 0;
    const std::vector<int> xs = planetCentres(sunCx, cy);
    for (size_t i = 0; i < xs.size(); ++i) {
        const Animation &a = _planets[i];
        if (x >= xs[i] - a.frameWidth / 2 && x < xs[i] - a.frameWidth / 2 + a.frameWidth &&
            y >= cy - a.frameHeight / 2 && y < cy - a.frameHeight / 2 + a.frameHeight)
            return int(i);
    }
    return -1;
}

void GalaxyScreen::drawSystemStrip(Surface &screen, const SavedSystem &system) const {
    const std::vector<WindowLayout::Placed> &strip = _systemStrip;
    if (const image::Image *back = _assets.picture(_systemLayout.root().background))
        image::blit(screen, *back, strip.front().x, strip.front().y);
    const WindowLayout::Placed &area = *stripWidget(strip, kStripArea);
    int cy = 0;
    int sunCx = 0;
    const std::vector<int> xs = planetCentres(sunCx, cy);
    const int sunW = _sun.frameWidth;
    if (system.controllingRace >= 0 && system.controllingRace < int(std::size(kLetters)))
        if (const image::Image *emblem =
                _assets.dimmed(std::string("i_") + kLetters[system.controllingRace] + "46.tga", kEmblemBrightness))
            image::blit(screen, *emblem, area.x, area.y);
    drawFrame(screen, _sun, kStarFrameMillis, sunCx, cy);
    if (const image::Image *icon = system.dilithium > 0 ? _assets.picture(kDilithiumIcon) : nullptr)
        drawKeyed(screen, *icon, sunCx - sunW / 2, area.y);
    const Font *small = _assets.font("human9.fnt");
    for (size_t i = 0; i < _planets.size(); ++i) {
        const Animation &a = _planets[i];
        drawFrame(screen, a, kPlanetFrameMillis, xs[i], cy);
        const SavedPlanet &p = system.planets[i];
        if (const char *file = bonusIcon(p))
            if (const image::Image *icon = _assets.picture(file))
                drawKeyed(screen, *icon, xs[i] - a.frameWidth / 2, cy - a.frameHeight / 2 - icon->h);
        if (!small || p.type < 0 || p.type >= int(std::size(kPlanetClasses)))
            continue;
        const std::string letter(1, kPlanetClasses[p.type]);
        const Color c = p.type == kGasGiant                    ? kClassColors[0]
                        : p.terraformDone >= p.terraformNeeded ? kClassColors[1]
                                                               : kClassColors[2];
        // Centred under the frame as the map's names are (14 letters of
        // Sol's, Deneb's and Babel's agree).
        const int left = xs[i] - a.frameWidth / 2;
        const int spare = a.frameWidth - 1 - small->measure(letter);
        small->drawInColor(screen, left + (spare - (spare < 0 && spare % 2 != 0 ? 1 : 0)) / 2, cy + a.frameHeight / 2,
                           letter, c);
    }
    // The title: "SYSTEM" and its name, and who lives there.
    const WindowLayout::Placed &title = *stripWidget(strip, kStripTitle);
    drawTextAt(screen, *title.widget, title.x, title.y, upperAscii(word(lex::kSystem, "system")), kLabelColor);
    drawTextAt(screen, *title.widget, title.x + kTitleNameX, title.y, system.name, kFigureColor);
    // Who lives there: "HUMANS" for the Federation's people, a minor
    // race's name in capitals ("BANDI" at Deneb); the other empires' are
    // taken to be their names' too (guessed).
    std::string people;
    if (system.residentRace < 0)
        people = upperAscii(word(lex::kUninhabited, "uninhabited"));
    else if (system.residentRace == 1)
        people = word(lex::kHumans, "HUMANS");
    else if (size_t(system.residentRace) < _races.count())
        people = upperAscii(_races.name(size_t(system.residentRace)));
    const WindowLayout::Placed &heading = *stripWidget(strip, kStripHeading);
    drawTextAt(screen, *heading.widget, heading.x, heading.y, people, kLabelColor);
    // The labels and the figures known; the growth rate and the orbitals
    // aren't worked out yet. With the pointer over a planet, its details
    // instead.
    const WindowLayout::Placed &labels = *stripWidget(strip, kStripLabels);
    const WindowLayout::Placed &values = *stripWidget(strip, kStripValues);
    const Font *font = _assets.font(labels.widget->fonts.empty() ? "" : labels.widget->fonts[0].file);
    if (!font)
        return;
    auto line = [&](int i, const std::string &label, const std::string &figure) {
        font->drawInColor(screen, labels.x, labels.y + 1 + i * kStripLinePitch, label, kLabelColor);
        font->drawInColor(screen, values.x + values.widget->w - font->measure(figure),
                          values.y + 1 + i * kStripLinePitch, figure, kFigureColor);
    };
    if (_pointedPlanet >= 0 && size_t(_pointedPlanet) < system.planets.size()) {
        drawPlanetDetails(system, system.planets[size_t(_pointedPlanet)], line);
        return;
    }
    const std::string label[] = {word(lex::kPopulation, "Population"), word(lex::kMaxPop, "Max Pop"),
                                 word(lex::kGrowthRate, "Growth Rate"), word(lex::kOrbitals, "Orbitals")};
    const std::string figure[] = {std::to_string(system.population), std::to_string(system.maxPopulation), "", ""};
    for (int i = 0; i < 4; ++i)
        line(i, upperAscii(label[i]), figure[i]);
}

void GalaxyScreen::drawPlanetDetails(
    const SavedSystem &system, const SavedPlanet &p,
    const std::function<void(int, const std::string &, const std::string &)> &line) const {
    // Its name and its type's word ("Volcanic"), its maximum population
    // (planspac.bin's, by its type, size and bonuses: every planet of Sol
    // and Xerxes on the screenshots), its growth rate (not worked out yet),
    // then its terraforming done and needed, or once terraformed who lives
    // there: "Humans" for the Federation's people, a minor race's name.
    const std::string type = p.type >= 0 && p.type < int(std::size(kPlanetTypeWords))
                                 ? word(kPlanetTypeWords[p.type].entry, kPlanetTypeWords[p.type].english)
                                 : "";
    line(0, p.name, type);
    const int maxPop =
        _planetPopulations.maxPopulation(p.type, p.size, bonusLevel(p.growthBonus), bonusLevel(p.energyBonus));
    line(1, word(lex::kMaxPop, "Max Pop"), std::to_string(maxPop));
    line(2, word(lex::kGrowthRate, "Growth Rate"), "");
    if (p.terraformDone < p.terraformNeeded) {
        line(3, word(lex::kTerraform, "Terraform"),
             std::to_string(p.terraformDone) + " / " + std::to_string(p.terraformNeeded));
        return;
    }
    std::string people;
    if (system.residentRace == 1)
        people = titleCase(word(lex::kHumans, "HUMANS"));
    else if (system.residentRace >= 0 && size_t(system.residentRace) < _races.count())
        people = _races.name(size_t(system.residentRace));
    line(3, "", people);
}

void GalaxyScreen::drawStellarStrip(Surface &screen, const SavedStellarObject &object) const {
    const std::vector<WindowLayout::Placed> &strip = _stellarStrip;
    if (const image::Image *back = _assets.picture(_stellarLayout.root().background))
        image::blit(screen, *back, strip.front().x, strip.front().y);
    const WindowLayout::Placed &area = *stripWidget(strip, kStripArea);
    drawFrame(screen, _sun, kStellarFrameMillis, area.x + area.widget->w / 2, area.y + area.widget->h / 2);
    const WindowLayout::Placed &title = *stripWidget(strip, kStripTitle);
    drawTextAt(screen, *title.widget, title.x, title.y, object.name, kLabelColor);
    const WindowLayout::Placed &heading = *stripWidget(strip, kStripHeading);
    drawTextAt(screen, *heading.widget, heading.x, heading.y, word(lex::kDescription, "Description"), kLabelColor);
    // Wrapped to the box's width, a line every 17 pixels; the sixth line
    // goes past the box's foot, as on the neutron star's screenshot.
    const WindowLayout::Placed &text = *stripWidget(strip, kStripValues);
    const Font *font = _assets.font(text.widget->fonts.empty() ? "" : text.widget->fonts[0].file);
    if (!font)
        return;
    int y = text.y + text.widget->textY + 1;
    for (const std::string &line : font->wrap(word(stellarStrip(object.type)->description, ""), text.widget->textW)) {
        font->drawInColor(screen, text.x + text.widget->textX, y, line, kDescriptionColor);
        y += kStripLinePitch;
    }
}

} // namespace reluva::botf
