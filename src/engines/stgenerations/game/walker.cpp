// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/walker.h"

#include <algorithm>
#include <cmath>

#include "common/serializer.h"

namespace reluva::stgenerations {

namespace {

const double kFixed = 65536.0;
// Distance kept from a wall after touching it.
const double kSkin = 0.01;

double fromFixed(int32_t v) {
    return v / kFixed;
}

// Even-odd point in polygon test on the floor plan.
bool containsPoint(const std::vector<LevelFile::Vertex> &vertices, const LevelFile::Sector &flat, double x, double y) {
    bool inside = false;
    size_t n = flat.vertices.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        double xi = fromFixed(vertices[flat.vertices[i]].x), yi = fromFixed(vertices[flat.vertices[i]].y);
        double xj = fromFixed(vertices[flat.vertices[j]].x), yj = fromFixed(vertices[flat.vertices[j]].y);
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}

// Whether (x, y) is inside the flat or within margin of its edges.
bool nearPolygon(const std::vector<LevelFile::Vertex> &vertices, const LevelFile::Sector &flat, double x, double y,
                 double margin) {
    if (containsPoint(vertices, flat, x, y))
        return true;
    size_t n = flat.vertices.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        double xi = fromFixed(vertices[flat.vertices[i]].x), yi = fromFixed(vertices[flat.vertices[i]].y);
        double xj = fromFixed(vertices[flat.vertices[j]].x), yj = fromFixed(vertices[flat.vertices[j]].y);
        double ex = xj - xi, ey = yj - yi, length2 = ex * ex + ey * ey;
        double t = length2 > 0 ? std::max(0.0, std::min(1.0, ((x - xi) * ex + (y - yi) * ey) / length2)) : 0;
        double dx = xi + ex * t - x, dy = yi + ey * t - y;
        if (dx * dx + dy * dy <= margin * margin)
            return true;
    }
    return false;
}

} // namespace

Walker::Walker(const LevelFile &level) : _level(level) {
    std::shared_ptr<std::vector<WallLine>> lines(new std::vector<WallLine>(level.getWalls().size()));
    _walls = lines;
    for (size_t i = 0; i < lines->size(); i++)
        makeLine((int)i, (*lines)[i]);
}

void Walker::makeLine(int index, WallLine &line) const {
    const std::vector<LevelFile::Vertex> &vertices = _level.getVertices();
    const LevelFile::Wall &wall = _level.getWalls()[index];
    line.index = index;
    line.x1 = fromFixed(vertices[wall.vertex1].x);
    line.y1 = fromFixed(vertices[wall.vertex1].y);
    line.x2 = fromFixed(vertices[wall.vertex2].x);
    line.y2 = fromFixed(vertices[wall.vertex2].y);
    line.nx = fromFixed(wall.normalX);
    line.ny = fromFixed(wall.normalY);
    line.d = fromFixed(wall.distance);
    line.top = fromFixed(wall.top);
    line.bottom = fromFixed(wall.bottom);
    line.zones = wall.zones;
    line.bothSides = (wall.flags & LevelFile::kWallSolidBothSides) != 0;
    line.passable = (wall.flags & LevelFile::kWallNoCollision) != 0;
}

void Walker::refreshWalls(const std::vector<int> &walls) {
    // Every copy of this walker (the actors' too) shares the lines.
    for (int index : walls)
        makeLine(index, (*_walls)[index]);
}

void Walker::sync(Serializer &s) {
    s.syncAsDouble(_x);
    s.syncAsDouble(_y);
    s.syncAsDouble(_feet);
    s.syncAsDouble(_fallSpeed);
    s.syncAsInt(_floor);
    s.syncAsBool(_falls);
    s.syncAsUint32(_ignoredFloors);
    _ignoredFloors |= LevelFile::kFlagPassThrough;
    s.syncAsDouble(_eyeHeight);
    s.syncAsDouble(_stepHeight);
    s.syncAsDouble(_headroom);
    s.syncAsDouble(_radius);
    s.syncAsUint64(_zones);
    s.syncAsBool(_collision);
    s.syncAsDouble(_gravity);
    s.syncAsDouble(_maxFallSpeed);
    if (s.version() == 0 || s.version() >= kStepDownSaveVersion)
        s.syncAsDouble(_stepDown);
    else
        _stepDown = kStepDown;
    if (s.isLoading()) {
        _contactWall = -1;
        _contactObstacle = -1;
    }
}

void Walker::place(double x, double y, double feet) {
    _x = x;
    _y = y;
    _feet = feet;
    _fallSpeed = 0;
    _zones = kAllZones;
    _contactWall = -1;
    _contactObstacle = -1;
    _floor = findFloor(x, y, feet + _stepHeight, _zones);
    if (_floor >= 0) {
        const LevelFile::Sector &flat = _level.getSectors()[_floor];
        _feet = fromFixed(flat.height);
        _zones = flat.getActiveZones() ? flat.getActiveZones() : kAllZones;
    }
    duck();
}

