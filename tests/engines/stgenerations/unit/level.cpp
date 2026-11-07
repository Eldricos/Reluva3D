// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tests/engines/stgenerations/support.h"

#include <cmath>
#include <cstring>

#include "common/bytes.h"
#include "common/serializer.h"
#include "common/stream.h"
#include "engines/stgenerations/game/actors.h"
#include "engines/stgenerations/formats/level.h"
#include "engines/stgenerations/game/mission.h"
#include "engines/stgenerations/game/movers.h"
#include "engines/stgenerations/formats/opcodes.h"
#include "engines/stgenerations/game/render3d.h"
#include "engines/stgenerations/game/walker.h"

using namespace reluva;
using namespace reluva::stgenerations;

namespace {

struct Chunk {
    uint16_t type;
    uint16_t id;
    uint16_t count;
    std::vector<uint8_t> data;
};

// Build an LV2 file from chunks, laying them out after the directory.
std::vector<uint8_t> makeLevelFile(const std::vector<Chunk>& chunks) {
    const uint32_t headerSize = 48, entrySize = 16;
    std::vector<uint8_t> file(headerSize + chunks.size() * entrySize);
    writeLE32(&file[0], LevelFile::kVersion);
    for (int i = 0; i < LevelFile::kRecordSizeCount; i++)
        writeLE32(&file[4 + i * 4], LevelFile::kRecordSizes[i]);
    writeLE32(&file[40], (uint32_t)chunks.size());
    writeLE32(&file[44], (uint32_t)chunks.size() * entrySize);
    for (size_t i = 0; i < chunks.size(); i++) {
        uint8_t* entry = &file[headerSize + i * entrySize];
        writeLE32(entry, (uint32_t)chunks[i].data.size());
        writeLE32(entry + 4, (uint32_t)file.size());
        writeLE16(entry + 8, chunks[i].type);
        writeLE16(entry + 10, chunks[i].id);
        writeLE16(entry + 12, chunks[i].count);
        writeLE16(entry + 14, chunks[i].count);
        file.insert(file.end(), chunks[i].data.begin(), chunks[i].data.end());
    }
    return file;
}

std::vector<Chunk> makeSquareLevel() {
    std::vector<Chunk> chunks;

    Chunk palette = { LevelFile::kChunkPalette, 1, 256, std::vector<uint8_t>(1024) };
    palette.data[4 * 7] = 200; // blue: entries are B, G, R, 0
    chunks.push_back(palette);

    // A 2x2 unit square, in 16.16.
    const int32_t corners[4][2] = { { 0, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } };
    Chunk vertices = { LevelFile::kChunkVertices, 1, 4, std::vector<uint8_t>(32) };
    for (int i = 0; i < 4; i++) {
        writeLE32(&vertices.data[i * 8], (uint32_t)(corners[i][0] << 16));
        writeLE32(&vertices.data[i * 8 + 4], (uint32_t)(corners[i][1] << 16));
    }
    chunks.push_back(vertices);

    Chunk sectors = { LevelFile::kChunkSectors, 1, 1, std::vector<uint8_t>(292) };
    writeLE32(&sectors.data[108], 0x12345678);
    writeLE16(&sectors.data[266], 4);
    for (int i = 0; i < 4; i++)
        writeLE16(&sectors.data[268 + i * 2], (uint16_t)i);
    chunks.push_back(sectors);

    Chunk walls = { LevelFile::kChunkWalls, 1, 1, std::vector<uint8_t>(152) };
    // x = 2 from (2, 2) to (2, 0), facing into the square: the normal is
    // on the right of vertex 1 to vertex 2, as in the game's levels.
    writeLE32(&walls.data[28], (uint32_t)-0x10000);
    writeLE32(&walls.data[36], 0);
    writeLE32(&walls.data[40], 0x20000);
    writeLE16(&walls.data[134], 2);
    writeLE16(&walls.data[136], 1);
    chunks.push_back(walls);

    // A 2x3 texture, stored column by column.
    Chunk texture = { LevelFile::kChunkTexture, 9, 0, std::vector<uint8_t>(20 + 6 + 6) };
    writeLE16(&texture.data[12], 9);
    writeLE16(&texture.data[14], 2);
    writeLE16(&texture.data[16], 3);
    writeLE16(&texture.data[18], 0x40);
    for (int i = 0; i < 6; i++)
        texture.data[20 + i] = (uint8_t)(10 + i);
    chunks.push_back(texture);
    return chunks;
}

bool loadLevel(LevelFile& level, const std::vector<uint8_t>& bytes) {
    MemoryReadStream stream(bytes);
    return level.load(stream);
}

} // namespace

static void level_tables() {
    LevelFile level;
    CHECK(loadLevel(level, makeLevelFile(makeSquareLevel())));

    CHECK_EQUAL(level.getDirectory().size(), (size_t)5);
    uint8_t r, g, b;
    level.getPalette().getColor(7, r, g, b);
    CHECK_EQUAL(b, 200);
    CHECK_EQUAL(r, 0);

    CHECK_EQUAL(level.getVertices().size(), (size_t)4);
    CHECK_EQUAL(level.getVertices()[2].x, 2 << 16);
    CHECK_EQUAL(level.getVertices()[3].y, 2 << 16);

    CHECK_EQUAL(level.getSectors().size(), (size_t)1);
    const LevelFile::Sector& sector = level.getSectors()[0];
    CHECK_EQUAL(sector.flags, 0x12345678u);
    CHECK_EQUAL(sector.vertices.size(), (size_t)4);
    CHECK_EQUAL(sector.vertices[3], 3);
    CHECK_EQUAL(sector.raw.size(), (size_t)292);

    CHECK_EQUAL(level.getWalls().size(), (size_t)1);
    const LevelFile::Wall& wall = level.getWalls()[0];
    CHECK_EQUAL(wall.vertex1, 2);
    CHECK_EQUAL(wall.vertex2, 1);
    CHECK_EQUAL(wall.normalX, -0x10000);
    CHECK_EQUAL(wall.distance, 0x20000);

    CHECK(level.getChunkData(LevelFile::kChunkWalls, 1).size() == 152);
    CHECK(level.getChunkData(LevelFile::kChunkWalls, 2).empty());
}

static void level_texture_is_column_major() {
    LevelFile level;
    CHECK(loadLevel(level, makeLevelFile(makeSquareLevel())));

    std::vector<uint16_t> ids = level.getTextureIds();
    CHECK_EQUAL(ids.size(), (size_t)1);
    CHECK_EQUAL(ids[0], 9);

    LevelFile::Texture texture;
    CHECK(level.loadTexture(9, texture));
    CHECK_EQUAL(texture.flags, 0x40);
    CHECK_EQUAL(texture.surface.w, 2);
    CHECK_EQUAL(texture.surface.h, 3);
    // Each column is stored bottom first.
    CHECK_EQUAL(texture.surface.getPixel(0, 2), 10u);
    CHECK_EQUAL(texture.surface.getPixel(0, 0), 12u);
    CHECK_EQUAL(texture.surface.getPixel(1, 2), 13u);
    CHECK(!level.loadTexture(8, texture));
}

static void level_rejects_bad_files() {
    LevelFile level;

    std::vector<uint8_t> bytes = makeLevelFile(makeSquareLevel());
    writeLE32(&bytes[8], 9); // record size 1
    CHECK(!loadLevel(level, bytes));

    bytes = makeLevelFile(makeSquareLevel());
    writeLE32(&bytes[48 + 4], (uint32_t)bytes.size()); // chunk 0 offset
    CHECK(!loadLevel(level, bytes));

    std::vector<Chunk> chunks = makeSquareLevel();
    writeLE16(&chunks[2].data[268], 4); // sector vertex out of range
    CHECK(!loadLevel(level, makeLevelFile(chunks)));

    CHECK(!loadLevel(level, std::vector<uint8_t>(20)));
}

static void level_map_draws_sectors() {
    LevelFile level;
    CHECK(loadLevel(level, makeLevelFile(makeSquareLevel())));

    Canvas map;
    map.create(64, 32, Canvas::kClut8);
    map.fillRect(Rect(0, 0, map.w, map.h), 0xEE);
    drawLevelMap(level, map);
    // The square fills the middle of the surface; the sides stay clear.
    CHECK(map.getPixel(32, 16) != 0xEEu);
    CHECK_EQUAL(map.getPixel(2, 16), 0xEEu);
    CHECK_EQUAL(map.getPixel(61, 16), 0xEEu);
}

