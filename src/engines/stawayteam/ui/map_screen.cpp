// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/map_screen.h"

#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/game/controls.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/main_menu.h"
#include "engines/stawayteam/ui/movies.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::stawayteam {

namespace {

// The ground's cells, square (0x4ef471).
constexpr int kCell = 64;
// The mouse this near the view's edge scrolls it (inferred).
constexpr int kEdge = 4;
// An officer's model's directions, 0 facing left and turning anticlockwise
// on the screen: 3 towards the viewer, 6 right, 9 away (seen in the
// frames).
constexpr int kDirections = 12;
// The selection's marker under the officer chosen, as the team selection
// puts it under a figure (TeamSelectionMemberSelectOffset Pos).
constexpr int kMarkerY = 19;
// An officer's sequences moving and standing, by how they move: walking,
// running, crouching (the body models' Walk, Run, Crouch, Idle,
// IdleCrouch).
const char *const kMoving[] = {"Walk", "Run", "Crouch"};
const char *const kStanding[] = {"Idle", "Idle", "IdleCrouch"};
// The info dialog stays this long after its voice-over (inferred), or this
// long without one (inferred), in seconds.
constexpr float kDialogAfter = 1.5f;
constexpr float kDialogWithoutVoice = 4.0f;
// A walk's speed for a model without one: the retail bodies' sideways,
// 26 pixels in 10 frames at 14 a second.
constexpr float kDefaultWalk = 36.4f;
// How far apart officers sent together stand (inferred).
constexpr float kSpacing = 24.0f;
// How far the mouse goes, held, before it's dragging a box (inferred).
constexpr int kDrag = 4;
// Too near a waypoint to walk any further to it.
constexpr float kArrived = 1.0f;
const char *const kExitQuestion = "Are you sure you want to quit the game and exit to the main menu?"; // 0x63a9e0

// Whether an action, or one in it, is of class `className`.
bool contains(const TextObject *action, const char *className) {
    if (!action)
        return false;
    if (action->className == className)
        return true;
    for (const TextField &f : action->fields)
        if (contains(f.object.get(), className))
            return true;
    return false;
}

// The items the inventory's keys choose, by the keys' names: an item whose
// name starts with the item's (inferred).
const std::vector<std::pair<const char *, const char *>> &itemKeys() {
    static const std::vector<std::pair<const char *, const char *>> kKeys = {
        {"Phaser", "Phaser"},
        {"Sniper Rifle", "Sniper Rifle"},
        {"Concussion Grenade", "Concussion"},
        {"EMP Grenade", "EM Pulse Grenade"},
        {"Neural Disruptor", "Phase Neural Disrupter"},
        {"Phaser Rifle", "Phaser Rifle"},
        {"Proximity Mines", "Proximity Mine"},
        {"Remote Mines", "Remote Mine"},
        {"Audio Decoy", "Audio Decoy"},
        {"Holographic Projector", "Holographic Projection"},
        {"Vulcan Nerve Pinch", "Vulcan Neck Pinch"},
        {"Herbal Paralysis", "Botanical Paralysis"},
        {"Hypospray", "Hypospray"},
        {"Cloaking Band", "Personal Visual Dampening"},
        {"Tricorder", "Tricorder"},
        {"Medical Tricorder", "Medical Tricorder"},
        {"Extractor", "Extractor"},
        {"Herbal Healing", "Botanical Healing"},
        {"Stasis Field Generator", "Portable Containmnet Field"},
        {"Teleportation Brig", "Telportation Brig"},
        {"Brig Controller", "Telportation Brig Controller"},
        {"Computer Systems", "Computer Systems"},
        {"Security Systems", "Security Doors & Systems"},
        {"Engineering Tech Kit", "Terminal Repair"},
    };
    return kKeys;
}

int actionIndex(const char *name) {
    const auto &actions = gameActions();
    for (size_t i = 0; i < actions.size(); ++i)
        if (std::strcmp(actions[i].name, name) == 0)
            return int(i);
    return -1;
}

const uint8_t *texel(const image::Image &img, int x, int y) {
    // Textures repeat across the map, from its corner (inferred).
    const int tx = ((x % img.w) + img.w) % img.w;
    const int ty = ((y % img.h) + img.h) % img.h;
    return img.pixel(tx, ty);
}

} // namespace

