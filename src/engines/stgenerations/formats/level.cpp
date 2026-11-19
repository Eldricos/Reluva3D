// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/level.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <map>

#include "engines/stgenerations/debug.h"
#include "common/bytes.h"
#include "common/serializer.h"

namespace reluva::stgenerations {

const uint32_t LevelFile::kRecordSizes[kRecordSizeCount] = {1024, 8, 292, 152, 140, 276, 196, 64, 24};

namespace {

const uint32_t kHeaderSize = 48;
const uint32_t kEntrySize = 16;
const uint32_t kTextureHeaderSize = 20;

LevelFile::LightInfo readLight(const uint8_t *record) {
    LevelFile::LightInfo light;
    light.group = (int16_t)readLE16(record + 0x64);
    light.brightness = readLE16(record + 0x66);
    light.level = record[0x68];
    return light;
}

} // namespace

void LevelFile::updateWallGeometry(int index) {
    Wall &wall = _walls[index];
    double x1 = _vertices[wall.vertex1].x / 65536.0;
    double y1 = _vertices[wall.vertex1].y / 65536.0;
    double x2 = _vertices[wall.vertex2].x / 65536.0;
    double y2 = _vertices[wall.vertex2].y / 65536.0;
    double dx = x2 - x1;
    double dy = y2 - y1;
    double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0)
        return;
    double nx = dy / length;
    double ny = -dx / length;
    wall.normalX = (int32_t)std::lround(nx * 65536);
    wall.normalY = (int32_t)std::lround(ny * 65536);
    wall.distance = (int32_t)std::lround(-(nx * x2 + ny * y2) * 65536);
}

void LevelFile::LightInfo::sync(Serializer &s) {
    s.syncAsSint16(group);
    s.syncAsUint16(brightness);
    s.syncAsByte(level);
}

void LevelFile::syncState(Serializer &s) {
    s.syncCount((uint32_t)_walls.size());
    for (Wall &wall : _walls) {
        if (s.err())
            return;
        s.syncAsSint16(wall.uScroll);
        s.syncAsSint16(wall.vScroll);
        s.syncAsUint16(wall.texture);
        s.syncAsUint16(wall.frames);
        s.syncAsUint16(wall.speed);
        s.syncAsUint32(wall.flags);
        wall.light.sync(s);
        s.syncAsBool(wall.fullbright);
    }
    s.syncCount((uint32_t)_sectors.size());
    for (Sector &sector : _sectors) {
        if (s.err())
            return;
        s.syncAsSint32(sector.height);
        s.syncAsUint64(sector.activeZones);
        s.syncAsSint16(sector.uScroll);
        s.syncAsSint16(sector.vScroll);
        s.syncVector(sector.zoneBands, [](Serializer &ser, std::pair<int32_t, uint64_t> &band) {
            ser.syncAsSint32(band.first);
            ser.syncAsUint64(band.second);
        });
        s.syncAsUint16(sector.texture);
        s.syncAsUint16(sector.frames);
        s.syncAsUint16(sector.speed);
        s.syncAsUint32(sector.flags);
        sector.light.sync(s);
        s.syncAsBool(sector.fullbright);
    }
    s.syncCount((uint32_t)_billboards.size());
    for (Billboard &billboard : _billboards) {
        if (s.err())
            return;
        s.syncAsSint32(billboard.x);
        s.syncAsSint32(billboard.y);
        s.syncAsSint32(billboard.z);
        s.syncAsUint16(billboard.texture);
        s.syncAsUint16(billboard.frames);
        s.syncAsUint16(billboard.speed);
        s.syncAsUint32(billboard.flags);
        billboard.light.sync(s);
        s.syncAsUint64(billboard.zones);
        s.syncAsBool(billboard.hidden);
        s.syncAsBool(billboard.fullbright);
    }
}

int LevelFile::LightInfo::getLevel(int percent) const {
    // As the original computes it at 0x4BE0E0 (on load) and 0x4B7BA0 (when
    // a script changes a group's brightness).
    if (group == 0)
        return std::min<int>(level, kLightLevels - 1);
    uint32_t scaled = (uint32_t)std::max(percent, 0) * brightness / (3200 * 100);
    return kLightLevels - 1 - (int)std::min<uint32_t>(scaled, kLightLevels - 1);
}

