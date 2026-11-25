// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Need for Speed II SE's own formats, on files built here: its tracks
// (.trk) and their .col files, skies and lights, animation scripts (.can),
// car models (.geo) and movies (.dct).
//
//   reluva-tests nfs.unit.nfs2se

#include "engines/nfs/nfs2se/formats/can.h"
#include "engines/nfs/nfs2se/formats/col.h"
#include "engines/nfs/nfs2se/formats/dct.h"
#include "engines/nfs/nfs2se/formats/geo.h"
#include "engines/nfs/nfs2se/formats/hrz.h"
#include "engines/nfs/nfs2se/formats/paint.h"
#include "engines/nfs/nfs2se/formats/trk.h"
#include "engines/nfs/nfs2se/glide_sky.h"
#include "tests/engines/nfs/fixtures.h"

using namespace reluva::nfs::testing;

namespace {

void putS16(Bytes &b, int v) {
    put16(b, unsigned(v) & 0xffff);
}

// A mesh record (extrablock 8): one quad over four vertices.
Bytes mesh() {
    Bytes m;
    put32(m, 8 + 4 * 6 + 8);
    put16(m, 4);
    put16(m, 1);
    for (int v : {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0})
        putS16(m, v);
    put16(m, 3);
    put16(m, 0xffff);
    m.insert(m.end(), {0, 1, 2, 3});
    return m;
}

// A fixed placement of mesh 0 at (x, 0, y), and one of type 4.
Bytes placements(int x, int y) {
    Bytes p;
    put16(p, 16);
    p.insert(p.end(), {1, 0});
    put32(p, uint32_t(x * 65536));
    put32(p, 0);
    put32(p, uint32_t(y * 65536));
    put16(p, 20);
    p.insert(p.end(), {4, 0});
    put32(p, 0), put32(p, 0), put32(p, 0);
    put32(p, 0); // its obstacle
    return p;
}

Bytes extrablock(uint16_t id, uint16_t count, const Bytes &records) {
    Bytes b;
    put32(b, uint32_t(8 + records.size()));
    put16(b, id);
    put16(b, count);
    b.insert(b.end(), records.begin(), records.end());
    return b;
}

// A block: one vertex from the next block, levels of 1, 2 and 3 own vertices;
// each level one quad, the high level a second-group quad too; a mesh, two
// placements, the type 4's obstacle and a sound. `badIndex` gives the low
// level a vertex it hasn't.
Bytes block(uint32_t serial, bool badIndex = false) {
    Bytes b(88, 0);
    putU32(b, 8, 4); // four extrablocks
    putU32(b, 12, serial);
    for (int c = 0; c < 4; ++c)
        putU32(b, size_t(16 + 12 * c), uint32_t(c * 65536));
    const uint16_t counts[] = {1, 1, 2, 3, 1, 0, 1, 0, 1, 1}; // nv8, nv4, nv2, nv1; then pairs
    for (int i = 0; i < 10; ++i) {
        b[size_t(68 + 2 * i)] = uint8_t(counts[i]);
        b[size_t(69 + 2 * i)] = 0;
    }
    for (int v = 0; v < 4; ++v) // x, height, y in 256ths of the unit
        putS16(b, v * 256), putS16(b, 0), putS16(b, v * 512);
    auto poly = [&](uint16_t texture, std::array<uint8_t, 4> v) {
        put16(b, texture);
        put16(b, 0x1fff); // vertex 2's shade 0, the others 7
        b.insert(b.end(), v.begin(), v.end());
    };
    poly(1, {0, 1, 1, uint8_t(badIndex ? 2 : 0)}); // low: vertices 0-1
    poly(2, {0, 1, 2, 2});                         // medium: 0-2
    poly(3, {0, 1, 2, 3});                         // high: 0-3
    poly(4, {3, 2, 1, 0});                         // high, second group
    const uint32_t tableField = 64;
    const size_t table = (b.size() + 3) & ~size_t(3);
    b.resize(table, 0);
    putU32(b, tableField, uint32_t(table - tableField));
    b.resize(table + 16, 0);
    Bytes obstacle;
    Bytes sound;
    put32(obstacle, 0), put32(obstacle, 0), put32(obstacle, 0);
    put16(obstacle, 384); // 1.5 units across
    put16(obstacle, 9);
    obstacle.insert(obstacle.end(), {3, 0, 1, 2});
    put32(sound, 65536), put32(sound, 0), put32(sound, 0);
    put16(sound, 4);
    put16(sound, 1);
    const Bytes extras[] = {extrablock(8, 1, mesh()), extrablock(7, 2, placements(5, 6)), extrablock(11, 1, obstacle),
                            extrablock(17, 1, sound)};
    for (size_t i = 0; i < 4; ++i) {
        putU32(b, table + 4 * i, uint32_t(b.size()));
        b.insert(b.end(), extras[i].begin(), extras[i].end());
    }
    putU32(b, 0, uint32_t(b.size()));
    putU32(b, 4, uint32_t(b.size()));
    return b;
}

// A track of one superblock of two blocks, their reference points at
// (0, 0, 0) and (0, 1, 32).
Bytes track(bool badIndex = false) {
    Bytes t = {'T', 'R', 'A', 'C'};
    for (uint32_t v : {22u, 0u, 0u, 0u, 0u, 1u, 2u})
        put32(t, v);
    const size_t superblockOffset = t.size();
    put32(t, 0);
    for (int c : {0, 0, 0, 0, 1, 32})
        put32(t, uint32_t(c * 65536));
    put16(t, 0), put16(t, 0);
    putU32(t, superblockOffset, uint32_t(t.size()));
    const Bytes b0 = block(0, badIndex);
    const Bytes b1 = block(1);
    Bytes sb;
    put32(sb, uint32_t(20 + b0.size() + b1.size()));
    put32(sb, 2);
    put32(sb, 0);
    put32(sb, 20);
    put32(sb, uint32_t(20 + b0.size()));
    sb.insert(sb.end(), b0.begin(), b0.end());
    sb.insert(sb.end(), b1.begin(), b1.end());
    t.insert(t.end(), sb.begin(), sb.end());
    return t;
}

void testTrack() {
    nfs2se::Track t;
    CHECK(!nfs2se::parseTrack(track(), t));
    CHECK(t.header[0] == 22 && t.blocks.size() == 2 && t.superblockOf == (std::vector<uint16_t>{0, 0}));
    if (t.blocks.size() != 2)
        return;
    const nfs2se::TrackBlock &b = t.blocks[0];
    CHECK(b.serial == 0 && b.fromNext == 1);
    CHECK(b.corners[1][0] == 1.0f && b.reference[2] == 0.0f);
    // Vertex 0 is the next block's, from its point (0, 1, 32); the others
    // from this block's, (0, 0, 0), in 256ths of the unit: (512, 0, 1024).
    CHECK(b.vertices.size() == 4);
    if (b.vertices.size() == 4) {
        CHECK(b.vertices[0] == (nfs2se::Point{0.0f, 1.0f, 32.0f}));
        CHECK(b.vertices[2] == (nfs2se::Point{2.0f, 0.0f, 4.0f}));
    }
    CHECK(b.levels[nfs2se::TrackBlock::kLow].first.size() == 1 && b.levels[nfs2se::TrackBlock::kLow].second.empty());
    CHECK(b.levels[nfs2se::TrackBlock::kHigh].first.size() == 1 &&
          b.levels[nfs2se::TrackBlock::kHigh].second.size() == 1);
    if (!b.levels[nfs2se::TrackBlock::kHigh].second.empty())
        CHECK(b.levels[nfs2se::TrackBlock::kHigh].second[0].texture == 4 &&
              b.levels[nfs2se::TrackBlock::kHigh].second[0].shades == 0x1fff);
    CHECK(b.meshes.size() == 1 && b.meshes[0].vertices.size() == 4 && b.meshes[0].quads.size() == 1);
    CHECK(b.placements.size() == 2);
    if (b.placements.size() == 2) {
        CHECK(b.placements[0].type == 1 && b.placements[0].position == (nfs2se::Point{5.0f, 0.0f, 6.0f}));
        CHECK(b.placements[1].type == 4 && b.placements[1].obstacle == 0);
    }
    CHECK(b.obstacles.size() == 1 && b.sounds.size() == 1);
    if (b.obstacles.size() == 1 && b.sounds.size() == 1) {
        const nfs2se::Obstacle &o = b.obstacles[0];
        CHECK(o.radius == 384 && o.serial == 9 && o.height == 3 && o.placement == 1 && o.kind == 2);
        CHECK(b.sounds[0].position[0] == 1.0f && b.sounds[0].sound == 4 && b.sounds[0].random == 1);
    }
    CHECK(b.extra.size() == 4 && b.extra[0].id == 8 && b.extra[1].id == 7);

    // A polygon using a vertex its level hasn't fails; so does a cut file.
    CHECK(nfs2se::parseTrack(track(true), t));
    Bytes cut = track();
    cut.resize(cut.size() - 10);
    CHECK(nfs2se::parseTrack(cut, t));
    CHECK(nfs2se::parseTrack(Bytes{'T', 'R', 'A', 'X'}, t));
}

void testCollision() {
    Bytes col = {'C', 'O', 'L', 'L'};
    put32(col, 11);
    put32(col, 0);
    put32(col, 4);
    const size_t table = col.size();
    col.resize(table + 16, 0);
    Bytes textures;
    put16(textures, 7);
    textures.insert(textures.end(), {0x24, 0x09, 178, 178, 178, 3, 5, 0}); // see-through, animated; a turn

    Bytes road(36, 0);
    putU32(road, 0, 65536 * 2);
    road[18] = 127; // across
    road[22] = 3;   // block
    road[26] = 10;
    road[28] = 20;
    road[30] = 0x01; // a wall on the right
    road[31] = 0x12; // a lane on the left, two on the right
    road[32] = 7;
    road[33] = 8;
    const Bytes blocks[] = {extrablock(2, 1, textures), extrablock(8, 1, mesh()), extrablock(7, 2, placements(1, 2)),
                            extrablock(15, 1, road)};
    for (int i = 0; i < 4; ++i) {
        putU32(col, table + 4 * size_t(i), uint32_t(col.size() - 16));
        col.insert(col.end(), blocks[i].begin(), blocks[i].end());
    }
    CHECK(nfs2se::isNfs2Collision(col));
    nfs2se::Collision c;
    CHECK(!nfs2se::parseCollision(col, c));
    CHECK(c.textures.size() == 1);
    if (!c.textures.empty()) {
        const nfs2se::Collision::Texture &x = c.textures[0];
        CHECK(x.picture == 7 && (x.flags & nfs2se::Collision::kSeeThrough) && x.orientation == 0x09);
        CHECK(x.colour[2] == 178 && x.frames == 3 && x.ticksPerFrame == 5);
    }
    CHECK(c.meshes.size() == 1 && c.placements.size() == 2);
    CHECK(c.road.size() == 1);
    if (!c.road.empty()) {
        CHECK(c.road[0].position[0] == 2.0f && c.road[0].across[0] == 127 && c.road[0].block == 3);
        const nfs2se::Collision::RoadPoint &r = c.road[0];
        CHECK(r.leftEdge == 10 && r.rightEdge == 20 && r.edges == 1 && r.lanes == 0x12);
        CHECK(r.leftLaneWidth == 7 && r.rightLaneWidth == 8);
    }
    // NFS III's, with 8-byte texture records, isn't.
    Bytes nfs3 = col;
    putU32(nfs3, size_t(16 + 16 + 0), 8 + 8); // its texture block holding 8 bytes
    CHECK(!nfs2se::isNfs2Collision(nfs3));
}

// Two keys: from 2 units back to the car, unturned, then turned half round y.
void testAnimationScript() {
    Bytes s;
    put16(s, 8 + 2 * 20);
    put16(s, 0x0164);
    put16(s, 2);
    put16(s, 6);
    put32(s, 0), put32(s, 0), put32(s, uint32_t(-2 * 65536));
    for (int v : {0, 0, 0, 0x4000})
        putS16(s, v);
    put32(s, 0), put32(s, 32768), put32(s, 0);
    for (int v : {0, 0x4000, 0, 0})
        putS16(s, v);
    nfs2se::AnimationScript a;
    CHECK(!nfs2se::parseAnimationScript(s, a));
    CHECK(a.unknown == 0x0164 && a.ticksPerKey == 6 && a.keys.size() == 2);
    if (a.keys.size() == 2) {
        CHECK(a.keys[0].offset == (nfs2se::Point{0.0f, 0.0f, -2.0f}) && a.keys[0].rotation[3] == 0x4000);
        CHECK(a.keys[1].offset[1] == 0.5f && a.keys[1].rotation[1] == 0x4000);
    }
    Bytes cut = s;
    cut.resize(cut.size() - 1);
    CHECK(nfs2se::parseAnimationScript(cut, a)); // its header gives another size
    Bytes extra = s;
    put32(extra, 0);
    extra[0] = uint8_t(extra.size());
    CHECK(nfs2se::parseAnimationScript(extra, a)); // not a whole key
}

// carhsvt.txt's colours, and a car's greens painted with them.
// The 3dfx program's fog by track and road point, and its sky's ring colours.
void testGlideSky() {
    // Track 0: table 0 throughout, its colour a grey over a stretch.
    nfs2se::GlideFog fog = nfs2se::glideFog(0, 100);
    CHECK(std::abs(fog.density - 0.008f) < 1e-6f && fog.colour == (nfs2se::Rgb{0, 2, 4}));
    CHECK(nfs2se::glideFog(0, 560).colour == (nfs2se::Rgb{62, 62, 62}));
    CHECK(nfs2se::glideFog(0, 700).colour == (nfs2se::Rgb{0, 2, 4}));
    // Track 4's table 14 (of 20, 0.002 to 0.022) in its stretch, 0 after.
    CHECK(std::abs(nfs2se::glideFog(4, 700).density - (0.002f + 14 * 0.001f)) < 1e-6f);
    CHECK(std::abs(nfs2se::glideFog(4, 900).density - 0.002f) < 1e-6f);
    CHECK(nfs2se::glideFog(9, 0).density == 0);

    nfs2se::Sky sky;
    sky.skyBaseSun = {0, 0, 0};
    sky.skyBaseOpposite = {80, 160, 240};
    const auto ring = nfs2se::skyRingColours(sky);
    CHECK(ring[0] == (nfs2se::Rgb{0, 0, 0}) && ring[16] == ring[0] && ring[15] == ring[0]);
    CHECK(ring[7] == ring[8] && ring[7] == (nfs2se::Rgb{70, 140, 210}));
}

void testPaint() {
    std::vector<nfs2se::PaintColour> colours;
    CHECK(!nfs2se::parsePaintColours("/* Color 0 */\n0,0,210,80\n/* Color 1 */\n0,255,200,64\n", colours));
    CHECK(colours.size() == 2);
    CHECK(nfs2se::parsePaintColours("1,2,3", colours));
    if (colours.size() != 2)
        return;
    CHECK(nfs2se::paintRgb(colours[0]) == (std::array<int, 3>{210, 210, 210})); // no saturation: a grey
    CHECK(nfs2se::paintRgb(colours[1]) == (std::array<int, 3>{200, 0, 0}));     // hue 0: red
    CHECK(nfs2se::paintRgb({128, 256, 256, 64})[2] > 200);                      // half a turn: toward blue
    CHECK(nfs2se::isPaint(0, 0x80, 0) && !nfs2se::isPaint(0, 0x7f, 0) && !nfs2se::isPaint(0x56, 0xff, 0));
    const nfs2se::PaintColour red = colours[1]; // threshold 64
    // Shade 32, below the threshold: half the red.
    CHECK(nfs2se::repaint(0, 0x80 + 32, 0, red) == (std::array<uint8_t, 3>{100, 0, 0}));
    // Shade 127, the brightest: four times the red, as far as 255.
    CHECK(nfs2se::repaint(0, 0xff, 0, red) == (std::array<uint8_t, 3>{255, 0, 0}));
    // Red and blue in it whiten it; other colours stay as they are.
    CHECK(nfs2se::repaint(10, 0x80 + 32, 20, red) == (std::array<uint8_t, 3>{115, 16, 30}));
    CHECK(nfs2se::repaint(200, 50, 50, red) == (std::array<uint8_t, 3>{200, 50, 50}));
}

// A picture's corners on a quad's vertices, as the program turns them: 0
// bottom left, 1 bottom right, 2 top right, 3 top left.
void testPictureCorners() {
    using C = std::array<uint8_t, 4>;
    CHECK(nfs2se::pictureCorners(0x01) == (C{1, 0, 3, 2})); // top right first
    CHECK(nfs2se::pictureCorners(0x00) == nfs2se::pictureCorners(0x01));
    CHECK(nfs2se::pictureCorners(0x10) == (C{0, 1, 2, 3}));
    CHECK(nfs2se::pictureCorners(0x40) == (C{2, 3, 0, 1}));
    CHECK(nfs2se::pictureCorners(0x05) == (C{3, 2, 1, 0}));
    // The turns, and after a mirror the other way round.
    CHECK(nfs2se::pictureCorners(0x03) == (C{2, 1, 0, 3}));
    CHECK(nfs2se::pictureCorners(0x09) == (C{0, 3, 2, 1}));
    CHECK(nfs2se::pictureCorners(0x12) == (C{1, 2, 3, 0}));
    CHECK(nfs2se::pictureCorners(0x18) == (C{3, 0, 1, 2}));
}

void testSky() {
    // The software program's: 26 values, a comment holding values that
    // aren't read.
    const std::string software = "/* Black Horizon */ 0,\n/* Mirror Flag */ 1,\n/* radius */ 1500,\n"
                                 "/* angle */ 0,\n/* flat */ 1000,\n/* base */ -700,\n/* height */ 1043,\n"
                                 "/* pixmap base */ 60,\n/* sky */ 21,18,45,\n/* base */ 151,154,190,\n"
                                 "/* distances */ 0,250,500,900,\n/* lights */ 0,0,10,15,\n/* system */ 0,\n"
                                 "/* value */ 40,40,40 /* 1,2,3 not these */\n";
    nfs2se::Sky sky;
    CHECK(!nfs2se::parseSky(software, sky));
    CHECK(sky.kind == nfs2se::Sky::kSoftware && sky.mirror && !sky.blackHorizon);
    CHECK(sky.radius == 1500 && sky.base == -700 && sky.pixmapBase == 60);
    CHECK(sky.skyColour == (nfs2se::Rgb{21, 18, 45}) && sky.depthLights[3] == 15 && sky.colourValue[2] == 40);

    // The 3dfx program's: a cloud dome first, then shaded earth and sky.
    const std::string glide = "/*dome*/ 50, 100,\n/*black*/ 0,\n/*mirror*/ 1,\n1500, 0, 1000, -750, 1250, 1095, "
                              "1100, 725,\n0,4,8,\n0,4,8,\n0,0,0,\n8,8,25 /*37,31,102*/\n9,9,26\n";
    CHECK(!nfs2se::parseSky(glide, sky));
    CHECK(sky.kind == nfs2se::Sky::kGlide && sky.cloudDomeOffset == 50 && sky.cloudDomeHeight == 100);
    CHECK(sky.midpoint == 1095 && sky.pixmapBottom == 725 && sky.skyBaseOpposite == (nfs2se::Rgb{9, 9, 26}));

    CHECK(nfs2se::parseSky("1, 2, 3", sky));

    std::string lights = "/* Depth cueing */ 0,15, 100,15, 200,13, 250,10,\n/* palettes */ ";
    for (int i = 0; i < 16; ++i)
        lights += std::to_string(i) + ", 0, 127,\n";
    lights += "/* max, min */ 127, 0,\n/* sfx */ 1, 2, 3, 4, 5, 6, 7\n";
    nfs2se::Lights l;
    CHECK(!nfs2se::parseLights(lights, l));
    CHECK(l.depthCueing[3] == (std::array<int, 2>{250, 10}) && l.palette[15] == (nfs2se::Rgb{15, 0, 127}));
    CHECK(l.maximum == 127 && l.effectLight[6] == 7);
    CHECK(nfs2se::parseLights("1, 2", l));
}

// A model: part 0 with three vertices (odd: 6 bytes follow them) and one
// quad, part 1 at (1, 0.5, -2) with two vertices and no polygons, the rest
// empty.
Bytes carModel(uint8_t badVertex = 0) {
    Bytes m;
    put32(m, 2);
    for (int i = 0; i < 32; ++i)
        put32(m, uint32_t(0x770000 + 0x40 * i));
    m.resize(140, 0);
    auto header = [&](uint32_t nv, uint32_t np, int x, int y, int z) {
        put32(m, nv);
        put32(m, np);
        put32(m, uint32_t(x)), put32(m, uint32_t(y)), put32(m, uint32_t(z));
        m.resize(m.size() + 8, 0);
        for (uint32_t w : {0u, 0u, 1u, 0u, 1u, 0u})
            put32(m, w);
    };
    header(3, 1, 0, 0, 0);
    for (int v : {256, 0, 0, 0, 512, 0, 0, 0, -256})
        putS16(m, v);
    m.resize(m.size() + 6, 0);
    put32(m, 0xdead0085); // 4 frames, facing the other way, a triangle; the rest the program's
    m.insert(m.end(), {0, 1, 2, badVertex});
    putText(m, "a000", 4);
    header(2, 0, 65536, 32768, -131072);
    for (int v = 0; v < 6; ++v)
        putS16(m, 0);
    for (int i = 2; i < 32; ++i)
        header(0, 0, 0, 0, 0);
    return m;
}

void testCarModel() {
    nfs2se::CarModel model;
    CHECK(!nfs2se::parseCarModel(carModel(), model));
    CHECK(model.unknown == 2);
    const auto &p0 = model.parts[0];
    CHECK(p0.vertices.size() == 3 && p0.polygons.size() == 1);
    if (p0.vertices.size() == 3)
        CHECK(p0.vertices[0] == (nfs2se::Point{1.0f, 0.0f, 0.0f}) &&
              p0.vertices[2] == (nfs2se::Point{0.0f, 0.0f, -1.0f}));
    if (!p0.polygons.empty())
        CHECK(p0.polygons[0].flags == 0x85 && p0.polygons[0].frames() == 4 && p0.polygons[0].texture == "a000");
    CHECK(model.parts[1].position == (nfs2se::Point{1.0f, 0.5f, -2.0f}) && model.parts[1].vertices.size() == 2);
    CHECK(model.parts[31].vertices.empty());

    CHECK(nfs2se::parseCarModel(carModel(3), model)); // a vertex part 0 hasn't
    Bytes extra = carModel();
    extra.push_back(0);
    CHECK(nfs2se::parseCarModel(extra, model));
    Bytes cut = carModel();
    cut.resize(cut.size() - 20);
    CHECK(nfs2se::parseCarModel(cut, model));
}

void testMovie() {
    auto chunk = [](Bytes &out, const char *tag, const Bytes &body) {
        out.insert(out.end(), tag, tag + 4);
        put32(out, uint32_t(8 + body.size()));
        out.insert(out.end(), body.begin(), body.end());
    };
    // A video chunk: 0, the size, then a 16x16 MDEC frame of flat grey
    // (six blocks of DC 0 and their ends).
    auto video = [](int width, int height) {
        Bytes v;
        put32(v, 0);
        put16(v, unsigned(width));
        put16(v, unsigned(height));
        for (unsigned w : {5u, 0x3800u, 1u, 2u, 0x20u, 0x200u, 0x2002u, 0x20u, 0x200u})
            put16(v, w);
        return v;
    };
    // The sound: one block of 28 stereo EA-XA samples, a frame either side.
    Bytes dct;
    chunk(dct, "mTCD", video(16, 16));
    chunk(dct, "SCHl", ptHeader({{0x82, 2}, {0x83, 7}, {0x85, 28}}));
    Bytes count;
    put32(count, 1);
    chunk(dct, "SCCl", count);
    chunk(dct, "mTCD", video(16, 16));
    Bytes xa;
    put32(xa, 28);
    xa.insert(xa.end(), 8, 0);
    Bytes group(30, 0);
    group[1] = 0xcc;
    xa.insert(xa.end(), group.begin(), group.end());
    chunk(dct, "SCDl", xa);
    chunk(dct, "SCEl", {});

    nfs2se::DctMovie movie;
    CHECK(!movie.open(dct));
    CHECK(movie.frames().size() == 2 && movie.width() == 16 && movie.height() == 16);
    CHECK(movie.frames()[0].offset == 16 && movie.frames()[0].size == 18);
    CHECK(movie.hasAudio() && movie.framesPerSecond() == 2.0 * 22050 / 28);
    reluva::mdec::Decoder decoder;
    CHECK(!movie.decodeFrame(decoder, 1) && decoder.plane(0)[255] == 128);
    CHECK(movie.decodeFrame(decoder, 2));
    reluva::audio::Sound sound;
    CHECK(!movie.decodeAudio(sound) && sound.channels == 2 && sound.samples.size() == 56);

    Bytes silent;
    chunk(silent, "mTCD", video(16, 16));
    CHECK(!movie.open(silent) && !movie.hasAudio() && movie.framesPerSecond() == 15);
    CHECK(movie.decodeAudio(sound));

    Bytes sizes = silent;
    chunk(sizes, "mTCD", video(32, 16));
    CHECK(movie.open(sizes));
    Bytes cut = dct;
    cut.resize(cut.size() - 4);
    CHECK(movie.open(cut));
    Bytes noVideo;
    chunk(noVideo, "SCEl", {});
    CHECK(movie.open(noVideo));
}

} // namespace

static const reluva::test::Suite kSuite("nfs.unit.nfs2se",
                                        {
                                            {"track", [](const stdfs::path &) { testTrack(); }},
                                            {"collision", [](const stdfs::path &) { testCollision(); }},
                                            {"sky", [](const stdfs::path &) { testSky(); }},
                                            {"picture corners", [](const stdfs::path &) { testPictureCorners(); }},
                                            {"animation script", [](const stdfs::path &) { testAnimationScript(); }},
                                            {"paint", [](const stdfs::path &) { testPaint(); }},
                                            {"glide sky", [](const stdfs::path &) { testGlideSky(); }},
                                            {"car model", [](const stdfs::path &) { testCarModel(); }},
                                            {"movie", [](const stdfs::path &) { testMovie(); }},
                                        });