MapScreen::MapScreen(const Archive &data, const TextObject &rules, GameMap map, const GameOptions &options,
                     uint64_t now, std::vector<TeamMember> team, WayPoints wayPoints, audio::Mixer *mixer)
    : _data(data), _rules(rules), _map(std::move(map)), _options(options), _last(now), _team(std::move(team)),
      _wayPoints(std::move(wayPoints)), _mixer(mixer) {
    // The view: Rules.txt's Screen Pos and Screen Size.
    const TextObject *custom = rules.object("Custom Rules");
    readPoint(custom, "Screen Pos", _viewX, _viewY);
    readPoint(custom, "Screen Size", _viewW, _viewH);
    if (_viewW <= 0 || _viewH <= 0) {
        _viewW = 640;
        _viewH = 480;
    }

    // The ground's textures, and what each value draws (0x538d20): its
    // place among the textures, value x count / 256, picks a texture; past
    // 1 - Blending into its band, the next one shows over it, more of it
    // further in.
    const GroundMap &g = _map.ground;
    for (const std::string &name : g.textures)
        _groundTextures.push_back(cachePicture(data, "Textures/" + name));
    const int count = int(g.textures.size());
    const float blending = g.blending > 0.0f ? g.blending : 1.0f;
    for (int v = 0; v < 256 && count > 0; ++v) {
        const float place = float(v) * float(count) / 256.0f;
        const int i = int(place);
        float fraction = place - float(i);
        if (i + 1 >= count)
            fraction = 0.0f;
        const float into = fraction > 1.0f - blending ? (fraction - (1.0f - blending)) / blending : 0.0f;
        Blend &b = _blends[size_t(v)];
        b.texture = i;
        b.weight = int(std::lround(into * 255.0f));
        b.next = b.weight > 0 ? i + 1 : -1;
    }

    // The polygons laid on the ground, by the cells they cover, in the
    // map's order; the models, back to front by where they stand.
    const int cellsX = std::max(0, g.width - 1);
    const int cellsY = std::max(0, g.height - 1);
    _cellPolygons.resize(size_t(cellsX) * size_t(cellsY));
    for (size_t i = 0; i < _map.parts.size(); ++i) {
        const MapPart &p = _map.parts[i];
        if (!p.polygon.empty()) {
            // The ground's own, drawn into its cells; the Trans colours are
            // drawn over it while they're active (below).
            if (!p.visible || !p.active ||
                (p.renderType != "Textured" && p.renderType != "Black" && p.renderType != "Shadow")) {
                if (p.renderType.rfind("Trans ", 0) == 0)
                    _overlays.push_back(i);
                continue;
            }
            float x0 = p.polygon[0].first;
            float x1 = x0;
            float y0 = p.polygon[0].second;
            float y1 = y0;
            for (const auto &[x, y] : p.polygon) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
            for (int cy = std::max(0, int(y0) / kCell); cy <= std::min(cellsY - 1, int(y1) / kCell); ++cy)
                for (int cx = std::max(0, int(x0) / kCell); cx <= std::min(cellsX - 1, int(x1) / kCell); ++cx)
                    _cellPolygons[size_t(cy) * size_t(cellsX) + size_t(cx)].push_back(i);
        } else if (!p.model.empty()) {
            _standing.push_back(i);
        }
    }
    std::stable_sort(_standing.begin(), _standing.end(), [&](size_t a, size_t b) {
        return _map.parts[a].y + float(_map.parts[a].renderingHeight) <
               _map.parts[b].y + float(_map.parts[b].renderingHeight);
    });
    moveCamera(_map.viewX, _map.viewY);

    // The team at the map's spawn points, the first at Away Team Spawn
    // Point 1, facing as each says (inferred: the program's spawning isn't
    // traced).
    for (size_t i = 0; i < _team.size(); ++i) {
        Officer o;
        o.member = i;
        o.x = _map.viewX;
        o.y = _map.viewY;
        const std::string spawn = "Away Team Spawn Point " + std::to_string(i + 1);
        for (const MapPart &p : _map.parts)
            if (p.name == spawn) {
                o.x = p.x;
                o.y = p.y;
                o.direction = int(std::lround(p.facing / 30.0f)) % kDirections;
            }
        _officers.push_back(o);
    }
    if (!_officers.empty())
        choose(0);
    _hud = std::make_unique<GameHud>(data, rules, _team, _map.minimap, _map.width, _map.height);
    setupTriggers();
    setupEnemies();
    setupCombat();
    setupItems();
    runSpawnActions();

    // The map's camera scripts play as it starts. The team's spawn points
    // hide them (their CSpawnPointAI's CMakeVisibleAction): one of the
    // scripts beams them in. Without scripts, the view starts on the team
    // (inferred).
    _scripts = _map.cameraScripts;
    bool beams = false;
    for (const CameraScript &s : _scripts)
        beams = beams || contains(s.action, "CAddEffectToTeam");
    _teamShown = !beams;
    if (_scripts.empty() && !_officers.empty())
        moveCamera(_officers[0].x, _officers[0].y);
    _dialogBackground = screenPicture(data, "Dialog Window/Background");
    (void)_dialogFont.load(data, readText(custom, "Dialog Font"));
}

MapScreen::~MapScreen() = default;

void MapScreen::moveCamera(float x, float y) {
    // Kept so the view stays on the map (inferred).
    const float halfW = float(_viewW) / 2.0f;
    const float halfH = float(_viewH) / 2.0f;
    _cameraX = _map.width > _viewW ? std::clamp(x, halfW, float(_map.width) - halfW) : float(_map.width) / 2.0f;
    _cameraY = _map.height > _viewH ? std::clamp(y, halfH, float(_map.height) - halfH) : float(_map.height) / 2.0f;
}

const image::Image &MapScreen::texture(const std::string &name) {
    auto it = _textures.find(name);
    if (it == _textures.end())
        it = _textures.emplace(name, cachePicture(_data, "Textures/" + name)).first;
    return it->second;
}