bool LevelFile::load(SeekableReadStream &stream) {
    _directory.clear();
    _data = stream.readAll();
    if (_data.size() < kHeaderSize) {
        log::warning("LevelFile: file too short");
        return false;
    }

    if (readLE32(&_data[0]) != kVersion) {
        log::warning("LevelFile: unsupported version %u", readLE32(&_data[0]));
        return false;
    }
    for (int i = 0; i < kRecordSizeCount; i++) {
        uint32_t size = readLE32(&_data[4 + i * 4]);
        if (size != kRecordSizes[i]) {
            log::warning("LevelFile: record size %d is %u, expected %u", i, size, kRecordSizes[i]);
            return false;
        }
    }

    uint32_t count = readLE32(&_data[40]);
    if ((uint64_t)kHeaderSize + (uint64_t)count * kEntrySize > _data.size()) {
        log::warning("LevelFile: directory of %u entries does not fit", count);
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *p = &_data[kHeaderSize + i * kEntrySize];
        ChunkEntry entry = {readLE32(p),      readLE32(p + 4),  readLE16(p + 8),
                            readLE16(p + 10), readLE16(p + 12), readLE16(p + 14)};
        if ((uint64_t)entry.offset + entry.size > _data.size()) {
            log::warning("LevelFile: chunk %u (type %u) lies outside the file", i, entry.type);
            return false;
        }
        _directory.push_back(entry);
    }
    return loadTables();
}

const LevelFile::ChunkEntry *LevelFile::findChunk(uint16_t type, uint16_t id) const {
    for (const ChunkEntry &entry : _directory) {
        if (entry.type == type && entry.id == id)
            return &entry;
    }
    return nullptr;
}

std::vector<uint8_t> LevelFile::getChunkData(uint16_t type, uint16_t id) const {
    const ChunkEntry *entry = findChunk(type, id);
    if (!entry)
        return {};
    return std::vector<uint8_t>(_data.begin() + entry->offset, _data.begin() + entry->offset + entry->size);
}

