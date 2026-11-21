// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/galaxy_screen.h"

#include <algorithm>
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
    const char *starSuffix; // its picture: s-y-maps.tga, s-y-map.tga
    // The empire's emblem for its task forces, after "i_" and the letter,
    // a pixel right of its sector's corner and one above or below it.
    const char *fleet;
    int fleetX;
    int fleetY;
};
constexpr Zoom kZooms[] = {{39, 7, -3, "maps.tga", "i18.tga", 1, -1}, {61, 12, -8, "map.tga", "26.tga", 1, 1}};
constexpr int kOutSector = kZooms[0].sector;

constexpr Color kGridColor{0, 0, 8};            // the lines outside the scanners' range
constexpr Color kScannedGridColor{24, 73, 173}; // dotted, inside it
constexpr Color kNameColor{0, 251, 255};        // the empire's own systems'
// Those of systems nobody holds; the others' are taken to be the same
// (guessed: no screenshot shows one).
constexpr Color kOtherNameColor{255, 186, 57};
constexpr Color kSelectedColor{247, 125, 0};
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
    // The ships' class names; without the table, the rows have none.
    std::vector<uint8_t> data;
    for (const TableLayout &t : tableLayouts())
        if (std::string(t.file) == "shiplist.sst" && !_assets.archive().read("shiplist.sst", data))
            (void)_shipClasses.load(t, data);
    return Error::success();
}

void GalaxyScreen::open(SavedGame game) {
    _game = std::move(game);
    _empire = std::clamp(_game.summary.empire, 0, 4);
    // What the player's scanners reach: a sector around each of its
    // systems and task forces (inferred from the first turn's screenshot,
    // which shows the 3 by 3 sectors around Sol).
    _scanned.clear();
    auto around = [&](int cx, int cy) {
        for (int y = cy - 1; y <= cy + 1; ++y)
            for (int x = cx - 1; x <= cx + 1; ++x)
                if (x >= 0 && y >= 0 && x < _game.galaxy.width && y < _game.galaxy.height)
                    _scanned.insert({x, y});
    };
    for (int y = 0; y < _game.galaxy.height; ++y)
        for (int x = 0; x < _game.galaxy.width; ++x)
            if (const int s = _game.galaxy.at(x, y).system;
                s >= 0 && _game.galaxy.systems[size_t(s)].controllingRace == _empire)
                around(x, y);
    for (const SavedTaskForce &t : _game.taskForces)
        if (t.owner == _empire)
            around(t.x, t.y);
    // The ranges are round the empire's systems; the first of them is
    // selected at the start (Sol, the only one on the first turn).
    _rangeCentres.clear();
    for (int y = 0; y < _game.galaxy.height; ++y)
        for (int x = 0; x < _game.galaxy.width; ++x)
            if (const int sys = _game.galaxy.at(x, y).system;
                sys >= 0 && _game.galaxy.systems[size_t(sys)].controllingRace == _empire)
                _rangeCentres.push_back({x, y});
    if (_rangeCentres.empty())
        select(-1, -1);
    else
        select(_rangeCentres.front().first, _rangeCentres.front().second);
}

void GalaxyScreen::select(int x, int y) {
    const SavedGalaxy &g = _game.galaxy;
    const bool on = x >= 0 && y >= 0 && x < g.width && y < g.height;
    _selected = on ? std::pair{x, y} : std::pair{-1, -1};
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

bool GalaxyScreen::view(int &x0, int &y0, int &x1, int &y1) const {
    const Placed *map = find(kMap);
    if (!map || _game.galaxy.width <= 0)
        return false;
    // The zoomed-out grid, lines included, within the map's place.
    const int gx = map->x + (map->widget->w - _game.galaxy.width * kOutSector) / 2;
    const int gy = map->y + (map->widget->h - _game.galaxy.height * kOutSector) / 2;
    x0 = std::max(gx, map->x);
    y0 = std::max(gy, map->y);
    x1 = std::min(gx + _game.galaxy.width * kOutSector, map->x + map->widget->w - 1);
    y1 = std::min(gy + _game.galaxy.height * kOutSector, map->y + map->widget->h - 1);
    return true;
}

bool GalaxyScreen::gridCorner(int &gx, int &gy) const {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!view(x0, y0, x1, y1))
        return false;
    const SavedGalaxy &g = _game.galaxy;
    const int sector = kZooms[_zoom].sector;
    // Zoomed out, the grid is the view. Zoomed in, the selected sector's
    // corner is at the view's middle (Sol's on the screenshot, at 511), the
    // grid kept over the whole view (there, at the galaxy's top).
    if (_zoom == 0) {
        const Placed *map = find(kMap);
        gx = map->x + (map->widget->w - g.width * sector) / 2;
        gy = map->y + (map->widget->h - g.height * sector) / 2;
        return true;
    }
    const int sx = std::max(_selected.first, 0);
    const int sy = std::max(_selected.second, 0);
    auto place = [&](int middle, int s, int first, int last, int sectors) {
        const int at = middle - s * sector;
        return std::clamp(at, std::min(first, last - sectors * sector), first);
    };
    gx = place((x0 + x1) / 2, sx, x0, x1, g.width);
    gy = place((y0 + y1) / 2, sy, y0, y1, g.height);
    return true;
}