namespace {

// The square level made renderable: a visible floor at height 0 and a wall
// one unit high along x = 2, both using the 2x3 texture, and a 4x2 sky.
std::vector<Chunk> makeRenderableLevel() {
    std::vector<Chunk> chunks = makeSquareLevel();
    uint8_t* sector = chunks[2].data.data();
    writeLE32(sector + 108, LevelFile::kFlagVisible);
    writeLE32(sector + 0x20, 0);
    writeLE32(sector + 0x48, 0x10000);
    writeLE32(sector + 0x50, 0x10000);
    writeLE16(sector + 0x11C, 9);
    writeLE32(sector + 0x3C, 1); // zone 0
    writeLE32(sector + 0x74, 1); // only zone 0 in play

    uint8_t* wall = chunks[3].data.data();
    writeLE32(wall + 0x2C, 0x10000);
    writeLE32(wall + 0x30, 0);
    writeLE32(wall + 0x48, 128 << 16); // the whole texture once
    writeLE32(wall + 0x50, 128 << 16);
    writeLE32(wall + 0x6C, LevelFile::kFlagVisible);
    writeLE16(wall + 0x96, 9);
    writeLE32(wall + 0x3C, 1);

    Chunk billboards = { LevelFile::kChunkBillboards, 1, 1, std::vector<uint8_t>(140) };
    writeLE32(&billboards.data[0x1C], 0x1CCCD);           // x 1.8
    writeLE32(&billboards.data[0x20], 0x4000);            // z 0.25
    writeLE32(&billboards.data[0x24], (uint32_t)-0x4CCD);   // y -0.3
    writeLE32(&billboards.data[0x34], 0x2000);
    writeLE32(&billboards.data[0x38], 0x4000);
    writeLE16(&billboards.data[0x86], 1);
    writeLE16(&billboards.data[0x8A], 9);
    writeLE32(&billboards.data[0x3C], 1);
    chunks.push_back(billboards);

    Chunk sky = { LevelFile::kChunkSky, 0, 1, std::vector<uint8_t>(64 + 8, 0x77) };
    writeLE32(&sky.data[16], 8);
    writeLE32(&sky.data[24], 2);
    chunks.push_back(sky);
    return chunks;
}

} // namespace

static void level_surfaces_billboards_and_sky() {
    LevelFile level;
    CHECK(loadLevel(level, makeLevelFile(makeRenderableLevel())));

    const LevelFile::Sector& flat = level.getSectors()[0];
    CHECK(!flat.isCeiling());
    CHECK_EQUAL(flat.texture, 9);
    CHECK_EQUAL(flat.uScale, 0x10000);

    const LevelFile::Wall& wall = level.getWalls()[0];
    CHECK_EQUAL(wall.top, 0x10000);
    CHECK_EQUAL(wall.bottom, 0);
    CHECK_EQUAL(wall.vLength, 128 << 16);
    CHECK_EQUAL(wall.texture, 9);

    CHECK_EQUAL(level.getBillboards().size(), (size_t)1);
    CHECK_EQUAL(level.getBillboards()[0].y, -0x4CCD);
    CHECK_EQUAL(level.getBillboards()[0].texture, 9);

    Canvas sky;
    CHECK(level.loadSky(sky));
    CHECK_EQUAL(sky.w, 4);
    CHECK_EQUAL(sky.h, 2);
    CHECK_EQUAL(sky.getPixel(3, 1), 0x77u);
}

static void level_sky_size_when_the_header_does_not_fit() {
    // A 128 x 32 sky whose header claims 252 rows, as in halee1.
    std::vector<Chunk> chunks = makeRenderableLevel();
    for (int i = 0; i < 256; i++) {
        chunks[0].data[i * 4] = (uint8_t)i;
        chunks[0].data[i * 4 + 1] = (uint8_t)i;
        chunks[0].data[i * 4 + 2] = (uint8_t)i;
    }
    Chunk& sky = chunks.back();
    sky.data.assign(64 + 128 * 32, 0);
    writeLE32(&sky.data[16], 128 * 32);
    writeLE32(&sky.data[24], 252);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 128; x++)
            sky.data[64 + y * 128 + x] = (uint8_t)((x + 3 * y) % 128);
    }
    LevelFile level;
    CHECK(loadLevel(level, makeLevelFile(chunks)));
    Canvas image;
    CHECK(level.loadSky(image));
    CHECK_EQUAL(image.w, 128);
    CHECK_EQUAL(image.h, 32);
}

static void level_renderer_draws_floor_wall_and_sky() {
    LevelFile level;
    CHECK(loadLevel(level, makeLevelFile(makeRenderableLevel())));
    LevelRenderer renderer(level);

    // Standing in the square, looking along +x at the wall.
    Camera camera;
    camera.x = 0.2;
    camera.y = 1;
    camera.z = 0.25;
    Canvas view;
    view.create(64, 32, Canvas::kClut8);
    renderer.render(camera, view, 0);

    auto isTexel = [](uint32_t c) { return c >= 10 && c <= 15; };
    CHECK(isTexel(view.getPixel(32, 31))); // floor
    CHECK(isTexel(view.getPixel(28, 17))); // wall
    // The billboard stands beyond the end of the wall, against the sky.
    CHECK(isTexel(view.getPixel(57, 13)));
    CHECK_EQUAL(view.getPixel(57, 4), 0x77u);
    CHECK_EQUAL(view.getPixel(32, 0), 0x77u); // sky above the wall
}

static void level_renderer_wall_sides_and_mirroring() {
    auto render = [](uint32_t wallFlags, double x, double yaw, int px, int py) {
        std::vector<Chunk> chunks = makeRenderableLevel();
        writeLE32(&chunks[3].data[0x6C], LevelFile::kFlagVisible | wallFlags);
        LevelFile level;
        loadLevel(level, makeLevelFile(chunks));
        LevelRenderer renderer(level);
        Camera camera;
        camera.x = x;
        camera.y = 1;
        camera.z = 0.25;
        camera.yaw = yaw;
        Canvas view;
        view.create(64, 32, Canvas::kClut8);
        renderer.render(camera, view, 0);
        return view.getPixel(px, py);
    };
    const double kPi = 3.14159265358979323846;
    auto inColumn = [](uint32_t c, int column) { return c >= 10u + column * 3 && c <= 12u + column * 3; };

    // From the front, u runs from vertex 1 (on the left) to vertex 2;
    // flag 0x20 turns it round.
    CHECK(inColumn(render(0, 0.2, 0, 20, 17), 0));
    CHECK(inColumn(render(0, 0.2, 0, 44, 17), 1));
    CHECK(inColumn(render(LevelFile::kWallMirror, 0.2, 0, 20, 17), 1));
    CHECK(inColumn(render(LevelFile::kWallMirror, 0.2, 0, 44, 17), 0));

    // From behind only two-sided walls show; the sky is seen through others.
    CHECK_EQUAL(render(0, 3.8, kPi, 32, 12), 0x77u);
    CHECK(render(LevelFile::kWallTwoSided, 3.8, kPi, 32, 12) != 0x77u);
}

static void level_light_levels() {
    LevelFile::LightInfo fixed = { 0, 0, 3 };
    CHECK_EQUAL(fixed.getLevel(100), 3);
    CHECK_EQUAL(fixed.getLevel(0), 3);

    // 3200 brightness per level, scaled by the group's percentage.
    LevelFile::LightInfo grouped = { 1, 7680, 0 };
    CHECK_EQUAL(grouped.getLevel(100), 5);
    CHECK_EQUAL(grouped.getLevel(50), 6);
    CHECK_EQUAL(grouped.getLevel(0), 7);
    grouped.brightness = 25600;
    CHECK_EQUAL(grouped.getLevel(100), 0);
}

static void level_shade_tables() {
    Palette palette;
    palette.setColor(0, 0, 0, 0);
    palette.setColor(1, 128, 128, 128);
    for (int i = 2; i < 256; i++)
        palette.setColor(i, 255, 255, 255);

    uint8_t tables[LevelFile::kLightLevels][256];
    LevelRenderer::buildShadeTables(palette, 0, 0, 0, tables);
    CHECK_EQUAL(tables[0][2], 2);
    CHECK_EQUAL(tables[1][2], 2); // 224: still nearest to white
    CHECK_EQUAL(tables[4][2], 1); // 127: grey
    CHECK_EQUAL(tables[7][2], 0); // 31: black
    CHECK_EQUAL(tables[7][0], 0);
}

