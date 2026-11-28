// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/map_screen.h"

#include "common/text.h"
#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/game/controls.h"
#include "engines/stawayteam/game/ground_fractal.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/main_menu.h"
#include "engines/stawayteam/ui/movies.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace reluva::stawayteam {

namespace {

// The ground's cells, square (0x4ef471).
constexpr int kCell = kGroundCell;
// The scrolling's frames a second: the program takes a step each frame
// it draws (0x4f73e2), as fast as the machine draws; Reluva takes 30, the
// program's longest step (0x4f4b6f).
constexpr float kScrollFrames = 30.0f;
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
// The info dialog stays this long after its voice-over (0x5c9eb9), or, a
// line without one, as long as the program's lines of plain text do
// (0x5c9e37; inferred for these), in seconds.
constexpr float kDialogAfter = 0.25f;
constexpr float kDialogWithoutVoice = 2.5f;
// A walk's speed for a model without one: the retail bodies' sideways,
// 26 pixels in 10 frames at 14 a second.
constexpr float kDefaultWalk = 36.4f;
// How far the mouse goes, held, before it's dragging a box (inferred).
constexpr int kDrag = 4;
// Too near a waypoint to walk any further to it.
constexpr float kArrived = 1.0f;
// A way that would take a walker through a character costs this many
// times its length (0x438ecf): its outline swept along the way meeting
// theirs (0x438d90); for a model without an outline, within this of them
// (inferred).
constexpr float kThroughCharacter = 5.0f;
constexpr float kCharacterReach = 18.0f;
// Time stopped: its words' font (0x5be102).
const char *const kPauseFont = "Trek Title Red";

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

// The items the inventory's keys choose: the program's table of the keys'
// commands and the items' definitions (0x6492e0); Security Systems has no
// row, so does nothing.
const std::vector<std::pair<const char *, const char *>> &itemKeys() {
    static const std::vector<std::pair<const char *, const char *>> kKeys = {
        {"Phaser", "Phaser"},
        {"Concussion Grenade", "Concussion Grenade"},
        {"Neural Disruptor", "Phase Neural Disrupter"},
        {"Hypospray", "Hypospray"},
        {"Sniper Rifle", "Sniper Rifle"},
        {"Vulcan Nerve Pinch", "Vulcan Neck Pinch"},
        {"Enhanced Mind Meld", "Psychic Rack"},
        {"EMP Grenade", "EM Pulse Grenade"},
        {"Cloaking Band", "Personal Visual Dampening Band"},
        {"Medical Tricorder", "Medical Tricorder"},
        {"Tricorder", "Tricorder"},
        {"Audio Decoy", "Audio Decoy"},
        {"Holographic Projector", "Holographic Projection"},
        {"Extractor", "Extractor"},
        {"Herbal Paralysis", "Botanical Paralysis"},
        {"Herbal Healing", "Botanical Healing"},
        {"Stasis Field Generator", "Portable Containmnet Field"},
        {"Teleportation Brig", "Telportation Brig"},
        {"Brig Controller", "Telportation Brig Controller"},
        {"Phaser Rifle", "Phaser Rifle Type III"},
        {"Proximity Mines", "Proximity Mine"},
        {"Remote Mines", "Remote Mine"},
        {"Engineering Tech Kit", "Terminal Repair"},
        {"Computer Systems", "Computer Systems"},
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
    // Textures repeat across the map from its corner: a pixel's place on
    // it masked to the texture's size, a power of 2 (0x54006e).
    const int tx = ((x % img.w) + img.w) % img.w;
    const int ty = ((y % img.h) + img.h) % img.h;
    return img.pixel(tx, ty);
}

// The outline `shape` moved from (ax, ay) to (bx, by), as the hull of it at
// both ends (0x53ba20).
std::vector<std::pair<float, float>> swept(const std::vector<std::pair<float, float>> &shape, float ax, float ay,
                                           float bx, float by) {
    std::vector<std::pair<float, float>> points;
    for (const auto &[x, y] : shape) {
        points.emplace_back(x + ax, y + ay);
        points.emplace_back(x + bx, y + by);
    }
    std::sort(points.begin(), points.end());
    const auto cross = [](const std::pair<float, float> &o, const std::pair<float, float> &a,
                          const std::pair<float, float> &b) {
        return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first);
    };
    std::vector<std::pair<float, float>> hull(points.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < points.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0f)
            --k;
        hull[k++] = points[i];
    }
    for (size_t i = points.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 0.0f)
            --k;
        hull[k++] = points[i - 1];
    }
    hull.resize(k > 1 ? k - 1 : k);
    return hull;
}

// Whether two outlines meet: a corner of one inside the other, or their
// sides crossing.
bool meet(const std::vector<std::pair<float, float>> &a, const std::vector<std::pair<float, float>> &b) {
    for (const auto &[x, y] : a)
        if (inside(b, x, y))
            return true;
    for (const auto &[x, y] : b)
        if (inside(a, x, y))
            return true;
    const auto side = [](const std::pair<float, float> &p, const std::pair<float, float> &q,
                         const std::pair<float, float> &r) {
        return (q.first - p.first) * (r.second - p.second) - (q.second - p.second) * (r.first - p.first);
    };
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = 0; j < b.size(); ++j) {
            const auto &p = a[i];
            const auto &q = a[(i + 1) % a.size()];
            const auto &r = b[j];
            const auto &s = b[(j + 1) % b.size()];
            if ((side(p, q, r) > 0.0f) != (side(p, q, s) > 0.0f) && (side(r, s, p) > 0.0f) != (side(r, s, q) > 0.0f))
                return true;
        }
    return false;
}

} // namespace

MapScreen::MapScreen(const Archive &data, const TextObject &rules, GameMap map, GameOptions &options, uint64_t now,
                     std::vector<TeamMember> team, WayPoints wayPoints, audio::Mixer *mixer, bool arriving)
    : _data(data), _rules(rules), _map(std::move(map)), _options(options), _last(now), _team(std::move(team)),
      _wayPoints(std::move(wayPoints)), _mixer(mixer), _progress(std::make_shared<MissionProgress>()) {
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
                else if (p.renderType == "Warped" && !p.warpEffect.empty())
                    _warped.push_back(i);
                continue;
            }
            // Not on the ground, it's sorted with the models (0x54ed6f).
            if (!p.onGround) {
                _raised.push_back(i);
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
    std::stable_sort(_standing.begin(), _standing.end(),
                     [&](size_t a, size_t b) { return _map.parts[a].y < _map.parts[b].y; });
    moveCamera(_map.viewX, _map.viewY);

    // The team at the map's spawn points (0x5bc2b0): each at the part named
    // "<their name> Spawn Point" if there's one (Sheila Thatcher's in
    // Mission 13), else the first of "<their field> 1" to 6 no one's taken
    // ("science 1" in Missions 10 and 13), else of Away Team Spawn Point 1
    // to 6 (0x5bc160); facing as it says, if it sets the facing (0x415c49:
    // its degrees as radians, the models' directions turning through 2 pi).
    std::vector<bool> taken(_map.parts.size(), false);
    const auto spawnNamed = [&](const std::string &name) {
        for (size_t k = 0; k < _map.parts.size(); ++k)
            if (!taken[k] && text::equalsIgnoreCase(_map.parts[k].name, name))
                return int(k);
        return -1;
    };
    for (size_t i = 0; i < _team.size(); ++i) {
        Officer o;
        o.member = i;
        o.x = _map.viewX;
        o.y = _map.viewY;
        o.spawn = spawnNamed(_team[i].name() + " Spawn Point");
        for (int n = 1; n <= 6 && o.spawn < 0 && !_team[i].field.empty(); ++n)
            o.spawn = spawnNamed(_team[i].field + " " + std::to_string(n));
        for (int n = 1; n <= 6 && o.spawn < 0; ++n)
            o.spawn = spawnNamed("Away Team Spawn Point " + std::to_string(n));
        if (o.spawn >= 0) {
            const MapPart &p = _map.parts[size_t(o.spawn)];
            taken[size_t(o.spawn)] = true;
            o.x = p.x;
            o.y = p.y;
            if (p.setsFacing)
                o.direction = int(std::lround(p.facing / 30.0f)) % kDirections;
        }
        o.present = !arriving;
        _officers.push_back(o);
    }
    if (!_officers.empty() && !arriving)
        choose(0);
    _hud = std::make_unique<GameHud>(data, rules, _team, _map.minimap, _map.width, _map.height);
    setupTriggers();
    setupEnemies();
    setupCombat();
    setupItems();
    _itemTypes = loadItemTypes(data);
    if (!arriving)
        runSpawnActions();

    // The map's camera scripts play as it starts. The team's spawn points
    // hide them (their CSpawnPointAI's CMakeVisibleAction): one of the
    // scripts beams them in. Without scripts, the view starts on the team
    // (inferred).
    _scripts = _map.cameraScripts;
    bool beams = false;
    for (const CameraScript &s : _scripts)
        beams = beams || contains(s.action, "CAddEffectToTeam");
    _teamShown = !beams || arriving;
    if (_scripts.empty() && !_officers.empty())
        moveCamera(_officers[0].x, _officers[0].y);
    _dialogBackground = screenPicture(data, "Dialog Window/Background");
    (void)_pauseIndicator.load(data, "Pause Indicator");
    (void)_pauseFont.load(data, kPauseFont);
    (void)_dialogFont.load(data, readText(custom, "Dialog Font"));
}

