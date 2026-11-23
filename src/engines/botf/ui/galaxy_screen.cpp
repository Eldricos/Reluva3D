// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/galaxy_screen.h"

#include "engines/botf/ui/galaxy_parts.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <set>
#include <string>

namespace reluva::botf {

using namespace galaxy;

Error GalaxyScreen::load(int empire) {
    _empire = std::clamp(empire, 0, 4);
    _letter = kLetters[_empire];
    _tips.load(_assets);
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
    if (e.type == EventType::kMouseMove || e.type == EventType::kMouseDown)
        _tipPointer = {e.mouseX, e.mouseY};
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
    if (_toolTips && _tipPointer.first >= 0)
        if (const int32_t id = toolTipAt(_tipPointer.first, _tipPointer.second))
            _tips.draw(screen, id, _tipPointer.first, _tipPointer.second);
}

int32_t GalaxyScreen::toolTipAt(int x, int y) const {
    if (const int32_t button = buttonAt(x, y); button && _tips.has(button))
        return button;
    // The system strip's population boxes; the stellar object's strip
    // shares their ids, but its description isn't about population.
    if (_selected.first < 0 || game().galaxy.at(_selected.first, _selected.second).system < 0)
        return 0;
    for (const int32_t id : {kStripLabels, kStripValues}) {
        const WindowLayout::Placed *box = stripWidget(_systemStrip, id);
        if (box && x >= box->x && y >= box->y && x < box->x + box->widget->w && y < box->y + box->widget->h &&
            _tips.has(id))
            return id;
    }
    return 0;
}

} // namespace reluva::botf
