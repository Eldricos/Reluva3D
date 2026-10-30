// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: Collision, the server's world, area portals and marks.

#include "common/fs.h"
#include "engines/idtech3/bindings.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/client/player_input.h"
#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_library.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_info.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/server/server_world.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/lexer.h"
#include "engines/idtech3/marks.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/wire/message.h"
#include "engines/idtech3/wire/netchan.h"
#include "engines/idtech3/patch.h"
#include "engines/idtech3/player_animation.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/server_browser.h"
#include "backends/sockets/socket_net.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/raven/ambient_sets.h"
#include "engines/idtech3/sound_system.h"
#include "engines/idtech3/sounds.h"
#include "engines/idtech3/pages/page.h"
#include "common/audio/wav.h"
#include "common/md4.h"
#include "common/zip.h"
#include "engines/registry.h"
#include "tests/engines/idtech3/data/qvm/fake_cgame_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_game_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_ui_qvm.h"
#include "tests/engines/idtech3/data/qvm/test_vm_qvm.h"
#include "engines/idtech3/vm_services.h"
#include "tests/engines/idtech3/fixtures.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <memory>
#include <thread>
#include <chrono>
#include <set>
#include <string>

using namespace reluva;
using namespace reluva::idtech3;
namespace q3 = reluva::idtech3;
using namespace reluva::idtech3::fixtures;

// The server's world: what its tree finds is what looking at every linked
// entity finds, boxes and moves alike, in the same order.
static void testServerWorld() {
    using namespace reluva::idtech3;
    std::map<int32_t, EntityShape> shapes;
    ServerWorld world([&](int32_t n, EntityShape& out) {
        const auto it = shapes.find(n);
        if (it == shapes.end())
            return false;
        out = it->second;
        return true;
    });
    Bsp map; // no brushes: only its bounds, for the tree
    map.models.push_back({});
    map.models[0].mins = {-1024, -1024, -256};
    map.models[0].maxs = {1024, 1024, 256};
    world.setLevel(&map, nullptr, nullptr);
    std::mt19937 rng(7);
    auto r = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
    for (int32_t n = 0; n < 200; ++n) {
        EntityShape s;
        s.origin = {r(-1000, 1000), r(-1000, 1000), r(-200, 200)};
        const float size = r(4, n % 10 == 0 ? 600 : 40);
        s.mins = {-size, -size, -24};
        s.maxs = {size, size, 32};
        s.contents = n % 3 ? 1 : 0x2000000;
        s.owner = n % 7 == 0 ? n - 1 : -1;
        shapes[n] = s;
        Vec3f lo{}, hi{};
        world.link(n, s, lo, hi);
        CHECK(lo[0] == s.origin[0] + s.mins[0] - 1 && hi[2] == s.origin[2] + s.maxs[2] + 1);
    }
    for (int32_t n = 0; n < 200; n += 9) {
        world.unlink(n);
        shapes.erase(n);
    }
    auto touches = [&](int32_t n, const Vec3f& lo, const Vec3f& hi) {
        const ServerWorld::Placement* p = world.placement(n);
        for (int i = 0; i < 3; ++i)
            if (p->absMin[size_t(i)] > hi[size_t(i)] || p->absMax[size_t(i)] < lo[size_t(i)])
                return false;
        return true;
    };
    bool boxesAgree = true, movesAgree = true, pointsAgree = true;
    for (int k = 0; k < 300; ++k) {
        const Vec3f lo{r(-1100, 1000), r(-1100, 1000), r(-300, 200)};
        const Vec3f hi{lo[0] + r(0, 300), lo[1] + r(0, 300), lo[2] + r(0, 100)};
        std::vector<int32_t> found, expected;
        world.entitiesInBox(lo, hi, found);
        for (const int32_t n : world.linked())
            if (touches(n, lo, hi))
                expected.push_back(n);
        boxesAgree &= found == expected;
        // A move: the nearest entity it meets, as by trying every one.
        const Vec3f a{r(-1000, 1000), r(-1000, 1000), 0}, b{r(-1000, 1000), r(-1000, 1000), 0};
        const Vec3f mins{-16, -16, -24}, maxs{16, 16, 32};
        const int32_t pass = int32_t(rng() % 200);
        int32_t hit = 0;
        const TraceResult t = world.trace(a, mins, maxs, b, pass, 1, hit);
        float bestFraction = 1;
        int32_t bestHit = ServerWorld::kNone;
        for (const int32_t n : world.linked()) {
            const EntityShape& s = shapes[n];
            if (n == pass || s.owner == pass || (shapes.count(pass) && shapes[pass].owner == n) || !(s.contents & 1))
                continue;
            Vec3f boxMin = s.mins, boxMax = s.maxs;
            for (int i = 0; i < 3; ++i) {
                boxMin[size_t(i)] += s.origin[size_t(i)];
                boxMax[size_t(i)] += s.origin[size_t(i)];
            }
            const TraceResult e = CollisionWorld::traceBox(a, b, mins, maxs, boxMin, boxMax, s.contents);
            if (e.startSolid || e.allSolid) {
                bestHit = n;
                if (e.allSolid)
                    break;
            }
            if (e.fraction < bestFraction) {
                bestFraction = e.fraction;
                bestHit = n;
            }
        }
        movesAgree &= t.fraction == bestFraction && hit == bestHit;
        int32_t contents = 0;
        for (const int32_t n : world.linked()) {
            const EntityShape& s = shapes[n];
            bool inside = true;
            for (int i = 0; i < 3; ++i)
                inside &= a[size_t(i)] >= s.origin[size_t(i)] + s.mins[size_t(i)] &&
                          a[size_t(i)] <= s.origin[size_t(i)] + s.maxs[size_t(i)];
            if (inside && n != pass)
                contents |= s.contents;
        }
        pointsAgree &= world.pointContents(a, pass) == contents;
    }
    CHECK(boxesAgree);
    CHECK(movesAgree);
    CHECK(pointsAgree);
    // The size the client game is told: height above (+32), depth below
    // and half width, a byte each; a brush model's marker; nothing for
    // what isn't solid.
    EntityShape box;
    box.mins = {-15, -15, -24};
    box.maxs = {15, 15, 32};
    box.contents = 1;
    CHECK(ServerWorld::packedSolid(box, 0xffffff) == (64 << 16 | 24 << 8 | 15));
    box.bmodel = true;
    CHECK(ServerWorld::packedSolid(box, 0xffffff) == 0xffffff);
    box.bmodel = false;
    box.contents = 0x40000000;
    CHECK(ServerWorld::packedSolid(box, 0xffffff) == 0);
}

