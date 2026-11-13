// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/model_render.h"
#include "engines/newworlds/star_system.h"
#include "engines/newworlds/soft3d.h"
#include "engines/newworlds/viewers.h"

#include "audio/decode.h"
#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iterator>

namespace reluva::newworlds {

namespace {

constexpr Color kHeadingColor{255, 220, 90};
constexpr Color kTextColor{230, 230, 230};
constexpr Color kNoteColor{150, 160, 190};
constexpr int kPanelMargin = 12;              // round the panel, and inside it
constexpr int kMusicVolume = 160;             // under the speech
constexpr float kBriefingTurn = 0.05f / 1000; // radians per millisecond (about 3 degrees a second)

std::string upperCase(std::string s) {
    for (char &c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

const char *sideName(char side) {
    switch (side) {
    case 'K':
        return "the Klingons";
    case 'R':
        return "the Romulans";
    default:
        return "the Federation";
    }
}

} // namespace

MissionViewer::MissionViewer(System &sys, Surface &screen, const Archives &archives, std::vector<Level> levels,
                             audio::Mixer *mixer, const std::filesystem::path &gameDir)
    : _sys(sys), _screen(screen), _archives(archives), _missions(std::move(levels)), _mixer(mixer),
      _map(sys, screen, archives) {
    _map.setLevels(_missions);
    _map.setOverlay([this] { drawBriefing(); });
    _map.setTitleFilter([this](const std::string &mapTitle) {
        _mapTitle = mapTitle;
        return title(mapTitle);
    });
    _fonts = loadGameFonts(_archives);
    loadBriefingArt();
    _textureFiles = listTextureFiles(_archives);
    for (const GameFont &f : _fonts)
        if (f.name == "fnt/fe8pt")
            _font = &f;

    std::error_code ec;
    if (!gameDir.empty())
        for (std::filesystem::recursive_directory_iterator it(gameDir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string ext = fs::toLower(it->path().extension().string());
            if (it->is_regular_file(ec) && (ext == ".mp3" || ext == ".wav"))
                _mediaFiles.emplace(upperCase(it->path().stem().string()), it->path());
        }

    // Without any of the missions' maps or text there's nothing to show.
    const bool any = std::any_of(_missions.begin(), _missions.end(), [&](const Level &l) {
        for (const auto &a : _archives) {
            for (const std::string &name : missionMapNames(l.shortName))
                if (a->find(name))
                    return true;
            for (char side : missionSides(l.shortName))
                if (const std::string table = missionTableName(l.shortName, side); !table.empty() && a->find(table))
                    return true;
        }
        return false;
    });
    if (!any)
        _missions.clear();
}

void MissionViewer::activate() {
    if (!_shown) {
        _shown = true;
        show(0);
        return;
    }
    if (_hasMap)
        _map.activate();
    if (_music)
        startMusic();
    redraw();
    updateTitle();
}

void MissionViewer::deactivate() {
    stopClip();
    if (_mixer)
        _mixer->stop(_musicHandle);
    _musicHandle = audio::kNoSound;
}

void MissionViewer::show(int index) {
    stopPlay();
    stopClip();
    _soundStatus.clear();
    _current = index;
    _side = 0;
    const Level &level = _missions[size_t(_current)];
    _hasMap = false;
    _mapName.clear();
    _mapNote.clear();
    _mapTitle.clear();
    const std::vector<std::string> names = missionMapNames(level.shortName);
    for (size_t i = 0; i < names.size() && !_hasMap; ++i)
        if (_map.showMap(names[i])) {
            _hasMap = true;
            _mapName = names[i];
            if (!level.shortName.empty() && (level.shortName[0] == 'C' || level.shortName[0] == 'c'))
                _mapNote = "the map its recording fits";
            else if (i > 0)
                _mapNote = "mission 12's map, whose world the briefing names";
        }
    if (!_hasMap)
        _mapNote = "no " + names[0] + " in these archives";
    loadSide();
    if (_music)
        startMusic();
    redraw();
    updateTitle();
}

void MissionViewer::loadSide() {
    stopPlay();
    stopScript();
    stopBriefing();
    stopClip();
    _page = _clip = 0;
    _pages.clear();
    _clips.clear();
    _status.clear();
    const Level &level = _missions[size_t(_current)];
    const char side = missionSides(level.shortName)[size_t(_side)];
    _tableName = missionTableName(level.shortName, side);

    _table = StringTable{};
    StringTable &table = _table;
    if (_tableName.empty())
        _status = "no briefing known for this mission";
    else {
        _status = "no " + _tableName + " in these archives";
        for (const auto &a : _archives)
            if (const CatEntry *e = a->find(_tableName)) {
                std::vector<uint8_t> data;
                if (Error err = a->read(*e, data))
                    _status = err.message;
                else if (Error err2 = table.parse(data))
                    _status = err2.message;
                else
                    _status.clear();
                break;
            }
    }
    // Pages in the table's order: a "~HEADING~" string each, and the plain
    // strings between them (the title and stardate first, the objectives
    // as listed in game last) run together.
    for (const StringTable::String &s : table.strings()) {
        const size_t close = s.text.size() > 1 && s.text[0] == '~' ? s.text.find('~', 1) : std::string::npos;
        if (close != std::string::npos) {
            std::string heading = s.text.substr(1, close - 1);
            std::replace(heading.begin(), heading.end(), '\t', ' ');
            _pages.push_back({std::move(heading), s.text.substr(close + 1), s.id});
            continue;
        }
        if (_pages.empty() || !_pages.back().heading.empty())
            _pages.push_back({{}, {}, s.id});
        Page &page = _pages.back();
        page.text += (page.text.empty() ? "" : "\n") + s.text;
    }

    for (const auto &[stem, path] : _mediaFiles)
        if (fs::toLower(path.extension().string()) == ".mp3" && isMissionClip(stem, level.shortName, side))
            _clips.push_back(stem);
    std::sort(_clips.begin(), _clips.end(), clipBefore);
    _script = briefingScript(level.shortName, side, _table, _clips);
    loadStarSystem();
    startColony();
}

void MissionViewer::startColony() {
    _colony.reset();
    _colonySite = {};
    if (_hasMap) {
        const Level &level = _missions[size_t(_current)];
        const char side = missionSides(level.shortName)[size_t(_side)];
        _colonySite = colonySite(level.shortName, side);
        if (_colonySite.a >= 0) {
            ColonyGround ground{_map.blocksWide(), _map.blocksHigh(),
                                [this](int bx, int by) { return _map.depositAt(bx, by); },
                                [this](int a, int b) { return _map.groundAt(a, b); },
                                [this](const std::string &model) { return _map.modelSize(model); }};
            _colony = std::make_unique<ColonyLayout>(side, std::move(ground));
            if (!_colony->start(_colonySite.a, _colonySite.b))
                _colony.reset();
        }
    }
    showColony();
}

void MissionViewer::showColony() {
    std::vector<MapViewer::Building> buildings;
    // In play, each has the side's construction bee to work on it while
    // it goes up.
    std::string bee;
    if (_economy) {
        const char side = missionSides(_missions[size_t(_current)].shortName)[size_t(_side)];
        bee = side == 'K' ? "geo/kln/conbee.geo" : side == 'R' ? "geo/rom/rcb.geo" : "geo/fed/fconbee.geo";
    }
    if (_colony && _colonyShown)
        for (const ColonyBuilding &c : _colony->buildings())
            buildings.push_back({c.model, c.a, c.b, 0, 1, bee});
    _map.setBuildings(buildings);
    showProgress();
    _map.setPaved(_colony && _colonyShown ? _colony->pad() : std::vector<std::pair<int, int>>{});
}

bool MissionViewer::hubPosition(float &x, float &y) const {
    if (!_colony || _colony->buildings().empty())
        return false;
    const ColonyBuilding &hub = _colony->buildings()[0];
    x = float(hub.b) / 8 * MapViewer::kBlockUnits;
    y = float(hub.a) / 8 * MapViewer::kBlockUnits;
    return true;
}

// Play: the colony's starting units by the hub (four of the side's ground
// scouts, as F1 and R1 start with four vehicles of one kind), the game's
// camera over it, the interface up.
void MissionViewer::startPlay() {
    if (!_hasMap || _running)
        return;
    const Level &level = _missions[size_t(_current)];
    const char side = missionSides(level.shortName)[size_t(_side)];
    _world = std::make_unique<World>(_map.terrain());
    // The colony as it starts: the hub alone, and the stock every
    // recording started with (the Romulans a little less).
    _colonyShown = true;
    startColony();
    static constexpr Stock kStart{500, 1000, 2300, 500, 4000, 5000};
    static constexpr Stock kRomulan{500, 988, 2211, 497, 3500, 5000};
    ColonyMap ground{[this](int tx, int ty) { return _map.depositAt(tx / 2, ty / 2); },
                     [this](int tx, int ty) { return _map.depositAmount(tx / 2, ty / 2); }};
    _economy = std::make_unique<Economy>(side, side == 'R' ? kRomulan : kStart, 20, 50, std::move(ground));
    _structureIds.clear();
    if (_colony)
        for (const ColonyBuilding &c : _colony->buildings())
            _structureIds.push_back(_economy->addBuilt(c.name, float(c.b) / 8 * MapViewer::kBlockUnits,
                                                       float(c.a) / 8 * MapViewer::kBlockUnits));
    // The mission's script, from its start, if Reluva has one.
    _run.reset();
    _walk.clear();
    _log.clear();
    _walkNote.clear();
    if (const MissionScript *script = findMissionScript(level.shortName, side)) {
        _run = std::make_unique<MissionRun>(*script, findMissionScript("generic", 'F'));
        const ScriptEvent start{"start", {}};
        deliver(start, _run->fire(start));
    }
    _buildMenu = false;
    _placing.clear();
    float hx = _map.worldWidth() / 2;
    float hy = _map.worldHeight() / 2;
    hubPosition(hx, hy);
    // In a row south of the hub, each on the nearest open tile.
    for (int i = 0; i < 4; ++i) {
        const float x = hx + (float(i) - 1.5f) * 2500;
        const float y = hy - 1.4f * kTile;
        const auto path = _world->findPath(x, y, x, y);
        const float px = path.empty() ? x : path.back().first;
        const float py = path.empty() ? y : path.back().second;
        _world->addUnit(side, groundScout(side), px, py, 3.1416f / 2);
    }
    _playing = true;
    _interface = true;
    _briefing = false;
    _boxing = _tricorderFull = false;
    _lastTickMs = _nowMs;
    _map.centreOn(hx, hy);
    _map.setView(MapViewer::View::kBirdsEye);
    showUnits();
    redraw();
    updateTitle();
}

void MissionViewer::stopPlay() {
    if (!_playing)
        return;
    _playing = false;
    _world.reset();
    _economy.reset();
    _structureIds.clear();
    _run.reset();
    _log.clear();
    _walkNote.clear();
    _buildMenu = false;
    _placing.clear();
    _map.setGhost({}, 0, 0, false);
    _map.setUnits({});
    _map.unlock();
    redraw();
    updateTitle();
}

void MissionViewer::showUnits() {
    std::vector<MapViewer::UnitDraw> draw;
    if (_world)
        for (const Unit &u : _world->units())
            draw.push_back({u.kind.model, u.x, u.y, u.heading, u.selected});
    _map.setUnits(std::move(draw));
}

std::vector<const BuildingType *> MissionViewer::buildMenu() const {
    std::vector<const BuildingType *> out;
    if (!_economy)
        return out;
    const char side = missionSides(_missions[size_t(_current)].shortName)[size_t(_side)];
    for (const BuildingType &t : buildingTypes())
        if (t.needs != "-" && !buildingModel(t.id, side).empty())
            out.push_back(&t);
    return out;
}

void MissionViewer::choose(const BuildingType &t) {
    if (!_economy || !_economy->available(t))
        return;
    _placing = t.id;
    _buildMenu = false;
    _ghostA = _ghostB = -1;
    updateGhost();
}

// The hologram follows the pointer, on the tile grid, green where the
// building can go.
void MissionViewer::updateGhost() {
    if (_placing.empty() || !_colony)
        return;
    float x = 0;
    float y = 0;
    int a = -1;
    int b = -1;
    bool ok = false;
    if (_map.screenToGround(_mouseX, _mouseY, x, y)) {
        const int pa = int(y / MapViewer::kBlockUnits * 8);
        const int pb = int(x / MapViewer::kBlockUnits * 8);
        if (!_colony->spotAt(_placing, pa, pb, a, b, ok))
            a = b = -1;
    }
    if (a == _ghostA && b == _ghostB && ok == _ghostOk)
        return;
    _ghostA = a, _ghostB = b, _ghostOk = ok;
    const char side = missionSides(_missions[size_t(_current)].shortName)[size_t(_side)];
    _map.setGhost(a < 0 ? std::string() : buildingModel(_placing, side), a, b, ok);
}

void MissionViewer::placeBuilding() {
    if (_placing.empty() || !_colony || !_economy || !_ghostOk || _ghostA < 0)
        return;
    const float x = float(_ghostB) / 8 * MapViewer::kBlockUnits;
    const float y = float(_ghostA) / 8 * MapViewer::kBlockUnits;
    const int id = _economy->order(_placing, x, y, int(x / kTile), int(y / kTile));
    if (id == 0 || !_colony->addAt(_placing, _ghostA, _ghostB))
        return;
    _structureIds.push_back(id);
    _placing.clear();
    _map.setGhost({}, 0, 0, false);
    showColony();
}

void MissionViewer::showProgress() {
    if (!_economy)
        return;
    const auto &all = _economy->structures();
    for (size_t i = 0; i < _structureIds.size(); ++i)
        for (const ColonyStructure &c : all)
            if (c.id == _structureIds[i])
                _map.setBuildingProgress(i, c.progress);
}

bool MissionViewer::playEvent(const Event &ev) {
    if (ev.type == EventType::MouseMove || ev.type == EventType::MouseDown || ev.type == EventType::MouseUp)
        _mouseX = ev.mouseX, _mouseY = ev.mouseY;
    const bool ctrl = (ev.mods & keymod::Ctrl) != 0;
    const bool shift = (ev.mods & keymod::Shift) != 0;
    switch (ev.type) {
    case EventType::KeyDown:
        if (ev.key == key::Escape && (_buildMenu || !_placing.empty())) {
            _buildMenu = false;
            _placing.clear();
            _map.setGhost({}, 0, 0, false);
            break;
        }
        if (ev.key == key::Escape || ev.key == key::F1 + 2) {
            stopPlay();
            return true;
        }
        if (ev.key == 'b') {
            _buildMenu = !_buildMenu;
            _placing.clear();
            _map.setGhost({}, 0, 0, false);
            break;
        }
        if (_buildMenu && ev.key >= '0' && ev.key <= '9') {
            // 1-9, then 0 for the tenth.
            const size_t n = ev.key == '0' ? 9 : size_t(ev.key - '1');
            const auto menu = buildMenu();
            if (n < menu.size())
                choose(*menu[n]);
            break;
        }
        if (ev.key == key::Tab) {
            _tricorderFull = !_tricorderFull;
            break;
        }
        if (ev.key == key::Home) {
            float x = 0;
            float y = 0;
            if (hubPosition(x, y))
                _map.centreOn(x, y);
            break;
        }
        if (ev.key == 'v' && shift) {
            // To the selected vehicles, or the first one.
            float x = 0;
            float y = 0;
            if (_world->groupCentre(0, x, y))
                _map.centreOn(x, y);
            else if (!_world->units().empty())
                _map.centreOn(_world->units()[0].x, _world->units()[0].y);
            break;
        }
        if (ev.key >= '1' && ev.key <= '9') {
            const int n = int(ev.key - '0');
            float x = 0;
            float y = 0;
            if (ctrl)
                _world->setGroup(n);
            else if (shift && _world->groupCentre(n, x, y))
                _map.centreOn(x, y);
            else if (!shift)
                _world->selectGroup(n);
            showUnits();
            break;
        }
        if (ev.key == 'i') {
            _interface = !_interface;
            break;
        }
        if (_map.gameKey(ev.key, _mouseX, _mouseY))
            break;
        return true; // the data viewer's keys don't apply in play
    case EventType::MouseDown:
        if (!_placing.empty() && !ctrl) {
            // Left click builds where the hologram is green; right click
            // puts it away.
            if (ev.button == MouseButton::Left)
                placeBuilding();
            else if (ev.button == MouseButton::Right) {
                _placing.clear();
                _map.setGhost({}, 0, 0, false);
            }
            break;
        }
        if (ev.button == MouseButton::Left && ctrl) {
            _map.zoomAt(ev.mouseX, ev.mouseY, true);
        } else if (ev.button == MouseButton::Right && ctrl) {
            _map.zoomAt(ev.mouseX, ev.mouseY, false);
        } else if (ev.button == MouseButton::Right) {
            // Right-click drops the selection, or the camera's lock.
            if (_map.locked())
                _map.unlock();
            else
                _world->clearSelection();
            showUnits();
        } else if (ev.button == MouseButton::Left) {
            _boxing = true;
            _boxX = ev.mouseX, _boxY = ev.mouseY;
            return true;
        }
        break;
    case EventType::MouseMove:
        if (!_placing.empty()) {
            updateGhost();
            return true;
        }
        if (!_boxing)
            return true;
        break;
    case EventType::MouseUp: {
        if (ev.button != MouseButton::Left || !_boxing)
            return true;
        _boxing = false;
        const Level &level = _missions[size_t(_current)];
        const char side = missionSides(level.shortName)[size_t(_side)];
        const int x0 = std::min(_boxX, ev.mouseX);
        const int x1 = std::max(_boxX, ev.mouseX);
        const int y0 = std::min(_boxY, ev.mouseY);
        const int y1 = std::max(_boxY, ev.mouseY);
        std::vector<int> picked;
        if (x1 - x0 > 6 || y1 - y0 > 6) {
            // A box: the side's units inside it.
            for (const Unit &u : _world->units()) {
                int sx = 0;
                int sy = 0;
                if (u.side == side && _map.worldToScreen(u.x, u.y, _map.groundAtWorld(u.x, u.y), sx, sy) && sx >= x0 &&
                    sx <= x1 && sy >= y0 && sy <= y1)
                    picked.push_back(u.id);
            }
            _world->select(picked);
        } else {
            // A click: the nearest unit to it, if close; else, with units
            // selected, they go there; a double click on the ground locks
            // the camera on it.
            int best = -1;
            int bestD = 18 * 18;
            for (const Unit &u : _world->units()) {
                int sx = 0;
                int sy = 0;
                if (!_map.worldToScreen(u.x, u.y, _map.groundAtWorld(u.x, u.y) + 300, sx, sy))
                    continue;
                const int d = (sx - ev.mouseX) * (sx - ev.mouseX) + (sy - ev.mouseY) * (sy - ev.mouseY);
                if (d < bestD && u.side == side)
                    best = u.id, bestD = d;
            }
            float gx = 0;
            float gy = 0;
            if (best >= 0)
                _world->select({best});
            else if (_map.screenToGround(ev.mouseX, ev.mouseY, gx, gy)) {
                if (!_world->selection().empty())
                    _world->moveSelected(gx, gy);
                else if (ev.clicks >= 2)
                    _map.lockOn(gx, gy);
            }
        }
        showUnits();
        break;
    }
    case EventType::MouseWheel:
        _map.handleEvent(ev);
        return true;
    default:
        return true;
    }
    redraw();
    updateTitle();
    return true;
}

void MissionViewer::handleEvent(const Event &ev) {
    if (_missions.empty())
        return;
    if (_playing && playEvent(ev))
        return;
    if (ev.type == EventType::KeyDown && ev.key == key::F1 + 2) {
        startPlay();
        return;
    }
    if (ev.type != EventType::KeyDown) {
        if (_hasMap)
            _map.handleEvent(ev);
        return;
    }
    const int n = int(_missions.size());
    const int sides = int(missionSides(_missions[size_t(_current)].shortName).size());
    switch (ev.key) {
    case key::Right:
        show((_current + 1) % n);
        return;
    case key::Left:
        show((_current + n - 1) % n);
        return;
    case key::Down:
    case key::PageDown:
        if (_page + 1 < int(_pages.size()))
            ++_page, _briefing = true;
        break;
    case key::Up:
    case key::PageUp:
        if (_page > 0)
            --_page, _briefing = true;
        break;
    case 'b':
        _briefing = !_briefing;
        break;
    case key::Return:
        if (_running)
            startStep(_step + 1);
        else
            startBriefing();
        break;
    case key::Backspace:
        stopBriefing();
        break;
    case 'x':
        stepScript();
        break;
    case 'z':
        stopScript();
        break;
    case 'i':
        _interface = !_interface;
        break;
    case 'c':
        _colonyShown = !_colonyShown;
        showColony();
        break;
    case 'v':
        if (_colony && !_colony->buildings().empty())
            _map.lookAt(_colony->buildings()[0].a, _colony->buildings()[0].b, true);
        break;
    case 'j':
        if (sides < 2)
            return;
        _side = (_side + 1) % sides;
        loadSide();
        break;
    case key::Space:
        if (clipPlaying())
            stopClip();
        else
            playClip(_clip);
        break;
    case 'n':
        if (!_clips.empty())
            playClip((_clip + 1) % int(_clips.size()));
        break;
    case 'p':
        if (!_clips.empty())
            playClip((_clip + int(_clips.size()) - 1) % int(_clips.size()));
        break;
    case 'm':
        _music = !_music;
        _soundStatus.clear();
        if (_music)
            startMusic();
        else
            stopMusic();
        break;
    default:
        if (_hasMap)
            _map.handleEvent(ev);
        return;
    }
    redraw();
    updateTitle();
}

void MissionViewer::update(uint64_t nowMs) {
    if (_missions.empty())
        return;
    _nowMs = nowMs;
    // The briefing moves on when its step's clip ends, or its time is up.
    if (_running && (_stepEndMs ? nowMs >= _stepEndMs : !clipPlaying()))
        startStep(_step + 1);
    // The galaxy and models turn slowly too.
    if (_running && !_stepOnMap &&
        (_script[size_t(_step)].view == BriefingView::kGalaxy || _script[size_t(_step)].view == BriefingView::kModel) &&
        nowMs - _lastTurnMs >= 100) {
        _lastTurnMs = nowMs;
        _dirty = true;
    }
    // The briefing's map views turn slowly about their target.
    if (_running && _stepOnMap && nowMs - _lastTurnMs >= 100) {
        _map.turn(float(nowMs - _lastTurnMs) * kBriefingTurn);
        _lastTurnMs = nowMs;
    }
    if (_playing && _world) {
        const double dt = double(nowMs - _lastTickMs) / 1000.0;
        _lastTickMs = nowMs;
        if (_world->advance(std::min(dt, 0.25)))
            showUnits();
        std::vector<ScriptEvent> happened;
        if (_economy) {
            happened = _economy->advance(std::min(dt, 0.25));
            showProgress();
        }
        // The script hears what happened, and its timers run.
        if (_run && !_run->finished()) {
            for (const ScriptEvent &e : happened)
                deliver(e, _run->fire(e));
            const ScriptEvent tick{"timer", {}};
            deliver(tick, _run->advance(std::min(dt, 0.25)));
        }
        updateGhost(); // the camera may have moved under the pointer
        if (_economy && nowMs / 1000 != (nowMs - uint64_t(dt * 1000)) / 1000)
            redraw(); // the panels' figures, once a second
    }
    if (mapInView())
        _map.update(nowMs);
    else if (_dirty) {
        if (_running)
            drawBackdrop(_script[size_t(_step)].view);
        else
            drawNoMap();
        drawBriefing();
        _dirty = false;
    }
    // A clip ending changes the title.
    const bool playing = clipPlaying();
    if (playing != _wasPlaying)
        updateTitle();
    _wasPlaying = playing;
}

void MissionViewer::redraw() {
    if (mapInView())
        _map.redraw();
    else
        _dirty = true;
}

int MissionViewer::measure(std::string_view text) const {
    return _font ? _font->textWidth(text) : int(font::length(text)) * font::kAdvance;
}

int MissionViewer::lineHeight() const {
    return _font ? _font->lineHeight : font::kLineHeight;
}

void MissionViewer::drawText(int x, int y, std::string_view text, Color color) {
    if (_font)
        _font->drawText(_screen, x, y, text, color);
    else
        font::drawText(_screen, x, y, text, color);
}

void MissionViewer::drawNoMap() {
    for (int y = 0; y < _screen.h; ++y) {
        uint8_t *row = _screen.row(y);
        for (int x = 0; x < _screen.w; ++x, row += 4)
            row[0] = 22, row[1] = 26, row[2] = 38, row[3] = 255;
    }
    drawText(kPanelMargin, kPanelMargin, _mapNote, kNoteColor);
}

void MissionViewer::drawBriefing() {
    if (_playing) {
        if (_interface)
            drawInterface();
        drawPlay();
        return;
    }
    if (_interface && !_running) {
        drawInterface();
        return;
    }
    if (!_briefing)
        return;
    // Lay the page out in lines, word-wrapped to the panel.
    struct Line {
        std::string text;
        Color color;
    };
    std::vector<Line> lines;
    const int width = _screen.w - 4 * kPanelMargin;
    auto wrap = [&](const std::string &text, Color color) {
        size_t start = 0;
        while (start <= text.size()) {
            const size_t end = std::min(text.find('\n', start), text.size());
            const std::string para = text.substr(start, end - start);
            std::string line;
            size_t i = 0;
            while (i < para.size()) {
                const size_t space = std::min(para.find(' ', i), para.size());
                const std::string word = para.substr(i, space - i);
                const std::string next = line.empty() ? word : line + " " + word;
                if (!line.empty() && measure(next) > width) {
                    lines.push_back({line, color});
                    line = word;
                } else
                    line = next;
                i = space + 1;
            }
            lines.push_back({line, color});
            start = end + 1;
        }
    };
    if (_run && !_running) {
        // The mission's objectives, then its latest messages.
        wrap("OBJECTIVES", kHeadingColor);
        const MissionScript *script = findMissionScript(
            _missions[size_t(_current)].shortName, missionSides(_missions[size_t(_current)].shortName)[size_t(_side)]);
        for (const MissionScript::Objective &o : script->objectives) {
            const MissionRun::State st = _run->objectiveState(o.id);
            if (st == MissionRun::State::kHidden)
                continue;
            const std::string *text = _table.find(o.id);
            wrap(std::string(st == MissionRun::State::kComplete ? "[done] " : "[    ] ") +
                     (text ? *text : "objective " + std::to_string(o.id)),
                 st == MissionRun::State::kComplete ? kNoteColor : kTextColor);
        }
        if (!_log.empty())
            wrap("COMMUNICATIONS " + std::to_string(_log.size()), kHeadingColor);
        for (size_t i = _log.size() > 4 ? _log.size() - 4 : 0; i < _log.size(); ++i)
            wrap(_log[i].first + ": " + _log[i].second, kTextColor);
        lines.push_back({_walkNote + " (X: next; Z: stop)", kNoteColor});
    } else if (_running) {
        // The step's page, if it has one.
        const uint32_t id = _script[size_t(_step)].page;
        for (const Page &page : _pages)
            if (id && page.id == id) {
                if (!page.heading.empty())
                    wrap(page.heading, kHeadingColor);
                wrap(page.text, kTextColor);
                break;
            }
        if (!lines.empty())
            lines.push_back({"briefing " + std::to_string(_step + 1) + "/" + std::to_string(_script.size()) +
                                 " (Enter: next; Backspace: stop)",
                             kNoteColor});
    } else if (!_status.empty())
        wrap(_status, kNoteColor);
    else if (!_pages.empty()) {
        const Page &page = _pages[size_t(_page)];
        if (!page.heading.empty())
            wrap(page.heading, kHeadingColor);
        wrap(page.text, kTextColor);
        lines.push_back(
            {"page " + std::to_string(_page + 1) + "/" + std::to_string(_pages.size()) + " (Up/Down; B hides)",
             kNoteColor});
    }
    if (lines.empty())
        return;

    // A dark panel along the bottom, as tall as the page (or the screen).
    const int lh = lineHeight();
    const int maxLines = std::max(1, (_screen.h - 4 * kPanelMargin) / lh);
    if (int(lines.size()) > maxLines) {
        lines.resize(size_t(maxLines));
        lines.back() = {"...", kNoteColor};
    }
    const int height = int(lines.size()) * lh + 2 * kPanelMargin;
    const int top = _screen.h - kPanelMargin - height;
    for (int y = std::max(top, 0); y < _screen.h - kPanelMargin; ++y) {
        uint8_t *px = _screen.row(y) + kPanelMargin * 4;
        for (int x = kPanelMargin; x < _screen.w - kPanelMargin; ++x, px += 4) {
            px[0] = uint8_t(px[0] / 4 + 8);
            px[1] = uint8_t(px[1] / 4 + 12);
            px[2] = uint8_t(px[2] / 4 + 28);
        }
    }
    for (size_t i = 0; i < lines.size(); ++i)
        drawText(2 * kPanelMargin, top + kPanelMargin + int(i) * lh, lines[i].text, lines[i].color);
}

std::string MissionViewer::title(const std::string &mapTitle) const {
    if (_missions.empty())
        return {};
    const Level &level = _missions[size_t(_current)];
    const std::vector<char> sides = missionSides(level.shortName);
    std::string t = "New Worlds missions " + std::to_string(_current + 1) + "/" + std::to_string(_missions.size()) +
                    ": " + level.shortName + " \"" + level.title + "\", " + std::to_string(level.maxPlayers) +
                    (level.maxPlayers == 1 ? " player" : " players") + (level.commentedOut ? " (not listed)" : "");
    if (sides.size() > 1)
        t += std::string(", as ") + sideName(sides[size_t(_side)]) + " (J: side)";
    t += _hasMap ? "; " + _mapName : "";
    if (!_mapNote.empty())
        t += " (" + _mapNote + ")";
    if (_clips.empty())
        t += "; no voice clips";
    else {
        t += "; voice " + std::to_string(_clip + 1) + "/" + std::to_string(_clips.size()) + " " + _clips[size_t(_clip)];
        t += clipPlaying() ? " [playing]" : " (Space: play)";
    }
    if (_colony)
        t += "; colony at " + std::to_string(_colonySite.a) + ":" + std::to_string(_colonySite.b) + " (" +
             _colonySite.source + "), " + std::to_string(_colony->buildings().size()) + " building" +
             (_colony->buildings().size() == 1 ? "" : "s") + (_colonyShown ? "" : ", hidden (C)");
    else if (_hasMap)
        t += "; no colony site known";
    if (_playing && _world)
        t += "; playing: " + std::to_string(_world->selection().size()) + " selected (F3 or Escape: stop)";
    if (_playing && _economy)
        t += "; power " + std::to_string(int(_economy->powerSupply())) + "/" +
             std::to_string(int(_economy->powerDemand())) + " (B: build)";
    if (_run)
        t += "; script: " + _walkNote;
    if (_running)
        t += "; briefing step " + std::to_string(_step + 1) + "/" + std::to_string(_script.size()) +
             (hasRecordedBriefing(level.shortName, sides[size_t(_side)]) ? "" : " (by the general rule)");
    if (_music && !_musicName.empty())
        t += " [music " + _musicName + "]";
    if (!_soundStatus.empty())
        t += " [" + _soundStatus + "]";
    // Then the map's own details: its archive, size and view settings.
    if (const size_t p = mapTitle.find(" ("); _hasMap && p != std::string::npos)
        t += mapTitle.substr(p);
    return t;
}

void MissionViewer::updateTitle() {
    if (!_missions.empty())
        _sys.setWindowTitle(title(_mapTitle));
}

std::shared_ptr<const audio::Sound> MissionViewer::loadSound(const std::string &stem) {
    const auto it = _mediaFiles.find(upperCase(stem));
    if (it == _mediaFiles.end()) {
        _soundStatus = "no " + stem + " in the game folder";
        return nullptr;
    }
    std::ifstream in(it->second, std::ios::binary);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto sound = std::make_shared<audio::Sound>();
    if (Error err = audio::decodeSound(data, *sound)) {
        _soundStatus = stem + ": " + err.message;
        return nullptr;
    }
    return sound;
}

// What play adds over the map: the full-screen Tricorder (Tab), with the
// units on it, and the box a left drag draws.
void MissionViewer::drawPlay() {
    if (_tricorderFull && _hasMap) {
        const int size = std::min(_screen.w, _screen.h) - 40;
        const int x0 = (_screen.w - size) / 2;
        const int y0 = (_screen.h - size) / 2;
        for (int y = 0; y < _screen.h; ++y) {
            uint8_t *row = _screen.row(y);
            for (int x = 0; x < _screen.w; ++x, row += 4)
                row[0] = uint8_t(row[0] / 4), row[1] = uint8_t(row[1] / 4), row[2] = uint8_t(row[2] / 4);
        }
        _map.drawMinimap(_screen, x0, y0, size, size);
        if (_world)
            for (const Unit &u : _world->units()) {
                int px = 0;
                int py = 0;
                _map.minimapPoint(u.x, u.y, x0, y0, size, size, px, py);
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        if (px + dx >= 0 && py + dy >= 0 && px + dx < _screen.w && py + dy < _screen.h) {
                            uint8_t *p = _screen.row(py + dy) + (px + dx) * 4;
                            p[0] = u.selected ? 255 : 120, p[1] = u.selected ? 150 : 255, p[2] = u.selected ? 30 : 120;
                        }
            }
    }
    if (_boxing && (std::abs(_mouseX - _boxX) > 6 || std::abs(_mouseY - _boxY) > 6)) {
        const int x0 = std::min(_boxX, _mouseX);
        const int x1 = std::max(_boxX, _mouseX);
        const int y0 = std::min(_boxY, _mouseY);
        const int y1 = std::max(_boxY, _mouseY);
        auto put = [&](int x, int y) {
            if (x >= 0 && y >= 0 && x < _screen.w && y < _screen.h) {
                uint8_t *p = _screen.row(y) + x * 4;
                p[0] = 120, p[1] = 255, p[2] = 120;
            }
        };
        for (int x = x0; x <= x1; ++x)
            put(x, y0), put(x, y1);
        for (int y = y0; y <= y1; ++y)
            put(x0, y), put(x1, y);
    }
}

// The in-game interface, laid out as the recordings show it on the 640 x
// 480 screen; see docs/engines/newworlds/gui.md#the-in-game-interface.
void MissionViewer::drawInterface() {
    const Level &level = _missions[size_t(_current)];
    const char side = missionSides(level.shortName)[size_t(_side)];
    if (_uiSide != side) {
        _uiSide = side;
        _ui = GameUi{};
        _ui.load(_archives, side);
    }
    if (!_ui.loaded()) {
        drawText(kPanelMargin, kPanelMargin, "no GUI archive for this side", kNoteColor);
        return;
    }
    const int lh = _ui.smallHeight();
    const Color text = _ui.textColor();
    const Color bright = _ui.brightColor();
    // The right-hand panels are about 100 pixels wide in the recordings;
    // wide enough here for their longest lines in the font we have.
    const int sideW = std::max({100, _ui.smallWidth("COLONY SIZE = 20/50"), _ui.smallWidth("TRANSALUMIN = 5000"),
                                _ui.smallWidth("TERRAIN AT <000:000>")}) +
                      22;

    // The compass strip along the top: a tick every 5 degrees, a label
    // every 10, N E S W at the quarters, centred on the camera's heading.
    if (_hasMap) {
        const float heading = _map.heading();
        const int cx = _screen.w / 2;
        const int span = 120;
        const int pxPerDeg = 2;
        for (int d = -span / 2; d <= span / 2; ++d) {
            int deg = int(std::lround(heading)) + d;
            deg = ((deg % 360) + 360) % 360;
            const int x = cx + d * pxPerDeg;
            if (deg % 10 == 0) {
                static const char *kPoints[] = {"N", "E", "S", "W"};
                const std::string label =
                    deg % 90 == 0 ? kPoints[deg / 90] : (deg < 100 ? "0" : "") + std::to_string(deg);
                _ui.drawSmall(_screen, x - _ui.smallWidth(label) / 2, 2, label, deg % 90 == 0 ? bright : text);
            } else if (deg % 5 == 0) {
                _ui.drawSmall(_screen, x - 1, 2, "+", text);
            }
        }
    }

    // NEW WORLDS: the colony panel: the colony's size and stock (in play
    // the economy's; else what every recording started with), the
    // selection, or the build menu.
    {
        const auto menu = _buildMenu ? buildMenu() : std::vector<const BuildingType *>{};
        const int lines = _buildMenu ? int(menu.size()) + 1 : 7;
        const int x = _screen.w - sideW;
        const int y = 0;
        const int w = sideW;
        const int h = 24 + lines * lh + 8;
        _ui.drawPanel(_screen, x, y, w, h, "NEW WORLDS");
        // The Romulans start with a little less (most of their recordings).
        static const char *const kNames[] = {"DILITHIUM",   "POLYCOMP", "THERMOCONC",
                                             "TRANSALUMIN", "NITRIUM",  "DURANIUM"};
        static constexpr int kStart[] = {500, 1000, 2300, 500, 4000, 5000};
        static constexpr int kRomulan[] = {500, 988, 2211, 497, 3500, 5000};
        std::pair<const char *, int> kStock[6];
        for (int i = 0; i < 6; ++i)
            kStock[i] = {kNames[i], side == 'R' ? kRomulan[i] : kStart[i]};
        int ty = y + 16;
        const std::vector<int> sel = _world ? _world->selection() : std::vector<int>{};
        const BuildingType *placing = _placing.empty() ? nullptr : buildingType(_placing);
        if (_buildMenu) {
            // The menu: each type with its key; dim if what it needs
            // doesn't stand yet, plain if the stock can't pay for it.
            _ui.drawSmall(_screen, x + 10, ty, "BUILD", bright);
            const Color dim{uint8_t(text.r / 2), uint8_t(text.g / 2), uint8_t(text.b / 2)};
            for (size_t i = 0; i < menu.size(); ++i) {
                const BuildingType &t = *menu[i];
                const Color c = !_economy->available(t) ? dim : _economy->affordable(t) ? bright : text;
                _ui.drawSmall(_screen, x + 10, ty + lh * int(i + 1), std::to_string((i + 1) % 10) + " " + t.panel, c);
            }
        } else if (placing) {
            // Placing: the building and what it costs.
            _ui.drawSmall(_screen, x + 10, ty, placing->panel, bright);
            int line = 1;
            for (int m = 0; m < kMaterials; ++m)
                if (placing->cost[size_t(m)] > 0)
                    _ui.drawSmall(_screen, x + 10, ty + lh * line++,
                                  std::string(materialName(m)) + " = " + std::to_string(placing->cost[size_t(m)]),
                                  _economy->stock()[size_t(m)] >= placing->cost[size_t(m)] ? text : Color{255, 80, 60});
        } else if (sel.size() == 1) {
            // One unit: its name and state, as the game's panel.
            const Unit *u = _world->unit(sel[0]);
            _ui.drawSmall(_screen, x + 10, ty, u->kind.name, bright);
            _ui.drawSmall(_screen, x + 10, ty + lh, u->moving() ? "EN ROUTE" : "READY", text);
        } else if (sel.size() > 1) {
            _ui.drawSmall(_screen, x + 10, ty, "MULTISELECTION", bright);
            for (size_t i = 0; i < sel.size() && i < 6; ++i)
                _ui.drawSmall(_screen, x + 10, ty + lh * int(i + 1), _world->unit(sel[i])->kind.name, text);
        } else {
            if (_economy) {
                for (int i = 0; i < 6; ++i)
                    kStock[i].second = _economy->stock()[size_t(i)];
                _ui.drawSmall(_screen, x + 10, ty,
                              "COLONY SIZE = " + std::to_string(_economy->crew()) + "/" +
                                  std::to_string(_economy->capacity()),
                              bright);
            } else {
                _ui.drawSmall(_screen, x + 10, ty, "COLONY SIZE = 20/50", bright);
            }
            for (const auto &[name, n] : kStock) {
                ty += lh;
                _ui.drawSmall(_screen, x + 10, ty, std::string(name) + " = " + std::to_string(n), text);
            }
        }
    }

    // TRICORDER: the map small, and the camera's position.
    {
        const int x = _screen.w - sideW;
        const int w = sideW;
        const int h = 102;
        const int y = _screen.h - h;
        _ui.drawPanel(_screen, x, y, w, h, "TRICORDER");
        if (_hasMap) {
            _map.drawMinimap(_screen, x + 12, y + 14, w - 24, h - 30 - lh);
            if (_world)
                for (const Unit &u : _world->units()) {
                    int px = 0;
                    int py = 0;
                    _map.minimapPoint(u.x, u.y, x + 12, y + 14, w - 24, h - 30 - lh, px, py);
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                            if (px + dx >= 0 && py + dy >= 0 && px + dx < _screen.w && py + dy < _screen.h) {
                                uint8_t *p = _screen.row(py + dy) + (px + dx) * 4;
                                p[0] = 120, p[1] = 255, p[2] = 120;
                            }
                }
            int a = 0;
            int b = 0;
            _map.cameraPosition(a, b);
            auto pad = [](int v) {
                std::string s = std::to_string(v);
                return std::string(s.size() < 3 ? 3 - s.size() : 0, '0') + s;
            };
            _ui.drawSmall(_screen, x + 10, y + h - 12 - lh, "TERRAIN AT <" + pad(a) + ":" + pad(b) + ">", text);
        }
    }

    // COMMUNICATIONS: the script's latest message, with its sender's portrait.
    {
        const int w = 220;
        const int h = 70;
        const int x = (_screen.w - sideW - w) / 2 + 10;
        const int y = _screen.h - h;
        _ui.drawPanel(_screen, x, y, w, h, "COMMUNICATIONS");
        if (_log.empty()) {
            _ui.drawSmall(_screen, x + 60, y + 16, "NO MESSAGES ON FILE.", text);
        } else {
            const auto &[sender, message] = _log.back();
            const std::string natives = level.shortName == "C9" ? "orion" : "taubat";
            if (const Texture *p = _ui.portraitFor(sender, natives))
                _ui.drawPicture(_screen, *p, x + 10, y + 13, 44, 41);
            std::string up = sender + ":";
            std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            int ty = y + 14;
            _ui.drawSmall(_screen, x + 58, ty, up, text);
            for (const std::string &l : _ui.wrapSmall(message, w - 70)) {
                ty += lh;
                if (ty > y + h - 12 - lh)
                    break;
                _ui.drawSmall(_screen, x + 58, ty, l, text);
            }
            const std::string counter = std::to_string(_log.size()) + "/" + std::to_string(_log.size());
            _ui.drawSmall(_screen, x + w - 12 - _ui.smallWidth(counter), y + h - 11 - lh, counter, text);
        }
    }

    // COMMAND: the objectives, as the recordings' panel lists them.
    if (_run) {
        const MissionScript *script = findMissionScript(level.shortName, side);
        std::vector<std::pair<std::string, Color>> lines;
        const int w = 260;
        for (const MissionScript::Objective &o : script->objectives) {
            const MissionRun::State st = _run->objectiveState(o.id);
            if (st == MissionRun::State::kHidden)
                continue;
            const std::string *t = _table.find(o.id);
            const std::string s = "MISSION OBJECTIVE: " + (t ? *t : std::to_string(o.id)) +
                                  (st == MissionRun::State::kComplete ? " - COMPLETE" : " - INCOMPLETE");
            for (const std::string &l : _ui.wrapSmall(s, w - 24))
                lines.push_back({l, st == MissionRun::State::kComplete ? bright : text});
        }
        const int h = 30 + int(lines.size()) * lh;
        _ui.drawPanel(_screen, 0, 0, w, h, "COMMAND");
        for (size_t i = 0; i < lines.size(); ++i)
            _ui.drawSmall(_screen, 12, 15 + int(i) * lh, lines[i].first, lines[i].second);
    }
}

void MissionViewer::stepScript() {
    const Level &level = _missions[size_t(_current)];
    const char side = missionSides(level.shortName)[size_t(_side)];
    if (!_run) {
        const MissionScript *script = findMissionScript(level.shortName, side);
        if (!script) {
            _soundStatus = "no script for this mission yet";
            updateTitle();
            return;
        }
        stopBriefing();
        _run = std::make_unique<MissionRun>(*script, findMissionScript("generic", 'F'));
        _walk = _run->walkthrough();
        _walkPos = 0;
        _log.clear();
        _walkNote = "script loaded";
        _briefing = true;
        redraw();
        updateTitle();
        return;
    }
    if (_run->finished())
        return;
    std::vector<MissionRun::Output> out;
    ScriptEvent fired{"timer", {}};
    if (_walkPos < _walk.size()) {
        const ScriptEvent &e = _walk[_walkPos++];
        _walkNote = "event " + std::to_string(_walkPos) + "/" + std::to_string(_walk.size()) + ": " + e.kind;
        for (const std::string &a : e.args)
            _walkNote += " " + a;
        out = _run->fire(e);
        fired = e;
        // The colony grows as the script builds (on the missions page only:
        // the game's own building is the player's).
        if (e.kind == "built" && !e.args.empty() && _colony) {
            if (_colony->add(e.args[0]))
                showColony();
            else
                _walkNote += " (not placed)";
        }
    } else if (_run->nextTimer() >= 0) {
        _walkNote = "time passes (" + std::to_string(int(_run->nextTimer())) + " s)";
        out = _run->advance(_run->nextTimer());
    } else {
        _walkNote = "nothing more scripted";
    }
    deliver(fired, out);
    redraw();
    updateTitle();
}

// What the script said: its messages to the log, the game's spoken
// announcement for it (Media/Misc), if any, and the outcome in the note.
void MissionViewer::deliver(const ScriptEvent &fired, const std::vector<MissionRun::Output> &out) {
    const char side = missionSides(_missions[size_t(_current)].shortName)[size_t(_side)];
    if (_playing && !out.empty())
        _walkNote = fired.kind; // in play, the latest that said anything
    if (const std::string voice = announcementFor(fired, out, side); !voice.empty() && _mixer) {
        stopClip();
        if (auto sound = loadSound(voice))
            _clipHandle = _mixer->play(std::move(sound), audio::SoundType::Speech);
        else
            _soundStatus.clear(); // the game folder may not have Misc
        _walkNote += " [" + voice + "]";
    }
    for (const MissionRun::Output &o : out) {
        if (o.kind == MissionRun::Output::kMessage)
            _log.push_back({o.sender, o.text});
        else if (o.kind == MissionRun::Output::kWon)
            _walkNote += "; mission won";
        else if (o.kind == MissionRun::Output::kLost)
            _walkNote += "; mission lost";
    }
    if (!out.empty())
        redraw();
}

void MissionViewer::stopScript() {
    if (!_run)
        return;
    _run.reset();
    _walk.clear();
    _log.clear();
    startColony();
    redraw();
    updateTitle();
}

void MissionViewer::startBriefing() {
    if (_script.empty())
        return;
    _savedColouring = _map.colouring();
    _running = true;
    _briefing = true;
    startStep(0);
}

void MissionViewer::stopBriefing() {
    if (!_running)
        return;
    _running = false;
    stopClip();
    _map.setColouring(_savedColouring);
    _map.resetCamera();
    redraw();
    updateTitle();
}

void MissionViewer::startStep(int step) {
    stopClip();
    if (step >= int(_script.size())) {
        stopBriefing();
        return;
    }
    _step = step;
    const BriefingStep &s = _script[size_t(step)];
    _stepOnMap = _hasMap && (s.view == BriefingView::kGround || s.view == BriefingView::kRelief);
    if (_stepOnMap) {
        _map.setColouring(s.view == BriefingView::kRelief ? 1 : 0);
        _map.lookAt(s.siteA, s.siteB, s.view == BriefingView::kGround);
        _lastTurnMs = _nowMs;
    }
    // Its clip, if the game folder has it; else the step is shown a while.
    _stepEndMs = _nowMs + 5000;
    const auto it = std::find(_clips.begin(), _clips.end(), s.clip);
    if (!s.clip.empty() && it != _clips.end()) {
        playClip(int(it - _clips.begin()));
        if (_clipHandle != audio::kNoSound)
            _stepEndMs = 0;
    }
    redraw();
    updateTitle();
}

bool MissionViewer::mapInView() const {
    return _hasMap && (!_running || _stepOnMap);
}

namespace {

// A texture from whichever archive has it; false if none does or it doesn't decode.
bool loadTexture(const Archives &archives, const std::string &name, Texture &out) {
    for (const auto &a : archives)
        if (const CatEntry *e = a->find(name)) {
            std::vector<uint8_t> data;
            return !a->read(*e, data) && !decodeTexture(data, out);
        }
    return false;
}

} // namespace

void MissionViewer::loadBriefingArt() {
    // The emblem screens are six tiles, three across and two down:
    // 256 + 256 + 128 by 256 + 224, the 640 x 480 screen.
    const std::pair<char, const char *> sets[] = {
        {'F', "lbp/gui/ufp00"}, {'K', "lbp/gui/kliemp0"}, {'R', "lbp/gui/romemp0"}};
    for (const auto &[side, prefix] : sets) {
        Texture screen;
        screen.width = 640;
        screen.height = 480;
        screen.rgba.assign(size_t(640) * 480 * 4, 0);
        bool ok = true;
        for (int t = 0; t < 6 && ok; ++t) {
            Texture tile;
            ok = loadTexture(_archives, prefix + std::to_string(t + 1) + ".lbp", tile);
            const int x0 = (t % 3) * 256;
            const int y0 = (t / 3) * 256;
            for (int y = 0; ok && y < tile.height && y0 + y < 480; ++y)
                for (int x = 0; x < tile.width && x0 + x < 640; ++x)
                    std::copy_n(&tile.rgba[(size_t(y) * size_t(tile.width) + size_t(x)) * 4], 4,
                                &screen.rgba[(size_t(y0 + y) * 640 + size_t(x0 + x)) * 4]);
        }
        if (ok)
            _emblems[side] = std::move(screen);
    }
    // The galaxy: a slab of star field round three flat layers (blue arm,
    // red ribbon, stars), numbering tabras01-04 in name order.
    for (const auto &a : _archives)
        if (const CatEntry *e = a->find("geo/solsys/tabras.geo")) {
            std::vector<uint8_t> data;
            if (a->read(*e, data) || parseGeo(data, _galaxy))
                _galaxy = GeoModel{};
            break;
        }
    for (int t = 1; t <= 4 && !_galaxy.faces.empty(); ++t) {
        Texture tex;
        loadTexture(_archives, "lbp/txr/solsys/tabras0" + std::to_string(t) + ".mp0", tex);
        _galaxyTextures.push_back(std::move(tex));
    }
    // The star systems' models, by name (the galaxy isn't one).
    for (const auto &a : _archives)
        for (const CatEntry &e : a->entries()) {
            std::string folder;
            std::string stem;
            std::string ext;
            splitName(e.name, folder, stem, ext);
            if (folder == "solsys" && ext == "geo" && stem != "tabras" &&
                std::find(_systemNames.begin(), _systemNames.end(), stem) == _systemNames.end())
                _systemNames.push_back(stem);
        }
}

// The star system the mission's table names, with its pictures.
void MissionViewer::loadStarSystem() {
    _system = GeoModel{};
    _systemLayout = {};
    _systemTextures.clear();
    _systemName = starSystemName(_table, _systemNames);
    if (_systemName.empty())
        return;
    for (const auto &a : _archives)
        if (const CatEntry *e = a->find("geo/solsys/" + _systemName + ".geo")) {
            std::vector<uint8_t> data;
            if (a->read(*e, data) || parseGeo(data, _system))
                _system = GeoModel{};
            break;
        }
    if (_system.faces.empty())
        return;
    _systemLayout = readStarSystem(_system);
    for (const std::string &stem :
         starSystemTextures(_systemLayout, _hasMap ? planetPicture(_map.mapArchive()) : "", _systemName)) {
        Texture tex;
        if (!stem.empty())
            loadTexture(_archives, "lbp/txr/solsys/" + stem + ".mp0", tex);
        _systemTextures.push_back(std::move(tex));
    }
}

// The system from near the mission's planet, turning about it: the
// starfield adding its light behind (its alpha layer is its brightness,
// dimmed as in the recordings), the star and planets as stacks of round
// pictures, each turned to face the camera (so its slices stand one behind
// another, a ball from any side; seen edge-on they'd be a pile of discs).
bool MissionViewer::drawStarSystem() {
    if (_system.faces.empty())
        return false;
    for (int y = 0; y < _screen.h; ++y) {
        uint8_t *row = _screen.row(y);
        for (int x = 0; x < _screen.w; ++x, row += 4)
            row[0] = row[1] = row[2] = 0, row[3] = 255;
    }
    const int body = missionBody(_systemLayout);
    GeoModel::Vec3 target{0, 0, 0};
    if (body >= 0)
        target = _systemLayout.bodies[size_t(body)].centre;
    const float yaw = float(_nowMs % 120000) / 120000.0f * 6.2832f;
    const OrbitCamera cam({target.x, target.y, target.z}, yaw, 0.25f, 2200.0f, _screen.w, _screen.h);
    _raster.begin(_screen);
    for (int pass = 0; pass < 2; ++pass) // the starfield first
        for (size_t f = 0; f < _system.faces.size(); ++f) {
            const GeoModel::Face &face = _system.faces[f];
            const int slot = _systemLayout.faceSlot[f];
            const bool backdrop = std::find(_systemLayout.backdropSlots.begin(), _systemLayout.backdropSlots.end(),
                                            slot) != _systemLayout.backdropSlots.end();
            if (slot < 0 || size_t(slot) >= _systemTextures.size() || _systemTextures[size_t(slot)].rgba.empty() ||
                backdrop != (pass == 0) || face.corners.size() < 3 || face.part >= _system.parts.size())
                continue;
            const Texture &tex = _systemTextures[size_t(slot)];
            const auto &verts = _system.parts[face.part].vertices;
            const int body = _systemLayout.faceBody[f];
            std::vector<ViewVertex> poly;
            for (const GeoModel::Corner &c : face.corners) {
                if (c.vertex >= verts.size())
                    break;
                const GeoModel::Vec3 &p = verts[c.vertex];
                Vec at{p.x, p.y, p.z};
                if (body >= 0) {
                    const GeoModel::Vec3 &o = _systemLayout.bodies[size_t(body)].centre;
                    const Vec d{p.x - o.x, p.y - o.y, p.z - o.z};
                    at = Vec{o.x, o.y, o.z} + cam.right * d.x + cam.up * d.y - cam.forward * d.z;
                }
                poly.push_back(cam.toView(at, c.u, c.v));
            }
            if (poly.size() != face.corners.size())
                continue;
            poly = clipNear(poly, cam.nearZ);
            if (poly.size() < 3)
                continue;
            std::vector<ScreenVertex> s;
            for (const ViewVertex &v : poly)
                s.push_back(cam.project(v));
            auto shade = [&](float u, float v, uint8_t *out) {
                const int tx = std::clamp(int(u * float(tex.width)), 0, tex.width - 1);
                const int ty = std::clamp(int(v * float(tex.height)), 0, tex.height - 1);
                const size_t i = size_t(ty) * size_t(tex.width) + size_t(tx);
                const int a = tex.alpha.empty() ? 255 : tex.alpha[i];
                if (backdrop) {
                    for (int k = 0; k < 3; ++k)
                        out[k] = uint8_t(std::min(255, out[k] + tex.rgba[i * 4 + size_t(k)] * a / 512));
                    return false;
                }
                if (a < 128)
                    return false; // outside the disc
                for (int k = 0; k < 3; ++k)
                    out[k] = tex.rgba[i * 4 + size_t(k)];
                return true;
            };
            for (size_t i = 1; i + 1 < s.size(); ++i)
                _raster.drawTriangle(s[0], s[i], s[i + 1], shade);
        }
    return true;
}

bool MissionViewer::drawEmblem(char side) {
    const auto it = _emblems.find(side);
    if (it == _emblems.end())
        return false;
    const Texture &e = it->second;
    for (int y = 0; y < _screen.h; ++y) {
        uint8_t *row = _screen.row(y);
        const int sy = y * e.height / _screen.h;
        for (int x = 0; x < _screen.w; ++x, row += 4) {
            const uint8_t *p = &e.rgba[(size_t(sy) * size_t(e.width) + size_t(x * e.width / _screen.w)) * 4];
            row[0] = p[0], row[1] = p[1], row[2] = p[2], row[3] = 255;
        }
    }
    return true;
}

bool MissionViewer::drawGalaxy() {
    if (_galaxy.faces.empty())
        return false;
    for (int y = 0; y < _screen.h; ++y) {
        uint8_t *row = _screen.row(y);
        for (int x = 0; x < _screen.w; ++x, row += 4)
            row[0] = row[1] = row[2] = 0, row[3] = 255;
    }
    // Seen from above at an angle, turning; each layer adds its light (its
    // alpha layer is its brightness), so there's no depth to sort.
    const float yaw = float(_nowMs % 120000) / 120000.0f * 6.2832f;
    const OrbitCamera cam({0, 0, 0}, yaw, 0.45f, 16000.0f, _screen.w, _screen.h);
    _raster.begin(_screen);
    for (const GeoModel::Face &face : _galaxy.faces) {
        if (face.corners.size() < 3 || face.part >= _galaxy.parts.size())
            continue;
        const uint32_t ti = face.corners.back().extra[4];
        if (ti >= _galaxyTextures.size() || _galaxyTextures[ti].rgba.empty())
            continue;
        const Texture &tex = _galaxyTextures[ti];
        const auto &verts = _galaxy.parts[face.part].vertices;
        std::vector<ViewVertex> poly;
        for (const GeoModel::Corner &c : face.corners) {
            if (c.vertex >= verts.size())
                break;
            const GeoModel::Vec3 &p = verts[c.vertex];
            poly.push_back(cam.toView({p.x, p.y, p.z}, c.u, c.v));
        }
        if (poly.size() != face.corners.size())
            continue;
        poly = clipNear(poly, cam.nearZ);
        if (poly.size() < 3)
            continue;
        std::vector<ScreenVertex> s;
        for (const ViewVertex &v : poly)
            s.push_back(cam.project(v));
        auto shade = [&](float u, float v, uint8_t *out) {
            const int tx = std::clamp(int(u * float(tex.width)), 0, tex.width - 1);
            const int ty = std::clamp(int(v * float(tex.height)), 0, tex.height - 1);
            const size_t i = size_t(ty) * size_t(tex.width) + size_t(tx);
            const int a = tex.alpha.empty() ? 255 : tex.alpha[i];
            for (int k = 0; k < 3; ++k)
                out[k] = uint8_t(std::min(255, out[k] + tex.rgba[i * 4 + size_t(k)] * a / 255));
            return false;
        };
        for (size_t i = 1; i + 1 < s.size(); ++i)
            _raster.drawTriangle(s[0], s[i], s[i + 1], shade);
    }
    return true;
}

bool MissionViewer::drawModel(const std::string &name) {
    if (name.empty())
        return false;
    auto [it, added] = _models.try_emplace(name);
    BriefingModel &m = it->second;
    if (added)
        m.ok = loadTexturedModel(_archives, _textureFiles, name, m.model);
    if (!m.ok)
        return false;

    // The grid, as behind the recordings' model views: teal lines for the
    // Federation, dark red for the Klingons, green crosses for the Romulans.
    const Level &level = _missions[size_t(_current)];
    const char side = missionSides(level.shortName)[size_t(_side)];
    const uint8_t grid[3] = {uint8_t(side == 'F'   ? 20
                                     : side == 'R' ? 20
                                                   : 70),
                             uint8_t(side == 'F'   ? 45
                                     : side == 'R' ? 80
                                                   : 14),
                             uint8_t(side == 'F'   ? 60
                                     : side == 'R' ? 25
                                                   : 14)};
    const int w = _screen.w;
    const int h = _screen.h;
    for (int y = 0; y < h; ++y) {
        uint8_t *row = _screen.row(y);
        for (int x = 0; x < w; ++x, row += 4) {
            const bool line = side == 'R' ? (y % 24 == 0 && (x + 2) % 24 < 5) || (x % 24 == 0 && (y + 2) % 24 < 5)
                                          : (y % 24 == 0 && x % 4 == 0) || (x % 24 == 0 && y % 4 == 0);
            row[0] = line ? grid[0] : 0, row[1] = line ? grid[1] : 0, row[2] = line ? grid[2] : 0, row[3] = 255;
        }
    }
    // Turning once a minute, in the side's style.
    return drawModelView(_screen, _raster, m.model, float(_nowMs % 60000) / 60000.0f * 6.2832f, 0.35f,
                         briefingModelStyle(side));
}

// The briefing's views without a map: the game's galaxy, star system,
// models and emblem where the archives have them; else Reluva's own
// stand-ins.
void MissionViewer::drawBackdrop(BriefingView view) {
    const char side = missionSides(_missions[size_t(_current)].shortName)[size_t(_side)];
    if ((view == BriefingView::kGalaxy && drawGalaxy()) || (view == BriefingView::kEmblem && drawEmblem(side)) ||
        (view == BriefingView::kStarSystem && drawStarSystem()) ||
        (view == BriefingView::kModel && drawModel(_script[size_t(_step)].model)))
        return;
    const int w = _screen.w;
    const int h = _screen.h;
    for (int y = 0; y < h; ++y) {
        uint8_t *row = _screen.row(y);
        for (int x = 0; x < w; ++x, row += 4)
            row[0] = row[1] = row[2] = 0, row[3] = 255;
    }
    auto plot = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x >= 0 && y >= 0 && x < w && y < h) {
            uint8_t *p = _screen.row(y) + x * 4;
            p[0] = r, p[1] = g, p[2] = b;
        }
    };
    uint32_t seed = 12345;
    auto rnd = [&] { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    switch (view) {
    case BriefingView::kGalaxy:
        for (int i = 0; i < 900; ++i) {
            // A flattened disc of stars with a red and a blue arm.
            const float t = float(rnd() % 1000) / 1000.0f * 6.283f;
            const float rr = float(rnd() % 1000) / 1000.0f;
            const int x = w / 2 + int(std::cos(t) * rr * float(w) * 0.32f);
            const int y = h / 2 + int(std::sin(t) * rr * float(h) * 0.16f);
            const uint8_t v = uint8_t(120 + rnd() % 136);
            plot(x, y, v, v, v);
        }
        for (int i = 0; i < 300; ++i) {
            const int x = int(rnd() % unsigned(w / 3));
            const int d = int(rnd() % 9) - 4;
            plot(w / 3 + x, h / 2 - 30 + x / 4 + d, 200, 40, 40);
            plot(w / 3 + x, h / 2 + 20 - x / 5 + d, 60, 80, 220);
        }
        break;
    case BriefingView::kStarSystem:
        for (int i = 0; i < 400; ++i)
            plot(int(rnd() % unsigned(w)), int(rnd() % unsigned(h)), 90, 90, 110);
        for (int i = 0; i < 7; ++i) {
            const int cx = int(rnd() % unsigned(w));
            const int cy = int(rnd() % unsigned(h));
            const int r = 2 + int(rnd() % 5);
            const uint8_t cr = uint8_t(rnd() % 256);
            const uint8_t cg = uint8_t(rnd() % 256);
            const uint8_t cb = uint8_t(rnd() % 256);
            for (int y = -r; y <= r; ++y)
                for (int x = -r; x <= r; ++x)
                    if (x * x + y * y <= r * r)
                        plot(cx + x, cy + y, cr, cg, cb);
        }
        break;
    case BriefingView::kModel:
    case BriefingView::kGround:
    case BriefingView::kRelief:
        // A dim red grid, as the model views have (and the map views when
        // there's no map).
        for (int y = 0; y < h; y += 24)
            for (int x = 0; x < w; x += 4)
                plot(x, y, 70, 14, 14);
        for (int x = 0; x < w; x += 24)
            for (int y = 0; y < h; y += 4)
                plot(x, y, 70, 14, 14);
        drawText(kPanelMargin, kPanelMargin, view == BriefingView::kModel ? "(a model turning)" : _mapNote, kNoteColor);
        break;
    case BriefingView::kEmblem: {
        const char *name = side == 'K'   ? "KLINGON EMPIRE"
                           : side == 'R' ? "ROMULAN STAR EMPIRE"
                                         : "UNITED FEDERATION OF PLANETS";
        const Color c = side == 'K' ? Color{200, 60, 40} : side == 'R' ? Color{60, 170, 90} : Color{90, 140, 230};
        const int scale = 3;
        const int tw = int(font::length(name)) * font::kAdvance * scale;
        font::drawText(_screen, (w - tw) / 2, h / 3, name, c, scale);
        break;
    }
    }
}

bool MissionViewer::clipPlaying() const {
    return _mixer && _mixer->isPlaying(_clipHandle);
}

void MissionViewer::playClip(int index) {
    stopClip();
    _clip = index;
    _soundStatus.clear();
    if (_clips.empty())
        return;
    if (!_mixer) {
        _soundStatus = "no audio output";
        return;
    }
    if (auto sound = loadSound(_clips[size_t(_clip)]))
        _clipHandle = _mixer->play(std::move(sound), audio::SoundType::Speech);
}

void MissionViewer::stopClip() {
    if (_mixer)
        _mixer->stop(_clipHandle);
    _clipHandle = audio::kNoSound;
}

void MissionViewer::startMusic() {
    const std::string theme = _hasMap ? planetTheme(_map.mapArchive()) : std::string();
    if (_mixer && _musicHandle != audio::kNoSound && _mixer->isPlaying(_musicHandle) && theme == _musicName)
        return; // already playing it
    stopMusic();
    _musicName.clear();
    if (theme.empty()) {
        _soundStatus = "no music known for this map";
        return;
    }
    if (!_mixer) {
        _soundStatus = "no audio output";
        return;
    }
    if (auto sound = loadSound(theme)) {
        _musicName = theme;
        _musicHandle = _mixer->play(std::move(sound), audio::SoundType::Music, kMusicVolume, 0, true);
    }
}

void MissionViewer::stopMusic() {
    if (_mixer)
        _mixer->stop(_musicHandle);
    _musicHandle = audio::kNoSound;
}

} // namespace reluva::newworlds