void GalaxyScreen::setZoomedIn(bool in) {
    _zoom = in ? 1 : 0;
}

bool GalaxyScreen::click(int x, int y) {
    int gx = 0;
    int gy = 0;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!view(x0, y0, x1, y1) || !gridCorner(gx, gy) || x < x0 || y < y0 || x > x1 || y > y1 || x < gx || y < gy)
        return false;
    const int sector = kZooms[_zoom].sector;
    const int sx = (x - gx) / sector;
    const int sy = (y - gy) / sector;
    if (sx >= _game.galaxy.width || sy >= _game.galaxy.height)
        return false;
    select(sx, sy);
    return true;
}

bool GalaxyScreen::handle(const Event &e) {
    if (e.type != EventType::kMouseDown || e.button != MouseButton::kLeft)
        return false;
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
    return click(e.mouseX, e.mouseY) || zoomOpen;
}

void GalaxyScreen::draw(Surface &screen) const {
    // The zoom's buttons: the current zoom's lit, as the Map button while
    // they're open (guessed).
    std::set<int32_t> lit{kMilitary, _zoom == 1 ? kSmallMap : kLargeMap};
    if (panelShown(_zoomPanel))
        lit.insert(kMapButton);
    drawLayouts(screen, lit);
    drawMap(screen);
    drawPanelsOver(screen, lit);
    drawPanel(screen);
    drawStrip(screen);
    drawText(screen, kTurnText, upperAscii(word(lex::kTurn, "Turn")) + " " + std::to_string(_game.summary.turn + 1),
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
    const SavedEmpire &e = _game.empires[size_t(_empire)];
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
            // The dilithium's figure first, in the figures' colour, and its
            // word 6 pixels after it (so after "+1" and "0" on screenshots);
            // the last turn's events aren't read from the save yet.
            const std::string dilithium = withSign(e.dilithiumPerTurn);
            line(2, "", kLabelColor, word(lex::kNoEvents, "No Events"), kLabelColor);
            font->drawInColor(screen, box->x, box->y + 1 + 2 * kLinePitch, dilithium, kFigureColor);
            font->drawInColor(screen, box->x + font->measure(dilithium) + kDilithiumGap, box->y + 1 + 2 * kLinePitch,
                              upperAscii(word(lex::kDilithium, "Dilithium")), kLabelColor);
        }
    drawTaskForcesBar(screen);
    // The other empires' emblems, each in its place.
    for (int other = 0; other < 5; ++other)
        if (const Placed *p = other == _empire ? nullptr : find(kFirstEmblem + other))
            drawIcon(screen, p->x, p->y, *p->widget, std::string("i_") + kLetters[other] + "26.tga");
    // A row for each of the empire's task forces: its emblem, its mission
    // and its first ship's class.
    const Placed *icon = find(kRowIcon);
    const Placed *text = find(kRowText);
    const Placed *panel = find(kPanel);
    if (!icon || !text || !panel)
        return;
    const Font *font = _assets.font(text->widget->fonts.empty() ? "" : text->widget->fonts[0].file);
    int row = 0;
    for (const SavedTaskForce &t : _game.taskForces) {
        if (t.owner != _empire)
            continue;
        const int dy = row++ * kRowHeight;
        if (icon->y + dy >= panel->y + panel->widget->h)
            break;
        drawIcon(screen, icon->x, icon->y + dy, *icon->widget, std::string("i_") + _letter + "26.tga");
        if (!font)
            continue;
        std::string ship;
        for (const SavedShip &s : _game.ships)
            if (!t.shipIds.empty() && s.id == t.shipIds.front() && size_t(s.shipClass) < _shipClasses.count())
                ship = _shipClasses.name(size_t(s.shipClass));
        font->drawInColor(screen, text->x, text->y + dy + 1, missionWord(t.mission), kTaskForceColor);
        font->drawInColor(screen, text->x, text->y + dy + 1 + kLinePitch, ship, kTaskForceColor);
    }
}