void Walker::move(double dx, double dy, double seconds) {
    _contactWall = -1;
    _contactObstacle = -1;
    if (dx != 0 || dy != 0)
        collideAndSlide(dx, dy);
    followFloor(seconds);
}

int Walker::findFloor(double x, double y, double maxHeight, uint64_t zones, uint32_t ignoredFlags) const {
    int best = -1;
    double bestHeight = 0;
    const std::vector<LevelFile::Sector> &flats = _level.getSectors();
    for (size_t i = 0; i < flats.size(); i++) {
        const LevelFile::Sector &flat = flats[i];
        if (flat.isCeiling() || flat.vertices.size() < 3 || !(flat.zones & zones) || (flat.flags & ignoredFlags))
            continue;
        double h = fromFixed(flat.height);
        if (h > maxHeight || (best >= 0 && h <= bestHeight))
            continue;
        if (containsPoint(_level.getVertices(), flat, x, y)) {
            best = (int)i;
            bestHeight = h;
        }
    }
    return best;
}

bool Walker::sweep(double x, double y, double dx, double dy, Hit &hit) const {
    double length2 = dx * dx + dy * dy;
    if (length2 == 0)
        return false;

    hit.t = 2;
    hit.wall = hit.obstacle = -1;
    double low = _feet + _stepHeight, high = getEye() + _headroom;
    const std::vector<LevelFile::Wall> &walls = _level.getWalls();
    for (const WallLine &w : *_walls) {
        // Like the original, only walls that are drawn get in the way, and
        // scripts can hide them.
        if (!(walls[w.index].flags & LevelFile::kFlagVisible) || w.passable)
            continue;
        // A wall whose bottom the head just touches doesn't get in the way.
        if (!(w.zones & _zones) || w.bottom >= high || w.top < low)
            continue;
        // A door slid all the way open has no height left. The doorway can
        // be exactly as tall as the player's head (AMARGOS1's upper doors).
        if (w.top <= w.bottom)
            continue;

        // The wall's line, approached from its solid side.
        double nx = w.nx, ny = w.ny, d = w.d;
        double side0 = nx * x + ny * y + d;
        bool facing = true;
        if (side0 < 0) {
            if (w.bothSides) {
                nx = -nx;
                ny = -ny;
                d = -d;
                side0 = -side0;
            } else {
                facing = false;
            }
        }
        if (facing) {
            // Already closer than the radius: let the player move away.
            double r = std::min(_radius, side0);
            double side1 = nx * (x + dx) + ny * (y + dy) + d;
            if (side1 < r && side0 > side1) {
                double t = std::max(0.0, (side0 - r) / (side0 - side1));
                double qx = x + dx * t - nx * r, qy = y + dy * t - ny * r;
                double wx = w.x2 - w.x1, wy = w.y2 - w.y1;
                double wl2 = wx * wx + wy * wy;
                double s = wl2 > 0 ? ((qx - w.x1) * wx + (qy - w.y1) * wy) / wl2 : -1;
                if (s >= 0 && s <= 1 && t < hit.t) {
                    double wl = std::sqrt(wl2);
                    hit.t = t;
                    hit.slideX = wx / wl;
                    hit.slideY = wy / wl;
                    hit.wall = w.index;
                    hit.obstacle = -1;
                }
            }
        }

        // The wall's ends, as posts the player's circle can catch on. They
        // are a little slimmer than the player, so a gap exactly as wide as
        // the player (enterp's archways for its width of 3) lets them by.
        const double ends[2][2] = {{w.x1, w.y1}, {w.x2, w.y2}};
        double post = std::max(0.0, _radius - kPostSlack);
        for (const auto &end : ends) {
            double px = x - end[0], py = y - end[1];
            double b = 2 * (dx * px + dy * py);
            double c = px * px + py * py - post * post;
            if (b >= 0)
                continue; // moving away
            double t;
            if (c <= 0) {
                t = 0;
            } else {
                double disc = b * b - 4 * length2 * c;
                if (disc < 0)
                    continue;
                t = (-b - std::sqrt(disc)) / (2 * length2);
            }
            if (t < 0 || t > 1 || t >= hit.t)
                continue;
            double cx = px + dx * t, cy = py + dy * t;
            double cl = std::sqrt(cx * cx + cy * cy);
            if (cl == 0)
                continue;
            hit.t = t;
            hit.slideX = -cy / cl;
            hit.slideY = cx / cl;
            hit.wall = w.index;
            hit.obstacle = -1;
        }
    }

    if (_obstacles) {
        for (size_t i = 0; i < _obstacles->size(); i++) {
            const Obstacle &o = (*_obstacles)[i];
            if ((int)i == _ignoredObstacle || o.radius <= 0 || o.bottom > high || o.top < low)
                continue;
            // As a wall end, with the two radii combined.
            double r = _radius + o.radius;
            double px = x - o.x, py = y - o.y;
            double b = 2 * (dx * px + dy * py);
            if (b >= 0)
                continue;
            double c = px * px + py * py - r * r;
            double t;
            if (c <= 0) {
                t = 0;
            } else {
                double disc = b * b - 4 * length2 * c;
                if (disc < 0)
                    continue;
                t = (-b - std::sqrt(disc)) / (2 * length2);
            }
            if (t < 0 || t > 1 || t >= hit.t)
                continue;
            double cx = px + dx * t, cy = py + dy * t;
            double cl = std::sqrt(cx * cx + cy * cy);
            if (cl == 0)
                continue;
            hit.t = t;
            hit.slideX = -cy / cl;
            hit.slideY = cx / cl;
            hit.wall = -1;
            hit.obstacle = (int)i;
        }
    }
    return hit.t <= 1;
}

