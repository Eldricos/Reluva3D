// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the interface shows of the map: the inventory bar's places, the
// zoom and the mouse pointer (docs/engines/stawayteam/systems/maps.md,
// items.md).

#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

// The mouse this near the view's edge scrolls it, as in scroll().
constexpr int kEdge = 4;
// Each Zoom In or Out, and how far (inferred: the program's steps
// aren't traced).
constexpr float kZoomStep = 1.25f;
constexpr float kZoomMost = 2.0f;
constexpr float kZoomLeast = 0.5f;

std::string named(const std::string &cursor) {
    return cursor.empty() || cursor == "!None" ? "Pointer" : cursor;
}

} // namespace

std::vector<std::vector<int>> MapScreen::itemPlaces(const Officer &o) const {
    // A place for each type, the last type's first, as the team selection
    // has its columns (0x5e6b80); those shown in the inventory only.
    std::vector<std::vector<int>> places(_itemTypes.size());
    for (size_t k = 0; k < o.items.size(); ++k) {
        const auto def = _items.find(o.items[k].name);
        if (def == _items.end() || !def->second.displayInInventory)
            continue;
        const auto type = std::find(_itemTypes.begin(), _itemTypes.end(), def->second.type);
        if (type != _itemTypes.end())
            places[size_t(_itemTypes.end() - type) - 1].push_back(int(k));
    }
    return places;
}

void MapScreen::stepItem(int officer, int step) {
    if (officer < 0 || size_t(officer) >= _officers.size())
        return;
    Officer &o = _officers[size_t(officer)];
    const std::vector<std::vector<int>> places = itemPlaces(o);
    const int n = int(places.size());
    if (n == 0)
        return;
    o.shown.resize(places.size(), 0);
    // From the chosen item's place; with none, the first going on, the last
    // going back.
    int from = step > 0 ? 0 : n - 1;
    for (int p = 0; p < n; ++p)
        if (std::find(places[size_t(p)].begin(), places[size_t(p)].end(), o.item) != places[size_t(p)].end())
            from = p;
    int &at = o.shown[size_t(from)];
    const int count = int(places[size_t(from)].size());
    at += step > 0 ? 1 : -1;
    if (at >= 0 && at < count) {
        chooseItem(officer, places[size_t(from)][size_t(at)]);
        _itemTitle = kItemTitleTime;
        return;
    }
    // Past the place's last (or first): it goes back to its first (last),
    // and the next place with any shows its first (the one before, its
    // last).
    at = step > 0 ? 0 : count - 1;
    for (int p = (from + step + n) % n; p != from; p = (p + step + n) % n) {
        const std::vector<int> &place = places[size_t(p)];
        if (place.empty())
            continue;
        o.shown[size_t(p)] = step > 0 ? 0 : int(place.size()) - 1;
        chooseItem(officer, place[size_t(o.shown[size_t(p)])]);
        _itemTitle = kItemTitleTime;
        return;
    }
    // The only place with any: round to its own (inferred: the program
    // leaves the item chosen as it was).
    if (count > 0) {
        chooseItem(officer, places[size_t(from)][size_t(at)]);
        _itemTitle = kItemTitleTime;
    }
}

void MapScreen::setZoom(int how) {
    // In and out by steps, the view kept on the map going out (the zoom's
    // scale is held to the map, 0x4f97a0; inferred for the steps).
    if (how == 0)
        _zoom = 1.0f;
    else
        _zoom = std::clamp(how > 0 ? _zoom * kZoomStep : _zoom / kZoomStep, kZoomLeast, kZoomMost);
    if (_map.width > 0 && _map.height > 0)
        _zoom = std::max(
            _zoom, std::min(1.0f, std::max(float(_viewW) / float(_map.width), float(_viewH) / float(_map.height))));
    moveCamera(_cameraX, _cameraY);
}