bool LevelFile::loadTables() {
    _vertices.clear();
    _walls.clear();
    _sectors.clear();
    _billboards.clear();
    _actors.clear();

    // Each table's record count is in count B; check it against the size.
    auto table = [this](uint16_t type, uint32_t recordSize, const uint8_t *&data, uint32_t &count) {
        const ChunkEntry *entry = findChunk(type, 1);
        data = nullptr;
        count = 0;
        if (!entry)
            return true;
        count = entry->countB;
        if ((uint64_t)count * recordSize > entry->size) {
            log::warning("LevelFile: table %u has %u records of %u bytes but only %u bytes", type, count, recordSize,
                         entry->size);
            return false;
        }
        data = &_data[entry->offset];
        return true;
    };

    const uint8_t *p;
    uint32_t count;

    if (!table(kChunkPalette, 4, p, count))
        return false;
    // Windows RGBQUADs: blue, green, red, unused. The console's bitmaps
    // share these indices, which is how their colours were matched up.
    for (uint32_t i = 0; i < std::min<uint32_t>(count, 256); i++)
        _palette.setColor(i, p[i * 4 + 2], p[i * 4 + 1], p[i * 4]);

    if (!table(kChunkVertices, kRecordSizes[1], p, count))
        return false;
    for (uint32_t i = 0; i < count; i++)
        _vertices.push_back({(int32_t)readLE32(p + i * 8), (int32_t)readLE32(p + i * 8 + 4)});

    if (!table(kChunkWalls, kRecordSizes[3], p, count))
        return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = p + i * kRecordSizes[3];
        Wall wall;
        wall.vertex1 = readLE16(r + 134);
        wall.vertex2 = readLE16(r + 136);
        wall.normalX = (int32_t)readLE32(r + 28);
        wall.normalY = (int32_t)readLE32(r + 36);
        wall.distance = (int32_t)readLE32(r + 40);
        wall.top = (int32_t)readLE32(r + 0x2C);
        wall.bottom = (int32_t)readLE32(r + 0x30);
        wall.uStart = (int32_t)readLE32(r + 0x44);
        wall.uLength = (int32_t)readLE32(r + 0x48);
        wall.uEnd1 = (int32_t)readLE32(r + 0x70);
        wall.uEnd2 = (int32_t)readLE32(r + 0x74);
        wall.vStart = (int32_t)readLE32(r + 0x4C);
        wall.vLength = (int32_t)readLE32(r + 0x50);
        wall.flags = readLE32(r + 0x6C);
        wall.speed = readLE16(r + 0x80);
        wall.frames = readLE16(r + 0x82);
        wall.objectId = readLE16(r + 0x14);
        wall.texture = readLE16(r + 0x96);
        wall.light = readLight(r);
        wall.zones = readLE64(r + 0x3C);
        wall.moverId = (int16_t)readLE16(r + 0x8A);
        wall.moverMode = (int16_t)readLE16(r + 0x8C);
        wall.moverSpeed = readLE16(r + 0x8E);
        wall.moverWait = readLE16(r + 0x90);
        wall.bumpMover = (int16_t)readLE16(r + 0x92);
        wall.bumpMode = (int16_t)readLE16(r + 0x94);
        if (wall.vertex1 >= _vertices.size() || wall.vertex2 >= _vertices.size()) {
            log::warning("LevelFile: wall %u refers to a missing vertex", i);
            return false;
        }
        _walls.push_back(wall);
    }

    if (!table(kChunkSectors, kRecordSizes[2], p, count))
        return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = p + i * kRecordSizes[2];
        Sector sector;
        sector.flags = readLE32(r + 108);
        sector.height = (int32_t)readLE32(r + 0x20);
        sector.moverId = (int16_t)readLE16(r + 0x106);
        sector.moverMode = (int16_t)readLE16(r + 0x108);
        sector.uOffset = (int32_t)readLE32(r + 0x44);
        sector.uScale = (int32_t)readLE32(r + 0x48);
        sector.vOffset = (int32_t)readLE32(r + 0x4C);
        sector.vScale = (int32_t)readLE32(r + 0x50);
        sector.light = readLight(r);
        sector.zones = readLE64(r + 0x3C);
        sector.activeZones = readLE64(r + 0x74);
        sector.frames = readLE16(r + 0x100);
        sector.speed = readLE16(r + 0x102);
        sector.objectId = readLE16(r + 0x14);
        sector.texture = readLE16(r + 0x11C);
        sector.rotation = readLE16(r + 0x11E);
        uint16_t n = readLE16(r + 266);
        if (n > 8) {
            log::warning("LevelFile: sector %u has %u vertices", i, n);
            return false;
        }
        for (uint16_t k = 0; k < n; k++) {
            uint16_t vertex = readLE16(r + 268 + k * 2);
            if (vertex >= _vertices.size()) {
                log::warning("LevelFile: sector %u refers to a missing vertex", i);
                return false;
            }
            sector.vertices.push_back(vertex);
        }
        sector.raw.assign(r, r + kRecordSizes[2]);
        _sectors.push_back(std::move(sector));
    }

    if (!table(kChunkBillboards, kRecordSizes[4], p, count))
        return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = p + i * kRecordSizes[4];
        Billboard billboard;
        billboard.x = (int32_t)readLE32(r + 0x1C);
        billboard.z = (int32_t)readLE32(r + 0x20);
        billboard.y = (int32_t)readLE32(r + 0x24);
        billboard.halfWidth = (int32_t)readLE32(r + 0x34);
        billboard.height = (int32_t)readLE32(r + 0x38);
        billboard.flags = readLE32(r + 0x6C);
        billboard.speed = readLE16(r + 0x84);
        billboard.frames = readLE16(r + 0x86);
        billboard.radius = (int32_t)readLE32(r + 0x54);
        billboard.boxHeight = (int32_t)readLE32(r + 0x58);
        billboard.objectId = readLE16(r + 0x14);
        billboard.texture = readLE16(r + 0x8A);
        billboard.light = readLight(r);
        billboard.zones = readLE64(r + 0x3C);
        _billboards.push_back(billboard);
    }

    if (!table(kChunkActors, kRecordSizes[5], p, count))
        return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = p + i * kRecordSizes[5];
        Actor actor;
        actor.objectId = readLE16(r + 0x14);
        actor.x = (int32_t)readLE32(r + 0x1C);
        actor.z = (int32_t)readLE32(r + 0x20);
        actor.y = (int32_t)readLE32(r + 0x24);
        actor.reach = (int32_t)readLE32(r + 0x28);
        actor.drawOffset = (int32_t)readLE32(r + 0x30);
        actor.scale = (int32_t)readLE32(r + 0x34);
        actor.height = (int32_t)readLE32(r + 0x38);
        actor.radius = (int32_t)readLE32(r + 0x54);
        actor.bodyHeight = (int32_t)readLE32(r + 0x58);
        actor.light = readLight(r);
        actor.flags = readLE32(r + 0x6C);
        actor.outerRadius = (int32_t)readLE32(r + 0x90);
        actor.innerRadius = (int32_t)readLE32(r + 0x94);
        actor.homeRadius = (int32_t)readLE32(r + 0x98);
        actor.minWalk = (int32_t)readLE32(r + 0x9C);
        actor.maxWalk = (int32_t)readLE32(r + 0xA0);
        actor.stepHeight = (int32_t)readLE32(r + 0xAC);
        actor.muzzleX = (int32_t)readLE32(r + 0x84);
        actor.muzzleZ = (int32_t)readLE32(r + 0x88);
        actor.muzzleY = (int32_t)readLE32(r + 0x8C);
        for (int m = 0; m < kAnimationCount; m++) {
            const uint8_t *a = r + 0xC4 + m * 8;
            Animation &anim = actor.animations[m];
            // With the original loader's fixes (0x4BE39B..0x4BE3DB).
            anim.speed = readLE16(a) ? readLE16(a) : 0x40;
            anim.rate = a[2];
            anim.frames = a[3] ? a[3] : 1;
            anim.loopStart = std::min<uint8_t>(a[4], (uint8_t)(anim.frames - 1));
            anim.directions = a[5];
            anim.mirror = a[6];
        }
        actor.spriteSet = readLE16(r + 0xFC);
        actor.minTurn = readLE16(r + 0x100);
        actor.maxTurn = readLE16(r + 0x102);
        actor.turnSpeed = readLE16(r + 0x104);
        actor.heading = readLE16(r + 0x108);
        actor.guardMode = r[0x10D];
        actor.mode = r[0x10E];
        _actors.push_back(actor);
    }
    return true;
}