static void testPatchCollision() {
    using namespace reluva::idtech3;
    // One patch, 3x3 control points over x and y from -64 to 64: flat at
    // z = 0, or (bent) its middle row raised to 64, an arch along y.
    for (bool bent : {false, true}) {
        Bsp bsp;
        bsp.shaders.push_back({"curve", 0, contents::Solid});
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) {
                BspVertex v;
                v.pos = {float(x - 1) * 64, float(y - 1) * 64, bent && y == 1 ? 64.0f : 0.0f};
                bsp.vertices.push_back(v);
            }
        BspSurface surf;
        surf.type = SurfaceType::Patch;
        surf.numVertices = 9;
        surf.patchWidth = surf.patchHeight = 3;
        bsp.surfaces.push_back(surf);
        BspModel model;
        model.numSurfaces = 1;
        bsp.models.push_back(model);
        CollisionWorld world(bsp);
        const Vec3f mins{-15, -15, -24}, maxs{15, 15, 32};
        // Dropped onto its middle: stopped with the box's bottom on it.
        TraceResult t = world.trace({0, 0, 200}, {0, 0, -200}, mins, maxs, 0, contents::Solid);
        const float top = bent ? 32.0f : 0.0f; // the arch's height there: half the raised row's
        CHECK(t.fraction < 1 && !t.startSolid && std::fabs(t.endPos[2] - (top + 24.5f)) < 1);
        CHECK(t.normal[2] > 0.9f);
        // From below, too: both sides stop.
        t = world.trace({0, 0, -200}, {0, 0, 200}, {}, {}, 0, contents::Solid);
        CHECK(t.fraction < 1 && std::fabs(t.endPos[2] - (top - 0.5f)) < 1);
        // A point passing beside it isn't stopped, nor other contents.
        CHECK(world.trace({100, 0, 200}, {100, 0, -200}, {}, {}, 0, contents::Solid).fraction == 1);
        CHECK(world.trace({0, 0, 200}, {0, 0, -200}, {}, {}, 0, contents::Water).fraction == 1);
        if (bent) {
            // Walking into the arch's slope from the side: stopped, the
            // normal leaning back towards the walker.
            t = world.trace({0, -100, 8}, {0, 100, 8}, {}, {}, 0, contents::Solid);
            CHECK(t.fraction < 1 && t.normal[1] < -0.3f);
        }
    }
}