MapScreen::~MapScreen() = default;

void MapScreen::moveCamera(float x, float y) {
    // The view's corner kept from 0 to the map's size less one, rounded
    // down to 128, less the view's (0x4f1f2c).
    const int mapW = (_map.width - 1) & ~127;
    const int mapH = (_map.height - 1) & ~127;
    const float halfW = float(shownW()) / 2.0f;
    const float halfH = float(shownH()) / 2.0f;
    // On a map smaller than the view, the second holds.
    _cameraX = std::min(std::max(x - halfW, 0.0f), float(mapW - shownW())) + halfW;
    _cameraY = std::min(std::max(y - halfH, 0.0f), float(mapH - shownH())) + halfH;
}

const image::Image &MapScreen::texture(const std::string &name) {
    auto it = _textures.find(name);
    if (it == _textures.end())
        it = _textures.emplace(name, cachePicture(_data, "Textures/" + name)).first;
    return it->second;
}

void MapScreen::drawPolygon(const std::function<uint8_t *(int)> &rowOf, int w, int h, int cellX, int cellY,
                            const MapPart &part) {
    const image::Image *tex = part.renderType == "Textured" ? &texture(part.texture) : nullptr;
    if (part.renderType == "Textured" && (!tex || !tex->w))
        return;
    const auto &pts = part.polygon;
    // Each row's crossings of the polygon's sides, filled between pairs.
    for (int row = 0; row < h; ++row) {
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
            const int to = std::min(w, int(std::ceil(xs[k + 1] - 0.5f)) - cellX);
            for (int col = from; col < to; ++col) {
                uint8_t *p = rowOf(row) + size_t(col) * 4;
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
                    // A shadow halves what's under it (0x542378).
                    for (int c = 0; c < 3; ++c)
                        p[c] = uint8_t(p[c] / 2);
                }
            }
        }
    }
}

const image::Image *MapScreen::water(const std::string &name) {
    Water &w = _waters[name];
    if (!w.loaded) {
        w.loaded = true;
        std::vector<uint8_t> bytes;
        TextObject object;
        if (!_data.read("Water Warp Effects/" + name + ".txt", bytes) &&
            !readTextObject(std::string(bytes.begin(), bytes.end()), object))
            w.warp = readWaterWarp(object);
    }
    const image::Image &tex = texture(w.warp.texture);
    if (!tex.w)
        return nullptr;
    // Warped again only as the game's time moves on (0x5f5461).
    if (w.at != _waterTime) {
        warpWater(w.warp, tex, _waterTime, w.picture);
        w.at = _waterTime;
    }
    return &w.picture;
}