static void level_blend_table() {
    Palette palette;
    for (int i = 0; i < 256; i++)
        palette.setColor(i, 0, 255, 0);
    palette.setColor(1, 0, 0, 200);
    palette.setColor(2, 100, 0, 100);
    palette.setColor(3, 200, 0, 0);
    palette.setColor(LevelRenderer::kBlendFirst, 200, 0, 0);

    uint8_t table[LevelRenderer::kBlendColors][256];
    LevelRenderer::buildBlendTable(palette, table);
    CHECK_EQUAL(table[0][1], 2); // red over blue: purple
    CHECK_EQUAL(table[0][3], 3); // red over red: the first red
}

static void level_renderer_lighting() {
    // Put the wall in light group 1 at full brightness. Switching the group
    // off gives light level 7, and with the test palette (black apart from
    // one red entry) every texel then shades to index 0.
    std::vector<Chunk> chunks = makeRenderableLevel();
    writeLE16(&chunks[3].data[0x64], 1);
    writeLE16(&chunks[3].data[0x66], 25600);
    LevelFile level;
    CHECK(loadLevel(level, makeLevelFile(chunks)));
    CHECK_EQUAL(level.getWalls()[0].light.group, 1);

    LevelRenderer renderer(level);
    Camera camera;
    camera.x = 0.2;
    camera.y = 1;
    camera.z = 0.25;
    Canvas view;
    view.create(64, 32, Canvas::kClut8);

    renderer.render(camera, view, 0);
    CHECK(view.getPixel(28, 17) >= 10);

    renderer.setGroupBrightness(1, 0);
    renderer.render(camera, view, 0);
    CHECK_EQUAL(view.getPixel(28, 17), 0u);
    CHECK(view.getPixel(32, 31) >= 10); // the floor has no group and stays lit

    renderer.setLightingEnabled(false);
    renderer.render(camera, view, 0);
    CHECK(view.getPixel(28, 17) >= 10);
}

static void level_renderer_draws_only_visible_zones() {
    LevelFile level;
    CHECK(loadLevel(level, makeLevelFile(makeRenderableLevel())));
    LevelRenderer renderer(level);
    Camera camera;
    camera.x = 0.2;
    camera.y = 1;
    camera.z = 0.25;
    Canvas view;
    view.create(64, 32, Canvas::kClut8);

    renderer.setVisibleZones(2); // everything in the level is in zone 0
    renderer.render(camera, view, 0);
    CHECK_EQUAL(view.getPixel(28, 17), 0x77u);
    CHECK_EQUAL(view.getPixel(32, 31), 0x77u);
}

namespace {

// Builds levels of rectangular floors and straight walls for the walker.
struct LevelBuilder {
    std::vector<std::pair<int32_t, int32_t>> vertices;
    std::vector<std::vector<uint8_t>> flats, walls, actors, billboards;

    // A 2 x 4 billboard standing at (x, y, z).
    void addBillboard(uint16_t id, double x, double y, double z, uint32_t flags, uint32_t zones) {
        std::vector<uint8_t> r(140);
        writeLE16(&r[0x14], id);
        writeLE32(&r[0x1C], (uint32_t)(int32_t)(x * 65536));
        writeLE32(&r[0x20], (uint32_t)(int32_t)(z * 65536));
        writeLE32(&r[0x24], (uint32_t)(int32_t)(y * 65536));
        writeLE32(&r[0x34], 1 << 16);
        writeLE32(&r[0x38], 4 << 16);
        writeLE32(&r[0x3C], zones);
        writeLE32(&r[0x6C], flags);
        billboards.push_back(r);
    }

    // An actor of sprite set 1 standing at (x, y, z). Animation 0 (stand)
    // has one frame in eight mirrored directions; animation 6 (walk) four
    // frames in one direction at 1 unit per frame.
    std::vector<uint8_t>& addActor(uint16_t id, double x, double y, double z, int mode, uint16_t heading) {
        std::vector<uint8_t> r(276);
        writeLE16(&r[0x14], id);
        writeLE32(&r[0x1C], (uint32_t)(int32_t)(x * 65536));
        writeLE32(&r[0x20], (uint32_t)(int32_t)(z * 65536));
        writeLE32(&r[0x24], (uint32_t)(int32_t)(y * 65536));
        writeLE32(&r[0x28], 2 << 16); // reach
        writeLE32(&r[0x34], 1 << 16); // scale
        writeLE32(&r[0x38], 6 << 16); // height
        writeLE32(&r[0x54], 1 << 16); // radius
        writeLE32(&r[0x58], 6 << 16); // body height
        writeLE32(&r[0xAC], 2 << 16); // step height
        const uint8_t stand[8] = { 0, 0, 0, 1, 0, 8, 1, 0 };
        memcpy(&r[0xC4], stand, 8);
        const uint8_t walk[8] = { 0, 1, 64, 4, 0, 1, 0, 1 };
        memcpy(&r[0xC4 + 6 * 8], walk, 8);
        writeLE16(&r[0xFC], 1);
        writeLE16(&r[0x104], 0xFFFF); // turn speed
        writeLE16(&r[0x108], heading);
        r[0x10E] = (uint8_t)mode;
        actors.push_back(r);
        return actors.back();
    }

    uint16_t addVertex(double x, double y) {
        vertices.push_back({ (int32_t)(x * 65536), (int32_t)(y * 65536) });
        return (uint16_t)(vertices.size() - 1);
    }

    void addFloor(double x0, double y0, double x1, double y1, double height, uint32_t zone = 1, uint32_t active = 1,
                  uint32_t flags = 0) {
        std::vector<uint8_t> r(292);
        writeLE32(&r[108], LevelFile::kFlagVisible | flags);
        writeLE32(&r[0x20], (uint32_t)(int32_t)(height * 65536));
        writeLE32(&r[0x3C], zone);
        writeLE32(&r[0x74], active);
        writeLE16(&r[266], 4);
        writeLE16(&r[268], addVertex(x0, y0));
        writeLE16(&r[270], addVertex(x1, y0));
        writeLE16(&r[272], addVertex(x1, y1));
        writeLE16(&r[274], addVertex(x0, y1));
        flats.push_back(r);
    }

    // A wall from (x0, y0) to (x1, y1), solid on its left as seen walking along it.
    void addWall(double x0, double y0, double x1, double y1, double bottom, double top, uint32_t flags = 0,
                 uint32_t zone = 1, uint16_t objectId = 0) {
        std::vector<uint8_t> r(152);
        writeLE16(&r[0x14], objectId);
        double dx = x1 - x0, dy = y1 - y0, length = std::sqrt(dx * dx + dy * dy);
        double nx = -dy / length, ny = dx / length, d = -(nx * x0 + ny * y0);
        writeLE32(&r[0x1C], (uint32_t)(int32_t)(nx * 65536));
        writeLE32(&r[0x24], (uint32_t)(int32_t)(ny * 65536));
        writeLE32(&r[0x28], (uint32_t)(int32_t)(d * 65536));
        writeLE32(&r[0x2C], (uint32_t)(int32_t)(top * 65536));
        writeLE32(&r[0x30], (uint32_t)(int32_t)(bottom * 65536));
        writeLE32(&r[0x3C], zone);
        writeLE32(&r[0x6C], LevelFile::kFlagVisible | flags);
        writeLE16(&r[0x86], addVertex(x0, y0));
        writeLE16(&r[0x88], addVertex(x1, y1));
        walls.push_back(r);
    }

    std::vector<uint8_t> build() const {
        std::vector<Chunk> chunks;
        chunks.push_back({ LevelFile::kChunkPalette, 1, 256, std::vector<uint8_t>(1024) });
        Chunk v = { LevelFile::kChunkVertices, 1, (uint16_t)vertices.size(), {} };
        for (const auto& p : vertices) {
            uint8_t b[8];
            writeLE32(b, (uint32_t)p.first);
            writeLE32(b + 4, (uint32_t)p.second);
            v.data.insert(v.data.end(), b, b + 8);
        }
        chunks.push_back(v);
        Chunk f = { LevelFile::kChunkSectors, 1, (uint16_t)flats.size(), {} };
        for (const auto& r : flats)
            f.data.insert(f.data.end(), r.begin(), r.end());
        chunks.push_back(f);
        Chunk w = { LevelFile::kChunkWalls, 1, (uint16_t)walls.size(), {} };
        for (const auto& r : walls)
            w.data.insert(w.data.end(), r.begin(), r.end());
        chunks.push_back(w);
        Chunk b = { LevelFile::kChunkBillboards, 1, (uint16_t)billboards.size(), {} };
        for (const auto& r : billboards)
            b.data.insert(b.data.end(), r.begin(), r.end());
        chunks.push_back(b);
        Chunk a = { LevelFile::kChunkActors, 1, (uint16_t)actors.size(), {} };
        for (const auto& r : actors)
            a.data.insert(a.data.end(), r.begin(), r.end());
        chunks.push_back(a);
        return makeLevelFile(chunks);
    }
};

// A 20 x 20 floor at height 0, with a wall along x = 10 facing -x.
LevelBuilder makeRoom(double wallTop = 12, uint32_t wallFlags = 0, uint32_t wallZone = 1) {
    LevelBuilder builder;
    builder.addFloor(0, 0, 20, 20, 0);
    builder.addWall(10, 0, 10, 20, 0, wallTop, wallFlags, wallZone);
    return builder;
}

} // namespace