void MapScreen::drawPolygon(image::Image &cell, int cellX, int cellY, const MapPart &part) {
    const image::Image *tex = part.renderType == "Textured" ? &texture(part.texture) : nullptr;
    if (part.renderType == "Textured" && (!tex || !tex->w))
        return;
    const auto &pts = part.polygon;
    // Each row's crossings of the polygon's sides, filled between pairs.
    for (int row = 0; row < cell.h; ++row) {
        const float y = float(cellY + row) + 0.5f;
        std::vector<float> xs;
        for (size_t k = 0; k + 1 < pts.size(); ++k) {
            const auto [ax, ay] = pts[k];
            const auto [bx, by] = pts[k + 1];
            if ((ay <= y && by > y) || (by <= y && ay > y))
                xs.push_back(ax + (y - ay) * (bx - ax) / (by - ay));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            const int from = std::max(0, int(std::ceil(xs[k] - 0.5f)) - cellX);
            const int to = std::min(cell.w, int(std::ceil(xs[k + 1] - 0.5f)) - cellX);
            for (int col = from; col < to; ++col) {
                uint8_t *p = cell.pixel(col, row);
                if (tex) {
                    const uint8_t *t = texel(*tex, cellX + col, cellY + row);
                    if (t[3] == 0)
                        continue;
                    const int a = t[3];
                    for (int c = 0; c < 3; ++c)
                        p[c] = uint8_t((t[c] * a + p[c] * (255 - a)) / 255);
                } else if (part.renderType == "Black") {
                    p[0] = p[1] = p[2] = 0;
                } else {
                    // A shadow halves what's under it (inferred).
                    for (int c = 0; c < 3; ++c)
                        p[c] = uint8_t(p[c] / 2);
                }
            }
        }
    }
}

const image::Image &MapScreen::groundCell(int cx, int cy) {
    const GroundMap &g = _map.ground;
    const int cellsX = std::max(0, g.width - 1);
    const int key = cy * 4096 + cx;
    auto it = _cells.find(key);
    if (it != _cells.end())
        return it->second;
    image::Image cell;
    cell.w = kCell;
    cell.h = kCell;
    cell.rgba.assign(size_t(kCell) * kCell * 4, 0);
    for (size_t i = 3; i < cell.rgba.size(); i += 4)
        cell.rgba[i] = 255;
    if (cx >= 0 && cy >= 0 && cx < cellsX && cy + 1 < g.height) {
        // Each pixel's value between its cell's four corners', taken
        // straight across (the program's own adds a fractal's roughness,
        // seeded by the map: not yet).
        const auto corner = [&](int x, int y) { return float(g.values[size_t(y) * size_t(g.width) + size_t(x)]); };
        const float v00 = corner(cx, cy);
        const float v10 = corner(cx + 1, cy);
        const float v01 = corner(cx, cy + 1);
        const float v11 = corner(cx + 1, cy + 1);
        for (int y = 0; y < kCell; ++y) {
            const float fy = float(y) / float(kCell);
            for (int x = 0; x < kCell; ++x) {
                const float fx = float(x) / float(kCell);
                const float v = (v00 * (1 - fx) + v10 * fx) * (1 - fy) + (v01 * (1 - fx) + v11 * fx) * fy;
                const Blend &b = _blends[size_t(std::clamp(int(std::lround(v)), 0, 255))];
                if (b.texture < 0 || size_t(b.texture) >= _groundTextures.size() || !_groundTextures[b.texture].w)
                    continue;
                const int wx = cx * kCell + x;
                const int wy = cy * kCell + y;
                const uint8_t *a = texel(_groundTextures[size_t(b.texture)], wx, wy);
                uint8_t *p = cell.pixel(x, y);
                if (b.next >= 0 && size_t(b.next) < _groundTextures.size() && _groundTextures[b.next].w) {
                    const uint8_t *n = texel(_groundTextures[size_t(b.next)], wx, wy);
                    for (int c = 0; c < 3; ++c)
                        p[c] = uint8_t((a[c] * (255 - b.weight) + n[c] * b.weight) / 255);
                } else {
                    std::memcpy(p, a, 3);
                }
            }
        }
        for (size_t i : _cellPolygons[size_t(cy) * size_t(cellsX) + size_t(cx)])
            drawPolygon(cell, cx * kCell, cy * kCell, _map.parts[i]);
    }
    return _cells.emplace(key, std::move(cell)).first->second;
}

void MapScreen::scroll(float seconds) {
    // The options' Scroll Speed in pixels a second (inferred; its
    // acceleration isn't there yet).
    float dx = 0.0f;
    float dy = 0.0f;
    const bool inView = _mouseX >= _viewX && _mouseY >= _viewY && _mouseX < _viewX + _viewW + kEdge &&
                        _mouseY < _viewY + _viewH + kEdge;
    if (_left || (inView && _mouseX < _viewX + kEdge))
        dx -= 1.0f;
    if (_right || (inView && _mouseX >= _viewX + _viewW - kEdge))
        dx += 1.0f;
    if (_up || (inView && _mouseY < _viewY + kEdge))
        dy -= 1.0f;
    if (_down || (inView && _mouseY >= _viewY + _viewH - kEdge))
        dy += 1.0f;
    if (dx != 0.0f || dy != 0.0f)
        moveCamera(_cameraX + dx * _options.scrollSpeed * seconds, _cameraY + dy * _options.scrollSpeed * seconds);
}

const Animation &MapScreen::animation(const std::string &modelName, const std::string &sequence) {
    const std::string key = modelName + "\n" + sequence;
    auto it = _models.find(key);
    if (it == _models.end()) {
        Animation a;
        (void)a.load(_data, modelName, sequence);
        it = _models.emplace(key, std::move(a)).first;
    }
    return it->second;
}