void MapScreen::drawWater(Surface &view, int left, int top) {
    // Each drawn as a textured polygon is, its texture the water warped
    // (0x54eb66; inferred for where the texture's laid, as the others').
    for (size_t i : _warped) {
        if (!shown(i))
            continue;
        const MapPart &p = _map.parts[i];
        const image::Image *tex = water(p.warpEffect);
        if (!tex)
            continue;
        for (int y = 0; y < view.h; ++y) {
            const float wy = float(top + y) + 0.5f;
            std::vector<float> xs;
            for (size_t k = 0; k + 1 < p.polygon.size(); ++k) {
                const auto [ax, ay] = p.polygon[k];
                const auto [bx, by] = p.polygon[k + 1];
                if ((ay <= wy && by > wy) || (by <= wy && ay > wy))
                    xs.push_back(ax + (wy - ay) * (bx - ax) / (by - ay));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                const int from = std::max(0, int(std::ceil(xs[k] - 0.5f)) - left);
                const int to = std::min(view.w, int(std::ceil(xs[k + 1] - 0.5f)) - left);
                for (int x = from; x < to; ++x) {
                    const uint8_t *t = texel(*tex, left + x, top + y);
                    std::memcpy(view.row(y) + x * 4, t, 3);
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
        // Each pixel draws its value of the cell's fractal (0x5387a0). In
        // 8-bit colour a cell whose corners draw one texture unblended is
        // that texture (0x537d1d); the game's 16-bit has no such shortcut.
        const std::vector<uint8_t> values = groundFractal(g, cx, cy);
        for (int y = 0; y < kCell; ++y) {
            for (int x = 0; x < kCell; ++x) {
                const Blend &b = _blends[values[size_t(y) * (kCell + 1) + size_t(x)]];
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
            drawPolygon([&](int row) { return cell.pixel(0, row); }, cell.w, cell.h, cx * kCell, cy * kCell,
                        _map.parts[i]);
    }
    return _cells.emplace(key, std::move(cell)).first->second;
}

void MapScreen::scroll(float seconds) {
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
    // Each frame, a way held adds the Scroll Acceleration to the velocity,
    // held to the Scroll Speed; none held, it slows by a factor from 0.95
    // at the least acceleration to 0.5 at the most, stopping under 0.05
    // (0x5c0de0, 0x5bc6a6), at kScrollFrames.
    const float speed = _options.scrollSpeed;
    const float accel = _options.scrollAccel;
    const float slowing = 0.5f + 0.45f * (1.0f - (accel - GameOptions::kScrollAccelMin) / 67.5f);
    const float frame = 1.0f / kScrollFrames;
    _scrollTime = std::min(_scrollTime + seconds, 0.5f);
    while (_scrollTime >= frame) {
        _scrollTime -= frame;
        if (dx != 0.0f || dy != 0.0f) {
            _scrollVX += dx * accel;
            _scrollVY += dy * accel;
            const float v = std::hypot(_scrollVX, _scrollVY);
            if (v > speed) {
                _scrollVX *= speed / v;
                _scrollVY *= speed / v;
            }
        } else {
            _scrollVX *= slowing;
            _scrollVY *= slowing;
            if (std::abs(_scrollVX) <= 0.05f)
                _scrollVX = 0.0f;
            if (std::abs(_scrollVY) <= 0.05f)
                _scrollVY = 0.0f;
        }
        if (_scrollVX != 0.0f || _scrollVY != 0.0f)
            moveCamera(_cameraX + _scrollVX * frame, _cameraY + _scrollVY * frame);
        stepZoom(frame);
    }
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

int MapScreen::officerAt(float x, float y) const {
    // About their figure: the frames reach from 19 above where they stand
    // to 23 under it (inferred from the walk's bounds).
    for (size_t i = 0; i < _officers.size(); ++i)
        if (_officers[i].alive() && _officers[i].present && std::abs(_officers[i].x - x) <= 12.0f &&
            y >= _officers[i].y - 20.0f && y <= _officers[i].y + 24.0f)
            return int(i);
    return -1;
}

void MapScreen::choose(int officer, bool add) {
    if (officer < 0 || size_t(officer) >= _officers.size())
        return;
    if (!add)
        for (Officer &o : _officers)
            o.chosen = false;
    _meldChosen = false;
    Officer &o = _officers[size_t(officer)];
    o.chosen = o.alive() && o.present && (add ? !o.chosen : true);
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
    _meldChosen = false;
    for (Officer &o : _officers)
        if (o.alive() && o.present && o.x >= x0 && o.x <= x1 && o.y >= y0 && o.y <= y1)
            o.chosen = true;
    _selected = -1;
    for (size_t i = 0; i < _officers.size() && _selected < 0; ++i)
        if (_officers[i].chosen)
            _selected = int(i);
}

void MapScreen::chooseAll() {
    _meldChosen = false;
    for (size_t i = 0; i < _officers.size(); ++i) {
        _officers[i].chosen = _officers[i].alive() && _officers[i].present;
        if (_selected < 0 && _officers[i].chosen)
            _selected = int(i);
    }
}

void MapScreen::order(float x, float y, bool sprint) {
    // Every one chosen to the place itself: the game gives each the first
    // one's point (0x5bfcdb, 0x5b8050, 0x5b7e50), and a place with only a
    // character on it is walked onto (0x449481). Sprinting they run, the
    // move they had kept till they're there (0x5b6650, 0x4aa000, given
    // back on CProcessDoneMessage at 0x4a9fa0).
    for (Officer &o : _officers) {
        if (!o.chosen)
            continue;
        o.path = route(o, x, y);
        o.attacking = -1;
        if (sprint) {
            if (o.sprintFrom < 0)
                o.sprintFrom = o.move;
            o.move = 1;
            o.travelled = 0.0f;
        }
    }
}

void MapScreen::setMove(int move) {
    for (Officer &o : _officers)
        if (o.chosen) {
            o.move = std::clamp(move, 0, 2);
            o.sprintFrom = -1;
            o.travelled = 0.0f;
        }
}

void MapScreen::walk(Officer &o, float seconds) {
    if (o.path.empty() || o.down())
        return;
    const Animation &walking = animation(_team[o.member].bodyModel, kMoving[o.move]);
    // Running, their sequence plays at their Speed's part of 100 and a half
    // more, 1.5 without one (0x4aa063, 0x5e54b0).
    float play = 1.0f;
    if (o.move == 1) {
        play = 1.5f;
        for (const TeamMember::Attribute &a : _team[o.member].attributes)
            if (a.name == "Speed")
                play = float(a.value) / kAttributeWhole + 0.5f;
    }
    const float fps = walking.fps * play;
    float left = seconds;
    while (!o.path.empty() && left > 0.0f) {
        const float dx = o.path.front().first - o.x;
        const float dy = o.path.front().second - o.y;
        const float distance = std::hypot(dx, dy);
        if (distance < kArrived) {
            o.path.erase(o.path.begin());
            continue;
        }
        // Facing the way they go; as far as their walk's frames go in the
        // time, each frame's own movement (0x44a03f), straight along the
        // way.
        const float angle = std::atan2(dy, dx);
        o.direction = ((6 - int(std::lround(angle / (3.14159265f / 6.0f)))) % kDirections + kDirections) % kDirections;
        const int per = walking.perDirection();
        const float before = walking.frameAt(o.direction, o.travelled);
        float used = left;
        float step = walking.walk(o.direction, o.travelled, left, fps, distance, used);
        if (step < 0.0f) {
            step = std::min(distance, kDefaultWalk * play * left);
            used = step / (kDefaultWalk * play);
        }
        // Through anyone in the way: the move ignores one character meeting
        // another (0x497c60), and an officer's doesn't collide at all, its
        // Tries To Collide off (0x497c0f). The way their direction's frames
        // go (0x5512e0), turned again each update; the last step onto the
        // place itself (inferred).
        float wx = dx / distance;
        float wy = dy / distance;
        if (step < distance)
            walking.cycleWay(o.direction, wx, wy);
        o.x += wx * step;
        o.y += wy * step;
        // The footsteps its frames passed make their noise (the
        // sequence's Trigger, 0x518100).
        if (per > 0 && fps > 0.0f) {
            const int from = int(before);
            const int to = int(before + used * fps);
            for (int f = from + 1; f <= to; ++f)
                for (const Animation::Noise &n : walking.noises)
                    if (n.frame == f % per) {
                        makeNoise(o.x, o.y, footstepNoise(o, n), n.age);
                        // Its sound, where it has one (inferred: by the
                        // ground under it, CGroundTextureFrame's Surface
                        // Type, isn't there; no model has either).
                        if (_mixer && !n.sound.empty())
                            if (auto sound = gameSound(_data, n.sound))
                                _mixer->play(sound, audio::SoundType::kEffects);
                    }
        }
        left -= std::max(used, 1e-6f);
    }
    if (o.path.empty() && o.sprintFrom >= 0) {
        o.move = o.sprintFrom;
        o.sprintFrom = -1;
        o.travelled = 0.0f;
    }
}

void MapScreen::drawLine(Surface &view, float x0, float y0, float x1, float y1, const std::array<uint8_t, 3> &rgb,
                         float opacity) {
    const int steps = int(std::ceil(std::max(std::abs(x1 - x0), std::abs(y1 - y0))));
    for (int k = 0; k <= steps; ++k) {
        const float t = steps > 0 ? float(k) / float(steps) : 0.0f;
        const int x = int(std::lround(x0 + (x1 - x0) * t));
        const int y = int(std::lround(y0 + (y1 - y0) * t));
        if (x < 0 || y < 0 || x >= view.w || y >= view.h)
            continue;
        uint8_t *p = view.row(y) + x * 4;
        for (int c = 0; c < 3; ++c)
            p[c] = uint8_t(float(p[c]) + (float(rgb[size_t(c)]) - float(p[c])) * opacity);
    }
}

bool MapScreen::blockedAt(size_t part, float x, float y, size_t *struck) const {
    const std::vector<std::pair<float, float>> &shape = modelInfo(_map.parts[part].model).footprint;
    if (shape.size() < 3)
        return false;
    return shapeBlockedAt(shape, part, x, y, struck);
}

bool MapScreen::shapeBlockedAt(const std::vector<std::pair<float, float>> &shape, size_t ignore, float x, float y,
                               size_t *struck) const {
    const auto met = [&](size_t part) {
        if (struck)
            *struck = part;
        return true;
    };
    if (shape.size() < 3) {
        for (const Wall &w : _walls) {
            const PartState &state = _partStates[w.part];
            if (w.part != ignore && state.active && !state.deleted && state.collideable && inside(w.points, x, y))
                return met(w.part);
        }
        return false;
    }
    std::vector<std::pair<float, float>> outline;
    float x0 = 1e9f;
    float y0 = 1e9f;
    float x1 = -1e9f;
    float y1 = -1e9f;
    for (const auto &[px, py] : shape) {
        outline.emplace_back(px + x, py + y);
        x0 = std::min(x0, px + x);
        y0 = std::min(y0, py + y);
        x1 = std::max(x1, px + x);
        y1 = std::max(y1, py + y);
    }
    for (const Wall &w : _walls) {
        const PartState &state = _partStates[w.part];
        if (w.part == ignore || !state.active || state.deleted || !state.collideable || w.x1 < x0 || w.x0 > x1 ||
            w.y1 < y0 || w.y0 > y1)
            continue;
        if (meet(outline, w.points))
            return met(w.part);
    }
    return false;
}

bool MapScreen::spotFree(size_t officer, size_t part, size_t before) const {
    // Free if the officer's shape there meets nothing that collides, the
    // officers standing here among it (0x4375d0, 0x4f0f80).
    const std::vector<std::pair<float, float>> &shape = modelInfo(_team[_officers[officer].member].bodyModel).footprint;
    const auto outline = [&](float px, float py) {
        std::vector<std::pair<float, float>> out;
        for (const auto &[sx, sy] : shape)
            out.emplace_back(px + sx, py + sy);
        return out;
    };
    float px = 0.0f;
    float py = 0.0f;
    placeOf(part, px, py);
    if (shapeBlockedAt(shape, part, px, py))
        return false;
    if (shape.size() >= 3)
        for (size_t i = 0; i < std::min(before, _officers.size()); ++i)
            if (i != officer && _officers[i].present && meet(outline(px, py), outline(_officers[i].x, _officers[i].y)))
                return false;
    return true;
}

bool MapScreen::arrivalPlace(size_t officer, const std::string &location, float &x, float &y) const {
    const auto free = [&](size_t part) { return spotFree(officer, part); };
    const auto named = [&](const std::string &name) -> const std::vector<size_t> * {
        const auto it = _byName.find(text::toLower(name));
        return it == _byName.end() || it->second.empty() ? nullptr : &it->second;
    };
    // The first free of "<name> 1" to "<name> 5" (0x5bc160).
    const auto numbered = [&](const std::string &name) {
        for (int n = 1; n <= 5; ++n)
            if (const std::vector<size_t> *parts = named(name + " " + std::to_string(n)))
                if (free(parts->front())) {
                    placeOf(parts->front(), x, y);
                    return true;
                }
        return false;
    };
    // The name whole, commas and all: "space1,space2,space3,space4" is no
    // part's, so falls to the spawn points (0x56a910).
    if (!location.empty()) {
        if (const std::vector<size_t> *parts = named(location)) {
            placeOf(parts->front(), x, y);
            return true;
        }
        if (numbered(location))
            return true;
    }
    const std::string name = _team[_officers[officer].member].name();
    if (const std::vector<size_t> *parts = named(name + " Spawn Point")) {
        placeOf(parts->front(), x, y);
        return true;
    }
    return numbered(name) || numbered("Away Team Spawn Point");
}

std::vector<std::pair<float, float>> MapScreen::route(float x0, float y0, float x1, float y1, int officer, int enemy,
                                                      bool straight) const {
    // Planned as the move starts, round where the characters stand then,
    // and not again (0x4492c0): its path collide checker makes a line
    // through one cost five times as much, and one through anything else
    // in the way not taken (0x438d90); here the doors, the waypoints
    // already keeping off the rest (inferred).
    // With no waypoint near either end there's no way, and the walker goes
    // straight there (0x52e8a4). It plans first with a plain checker, every
    // line clear of what's fixed costing 1 (0x6523b8, 0x435d20), then with
    // its own; with no way by either it goes straight there too (0x44960a,
    // 0x4495ef).
    if (_wayPoints.points.empty() || !_wayPoints.reaches(x0, y0) || !_wayPoints.reaches(x1, y1))
        return {{x1, y1}};
    // Each character standing, by its outline (its body's or its model's
    // Bounds Poly) where it stands.
    struct Other {
        float x;
        float y;
        std::vector<std::pair<float, float>> outline;
    };
    const auto outlineAt = [](const std::vector<std::pair<float, float>> &shape, float x, float y) {
        std::vector<std::pair<float, float>> out;
        for (const auto &[px, py] : shape)
            out.emplace_back(px + x, py + y);
        return out;
    };
    std::vector<Other> others;
    for (size_t i = 0; i < _officers.size(); ++i)
        if (int(i) != officer && _officers[i].present && _officers[i].alive())
            others.push_back(
                {_officers[i].x, _officers[i].y,
                 outlineAt(modelInfo(_team[_officers[i].member].bodyModel).footprint, _officers[i].x, _officers[i].y)});
    for (size_t i = 0; i < _enemies.size(); ++i)
        if (int(i) != enemy && _enemies[i].alive() && shown(_enemies[i].part))
            others.push_back(
                {_enemies[i].x, _enemies[i].y,
                 outlineAt(modelInfo(_map.parts[_enemies[i].part].model).footprint, _enemies[i].x, _enemies[i].y)});
    const std::vector<std::pair<float, float>> &shape =
        officer >= 0 ? modelInfo(_team[_officers[size_t(officer)].member].bodyModel).footprint
        : enemy >= 0 ? modelInfo(_map.parts[_enemies[size_t(enemy)].part].model).footprint
                     : modelInfo("").footprint;
    // The closed doors in the way: a line through one isn't taken, but
    // through one not locked that answers to this kind of walker, its
    // Triggered By Players or Enemies (0x48e070: asked as the way's
    // planned, it lets them by, and is opened as they come).
    std::vector<const std::vector<std::pair<float, float>> *> doors;
    for (const Wall &w : _walls) {
        const auto door = _doors.find(w.part);
        const PartState &state = _partStates[w.part];
        if (door == _doors.end() || !state.active || state.deleted || !state.collideable)
            continue;
        const Door &d = door->second;
        if (!d.locked && ((officer >= 0 && d.byPlayers) || (enemy >= 0 && d.byEnemies)))
            continue;
        doors.push_back(&w.points);
    }
    const auto through = [](const std::vector<std::pair<float, float>> &outline, float ax, float ay, float bx,
                            float by) {
        if (inside(outline, ax, ay) || inside(outline, bx, by))
            return true;
        const auto side = [](float px, float py, float qx, float qy, float rx, float ry) {
            return (qx - px) * (ry - py) - (qy - py) * (rx - px);
        };
        for (size_t k = 0; k < outline.size(); ++k) {
            const auto [cx, cy] = outline[k];
            const auto [dx, dy] = outline[(k + 1) % outline.size()];
            if ((side(ax, ay, bx, by, cx, cy) > 0.0f) != (side(ax, ay, bx, by, dx, dy) > 0.0f) &&
                (side(cx, cy, dx, dy, ax, ay) > 0.0f) != (side(cx, cy, dx, dy, bx, by) > 0.0f))
                return true;
        }
        return false;
    };
    const auto factor = [&](float ax, float ay, float bx, float by) {
        for (const auto *outline : doors)
            if (through(*outline, ax, ay, bx, by))
                return 0.0f;
        // The walker's outline swept along the line meeting a character's.
        const std::vector<std::pair<float, float>> sweep =
            shape.size() >= 3 ? swept(shape, ax, ay, bx, by) : std::vector<std::pair<float, float>>{};
        const float dx = bx - ax;
        const float dy = (by - ay) / ParticleSystem::kYToX;
        const float length = dx * dx + dy * dy;
        for (const Other &o : others) {
            if (sweep.size() >= 3 && o.outline.size() >= 3) {
                if (meet(sweep, o.outline))
                    return kThroughCharacter;
                continue;
            }
            // Without outlines: how near it comes to the line, on the
            // ground unforeshortened.
            const float cx = o.x;
            const float cy = o.y;
            const float px = cx - ax;
            const float py = (cy - ay) / ParticleSystem::kYToX;
            const float t = length > 0.0f ? std::clamp((px * dx + py * dy) / length, 0.0f, 1.0f) : 0.0f;
            if (std::hypot(px - t * dx, py - t * dy) < kCharacterReach)
                return kThroughCharacter;
        }
        return 1.0f;
    };
    std::vector<std::pair<float, float>> way = _wayPoints.path(x0, y0, x1, y1, factor);
    if (way.empty() && straight)
        return {{x1, y1}};
    return way;
}

void MapScreen::showDialog(const std::string &script, const std::string &line, const std::string &speaker) {
    _dialogText.clear();
    std::vector<uint8_t> bytes;
    if (!_data.read("Levels/" + script + ".RDS", bytes)) {
        const RdsScript rds = readRdsScript(std::string(bytes.begin(), bytes.end()));
        if (const std::string *text = rds.find(line))
            _dialogText = *text;
    }
    _dialogSpeaker = speaker;
    _dialogIcon = cachePicture(_data, "Character/Character Icons/" + speaker);
    if (_mixer && _dialogVoice != audio::kNoSound)
        _mixer->stop(_dialogVoice);
    _dialogVoice = audio::kNoSound;
    // Shown while its voice-over plays, or for as long as it would
    // (0x4362f1), and a moment after; its time stands still while the
    // voice plays or the game's paused (0x435f54).
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
    _itemTitle = std::max(0.0f, _itemTitle - seconds);
    // The water moves by the game's own clock, whatever's held or paused
    // (0x5f545e, moved on each frame step at 0x4d54b9).
    _waterTime += seconds;
    // The objectives window and the escape menu hold the game still,
    // counted on the program's holds (0x51ad9b, 0x5ba9f9); the End Game
    // Menu only takes the map's input (0x5ca130), the game going on under
    // it and its time counted (0x5b597f).
    if (_objectivesWindow || _escape)
        return;
    // Time stopped, the view can still be moved (inferred).
    if (_paused) {
        scroll(seconds);
        return;
    }
    if (_active)
        _progress->time += seconds;
    // The view scrolls by the keys and the mouse while the camera's scripts
    // play too: nothing in the game's update stops it but an in-game movie
    // (0x5bff1a, 0x5c0139); a script panning sets the view after it each
    // frame (0x47779c), so only its wait leaves it to the player.
    scroll(seconds);
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
    // A script whose manager has Pause Actors tells every character on the
    // maps to stop as it starts (a CPauseActorMessage, 0x478246, 0x5ca760)
    // and to go on as it ends (0x4780f5): they neither walk nor act nor
    // shoot meanwhile. One with Pause Game stops the game (0x47822b),
    // here as that (inferred: what the game's stopping leaves going isn't
    // traced).
    const bool held = !_scripts.empty() && _scripts.front().holdsActors;
    for (Officer &o : _officers)
        if (o.held <= 0.0f && !held)
            walk(o, seconds);
    // The triggers see where everyone has got to.
    updateNoises(seconds);
    if (!held)
        updateEnemies(seconds);
    updateMeld(seconds);
    updateEmitters(seconds);
    if (!held)
        updateCombat(seconds);
    updateItems(seconds);
    updateCharges(seconds);
    updateDeployed(seconds);
    updateTriggers(seconds);
}

void MapScreen::drawOfficer(Surface &view, const Officer &o, int left, int top, uint64_t now) {
    if (!_teamShown || !o.present)
        return;
    const TeamMember &m = _team[o.member];
    int fought = 0;
    const std::string fighting =
        combatSequence(o.shooting, o.dead, {o.stunned, o.stunTotal, o.stunLength}, fought, m.bodyModel, o.discharge);
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
        if (body.cycleDistance(o.direction) > 0.0f)
            frame = int(body.frameAt(o.direction, o.travelled));
    }
    const int x = int(std::lround(o.x)) - left;
    const int y = int(std::lround(o.y)) - top;
    // Beaming, as the parts do; once it's done they're left as it ended.
    const float warped = _warpTime > 0.0f ? 1.0f - _warpLeft / _warpTime : 1.0f;
    const float shown =
        _warpTime > 0.0f ? stawayteam::warpShown(_warpTime - _warpLeft, _warpTime, _warpFadeIn, _warpIn) : 1.0f;
    const int opacity = int(std::lround(255.0f * shown * o.opacity));
    // Under the warp's noise, as the parts beamed (0x563f80).
    const std::string effect = _warpLeft > 0.0f ? _warpEffect : std::string();
    const float amount = warpAmount(_warpTime - _warpLeft, _warpTime, _warpFadeIn);
    blitBy(view, shadow.frame(frame, o.direction), x, y, 1.0f, opacity);
    blitBy(view, underWarp(body.frame(frame, o.direction), effect, amount), x, y, 1.0f, opacity);
    blitBy(view, underWarp(head.frame(frame, o.direction), effect, amount), x, y, 1.0f, opacity);
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
        case EndGameWindow::Choice::kLoadGame:
            return Choice::kLoadGame;
        default:
            return Choice::kNone;
        }
    }
    if (_objectivesWindow) {
        // The objectives button closes it again, and its key with it
        // (0x476ab4).
        const int i = actionIndex("Display Objectives");
        const bool again = ev.type == EventType::kKeyDown && i >= 0 && scanCode(ev.key) != 0 &&
                           _options.controls.binding(size_t(i)).key == scanCode(ev.key);
        if (again || _objectivesWindow->handleEvent(ev, mixer))
            _objectivesWindow.reset();
        return Choice::kNone;
    }
    if (_escape) {
        switch (_escape->handleEvent(ev, mixer)) {
        case EscapeWindow::Choice::kResume:
            _escape.reset();
            return Choice::kNone;
        case EscapeWindow::Choice::kConfigure:
            return Choice::kConfigure;
        case EscapeWindow::Choice::kSaveGame:
            return Choice::kSaveGame;
        case EscapeWindow::Choice::kLoadGame:
            return Choice::kLoadGame;
        case EscapeWindow::Choice::kRestart:
            return Choice::kRestart;
        case EscapeWindow::Choice::kEndGame:
            return Choice::kExit;
        default:
            return Choice::kNone;
        }
    }
    if (ev.type == EventType::kMouseMove) {
        _mouseX = ev.mouseX;
        _mouseY = ev.mouseY;
    }
    const GameHud::Result hud = _hud->handleEvent(ev, now);
    if (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown || ev.type == EventType::kMouseUp)
        _overInterface = hud.used;
    if (hud.select >= 0 && !_officers[size_t(hud.select)].present) {
        // One on another of the mission's maps: that map's shown (inferred).
        _switchTo = hud.select;
    } else if (hud.select >= 0) {
        // A click on an officer's slot chooses them, with Shift adds them
        // or takes them away; a double click shows them too (inferred).
        choose(hud.select, (ev.mods & keymod::kShift) != 0);
        if (ev.clicks >= 2)
            moveCamera(_officers[size_t(_selected)].x, _officers[size_t(_selected)].y);
    }
    if (hud.moveCamera)
        moveCamera(hud.cameraX, hud.cameraY);
    // The move button goes round its states, crouching, walking, running
    // (0x51a600, 0x4764e5).
    if (hud.cycleMove && _selected >= 0)
        setMove((_officers[size_t(_selected)].move + 1) % 3);
    if (hud.selectAll)
        chooseAll();
    if (hud.item >= 0) {
        chooseItem(_selected, hud.item);
        _itemTitle = kItemTitleTime;
    }
    if (hud.step != 0)
        stepItem(_selected, hud.step);
    if (hud.toggleSound)
        _listening = !_listening;
    if (hud.pause)
        _paused = !_paused;
    if (hud.toggleStun)
        toggleStun();
    if (hud.toggleCharge)
        toggleCharge();
    if (hud.toggleOverload)
        toggleOverload();
    if (hud.detonate)
        detonate();
    if (hud.breakMeld)
        breakMeld();
    if (hud.centreMeld && _melded >= 0)
        moveCamera(_enemies[size_t(_melded)].x, _enemies[size_t(_melded)].y);
    if (hud.cancelOrders)
        cancelOrders();
    if (hud.vision)
        pressVision();
    if (hud.options) {
        _escape = std::make_unique<EscapeWindow>(_data, _rules, 640, 480);
        return Choice::kNone;
    }
    if (hud.objectives)
        _objectivesWindow = std::make_unique<ObjectivesWindow>(_data, _rules, 640, 480);
    // On the map, the mouse's buttons by its scheme (the options' Control
    // Scheme, 0x5bec60): normally the left the Move Button and the right
    // the Action Button, the other way round reversed; the middle cycles
    // the inventory (inferred). The Move Button chooses a character
    // clicked (Shift: adds or takes them away), or with the vision button
    // choosing, shows an enemy's sight, else sends those chosen there,
    // running with Ctrl (0x5bfba0); dragged, it chooses those in its box
    // (0x5c06dc). The Action Button uses the chosen item on what's clicked
    // or where, or a part's Use, and chooses no one (0x5cb2b0, 0x43d280).
    const auto toMapX = [&](int sx) { return mapX(sx); };
    const auto toMapY = [&](int sy) { return mapY(sy); };
    const bool inView =
        ev.mouseX >= _viewX && ev.mouseY >= _viewY && ev.mouseX < _viewX + _viewW && ev.mouseY < _viewY + _viewH;
    // The clicks are taken while the camera's scripts play, as the scroll
    // is (0x5bfe00); not before the team's beamed in, there to click.
    const bool playing = _teamShown;
    const bool reversed = _options.controls.scheme == 1;
    const auto isMove = [&](MouseButton b) { return b == (reversed ? MouseButton::kRight : MouseButton::kLeft); };
    const auto isAction = [&](MouseButton b) { return b == (reversed ? MouseButton::kLeft : MouseButton::kRight); };
    if (!hud.used && playing && inView && ev.type == EventType::kMouseMove && !_pressed)
        hoverAt(toMapX(ev.mouseX), toMapY(ev.mouseY));
    if (!hud.used && playing && inView && ev.type == EventType::kMouseDown &&
        (isMove(ev.button) || isAction(ev.button))) {
        _pressed = true;
        _pressedMove = isMove(ev.button);
        _dragging = false;
        _pressX = ev.mouseX;
        _pressY = ev.mouseY;
    }
    if (!hud.used && playing && inView && ev.type == EventType::kMouseDown && ev.button == MouseButton::kMiddle &&
        _selected >= 0)
        stepItem(_selected, 1);
    // The wheel by the bindings: Inventory Next down, Prev up.
    if (!hud.used && playing && ev.type == EventType::kMouseWheel && ev.wheelY != 0 && _selected >= 0) {
        const int bit = ev.wheelY < 0 ? 8 : 16;
        const int next = actionIndex("Inventory Next");
        const int prev = actionIndex("Inventory Prev");
        if (next >= 0 && (_options.controls.binding(size_t(next)).mouse & bit))
            stepItem(_selected, 1);
        else if (prev >= 0 && (_options.controls.binding(size_t(prev)).mouse & bit))
            stepItem(_selected, -1);
    }
    if (_pressed && ev.type == EventType::kMouseMove &&
        (std::abs(ev.mouseX - _pressX) > kDrag || std::abs(ev.mouseY - _pressY) > kDrag))
        _dragging = true;
    if (_pressed && ev.type == EventType::kMouseUp && (_pressedMove ? isMove(ev.button) : isAction(ev.button))) {
        _pressed = false;
        const bool add = (ev.mods & keymod::kShift) != 0;
        const float px = toMapX(_pressX);
        const float py = toMapY(_pressY);
        if (_dragging) {
            chooseIn(px, py, toMapX(ev.mouseX), toMapY(ev.mouseY), add);
        } else if (_pressedMove) {
            const bool sprint = (ev.mods & keymod::kCtrl) != 0;
            if (_melded >= 0 && enemyAt(px, py) == _melded && !_meldChosen)
                chooseMelded();
            else if (const int officer = officerAt(px, py); officer >= 0)
                choose(officer, add);
            else if (const int seen = enemyAt(px, py); _vision == 1 && seen >= 0) {
                _visionEnemy = seen;
                _vision = 2;
            } else if (_meldChosen)
                orderMelded(px, py);
            else
                order(px, py, sprint);
        } else if (useItem(toMapX(_pressX), toMapY(_pressY))) {
            // The chosen officer's item used there.
        } else if (startProcess(toMapX(_pressX), toMapY(_pressY))) {
            // The part's process, with the chosen item or none.
        } else if (const int enemy = enemyAt(toMapX(_pressX), toMapY(_pressY)); enemy >= 0) {
            attack(enemy);
        }
        _dragging = false;
    }
    const bool joystick = ev.type == EventType::kJoyButtonDown || ev.type == EventType::kJoyButtonUp;
    if (ev.type == EventType::kKeyDown || ev.type == EventType::kKeyUp || joystick) {
        const bool down = ev.type == EventType::kKeyDown || ev.type == EventType::kJoyButtonDown;
        // Esc as the escape button (inferred).
        if (down && !joystick && ev.key == key::kEscape) {
            _escape = std::make_unique<EscapeWindow>(_data, _rules, 640, 480);
            return Choice::kNone;
        }
        // The keys bound to each action (the keyboard's settings), or the
        // joystick's button bound to it, "Joystick Button" counted from 1
        // (0x484f00).
        const int code = joystick ? 0 : scanCode(ev.key);
        const auto bound = [&](const char *action) {
            const int i = actionIndex(action);
            if (i < 0)
                return false;
            const Binding &b = _options.controls.binding(size_t(i));
            return joystick ? b.joystick != 0 && b.joystick == ev.joyButton + 1 : code != 0 && b.key == code;
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
        // Pause stops time or starts it again; Cancel Orders presses its
        // button (0x476c75); Toggle Vision Cones presses the vision
        // button, from choosing to off (0x476c01); Cycle Vision Cones goes
        // on to the next enemy.
        if (down && bound("Pause"))
            _paused = !_paused;
        if (down && bound("Cancel Orders"))
            cancelOrders();
        if (down && bound("Toggle Vision Cones")) {
            if (_vision == 1)
                _vision = 2;
            pressVision();
        }
        if (down && bound("Cycle Vision Cones"))
            cycleVision();
        // Display Objectives presses the objectives button (0x476c90).
        if (down && bound("Display Objectives") && playing) {
            _objectivesWindow = std::make_unique<ObjectivesWindow>(_data, _rules, 640, 480);
            return Choice::kNone;
        }
        // Toggle Sound Cones presses the sound button (0x476c3f).
        if (down && bound("Toggle Sound Cones"))
            _listening = !_listening;
        // Inventory Next and Prev go along the chosen officer's items; an
        // item's own key chooses its row's item, or the one overriding it
        // that's been discovered, along the Overridden By (0x5bf2a6,
        // 0x5b7c90), for the officers chosen who have it.
        if (down && _selected >= 0 && (bound("Inventory Next") || bound("Inventory Prev")))
            stepItem(_selected, bound("Inventory Next") ? 1 : -1);
        // Toggle Minimap presses the info bar's open button (0x4dee47).
        if (down && bound("Toggle Minimap"))
            _hud->toggleInfoBar();
        // Zoom In and Out while held, Reset as pressed (0x5bf25c).
        if (bound("Zoom In"))
            holdZoom(1, down);
        if (bound("Zoom Out"))
            holdZoom(-1, down);
        if (down && bound("Zoom Reset"))
            holdZoom(0, true);
        if (down)
            for (const auto &[action, item] : itemKeys()) {
                if (!bound(action))
                    continue;
                const std::string held = heldAs(_items, item, _progress->discovered);
                if (held.empty())
                    continue;
                for (size_t i = 0; i < _officers.size(); ++i)
                    if (_officers[i].chosen)
                        for (size_t k = 0; k < _officers[i].items.size(); ++k)
                            if (_officers[i].items[k].name == held) {
                                chooseItem(int(i), int(k));
                                break;
                            }
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
    _warpedPictures.clear();
    clearScreen(screen);
    // Drawn into a picture of what the view shows, put on the screen at
    // the zoom's scale.
    const int viewW = shownW();
    const int viewH = shownH();
    const int left = int(std::lround(_cameraX)) - viewW / 2;
    const int top = int(std::lround(_cameraY)) - viewH / 2;
    Surface view;
    view.create(viewW, viewH, PixelFormat::kRGBA32);
    for (int cy = std::max(0, top / kCell); cy <= (top + viewH) / kCell; ++cy)
        for (int cx = std::max(0, left / kCell); cx <= (left + viewW) / kCell; ++cx)
            image::blit(view, groundCell(cx, cy), cx * kCell - left, cy * kCell - top);
    drawWater(view, left, top);
    // The guides laid on the ground while they're active: the Trans
    // polygons, each adding 10 steps of 16-bit colour to its channel,
    // red and blue of 31, green of 63, at most full (0x53f985, 0x53fe85,
    // 0x5429e4).
    for (size_t i : _overlays) {
        if (!shown(i))
            continue;
        const MapPart &p = _map.parts[i];
        const std::string &type = p.renderType;
        const int channel = type == "Trans Red" ? 0 : type == "Trans Green" ? 1 : 2;
        const int add = channel == 1 ? 10 * 255 / 63 : 10 * 255 / 31;
        for (int y = 0; y < viewH; ++y) {
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
                for (int x = std::max(0, int(xs[k]) - left); x < std::min(viewW, int(xs[k + 1]) - left); ++x) {
                    uint8_t &c = view.row(y)[x * 4 + channel];
                    c = uint8_t(std::min(255, c + add));
                }
        }
    }
    // The selection's marker on the ground under each officer chosen.
    if (_teamShown)
        for (const Officer &o : _officers)
            if (o.chosen)
                blitBy(view, animation("Interface/Selection", "Idle").at(now), int(std::lround(o.x)) - left,
                       int(std::lround(o.y)) + kMarkerY - top);
    // The mines put down, on the ground under everyone: their Deploy
    // Sequence once as they're put down, then Idle (0x48eb9d), not their
    // model's Default Sequence; set off, its Activate once, its last frame
    // held till it goes (the proximity mine's Enter Action plays it, and
    // its last frame destroys it; the remote mine goes at once).
    for (const Mine &m : _mines) {
        const ModelInfo &info = modelInfo(m.model);
        const auto once = [&](const std::string &sequence, float seconds) -> const Animation::Picture * {
            const Animation &a = animation(m.model, sequence);
            const int frames = a.perDirection();
            if (sequence.empty() || frames <= 0 || a.fps <= 0.0f)
                return nullptr;
            return a.frame(std::min(int(seconds * a.fps), frames - 1), 0);
        };
        const Animation::Picture *p = nullptr;
        if (m.setOff)
            p = once("Activate", info.fuseAfterTrigger - m.fuse);
        else if (const Animation &d = animation(m.model, m.item->deploySequence);
                 !m.item->deploySequence.empty() && d.fps > 0.0f && m.placed * d.fps < float(d.perDirection()))
            p = once(m.item->deploySequence, m.placed);
        if (!p)
            p = animation(m.model, "Idle").at(now);
        blitBy(view, p, int(std::lround(m.x)) - left, int(std::lround(m.y)) - top);
    }
    // The models and the officers among them, back to front: each picture,
    // or each strip of it its sequence's cuts make, by where it stands, its
    // model's Sorting Y and the strip's offset, eight times over, and the
    // layer drawn (0x4915d2); ties below.
    struct Look {
        const Animation *body = nullptr;
        std::string sequence;
        int frame = 0;
        int direction = 0;
        int x = 0; // on the view, where it stands
        int y = 0;
        int lift = 0; // its Rendering Height
        int shown = 255;
    };
    struct Piece {
        float key;
        int part;       // or -1 for an officer, -2 for a particle, -3 for a standing polygon
        size_t officer; // the officer's, or the standing polygon's part
        int clipLeft;
        int clipRight;
        Look look;
        const Animation::Picture *particle = nullptr; // a particle's, at the look's x and y
    };
    std::vector<Piece> order;
    for (size_t i : _standing) {
        const MapPart &p = _map.parts[i];
        if (!shown(i))
            continue;
        if (p.x < float(left - 600) || p.x > float(left + viewW + 600) || p.y < float(top - 600) ||
            p.y > float(top + viewH + 600 + p.renderingHeight))
            continue;
        // Facing its way; an enemy where its AI has taken it, walking or
        // standing.
        const auto enemy = _enemyOfPart.find(i);
        const Enemy *e = enemy != _enemyOfPart.end() ? &_enemies[enemy->second] : nullptr;
        Look look;
        look.sequence = p.sequence.empty() ? "Idle" : p.sequence;
        if (e && (look.sequence == "Idle" || look.sequence == "Walk"))
            look.sequence = e->walking ? (e->running ? "Run" : "Walk") : "Idle";
        int fought = 0;
        const std::string fighting =
            e ? combatSequence(e->shooting, e->dead, {e->stunned, e->stunTotal, e->stunLength}, fought, p.model) : "";
        if (!fighting.empty())
            look.sequence = fighting;
        if (e && !e->onceSequence.empty())
            look.sequence = e->onceSequence;
        const Animation &body = animation(p.model, look.sequence);
        look.body = &body;
        look.direction = body.direction(e ? e->angle : p.angle);
        look.frame = !fighting.empty() ? fought : int(double(now) * double(body.fps) / 1000.0) % 100000;
        if (const auto stop = _stoppedAt.find(i); stop != _stoppedAt.end() && fighting.empty())
            look.frame = std::min(look.frame, stop->second);
        // Something put down plays its sequence from its first frame, once
        // through if it's to stop.
        for (const Deployed &d : _deployed)
            if (d.part == i) {
                look.frame = int(d.time * body.fps);
                if (d.once)
                    look.frame = std::min(look.frame, std::max(0, body.perDirection() - 1));
            }
        if (e && !e->onceSequence.empty())
            look.frame = std::min(int(e->onceTime * body.fps), std::max(0, body.perDirection() - 1));
        if (e && e->walking && fighting.empty()) {
            if (body.cycleDistance(look.direction) > 0.0f)
                look.frame = int(body.frameAt(look.direction, e->travelled));
        }
        const float standY = e ? e->y : p.y;
        look.x = int(std::lround(e ? e->x : p.x)) - left;
        look.y = int(std::lround(standY)) - top;
        look.lift = p.renderingHeight;
        // Beamed away, it fades as it goes (inferred).
        look.shown = int(std::lround(255.0f * warpShown(i) * deathShown(i)));
        const float sortY = standY + float(modelInfo(p.model).sortingY);
        const Animation::Picture *picture = body.frame(look.frame, look.direction);
        if (!picture || body.cuts.empty()) {
            order.push_back({(sortY + float(body.sortingY)) * 8.0f, int(i), 0, INT_MIN, INT_MAX, look});
            continue;
        }
        // Its strips: from the frame's left to the first cut, between the
        // cuts, and from the last to its right (0x5518d0).
        const int frameLeft = -picture->originX;
        const int frameRight = picture->image.w - picture->originX;
        for (size_t k = 0; k <= body.cuts.size(); ++k) {
            const int from = k > 0 ? std::max(frameLeft, body.cuts[k - 1].x) : frameLeft;
            const int to = k < body.cuts.size() ? std::min(frameRight, body.cuts[k].x) : frameRight;
            if (from >= to)
                continue;
            const int offset = k > 0 ? body.cuts[k - 1].sortingY : body.sortingY;
            order.push_back({(sortY + float(offset)) * 8.0f, int(i), 0, look.x + from, look.x + to, look});
        }
    }
    // A standing polygon by its bounds' foot and its YSort, across by its
    // bounds' left (0x54e9a3); the bounds' rounded as the polygon's place
    // is (0x53c91f).
    for (size_t i : _raised) {
        if (!shown(i))
            continue;
        const MapPart &p = _map.parts[i];
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
        if (x1 < float(left) || x0 > float(left + viewW) || y1 < float(top) || y0 > float(top + viewH))
            continue;
        Look at;
        at.x = int(std::lround(x0)) - left;
        order.push_back({float((std::lround(y1) + p.ySort) * 8), -3, i, INT_MIN, INT_MAX, at});
    }
    for (size_t i = 0; i < _officers.size(); ++i) {
        const float sortY = _officers[i].y + float(modelInfo(_team[_officers[i].member].bodyModel).sortingY);
        order.push_back({sortY * 8.0f, -1, i, INT_MIN, INT_MAX, Look{}});
    }
    for (const ParticleDrawn &d : particlesDrawn()) {
        Look at;
        at.x = d.x - left;
        at.y = d.y - top;
        order.push_back({d.key, -2, 0, INT_MIN, INT_MAX, at, d.picture});
    }
    // Equal keys by where they're drawn across, the left first (0x4a80e0,
    // the x at 0x4915b5); equal both, as they come (inferred: the program's
    // qsort leaves those in no set order).
    const auto across = [&](const Piece &p) {
        return p.part == -1 ? int(std::lround(_officers[p.officer].x)) - left : p.look.x;
    };
    std::stable_sort(order.begin(), order.end(), [&](const Piece &a, const Piece &b) {
        return a.key != b.key ? a.key < b.key : across(a) < across(b);
    });
    _drawn.clear();
    for (const Piece &piece : order) {
        if (piece.part == -2) {
            blitBy(view, piece.particle, piece.look.x, piece.look.y);
            continue;
        }
        if (piece.part == -3) {
            drawPolygon([&](int row) { return view.row(row); }, viewW, viewH, left, top, _map.parts[piece.officer]);
            continue;
        }
        if (piece.part < 0) {
            drawOfficer(view, _officers[piece.officer], left, top, now);
            continue;
        }
        const MapPart &p = _map.parts[size_t(piece.part)];
        const Look &look = piece.look;
        // Its shadow under it and its head over it, frame for frame
        // (inferred: as the officers'); each lifted by its Rendering
        // Height, but for a sequence on the ground, and a shadow's cast
        // down and left (0x491444).
        const auto place = [&](const Animation &a, int &x, int &y) {
            x = look.x;
            y = look.y;
            if (a.shadow) {
                x -= int(std::lround(float(look.lift) * kShadowSlantX));
                y -= int(std::lround(float(look.lift) * 0.5f));
            } else if (!a.onGround) {
                y -= look.lift;
            }
        };
        int x = 0;
        int y = 0;
        if (look.body->rotations > 1) {
            const Animation &shadow = animation(p.model, look.sequence + " Shadow");
            place(shadow, x, y);
            blitBy(view, shadow.frame(look.frame, look.direction), x, y, 1.0f, look.shown, piece.clipLeft,
                   piece.clipRight);
        }
        // Kept to pick by: each picture drawn, a strip within its clip.
        const auto drawn = [&](const Animation::Picture *picture, int at, int up) {
            blitBy(view, picture, at, up, 1.0f, look.shown, piece.clipLeft, piece.clipRight);
            if (picture)
                _drawn.push_back({size_t(piece.part), picture, at + left, up + top,
                                  piece.clipLeft == INT_MIN ? INT_MIN : piece.clipLeft + left,
                                  piece.clipRight == INT_MAX ? INT_MAX : piece.clipRight + left});
        };
        // Beamed away, its pictures under the warp's noise (0x563f80).
        std::string effect;
        float amount = 0.0f;
        for (const Warp &w : _warping)
            if (w.part == size_t(piece.part)) {
                effect = w.effect;
                amount = warpAmount(w.time - w.left, w.time, w.fadeIn);
            }
        place(*look.body, x, y);
        drawn(underWarp(look.body->frame(look.frame, look.direction), effect, amount), x, y);
        if (!p.overlay.empty()) {
            const Animation &head = animation(p.overlay, look.sequence);
            place(head, x, y);
            drawn(underWarp(head.frame(look.frame, look.direction), effect, amount), x, y);
        }
    }
    drawVision(view, left, top);
    drawNoises(view, left, top);
    // The grenades thrown, flying or lying.
    for (const Thrown &t : _thrown) {
        const Animation &a = animation(t.model, "Idle");
        blitBy(view, a.at(now), int(std::lround(t.x)) - left, int(std::lround(t.y - t.z)) - top);
    }
    // The beams: their middle line, and one either side in their edges'
    // colour (CEntityAnimatedLine, 0x499470), plain.
    for (const Beam &b : _beams) {
        constexpr float kPlain = 1.0f;
        const float len = std::hypot(b.x1 - b.x0, b.y1 - b.y0);
        const float px = len > 0.0f ? -(b.y1 - b.y0) / len : 0.0f;
        const float py = len > 0.0f ? (b.x1 - b.x0) / len : 0.0f;
        for (const int side : {-1, 1})
            drawLine(view, b.x0 + px * float(side) - float(left), b.y0 + py * float(side) - float(top),
                     b.x1 + px * float(side) - float(left), b.y1 + py * float(side) - float(top), b.outside, kPlain);
        drawLine(view, b.x0 - float(left), b.y0 - float(top), b.x1 - float(left), b.y1 - float(top), b.middle, kPlain);
        // Its flashes on its ends, 2 down, round their frames (0x5361e0,
        // 0x535fd2).
        const auto flash = [&](const std::string &model, float x, float y) {
            const Animation &a = animation(model, "Idle");
            const int frames = a.perDirection();
            if (model.empty() || frames <= 0)
                return;
            const int frame = int(b.time * std::max(a.fps, 0.0f)) % frames;
            blitBy(view, a.frame(frame, 0), int(std::lround(x)) - left, int(std::lround(y + 2.0f)) - top);
        };
        flash(b.shotFlash, b.x0, b.y0);
        if (b.hit)
            flash(b.hitFlash, b.x1, b.y1);
    }
    // The parts beamed away, their Warp Overlay Effect over them: its frames
    // a direction's worth after another, for Num Rotations directions; the
    // direction the figure faces, when its own has as many, else the first;
    // its frames played over the warp's time, raised by its Overlay YOffset
    // (0x584100, 0x563940).
    for (const Warp &w : _warping) {
        if (w.overlay.empty() || w.time <= 0.0f)
            continue;
        const Animation &a = animation(w.overlay, "");
        const int each = a.perDirection() / w.rotations;
        if (each <= 0)
            continue;
        const MapPart &p = _map.parts[w.part];
        const auto enemy = _enemyOfPart.find(w.part);
        const float angle = enemy != _enemyOfPart.end() ? _enemies[enemy->second].angle : p.angle;
        const Animation &body = animation(p.model, "Idle");
        const int direction = body.rotations == w.rotations ? body.direction(angle) : 0;
        const float done = std::clamp(1.0f - w.left / w.time, 0.0f, 1.0f);
        const int frame = direction * each + std::min(each - 1, int(done * float(each)));
        float x = 0.0f;
        float y = 0.0f;
        placeOf(w.part, x, y);
        blitBy(view, a.frame(frame, 0), int(std::lround(x)) - left, int(std::lround(y)) + w.overlayY - top);
    }
    // The shots' flashes, over everyone.
    for (const Flash &f : _flashes) {
        const Animation &a = animation(f.model, f.sequence);
        const bool on = f.follow >= 0 && size_t(f.follow) < _enemies.size();
        const float fx = on ? _enemies[size_t(f.follow)].x : f.x;
        const float fy = on ? _enemies[size_t(f.follow)].y : f.y;
        blitBy(view, a.frame(int(f.time * a.fps), 0), int(std::lround(fx)) - left, int(std::lround(fy)) - top);
    }
    if (viewW == _viewW && viewH == _viewH) {
        for (int y = 0; y < _viewH && _viewY + y < screen.h; ++y)
            std::memcpy(screen.row(_viewY + y) + _viewX * 4, view.row(y),
                        size_t(std::min(_viewW, screen.w - _viewX)) * 4);
    } else {
        for (int y = 0; y < _viewH && _viewY + y < screen.h; ++y) {
            const uint8_t *from = view.row(std::min(viewH - 1, int(float(y) / _zoom)));
            uint8_t *to = screen.row(_viewY + y) + _viewX * 4;
            for (int x = 0; x < std::min(_viewW, screen.w - _viewX); ++x)
                std::memcpy(to + x * 4, from + std::min(viewW - 1, int(float(x) / _zoom)) * 4, 4);
        }
    }
    // Time stopped, shown on the view, under the interface (inferred).
    if (_paused)
        drawPause(screen, now);
    _hud->setMove(_selected >= 0 ? _officers[size_t(_selected)].move : 0);
    std::vector<bool> chosen;
    for (const Officer &o : _officers)
        chosen.push_back(o.chosen);
    _hud->setChosen(chosen);
    std::vector<float> health;
    for (const Officer &o : _officers)
        health.push_back(o.maxHealth > 0.0f ? o.health / o.maxHealth : 1.0f);
    _hud->setHealth(health);
    std::vector<bool> onTeam;
    for (const Officer &o : _officers)
        onTeam.push_back(o.standIn < 0);
    _hud->setOnTeam(onTeam);
    _hud->setListening(_listening);
    _hud->setPaused(_paused);
    if (_selected >= 0) {
        const Officer &chosen = _officers[size_t(_selected)];
        const bool stun = chosen.item >= 0 && chosen.items[size_t(chosen.item)].stun;
        _hud->setItemPanel(stun, chosen.scanText, size_t(std::max(0.0f, chosen.scanTyped)));
        _hud->setEnhancement(enhancement(_selected));
        _hud->setOverload(chosen.item >= 0 ? chosen.items[size_t(chosen.item)].overload : -1.0f);
        if (chosen.item >= 0)
            _hud->setCharge(chosen.items[size_t(chosen.item)].energy, chosen.items[size_t(chosen.item)].on);
    }
    _hud->setVision(_vision);
    if (_meldChosen) {
        // The enemy taken over has the Psychic Rack's Controller Item, and
        // its panel (0x5d5233): the charge, and BREAK CONTROL.
        const Officer &o = _officers[size_t(_melder)];
        std::string controller;
        for (const Officer::Held &h : o.items)
            if (const auto def = _items.find(h.name); def != _items.end() && def->second.psyche && h.on) {
                controller = def->second.controllerItem;
                _hud->setCharge(h.energy, true);
            }
        std::vector<GameHud::Place> places(_itemTypes.size());
        const auto def = _items.find(controller);
        const auto type =
            def == _items.end() ? _itemTypes.end() : std::find(_itemTypes.begin(), _itemTypes.end(), def->second.type);
        if (type != _itemTypes.end())
            places[size_t(type - _itemTypes.begin())] = {0, 1, {0}};
        _hud->setItemPanel(false, {}, 0);
        _hud->setOverload(-1.0f);
        _hud->setInventory({{controller, -1}}, std::move(places), 0);
    } else if (_selected >= 0) {
        const Officer &o = _officers[size_t(_selected)];
        std::vector<GameHud::Held> held;
        for (const Officer::Held &h : o.items)
            held.push_back({h.name, h.count});
        // Each place shows the item of its own it's come to.
        std::vector<GameHud::Place> places;
        const std::vector<std::vector<int>> slots = itemPlaces(o);
        for (size_t p = 0; p < slots.size(); ++p) {
            const int at = p < o.shown.size() ? o.shown[p] : 0;
            places.push_back({slots[p].empty() ? -1 : slots[p][size_t(std::clamp(at, 0, int(slots[p].size()) - 1))],
                              int(slots[p].size()), slots[p]});
        }
        _hud->setInventory(std::move(held), std::move(places), o.item);
    } else {
        _hud->setInventory({}, {}, -1);
    }
    _hud->setMarks(minimapMarks());
    _hud->setItemTitle(_itemTitle > 0.0f);
    _hud->draw(screen, now, _selected, _cameraX, _cameraY, viewW, viewH);
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
    // The mission ended on another of its maps: its end shown here too.
    if (_progress->over && !_endGame) {
        _objectivesWindow.reset();
        _endGame = std::make_unique<EndGameWindow>(_data, _rules, _progress->completed, _progress->overText, 640, 480);
    }
    if (_objectivesWindow)
        _objectivesWindow->draw(screen, objectiveLines());
    if (_escape)
        _escape->draw(screen, now);
    if (_endGame)
        _endGame->draw(screen, objectiveLines());
}

} // namespace reluva::stawayteam