static void walker_stops_at_walls() {
    LevelFile level;
    CHECK(loadLevel(level, makeRoom().build()));
    Walker walker(level);
    walker.place(5, 10, 0);
    CHECK_EQUAL(walker.getFloor(), 0);
    CHECK(walker.getFeet() == 0);
    CHECK(walker.getEye() == Walker::kEyeHeight);

    walker.move(10, 0, 0.1);
    CHECK(std::fabs(walker.getX() - (10 - Walker::kRadius)) < 0.05);
    CHECK(std::fabs(walker.getY() - 10) < 1e-9);

    // Pushing into the wall at an angle slides along it.
    walker.move(5, 4, 0.1);
    CHECK(walker.getX() < 10 - Walker::kRadius + 0.01);
    CHECK(std::fabs(walker.getY() - 14) < 0.05);

    // Walking away is never blocked.
    walker.move(-3, 0, 0.1);
    CHECK(std::fabs(walker.getX() - (7 - Walker::kRadius)) < 0.05);

    walker.setCollisionEnabled(false);
    walker.move(10, 0, 0.1);
    CHECK(walker.getX() > 15);
}

static void walker_wall_sides_heights_and_zones() {
    // From behind, a one-sided wall lets the player through...
    {
        LevelFile level;
        CHECK(loadLevel(level, makeRoom().build()));
        Walker walker(level);
        walker.place(15, 10, 0);
        walker.move(-10, 0, 0.1);
        CHECK(std::fabs(walker.getX() - 5) < 1e-9);
    }
    // ...but one solid on both sides does not.
    {
        LevelFile level;
        CHECK(loadLevel(level, makeRoom(12, LevelFile::kWallSolidBothSides).build()));
        Walker walker(level);
        walker.place(15, 10, 0);
        walker.move(-10, 0, 0.1);
        CHECK(std::fabs(walker.getX() - (10 + Walker::kRadius)) < 0.05);
    }
    // A wall lower than the step height is stepped over.
    {
        LevelFile level;
        CHECK(loadLevel(level, makeRoom(1.5).build()));
        Walker walker(level);
        walker.place(5, 10, 0);
        walker.move(10, 0, 0.1);
        CHECK(std::fabs(walker.getX() - 15) < 1e-9);
    }
    // A wall in a zone that isn't in play is ignored.
    {
        LevelFile level;
        CHECK(loadLevel(level, makeRoom(12, 0, 2).build()));
        Walker walker(level);
        walker.place(5, 10, 0);
        CHECK(walker.getActiveZones() == 1);
        walker.move(10, 0, 0.1);
        CHECK(std::fabs(walker.getX() - 15) < 1e-9);
    }
}

static void walker_steps_up_and_falls() {
    LevelBuilder builder;
    builder.addFloor(0, 0, 10, 10, 0);
    builder.addFloor(10, 0, 20, 10, 1.5);  // a step up
    builder.addFloor(20, 0, 30, 10, 5);    // too high to step onto
    builder.addFloor(0, 10, 10, 20, -10);  // a drop
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    Walker walker(level);

    walker.place(5, 5, 0);
    walker.move(10, 0, 0.1);
    CHECK_EQUAL(walker.getFloor(), 1);
    CHECK(walker.getFeet() == 1.5);

    // The higher floor can't be stepped onto. Under it there is no floor
    // at all, so the player keeps their height and every zone opens up.
    walker.move(10, 0, 0.1);
    CHECK_EQUAL(walker.getFloor(), -1);
    CHECK(walker.getFeet() == 1.5);
    CHECK(walker.getActiveZones() == Walker::kAllZones);

    walker.place(5, 5, 0);
    walker.move(0, 10, 0.1);
    CHECK_EQUAL(walker.getFloor(), 3);
    CHECK(walker.getFeet() < 0);
    CHECK(walker.getFeet() > -10);
    for (int i = 0; i < 50; i++)
        walker.move(0, 0, 0.05);
    CHECK(walker.getFeet() == -10);
}

static void walker_steps_down_jumps_and_passes_through() {
    LevelBuilder builder;
    builder.addFloor(0, 0, 10, 10, 0);
    builder.addFloor(10, 0, 20, 10, -0.25);  // small enough to step down
    builder.addFloor(20, 0, 30, 10, -1);     // a drop
    builder.addFloor(0, 10, 10, 20, -5);
    builder.addFloor(0, 10, 10, 20, -1, 1, 1, LevelFile::kFlagPassThrough);
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    Walker walker(level);

    // A quarter unit down is one step; more is a fall.
    walker.place(5, 5, 0);
    walker.move(10, 0, 0.05);
    CHECK(walker.getFeet() == -0.25);
    CHECK(!walker.isAirborne());
    walker.move(10, 0, 0.05);
    CHECK(walker.isAirborne());
    CHECK(walker.getFeet() > -1);

    // A jump goes up, then lands where it started.
    walker.place(5, 5, 0);
    walker.jump(13.9);
    double top = 0;
    for (int i = 0; i < 40 && walker.isAirborne(); i++) {
        walker.move(0, 0, 0.033);
        top = std::max(top, walker.getFeet());
    }
    CHECK(top > 1);
    CHECK(walker.getFeet() == 0);
    CHECK(walker.takeLanding() > 0);

    // Nothing rests on a pass-through floor.
    walker.place(5, 15, 0);
    for (int i = 0; i < 50; i++)
        walker.move(0, 0, 0.05);
    CHECK_EQUAL(walker.getFloor(), 3);
    CHECK(walker.getFeet() == -5);
}

namespace {

// Wrap program words in an SGEN file with empty sections.
std::vector<uint8_t> makeScriptFile(const std::vector<uint16_t>& program) {
    std::vector<uint8_t> file(40);
    writeBE32(&file[0], tagBE('S', 'G', 'E', 'N'));
    writeLE16(&file[4], 22);
    writeLE16(&file[6], 2);
    writeLE32(&file[12], (uint32_t)program.size() * 2);
    for (uint16_t word : program) {
        file.push_back((uint8_t)word);
        file.push_back((uint8_t)(word >> 8));
    }
    writeLE32(&file[8], (uint32_t)file.size() - 12);
    return file;
}

} // namespace

static void mission_runs_level_scripts() {
    const uint16_t P = OP_PUSH;
    std::vector<uint16_t> program = {
        // A logic object: subscribes to broadcasts and starts a 100 ms timer
        // on entering state 0, counts timer ticks in global 0, stores signals
        // in global 1, and puts the player at (5, 10) facing +x on the start
        // broadcast.
        1, 0xE001,
        0x8000,
        kEventEnterState, 0xFFFF, OP_PUSH_SELF, 0x31, P, 100, OP_PUSH_SELF, 0x32, OP_END,
        Mission::kEventStart, 0xFFFF, OP_PUSH_BLOCK, 90, 0, 0, 0, 10, 0, 5, 0x75, OP_END,
        Mission::kEventTimer, 0xFFFF, P, 0, OP_GET_GLOBAL, OP_INC, P, 0, OP_SET_GLOBAL, OP_END,
        Mission::kEventSignal, 0xFFFF, P, 1, OP_SET_GLOBAL, OP_END,
        0x3000,
        0x2000,
        // The wall with id 5: when touched, sets global 2, signals 77 to the
        // logic object and hides itself.
        2, 0x4005,
        0x8000,
        Mission::kEventTouch, 0xFFFF, P, 1, P, 2, OP_SET_GLOBAL, P, 77, P, 0xE001, OP_SEND_SIGNAL, OP_PUSH_SELF,
        0x46, OP_END,
        0x3000,
        0x2000,
        0x1000
    };
    std::vector<uint8_t> scriptBytes = makeScriptFile(program);
    MemoryReadStream scriptStream(scriptBytes);
    ScriptFile script;
    CHECK(script.load(scriptStream));

    LevelBuilder builder;
    builder.addFloor(0, 0, 20, 20, 0);
    builder.addWall(10, 0, 10, 20, 0, 12, 0, 1, 5);
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    LevelRenderer renderer(level);
    Walker walker(level);
    walker.place(2, 2, 0);

    Mission mission(level, script, renderer, walker, nullptr);
    mission.start();
    CHECK(walker.getX() == 5);
    CHECK(walker.getY() == 10);
    double yaw = 1;
    CHECK(mission.takePlayerYaw(yaw));
    CHECK(std::fabs(yaw) < 1e-9);

    mission.update(250);
    CHECK_EQUAL(mission.getVM().getGlobal(0), 2);

    // Walking into the wall tells its object, which hides it.
    walker.move(10, 0, 0.01);
    CHECK(walker.getX() < 10);
    CHECK_EQUAL(walker.getContactWall(), 0);
    mission.update(10);
    CHECK_EQUAL(mission.getVM().getGlobal(2), 1);
    CHECK_EQUAL(mission.getVM().getGlobal(1), 77);
    CHECK(!(level.getWalls()[0].flags & LevelFile::kFlagVisible));

    walker.move(10, 0, 0.01);
    CHECK(walker.getX() > 15);
    CHECK(mission.getMissingNatives().empty());
}