void Walker::collideAndSlide(double dx, double dy) {
    if (!_collision) {
        _x += dx;
        _y += dy;
        return;
    }

    Hit hit;
    if (!sweep(_x, _y, dx, dy, hit)) {
        _x += dx;
        _y += dy;
        return;
    }
    _contactWall = hit.wall;
    _contactObstacle = hit.obstacle;

    // Stop just short of the wall, then slide along it with the rest.
    double length = std::sqrt(dx * dx + dy * dy);
    double t = std::max(0.0, hit.t - kSkin / length);
    _x += dx * t;
    _y += dy * t;
    double restX = dx * (1 - t), restY = dy * (1 - t);
    double along = restX * hit.slideX + restY * hit.slideY;
    double sx = hit.slideX * along, sy = hit.slideY * along;

    Hit second;
    if (!sweep(_x, _y, sx, sy, second)) {
        _x += sx;
        _y += sy;
        return;
    }
    double slideLength = std::sqrt(sx * sx + sy * sy);
    double t2 = slideLength > 0 ? std::max(0.0, second.t - kSkin / slideLength) : 0;
    _x += sx * t2;
    _y += sy * t2;
}

void Walker::followFloor(double seconds) {
    followFloorOnly(seconds);
    duck();
}

void Walker::followFloorOnly(double seconds) {
    double maxHeight = _feet + _stepHeight;
    int floor = findFloor(_x, _y, maxHeight, _zones);
    if (floor < 0 && _zones != kAllZones)
        floor = findFloor(_x, _y, maxHeight, kAllZones);
    _floor = floor;
    if (floor < 0) {
        // Nothing underfoot: the original opens up every zone.
        _zones = kAllZones;
        _fallSpeed = 0;
        return;
    }

    const LevelFile::Sector &flat = _level.getSectors()[floor];
    _zones = flat.getActiveZones() ? flat.getActiveZones() : kAllZones;
    double height = fromFixed(flat.height);
    // Standing (not already falling), a small drop is taken in one step.
    // Rising from a jump, nothing holds the player down yet.
    if (!_falls || (_fallSpeed >= 0 && (height >= _feet || (_fallSpeed == 0 && _feet - height <= _stepDown)))) {
        // Step up (or stay on the level).
        _feet = height;
        _fallSpeed = 0;
        return;
    }
    _fallSpeed = std::min(_fallSpeed + _gravity * seconds, _maxFallSpeed);
    _feet -= _fallSpeed * seconds;
    if (_fallSpeed < 0) {
        // The head stops at the ceiling (0x4B46A0, event 3).
        int ceiling = -1;
        double top = findCeiling(_x, _y, _feet, _zones, &ceiling) - _headroom - _eyeHeight;
        if (_feet > top) {
            _feet = std::max(height, top);
            _ceilingSpeed = -_fallSpeed;
            _ceilingFlat = ceiling;
            _fallSpeed = 0;
        }
    }
    if (_feet <= height) {
        _feet = height;
        _landingSpeed = _fallSpeed;
        _landingFloor = floor;
        _fallSpeed = 0;
    }
}

bool Walker::flatContains(int flat, double x, double y) const {
    const std::vector<LevelFile::Sector> &flats = _level.getSectors();
    return flat >= 0 && flat < (int)flats.size() && flats[flat].vertices.size() >= 3 &&
           containsPoint(_level.getVertices(), flats[flat], x, y);
}

void Walker::jump(double speed) {
    // 0x4B6800: the fall speed takes the jump away, upwards.
    _fallSpeed -= speed;
}