int MapScreen::officerAt(float x, float y) {
    // About their figure: the frames reach from 19 above where they stand
    // to 23 under it (inferred from the walk's bounds).
    for (size_t i = 0; i < _officers.size(); ++i)
        if (_officers[i].alive() && std::abs(_officers[i].x - x) <= 12.0f && y >= _officers[i].y - 20.0f &&
            y <= _officers[i].y + 24.0f)
            return int(i);
    return -1;
}

void MapScreen::choose(int officer, bool add) {
    if (officer < 0 || size_t(officer) >= _officers.size())
        return;
    if (!add)
        for (Officer &o : _officers)
            o.chosen = false;
    Officer &o = _officers[size_t(officer)];
    o.chosen = o.alive() && (add ? !o.chosen : true);
    _selected = -1;
    if (o.chosen)
        _selected = officer;
    else
        for (size_t i = 0; i < _officers.size() && _selected < 0; ++i)
            if (_officers[i].chosen)
                _selected = int(i);
}

void MapScreen::chooseIn(float x0, float y0, float x1, float y1, bool add) {
    if (x1 < x0)
        std::swap(x0, x1);
    if (y1 < y0)
        std::swap(y0, y1);
    if (!add)
        for (Officer &o : _officers)
            o.chosen = false;
    for (Officer &o : _officers)
        if (o.alive() && o.x >= x0 && o.x <= x1 && o.y >= y0 && o.y <= y1)
            o.chosen = true;
    _selected = -1;
    for (size_t i = 0; i < _officers.size() && _selected < 0; ++i)
        if (_officers[i].chosen)
            _selected = int(i);
}

void MapScreen::chooseAll() {
    for (size_t i = 0; i < _officers.size(); ++i) {
        _officers[i].chosen = _officers[i].alive();
        if (_selected < 0 && _officers[i].chosen)
            _selected = int(i);
    }
}

void MapScreen::order(float x, float y) {
    // Side by side about the place, the first chosen on it, the rest round
    // them a figure's width apart (inferred: the game's grouping isn't
    // traced).
    int k = 0;
    for (Officer &o : _officers) {
        if (!o.chosen)
            continue;
        const float angle = float(k - 1) * 1.5707963f;
        const float tx = k == 0 ? x : x + kSpacing * std::cos(angle);
        const float ty = k == 0 ? y : y + kSpacing * 0.7f * std::sin(angle);
        o.path = _wayPoints.points.empty() ? std::vector<std::pair<float, float>>{{tx, ty}}
                                           : _wayPoints.path(o.x, o.y, tx, ty);
        o.attacking = -1;
        ++k;
    }
}

void MapScreen::setMove(int move) {
    for (Officer &o : _officers)
        if (o.chosen) {
            o.move = std::clamp(move, 0, 2);
            o.travelled = 0.0f;
        }
}

void MapScreen::walk(Officer &o, float seconds) {
    if (o.path.empty() || o.down())
        return;
    const Animation &walking = animation(_team[o.member].bodyModel, kMoving[o.move]);
    float left = seconds;
    while (!o.path.empty() && left > 0.0f) {
        const float dx = o.path.front().first - o.x;
        const float dy = o.path.front().second - o.y;
        const float distance = std::hypot(dx, dy);
        if (distance < kArrived) {
            o.path.erase(o.path.begin());
            continue;
        }
        // Facing the way they go; at the walk's own pace, a cycle's
        // distance in a cycle's frames (inferred).
        const float angle = std::atan2(dy, dx);
        o.direction = ((6 - int(std::lround(angle / (3.14159265f / 6.0f)))) % kDirections + kDirections) % kDirections;
        const float cycle = walking.cycleDistance(o.direction);
        const int per = walking.perDirection();
        const float speed =
            cycle > 0.0f && per > 0 && walking.fps > 0.0f ? cycle * walking.fps / float(per) : kDefaultWalk;
        const float step = std::min(distance, speed * left);
        o.x += dx / distance * step;
        o.y += dy / distance * step;
        // The footsteps its frames passed make their noise (the
        // sequence's Trigger, 0x518100).
        if (cycle > 0.0f && per > 0) {
            const int from = int(o.travelled / cycle * float(per));
            const int to = int((o.travelled + step) / cycle * float(per));
            for (int f = from + 1; f <= to; ++f)
                for (const Animation::Noise &n : walking.noises)
                    if (n.frame == f % per)
                        makeNoise(o.x, o.y, footstepNoise(o, n), n.age);
        }
        o.travelled = cycle > 0.0f ? std::fmod(o.travelled + step, cycle) : 0.0f;
        left -= step / speed;
    }
}

void MapScreen::showDialog(const std::string &script, const std::string &line, const std::string &speaker) {
    _dialogText.clear();
    std::vector<uint8_t> bytes;
    if (!_data.read("Levels/" + script + ".RDS", bytes)) {
        const RdsScript rds = readRdsScript(std::string(bytes.begin(), bytes.end()));
        if (const std::string *text = rds.find(line))
            _dialogText = *text;
    }
    _dialogIcon = cachePicture(_data, "Character/Character Icons/" + speaker);
    if (_mixer && _dialogVoice != audio::kNoSound)
        _mixer->stop(_dialogVoice);
    _dialogVoice = audio::kNoSound;
    // Shown while its voice-over plays, or for as long as it would
    // (0x4362f1), and a moment after (inferred).
    const auto voice = voiceOver(_data, line);
    _dialogLeft = kDialogAfter;
    if (voice && voice->rate > 0 && voice->channels > 0) {
        const float length = float(voice->samples.size()) / float(voice->rate * voice->channels);
        if (_options.playVoiceovers && _mixer)
            _dialogVoice = _mixer->play(voice, audio::SoundType::kSpeech);
        else
            _dialogLeft += length;
    } else {
        _dialogLeft = kDialogWithoutVoice;
    }
}