static void mission_items_rest_on_floors() {
    const uint16_t P = OP_PUSH;
    // On the start broadcast: item 8 is dropped at the player, and item 7
    // is told to move, which items ignore.
    std::vector<uint16_t> program = {
        1, 0xE001,
        0x8000,
        kEventEnterState, 0xFFFF, OP_PUSH_SELF, 0x31, OP_END,
        Mission::kEventStart, 0xFFFF, P, 0, P, 0x2008, 0x71,
        OP_PUSH_BLOCK, 0, 0, 0, 35, 0, 35, P, 0x2007, 0x4D, OP_END,
        0x3000,
        0x2000,
        0x1000
    };
    std::vector<uint8_t> scriptBytes = makeScriptFile(program);
    MemoryReadStream scriptStream(scriptBytes);
    ScriptFile script;
    CHECK(script.load(scriptStream));

    LevelBuilder builder;
    builder.addFloor(0, 0, 20, 20, 0, 1, 1);
    builder.addFloor(20, 0, 40, 40, 3, 2, 2);
    builder.addFloor(0, 0, 10, 10, 1, 1, 1, LevelFile::kFlagPassThrough);
    // Items wait in a storage zone until they are placed.
    const uint32_t item = LevelFile::kBillboardItem | LevelFile::kFlagVisible;
    builder.addBillboard(7, 5, 5, 1.5, item, 0x80);
    builder.addBillboard(8, 100, 100, 0, item, 0x80);
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    LevelRenderer renderer(level);
    Walker walker(level);
    walker.place(30, 30, 3);

    Mission mission(level, script, renderer, walker, nullptr);
    // At the start: item 7 settles past the pass-through floor onto
    // the one below, and takes its zones. Item 8 has no floor under it.
    const LevelFile::Billboard& seven = level.getBillboards()[0], &eight = level.getBillboards()[1];
    CHECK(seven.z == 0);
    CHECK_EQUAL(seven.zones, (uint64_t)1);
    CHECK(eight.z == 0);
    CHECK_EQUAL(eight.zones, (uint64_t)0x80);

    mission.start();
    CHECK(eight.x == 30 << 16);
    CHECK(eight.y == 30 << 16);
    CHECK(eight.z == 3 << 16);
    CHECK_EQUAL(eight.zones, (uint64_t)2);
    CHECK(seven.x == 5 << 16);
    CHECK(mission.getMissingNatives().empty());
}

namespace {

// A sprite set of one-pixel frames whose colour is 10 + the frame number:
// sequence 0 (stand) is frames 0-4, one per view direction, and sequence 1
// (walk) frames 5-8.
bool loadTestSprites(uint16_t /*number*/, SpriteSet& set) {
    MemoryWriteStream file;
    file.writeUint32LE(2);
    file.writeUint32LE(9);
    file.writeUint32LE(0);
    file.writeUint32LE(2);
    file.writeUint32LE(0);
    file.writeUint32LE(0);
    const uint8_t sequences[] = { 5, 0, 1, 2, 3, 4, 4, 5, 6, 7, 8 };
    file.write(sequences, sizeof(sequences));
    for (int i = 0; i < 9; i++) {
        file.writeUint32LE(1 << 16); // half width 1.0
        file.writeUint32LE(1);
        file.writeUint32LE(1);
        const uint8_t pixel[4] = { (uint8_t)(10 + i), 0, 0, 3 }; // a solid column
        file.write(pixel, sizeof(pixel));
    }
    std::vector<uint8_t> data = file.data();
    MemoryReadStream stream(data);
    return set.load(stream);
}

} // namespace

static void actors_headings_and_view_directions() {
    CHECK_EQUAL(Actors::headingTowards(0, 1), 0);
    CHECK_EQUAL(Actors::headingTowards(-1, 0), 16384);
    CHECK_EQUAL(Actors::headingTowards(1, 0), 49152);

    LevelBuilder builder;
    builder.addFloor(0, 0, 40, 40, 0);
    builder.addActor(1, 10, 10, 0, Actors::kModeIdle, 0); // facing +y
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    Walker walker(level);
    Actors actors(level, walker, loadTestSprites);

    std::vector<SpriteDraw> sprites;
    auto viewFrom = [&](double x, double y, bool& mirrored) {
        actors.getSprites(x, y, Walker::kAllZones, sprites);
        mirrored = !sprites.empty() && sprites[0].mirrored;
        return sprites.empty() ? -1 : (int)sprites[0].surface->getPixel(0, 0) - 10;
    };
    bool mirrored;
    CHECK_EQUAL(viewFrom(10, 20, mirrored), 4); // in front
    CHECK_EQUAL(viewFrom(10, 0, mirrored), 0); // behind
    CHECK_EQUAL(viewFrom(20, 10, mirrored), 2); // from its right
    CHECK(!mirrored);
    CHECK_EQUAL(viewFrom(0, 10, mirrored), 2); // from its left: the right view flipped
    CHECK(mirrored);
    CHECK(sprites[0].halfWidth == 1.0);
    CHECK(sprites[0].height == 6.0);
    CHECK(sprites[0].bottom == 0.0);

    actors.setMode(0xA001, Actors::kModeHidden);
    viewFrom(10, 20, mirrored);
    CHECK(sprites.empty());
}

static void actors_take_hits() {
    const uint16_t P = OP_PUSH;
    // Actor 1: hit points 10, stun 10; hits above power 11 hurt, above 6
    // stun. Global 0 counts "hurt", global 1 "stunned", global 2 "killed".
    std::vector<uint16_t> program = {
        1, 0xA001,
        0x8000,
        kEventEnterState, 0xFFFF, P, 10, P, 10, P, 10, P, 10, P, 0, P, 6, P, 11, 0x41, P, 5, OP_PUSH_SELF, 0xF1,
        OP_END,
        Actors::kEventHurt, 0xFFFF, P, 0, OP_GET_GLOBAL, OP_INC, P, 0, OP_SET_GLOBAL, OP_END,
        Actors::kEventStunned, 0xFFFF, P, 1, OP_GET_GLOBAL, OP_INC, P, 1, OP_SET_GLOBAL, OP_END,
        Actors::kEventKilled, 0xFFFF, P, 1, P, 2, OP_SET_GLOBAL, OP_END,
        0x3000,
        0x2000,
        0x1000
    };
    std::vector<uint8_t> scriptBytes = makeScriptFile(program);
    MemoryReadStream scriptStream(scriptBytes);
    ScriptFile script;
    CHECK(script.load(scriptStream));

    LevelBuilder builder;
    builder.addFloor(0, 0, 40, 40, 0);
    builder.addActor(1, 10, 10, 0, Actors::kModeIdle, 0);
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    LevelRenderer renderer(level);
    Walker walker(level);
    walker.place(20, 20, 0);
    Actors actors(level, walker, loadTestSprites);
    Mission mission(level, script, renderer, walker, &actors);
    mission.start();

    mission.send(0xA001, Mission::kEventHit, { 3, 20, 5 }); // its own team: ignored
    CHECK_EQUAL(mission.getVM().getGlobal(0), 0);
    mission.send(0xA001, Mission::kEventHit, { 3, 9, 2 }); // stuns
    CHECK_EQUAL(mission.getVM().getGlobal(1), 1);
    CHECK_EQUAL(actors.getCombatStats(0xA001).stun, 7);
    CHECK_EQUAL(actors.getCombatStats(0xA001).hitPoints, 10);
    mission.send(0xA001, Mission::kEventHit, { 4, 20, 2 }); // hurts
    CHECK_EQUAL(mission.getVM().getGlobal(0), 1);
    CHECK_EQUAL(actors.getCombatStats(0xA001).hitPoints, 6);
    mission.send(0xA001, Mission::kEventHit, { 6, 20, 2 }); // kills
    CHECK_EQUAL(mission.getVM().getGlobal(2), 1);
    CHECK(mission.getMissingNatives().empty());
}

