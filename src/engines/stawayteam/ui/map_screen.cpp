// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/map_screen.h"

#include "engines/stawayteam/game/controls.h"
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
// A walk's speed for a model without one: the retail bodies' sideways,
// 26 pixels in 10 frames at 14 a second.
constexpr float kDefaultWalk = 36.4f;
// Too near a waypoint to walk any further to it.
constexpr float kArrived = 1.0f;
const char *const kExitQuestion = "Are you sure you want to quit the game and exit to the main menu?"; // 0x63a9e0

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
                     uint64_t now, std::vector<TeamMember> team, WayPoints wayPoints)
    : _data(data), _rules(rules), _map(std::move(map)), _options(options), _last(now), _team(std::move(team)),
      _wayPoints(std::move(wayPoints)) {
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
        if (!p.visible)
            continue;
        if (!p.polygon.empty()) {
            if (p.renderType != "Textured" && p.renderType != "Black" && p.renderType != "Shadow")
                continue;
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
    // On the team: the camera's scripts that take it there as the mission
    // starts aren't there yet.
    if (!_officers.empty()) {
        _selected = 0;
        moveCamera(_officers[0].x, _officers[0].y);
    }
    _hud = std::make_unique<GameHud>(data, rules, _team, _map.minimap, _map.width, _map.height);
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

const Animation &MapScreen::model(const MapPart &part) {
    const std::string sequence = part.sequence.empty() ? "Idle" : part.sequence;
    const std::string key = part.model + "\n" + sequence;
    auto it = _models.find(key);
    if (it == _models.end()) {
        Animation a;
        (void)a.load(_data, part.model, sequence);
        it = _models.emplace(key, std::move(a)).first;
    }
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
        if (std::abs(_officers[i].x - x) <= 12.0f && y >= _officers[i].y - 20.0f && y <= _officers[i].y + 24.0f)
            return int(i);
    return -1;
}

void MapScreen::order(float x, float y) {
    if (_selected < 0)
        return;
    Officer &o = _officers[size_t(_selected)];
    o.path = _wayPoints.points.empty() ? std::vector<std::pair<float, float>>{{x, y}} : _wayPoints.path(o.x, o.y, x, y);
}

void MapScreen::walk(Officer &o, float seconds) {
    if (o.path.empty())
        return;
    const Animation &walking = animation(_team[o.member].bodyModel, "Walk");
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
        o.travelled = cycle > 0.0f ? std::fmod(o.travelled + step, cycle) : 0.0f;
        left -= step / speed;
    }
}

void MapScreen::update(uint64_t now) {
    const float seconds = float(now - std::min(now, _last)) / 1000.0f;
    _last = now;
    scroll(seconds);
    for (Officer &o : _officers)
        walk(o, seconds);
}

void MapScreen::drawOfficer(Surface &view, const Officer &o, int left, int top, uint64_t now) {
    const TeamMember &m = _team[o.member];
    const bool walking = !o.path.empty();
    const char *sequence = walking ? "Walk" : "Idle";
    const Animation &body = animation(m.bodyModel, sequence);
    const Animation &shadow = animation(m.bodyModel, std::string(sequence) + " Shadow");
    const Animation &head = animation(m.headModel, sequence);
    // Walking, the frame is how far into the cycle they are; standing, by
    // the time (inferred). The head is drawn over the body, frame for frame
    // (its model's Overlay Model).
    int frame = int(double(now) * double(body.fps) / 1000.0) % 100000;
    if (walking) {
        const float cycle = body.cycleDistance(o.direction);
        frame = cycle > 0.0f ? int(o.travelled / cycle * float(body.perDirection())) : frame;
    }
    const int x = int(std::lround(o.x)) - left;
    const int y = int(std::lround(o.y)) - top;
    blitBy(view, shadow.frame(frame, o.direction), x, y);
    blitBy(view, body.frame(frame, o.direction), x, y);
    blitBy(view, head.frame(frame, o.direction), x, y);
}

MapScreen::Choice MapScreen::handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now) {
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
        // A double click on an officer's slot shows them too (inferred).
        _selected = hud.select;
        if (ev.clicks >= 2)
            moveCamera(_officers[size_t(_selected)].x, _officers[size_t(_selected)].y);
    }
    if (hud.moveCamera)
        moveCamera(hud.cameraX, hud.cameraY);
    // A click on the map: on an officer chooses them, elsewhere sends the
    // one chosen walking there (inferred: the mouse's scheme isn't traced).
    if (!hud.used && ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && ev.mouseX >= _viewX &&
        ev.mouseY >= _viewY && ev.mouseX < _viewX + _viewW && ev.mouseY < _viewY + _viewH) {
        const float x = _cameraX - float(_viewW) / 2.0f + float(ev.mouseX - _viewX);
        const float y = _cameraY - float(_viewH) / 2.0f + float(ev.mouseY - _viewY);
        if (const int officer = officerAt(x, y); officer >= 0)
            _selected = officer;
        else
            order(x, y);
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
    }
    return Choice::kNone;
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
    // The selection's marker on the ground under the officer chosen.
    if (_selected >= 0) {
        const Officer &o = _officers[size_t(_selected)];
        blitBy(view, animation("Interface/Selection", "Idle").at(now), int(std::lround(o.x)) - left,
               int(std::lround(o.y)) + kMarkerY - top);
    }
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
        if (p.x < float(left - 600) || p.x > float(left + _viewW + 600) || p.y < float(top - 600) ||
            p.y > float(top + _viewH + 600))
            continue;
        order.push_back({p.y + float(p.renderingHeight), int(i), 0});
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
        blitBy(view, model(p).at(now), int(std::lround(p.x)) - left, int(std::lround(p.y)) - top);
    }
    for (int y = 0; y < _viewH && _viewY + y < screen.h; ++y)
        std::memcpy(screen.row(_viewY + y) + _viewX * 4, view.row(y), size_t(std::min(_viewW, screen.w - _viewX)) * 4);
    _hud->draw(screen, now, _selected, _cameraX, _cameraY, _viewW, _viewH);
    if (_confirm)
        _confirm->draw(screen);
}

} // namespace reluva::stawayteam