void MapScreen::playScripts(float seconds) {
    for (size_t i = 0; i < _delayed.size();) {
        _delayed[i].left -= seconds;
        if (_delayed[i].left <= 0.0f) {
            const Delayed d = _delayed[i];
            _delayed.erase(_delayed.begin() + ptrdiff_t(i));
            runAction(d.action, d.instigator, d.trigger);
        } else {
            ++i;
        }
    }
    if (_scripts.empty())
        return;
    const CameraScript &s = _scripts.front();
    if (!_scriptBegun) {
        // From where the view is to the part named (0x477560).
        _scriptBegun = true;
        _scriptWaiting = false;
        _panTime = 0.0f;
        _panStartX = _cameraX;
        _panStartY = _cameraY;
        _panDirX = 0.0f;
        _panDirY = 0.0f;
        _panDistance = 0.0f;
        for (const MapPart &p : _map.parts)
            if (p.name == s.where) {
                _panDirX = p.x - _cameraX;
                _panDirY = p.y - _cameraY;
                _panDistance = std::hypot(_panDirX, _panDirY);
                if (_panDistance > 0.0f) {
                    _panDirX /= _panDistance;
                    _panDirY /= _panDistance;
                }
                break;
            }
    }
    if (!_scriptWaiting) {
        // Eased in and out over Time To Pan; there at once without Pan To
        // Position (0x4776f7).
        _panTime = std::min(_panTime + seconds, s.timeToPan);
        const float t = s.timeToPan > 0.0f && s.panToPosition ? _panTime / s.timeToPan : 1.0f;
        const float eased = (1.0f - std::cos(t * 3.14159265f)) / 2.0f;
        moveCamera(_panStartX + _panDirX * _panDistance * eased, _panStartY + _panDirY * _panDistance * eased);
        if (_panTime >= s.timeToPan) {
            _scriptWaiting = true;
            _waitLeft = s.timeToWait;
            const TextObject *action = s.action;
            runAction(action); // may add scripts: `s` isn't used after
        }
        return;
    }
    _waitLeft -= seconds;
    if (_waitLeft <= 0.0f) {
        _scripts.erase(_scripts.begin());
        _scriptBegun = false;
    }
}

void MapScreen::update(uint64_t now) {
    const float seconds = float(now - std::min(now, _last)) / 1000.0f;
    _last = now;
    // The windows over the map hold the game still (inferred).
    if (_endGame || _objectivesWindow)
        return;
    playScripts(seconds);
    _warpLeft = std::max(0.0f, _warpLeft - seconds);
    if (!_dialogText.empty()) {
        const bool speaking = _mixer && _dialogVoice != audio::kNoSound && _mixer->isPlaying(_dialogVoice);
        if (!speaking) {
            _dialogLeft -= seconds;
            if (_dialogLeft <= 0.0f)
                _dialogText.clear();
        }
    }
    // The camera's scripts move the view meanwhile (inferred).
    if (_scripts.empty())
        scroll(seconds);
    for (Officer &o : _officers)
        walk(o, seconds);
    // The triggers see where everyone has got to.
    updateNoises(seconds);
    updateEnemies(seconds);
    updateCombat(seconds);
    updateItems(seconds);
    updateTriggers(seconds);
}

void MapScreen::drawOfficer(Surface &view, const Officer &o, int left, int top, uint64_t now) {
    if (!_teamShown)
        return;
    const TeamMember &m = _team[o.member];
    int fought = 0;
    const std::string fighting = combatSequence(o.shooting, o.dead, o.stunned, fought, m.bodyModel, o.discharge);
    const bool walking = !o.path.empty() && fighting.empty();
    const std::string sequence = !fighting.empty() ? fighting : walking ? kMoving[o.move] : kStanding[o.move];
    const Animation &body = animation(m.bodyModel, sequence);
    const Animation &shadow = animation(m.bodyModel, sequence + " Shadow");
    const Animation &head = animation(m.headModel, sequence);
    // Walking, the frame is how far into the cycle they are; standing, by
    // the time (inferred). The head is drawn over the body, frame for frame
    // (its model's Overlay Model).
    int frame = !fighting.empty() ? fought : int(double(now) * double(body.fps) / 1000.0) % 100000;
    if (walking) {
        const float cycle = body.cycleDistance(o.direction);
        frame = cycle > 0.0f ? int(o.travelled / cycle * float(body.perDirection())) : frame;
    }
    const int x = int(std::lround(o.x)) - left;
    const int y = int(std::lround(o.y)) - top;
    // Beaming in: fading in, the transporter's sparkle over them through
    // the warp (inferred).
    const float warped = _warpTime > 0.0f ? 1.0f - _warpLeft / _warpTime : 1.0f;
    const int opacity = int(std::lround(255.0f * std::clamp(warped, 0.0f, 1.0f)));
    blitBy(view, shadow.frame(frame, o.direction), x, y, 1.0f, opacity);
    blitBy(view, body.frame(frame, o.direction), x, y, 1.0f, opacity);
    blitBy(view, head.frame(frame, o.direction), x, y, 1.0f, opacity);
    if (_warpLeft > 0.0f) {
        const Animation &sparkle = animation("Transporter Overlay", "");
        blitBy(view, sparkle.frame(int(warped * float(sparkle.perDirection())), 0), x, y);
    }
}