bool Walker::isAirborne() const {
    if (_fallSpeed != 0)
        return true;
    return _floor >= 0 && _feet > fromFixed(_level.getSectors()[_floor].height) + 1e-6;
}

void Walker::crouch(bool down, double seconds) {
    // 0x43AD04: the eye eases to 2.5 above the feet, half a unit a frame.
    double target = down ? std::max(0.0, _eyeHeight - kCrouchEye) : 0;
    double step = kCrouchSpeed * seconds;
    _crouch = _crouch < target ? std::min(target, _crouch + step) : std::max(target, _crouch - step);
}

void Walker::moveVertically(double dz) {
    // Swimming in low gravity: between the floor and the ceiling.
    _fallSpeed = 0;
    double floor = _floor >= 0 ? fromFixed(_level.getSectors()[_floor].height) : -1e9;
    double top = findCeiling(_x, _y, _feet, _zones) - _headroom - _eyeHeight;
    _feet = std::max(floor, std::min(top, _feet + dz));
}

double Walker::findCeiling(double x, double y, double minHeight, uint64_t zones, int *index) const {
    double best = 1e9;
    const std::vector<LevelFile::Sector> &flats = _level.getSectors();
    for (size_t i = 0; i < flats.size(); i++) {
        const LevelFile::Sector &flat = flats[i];
        // Pass-through ceilings (water surfaces) don't stop the head (0x4B46A0).
        if (!flat.isCeiling() || flat.vertices.size() < 3 || !(flat.zones & zones) ||
            (flat.flags & LevelFile::kFlagPassThrough))
            continue;
        double h = fromFixed(flat.height);
        if (h > minHeight && h < best && containsPoint(_level.getVertices(), flat, x, y)) {
            best = h;
            if (index)
                *index = (int)i;
        }
    }
    return best;
}

void Walker::duck() {
    // 0x4B46A0: with the head (eye + headroom) above the ceiling, the eye
    // comes down to fit; it stands up again where there is room.
    _ducked = 0;
    if (_floor < 0)
        return;
    double ceiling = findCeiling(_x, _y, _feet, _zones);
    double room = ceiling - _headroom - _feet;
    if (room < _eyeHeight)
        _ducked = _eyeHeight - std::max(room, _stepHeight);
}

bool Walker::lineOfSight(double ax, double ay, double az, double bx, double by, double bz, uint64_t zones) const {
    const double kSlack = 0.25, kFlatMargin = 0.5;
    double dx = bx - ax, dy = by - ay, dz = bz - az;
    double length = std::sqrt(dx * dx + dy * dy);
    if (length == 0 && dz == 0)
        return false;
    if (std::fabs(dz) > 2 * length)
        return false;

    const std::vector<LevelFile::Wall> &walls = _level.getWalls();
    double lowZ = std::min(az, bz), highZ = std::max(az, bz);
    for (const WallLine &w : *_walls) {
        if (!(walls[w.index].flags & LevelFile::kFlagVisible) || w.passable || !(w.zones & zones))
            continue;
        if (highZ < w.bottom - kSlack || lowZ > w.top + kSlack)
            continue;
        // a and b strictly on opposite sides of the wall's line...
        double sa = w.nx * ax + w.ny * ay + w.d, sb = w.nx * bx + w.ny * by + w.d;
        if ((sa > 0) == (sb > 0) || sa == 0 || sb == 0)
            continue;
        // ...and the wall, a little longer at each end, across the line of sight.
        double ex = w.x2 - w.x1, ey = w.y2 - w.y1, wallLength = std::sqrt(ex * ex + ey * ey);
        if (wallLength == 0)
            continue;
        double px = ex / wallLength * kSlack, py = ey / wallLength * kSlack;
        double s1 = dx * (w.y1 - py - ay) - dy * (w.x1 - px - ax);
        double s2 = dx * (w.y2 + py - ay) - dy * (w.x2 + px - ax);
        if ((s1 > 0) == (s2 > 0))
            continue;
        double z = az + dz * (sa / (sa - sb));
        if (z >= w.bottom - kSlack && z <= w.top + kSlack)
            return false;
    }

    if (dz != 0) {
        const std::vector<LevelFile::Sector> &flats = _level.getSectors();
        for (const LevelFile::Sector &flat : flats) {
            if (!(flat.flags & LevelFile::kFlagVisible) || !(flat.zones & zones) || flat.vertices.size() < 3)
                continue;
            double h = fromFixed(flat.height);
            if (h <= lowZ || h >= highZ)
                continue;
            double t = (h - az) / dz;
            if (nearPolygon(_level.getVertices(), flat, ax + dx * t, ay + dy * t, kFlatMargin))
                return false;
        }
    }
    return true;
}

} // namespace reluva::stgenerations