uint32_t LevelFile::guessSkyHeight(const uint8_t *pixels, uint32_t size) const {
    // The header's height doesn't always fit (halee1: 252 for a 512 x 128
    // sky), so take the width at which neighbouring rows match best. Twice
    // the true width matches nearly as well, two rows side by side, so a
    // width whose half does about as well gives way to the half.
    std::map<uint32_t, double> costs;
    for (uint32_t width = 64; width <= 4096; width++) {
        if (size % width != 0 || size / width < 16)
            continue;
        uint32_t height = size / width;
        double cost = 0;
        int samples = 0;
        for (uint32_t y = 0; y + 1 < height; y += std::max<uint32_t>(1, height / 32)) {
            for (uint32_t x = 0; x < width; x += std::max<uint32_t>(1, width / 64)) {
                uint8_t r1;
                uint8_t g1;
                uint8_t b1;
                uint8_t r2;
                uint8_t g2;
                uint8_t b2;
                _palette.getColor(pixels[y * width + x], r1, g1, b1);
                _palette.getColor(pixels[(y + 1) * width + x], r2, g2, b2);
                cost += std::abs(r1 - r2) + std::abs(g1 - g2) + std::abs(b1 - b2);
                samples++;
            }
        }
        costs[width] = cost / samples;
    }
    uint32_t best = 0;
    for (const auto &c : costs) {
        if (!best || c.second < costs[best])
            best = c.first;
    }
    if (!best)
        return 0;
    for (auto half = costs.find(best / 2); best % 2 == 0 && half != costs.end() && half->second <= costs[best] * 1.1;
         half = costs.find(best / 2))
        best /= 2;
    return size / best;
}

bool LevelFile::loadSky(Canvas &sky) const {
    const uint32_t headerSize = 64;
    const ChunkEntry *entry = nullptr;
    for (const ChunkEntry &e : _directory) {
        if (e.type == kChunkSky) {
            entry = &e;
            break;
        }
    }
    if (!entry || entry->size < headerSize)
        return false;

    const uint8_t *p = &_data[entry->offset];
    uint32_t size = readLE32(p + 16);
    uint32_t height = readLE32(p + 24);
    if (size == 0 || headerSize + (uint64_t)size > entry->size) {
        log::warning("LevelFile: sky has a bad size");
        return false;
    }
    if (height == 0 || size % height != 0 || size / height > 4096)
        height = guessSkyHeight(p + headerSize, size);
    if (height == 0) {
        log::warning("LevelFile: sky has a bad size");
        return false;
    }
    uint32_t width = size / height;
    sky.create((int16_t)width, (int16_t)height, Canvas::kClut8);
    sky.copyRectToSurface(p + headerSize, (int)width, 0, 0, (int)width, (int)height);
    return true;
}