MapScreen::Choice MapScreen::handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now) {
    if (_endGame) {
        switch (_endGame->handleEvent(ev, mixer)) {
        case EndGameWindow::Choice::kContinue:
            return Choice::kContinue;
        case EndGameWindow::Choice::kRestart:
            return Choice::kRestart;
        case EndGameWindow::Choice::kEndGame:
            return Choice::kExit;
        default:
            return Choice::kNone;
        }
    }
    if (_objectivesWindow) {
        if (_objectivesWindow->handleEvent(ev, mixer))
            _objectivesWindow.reset();
        return Choice::kNone;
    }
    if (_confirm) {
        const std::optional<bool> answer = _confirm->handleEvent(ev, mixer);
        if (answer && *answer)
            return Choice::kExit;
        if (answer)
            _confirm.reset();
        return Choice::kNone;
    }
    if (ev.type == EventType::kMouseMove) {
        _mouseX = ev.mouseX;
        _mouseY = ev.mouseY;
    }
    const GameHud::Result hud = _hud->handleEvent(ev, now);
    if (hud.select >= 0) {
        // A click on an officer's slot chooses them, with Shift adds them
        // or takes them away; a double click shows them too (inferred).
        choose(hud.select, (ev.mods & keymod::kShift) != 0);
        if (ev.clicks >= 2)
            moveCamera(_officers[size_t(_selected)].x, _officers[size_t(_selected)].y);
    }
    if (hud.moveCamera)
        moveCamera(hud.cameraX, hud.cameraY);
    // The move button goes round walking, running and crouching (inferred).
    if (hud.cycleMove && _selected >= 0)
        setMove((_officers[size_t(_selected)].move + 1) % 3);
    if (hud.selectAll)
        chooseAll();
    if (hud.item >= 0)
        chooseItem(_selected, hud.item);
    if (hud.toggleSound)
        _listening = !_listening;
    if (hud.objectives)
        _objectivesWindow = std::make_unique<ObjectivesWindow>(_data, _rules, 640, 480);
    // On the map, the left button: a click on an officer chooses them
    // (Shift: adds or takes them away), a box dragged chooses those in it,
    // a click on an enemy sends them to shoot it, a click elsewhere sends
    // them there (inferred: the mouse's scheme isn't traced).
    const auto toMapX = [&](int sx) { return _cameraX - float(_viewW) / 2.0f + float(sx - _viewX); };
    const auto toMapY = [&](int sy) { return _cameraY - float(_viewH) / 2.0f + float(sy - _viewY); };
    const bool inView =
        ev.mouseX >= _viewX && ev.mouseY >= _viewY && ev.mouseX < _viewX + _viewW && ev.mouseY < _viewY + _viewH;
    const bool playing = _scripts.empty() && _teamShown;
    if (!hud.used && playing && inView && ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _pressed = true;
        _dragging = false;
        _pressX = ev.mouseX;
        _pressY = ev.mouseY;
    }
    if (_pressed && ev.type == EventType::kMouseMove &&
        (std::abs(ev.mouseX - _pressX) > kDrag || std::abs(ev.mouseY - _pressY) > kDrag))
        _dragging = true;
    if (_pressed && ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _pressed = false;
        const bool add = (ev.mods & keymod::kShift) != 0;
        if (_dragging) {
            chooseIn(toMapX(_pressX), toMapY(_pressY), toMapX(ev.mouseX), toMapY(ev.mouseY), add);
        } else if (useItem(toMapX(_pressX), toMapY(_pressY))) {
            // The chosen officer's item used there.
        } else if (const int officer = officerAt(toMapX(_pressX), toMapY(_pressY)); officer >= 0) {
            choose(officer, add);
        } else if (const int enemy = enemyAt(toMapX(_pressX), toMapY(_pressY)); enemy >= 0) {
            attack(enemy);
        } else {
            order(toMapX(_pressX), toMapY(_pressY));
        }
        _dragging = false;
    }
    if (ev.type == EventType::kKeyDown || ev.type == EventType::kKeyUp) {
        const bool down = ev.type == EventType::kKeyDown;
        if (down && ev.key == key::kEscape) {
            _confirm = std::make_unique<ConfirmDialog>(_data, _rules, kExitQuestion, 640, 480);
            return Choice::kNone;
        }
        // The keys bound to scrolling the map (the keyboard's settings).
        const int code = scanCode(ev.key);
        const auto bound = [&](const char *action) {
            const int i = actionIndex(action);
            return i >= 0 && code != 0 && _options.controls.binding(size_t(i)).key == code;
        };
        if (bound("Scroll Map Left"))
            _left = down;
        if (bound("Scroll Map Right"))
            _right = down;
        if (bound("Scroll Map Up"))
            _up = down;
        if (bound("Scroll Map Down"))
            _down = down;
        if (down && bound("Walk"))
            setMove(0);
        if (down && bound("Run"))
            setMove(1);
        if (down && bound("Crouch"))
            setMove(2);
        if (down && bound("Select Entire Team") && playing)
            chooseAll();
        // Display Objectives presses the objectives button (0x476c90).
        if (down && bound("Display Objectives") && playing) {
            _objectivesWindow = std::make_unique<ObjectivesWindow>(_data, _rules, 640, 480);
            return Choice::kNone;
        }
        // Toggle Sound Cones presses the sound button (0x476c3f).
        if (down && bound("Toggle Sound Cones"))
            _listening = !_listening;
        // Inventory Next and Prev go along the chosen officer's items; an
        // item's own key chooses it for the officers chosen who have it
        // (inferred: by the keys' names).
        if (down && _selected >= 0 && (bound("Inventory Next") || bound("Inventory Prev"))) {
            const Officer &o = _officers[size_t(_selected)];
            const int n = int(o.items.size());
            if (n > 0)
                chooseItem(_selected, ((o.item < 0 ? 0 : o.item) + (bound("Inventory Next") ? 1 : n - 1)) % n);
        }
        if (down)
            for (const auto &[action, item] : itemKeys())
                if (bound(action))
                    for (size_t i = 0; i < _officers.size(); ++i)
                        if (_officers[i].chosen)
                            for (size_t k = 0; k < _officers[i].items.size(); ++k)
                                if (_officers[i].items[k].name.rfind(item, 0) == 0) {
                                    chooseItem(int(i), int(k));
                                    break;
                                }
        if (down && bound("Cycle Movement") && _selected >= 0)
            setMove((_officers[size_t(_selected)].move + 1) % 3);
    }
    return Choice::kNone;
}