namespace {

// A section 4 record: kind, speed in units per second, damage and team.
std::vector<uint8_t> projectileType(int kind, double unitsPerSecond, int damage, int team, int count = 0,
                                 double spacing = 0, int impact = -1, uint32_t lifetime = 5000) {
    std::vector<uint8_t> r(100);
    writeLE32(&r[0x00], (uint32_t)kind);
    writeLE32(&r[0x08], lifetime);
    writeLE32(&r[0x10], 0x2000);                // half width 1/8
    writeLE32(&r[0x14], 0x4000);                // height 1/4
    writeLE32(&r[0x1C], 1);                     // keyed
    writeLE32(&r[0x34], 1);                     // frames
    writeLE32(&r[0x3C], (uint32_t)(int32_t)(unitsPerSecond * 65.536 * 65536));
    writeLE32(&r[0x40], (uint32_t)-1);
    writeLE32(&r[0x44], (uint32_t)-1);
    writeLE32(&r[0x48], (uint32_t)damage);
    writeLE32(&r[0x4C], (uint32_t)team);
    writeLE32(&r[0x54], (uint32_t)count);
    writeLE32(&r[0x58], kind == 3 ? 0x1555 : (uint32_t)(int32_t)(spacing * 65536));
    writeLE32(&r[0x5C], (uint32_t)impact);
    return r;
}

} // namespace

static void projectiles_fly_and_hit() {
    // Type 0: a bolt at 50 units/s; 1: a beam with a 10-sprite trail 0.5
    // apart that throws type 2, a burst of 8 sparks lasting 300 ms, where
    // it hits.
    std::vector<uint8_t> section;
    for (const std::vector<uint8_t>& r : { projectileType(2, 50, 6, 4), projectileType(1, 100, 3, 5, 10, 0.5, 2),
                                        projectileType(3, 10, 0, 7, 8, 0, -1, 300) })
        section.insert(section.end(), r.begin(), r.end());
    section.resize(section.size() + 100); // the end of the table

    LevelFile level;
    CHECK(loadLevel(level, makeRoom().build())); // a wall along x = 10, facing -x
    Projectiles shots(level, section);
    CHECK_EQUAL(shots.getTypeCount(), 3);
    std::vector<Projectiles::Target> none;
    std::vector<Projectiles::Report> reports;
    auto run = [&](int ms, double playerX = 100) {
        for (int t = 0; t < ms; t += 40)
            shots.update(40, ~0ull, none, playerX, 10, 6, 1.2, reports);
    };

    // A bolt stops at the wall's face...
    shots.fire(0, 5, 10, 5, 0, 0, 0xA001);
    run(400);
    CHECK_EQUAL(reports.size(), (size_t)1);
    CHECK(reports[0].hit.target == Projectiles::Hit::kWall);
    CHECK(std::fabs(reports[0].hit.x - 10) < 1e-6);
    CHECK_EQUAL(shots.getCount(), 0);

    // ...but passes through it from behind.
    reports.clear();
    shots.fire(0, 15, 10, 5, 3.14159265358979, 0, 0xA001);
    run(400);
    CHECK(reports.empty());
    CHECK_EQUAL(shots.getCount(), 1);
    run(5000);
    CHECK_EQUAL(shots.getCount(), 0);

    // A beam hits the player, throwing sparks; its trail drains into the
    // hit point and goes.
    reports.clear();
    shots.fire(1, 2, 10, 5.5, 0, 0, 0xA001);
    run(80, 8);
    CHECK_EQUAL(reports.size(), (size_t)1);
    CHECK(reports[0].hit.target == Projectiles::Hit::kPlayer);
    CHECK(std::fabs(reports[0].hit.x - (8 - 1.2)) < 1e-6);
    CHECK_EQUAL(shots.getCount(), 1 + 8);
    run(400, 8);
    CHECK_EQUAL(shots.getCount(), 0);

    // Shooters don't hit themselves.
    reports.clear();
    std::vector<Projectiles::Target> actors = { { 0xA001, 4, 10, 0, 6, 1 } };
    shots.fire(0, 4, 10, 5, 0, 0, 0xA001);
    shots.update(40, ~0ull, actors, 100, 10, 6, 1.2, reports);
    CHECK_EQUAL(shots.getCount(), 1);
    CHECK(reports.empty());
    shots.fire(0, 1, 10, 5, 0, 0, 0xA002);
    shots.update(40, ~0ull, actors, 100, 10, 6, 1.2, reports);
    CHECK_EQUAL(reports.size(), (size_t)1);
    CHECK(reports[0].hit.target == Projectiles::Hit::kActor);
    CHECK_EQUAL(reports[0].hit.index, 0xA001);
}

static void mission_punch() {
    const uint16_t P = OP_PUSH;
    // A logic object sets the punch (damage 3, power 9) and counts hit
    // reports for actor 1 (global 0) and misses (global 1); actor 1 stuns
    // above power 6 and records being stunned in global 2.
    std::vector<uint16_t> program = {
        1, 0xE001,
        0x8000,
        kEventEnterState, 0xFFFF, OP_PUSH_SELF, 0x31, P, 0, P, 0xFFFF, P, 63, P, 21, P, 9, P, 3, P, 3, P, 100,
        P, 0xFFFF, 0xC4, OP_END,
        Mission::kEventHitReport, 0xFFFF, P, 0xA001, OP_IF_EQ, P, 0, OP_GET_GLOBAL, OP_INC, P, 0, OP_SET_GLOBAL,
        OP_ELSE, P, 1, OP_GET_GLOBAL, OP_INC, P, 1, OP_SET_GLOBAL, OP_ENDIF, OP_END,
        0x3000,
        0x2000,
        1, 0xA001,
        0x8000,
        kEventEnterState, 0xFFFF, P, 10, P, 10, P, 10, P, 10, P, 40, P, 6, P, 11, 0x41, OP_END,
        Actors::kEventStunned, 0xFFFF, P, 1, P, 2, OP_SET_GLOBAL, OP_END,
        0x3000,
        0x2000,
        0x1000
    };
    std::vector<uint8_t> scriptBytes = makeScriptFile(program);
    MemoryReadStream scriptStream(scriptBytes);
    ScriptFile script;
    CHECK(script.load(scriptStream));

    LevelBuilder builder;
    builder.addFloor(0, 0, 40, 40, 0);
    builder.addActor(1, 10, 10, 0, Actors::kModeIdle, 0);
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    LevelRenderer renderer(level);
    Walker walker(level);
    walker.place(12, 10, 0);
    Actors actors(level, walker, loadTestSprites);
    Mission mission(level, script, renderer, walker, &actors);
    mission.start();
    CHECK_EQUAL(mission.getPunch().damage, 3);
    CHECK_EQUAL(mission.getPunch().hitFrame, 3);

    // Within reach (1.2 + 0.5 + the actor's radius of 1), it lands...
    LevelRenderer::Pick pick;
    pick.kind = LevelRenderer::Pick::kSprite;
    pick.object = 0xA001;
    pick.depth = 2.5;
    mission.punch(pick);
    CHECK_EQUAL(mission.getVM().getGlobal(0), 1);
    CHECK_EQUAL(mission.getVM().getGlobal(2), 1);
    CHECK_EQUAL(actors.getCombatStats(0xA001).stun, 7);
    // ...beyond it, everyone hears of a miss.
    pick.depth = 3;
    mission.punch(pick);
    CHECK_EQUAL(mission.getVM().getGlobal(1), 1);
    CHECK_EQUAL(actors.getCombatStats(0xA001).stun, 7);
    CHECK(mission.getMissingNatives().empty());
}