static void testCollision() {
    const auto bsp = collisionMap();
    const CollisionWorld world(*bsp);
    const Vec3f zero{};
    // A point falling onto the floor stops an eighth above it.
    TraceResult t = world.trace({0, 0, 100}, {0, 0, -100}, zero, zero, 0, contents::Solid);
    CHECK(!t.startSolid && near(t.endPos[2], 0.125f) && near(t.fraction, 99.875f / 200));
    CHECK(t.normal[2] == 1 && t.contents == contents::Solid && near(t.dist, 0));
    // A player's box (feet 24 below its origin) stops with its feet there.
    t = world.trace({0, 0, 100}, {0, 0, -100}, {-15, -15, -24}, {15, 15, 32}, 0, contents::Solid);
    CHECK(near(t.endPos[2], 24.125f));
    // Nothing beyond the floor's edge; nothing for a mask without solid.
    CHECK(world.trace({1000, 0, 100}, {1000, 0, -100}, zero, zero, 0, contents::Solid).fraction == 1);
    CHECK(world.trace({0, 0, 100}, {0, 0, -100}, zero, zero, 0, contents::Water).fraction == 1);
    // Starting inside: startSolid; staying inside: allSolid.
    t = world.trace({0, 0, -10}, {0, 0, -1000}, zero, zero, 0, contents::Solid);
    CHECK(t.startSolid && !t.allSolid);
    t = world.trace({0, 0, -10}, {0, 0, -20}, zero, zero, 0, contents::Solid);
    CHECK(t.startSolid && t.allSolid && t.fraction == 0);
    // Sideways into the door (model 1, in its own coordinates).
    t = world.trace({-100, 0, 64}, {100, 0, 64}, zero, zero, 1, contents::Solid);
    CHECK(near(t.endPos[0], -8.125f) && t.normal[0] == -1);
    CHECK(world.pointContents({0, 0, -32}, 0) == contents::Solid && world.pointContents({0, 0, 32}, 0) == 0);
    CHECK(world.pointContents({0, 0, 64}, 1) == contents::Solid);
    Vec3f mins, maxs;
    CHECK(world.modelBounds(1, mins, maxs) && maxs[2] == 128 && !world.modelBounds(2, mins, maxs));
    // The door standing at x = 200 turned a quarter (yaw 90): it now lies
    // across x (136 to 264) and is thin along y (-8 to 8). A move along y
    // at x = 240 hits it, the plane turned back to the world's; one along
    // x at y = 40 misses, where the unturned door would have stopped it.
    const Vec3f at{200, 0, 0}, quarter{0, 90, 0};
    t = world.transformedTrace({240, -100, 64}, {240, 100, 64}, zero, zero, 1, contents::Solid, at, quarter);
    CHECK(t.fraction < 1 && near(t.endPos[1], -8.125f) && near(t.endPos[0], 240) && near(t.normal[1], -1) &&
          near(t.normal[0], 0));
    t = world.transformedTrace({100, 40, 64}, {300, 40, 64}, zero, zero, 1, contents::Solid, at, quarter);
    CHECK(t.fraction == 1 && near(t.endPos[0], 300));
    t = world.transformedTrace({100, 40, 64}, {300, 40, 64}, zero, zero, 1, contents::Solid, at, zero);
    CHECK(t.fraction < 1 && near(t.endPos[0], 191.875f)); // unturned, for comparison
    // An uneven box (4 either side, 0 to 8 up) is moved by its middle.
    t = world.transformedTrace({240, -100, 60}, {240, 100, 60}, {-4, -4, 0}, {4, 4, 8}, 1, contents::Solid, at,
                               quarter);
    CHECK(near(t.endPos[1], -12.125f));
    CHECK(world.transformedPointContents({240, 0, 64}, 1, at, quarter) == contents::Solid);
    CHECK(world.transformedPointContents({200, 40, 64}, 1, at, quarter) == 0);
    CHECK(world.transformedPointContents({200, 40, 64}, 1, at, zero) == contents::Solid);
    // Quake III's axes: yaw 90 turns forward to +y, left to -x.
    const auto axes = CollisionWorld::turnAxes(quarter);
    CHECK(near(axes[0][1], 1) && near(axes[1][0], -1) && near(axes[2][2], 1));
    // A box entity.
    t = CollisionWorld::traceBox({0, 0, 100}, {0, 0, -100}, zero, zero, {-16, -16, 0}, {16, 16, 32}, contents::Body);
    CHECK(near(t.endPos[2], 32.125f) && t.contents == contents::Body);
}