void MapScreen::drawDialog(Surface &screen) {
    if (_dialogText.empty())
        return;
    // At Rules.txt's Dialog Pos: the speaker's icon, and the words wrapped
    // to Dialog Text Width (0x435db6).
    const TextObject *custom = _rules.object("Custom Rules");
    int x = 0;
    int y = 0;
    int ix = 0;
    int iy = 0;
    int tx = 0;
    int ty = 0;
    readPoint(custom, "Dialog Pos", x, y);
    readPoint(custom, "Dialog Icon Pos", ix, iy);
    readPoint(custom, "Dialog Text Pos", tx, ty);
    const int width = custom ? int(custom->integer("Dialog Text Width").value_or(0)) : 0;
    image::blit(screen, _dialogBackground, x, y);
    image::blit(screen, _dialogIcon, x + ix, y + iy);
    if (_dialogFont.loaded())
        _dialogFont.drawWrapped(screen, x + tx, y + ty, width, _dialogText, Font::Align::kLeft);
}

void MapScreen::draw(Surface &screen, uint64_t now) {
    update(now);
    clearScreen(screen);
    const int left = int(std::lround(_cameraX)) - _viewW / 2;
    const int top = int(std::lround(_cameraY)) - _viewH / 2;
    // Drawn into the view alone: a picture of it, put on the screen.
    Surface view;
    view.create(_viewW, _viewH, PixelFormat::kRGBA32);
    for (int cy = std::max(0, top / kCell); cy <= (top + _viewH) / kCell; ++cy)
        for (int cx = std::max(0, left / kCell); cx <= (left + _viewW) / kCell; ++cx)
            image::blit(view, groundCell(cx, cy), cx * kCell - left, cy * kCell - top);
    // The guides laid on the ground while they're active: the Trans
    // polygons, half see-through in their colour (inferred for how).
    for (size_t i : _overlays) {
        if (!shown(i))
            continue;
        const MapPart &p = _map.parts[i];
        const std::string &type = p.renderType;
        const uint8_t r = type == "Trans Red" ? 255 : 0;
        const uint8_t g = type == "Trans Green" ? 255 : 0;
        const uint8_t b = type == "Trans Blue" ? 255 : 0;
        for (int y = 0; y < _viewH; ++y) {
            const float wy = float(top + y) + 0.5f;
            std::vector<float> xs;
            for (size_t k = 0; k + 1 < p.polygon.size(); ++k) {
                const auto [ax, ay] = p.polygon[k];
                const auto [bx, by] = p.polygon[k + 1];
                if ((ay <= wy && by > wy) || (by <= wy && ay > wy))
                    xs.push_back(ax + (wy - ay) * (bx - ax) / (by - ay));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2)
                for (int x = std::max(0, int(xs[k]) - left); x < std::min(_viewW, int(xs[k + 1]) - left); ++x) {
                    uint8_t *px = view.row(y) + x * 4;
                    px[0] = uint8_t((px[0] + r) / 2);
                    px[1] = uint8_t((px[1] + g) / 2);
                    px[2] = uint8_t((px[2] + b) / 2);
                }
        }
    }
    // The selection's marker on the ground under each officer chosen.
    if (_teamShown)
        for (const Officer &o : _officers)
            if (o.chosen)
                blitBy(view, animation("Interface/Selection", "Idle").at(now), int(std::lround(o.x)) - left,
                       int(std::lround(o.y)) + kMarkerY - top);
    // The models and the officers among them, back to front by where they
    // stand.
    struct Standing {
        float y;
        int part; // or -1 for an officer
        size_t officer;
    };
    std::vector<Standing> order;
    for (size_t i : _standing) {
        const MapPart &p = _map.parts[i];
        if (!shown(i))
            continue;
        if (p.x < float(left - 600) || p.x > float(left + _viewW + 600) || p.y < float(top - 600) ||
            p.y > float(top + _viewH + 600))
            continue;
        const auto enemy = _enemyOfPart.find(i);
        const float y = enemy != _enemyOfPart.end() ? _enemies[enemy->second].y : p.y;
        order.push_back({y + float(p.renderingHeight), int(i), 0});
    }
    for (size_t i = 0; i < _officers.size(); ++i)
        order.push_back({_officers[i].y, -1, i});
    std::stable_sort(order.begin(), order.end(), [](const Standing &a, const Standing &b) { return a.y < b.y; });
    for (const Standing &s : order) {
        if (s.part < 0) {
            drawOfficer(view, _officers[s.officer], left, top, now);
            continue;
        }
        const MapPart &p = _map.parts[size_t(s.part)];
        // Facing its way, its shadow under it and its head over it, frame
        // for frame (inferred: as the officers').
        // An enemy where its AI has taken it, walking or standing.
        const auto enemy = _enemyOfPart.find(size_t(s.part));
        const Enemy *e = enemy != _enemyOfPart.end() ? &_enemies[enemy->second] : nullptr;
        std::string sequence = p.sequence.empty() ? "Idle" : p.sequence;
        if (e && (sequence == "Idle" || sequence == "Walk"))
            sequence = e->walking ? (e->running ? "Run" : "Walk") : "Idle";
        int fought = 0;
        const std::string fighting = e ? combatSequence(e->shooting, e->dead, e->stunned, fought, p.model) : "";
        if (!fighting.empty())
            sequence = fighting;
        const Animation &body = animation(p.model, sequence);
        const int direction = body.direction(e ? e->angle : p.angle);
        int frame = !fighting.empty() ? fought : int(double(now) * double(body.fps) / 1000.0) % 100000;
        if (e && e->walking && fighting.empty()) {
            const float cycle = body.cycleDistance(direction);
            if (cycle > 0.0f)
                frame = int(e->travelled / cycle * float(body.perDirection()));
        }
        const int x = int(std::lround(e ? e->x : p.x)) - left;
        const int y = int(std::lround(e ? e->y : p.y)) - top;
        if (body.rotations > 1)
            blitBy(view, animation(p.model, sequence + " Shadow").frame(frame, direction), x, y);
        blitBy(view, body.frame(frame, direction), x, y);
        if (!p.overlay.empty())
            blitBy(view, animation(p.overlay, sequence).frame(frame, direction), x, y);
    }
    drawNoises(view, left, top);
    // The grenades thrown, flying or lying.
    for (const Thrown &t : _thrown) {
        const Animation &a = animation(t.model, "Idle");
        blitBy(view, a.at(now), int(std::lround(t.x)) - left, int(std::lround(t.y)) - top);
    }
    // The shots' flashes, over everyone.
    for (const Flash &f : _flashes) {
        const Animation &a = animation(f.model, "Idle");
        blitBy(view, a.frame(int(f.time * a.fps), 0), int(std::lround(f.x)) - left, int(std::lround(f.y)) - top);
    }
    for (int y = 0; y < _viewH && _viewY + y < screen.h; ++y)
        std::memcpy(screen.row(_viewY + y) + _viewX * 4, view.row(y), size_t(std::min(_viewW, screen.w - _viewX)) * 4);
    _hud->setMove(_selected >= 0 ? _officers[size_t(_selected)].move : 0);
    std::vector<bool> chosen;
    for (const Officer &o : _officers)
        chosen.push_back(o.chosen);
    _hud->setChosen(chosen);
    std::vector<float> health;
    for (const Officer &o : _officers)
        health.push_back(o.maxHealth > 0.0f ? o.health / o.maxHealth : 1.0f);
    _hud->setHealth(health);
    _hud->setListening(_listening);
    if (_selected >= 0) {
        std::vector<GameHud::Held> held;
        for (const Officer::Held &h : _officers[size_t(_selected)].items)
            held.push_back({h.name, h.count});
        _hud->setInventory(std::move(held), _officers[size_t(_selected)].item);
    } else {
        _hud->setInventory({}, -1);
    }
    _hud->draw(screen, now, _selected, _cameraX, _cameraY, _viewW, _viewH);
    // The box being dragged, outlined (inferred).
    if (_dragging) {
        const int x0 = std::min(_pressX, _mouseX);
        const int x1 = std::max(_pressX, _mouseX);
        const int y0 = std::max(_viewY, std::min(_pressY, _mouseY));
        const int y1 = std::min(_viewY + _viewH - 1, std::max(_pressY, _mouseY));
        const auto dot = [&](int x, int y) {
            if (x >= _viewX && x < _viewX + _viewW && y >= 0 && y < screen.h) {
                uint8_t *p = screen.row(y) + x * 4;
                p[0] = 0;
                p[1] = 255;
                p[2] = 0;
            }
        };
        for (int x = x0; x <= x1; ++x) {
            dot(x, y0);
            dot(x, y1);
        }
        for (int y = y0; y <= y1; ++y) {
            dot(x0, y);
            dot(x1, y);
        }
    }
    drawDialog(screen);
    if (_objectivesWindow)
        _objectivesWindow->draw(screen, objectiveLines());
    if (_endGame)
        _endGame->draw(screen, objectiveLines());
    if (_confirm)
        _confirm->draw(screen);
}

} // namespace reluva::stawayteam