std::string MapScreen::cursorName() {
    // Rules.txt's Mouse Cursors are Interface/Game Cursors' (0x5c5d2f); the
    // pointer's Interface/Cursors' Pointer (0x4f566a).
    const bool playing = _scripts.empty() && _teamShown && !_endGame && !_objectivesWindow && !_escape;
    if (!playing || _overInterface)
        return "Pointer";
    // At the view's edges, which way it scrolls (0x5c25ee).
    const bool inView = _mouseX >= _viewX && _mouseY >= _viewY && _mouseX < _viewX + _viewW + kEdge &&
                        _mouseY < _viewY + _viewH + kEdge;
    if (!inView)
        return "Pointer";
    const bool left = _mouseX < _viewX + kEdge;
    const bool right = _mouseX >= _viewX + _viewW - kEdge;
    const bool up = _mouseY < _viewY + kEdge;
    const bool down = _mouseY >= _viewY + _viewH - kEdge;
    if (left || right || up || down) {
        std::string name = "Move";
        if (up || down)
            name += up ? " Up" : " Down";
        if (left || right)
            name += left ? " Left" : " Right";
        return name;
    }
    // Choosing whose sight to show (0x5c5e4e).
    if (_vision == 1)
        return "Vision Cursor";
    if (_selected < 0)
        return "Pointer";
    const Officer &o = _officers[size_t(_selected)];
    if (o.down())
        return "Pointer";
    // As a click would go: the chosen item used, an officer chosen, a
    // part's process, an enemy shot; each with its item's cursors, over
    // what it's used on its Entity Hovering Cursor, that out of sight its
    // Obscured Cursor, elsewhere its Process Cursor, and where it can't be
    // used its Can't Use Cursor (inferred: the program's choosing isn't
    // traced).
    const float x = mapX(_mouseX);
    const float y = mapY(_mouseY);
    const int enemy = enemyAt(x, y);
    const int officer = officerAt(x, y);
    const int part = partAt(x, y);
    const ItemDefinition *def = itemOf(o);
    if (def && o.items[size_t(o.item)].count != 0) {
        const bool scans = def->processType == "Scan" || def->processType == "Medical Scan";
        if (!def->deployModel.empty() || def->dischargeAction == "CThrowProjectileWeaponAction") {
            if (!def->obscuredCursor.empty() && !clearSight(o.x, o.y, x, y))
                return named(def->obscuredCursor);
            return named(def->processCursor);
        }
        if (scans) {
            if (part >= 0)
                if (const TextObject *process = processFor(size_t(part), o))
                    return named(readText(process, "Process Cursor"));
            return named(part >= 0 || enemy >= 0 ? def->hoveringCursor : def->cantUseCursor);
        }
        if (def->health > 0.0f)
            return named(officer >= 0 && (officer != _selected || def->canUseOnSelf) ? def->hoveringCursor
                                                                                     : def->cantUseCursor);
    }
    if (officer >= 0)
        return "Pointer";
    if (part >= 0)
        if (const TextObject *process = processFor(size_t(part), o))
            return named(readText(process, "Process Cursor"));
    if (enemy >= 0 && o.weapon) {
        const Enemy &e = _enemies[size_t(enemy)];
        if (!o.weapon->obscuredCursor.empty() && !clearSight(o.x, o.y, e.x, e.y))
            return named(o.weapon->obscuredCursor);
        return named(o.weapon->hoveringCursor);
    }
    if (def && def == o.weapon)
        return named(def->processCursor);
    return "Pointer";
}

std::vector<GameHud::Mark> MapScreen::minimapMarks() const {
    // The officers here and the enemies standing (inferred: which the
    // program marks isn't traced), and the parts with a
    // CMiniMapPointOfInterestAI, the band dropped in Mission 01 given one.
    std::vector<GameHud::Mark> marks;
    for (const Officer &o : _officers)
        if (o.present && o.alive())
            marks.push_back({o.x, o.y, GameHud::Mark::kOfficer});
    for (const Enemy &e : _enemies)
        if (e.alive() && shown(e.part))
            marks.push_back({e.x, e.y, GameHud::Mark::kEnemy});
    for (size_t i = 0; i < _map.parts.size(); ++i)
        for (const TextObject *ai : _map.parts[i].activities)
            if (ai->className == "CMiniMapPointOfInterestAI" && shown(i)) {
                float x = 0.0f;
                float y = 0.0f;
                placeOf(i, x, y);
                marks.push_back({x, y, GameHud::Mark::kInterest});
                break;
            }
    return marks;
}

const Animation::Picture *MapScreen::cursor(uint64_t now) {
    const std::string name = cursorName();
    auto it = _cursors.find(name);
    if (it == _cursors.end()) {
        Animation a;
        const std::string path = name == "Pointer" ? "Cache/Interface/Cursors/Pointer.seq16"
                                                   : "Cache/Interface/Game Cursors/" + name + ".seq16";
        if (a.loadFile(_data, path))
            (void)a.loadFile(_data, "Cache/Interface/Cursors/Pointer.seq16");
        it = _cursors.emplace(name, std::move(a)).first;
    }
    return it->second.at(now);
}

} // namespace reluva::stawayteam
