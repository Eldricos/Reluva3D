// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Angel Game Engine's unit tests: the city's life: roads, traffic, its
// lights and rules, police, boats, bridges, trains, props, ambient sounds.
// Every file is built in the test.

#include "tests/engines/age/support.h"

using namespace reluva;
using namespace reluva::age;
using namespace reluva::age::test_support;

namespace {

void testPathDriver() {
    CollisionWorld flat;
    flat.add({-3000, 0, -3000}, {-3000, 0, 3000}, {3000, 0, 3000});
    flat.add({-3000, 0, -3000}, {3000, 0, 3000}, {3000, 0, -3000});
    flat.build(64);
    // Two laps of a 200 m square.
    const std::vector<Vec3> square = {{0, 0, -200}, {200, 0, -200}, {200, 0, 0}, {0, 0, 0}};
    CarSim car;
    car.reset(testCar(), {0, 0, 0}, 0);
    PathDriver driver;
    driver.start(square, 2, 0.9f);
    float t = 0;
    for (; t < 300 && !driver.finished(); t += 0.05f)
        car.step(driver.drive(car, 0.05f), flat, 0.05f);
    CHECK(driver.finished() && driver.lap() == 2 && t < 200); // lap() counts laps done
    CHECK(std::fabs(car.position[0]) < 20 && std::fabs(car.position[2]) < 20);
    CHECK(driver.passed() == 8); // four points, twice

    // The race's checkpoints, each matched to the nearest point of the
    // path as it's driven (two laps of the square: 0-3, then 0-3 again).
    const std::vector<Vec3> gates = {{230, 0, -230}, {230, 0, 30}, {-30, 0, 30}, {-30, 0, -230},
                                     {230, 0, -230}, {230, 0, 30}, {-30, 0, 30}, {-30, 0, -230}};
    const std::vector<size_t> at = pathPointsNearest(driver, gates);
    CHECK(at == std::vector<size_t>({1, 2, 3, 4, 5, 6, 7, 7}));

    // A route that goes out to a point and straight back the same way:
    // the spur's cut (no road graph here, so the points are joined as they are).
    RoadGraph none;
    size_t loopFrom = 0;
    const std::vector<Vec3> route =
        routePath(none, {0, 0, 0}, {{0, 0, -50}, {30, 0, -50}, {1, 0, -51}, {0, 0, -150}}, false, loopFrom);
    CHECK(route.size() == 2 && std::fabs(route[1][2] + 150) < 1e-3f);

    // A body's bounds: every axis from the first vertex on.
    Pkg pkg;
    pkg.meshes.resize(1);
    pkg.meshes[0].name = "BODY_H";
    pkg.meshes[0].groups.resize(1);
    for (const Vec3 &v : {Vec3{1, 2, 3}, Vec3{-1, 0, 5}, Vec3{0, 1, 4}}) {
        PkgVertex vertex;
        vertex.pos = v;
        pkg.meshes[0].groups[0].vertices.push_back(vertex);
    }
    Vec3 low{-99, -99, -99};
    Vec3 high{99, 99, 99};
    CHECK(bodyBounds(pkg, low, high) && low == Vec3({-1, 0, 3}) && high == Vec3({1, 2, 5}));
}

void testRoadGraph() {
    // Two roads (along X, then along Z further on) meeting at an
    // intersection fan in its own block.
    Bytes b;
    const float v[][3] = {{0, 0, 0},   {0, 0, -5},  {0, 0, 5},   {0, 0, 0},    {50, 0, -5},  {50, 0, 5},
                          {50, 0, 0},  {50, 0, 0},  {60, 0, -5}, {60, 0, 5},   {55, 0, -10}, {55, 0, 10},
                          {55, 0, 10}, {60, 0, 10}, {50, 0, 10}, {60, 0, 100}, {50, 0, 100}, {0, 0, 0}};
    b.str("PSD0").u32(2).u32(18);
    for (const auto &p : v)
        b.f32(p[0]).f32(p[1]).f32(p[2]);
    b.u32(1).f32(0).u32(1).u32(4).u32(0);
    auto block = [&](const std::vector<uint16_t> &attrs) {
        b.u32(0).u32(uint32_t(attrs.size()));
        for (uint16_t w : attrs)
            b.u16(w);
    };
    block({0x80 | 0 << 3 | 2, 0, 1, 2, 0, 0, 4, 5, 0});     // road along X: curbs 1-2, then 4-5
    block({0x80 | 5 << 3 | 4, 4, 5, 9, 8, 10, 11});         // the intersection's fan
    block({0x80 | 0 << 3 | 2, 0, 14, 13, 0, 0, 16, 15, 0}); // road along Z: curbs 14-13, 16-15
    b.u8(0).u8(0).u8(0).u8(0).u8(0).u8(0).u8(0).u8(0);
    for (int i = 0; i < 10; ++i)
        b.f32(0);
    Psdl psdl;
    CHECK(!readPsdl(b.v, psdl));
    RoadGraph g;
    g.build(psdl);
    CHECK(g.nodeCount() == 5 && g.edgeCount() == 4);
    const auto route = g.route({0, 0, 0}, {55, 0, 100});
    // Along the first road, through the intersection, up the second.
    CHECK(route.size() >= 3 && route.back() == (Vec3{55, 0, 100}));
    bool viaIntersection = false;
    for (const Vec3 &p : route)
        viaIntersection = viaIntersection || (std::fabs(p[0] - 55) < 6 && std::fabs(p[2]) < 6);
    CHECK(viaIntersection);
    CHECK(g.route({2000, 0, 2000}, {0, 0, 0}).size() == 1); // nowhere near a road
}

void testTraffic(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "geometry" / "va_test.pkg", toBytes("PKG3"));
    test::writeFile(tmp / "tune" / "vehicle" / "va_test.aiVehicleData",
                    toBytes("type: a\naiVehicleData {\n  Mass 1000\n  Size 2 1.5 4\n}\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    AiRoadMap m;
    CHECK(!readBai(loopBai(), m));
    AiMapSettings s;
    s.ambients.push_back({"va_test", 1});
    CollisionWorld flat;
    flat.add({-500, 0, -500}, {-500, 0, 500}, {500, 0, 500});
    flat.add({-500, 0, -500}, {500, 0, 500}, {500, 0, -500});
    flat.build(64);

    // One car round the loop: along the lanes, through both junctions.
    Traffic t;
    t.setup(files, m, s, 3);
    t.setDensity(1);
    t.setCarLimit(1);
    t.update(0.05f, {100, 0, -10}, {}, flat, true);
    CHECK(t.count() == 1);
    {
        // The density's a share of the lanes' capacity: a car every 25 m
        // of lane within reach at 1, a tenth of that at 0.1.
        float lane = 0;
        for (const TrafficLane &l : t.network().lanes())
            lane += l.spawns ? l.length() : 0;
        Traffic full;
        full.setup(files, m, s, 3);
        full.setDensity(1);
        full.setCarLimit(1000);
        CHECK(full.carsWanted({100, 0, -10}) == size_t(std::lround(lane / Traffic::kLaneAtFull)));
        full.setDensity(0.1f);
        CHECK(full.carsWanted({100, 0, -10}) == size_t(std::lround(0.1f * lane / Traffic::kLaneAtFull)));
        CHECK(full.carsWanted({5000, 0, 5000}) == 0); // no lane near
    }
    if (t.count() != 1)
        return;
    float minZ = 0;
    float maxZ = -20;
    float travelled = 0;
    std::vector<RoadUser> u;
    t.users(u);
    Vec3 last = u[0].position;
    for (int i = 0; i < 1200; ++i) { // a minute
        t.update(0.05f, {100, 0, -10}, {}, flat);
        u.clear();
        t.users(u);
        travelled += length(u[0].position - last);
        last = u[0].position;
        minZ = std::min(minZ, last[2]);
        maxZ = std::max(maxZ, last[2]);
        CHECK(length(u[0].velocity) <= 15.01f);
    }
    CHECK(travelled > 400 && minZ < -15 && maxZ > -5); // round both roads

    // Something stopped in its way: it stops behind.
    Traffic held;
    held.setup(files, m, s, 3);
    held.setDensity(1);
    held.setCarLimit(1);
    held.update(0.05f, {100, 0, -10}, {}, flat, true);
    u.clear();
    held.users(u);
    const Vec3 heading = normalized(u[0].velocity);
    const Vec3 block = u[0].position + heading * 25.0f;
    for (int i = 0; i < 160; ++i) // 8 s (after 10 it would go on through)
        held.update(0.05f, {100, 0, -10}, {{block, {}}}, flat);
    u.clear();
    held.users(u);
    const float gap = dot(block - u[0].position, heading);
    CHECK(length(u[0].velocity) < 0.5f && gap > 5 && gap < 12);

    // A driven car running into one from behind knocks it off its lane.
    Traffic hit;
    hit.setup(files, m, s, 3);
    hit.setDensity(1);
    hit.setCarLimit(1);
    hit.update(0.05f, {100, 0, -10}, {}, flat, true);
    u.clear();
    hit.users(u);
    const Vec3 way = normalized(u[0].velocity);
    CarSim car;
    car.reset(testCar(), u[0].position - way * 4.0f + Vec3{0, 0.5f, 0}, std::atan2(-way[0], -way[2]));
    car.velocity = way * 30.0f;
    CHECK(hit.collide(car) > 0);
    std::filesystem::remove_all(tmp);
}

void testTrafficLights() {
    // A crossroads: four roads into junction 0 from the north, east, south
    // and west, each from a junction of its own.
    const auto data = buildBai({{{0, 0, -100}, {0, 0, 0}, 1, 0},
                                {{100, 0, 0}, {0, 0, 0}, 2, 0},
                                {{0, 0, 100}, {0, 0, 0}, 3, 0},
                                {{-100, 0, 0}, {0, 0, 0}, 4, 0}},
                               {{0, 0, 0}, {0, 0, -100}, {100, 0, 0}, {0, 0, 100}, {-100, 0, 0}});
    AiRoadMap m;
    CHECK(!readBai(data, m));
    AiMapSettings s;
    TrafficLights lights;
    lights.build(m, s);
    CHECK(lights.count() == 8 && lights.has(0, 0) && !lights.has(9, 0));
    // Opposite roads together, the crossing ones the other way; each turn
    // green, then amber, then red.
    int greenTogether = 0;
    int conflicts = 0;
    int ambers = 0;
    for (float t = 0; t < 64; t += 0.5f) {
        lights.setClock(t);
        const Signal n = lights.signal(0, 0);
        const Signal e = lights.signal(1, 0);
        const Signal so = lights.signal(2, 0);
        const Signal w = lights.signal(3, 0);
        CHECK(n == so && e == w);
        greenTogether += n == Signal::kGreen;
        conflicts += n != Signal::kRed && e != Signal::kRed;
        ambers += n == Signal::kYellow;
    }
    CHECK(conflicts == 0 && greenTogether > 40 && ambers > 8);
    CHECK(lights.signal(7, 0) == Signal::kGreen); // no light: as green

    // No lights where the ends say none (control 3) or there are no points.
    const auto plain = buildBai({{{0, 0, 0}, {100, 0, 0}, 0, 1, 1, 0, 3, 3}}, {{0, 0, 0}, {100, 0, 0}});
    CHECK(!readBai(plain, m));
    lights.build(m, s);
    CHECK(lights.count() == 0);

    AiMapSettings london;
    CHECK(!parseAiMap("[Traffic Lights]\nsp_traflitsingle_ped_l sp_traflitsingle_ped_l\n", london));
    CHECK(london.trafficLights[1] == "sp_traflitsingle_ped_l" && s.trafficLights[0] == "sp_traflitsingle_f");
}

void testTrafficRules(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "geometry" / "va_test.pkg", toBytes("PKG3"));
    test::writeFile(tmp / "tune" / "vehicle" / "va_test.aiVehicleData",
                    toBytes("type: a\naiVehicleData {\n  Mass 1000\n  Size 2 1.5 4\n}\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    AiMapSettings s;
    s.ambients.push_back({"va_test", 1});
    CollisionWorld flat;
    flat.add({-500, 0, -500}, {-500, 0, 500}, {500, 0, 500});
    flat.add({-500, 0, -500}, {500, 0, 500}, {500, 0, -500});
    flat.build(64);

    // A loop of two-lane one-way roads.
    AiRoadMap m;
    CHECK(!readBai(buildBai({{{0, 0, 0}, {200, 0, 0}, 0, 1, 2, 0, 5}, {{200, 0, -30}, {0, 0, -30}, 1, 0, 2, 0, 5}},
                            {{-5, 0, -15}, {205, 0, -15}}),
                   m));
    TrafficNetwork net;
    net.build(m, s);
    CHECK(net.lanes().size() == 4);
    // Side by side: on the first road (heading +X, right is +Z) the lane
    // at z 7.5 has the one at 2.5 on its left.
    for (size_t i = 0; i < 2; ++i) {
        const TrafficLane &l = net.lanes()[i];
        const bool outer = l.points[0][2] > 5;
        CHECK((outer ? l.left : l.right) == int(1 - i) && (outer ? l.right : l.left) == -1);
    }

    // Stopped by a red light, on at green.
    TrafficLights lights;
    lights.build(m, s);
    float red = -1;
    for (float t = 0; t < 60 && red < 0; t += 0.5f) {
        lights.setClock(t);
        if (lights.signal(0, 0) == Signal::kRed && lights.signal(1, 0) == Signal::kRed)
            red = t;
    }
    CHECK(red >= 0);
    // Two cars, one on each road (a seed that starts them so).
    Traffic t;
    for (uint32_t seed = 3; seed < 40; ++seed) {
        t.setup(files, m, s, seed);
        t.setLights(&lights);
        t.setDensity(1);
        t.setCarLimit(2);
        t.update(0.05f, {100, 0, -15}, {}, flat, true);
        std::vector<RoadUser> two;
        t.users(two);
        if (two.size() == 2 && (two[0].position[2] > 0) != (two[1].position[2] > 0))
            break;
    }
    CHECK(t.count() == 2);
    for (int i = 0; i < 600; ++i) // 30 s: both reach their road's end and wait there
        t.update(0.05f, {100, 0, -15}, {}, flat);
    std::vector<RoadUser> u;
    t.users(u);
    for (const RoadUser &c : u)
        CHECK(length(c.velocity) < 0.5f && (c.position[0] > 180 || c.position[0] < 20));
    // Green for the first road: those waiting at its end go on through,
    // those at the second's go as its own light says.
    std::vector<uint32_t> roadOf;
    for (const RoadUser &c : u)
        roadOf.push_back(c.position[2] > 0 ? 0 : 1);
    float green = red;
    while (lights.signal(0, 0) != Signal::kGreen && green < red + 60)
        lights.setClock(green += 0.5f);
    for (int i = 0; i < 40; ++i) // 2 s, the lights held
        t.update(0.05f, {100, 0, -15}, {}, flat);
    u.clear();
    t.users(u);
    for (size_t i = 0; i < u.size(); ++i)
        CHECK((length(u[i].velocity) > 2) == (lights.signal(roadOf[i], 0) == Signal::kGreen));

    // Held up behind something stopped in its lane: round it in the other.
    AiMapSettings quick = s;
    bool tried = false;
    for (uint32_t seed = 1; seed < 40 && !tried; ++seed) {
        Traffic one;
        one.setup(files, m, quick, seed);
        one.setLaneChanges(true);
        one.setDensity(1);
        one.setCarLimit(1);
        one.update(0.05f, {100, 0, -15}, {}, flat, true);
        u.clear();
        one.users(u);
        // One on the first road with room ahead of it.
        if (u.size() != 1 || u[0].position[2] < -10 || u[0].position[0] < 15 || u[0].position[0] > 60)
            continue;
        const Vec3 block = u[0].position + Vec3{30, 0, 0};
        for (int i = 0; i < 160; ++i) // 8 s
            one.update(0.05f, {100, 0, -15}, {{block, {}}}, flat);
        u.clear();
        one.users(u);
        CHECK(u[0].position[0] > block[0] + 5 && std::fabs(u[0].position[2] - block[2]) > 4);
        tried = true;
    }
    CHECK(tried);
    std::filesystem::remove_all(tmp);
}

void testSirenTable() {
    // The computer-driven police's: samples, each with ways on.
    std::vector<SirenSample> s;
    CHECK(!readSirenTable("Sample name,\r\npolice1fastsirenloop,\r\nplay time,next index\r\n4.45,1\r\n"
                          "play time,next index\r\n2.65,2\r\nSample name,\r\npolice2hilowloop,\r\n"
                          "play time,next index\r\n2.31,2\r\nSample name,\r\npolice1whailerloop,\r\n"
                          "play time,next index\r\n1.11,1\r\n",
                          s));
    CHECK(s.size() == 3 && s[0].wave == "police1fastsirenloop" && s[0].volume == 1 && s[0].next.size() == 2);
    CHECK(std::fabs(s[0].next[1].first - 2.65f) < 1e-5f && s[0].next[1].second == 2 && s[2].next[0].second == 1);
    // The player's: an explosion first, volumes given.
    CHECK(!readSirenTable("Explosion sample,volume\nexplosion,0.95\nSample name,\nsiren_london,0.95\n"
                          "play time,next index\n15,1\nSample name,\nsiren_london2,0.95\nplay time,next index\n15,0\n",
                          s));
    CHECK(s.size() == 2 && s[1].wave == "siren_london2" && std::fabs(s[1].volume - 0.95f) < 1e-5f &&
          s[1].next[0] == (std::pair<float, int>{15, 0}));
    CHECK(readSirenTable("Explosion sample,volume\nexplosion,0.95\n", s));
}

void testSpeedLimit() {
    CollisionWorld flat;
    flat.add({-3000, 0, -3000}, {-3000, 0, 3000}, {3000, 0, 3000});
    flat.add({-3000, 0, -3000}, {3000, 0, 3000}, {3000, 0, -3000});
    flat.build(64);
    CarSim car;
    car.reset(testCar(), {0, 0, 0}, 0);
    PathDriver driver;
    driver.start({{0, 0, -500}, {0, 0, -1000}}, 0, 1.0f);
    driver.setSpeedLimit(15);
    float fastest = 0;
    for (float t = 0; t < 40 && !driver.finished(); t += 0.05f) {
        car.step(driver.drive(car, 0.05f), flat, 0.05f);
        fastest = std::max(fastest, car.forwardSpeed());
    }
    CHECK(fastest > 13 && fastest < 17.5f);
}

void testGizmos(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    // Models whose level-H mesh runs 0-2 m across: leaves 1 m either side.
    Bytes pkg;
    pkg.str("PKG3");
    fileSection(pkg, "H", meshSection(0x102, 0, {0, 1, 2}));
    for (const char *m : {"giz_bridge01_l", "giz_tug_l"})
        test::writeFile(tmp / "geometry" / (std::string(m) + ".pkg"), pkg.v);
    test::writeFile(tmp / "race" / "london" / "london_bridge.pathset",
                    buildPathset({{"giz_bridge01_l", {{0, 8, 0}, {30, 8, 0}, {60, 8, 0}}, 0x0801}}));
    test::writeFile(tmp / "race" / "london" / "london_ferry.pathset",
                    buildPathset({{"giz_tug_l", {{0, 0, 100}, {20, 0, 100}}, 0x0a01}, // back and forth at 1 m/s
                                  {"giz_nothere_l", {{0, 0, 0}, {9, 0, 0}}, 0x0a02}}));
    // A race's bridge, starting raised, over the city's (its first point
    // within 5 m).
    test::writeFile(tmp / "race" / "london" / "london_bridge_race0.pathset",
                    buildPathset({{"open:giz_bridge01_l", {{1, 8, 0}, {30, 8, 0}, {60, 8, 0}}, 0x0801}}));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);

    Gizmos g;
    CHECK(!g.load(files, "london"));
    CHECK(g.boats() == 1 && g.bridges() == 1); // no model for the second boat
    CHECK(g.raised(0) == 0);
    // Two leaves meeting in the middle: the city's road under them is
    // taken out, not that beyond the hinges, well above or to the side.
    CHECK(g.inGap({10, 8, -0.5f}, {12, 8, 0.5f}, {14, 8, 0}) && g.inGap({48, 8, 0}, {50, 8, 0.5f}, {49, 8, -0.5f}));
    CHECK(!g.inGap({-4, 8, 0}, {-3, 8, 0}, {-3, 8, 0.5f}) && !g.inGap({10, 20, 0}, {12, 20, 0}, {11, 20, 1}));
    CHECK(!g.inGap({10, 8, 3}, {12, 8, 3}, {11, 8, 4}));
    std::vector<RoadUser> u;
    g.users(u);
    CHECK(u.empty());
    CollisionWorld world;
    world.build();
    g.update(0, world);
    CollisionWorld::RayHit hit;
    const Vec3 down{0, -1, 0};
    CHECK(world.raycast({15, 20, 0}, down, 30, hit) && std::fabs(hit.point[1] - 8) < 1e-3f);
    // Down 74 s, then rising: the leaves tip up from the hinges, and the
    // traffic stops there.
    for (int i = 0; i < 86 * 4; ++i)
        g.update(0.25f, world);
    CHECK(g.raised(0) == 1);
    g.users(u);
    CHECK(!u.empty() && u[0].solid);
    for (const RoadUser &r : u)
        CHECK(std::fabs(r.position[0]) < 1e-3f || std::fabs(r.position[0] - 60) < 1e-3f);
    CHECK(!world.raycast({29, 20, 0}, down, 30, hit) || hit.point[1] > 15);
    CHECK(world.raycast({2, 20, 0}, down, 30, hit) &&
          std::fabs(hit.point[1] - (8 + 2 * std::tan(Gizmos::kRaised))) < 0.05f);

    // The race's bridge is up from the start.
    CHECK(!g.load(files, "london", "race0") && g.bridges() == 1 && g.raised(0) == 1);
}

void testParkedCars(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    Bytes pkg; // a level-H body 2 m across, 4 high and 2 long
    pkg.str("PKG3");
    fileSection(pkg, "H", meshSection(0x102, 0, {0, 1, 2}));
    test::writeFile(tmp / "geometry" / "va_test.pkg", pkg.v);
    test::writeFile(tmp / "geometry" / "sp_pcar_t.pkg", pkg.v);
    test::writeFile(tmp / "tune" / "vehicle" / "va_test.aiVehicleData",
                    toBytes("type: a\naiVehicleData {\n  Mass 1000\n  Size 2 1.5 4\n}\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    AiMapSettings s;
    s.ambients.push_back({"va_test", 1});
    CollisionWorld flat;
    flat.add({-500, 0, -500}, {-500, 0, 500}, {500, 0, 500});
    flat.add({-500, 0, -500}, {500, 0, 500}, {500, 0, -500});
    flat.build(64);
    AiRoadMap m; // the first road's lanes run along +X at z 2.5 and 7.5
    CHECK(!readBai(buildBai({{{0, 0, 0}, {200, 0, 0}, 0, 1, 2, 0, 5}, {{200, 0, -30}, {0, 0, -30}, 1, 0, 2, 0, 5}},
                            {{-5, 0, -15}, {205, 0, -15}}),
                   m));
    auto line = [](const std::string &name, const Vec3 &a, const Vec3 &b) {
        PathsetPath p;
        p.name = p.model = name;
        p.points = {a, b};
        p.end = 0x1402; // a 2 m gap
        return p;
    };
    // By the curb; in a car park far from the roads; with its own model.
    const std::vector<PathsetPath> lines = {line("PATH01", {20, 0.5f, 10}, {60, 0.5f, 10}),
                                            line("PATH02", {20, 0, 100}, {41, 0, 100}),
                                            line("sp_pcar_t", {100, 0, 10}, {130, 0, 10})};
    Traffic t;
    t.setup(files, m, s, 7);
    t.park(files, lines, {});
    CHECK(t.parked() > 3 && t.count() == 0);
    std::vector<std::pair<Vec3, Vec3>> places;
    t.parkedPlaces(places);
    auto onGrid = [](float x, float first, float step) {
        const float k = (x - first) / step;
        return std::fabs(k - std::round(k)) < 1e-3f;
    };
    for (const auto &[p, h] : places) {
        if (p[2] > 50) { // side by side, 3 m apart (2 wide, 1 between), facing across
            CHECK(std::fabs(h[2]) > 0.99f && p[0] > 20 && p[0] < 41 && onGrid(p[0], 21.5f, 3));
        } else if (p[0] < 70) { // nose to tail, 6 m apart (4 long, a 2 m gap), along
            CHECK(h[0] > 0.99f && p[0] > 20 && p[0] < 60 && onGrid(p[0], 23, 6));
        } else { // its own model, 2 m long
            CHECK(h[0] > 0.99f && onGrid(p[0], 102, 4));
        }
    }
    // Not among those to keep clear of, nor left out far from the player;
    // stood on the ground.
    std::vector<RoadUser> u;
    t.users(u);
    CHECK(u.empty());
    for (int i = 0; i < 10; ++i)
        t.update(0.05f, {0, 0, 5000}, {}, flat);
    const size_t parked = t.parked();
    CHECK(parked == places.size());
    places.clear();
    t.parkedPlaces(places);
    CHECK(std::fabs(places[0].first[1]) < 1e-3f);
    // Knocked, one slides off and is to be kept clear of.
    Vec3 position = places[0].first;
    Vec3 velocity{0, 0, -8};
    CHECK(t.collide({places[0].first + Vec3{0, 0.5f, 1.5f}}, 1, 1e9f, position, velocity) > 0);
    for (int i = 0; i < 10; ++i)
        t.update(0.05f, {0, 0, 5000}, {}, flat);
    t.users(u);
    CHECK(u.size() == 1 && u[0].position[2] < places[0].first[2] - 0.1f && t.parked() == parked);
    t.park(files, {}, {});
    CHECK(t.parked() == 0);
    std::filesystem::remove_all(tmp);
}

void testTrains(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    Bytes pkg; // carriages 2 m wide and 2 long, standing on their origin
    pkg.str("PKG3");
    fileSection(pkg, "H", meshSection(0x102, 0, {0, 1, 2}));
    test::writeFile(tmp / "geometry" / "va_ug_l.pkg", pkg.v);
    test::writeFile(tmp / "geometry" / "va_test.pkg", pkg.v);
    test::writeFile(tmp / "race" / "london" / "london_train.pathset",
                    buildPathset({{"PATH0", {{0, 0, 0}, {100, 0, 0}}, 0x1402}}));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    Gizmos g;
    CHECK(!g.load(files, "london") && g.trains() == 1 && g.boats() == 0);
    CollisionWorld world;
    world.build();
    // Off from the start, gathering speed.
    const float span = 3 * 2 + 2 * 0.6f;
    for (int i = 0; i < 50; ++i)
        g.update(0.1f, world);
    CHECK(std::fabs(g.trainSpeed(0) - 5 * Gizmos::kTrainAccel) < 0.2f && g.trainPosition(0)[0] > 10);
    // Standing at the far end, then back.
    float clock = 5;
    while (g.trainSpeed(0) > 0 && clock < 60) {
        g.update(0.1f, world);
        clock += 0.1f;
    }
    CHECK(clock > 15 && clock < 30 && std::fabs(g.trainPosition(0)[0] - (100 - span + 1)) < 0.05f);
    for (int i = 0; i < int(Gizmos::kTrainStand * 10) - 5; ++i)
        g.update(0.1f, world);
    CHECK(g.trainSpeed(0) == 0);
    for (int i = 0; i < 50; ++i)
        g.update(0.1f, world);
    CHECK(g.trainSpeed(0) > 1 && g.trainPosition(0)[0] < 100 - span);
    std::vector<RoadUser> u;
    g.users(u);
    CHECK(!u.empty() && u[0].solid && u[0].velocity[0] < -1);

    // A car in its way is shoved out of it.
    CarSim car;
    car.reset(testCar(), g.trainPosition(0) + Vec3{-2, 0, 0}, 0);
    car.velocity = {};
    float knock = 0;
    for (int i = 0; i < 20; ++i) {
        g.update(0.05f, world);
        knock = std::max(knock, g.collide(car));
    }
    CHECK(knock > 1 && car.position[0] < g.trainPosition(0)[0] - 1);
    // A parked car on the line is pushed aside.
    AiMapSettings s;
    s.ambients.push_back({"va_test", 1});
    Traffic t;
    t.setup(files, AiRoadMap{}, s, 1);
    PathsetPath across;
    across.name = across.model = "PATH01";
    across.points = {{20, 0, 1}, {20, 0, -1}};
    t.park(files, {across}, {});
    std::vector<std::pair<Vec3, Vec3>> before;
    std::vector<std::pair<Vec3, Vec3>> after;
    t.parkedPlaces(before);
    CHECK(before.size() <= 1);
    for (int i = 0; i < 400; ++i) {
        g.update(0.1f, world);
        g.push(t);
    }
    t.parkedPlaces(after);
    for (size_t i = 0; i < before.size(); ++i)
        CHECK(length(after[i].first - before[i].first) > 1);
    std::filesystem::remove_all(tmp);
}

void testProps(const std::filesystem::path &tmp) {
    // Placing: points; pairs (where, then a point it faces); a line with a
    // prop every 2 m (end word 20 << 8 | 2).
    std::vector<PathsetPath> paths(3);
    paths[0].model = "sp_cone_f";
    paths[0].points = {{0, 0, 0}, {5, 0, 0}};
    paths[1].model = "sp_dumpstr_f";
    paths[1].end = 1;
    paths[1].points = {{10, 0, 0}, {10, 0, 2}};
    paths[2].model = "sp_barricadewood_f";
    paths[2].end = 20 << 8 | 2;
    paths[2].points = {{0, 0, 10}, {5, 0, 10}};
    auto placed = placeProps(paths);
    CHECK(placed.size() == 2 + 1 + 3);
    // An unnamed path (a race's ramp line) places nothing.
    paths.push_back({});
    paths.back().model = "PATH01";
    paths.back().points = {{0, 0, 0}};
    CHECK(placeProps(paths).size() == placed.size());
    if (placed.size() == 6) {
        CHECK(placed[2].model == "sp_dumpstr_f" && std::fabs(placed[2].along[2] - 1) < 1e-4f);
        CHECK(placed[3].at == (Vec3{0, 0, 10}) && placed[5].at == (Vec3{4, 0, 10}) && placed[4].along[0] == 1);
    }
    // Street furniture: a road in rule 3's block, 100 m along +X, its
    // left side's curb at z -5 and outer edge at -7.
    const auto kinds = parsePropDefs("name,start,distance,maxUse,minLerp,maxLerp,file1,file2,file3,file4,,,\r\n"
                                     "streetlight,9,29,9999,0.1,0.1,sp_lightstreet_f\r\n"
                                     "mailbox,1,1,1,0.2,0.2,sp_mailbox_f\r\n"
                                     "newsboxs,7,10,1,0.1,0.1,sp_newsblue_f,sp_newsred_f,sp_newsyelw_f\r\n");
    CHECK(kinds.size() == 3 && kinds.at("streetlight").start == 9 && kinds.at("streetlight").spacing == 29 &&
          kinds.at("streetlight").most == 9999 && kinds.at("newsboxs").models.size() == 3 &&
          std::fabs(kinds.at("mailbox").minLerp - 0.2f) < 1e-6f);
    const auto rules =
        parsePropRules("rulename,prop1,prop2,,,\r\nn03left,streetlight,,mailbox\r\nn03right,newsboxs\r\n");
    CHECK(rules.size() == 2 && rules.at("n03left") == (std::vector<std::string>{"streetlight", "mailbox"}));
    AiRoadMap street;
    street.roads.resize(2);
    street.roads[0].blocks = {7};
    street.roads[1].blocks = {8}; // rule 0: none
    for (AiRoad &r : street.roads) {
        r.sides[0].curb = {{0, 0, -5}, {100, 0, -5}};
        r.sides[0].outerEdge = {{0, 0, -7}, {100, 0, -7}};
        r.sides[1].curb = {{0, 0, 5}, {100, 0, 5}};
        r.sides[1].outerEdge = {{0, 0, 7}, {100, 0, 7}};
    }
    std::vector<uint8_t> blockRules(9, 0);
    blockRules[7] = 3;
    const auto furniture = placeStreetProps(street, blockRules, kinds, rules);
    // Lights at 9, 38, 67 and 96 m and a mailbox on the left; a news box
    // on the right.
    CHECK(furniture.size() == 4 + 1 + 1);
    if (furniture.size() == 6) {
        CHECK(furniture[0].model == "sp_lightstreet_f" && std::fabs(furniture[0].at[0] - 9) < 1e-3f &&
              std::fabs(furniture[0].at[2] + 5.2f) < 1e-3f && std::fabs(furniture[3].at[0] - 96) < 1e-3f);
        CHECK(std::fabs(furniture[0].along[2] + 1) < 1e-4f); // X away from the road
        CHECK(furniture[4].model == "sp_mailbox_f" && std::fabs(furniture[4].at[2] + 5.4f) < 1e-3f);
        CHECK(furniture[5].model.rfind("sp_news", 0) == 0 && std::fabs(furniture[5].at[2] - 5.2f) < 1e-3f &&
              std::fabs(furniture[5].along[2] - 1) < 1e-4f);
    }

    BangerData b;
    CHECK(!parseBangerData("type: a\ndgBangerData {\n  Size 0.2 10.3 0.2\n  CG 0.18 5.15 0\n  Mass 9.3\n"
                           "  ImpulseLimit2 4657.7\n  NumParts 2\n  NumGlows 1\n  GlowOffset 0 1.99 0\n"
                           "  BirthRule {\n    Position 0 1 0\n    VelocityVar 5 2 1\n    Life 1\n"
                           "    Radius 0.54\n    InitialBlast 16\n    Gravity -9.8\n    TexFrameEnd 3\n  }\n"
                           "  TexNumber 6\n}\n",
                           b));
    CHECK(b.bursts() && b.texNumber == 6 && b.particles.initialBlast == 16 && b.particles.frameEnd == 3 &&
          b.particles.velocityVar[0] == 5 && b.particlesAt[1] == 1 && b.particles.gravity == -9.8f);
    CHECK(b.glows.size() == 1 && b.glows[0][1] == 1.99f);
    CHECK(std::fabs(b.mass - 9.3f) < 1e-4f && std::fabs(b.impulseLimit2 - 4657.7f) < 0.01f && b.parts == 2 &&
          b.cg[1] == 5.15f && b.size[1] == 10.3f);
    CHECK(parseBangerData("type: a\nother {\n}\n", b));

    // A cone (breaks at sqrt(2500) = 50 kg m/s) and a tree (never) in the
    // way of a car on a flat plane.
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "city" / "test" / "props.pathset",
                    buildPathset({{"sp_cone_f", {{0, 0, -30}}, 0}, {"sp_tree_s", {{6, 0, -30}}, 0}}));
    // The cone has two pieces: a base (its middle 0.1 up) and a top; it
    // throws out litter (texture fxpt3) as it breaks.
    test::writeFile(tmp / "tune" / "banger" / "sp_cone_f.dgBangerData",
                    toBytes("type: a\ndgBangerData {\n  Mass 10\n  ImpulseLimit2 2500\n  NumParts 2\n"
                            "  BirthRule {\n    Position 0 0.5 0\n    InitialBlast 8\n  }\n  TexNumber 3\n}\n"));
    test::writeFile(tmp / "tune" / "banger" / "sp_cone_f_BREAK01.dgBangerData",
                    toBytes("type: a\ndgBangerData {\n  Size 0.6 0.2 0.6\n  CG 0 0.1 0\n  Mass 4\n}\n"));
    test::writeFile(tmp / "tune" / "banger" / "sp_cone_f_BREAK02.dgBangerData",
                    toBytes("type: a\ndgBangerData {\n  Size 0.3 0.6 0.3\n  CG 0 0.5 0\n  Mass 6\n}\n"));
    test::writeFile(tmp / "tune" / "banger" / "sp_tree_s.dgBangerData",
                    toBytes("type: a\ndgBangerData {\n  Mass 50000\n  ImpulseLimit2 1e+30\n}\n"));
    // A race's own (race/<map>/<race>.pathset) besides the city's.
    test::writeFile(tmp / "race" / "test" / "circuit0.pathset",
                    buildPathset({{"sp_barricadeconcl_f", {{0, 0, 50}, {4, 0, 50}}, 20 << 8 | 2}}));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    Props props;
    props.load(files, "test", "circuit0");
    CHECK(props.count() == 2 + 3);
    props.load(files, "test");
    CHECK(props.count() == 2);
    props.prepare(files, [](const std::string &model, Vec3 &lo, Vec3 &hi) {
        lo = model == "sp_tree_s" ? Vec3{-0.5f, -3, -0.5f} : Vec3{-0.3f, -0.4f, -0.3f};
        hi = {-lo[0], -lo[1], -lo[2]};
        return true;
    });
    CollisionWorld flat;
    flat.add({-300, 0, -300}, {-300, 0, 300}, {300, 0, 300});
    flat.add({-300, 0, -300}, {300, 0, 300}, {300, 0, -300});
    flat.build();
    props.collide(flat);
    CollisionWorld::RayHit hit;
    CHECK(flat.raycast({0, 5, -30}, {0, -1, 0}, 10, hit) && hit.surface.prop == 0 &&
          std::fabs(hit.distance - 4.2f) < 0.01f);
    CHECK(hit.surface.breakImpulse == 50 && hit.surface.propMass == 10);
    CHECK(flat.raycast({6, 10, -30}, {0, -1, 0}, 20, hit) && hit.surface.prop == 1 && hit.surface.breakImpulse == 0);

    // Driven through: the cone comes loose and falls out of the way; the
    // car goes on, a little slower.
    TrafficLights lights;
    CarSim car;
    car.reset(testCar(), {0, 0, 0}, 0);
    CarControls go;
    go.throttle = 1;
    float speedBefore = 0;
    for (int i = 0; i < 160 && car.position[2] > -40; ++i) {
        if (car.position[2] > -26)
            speedBefore = car.forwardSpeed();
        car.step(go, flat, 0.05f);
        props.update(0.05f, {&car}, flat, lights);
    }
    CHECK(props.loose() == 1 && car.position[2] < -35 && car.forwardSpeed() > speedBefore * 0.8f);
    {
        const std::vector<Props::Burst> bursts = props.takeBursts();
        CHECK(bursts.size() == 1 && bursts[0].texture == "fxpt3" && bursts[0].rule.initialBlast == 8 &&
              std::fabs(bursts[0].at[1] - 0.5f) < 0.01f && std::fabs(bursts[0].at[2] + 30) < 0.01f);
        CHECK(props.takeBursts().empty());
    }
    CHECK(!flat.raycast({0, 5, -30}, {0, -1, 0}, 10, hit) || hit.surface.prop < 0);
    // It came apart: both pieces fly off and come to rest on the ground.
    for (int i = 0; i < 100; ++i)
        props.update(0.05f, {}, flat, lights);
    size_t resting = 0;
    float lowest = 0;
    CHECK(props.pieces(&resting, &lowest) == 2 && resting == 2 && lowest > 0.05f && lowest < 0.5f);
    // The tree stops a car.
    car.reset(testCar(), {6, 0, 0}, 0);
    for (int i = 0; i < 80; ++i) {
        car.step(go, flat, 0.05f);
        props.update(0.05f, {&car}, flat, lights);
    }
    CHECK(props.loose() == 1 && car.position[2] > -30);
}

void testAmbient(const std::filesystem::path &tmp) {
    const std::string text = "Min distance,Max distance,3D priority,Audible area,,,,,\r\n"
                             "5,75,12,2,,,,,\r\n"
                             "sample name,sample volume,sample type,oneshot time limit low,oneshot time limit high,"
                             "active,min speed,max speed,Doppler\r\n"
                             "buoybell,0.95,2,5,10,1,0,999999,0\r\n"
                             "streetcable,0.82,0,0,0,1,0,999999,1\r\n"
                             "VECTORPOINTS,,,,,,,,\r\nx,y,z,,,,,,\r\n"
                             "-146.180206,-0.000192,-662.411499,,,,,,\r\n"
                             "-6.795126,-0.000042,-603.490356\r\n";
    Ambience a;
    CHECK(!parseAmbience(text, a));
    CHECK(a.minDistance == 5 && a.maxDistance == 75 && a.priority == 12 && a.area == 2 && a.samples.size() == 2 &&
          a.points.size() == 2);
    if (a.samples.size() == 2 && a.points.size() == 2) {
        CHECK(a.samples[0].name == "buoybell" && std::fabs(a.samples[0].volume - 0.95f) < 1e-5f &&
              a.samples[0].type == 2 && a.samples[0].low == 5 && a.samples[0].high == 10 && a.samples[0].active);
        CHECK(a.samples[1].type == 0 && std::fabs(a.points[1][2] + 603.490356f) < 1e-3f);
        CHECK(!a.samples[0].doppler && a.samples[1].doppler && a.samples[1].maxSpeed == 999999);
    }
    CHECK(ambientGain(a, 3) == 1 && std::fabs(ambientGain(a, 40) - 0.5f) < 1e-5f && ambientGain(a, 80) == 0);
    CHECK(parseAmbience("sample name\n", a));
    CHECK(parseAmbientContainer("file names\r\nbirdies\r\nbuoyseals\r\n") ==
          (std::vector<std::string>{"birdies", "buoyseals"}));

    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "aud" / "ambient" / "testambientcontainer.csv", toBytes("file names\nbuoys\nmissing\n"));
    test::writeFile(tmp / "aud" / "ambient" / "buoys.csv", toBytes(text));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    AmbientSounds sounds;
    CHECK(!sounds.load(files, "test", nullptr) && sounds.ambiences().size() == 1);
    sounds.update(1, {0, 0, 0});
    CHECK(sounds.playing() == 0); // nothing to play them on
    // 20 m east of a point, facing north (right is east), driving at it:
    // heard on the left, the Doppler sound higher.
    sounds.update(0.1f, {-6.795126f + 20, 0, -603.490356f}, {1, 0, 0}, {-10, 0, 0});
    CHECK(sounds.heard(0).pan < -80 && sounds.heard(0).pitch == 1);
    CHECK(sounds.heard(1).pan < -80 && std::fabs(sounds.heard(1).pitch - 353.0f / 343) < 1e-4f);
    // Over the point: in the middle.
    sounds.update(0.1f, {-6.795126f, 0, -603.490356f}, {1, 0, 0});
    CHECK(sounds.heard(0).pan == 0);
    CHECK(sounds.load(files, "elsewhere", nullptr));
}

} // namespace

static const reluva::test::Suite
    kSuite("age.unit.city", {
                                {"path driver", [](const std::filesystem::path &) { testPathDriver(); }},
                                {"road graph", [](const std::filesystem::path &) { testRoadGraph(); }},
                                {"traffic", [](const std::filesystem::path &tmp) { testTraffic(tmp); }},
                                {"traffic lights", [](const std::filesystem::path &) { testTrafficLights(); }},
                                {"traffic rules", [](const std::filesystem::path &tmp) { testTrafficRules(tmp); }},
                                {"siren table", [](const std::filesystem::path &) { testSirenTable(); }},
                                {"speed limit", [](const std::filesystem::path &) { testSpeedLimit(); }},
                                {"gizmos", [](const std::filesystem::path &tmp) { testGizmos(tmp); }},
                                {"parked cars", [](const std::filesystem::path &tmp) { testParkedCars(tmp); }},
                                {"trains", [](const std::filesystem::path &tmp) { testTrains(tmp); }},
                                {"props", [](const std::filesystem::path &tmp) { testProps(tmp); }},
                                {"ambient", [](const std::filesystem::path &tmp) { testAmbient(tmp); }},
                            });