static void mission_flow() {
    const uint16_t P = OP_PUSH;
    // A logic object as verid1's e001: on the start (event 6) it sets two
    // objectives (bits 2 and 4) and runs the briefing transition, 100 ms a
    // phase; when that ends (event 7) it marks global 0. Event 8 (all done)
    // marks global 1 and starts the closing transition. Signal 1 marks
    // objective 4 done, signal 2 objective 2.
    std::vector<uint16_t> program = {
        1, 0xE001,
        0x8000,
        kEventEnterState, 0xFFFF, OP_PUSH_SELF, 0x31, OP_END,
        Mission::kEventStart, 0xFFFF, OP_PUSH32, 6, 0, 0x99, OP_PUSH32, 100, 0, OP_PUSH32, 100, 0, OP_PUSH32, 100, 0,
        0x91, OP_END,
        Mission::kEventBriefingDone, 0xFFFF, P, 1, P, 0, OP_SET_GLOBAL, OP_END,
        Mission::kEventObjectivesDone, 0xFFFF, P, 1, P, 1, OP_SET_GLOBAL, OP_PUSH32, 100, 0, OP_PUSH32, 100, 0,
        OP_PUSH32, 100, 0, 0x92, OP_END,
        Mission::kEventSignal, 0xFFFF, P, 1, OP_IF_EQ, OP_PUSH32, 4, 0, 0x9A, OP_ELSE, OP_PUSH32, 2, 0, 0x9A,
        OP_ENDIF, OP_END,
        0x3000,
        0x2000,
        0x1000
    };
    std::vector<uint8_t> scriptBytes = makeScriptFile(program);
    MemoryReadStream scriptStream(scriptBytes);
    ScriptFile script;
    CHECK(script.load(scriptStream));
    LevelBuilder builder;
    builder.addFloor(0, 0, 40, 40, 0);
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    LevelRenderer renderer(level);
    Walker walker(level);
    Mission mission(level, script, renderer, walker, nullptr);
    mission.start();

    // The briefing speckles over, crossfades to the game, and the
    // speckles clear; then the mission takes over.
    Mission::TransitionView view = mission.getTransitionView();
    CHECK(view.active && view.kind == Mission::TransitionView::kBriefing && view.phase == 1 && view.amount == 0);
    mission.update(50);
    CHECK_EQUAL(mission.getTransitionView().phase, 1);
    CHECK_EQUAL(mission.getTransitionView().amount, 128);
    mission.update(130);
    CHECK_EQUAL(mission.getTransitionView().phase, 2);
    CHECK_EQUAL(mission.getTransitionView().amount, 819);
    mission.update(200);
    CHECK(!mission.getTransitionView().active);
    CHECK_EQUAL(mission.getVM().getGlobal(0), 1);

    // One objective isn't enough...
    mission.send(0xE001, Mission::kEventSignal, { 1 });
    mission.update(40);
    CHECK(!mission.isAccomplished());

    // Saved here and loaded into a fresh copy of the level, it carries on
    // the same.
    walker.place(12, 7, 0);
    MemoryWriteStream saved;
    Serializer out(nullptr, &saved);
    mission.sync(out);
    CHECK(!out.err());
    {
        LevelFile level2;
        CHECK(loadLevel(level2, builder.build()));
        LevelRenderer renderer2(level2);
        Walker walker2(level2);
        Mission copy(level2, script, renderer2, walker2, nullptr);
        MemoryReadStream in(saved.data());
        Serializer load(&in, nullptr);
        copy.sync(load);
        CHECK(!load.err());
        CHECK_EQUAL(copy.getVM().getGlobal(0), 1);
        CHECK(std::fabs(walker2.getX() - 12) < 1e-9 && std::fabs(walker2.getY() - 7) < 1e-9);
        CHECK_EQUAL(copy.getTime(), mission.getTime());
        copy.send(0xE001, Mission::kEventSignal, { 2 });
        copy.update(40);
        CHECK(copy.isAccomplished());
        // A save from a different level is refused.
        LevelBuilder other;
        other.addFloor(0, 0, 40, 40, 0);
        other.addFloor(40, 0, 80, 40, 0);
        LevelFile level3;
        CHECK(loadLevel(level3, other.build()));
        LevelRenderer renderer3(level3);
        Walker walker3(level3);
        Mission wrong(level3, script, renderer3, walker3, nullptr);
        MemoryReadStream in3(saved.data());
        Serializer load3(&in3, nullptr);
        wrong.sync(load3);
        CHECK(load3.err());
    }
    // ...both are; event 8 starts the closing transition, which ends it.
    mission.send(0xE001, Mission::kEventSignal, { 2 });
    mission.update(40);
    CHECK(mission.isAccomplished());
    CHECK_EQUAL(mission.getVM().getGlobal(1), 1);
    CHECK(!mission.isFinished());
    mission.update(150);
    CHECK(mission.getTransitionView().kind == Mission::TransitionView::kEnding);
    CHECK_EQUAL(mission.getTransitionView().phase, 2);
    mission.update(200);
    CHECK(mission.isFinished());
    CHECK(mission.getMissingNatives().empty());
}

static void mission_player_settings() {
    const uint16_t P = OP_PUSH;
    // verid1's start-up settings: a cutscene freeze (speed 0, the rest
    // kept), gravity 80 with the third value kept, map zooms of 1, 2 and
    // 4 pixels a unit, and an item that is greyed out and given icon 8.
    std::vector<uint16_t> program = {
        1, 0xE001,
        0x8000,
        kEventEnterState, 0xFFFF,
        OP_PUSH32, 1, 0x8000, P, 0x8000, P, 0x8000, OP_PUSH32, 0, 0x8000, P, 0x8000, P, 0x8000, OP_PUSH32, 0, 0, 0xB2,
        OP_PUSH32, 0, 0x8000, OP_PUSH32, 0, 0x8000, OP_PUSH32, 0, 80, 0xA3,
        P, 15, P, 2, P, 15, P, 1, P, 16, P, 1, 0xC8,
        P, 0, P, 0xE001, 0xD7,
        P, 0, P, 1, P, 0xE001, 0x82, P, 8, OP_PUSH_SELF, 0x83,
        OP_END,
        0x3000,
        0x2000,
        0x1000
    };
    std::vector<uint8_t> scriptBytes = makeScriptFile(program);
    MemoryReadStream scriptStream(scriptBytes);
    ScriptFile script;
    CHECK(script.load(scriptStream));
    LevelBuilder builder;
    builder.addFloor(0, 0, 40, 40, 0);
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    LevelRenderer renderer(level);
    Walker walker(level);
    Mission mission(level, script, renderer, walker, nullptr);
    mission.start();
    CHECK_EQUAL(mission.getMotion().speed, 0.0);
    CHECK_EQUAL(mission.getMotion().turnRate, 15);
    CHECK(std::fabs(mission.getMotion().jump - 13.9) < 1e-9);
    CHECK_EQUAL(walker.getGravity(), 80.0);
    CHECK_EQUAL(walker.getMaxFallSpeed(), Walker::kMaxFallSpeed);
    CHECK_EQUAL(mission.getHud().getMapZoom(0), 1.0);
    CHECK_EQUAL(mission.getHud().getMapZoom(2), 4.0);
    mission.getHud().addItem(0xE001, 3);
    CHECK(!mission.getHud().isSlotEnabled(3));
    CHECK_EQUAL(mission.getItemIcon(0xE001), 8);
    CHECK(mission.getMissingNatives().empty());
}

