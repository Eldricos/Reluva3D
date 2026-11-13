// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A scene: its camera, walk grid, conditions, sprites, particles, the pick,
// shadows and lighting.

#include "tests/engines/awe/fixtures.h"

#include <filesystem>

using namespace reluva;
using namespace reluva::awe;
using namespace reluva::awe::testing;

namespace {

void testCamera() {
    SceneCamera c;
    c.fovY = 0.645772f, c.nearPlane = 10, c.farPlane = 10000;
    const float pos[3] = {-8.14098f, 121.795f, 515.485f};
    const float ear[3] = {-0.232019f, -0.0174533f, 0};
    const float off[3] = {-10, 276, 990};
    for (int k = 0; k < 3; k++)
        c.position[k] = pos[k], c.elevAzimRoll[k] = ear[k], c.offset[k] = off[k];
    const float dllView[16] = {0.9998f, 0.0040f,  -0.0170f, 0, 0,       0.9732f,    0.2299f,     0,
                               0.0175f, -0.2299f, 0.9731f,  0, 9.1433f, -275.9854f, -1519.7399f, 1};
    const gfx::Mat4 v = c.view();
    for (int k = 0; k < 16; k++)
        CHECK(std::fabs(v[size_t(k)] - dllView[k]) < (k >= 12 ? 0.05f : 0.001f));
    // The DLL's window y counts from the bottom; these are from the top.
    const float points[3][5] = {{-51.637f, 0, -317.618f, 481.8f, 768 - 256.4f},
                                {-51.637f, 170, -317.618f, 481.2f, 768 - 359.8f},
                                {-325.8f, 0, 216.3f, 236.6f, 768 - 96.1f}};
    for (const auto &p : points) {
        float x = 0;
        float y = 0;
        CHECK(c.project(p, 1024, 768, x, y));
        CHECK(std::fabs(x - p[3]) < 0.5f && std::fabs(y - p[4]) < 0.5f);
    }
    // A model's up (MAX's +Z) is the world's +Y; its front (MAX's -Y) faces
    // +Z, turned by rotY.
    const gfx::Mat4 m = characterMatrix(10, 0, 20, 0);
    const gfx::Vec3 top = gfx::transformPoint(m, {0, 0, 100});
    CHECK(top[0] == 10 && top[1] == 100 && top[2] == 20);
    const gfx::Vec3 front = gfx::transformPoint(m, {0, -1, 0});
    CHECK(front[2] == 21);
    // A click finds the floor point that projects back to it.
    for (const auto &p : points) {
        if (p[1] != 0)
            continue;
        float x = 0;
        float z = 0;
        CHECK(c.floorAt(p[3], p[4], 1024, 768, x, z));
        CHECK(std::fabs(x - p[0]) < 2 && std::fabs(z - p[2]) < 2);
    }
    float x;
    float z;
    CHECK(!c.floorAt(512, 0, 1024, 768, x, z)); // above the horizon
}

void testWalkGrid() {
    // A 20-unit grid, blocked down the middle (a = 32) but for b = 50.
    Ac2Record camr;
    camr.tag = "CAMR";
    Ac2Value size;
    size.type = Ac2Value::Type::U16;
    size.raw = 20;
    Ac2Value grid;
    grid.type = Ac2Value::Type::Bytes;
    grid.raw = 64;
    grid.bytes.assign(64 * 64, 0);
    for (int b = 0; b < 64; b++)
        if (b != 50)
            grid.bytes[size_t(32 * 64 + b)] = 1;
    grid.bytes[size_t(10 * 64 + 10)] = 3;
    camr.fields = {{"cellSize", size}, {"grid", grid}};
    WalkGrid g;
    CHECK(g.load(camr) && g.cellSize() == 20);
    // The game's conversions: x = (a - 32) * 20 + 10, z = (32 - b) * 20 - 10.
    float x;
    float z;
    g.centre({31, 0}, x, z);
    CHECK(x == -10 && z == 630);
    WalkGrid::Cell c = g.cellAt(-10, 630);
    CHECK(c.a == 31 && c.b == 0);
    c = g.cellAt(0, 0); // on boundaries: the later of the two, on both
    CHECK(c.a == 32 && c.b == 32);
    CHECK(!g.cellAt(-700, 0).valid());
    CHECK(g.at(32, 5) == 1 && g.blocked({32, 5}, true) && !g.blocked({32, 50}, false));
    CHECK(g.blocked({10, 10}, false) && !g.blocked({10, 10}, true));
    // Across the wall: through its one gap, at b = 50.
    std::vector<WalkGrid::Cell> path = g.findPath({28, 10}, {36, 10});
    CHECK(path.size() >= 3 && path.front() == (WalkGrid::Cell{28, 10}) && path.back() == (WalkGrid::Cell{36, 10}));
    bool through = false;
    for (size_t i = 0; i + 1 < path.size(); i++)
        through |= (path[i].a <= 32) != (path[i + 1].a <= 32) && path[i].b >= 48 && path[i + 1].b >= 48;
    CHECK(through);
    for (const WalkGrid::Cell &p : path)
        CHECK(!g.blocked(p, false));
    CHECK(g.findPath({20, 20}, {21, 21}).empty()); // too near to walk
    // Open ground: straight there.
    path = g.findPath({40, 0}, {60, 30});
    CHECK(path.size() == 2);
    // Round the 3 the first try avoids: the search's cells are dropped
    // in order while a turn from the last one kept clears the next, so
    // the one beside it stays.
    path = g.findPath({0, 0}, {20, 30});
    CHECK(path.size() == 3 && path[1] == (WalkGrid::Cell{9, 10}));
    std::vector<WalkGrid::Point> points;
    CHECK(g.search({0, 0}, {20, 30}, false, false, points) == 2 && points[0].a == 20.5 && points[0].b == 30.5);
    CHECK(g.search({0, 0}, {20, 30}, true, false, points) == 1); // with 3s open, straight there
    CHECK(g.search({0, 0}, {32, 5}, false, false, points) == 0); // the goal blocked
}

void testConditions() {
    GameState st;
    st.values["moving"] = 1;
    st.act = "06b";
    st.tags = {{"bardraweropen", 0}, {"eavesdrop04a", 3}};
    st.items = {{"icetongs"}};
    CHECK(testCondition("", st));
    CHECK(!testCondition("0", st));
    CHECK(testCondition("moving >0", st));
    CHECK(!testCondition("weather >0", st));
    CHECK(testCondition("!master", st));
    CHECK(!testCondition("debug", st));
    CHECK(testCondition("act 06a| act 06b| act 07a", st));
    CHECK(testCondition("act 06a | act 06b | act 07a", st));
    CHECK(!testCondition("act 07a", st));
    CHECK(testCondition("!has icebucket, act >06a", st));
    CHECK(!testCondition("!has icetongs", st));
    CHECK(testCondition("tag BarDrawerOpen", st)); // set, even to 0
    CHECK(!testCondition("tag micheladmonish", st));
    CHECK(testCondition("tag eavesdrop04a >2, tag eavesdrop04a <4, tag eavesdrop04a 3", st));
    CHECK(!testCondition("tag eavesdrop04a 2", st));
    CHECK(!testCondition("!has sellotape, !tag bardraweropen", st));
    CHECK(testCondition("moving 1, act <07a", st));
    st.act.clear();
    CHECK(!testCondition("act 06a", st));
    CHECK(testCondition("!act 06a", st));
    st.act = "06b";

    // As 0x407ae0 has it: a part in brackets first; a '|' anywhere makes
    // any clause do; words it doesn't know don't hold.
    const char *eavesdrop = "tag eavesdrop04a, (use waterglass | tag bardraweropen nouse)";
    CHECK(testCondition(eavesdrop, st)); // nothing in hand
    st.held = "waterglass";
    CHECK(testCondition(eavesdrop, st));
    st.held = "icetongs";
    CHECK(!testCondition(eavesdrop, st));
    CHECK(testCondition("use icetongs", st) && testCondition("use", st) && !testCondition("use waterglass", st));
    st.held.clear();
    CHECK(testCondition("act 07a, tag bardraweropen | has nothing", st));
    CHECK(testCondition("1", st) && !testCondition("null", st) && testCondition("!null", st));
    CHECK(testCondition("tag missing <5", st) && !testCondition("tag missing 0", st)); // -99
    CHECK(!testCondition("act >06b", st) && testCondition("act >06a", st) && testCondition("act <07a", st));
    // The inventory: has, available, used, has all.
    st.items[0].state = 4;
    CHECK(testCondition("has icetongs", st) && !testCondition("has icetongs available", st));
    CHECK(testCondition("used icetongs", st));
    st.items[0].state = 2;
    CHECK(testCondition("has icetongs available", st) && !testCondition("used icetongs", st));
    CHECK(!testCondition("has all passport", st));
    // Names in the scene, the scene left and the location.
    st.scene = "calais.hall_02_04a.ac2";
    st.lastScene = "Locations\\Calais.16\\Calais.16_02.ac2";
    st.location = "calais.hall";
    CHECK(testCondition("scene Calais.Hall_02", st) && testCondition("lastscene calais.16_02", st));
    CHECK(testCondition("location calais.hall", st) && !testCondition("location athens", st));
    // cantalk asks the dialogue.
    CHECK(!testCondition("cantalk dia_test a+", st));
    st.canTalk = [](const std::vector<std::string> &w) { return w.size() == 2 && w[0] == "dia_test"; };
    CHECK(testCondition("cantalk dia_test a+", st) && testCondition("!cantalk dia_other a", st));
}

void testSprite() {
    // Salon_01's left window: movingbg02.bik mirrored and turned -40
    // degrees about its top-left corner.
    SpriteTransform t;
    t.x = 100, t.y = 54, t.scaleX = -1.5f, t.scaleY = 1.5f, t.roll = -0.698132f;
    float x;
    float y;
    t.apply(0, 0, x, y);
    CHECK(x == 100 && y == 54);
    t.apply(256, 256, x, y);
    CHECK(std::fabs(x - 53.1f) < 0.5f && std::fabs(y - 595.0f) < 0.5f);
    // Drawn: a 2x2 picture scaled by 2 covers 4x4 pixels from (1, 1).
    image::Image img;
    img.w = img.h = 2;
    img.rgba.assign(16, 255);
    Surface dst;
    dst.create(8, 8, PixelFormat::kRGBA32);
    std::fill(dst.pixels.begin(), dst.pixels.end(), 0);
    SpriteTransform s2;
    s2.x = 1, s2.y = 1, s2.scaleX = 2, s2.scaleY = 2;
    drawSprite(dst, img, s2, true);
    CHECK(dst.row(1)[1 * 4 + 3] == 255 && dst.row(4)[4 * 4] == 255);
    CHECK(dst.row(0)[0 * 4 + 3] == 0 && dst.row(5)[5 * 4 + 3] == 0);
}

Ac2Value realValue(float f) {
    Ac2Value v;
    v.type = Ac2Value::Type::F32;
    std::memcpy(&v.raw, &f, 4);
    return v;
}

void testParticles() {
    // A SNOW record with a few fields: the rest are the class's defaults.
    Ac2Record snow;
    snow.tag = "SNOW";
    snow.fields = {{"x", realValue(-730)}, {"y", realValue(300)}, {"z", realValue(-330)}, {"depth", realValue(-10500)}};
    Ac2Record syst;
    syst.tag = "SYST";
    syst.fields = {{"velocityY", realValue(-300)}};
    snow.parts.push_back(syst);
    ParticleSettings s = ParticleSettings::fromRecord(snow);
    CHECK(s.mode == 2 && s.alphaBlend && s.condition == "weather >0");
    CHECK(s.velocity[1] == -300 && s.burst == 5 && s.spread == 2000 && s.size == 30);
    CHECK(s.depth == -10500 && s.position[1] == 300 && s.sway == 20);

    // Falling: a burst at once, then down at 300 a second, gone below 0.
    s.spread = 10;
    s.sway = 0;
    ParticleSystem fall(s);
    fall.update(0.01f);
    CHECK(fall.count() == 5);
    std::vector<ParticleSystem::Point> pts;
    fall.points(pts);
    CHECK(pts.size() == 5 && pts[0].position[1] == 300);
    CHECK(pts[0].position[0] >= -740 && pts[0].position[0] < -720 && pts[0].rgba[3] == 255);
    s.interval = 100; // one burst only
    ParticleSystem once(s);
    once.update(0.01f);
    once.update(0.5f);
    pts.clear();
    once.points(pts);
    CHECK(pts.size() == 5 && std::fabs(pts[0].position[1] - 150) < 0.01f);
    once.update(0.6f);
    CHECK(once.count() == 0);

    // Thrown straight up (mode 0, no spread across): gravity brings it back.
    ParticleSettings t;
    t.mode = 0;
    t.velocity[0] = t.velocity[2] = 0;
    t.velocity[1] = 100;
    t.gravity = 100;
    t.signY = 1;
    t.burst = 1;
    t.interval = 100;
    t.position[1] = 10;
    t.bounces = 0;
    t.fadeIn = 1;
    ParticleSystem up(t);
    up.update(0.001f);
    CHECK(up.count() == 1);
    up.update(0.5f);
    pts.clear();
    up.points(pts);
    // y = 10 + v*t - 50*t^2, v in [50, 100]: between 10 + 25 - 12.5 and 10 + 50 - 12.5.
    CHECK(pts.size() == 1 && pts[0].position[1] > 22 && pts[0].position[1] < 48);
    CHECK(pts[0].rgba[3] > 120 && pts[0].rgba[3] < 135); // half faded in
    up.update(2.5f);
    CHECK(up.count() == 0); // landed with no bounces left

    // A breath: the emitter moved and its velocity changed as it goes.
    ParticleSettings b;
    b.mode = 0, b.velocity[1] = 0, b.gravity = 0, b.burst = 1, b.interval = 100, b.spread = 1, b.sway = 0;
    ParticleSystem breath(b);
    const float mouth[3] = {100, 160, -50};
    const float ahead[3] = {0, 0, 50};
    breath.setEmitter(mouth, ahead);
    breath.update(0.001f);
    breath.update(1);
    pts.clear();
    breath.points(pts);
    CHECK(pts.size() == 1 && pts[0].position[1] == 160 && std::fabs(pts[0].position[0] - 100) <= 1);
    CHECK(std::fabs(pts[0].position[2] + 50) <= 1 + 50 * 0.7072f);
}

void testPicking() {
    // Front to back by depth; the later of equal ones first.
    std::vector<PickCandidate> c(3);
    c[0].depth = -10000; // the background: no outline, no rectangle
    c[1].depth = 5000, c[1].rect = true, c[1].clickable = true, c[1].hit = true; // a hotspot
    c[2].depth = 5000, c[2].rect = true, c[2].hit = true;                        // another, after it
    CHECK(pick(c, false) == 2);
    c[2].depth = 260;
    CHECK(pick(c, false) == 1);
    c[1].hit = c[2].hit = false;
    CHECK(pick(c, false) == -1);
    // A character in front of nothing but behind a hotspot: the characters
    // come first, those with neither rectangle nor outline by their bones'
    // boxes, if they can be clicked or an item is in hand.
    PickCandidate npc;
    npc.depth = 400, npc.character = true, npc.clickable = true;
    c[1].hit = true;
    c.push_back(npc);
    CHECK(pick(c, false) == 1); // bones missed: it's behind the hotspot's rectangle
    c[3].boneHit = true;
    CHECK(pick(c, false) == 3);
    c[3].clickable = false;
    CHECK(pick(c, false) == 1);
    CHECK(pick(c, true) == 3); // an item in hand: tried all the same
    // A character with a rectangle (or an outline) goes by it, not its bones.
    c[3].rect = true, c[3].boneHit = true, c[3].hit = false;
    CHECK(pick(c, false) == 1);
    c[3].hit = true;
    CHECK(pick(c, false) == 3);
    // pickFirst objects with a rectangle or an outline come before even
    // the characters.
    PickCandidate first;
    first.depth = -100, first.rect = true, first.pickFirst = true, first.hit = true;
    c.push_back(first);
    CHECK(pick(c, false) == 4);
    c[4].rect = false, c[4].outline = true;
    CHECK(pick(c, false) == 4);

    // Outlines: the pixel's centre against the polygon, by winding.
    const std::vector<std::pair<uint32_t, uint32_t>> square = {{10, 10}, {20, 10}, {20, 20}, {10, 20}};
    CHECK(inPolygon(square, 10, 10) && inPolygon(square, 19, 19) && !inPolygon(square, 20, 15));
    CHECK(!inPolygon(square, 9, 15) && !inPolygon({{0, 0}, {5, 5}}, 1, 1));
    // A star drawn in one stroke: its middle is in, by winding.
    const std::vector<std::pair<uint32_t, uint32_t>> star = {{50, 0}, {79, 90}, {2, 35}, {98, 35}, {21, 90}};
    CHECK(inPolygon(star, 50, 50) && inPolygon(star, 50, 10) && !inPolygon(star, 5, 80));

    // A ray against a box turned and moved: a bone's.
    gfx::Mat4 m = gfx::identity();
    m[0] = 0, m[1] = 1, m[4] = -1, m[5] = 0; // 90 degrees about z
    m[12] = 100, m[13] = 0, m[14] = 0;
    // along the bone's x: the world's +y
    const float lo[3] = {0, -5, -5};
    const float hi[3] = {50, 5, 5};
    CHECK(rayHitsBox(m, lo, hi, {100, 25, -100}, {0, 0, 1}));
    CHECK(!rayHitsBox(m, lo, hi, {100, 60, -100}, {0, 0, 1}));
    CHECK(!rayHitsBox(m, lo, hi, {100, 25, -100}, {0, 0, -1})); // behind the ray's start
    CHECK(!rayHitsBox(m, lo, hi, {120, 25, -100}, {0, 0, 1}));

    // The camera's ray through a pixel comes down where floorAt says.
    SceneCamera cam;
    cam.fovY = 0.645772f, cam.nearPlane = 10, cam.farPlane = 10000;
    cam.position[1] = 120, cam.elevAzimRoll[0] = -0.23f, cam.offset[1] = 276, cam.offset[2] = 990;
    gfx::Vec3 from;
    gfx::Vec3 dir;
    cam.ray(400, 600, 1024, 768, from, dir);
    float x = 0;
    float z = 0;
    CHECK(cam.floorAt(400, 600, 1024, 768, x, z));
    const float k = -from[1] / dir[1];
    CHECK(std::fabs(from[0] + k * dir[0] - x) < 0.01f && std::fabs(from[2] + k * dir[2] - z) < 0.01f);
}

void testShadows() {
    // The light: the lights' mean, raised by 1500; only those marked
    // shadowOnly if any are.
    Ac2Scene scene;
    auto light = [](float x, float y, float z, uint32_t marked) {
        Ac2Record r{"LITE", {}, {}};
        auto f = [](float v) {
            Ac2Value a;
            a.type = Ac2Value::Type::F32;
            std::memcpy(&a.raw, &v, 4);
            return a;
        };
        r.fields = {{"x", f(x)}, {"y", f(y)}, {"z", f(z)}};
        Ac2Value m;
        m.raw = marked;
        r.fields.push_back({"shadowOnly", m});
        return r;
    };
    scene.records = {light(5, 655, 450, 0), light(-19, 558, -330, 0)};
    std::array<float, 3> l = shadowLight(scene);
    CHECK(std::abs(l[0] + 7) < 1e-3f && std::abs(l[1] - 2106.5f) < 1e-3f && std::abs(l[2] - 60) < 1e-3f);
    scene.records.push_back(light(100, 500, 0, 1));
    l = shadowLight(scene);
    CHECK(l[0] == 100 && l[1] == 2000 && l[2] == 0);
    CHECK(shadowLight(Ac2Scene())[1] == 1500);
    // Flattened onto the floor along the line from the light.
    const float head[3] = {100, 1000, 0};
    const float foot[3] = {30, 0, 40};
    const std::array<float, 3> p = flattenOntoFloor({0, 2000, 0}, head);
    CHECK(std::abs(p[0] - 200) < 1e-3f && p[1] == 0 && p[2] == 0);
    const std::array<float, 3> q = flattenOntoFloor({0, 2000, 0}, foot);
    CHECK(q[0] == 30 && q[2] == 40);
    // The mask: pixel centres inside, either winding, its box kept.
    CoverageMask m(16, 16);
    const float a[2] = {0, 0};
    const float b[2] = {8, 0};
    const float c[2] = {0, 8};
    m.fillTriangle(a, c, b);
    CHECK(m.at(1, 1) && m.at(6, 0) && !m.at(7, 7) && !m.at(9, 1));
    CHECK(m.minX() == 0 && m.minY() == 0 && m.maxX() <= 8 && !m.empty());
    m.clear();
    CHECK(m.empty() && !m.at(1, 1));
    // A rigid mesh's: the light moved by its translation (added to the
    // sum, not the count), flattened onto the plane at its height.
    scene.records.pop_back();
    const float origin[3] = {10, 100, 20};
    l = shadowLight(scene, origin);
    CHECK(std::abs(l[0] + 2) < 1e-3f && std::abs(l[1] - 2156.5f) < 1e-3f && std::abs(l[2] - 70) < 1e-3f);
    const std::array<float, 3> r = flattenOntoPlane({0, 2000, 0}, 100, head);
    CHECK(std::abs(r[0] - 190) < 1e-3f && r[1] == 100 && r[2] == 0);
    // The stencil: depth-tested, shadows writing no depth, bodies their
    // own value and depth, pictures taking it back to 0.
    ShadowStencil st(8, 8);
    auto rect = [&](float x0, float x1, float z, uint8_t ref, bool depth) {
        const float a0[3] = {x0, 0, z};
        const float a1[3] = {x1, 0, z};
        const float a2[3] = {x1, 8, z};
        const float a3[3] = {x0, 8, z};
        st.fillTriangle(a0, a1, a2, ref, depth);
        st.fillTriangle(a0, a2, a3, ref, depth);
    };
    rect(0, 4, 0.2f, 0, true);  // an earlier character's body, on the left
    rect(0, 8, 0.5f, 1, false); // a later shadow, further away
    CHECK(st.at(1, 1) == 0 && st.at(6, 1) == 1);
    rect(0, 8, 0.1f, 1, false); // nearer: on that body
    CHECK(st.at(1, 1) == 1);
    rect(6, 8, 0.3f, 2, true); // the player's body
    CHECK(st.at(7, 1) == 2 && st.at(5, 1) == 1);
    std::vector<uint8_t> cover(64, 0);
    for (int i = 0; i < 32; i++)
        cover[size_t(i)] = 1; // a picture over the top half
    st.reset(cover);
    CHECK(st.at(5, 1) == 0 && st.at(5, 6) == 1 && st.at(7, 6) == 2);
    CoverageMask shadows(8, 8);
    st.select(1, shadows);
    CHECK(shadows.at(5, 6) && shadows.at(1, 6) && !shadows.at(7, 6) && !shadows.at(5, 1));
    st.clear();
    CHECK(st.at(5, 6) == 0);
}

void testLighting() {
    auto num = [](float v) {
        Ac2Value a;
        a.type = Ac2Value::Type::F32;
        std::memcpy(&a.raw, &v, 4);
        return a;
    };
    auto whole = [](uint32_t v) {
        Ac2Value a;
        a.raw = v;
        return a;
    };
    Ac2Scene scene;
    scene.records.push_back(
        {"CAMR", {{"ambientR", num(0.309804f)}, {"ambientG", num(0.5f)}, {"ambientB", num(1)}}, {}});
    // A directional light straight down, a point light out of range, and
    // two that don't light: off, and only for the shadows.
    auto light = [&](uint32_t type, float y, float diffuse) {
        return Ac2Record{"LITE",
                         {{"type", whole(type)},
                          {"y", num(y)},
                          {"dirX", num(0)},
                          {"dirY", num(-2)},
                          {"dirZ", num(0)},
                          {"diffuseR", num(diffuse)},
                          {"diffuseG", num(diffuse)},
                          {"diffuseB", num(diffuse)},
                          {"range", num(50)},
                          {"att0", num(1)},
                          {"att1", num(0)},
                          {"att2", num(0)}},
                         {}};
    };
    scene.records.push_back(light(3, 0, 0.5f));
    scene.records.push_back(light(1, 100, 1));
    scene.records.push_back(light(3, 0, 1));
    scene.records.back().fields.push_back({"on", whole(0)});
    scene.records.push_back(light(3, 0, 1));
    scene.records.back().fields.push_back({"shadowOnly", whole(1)});
    GameState state;
    SceneLighting lit = sceneLighting(scene, state);
    CHECK(lit.lights.size() == 2 && std::abs(lit.ambient[0] - 79 / 255.0f) < 1e-6f && lit.ambient[1] == 127 / 255.0f);
    Material material;
    material.ambient = {0.5f, 0.5f, 0.5f};
    const float origin[3] = {0, 0, 0};
    const float up[3] = {0, 1, 0};
    const float down[3] = {0, -1, 0};
    Rgb c = lightVertex(lit, material, origin, up);
    CHECK(std::abs(c[0] - (79 / 510.0f + 0.5f)) < 1e-5f && c[2] == 1);
    c = lightVertex(lit, material, origin, down);
    CHECK(std::abs(c[0] - 79 / 510.0f) < 1e-5f);
    // The point light in range: 1 / att0 of it.
    const float near[3] = {0, 60, 0};
    c = lightVertex(lit, material, near, up);
    CHECK(std::abs(c[0] - (79 / 510.0f + 0.5f + 1)) < 1e-5f || c[0] == 1);
    // The specular: the light overhead seen from above, so H is the normal;
    // none on the side away from the light.
    lit.eye = {0, 1000, 0};
    lit.lights[0].specular = {0.1f, 0.1f, 0.1f};
    material.specular = {1, 1, 1};
    material.power = 5;
    Rgb spec{};
    lightVertex(lit, material, origin, up, &spec);
    CHECK(std::abs(spec[0] - 0.1f) < 1e-5f);
    lightVertex(lit, material, origin, down, &spec);
    CHECK(spec[0] == 0);
    // Conditions as they were on entering: a light whose condition didn't
    // hold then stays off.
    std::vector<bool> entered = lightConditions(scene, state);
    const std::vector<const Ac2Record *> objects = scene.objects();
    for (size_t i = 0; i < objects.size(); i++)
        if (objects[i]->tag == "LITE") {
            entered[i] = false;
            break;
        }
    CHECK(sceneLighting(scene, state, entered).lights.size() == 1);
    // At night outdoors: no lights, a blue ambient.
    scene.records.push_back({"OUTD", {{"value", whole(1)}}, {}});
    state.values["time"] = 1;
    lit = sceneLighting(scene, state);
    CHECK(lit.lights.empty() && lit.ambient[0] == 69 / 255.0f && lit.ambient[2] == 139 / 255.0f);
    CHECK(!nightLighting(scene, 0));
    // The material from a character's fields.
    const Ac2Record npc{"NPCX", {{"ambientR", num(0.498039f)}, {"emissiveB", num(0.2f)}}, {}};
    material = materialOf(npc);
    CHECK(material.ambient[0] == 0.498039f && material.diffuse[1] == 1 && material.emissive[2] == 0.2f && material.lit);
    CHECK(materialOf(Ac2Record{"NPCX", {{"specularG", num(1)}, {"power", num(5)}}, {}}).power == 5);
}

} // namespace

static const reluva::test::Suite suite("awe.unit.scene",
                                       {
                                           {"camera", [](const std::filesystem::path &) { testCamera(); }},
                                           {"walk grid", [](const std::filesystem::path &) { testWalkGrid(); }},
                                           {"conditions", [](const std::filesystem::path &) { testConditions(); }},
                                           {"sprites", [](const std::filesystem::path &) { testSprite(); }},
                                           {"particles", [](const std::filesystem::path &) { testParticles(); }},
                                           {"picking", [](const std::filesystem::path &) { testPicking(); }},
                                           {"shadows", [](const std::filesystem::path &) { testShadows(); }},
                                           {"lighting", [](const std::filesystem::path &) { testLighting(); }},
                                       });
