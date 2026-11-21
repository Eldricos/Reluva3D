// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Levels: BSP models, placed actors, zone visibility through portals, and
// collision.
//
//   reluva-tests unreal.unit.levels

#include "tests/engines/unreal/support.h"

using namespace reluva;
using namespace reluva::unreal;
using namespace reluva::unreal::test_data;

namespace {

void testModel() {
    // One square floor polygon, 128 units across, at z = 0.
    PackageWriter w;
    const int32_t texClass = w.addImport("Engine", "Class", 0, "Texture");
    const int32_t modelClass = w.addImport("Engine", "Class", 0, "Model");
    const int32_t tex = w.addExport(texClass, 0, "Floor", {});
    // A light 64 units above the floor's corner, and the zone's ambient light.
    const int32_t lightClass = w.addImport("Engine", "Class", 0, "Light");
    const int32_t zoneClass = w.addImport("Engine", "Class", 0, "ZoneInfo");
    Bytes la;
    tag(la, w, "Location", PropertyType::kStruct, 12, 0, "Vector");
    la.vec(0, 0, 64);
    tag(la, w, "LightBrightness", PropertyType::kByte, 1);
    la.u8(128);
    tag(la, w, "LightRadius", PropertyType::kByte, 1);
    la.u8(9); // 250 units
    endProps(la, w);
    const int32_t light = w.addExport(lightClass, 0, "Light0", la.b);
    Bytes za;
    tag(za, w, "AmbientBrightness", PropertyType::kByte, 1);
    za.u8(16);
    endProps(za, w);
    const int32_t zone = w.addExport(zoneClass, 0, "ZoneInfo0", za.b);
    Bytes o;
    endProps(o, w);
    o.vec(0, 0, 0), o.vec(128, 128, 0), o.u8(1); // box
    o.vec(64, 64, 0), o.f32(90);                 // sphere
    o.index(3);                                  // vectors: normal, texture U, V
    o.vec(0, 0, 1), o.vec(1, 0, 0), o.vec(0, 1, 0);
    o.index(4); // points
    o.vec(0, 0, 0), o.vec(128, 0, 0), o.vec(128, 128, 0), o.vec(0, 128, 0);
    o.index(1); // nodes
    o.f32(0), o.f32(0), o.f32(1), o.f32(0);
    o.u64(1), o.u8(0);
    o.index(0), o.index(0), o.index(-1), o.index(-1), o.index(-1), o.index(-1), o.index(-1);
    o.u8(0), o.u8(0), o.u8(4), o.i32(-1), o.i32(-1);
    o.index(1); // surfs
    o.index(tex), o.u32(0), o.index(0), o.index(0), o.index(1), o.index(2), o.index(0), o.index(0);
    o.u16(16), o.u16(0), o.index(0);
    o.index(4); // verts
    for (int i = 0; i < 4; ++i)
        o.index(i), o.index(-1);
    o.i32(0), o.i32(1);
    // Lighting: the zone, no polys, one lightmap of 5x5 samples 32 units
    // apart (the floor's 128), one light's bits (all lit but the far
    // corner), no bounds, hulls or leaves, the light list, two flags.
    o.index(zone), o.u64(1), o.u64(1);
    o.index(0);
    o.index(1);
    o.i32(0), o.vec(0, 0, 0), o.index(5), o.index(5), o.f32(32), o.f32(32), o.i32(0);
    o.index(5);
    o.u8(0x1f), o.u8(0x1f), o.u8(0x1f), o.u8(0x1f), o.u8(0x0f);
    o.index(0), o.index(0), o.index(0);
    o.index(2), o.index(light), o.index(0);
    o.i32(0), o.i32(1);
    w.addExport(modelClass, 0, "Model1", o.b);
    // A brush's model, without nodes.
    Bytes brush;
    endProps(brush, w);
    for (int i = 0; i < 6; ++i)
        brush.f32(0);
    brush.u8(0);
    for (int i = 0; i < 4; ++i)
        brush.f32(0);
    for (int i = 0; i < 5; ++i)
        brush.index(0);
    brush.i32(0), brush.i32(0);
    w.addExport(modelClass, 0, "Model2", brush.b);

    Package p;
    CHECK(!p.openMemory(w.build(), "Level"));
    Model m;
    std::string err;
    CHECK(findLevelModel(p, &m, &err) == 3 && err.empty());
    CHECK(m.nodes.size() == 1 && m.surfs.size() == 1 && m.verts.size() == 4 && m.zoneCount == 1);
    const std::vector<Polygon> polys = levelPolygons(m);
    CHECK(polys.size() == 1);
    if (polys.size() == 1) {
        CHECK(polys[0].points.size() == 4 && polys[0].normal[2] == 1);
        CHECK(polys[0].texels[2][0] == 144 && polys[0].texels[2][1] == 128); // panned 16 in U
    }
    CHECK(m.hasLighting() && m.zones.size() == 1 && m.lightMaps.size() == 1 && m.lights.size() == 2 &&
          m.trailingBytes == 0);
    // The engine's colour: the hue in three straight thirds (channels
    // summing to 1), white by the saturation, the value through its curve
    // (0.821 at 255).
    auto near = [](const Vec3f &a, const Vec3f &b) {
        return std::fabs(a[0] - b[0]) < 1e-3f && std::fabs(a[1] - b[1]) < 1e-3f && std::fabs(a[2] - b[2]) < 1e-3f;
    };
    const float top = 0.98f / (std::sqrt(1.4f) + 0.01f);
    CHECK(near(engineHsv(0, 0, 255), Vec3f{top, 0, 0}) && near(engineHsv(0, 255, 255), Vec3f{top, top, top}));
    CHECK(near(engineHsv(42, 0, 255), Vec3f{43.0f / 85 * top, 42.0f / 85 * top, 0}));
    CHECK(near(engineHsv(128, 0, 255), Vec3f{0, 42.0f / 85 * top, 43.0f / 85 * top}));
    // A light: that at full value times brightness / 255, doubled.
    CHECK(near(lightColor(0, 0, 128), Vec3f{top * 256 / 255, 0, 0}));
    CHECK(near(lightColor(42, 255, 128, 0.5f), Vec3f{top * 128 / 255, top * 128 / 255, top * 128 / 255}));
    // Its falloff: 1 - 3 f^2 + 2 f^3.
    CHECK(lightFalloff(0) == 1 && lightFalloff(1) == 0 && std::fabs(lightFalloff(0.5f) - 0.5f) < 1e-6f);
    PackageSet set;
    const LevelLights lights = gatherLights(set, p, m);
    CHECK(lights.sources.size() == 2 && lights.unreadable == 0 && lights.sources[0].radius == 250);
    const Vec3f ambient = zoneAmbientColor(0, 255, 16);
    CHECK(lights.zoneAmbient.size() == 1 && near(lights.zoneAmbient[0], ambient));
    const float lit = ambient[0] + lights.sources[0].color[0] * lightFalloff(64.0f / 250);
    const int under = int(lit / 2 * 255 + 0.5f);
    const int shade = int(ambient[0] / 2 * 255 + 0.5f);
    const LightmapAtlas atlas = buildLightmaps(m, lights);
    CHECK(atlas.pages.size() == 1 && atlas.stats.lightmaps == 1 && atlas.stats.lit == 1);
    CHECK(atlas.stats.fromBounds == 0 && atlas.stats.badFit == 0);
    if (atlas.pages.size() == 1) {
        const auto uv = atlas.uv(0, {0, 0, 0});
        const auto far = atlas.uv(0, {128, 128, 0});
        CHECK(uv[0] * 1024 == 1.5f && uv[1] * 1024 == 1.5f && far[0] * 1024 == 5.5f);
        const image::Image &page = atlas.pages[0];
        // Under the light: ambient + its colour x the falloff, stored halved.
        CHECK(std::abs(int(page.pixel(1, 1)[0]) - under) <= 1);
        CHECK(std::abs(int(page.pixel(5, 5)[0]) - shade) <= 1); // in shadow: ambient alone
        CHECK(page.pixel(0, 0)[0] == page.pixel(1, 1)[0]);      // the border repeats the edge
        CHECK(page.pixel(3, 1)[0] < page.pixel(1, 1)[0]);       // further from the light, darker
    }
    // A light four times as bright: one light gives at most the texture's
    // own colours (1, stored halved), not brighter.
    LevelLights bright = lights;
    for (float &c : bright.sources[0].color)
        c *= 4;
    const LightmapAtlas capped = buildLightmaps(m, bright);
    if (capped.pages.size() == 1)
        CHECK(capped.pages[0].pixel(1, 1)[0] == 128 && std::abs(int(capped.pages[0].pixel(5, 5)[0]) - shade) <= 1);
    // The same light pulsing: its share kept apart, redrawn by its level.
    CHECK(lights.animated.empty() && atlas.animated.empty());
    LevelLights pulsing = lights;
    pulsing.sources[0].type = kLightPulse;
    pulsing.sources[0].animated = 0;
    pulsing.animated = {pulsing.sources[0]};
    const LightmapAtlas changing = buildLightmaps(m, pulsing);
    CHECK(changing.animated.size() == 1);
    if (changing.animated.size() == 1 && atlas.pages.size() == 1) {
        const AnimatedLightmap &a = changing.animated[0];
        CHECK(a.page == 0 && a.x == 0 && a.y == 0 && a.w == 7 && a.h == 7 && a.lights.size() == 1);
        std::vector<uint8_t> full;
        std::vector<uint8_t> off;
        a.draw({1.0f}, full);
        a.draw({0.0f}, off);
        CHECK(full.size() == 7 * 7 * 4 && full[(1 * 7 + 1) * 4] == atlas.pages[0].pixel(1, 1)[0]);
        CHECK(std::abs(int(off[(1 * 7 + 1) * 4]) - shade) <= 1); // out: the ambient light alone
        // Drawn into the page as at time 0: the pulse's 0.6.
        CHECK(std::abs(int(changing.pages[0].pixel(1, 1)[0]) - int(shade + (under - shade) * 0.6f)) <= 1);
    }
    LightSource l;
    l.type = kLightPulse;
    l.period = 2;
    CHECK(std::abs(lightLevel(l, 0.5f) - 0.99f) < 1e-4f && std::abs(lightLevel(l, 1.5f) - 0.21f) < 1e-4f);
    // Blink: on or out at random, frame by frame; strobe: on and out
    // alternate frames.
    l.type = kLightBlink;
    int on = 0;
    for (int f = 0; f < 350; ++f) {
        const float v = lightLevel(l, float(f) / 35 + 0.001f);
        CHECK(v == 0 || v == 1);
        on += v == 1;
    }
    CHECK(on > 100 && on < 250);
    l.type = kLightStrobe;
    CHECK(lightLevel(l, 0.001f) == 1 && lightLevel(l, 1.0f / 35 + 0.001f) == 0);
    l.type = kLightSteady;
    CHECK(lightLevel(l, 1.5f) == 1);
    // A model whose BSP doesn't hold together: refused, or read without it.
    {
        PackageWriter bad = w;
        // The node's vertex count (4) at its place in Model1's data: make
        // it 9, past the 4 vertices.
        std::vector<uint8_t> &d = bad.exports[3].data;
        const size_t nodesAt = 1 + 25 + 16 + 1 + 36 + 1 + 48 + 1 + 16 + 8 + 1 + 7;
        CHECK(d[nodesAt + 2] == 4);
        d[nodesAt + 2] = 9;
        Package pb;
        CHECK(!pb.openMemory(bad.build(), "Bad"));
        Model strict;
        Model lenient;
        CHECK(readModel(pb, 3, strict));
        CHECK(!readModel(pb, 3, lenient, false) && !lenient.geometryError.empty() && lenient.nodes.empty() &&
              lenient.hasLighting() && lenient.lightMaps.size() == 1);
    }
    // Invisible surfaces aren't drawn.
    m.surfs[0].polyFlags = polyflag::kInvisible;
    CHECK(levelPolygons(m).empty());
}

void testActors() {
    // Classes defined here: Brush, Mover extending it, ElevatorMover
    // extending that, SkyZoneInfo; a square brush polygon; an elevator
    // using it with two keyframes; a sky zone.
    PackageWriter w;
    const int32_t brushClass = w.addExport(0, 0, "Brush", {});
    const int32_t moverClass = w.addExport(0, 0, "Mover", {});
    w.exports.back().superRef = brushClass;
    const int32_t elevatorClass = w.addExport(0, 0, "ElevatorMover", {});
    w.exports.back().superRef = moverClass;
    const int32_t skyClass = w.addExport(0, 0, "SkyZoneInfo", {});
    const int32_t polysClass = w.addImport("Engine", "Class", 0, "Polys");
    const int32_t modelClass = w.addImport("Engine", "Class", 0, "Model");

    Bytes polys;
    endProps(polys, w);
    polys.i32(1), polys.i32(1);
    polys.index(4);
    polys.vec(0, 0, 0), polys.vec(0, 0, 1), polys.vec(1, 0, 0), polys.vec(0, 1, 0);
    polys.vec(0, 0, 0), polys.vec(64, 0, 0), polys.vec(64, 64, 0), polys.vec(0, 64, 0);
    polys.u32(0), polys.index(0), polys.index(0), polys.index(0), polys.index(0), polys.index(0);
    polys.u16(8), polys.u16(0);
    const int32_t polysRef = w.addExport(polysClass, 0, "Polys0", polys.b);

    Bytes model; // a brush's model: no geometry of its own, then its Polys
    endProps(model, w);
    for (int i = 0; i < 6; ++i)
        model.f32(0);
    model.u8(0);
    for (int i = 0; i < 4; ++i)
        model.f32(0);
    for (int i = 0; i < 5; ++i)
        model.index(0);
    model.i32(0), model.i32(0);
    model.index(polysRef);
    model.index(0), model.index(0), model.index(0), model.index(0), model.index(0), model.index(0);
    model.i32(0), model.i32(0);
    const int32_t modelRef = w.addExport(modelClass, 0, "Model5", model.b);

    Bytes mover;
    tag(mover, w, "Location", PropertyType::kStruct, 12, 0, "Vector");
    mover.vec(10, 20, 30);
    tag(mover, w, "KeyPos", PropertyType::kStruct, 12, 1, "Vector");
    mover.vec(0, 0, 64);
    tag(mover, w, "MoveTime", PropertyType::kFloat, 4);
    mover.f32(2);
    tag(mover, w, "Brush", PropertyType::kObject, 1);
    mover.index(modelRef);
    endProps(mover, w);
    w.addExport(elevatorClass, 0, "ElevatorMover0", mover.b);

    Bytes sky;
    tag(sky, w, "Location", PropertyType::kStruct, 12, 0, "Vector");
    sky.vec(1000, 2000, 3000);
    endProps(sky, w);
    w.addExport(skyClass, 0, "SkyZoneInfo0", sky.b);

    PackageSet set;
    set.addMemory("Level", ".unr", w.build());
    const Package *p = set.get("Level");
    CHECK(p != nullptr);
    if (!p)
        return;
    const int elevator = p->findExport("ElevatorMover0");
    const int skyZone = p->findExport("SkyZoneInfo0");
    CHECK(isA(set, *p, elevator, "mover") && isA(set, *p, elevator, "Brush") && !isA(set, *p, skyZone, "Mover"));

    std::vector<Poly> list;
    size_t trailing = 1;
    CHECK(!readPolys(*p, int(polysRef) - 1, list, &trailing) && trailing == 0);
    CHECK(list.size() == 1 && list[0].vertices.size() == 4 && list[0].normal[2] == 1 && list[0].panU == 8);

    std::vector<std::string> errors;
    const std::vector<Mover> movers = findMovers(set, *p, &errors);
    CHECK(errors.empty() && movers.size() == 1);
    if (movers.size() == 1) {
        const Mover &m = movers[0];
        CHECK(m.polys.size() == 1 && m.numKeys == 2 && m.keyNum == 0 && m.moveTime == 2);
        CHECK((m.basePosition() == Vec3f{10, 20, 30}) && (m.keyPos[1] == Vec3f{0, 0, 64}));
        CHECK((m.mainScale == Vec3f{1, 1, 1}));
    }
    const auto skyAt = findSkyZone(set, *p);
    CHECK(skyAt && (*skyAt == Vec3f{1000, 2000, 3000}));

    // Yaw a quarter turn: forward is +Y, right is -X.
    const auto axes = rotationAxes({0, 16384, 0});
    CHECK(std::abs(axes[0][1] - 1) < 1e-5f && std::abs(axes[1][0] + 1) < 1e-5f && std::abs(axes[2][2] - 1) < 1e-5f);
}

// A two-frame triangle mesh as the package's first export (its data at file
// offset 64, which its lazy arrays' end offsets need), classic or LOD.
// `importBlock`: as The Fallen stores them, the vertices not in their lazy
// array but in a block after the textures (1: one array of all frames'
// vertices; 2: one per frame).

void testZoneVisibility() {
    // One plane, x = 0, and on it a zone portal 100 square: zone 1 behind
    // (x < 0), zone 2 in front.
    Model m;
    BspNode n;
    n.plane = {1, 0, 0, 0};
    n.front = n.back = -1;
    n.zone[0] = 1;
    n.zone[1] = 2;
    n.leaf[0] = 0;
    n.leaf[1] = 1;
    n.vertexCount = 4;
    m.nodes.push_back(n);
    BspSurf s;
    s.polyFlags = polyflag::kPortal;
    m.surfs.push_back(s);
    for (const Vec3f &p : {Vec3f{0, -50, -50}, Vec3f{0, 50, -50}, Vec3f{0, 50, 50}, Vec3f{0, -50, 50}}) {
        m.verts.push_back({int32_t(m.points.size()), 0});
        m.points.push_back(p);
    }
    ZoneVisibility zones;
    zones.build(m);
    CHECK(zones.portalCount() == 1 && zones.zoneAt({-5, 0, 0}) == 1 && zones.zoneAt({5, 0, 0}) == 2);
    // A view from x = -200 along +x (screen x from y, up from z, depth
    // from x), then along -x.
    std::array<float, 16> ahead{};
    std::array<float, 16> behind{};
    ahead[4] = behind[4] = 1;
    ahead[9] = behind[9] = 1;
    ahead[3] = 1;
    ahead[15] = 200;
    behind[3] = -1;
    behind[15] = -200;
    CHECK(zones.visible({-200, 0, 0}, ahead) == 0b110);
    CHECK(zones.visible({-200, 0, 0}, behind) == 0b010);
    // Looking away but standing in the portal: the zone beyond fills half
    // the view, so it counts as seen.
    CHECK(zones.visible({-10, 0, 0}, behind) == 0b110);
    // Off to the side, the portal off screen: not seen.
    std::array<float, 16> aside = ahead;
    aside[12] = -1000;
    CHECK(zones.visible({-200, 1000, 0}, aside) == 0b010);
}

void testCollision() {
    // One plane, z = 0, facing up: open above, solid below.
    Model m;
    BspNode floor;
    floor.plane = {0, 0, 1, 0};
    floor.front = floor.back = -1;
    floor.vertexCount = 4; // a CSG node needs its polygon
    floor.collisionBound = 0;
    m.nodes.push_back(floor);
    m.rootOutside = false;
    // Its collision hull: its own plane, then a bounding box (as float bits).
    auto bits = [](float f) {
        int32_t i;
        std::memcpy(&i, &f, 4);
        return i;
    };
    auto setHull = [&](std::array<float, 6> box) {
        m.leafHulls = {0, -1};
        for (float f : box)
            m.leafHulls.push_back(bits(f));
    };
    setHull({-1000, -1000, -1000, 1000, 1000, 0});
    LevelCollision c(m);
    CHECK(c.solid(Vec3f{0, 0, -1}) && !c.solid(Vec3f{5, 5, 1}));
    CHECK(c.solid(Vec3f{0, 0, 5}, Vec3f{4, 4, 10}) && !c.solid(Vec3f{0, 0, 11}, Vec3f{4, 4, 10}));
    // A line down from 100 to -100 stops at the floor (a little short).
    LevelCollision::Hit h = c.trace({0, 0, 100}, {0, 0, -100});
    CHECK(h.hit() && std::abs(h.time - 0.5f) < 0.01f && h.normal[2] == 1 && h.location[2] > 0 && h.location[2] < 0.5f);
    // A box 10 high stops its half height above it.
    h = c.trace({0, 0, 100}, {0, 0, -100}, {4, 4, 10});
    CHECK(h.hit() && std::abs(h.location[2] - 10) < 0.5f);
    // Along it, nothing; from inside it, stuck at once.
    CHECK(!c.trace({0, 0, 20}, {100, 0, 20}, {4, 4, 10}).hit());
    CHECK(c.trace({0, 0, -5}, {0, 0, 50}).startSolid);
    // A non-solid brush's plane (0x01) divides nothing: both sides are
    // what the root is.
    m.nodes[0].flags = 0x01;
    CHECK(c.solid(Vec3f{0, 0, 1}) && c.solid(Vec3f{0, 0, -1}));
    m.rootOutside = true;
    CHECK(!c.solid(Vec3f{0, 0, -1}) && !c.trace({0, 0, 100}, {0, 0, -100}).hit());

    // A diagonal wall (x + y = 141.4 or so, open on the origin's side): a
    // box stopped by it is left clear of it, even coming in at a grazing
    // angle, and can then slide along it.
    const float s = std::sqrt(0.5f);
    m.nodes[0].plane = {-s, -s, 0, -100};
    m.nodes[0].flags = 0;
    setHull({-1000, -1000, -1000, 1000, 1000, 1000});
    m.rootOutside = false;
    const LevelCollision fresh(m); // its hulls are built once, so a new one for the new model
    const Vec3f box{17, 17, 47};
    for (const Vec3f &to : {Vec3f{200, 0, 0}, Vec3f{400, -280, 0}}) {
        h = fresh.trace({0, 0, 0}, to, box);
        CHECK(h.hit() && !h.startSolid && std::abs(h.normal[0] + s) < 1e-3f && !fresh.solid(h.location, box));
    }
    h = fresh.trace({0, 0, 0}, {200, 0, 0}, box);
    const Vec3f along{h.location[0] + 50, h.location[1] - 50, h.location[2]};
    CHECK(!fresh.trace(h.location, along, box).hit() && !fresh.solid(along, box));
}

} // namespace

static const reluva::test::Suite
    kSuite("unreal.unit.levels", {
                                     {"model", [](const std::filesystem::path &) { testModel(); }},
                                     {"actors", [](const std::filesystem::path &) { testActors(); }},
                                     {"zone visibility", [](const std::filesystem::path &) { testZoneVisibility(); }},
                                     {"collision", [](const std::filesystem::path &) { testCollision(); }},
                                 });
