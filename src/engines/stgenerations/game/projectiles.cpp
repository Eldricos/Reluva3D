// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/projectiles.h"

#include <algorithm>
#include <cmath>

#include "common/bytes.h"

namespace reluva::stgenerations {

namespace {

const double kFixed = 65536.0;
const double kPi = 3.14159265358979323846;
const size_t kRecordSize = 100;

double fromFixed(int32_t v) {
    return v / kFixed;
}

bool insideFlat(const std::vector<LevelFile::Vertex>& vertices, const LevelFile::Sector& flat, double x, double y) {
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

// Where along (x0, y0) + t (dx, dy), t in [0, 1], the point first comes
// within radius of (cx, cy); 0 if it starts there, or -1 if never.
double circleHit(double x0, double y0, double dx, double dy, double cx, double cy, double radius) {
    double fx = x0 - cx, fy = y0 - cy;
    double c = fx * fx + fy * fy - radius * radius;
    if (c <= 0)
        return 0;
    double a = dx * dx + dy * dy;
    if (a == 0)
        return -1;
    double b = 2 * (fx * dx + fy * dy);
    double discriminant = b * b - 4 * a * c;
    if (discriminant < 0)
        return -1;
    double t = (-b - std::sqrt(discriminant)) / (2 * a);
    return t >= 0 && t <= 1 ? t : -1;
}

} // namespace

const double Projectiles::kPlayerBelowEye = 6.0;
const double Projectiles::kPlayerAboveEye = 0.51;

Projectiles::Projectiles(const LevelFile& level, const std::vector<uint8_t>& section4) : _level(level), _random(0x50524F4A) {
    // The table ends at the first record of kind 0.
    for (size_t p = 0; p + kRecordSize <= section4.size(); p += kRecordSize) {
        const uint8_t* r = &section4[p];
        Type type;
        type.kind = (int)readLE32(r);
        if (type.kind == 0)
            break;
        type.animationRate = readLE32(r + 0x04);
        type.lifetime = readLE32(r + 0x08);
        type.texture = (uint16_t)readLE32(r + 0x0C);
        type.halfWidth = fromFixed((int32_t)readLE32(r + 0x10));
        type.height = fromFixed((int32_t)readLE32(r + 0x14));
        type.keyed = readLE32(r + 0x1C) != 0;
        type.translucent = readLE32(r + 0x20) != 0;
        type.frames = (uint16_t)std::max<uint32_t>(1, readLE32(r + 0x34));
        // A 16.16 value S: S * 1000 / 65536 units per second (0x4C5170
        // moves v * ms >> 16), so S / 65536 units per ms.
        type.speed = fromFixed((int32_t)readLE32(r + 0x3C)) / kFixed;
        type.fireSound = (int32_t)readLE32(r + 0x40);
        type.impactSound = (int32_t)readLE32(r + 0x44);
        type.damage = (int16_t)(int32_t)readLE32(r + 0x48);
        type.team = (int16_t)(int32_t)readLE32(r + 0x4C);
        type.count = (int32_t)readLE32(r + 0x54);
        uint32_t spacing = readLE32(r + 0x58);
        type.spacing = type.kind == kKindBurst ? (spacing & 0xFFFF) * 2 * kPi / 65536.0 : fromFixed((int32_t)spacing);
        type.impact = (int32_t)readLE32(r + 0x5C);
        _types.push_back(type);
    }

    _walls.resize(level.getWalls().size());
    for (size_t i = 0; i < _walls.size(); i++)
        makeLine((int)i);
}

void Projectiles::makeLine(int index) {
    const std::vector<LevelFile::Vertex>& vertices = _level.getVertices();
    const LevelFile::Wall& wall = _level.getWalls()[index];
    Line& line = _walls[index];
    line.x1 = fromFixed(vertices[wall.vertex1].x);
    line.y1 = fromFixed(vertices[wall.vertex1].y);
    line.x2 = fromFixed(vertices[wall.vertex2].x);
    line.y2 = fromFixed(vertices[wall.vertex2].y);
    line.nx = fromFixed(wall.normalX);
    line.ny = fromFixed(wall.normalY);
    line.d = fromFixed(wall.distance);
}

void Projectiles::refreshWalls(const std::vector<int>& walls) {
    for (int index : walls)
        makeLine(index);
}

Projectiles::~Projectiles() {}

void Projectiles::fire(int typeIndex, double x, double y, double z, double yaw, double pitch, int shooter,
                       double extraSpeed) {
    if (typeIndex < 0 || typeIndex >= (int)_types.size())
        return;
    const Type& type = _types[typeIndex];
    if (type.kind == kKindBurst) {
        spawnBurst(type, x, y, z, yaw, pitch, extraSpeed);
        return;
    }
    if (type.kind != kKindBeam && type.kind != kKindBolt)
        return;
    Shot shot;
    shot.type = typeIndex;
    shot.shooter = shooter;
    shot.x = shot.originX = x;
    shot.y = shot.originY = y;
    shot.z = shot.originZ = z;
    double speed = type.speed + extraSpeed;
    shot.vx = speed * std::cos(pitch) * std::cos(yaw);
    shot.vy = speed * std::cos(pitch) * std::sin(yaw);
    shot.vz = speed * std::sin(pitch);
    _shots.push_back(shot);
}

void Projectiles::spawnBurst(const Type& type, double x, double y, double z, double yaw, double pitch,
                             double extraSpeed) {
    // 0x4B6C50: sparks spread in a cone, each at two to four times the
    // type's speed (or a quarter of what threw them, for a type without
    // one), part way through their lives already.
    int typeIndex = (int)(&type - _types.data());
    double base = type.speed > 0 ? type.speed : extraSpeed / 4;
    for (int i = 0; i < type.count; i++) {
        double spread = type.spacing * _random.getRandomNumber(0xFFFF) / 65536.0;
        double angle = 2 * kPi * _random.getRandomNumber(0xFFFF) / 65536.0;
        double h = yaw + spread * std::sin(angle), p = pitch + spread * std::cos(angle);
        double speed = type.speed + base * (1 + 2 * _random.getRandomNumber(0xFFFF) / 65536.0);
        Shot shot;
        shot.type = typeIndex;
        shot.shooter = kShooterNone;
        shot.x = x;
        shot.y = y;
        shot.z = z;
        shot.vx = speed * std::cos(p) * std::cos(h);
        shot.vy = speed * std::cos(p) * std::sin(h);
        shot.vz = speed * std::sin(p);
        shot.age = type.lifetime > 1 ? _random.getRandomNumber(type.lifetime / 2 - 1) : 0;
        _shots.push_back(shot);
    }
}

Projectiles::Hit Projectiles::sweep(const Shot& shot, double x0, double y0, double z0, double x1, double y1,
                                    double z1, uint64_t zones, const std::vector<Target>& targets, double playerX,
                                    double playerY, double playerEye, double playerRadius, double& best,
                                    double& normalX, double& normalY) const {
    const Type& type = _types[shot.type];
    double r = type.halfWidth;
    double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    Hit hit;
    best = 2;
    auto take = [&](double t, Hit::Target target, int index, double nx, double ny) {
        if (t < 0 || t > 1 || t >= best)
            return;
        best = t;
        hit.target = target;
        hit.index = index;
        normalX = nx;
        normalY = ny;
    };

    // Walls: crossed from the front, or either way if solid on both sides,
    // within their height give or take the shot's radius.
    const std::vector<LevelFile::Wall>& walls = _level.getWalls();
    for (size_t i = 0; i < walls.size(); i++) {
        const LevelFile::Wall& wall = walls[i];
        if (!(wall.flags & LevelFile::kFlagVisible) || (wall.flags & LevelFile::kWallNoCollision) || !(wall.zones & zones))
            continue;
        const Line& w = _walls[i];
        double s0 = w.nx * x0 + w.ny * y0 + w.d, s1 = w.nx * x1 + w.ny * y1 + w.d;
        bool crosses = (s0 >= 0 && s1 < 0) || ((wall.flags & LevelFile::kWallSolidBothSides) && s0 < 0 && s1 >= 0);
        if (!crosses)
            continue;
        double t = s0 / (s0 - s1);
        double px = x0 + dx * t, py = y0 + dy * t, pz = z0 + dz * t;
        double ex = w.x2 - w.x1, ey = w.y2 - w.y1, length2 = ex * ex + ey * ey;
        double along = length2 > 0 ? ((px - w.x1) * ex + (py - w.y1) * ey) / length2 : -1;
        if (along < 0 || along > 1)
            continue;
        if (pz < fromFixed(wall.bottom) - r || pz > fromFixed(wall.top) + r)
            continue;
        take(t, Hit::kWall, (int)i, w.nx, w.ny);
    }

    // Floors when going down, ceilings when going up.
    if (dz != 0) {
        const std::vector<LevelFile::Sector>& flats = _level.getSectors();
        for (size_t i = 0; i < flats.size(); i++) {
            const LevelFile::Sector& flat = flats[i];
            if (!(flat.flags & LevelFile::kFlagVisible) || !(flat.zones & zones) || flat.vertices.size() < 3)
                continue;
            if (flat.isCeiling() != (dz > 0))
                continue;
            double h = fromFixed(flat.height);
            if ((h - z0) * (h - z1) > 0 || h == z0)
                continue;
            double t = (h - z0) / dz;
            if (insideFlat(_level.getVertices(), flat, x0 + dx * t, y0 + dy * t))
                take(t, Hit::kFlat, (int)i, 0, 0);
        }
    }

    // Billboards, actors and the player as upright cylinders.
    auto cylinder = [&](double cx, double cy, double radius, double bottom, double top, Hit::Target target,
                        int index) {
        double t = circleHit(x0, y0, dx, dy, cx, cy, radius);
        if (t < 0 || t >= best)
            return;
        double z = z0 + dz * t;
        if (z >= bottom && z <= top)
            take(t, target, index, 0, 0);
    };
    const std::vector<LevelFile::Billboard>& billboards = _level.getBillboards();
    for (size_t i = 0; i < billboards.size(); i++) {
        const LevelFile::Billboard& b = billboards[i];
        if (b.hidden || !(b.zones & zones))
            continue;
        double bottom = fromFixed(b.z);
        cylinder(fromFixed(b.x), fromFixed(b.y), fromFixed(b.radius) + r, bottom - r,
                 bottom + fromFixed(b.boxHeight) + r, Hit::kBillboard, (int)i);
    }
    for (const Target& a : targets) {
        if (a.radius <= 0 || (int)a.object == shot.shooter)
            continue;
        cylinder(a.x, a.y, a.radius + r, a.bottom - r, a.top + r, Hit::kActor, a.object);
    }
    if (shot.shooter != kShooterPlayer) {
        cylinder(playerX, playerY, playerRadius, playerEye - kPlayerBelowEye, playerEye + kPlayerAboveEye,
                 Hit::kPlayer, -1);
    }

    if (hit.target != Hit::kNothing) {
        hit.x = x0 + dx * best;
        hit.y = y0 + dy * best;
        hit.z = z0 + dz * best;
        hit.type = shot.type;
    }
    return hit;
}

void Projectiles::finish(Shot& shot, const Hit& hit, double yaw, double pitch, std::vector<Report>& reports) {
    const Type& type = _types[shot.type];
    if (hit.target != Hit::kNothing)
        reports.push_back({ Report::kHit, hit });
    if (type.impact >= 0 && type.impact < (int)_types.size()) {
        // 0x4C5030: the sparks fly back off what was hit.
        if (hit.target == Hit::kWall) {
            const Line& w = _walls[hit.index];
            yaw = 2 * std::atan2(w.y2 - w.y1, w.x2 - w.x1) - yaw;
        } else if (hit.target == Hit::kFlat) {
            pitch = -pitch;
        } else if (hit.target != Hit::kNothing) {
            yaw += kPi;
        }
        double speed = std::sqrt(shot.vx * shot.vx + shot.vy * shot.vy + shot.vz * shot.vz);
        Hit at = hit;
        at.x = shot.x;
        at.y = shot.y;
        at.z = shot.z;
        at.type = type.impact;
        if (_types[type.impact].fireSound >= 0)
            reports.push_back({ Report::kFired, at });
        fire(type.impact, shot.x, shot.y, shot.z, yaw, pitch, kShooterNone, speed);
    }
}

void Projectiles::update(uint32_t ms, uint64_t zones, const std::vector<Target>& targets, double playerX, double playerY,
                         double playerEye, double playerRadius, std::vector<Report>& reports) {
    // New shots (sparks) are appended while this runs; they start next time.
    size_t count = _shots.size();
    for (size_t i = 0; i < count; i++) {
        Shot shot = _shots[i];
        const Type& type = _types[shot.type];
        double speed = std::sqrt(shot.vx * shot.vx + shot.vy * shot.vy + shot.vz * shot.vz);
        double yaw = std::atan2(shot.vy, shot.vx), pitch = speed > 0 ? std::asin(shot.vz / speed) : 0;
        shot.age += ms;

        if (type.kind == kKindBeam && shot.hitAt >= 0) {
            // A spent beam: the trail runs on into the hit point.
            shot.travelled += speed * ms;
            shot.dead = shot.travelled - type.count * type.spacing > shot.hitAt || shot.age > type.lifetime;
            _shots[i] = shot;
            continue;
        }
        if (shot.age > type.lifetime) {
            if (type.kind == kKindBolt) {
                Hit nothing;
                finish(shot, nothing, yaw, pitch, reports);
            }
            shot.dead = true;
            _shots[i] = shot;
            continue;
        }

        double x1 = shot.x + shot.vx * ms, y1 = shot.y + shot.vy * ms, z1 = shot.z + shot.vz * ms;
        double t, nx = 0, ny = 0;
        Hit hit = sweep(shot, shot.x, shot.y, shot.z, x1, y1, z1, zones, targets, playerX, playerY, playerEye,
                        playerRadius, t, nx, ny);
        if (hit.target == Hit::kNothing) {
            shot.x = x1;
            shot.y = y1;
            shot.z = z1;
            shot.travelled += speed * ms;
            _shots[i] = shot;
            continue;
        }

        shot.x = hit.x;
        shot.y = hit.y;
        shot.z = hit.z;
        shot.travelled += speed * ms * t;
        if (type.kind == kKindBurst) {
            // Sparks die on people and bounce off everything else (0x4C5110).
            if (hit.target == Hit::kActor || hit.target == Hit::kPlayer) {
                shot.dead = true;
            } else if (hit.target == Hit::kWall) {
                double dot = shot.vx * nx + shot.vy * ny;
                shot.vx -= 2 * dot * nx;
                shot.vy -= 2 * dot * ny;
            } else if (hit.target == Hit::kFlat) {
                shot.vz = -shot.vz;
            } else {
                shot.vx = -shot.vx;
                shot.vy = -shot.vy;
            }
        } else {
            finish(shot, hit, yaw, pitch, reports);
            if (type.kind == kKindBeam)
                shot.hitAt = shot.travelled;
            else
                shot.dead = true;
        }
        _shots[i] = shot;
    }
    _shots.erase(std::remove_if(_shots.begin(), _shots.end(), [](const Shot& s) { return s.dead; }), _shots.end());
}

const Canvas* Projectiles::getTexture(uint16_t id) {
    std::unique_ptr<LevelFile::Texture>& texture = _textures[id];
    if (!texture) {
        texture.reset(new LevelFile::Texture());
        if (!_level.loadTexture(id, *texture))
            texture->surface.free();
    }
    return texture->surface.w ? &texture->surface : nullptr;
}

void Projectiles::getSprites(std::vector<SpriteDraw>& sprites) {
    for (const Shot& shot : _shots) {
        const Type& type = _types[shot.type];
        uint32_t frame = (uint32_t)(((uint64_t)shot.age * type.animationRate >> 16) + shot.phase) % type.frames;
        const Canvas* surface = getTexture((uint16_t)(type.texture + frame));
        if (!surface)
            continue;
        SpriteDraw sprite;
        sprite.surface = surface;
        sprite.halfWidth = type.halfWidth;
        sprite.height = type.height;
        sprite.mirrored = false;
        sprite.light = LevelFile::LightInfo();
        sprite.fullbright = true;
        sprite.translucent = type.keyed && type.translucent;
        auto add = [&](double x, double y, double z) {
            sprite.x = x;
            sprite.y = y;
            sprite.bottom = z - type.height / 2;
            sprites.push_back(sprite);
        };
        if (type.kind != kKindBeam) {
            add(shot.x, shot.y, shot.z);
            continue;
        }
        // The head and the trail strung out behind it, from the muzzle to
        // the head or the hit point.
        double speed = std::sqrt(shot.vx * shot.vx + shot.vy * shot.vy + shot.vz * shot.vz);
        if (speed == 0)
            continue;
        double ux = shot.vx / speed, uy = shot.vy / speed, uz = shot.vz / speed;
        double limit = shot.hitAt >= 0 ? shot.hitAt : shot.travelled;
        for (int k = 0; k <= type.count; k++) {
            double s = shot.travelled - k * type.spacing;
            if (s < 0)
                break;
            if (s > limit)
                continue;
            add(shot.originX + ux * s, shot.originY + uy * s, shot.originZ + uz * s);
        }
    }
}

} // namespace reluva::stgenerations