void GalaxyScreen::drawRanges(Surface &screen, int gx, int gy) const {
    // Each range's border: a line along every sector edge between a sector
    // in it and one out of it (or the grid's edge), the shorter ranges over
    // the longer.
    const SavedGalaxy &g = _game.galaxy;
    const int sector = kZooms[_zoom].sector;
    for (size_t r = std::size(kRanges); r-- > 0;) {
        auto in = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= g.width || y >= g.height)
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
    // The two the first turn's task forces have (lexicon.dic's).
    switch (mission) {
    case 5:
        return word(lex::kAvoid, "AVOID");
    case 6:
        return word(lex::kEngage, "ENGAGE");
    default:
        return {};
    }
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

void GalaxyScreen::drawGrid(Surface &screen, int gx, int gy) const {
    const Zoom &z = kZooms[_zoom];
    const int sector = z.sector;
    const int width = _game.galaxy.width * sector;
    const int height = _game.galaxy.height * sector;
    // The backdrop from the view's corner, whichever the zoom.
    if (const image::Image *backdrop = _assets.picture(std::string(1, _letter) + "m_glmap.tga"))
        image::blit(screen, *backdrop, 0, 0);
    // The scanners' sectors are darker. The game's pixels there don't follow
    // the backdrop's; the backdrop at a fifth comes nearest (guessed).
    for (const auto &[sx, sy] : _scanned)
        for (int y = std::max(gy + sy * sector, 0); y < std::min(gy + (sy + 1) * sector, screen.h); ++y)
            for (int x = std::max(gx + sx * sector, 0); x < std::min(gx + (sx + 1) * sector, screen.w); ++x) {
                uint8_t *p = screen.row(y) + size_t(x) * 4;
                for (int c = 0; c < 3; ++c)
                    p[c] = uint8_t(p[c] / 5);
            }
    // The grid: solid lines, dotted where the scanners reach.
    for (int row = 0; row <= _game.galaxy.height; ++row)
        for (int x = std::max(gx, 0); x < std::min(gx + width, screen.w); ++x) {
            const int col = (x - gx) / sector;
            const bool lit = scanned(col, row) || scanned(col, row - 1);
            if (!lit)
                put(screen, x, gy + row * sector, kGridColor);
            else if ((x - gx) % 2 == 0)
                put(screen, x, gy + row * sector, kScannedGridColor);
        }
    for (int col = 0; col <= _game.galaxy.width; ++col)
        for (int y = std::max(gy, 0); y < std::min(gy + height, screen.h); ++y) {
            const int row = (y - gy) / sector;
            const bool lit = scanned(col, row) || scanned(col - 1, row);
            if (!lit)
                put(screen, gx + col * sector, y, kGridColor);
            else if ((y - gy) % 2 == 0)
                put(screen, gx + col * sector, y, kScannedGridColor);
        }
    // The stars the scanners see, and those of the sectors the empire has
    // explored, with their names under them (the first turn shows Sol's,
    // not its neighbours'; the eleventh, every explored system's).
    const Font *font = _assets.font("human9.fnt");
    const SavedGalaxy &g = _game.galaxy;
    for (int sy = 0; sy < g.height; ++sy)
        for (int sx = 0; sx < g.width; ++sx) {
            const int s = g.at(sx, sy).system;
            const bool explored = (g.at(sx, sy).explored >> _empire & 1) != 0;
            if (s < 0 || !(explored || scanned(sx, sy)))
                continue;
            const SavedSystem &sys = g.systems[size_t(s)];
            const image::Image *star = _assets.picture(sys.starAnimation + z.starSuffix);
            if (!star)
                continue;
            const int x = gx + sx * sector + z.starStep * (sys.mapX - 5 * sx) + z.starOffset;
            const int y = gy + sy * sector + z.starStep * (sys.mapY - 5 * sy) + z.starOffset;
            // Black is the star's background.
            for (int py = 0; py < star->h; ++py)
                for (int px = 0; px < star->w; ++px) {
                    const uint8_t *p = star->pixel(px, py);
                    if (p[0] | p[1] | p[2])
                        put(screen, x + px, y + py, Color{p[0], p[1], p[2]});
                }
            if (font && explored)
                font->drawInColor(screen, x + star->w / 2 - font->measure(sys.name) / 2, y + star->h, sys.name,
                                  sys.controllingRace == _empire ? kNameColor : kOtherNameColor);
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
    // The empire's task forces: its emblem at their sector's corner, one
    // for the sector however many there are.
    if (const image::Image *icon = _assets.picture(std::string("i_") + _letter + z.fleet)) {
        std::set<std::pair<int, int>> drawn;
        for (const SavedTaskForce &t : _game.taskForces)
            if (t.owner == _empire && drawn.insert({t.x, t.y}).second)
                image::blit(screen, *icon, gx + t.x * sector + z.fleetX, gy + t.y * sector + z.fleetY);
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
    const SavedSector &sector = _game.galaxy.at(_selected.first, _selected.second);
    if (sector.system >= 0)
        drawSystemStrip(screen, _game.galaxy.systems[size_t(sector.system)]);
    else if (sector.stellarObject >= 0 && stellarStrip(_game.galaxy.stellarObjects[size_t(sector.stellarObject)].type))
        drawStellarStrip(screen, _game.galaxy.stellarObjects[size_t(sector.stellarObject)]);
}

void GalaxyScreen::drawSystemStrip(Surface &screen, const SavedSystem &system) const {
    const std::vector<WindowLayout::Placed> &strip = _systemStrip;
    if (const image::Image *back = _assets.picture(_systemLayout.root().background))
        image::blit(screen, *back, strip.front().x, strip.front().y);
    const WindowLayout::Placed &area = *stripWidget(strip, kStripArea);
    const int cy = area.y + area.widget->h / 2;
    // The star, then the planets leftward from it.
    const int sunW = _sun.frameWidth;
    int sunCx = area.x + area.widget->w - sunW + sunW / 2;
    drawFrame(screen, _sun, kStarFrameMillis, sunCx, cy);
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
    const Font *small = _assets.font("human9.fnt");
    for (size_t i = 0; i < _planets.size(); ++i) {
        const Animation &a = _planets[i];
        drawFrame(screen, a, kPlanetFrameMillis, xs[i], cy);
        const SavedPlanet &p = system.planets[i];
        if (!small || p.type < 0 || p.type >= int(std::size(kPlanetClasses)))
            continue;
        const std::string letter(1, kPlanetClasses[p.type]);
        const Color c = p.type == kGasGiant                    ? kClassColors[0]
                        : p.terraformDone >= p.terraformNeeded ? kClassColors[1]
                                                               : kClassColors[2];
        // In the middle of the planet's frame, from its left edge.
        const int left = xs[i] - a.frameWidth / 2;
        small->drawInColor(screen, left + (a.frameWidth - small->measure(letter)) / 2, cy + a.frameHeight / 2, letter,
                           c);
    }
    // The title: "SYSTEM" and its name, and who lives there.
    const WindowLayout::Placed &title = *stripWidget(strip, kStripTitle);
    drawTextAt(screen, *title.widget, title.x, title.y, upperAscii(word(lex::kSystem, "system")), kLabelColor);
    drawTextAt(screen, *title.widget, title.x + kTitleNameX, title.y, system.name, kFigureColor);
    // Only the Federation's people's word is known in capitals.
    std::string people;
    if (system.residentRace < 0)
        people = upperAscii(word(lex::kUninhabited, "uninhabited"));
    else if (system.residentRace == 1)
        people = word(lex::kHumans, "HUMANS");
    const WindowLayout::Placed &heading = *stripWidget(strip, kStripHeading);
    drawTextAt(screen, *heading.widget, heading.x, heading.y, people, kLabelColor);
    // The labels and the figures known; the growth rate and the orbitals
    // aren't read from the save yet.
    const WindowLayout::Placed &labels = *stripWidget(strip, kStripLabels);
    const WindowLayout::Placed &values = *stripWidget(strip, kStripValues);
    const Font *font = _assets.font(labels.widget->fonts.empty() ? "" : labels.widget->fonts[0].file);
    if (!font)
        return;
    const std::string label[] = {word(lex::kPopulation, "Population"), word(lex::kMaxPop, "Max Pop"),
                                 word(lex::kGrowthRate, "Growth Rate"), word(lex::kOrbitals, "Orbitals")};
    const std::string figure[] = {std::to_string(system.population), std::to_string(system.maxPopulation), "", ""};
    for (int i = 0; i < 4; ++i) {
        const int y = labels.y + 1 + i * kStripLinePitch;
        font->drawInColor(screen, labels.x, y, upperAscii(label[i]), kLabelColor);
        font->drawInColor(screen, values.x + values.widget->w - font->measure(figure[i]),
                          values.y + 1 + i * kStripLinePitch, figure[i], kFigureColor);
    }
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