void testMarks() {
    using namespace reluva::idtech3;
    // A floor, 128 units square at height 0, facing up, as two triangles.
    Bsp bsp;
    bsp.shaders.push_back({"floor", 0, 1});
    for (auto [x, y] : std::array<std::pair<float, float>, 4>{{{-64, -64}, {64, -64}, {64, 64}, {-64, 64}}}) {
        BspVertex v;
        v.pos = {x, y, 0};
        v.normal = {0, 0, 1};
        bsp.vertices.push_back(v);
    }
    bsp.indices = {0, 1, 2, 0, 2, 3};
    BspSurface floor;
    floor.type = SurfaceType::Planar;
    floor.numVertices = 4;
    floor.numIndices = 6;
    bsp.surfaces.push_back(floor);
    BspModel world;
    world.numSurfaces = 1;
    bsp.models.push_back(world);

    MarkSurfaces marks(bsp);
    CHECK(marks.triangleCount() == 2);
    // A scorch 16 units across, just above the floor, pushed down onto it.
    const std::vector<Vec3f> square{{-8, -8, 1}, {8, -8, 1}, {8, 8, 1}, {-8, 8, 1}};
    std::vector<Vec3f> points;
    std::vector<MarkSurfaces::Fragment> fragments;
    marks.mark(square, {0, 0, -20}, 384, 128, points, fragments);
    CHECK(!fragments.empty() && fragments.size() <= 2);
    float area = 0;
    for (const auto& f : fragments) {
        CHECK(f.numPoints >= 3);
        for (int i = 1; i + 1 < f.numPoints; ++i) {
            const Vec3f& a = points[size_t(f.firstPoint)];
            const Vec3f& b = points[size_t(f.firstPoint + i)];
            const Vec3f& c = points[size_t(f.firstPoint + i + 1)];
            area += std::fabs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) / 2;
        }
    }
    for (const Vec3f& p : points)
        CHECK(std::fabs(p[2]) < 1e-4f && std::fabs(p[0]) <= 8.001f && std::fabs(p[1]) <= 8.001f);
    CHECK(std::fabs(area - 256) < 0.1f); // the whole square, on the floor
    // From below, the floor faces away; too far above, it's out of reach.
    points.clear();
    fragments.clear();
    marks.mark(square, {0, 0, 20}, 384, 128, points, fragments);
    CHECK(fragments.empty());
    const std::vector<Vec3f> high{{-8, -8, 100}, {8, -8, 100}, {8, 8, 100}, {-8, 8, 100}};
    marks.mark(high, {0, 0, -20}, 384, 128, points, fragments);
    CHECK(fragments.empty());
    // Room for only a few points: nothing past the limit.
    marks.mark(square, {0, 0, -20}, 4, 128, points, fragments);
    CHECK(points.size() <= 4);
    // A surface that takes no marks.
    bsp.shaders[0].surfaceFlags = surfaceflags::NoMarks;
    MarkSurfaces none(bsp);
    CHECK(none.triangleCount() == 0);
}

void testAreaPortals() {
    using namespace reluva::idtech3;
    // Three areas in a row, portals between 0-1 and 1-2.
    AreaPortals areas(3);
    CHECK(areas.connected(1, 1) && !areas.connected(0, 1) && !areas.connected(0, -1));
    areas.adjust(0, 1, true);
    CHECK(areas.connected(0, 1) && !areas.connected(0, 2));
    areas.adjust(1, 2, true);
    CHECK(areas.connected(0, 2));
    // Opened twice, closed once: still open.
    areas.adjust(0, 1, true);
    areas.adjust(0, 1, false);
    CHECK(areas.connected(0, 2));
    areas.adjust(0, 1, false);
    CHECK(!areas.connected(0, 2) && areas.connected(1, 2));
    areas.adjust(0, 1, false); // closing a closed one changes nothing
    areas.adjust(0, 1, true);
    CHECK(areas.connected(0, 2));
}

static const reluva::test::Suite suite("idtech3.unit.collision", {
    {"traces", [](const std::filesystem::path&) { testCollision(); }},
    {"patch collision", [](const std::filesystem::path&) { testPatchCollision(); }},
    {"server world", [](const std::filesystem::path&) { testServerWorld(); }},
    {"area portals", [](const std::filesystem::path&) { testAreaPortals(); }},
    {"marks", [](const std::filesystem::path&) { testMarks(); }},
});