static void movers_open_and_close() {
    // A door across a room, sliding up (mode 6): mover 5, opening in about
    // a second (speed 25600), closing after about one (wait 256), the door
    // its own button, and reversing when it meets the player.
    LevelBuilder builder;
    builder.addFloor(0, 0, 40, 40, 0);
    builder.addWall(20, 0, 20, 40, 0.5, 7.0);
    std::vector<uint8_t>& r = builder.walls.back();
    writeLE32(&r[0x6C], readLE32(&r[0x6C]) | LevelFile::kWallMoverReverses);
    writeLE16(&r[0x8A], 5);
    writeLE16(&r[0x8C], 6);
    writeLE16(&r[0x8E], 25600);
    writeLE16(&r[0x90], 256);
    writeLE16(&r[0x92], 5);
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    Movers movers(level);
    CHECK_EQUAL(movers.getCount(), 1);
    Walker walker(level);
    walker.place(5, 20, 0);

    std::vector<int> moved;
    std::vector<Movers::Notice> notices;
    movers.bump(0, notices);
    CHECK_EQUAL(notices.size(), (size_t)1);
    CHECK_EQUAL(notices[0].object, 0x8005);
    CHECK_EQUAL(notices[0].event, Movers::kEventOpening);
    for (int t = 0; t < 1100; t += 100)
        movers.update(100, walker, moved, notices);
    CHECK_EQUAL(movers.getState(5), (int)Movers::kOpen);
    CHECK_EQUAL(level.getWalls()[0].bottom, level.getWalls()[0].top);
    CHECK(!moved.empty());
    // It closes by itself...
    for (int t = 0; t < 2500; t += 100)
        movers.update(100, walker, moved, notices);
    CHECK_EQUAL(movers.getPosition(5), 0);
    CHECK_EQUAL(level.getWalls()[0].bottom, 1 << 15);
    CHECK_EQUAL(notices.back().event, Movers::kEventClosed);

    // ...but not onto the player, for whom it opens again.
    movers.bump(0, notices);
    for (int t = 0; t < 1100; t += 100)
        movers.update(100, walker, moved, notices);
    walker.place(20, 20, 0);
    for (int t = 0; t < 1500; t += 100)
        movers.update(100, walker, moved, notices);
    CHECK(movers.getPosition(5) > 0);
    CHECK(movers.getState(5) == Movers::kOpening || movers.getState(5) == Movers::kOpen);

    // Locked, walking into it does nothing.
    walker.place(5, 20, 0);
    for (int t = 0; t < 4000; t += 100)
        movers.update(100, walker, moved, notices);
    CHECK_EQUAL(movers.getPosition(5), 0);
    movers.setLocked(5, true);
    notices.clear();
    movers.bump(0, notices);
    CHECK(notices.empty());
}

static void walker_fits_a_gap_its_width() {
    // enterp's archways are exactly as wide as its player (width 3): two
    // walls across a corridor with a 3-unit gap between their ends.
    LevelBuilder builder;
    builder.addFloor(0, 0, 40, 40, 0);
    builder.addWall(0, 20, 18.5, 20, 0, 10);
    builder.addWall(21.5, 20, 40, 20, 0, 10);
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    Walker walker(level);
    walker.setRadius(1.5);
    walker.place(20, 10, 0);
    for (int i = 0; i < 40; i++)
        walker.move(0, 0.5, 0.04);
    CHECK(walker.getY() > 25);
    // A narrower gap still stops them.
    LevelBuilder narrow;
    narrow.addFloor(0, 0, 40, 40, 0);
    narrow.addWall(0, 20, 18.7, 20, 0, 10);
    narrow.addWall(21.3, 20, 40, 20, 0, 10);
    LevelFile level2;
    CHECK(loadLevel(level2, narrow.build()));
    Walker walker2(level2);
    walker2.setRadius(1.5);
    walker2.place(20, 10, 0);
    for (int i = 0; i < 40; i++)
        walker2.move(0, 0.5, 0.04);
    CHECK(walker2.getY() < 20);
}

static void walker_line_of_sight() {
    LevelFile level;
    CHECK(loadLevel(level, makeRoom(12).build()));
    Walker walker(level);
    // The wall along x = 10 stands between x = 5 and x = 15, up to 12 high.
    CHECK(walker.lineOfSight(5, 5, 6, 8, 15, 6, ~0ull));
    CHECK(!walker.lineOfSight(5, 5, 6, 15, 5, 6, ~0ull));
    CHECK(walker.lineOfSight(5, 5, 13, 15, 5, 13, ~0ull)); // over the top
    CHECK(walker.lineOfSight(5, 5, 6, 15, 5, 6, 2));       // not in those zones
    CHECK(!walker.lineOfSight(5, 5, 0, 6, 5, 6, ~0ull));   // too steep
}

static void actors_walk_and_block() {
    LevelBuilder builder;
    builder.addFloor(0, 0, 40, 40, 0);
    builder.addActor(1, 10, 10, 0, Actors::kModeWalkTo, 49152); // facing +x
    builder.addActor(2, 10, 30, 0, Actors::kModeIdle, 0);
    LevelFile level;
    CHECK(loadLevel(level, builder.build()));
    Walker player(level);
    player.place(30, 10, 0);
    Actors actors(level, player, loadTestSprites);
    player.setObstacles(&actors.getObstacles());

    // Actor 1 walks at the player: 64 frames a second at 1 unit per frame.
    std::vector<Actors::ScriptEvent> events;
    int arrived = 0, cycles = 0;
    for (int i = 0; i < 15; i++) {
        events.clear();
        actors.update(20, player, events);
        for (const Actors::ScriptEvent& event : events) {
            CHECK_EQUAL(event.object, 0xA001);
            arrived += event.event == Actors::kEventArrived;
            cycles += event.event == Actors::kEventCycle;
        }
    }
    double x, y, z;
    uint16_t heading;
    CHECK(actors.getPosition(1, x, y, z, heading));
    CHECK(x > 20 && x < 30);
    CHECK(std::fabs(y - 10) < 0.01);
    CHECK_EQUAL(heading, 49152);
    CHECK(cycles >= 3);
    CHECK_EQUAL(arrived, 0);
    for (int i = 0; i < 20 && !arrived; i++) {
        events.clear();
        actors.update(20, player, events);
        for (const Actors::ScriptEvent& event : events)
            arrived += event.event == Actors::kEventArrived;
    }
    CHECK(arrived > 0);

    // Actor 2 stands in the player's way.
    player.place(10, 22, 0);
    player.move(0, 10, 0.1);
    CHECK(std::fabs(player.getY() - (30 - 1 - Walker::kRadius)) < 0.05);
    CHECK_EQUAL(actors.getObstacleObject(player.getContactObstacle()), 0xA002);
}

static const reluva::test::Suite suite("stgenerations.unit.level", {
    {"level tables", [](const std::filesystem::path&) { level_tables(); }},
    {"level texture is column major", [](const std::filesystem::path&) { level_texture_is_column_major(); }},
    {"level rejects bad files", [](const std::filesystem::path&) { level_rejects_bad_files(); }},
    {"level map draws sectors", [](const std::filesystem::path&) { level_map_draws_sectors(); }},
    {"level surfaces billboards and sky", [](const std::filesystem::path&) { level_surfaces_billboards_and_sky(); }},
    {"level sky size when the header does not fit", [](const std::filesystem::path&) { level_sky_size_when_the_header_does_not_fit(); }},
    {"level renderer draws floor wall and sky", [](const std::filesystem::path&) { level_renderer_draws_floor_wall_and_sky(); }},
    {"level renderer wall sides and mirroring", [](const std::filesystem::path&) { level_renderer_wall_sides_and_mirroring(); }},
    {"level light levels", [](const std::filesystem::path&) { level_light_levels(); }},
    {"level shade tables", [](const std::filesystem::path&) { level_shade_tables(); }},
    {"level blend table", [](const std::filesystem::path&) { level_blend_table(); }},
    {"level renderer lighting", [](const std::filesystem::path&) { level_renderer_lighting(); }},
    {"level renderer draws only visible zones", [](const std::filesystem::path&) { level_renderer_draws_only_visible_zones(); }},
    {"walker stops at walls", [](const std::filesystem::path&) { walker_stops_at_walls(); }},
    {"walker wall sides heights and zones", [](const std::filesystem::path&) { walker_wall_sides_heights_and_zones(); }},
    {"walker steps up and falls", [](const std::filesystem::path&) { walker_steps_up_and_falls(); }},
    {"walker steps down jumps and passes through", [](const std::filesystem::path&) { walker_steps_down_jumps_and_passes_through(); }},
    {"mission runs level scripts", [](const std::filesystem::path&) { mission_runs_level_scripts(); }},
    {"mission items rest on floors", [](const std::filesystem::path&) { mission_items_rest_on_floors(); }},
    {"actors headings and view directions", [](const std::filesystem::path&) { actors_headings_and_view_directions(); }},
    {"actors take hits", [](const std::filesystem::path&) { actors_take_hits(); }},
    {"projectiles fly and hit", [](const std::filesystem::path&) { projectiles_fly_and_hit(); }},
    {"mission punch", [](const std::filesystem::path&) { mission_punch(); }},
    {"mission flow", [](const std::filesystem::path&) { mission_flow(); }},
    {"mission player settings", [](const std::filesystem::path&) { mission_player_settings(); }},
    {"movers open and close", [](const std::filesystem::path&) { movers_open_and_close(); }},
    {"walker fits a gap its width", [](const std::filesystem::path&) { walker_fits_a_gap_its_width(); }},
    {"walker line of sight", [](const std::filesystem::path&) { walker_line_of_sight(); }},
    {"actors walk and block", [](const std::filesystem::path&) { actors_walk_and_block(); }},
});