std::vector<uint16_t> LevelFile::getTextureIds() const {
    std::vector<uint16_t> ids;
    for (const ChunkEntry &entry : _directory) {
        if (entry.type == kChunkTexture && entry.size > kTextureHeaderSize)
            ids.push_back(entry.id);
    }
    return ids;
}

bool LevelFile::loadTexture(uint16_t id, Texture &texture) const {
    const ChunkEntry *entry = findChunk(kChunkTexture, id);
    if (!entry || entry->size < kTextureHeaderSize)
        return false;

    // 12 bytes reserved for pointers, then id, width, height and flags;
    // pixels are column-major, each column bottom first, followed by 3
    // bytes of hints per column.
    const uint8_t *p = &_data[entry->offset];
    uint16_t width = readLE16(p + 14);
    uint16_t height = readLE16(p + 16);
    if (width == 0 || height == 0 || kTextureHeaderSize + (uint64_t)width * height > entry->size) {
        log::warning("LevelFile: texture %u has a bad size %ux%u", id, width, height);
        return false;
    }

    texture.id = readLE16(p + 12);
    texture.flags = readLE16(p + 18);
    texture.surface.create((int16_t)width, (int16_t)height, Canvas::kClut8);
    const uint8_t *pixels = p + kTextureHeaderSize;
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++)
            texture.surface.setPixel(x, height - 1 - y, pixels[x * height + y]);
    }
    return true;
}

void drawLevelMap(const LevelFile &level, Canvas &dst) {
    const std::vector<LevelFile::Vertex> &vertices = level.getVertices();
    if (vertices.empty())
        return;

    int64_t minX = INT32_MAX;
    int64_t minY = INT32_MAX;
    int64_t maxX = INT32_MIN;
    int64_t maxY = INT32_MIN;
    for (const LevelFile::Vertex &v : vertices) {
        minX = std::min<int64_t>(minX, v.x);
        maxX = std::max<int64_t>(maxX, v.x);
        minY = std::min<int64_t>(minY, v.y);
        maxY = std::max<int64_t>(maxY, v.y);
    }
    const int margin = 4;
    double spanX = (double)std::max<int64_t>(1, maxX - minX);
    double spanY = (double)std::max<int64_t>(1, maxY - minY);
    // Screen pixels per map unit, preserving aspect. Maps span tens of
    // millions of 16.16 units, so integer maths would round this to 0.
    double scale = std::min((dst.w - 2 * margin) / spanX, (dst.h - 2 * margin) / spanY);
    double offsetX = (dst.w - spanX * scale) / 2;
    double offsetY = (dst.h - spanY * scale) / 2;
    auto project = [&](const LevelFile::Vertex &v) {
        // Map y grows upwards; screen y grows downwards.
        return Point((int16_t)(offsetX + (v.x - minX) * scale + 0.5), (int16_t)(offsetY + (maxY - v.y) * scale + 0.5));
    };

    const Palette &palette = level.getPalette();
    uint8_t outline = palette.findBestColor(0, 0, 0);
    uint8_t wallColor = palette.findBestColor(120, 220, 255);

    // Sectors in varied colours so neighbours tend to differ.
    const std::vector<LevelFile::Sector> &sectors = level.getSectors();
    for (size_t i = 0; i < sectors.size(); i++) {
        std::vector<Point> points;
        for (uint16_t index : sectors[i].vertices)
            points.push_back(project(vertices[index]));
        uint8_t r = (uint8_t)(40 + (i * 53) % 100);
        uint8_t g = (uint8_t)(60 + (i * 97) % 120);
        uint8_t b = (uint8_t)(120 + (i * 31) % 120);
        dst.fillPolygon(points.data(), (int)points.size(), palette.findBestColor(r, g, b));
        for (size_t k = 0; k < points.size(); k++) {
            const Point &a = points[k];
            const Point &b2 = points[(k + 1) % points.size()];
            dst.drawLine(a.x, a.y, b2.x, b2.y, outline);
        }
    }

    for (const LevelFile::Wall &wall : level.getWalls()) {
        Point a = project(vertices[wall.vertex1]);
        Point b = project(vertices[wall.vertex2]);
        dst.drawLine(a.x, a.y, b.x, b.y, wallColor);
    }
}

} // namespace reluva::stgenerations
