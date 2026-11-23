// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: maps (.bzn), written here in the game's layout.

#include "common/archive.h"
#include "engines/storm3d/formats/bzn.h"
#include "engines/storm3d/pages/map_page.h"
#include "engines/storm3d/pages/sod_meshes.h"

#include "tests/support/test.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace reluva;
using reluva::storm3d::BznMap;
using reluva::storm3d::MapPage;
using reluva::storm3d::SodMeshes;
using reluva::storm3d::SpriteTable;

namespace {

// Writes a map's header and fields, each field's type word carrying
// leftover bytes above its type, as the game's do.
struct Writer {
    std::vector<uint8_t> out;
    explicit Writer(const std::string &description) {
        std::string hex;
        for (char c : description)
            hex += "0123456789abcdef"[(unsigned char)c >> 4], hex += "0123456789abcdef"[c & 15];
        hex.resize(64, '0');
        const std::string head = "version [1] =\r\n2051\r\nsaveGameDesc = " + hex + "\r\nbinarySave [1] =\r\ntrue\r\n";
        out.assign(head.begin(), head.end());
    }
    void field(uint8_t type, const void *data, uint32_t size) {
        const uint8_t word[4] = {type, 0x7c, 0x03, 0x78};
        out.insert(out.end(), word, word + 4);
        out.insert(out.end(), reinterpret_cast<const uint8_t *>(&size), reinterpret_cast<const uint8_t *>(&size) + 4);
        out.insert(out.end(), static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    }
    void text(const std::string &s, uint32_t size, uint8_t type = BznMap::kText) {
        std::vector<char> b(size, 0);
        std::memcpy(b.data(), s.data(), std::min<size_t>(s.size(), size));
        field(type, b.data(), size);
    }
    void integer(int32_t v) { field(BznMap::kInt, &v, 4); }
    void real(float v) { field(BznMap::kFloat, &v, 4); }
    void flag(bool v) {
        const uint8_t b = v;
        field(BznMap::kBool, &b, 1);
    }
    void vector(const std::vector<float> &v) { field(BznMap::kVector, v.data(), uint32_t(v.size() * 4)); }
    void raw(uint32_t v) { field(BznMap::kRaw, &v, 4); }

    void settings(int32_t objects) {
        text("test.bzn", 16);
        flag(true);
        text("test.bzn", 100);
        for (float f : {0.0f, 6400.0f, -28.5f, 105.5f, 0.0f, 6400.0f})
            real(f);
        flag(false);
        for (int i = 0; i < 3; ++i)
            real(0.25f);
        text("mbgstars.sod", 100);
        text("fed1.tt", 128);
        text("federation", 128);
        integer(objects);
    }
    // An object's opening fields, then some of its own: a weapon's name,
    // which is a definition's too, and a float.
    void object(const std::string &odf, int32_t team, const std::string &label, float x, float z) {
        text(odf, 8, BznMap::kClass);
        integer(int32_t(0x80010000u + out.size()));
        vector({x, 38.5f, z});
        integer(team);
        text(label, 40);
        const uint32_t pointer = 0x01e5a048;
        field(BznMap::kPointer, &pointer, 4);
        field(BznMap::kMatrix, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0, 1, x, 38.5f, z}.data(), 48);
        text("fbphas", 8, BznMap::kClass);
        real(750);
    }
    void path(const std::string &name, const std::vector<float> &points, uint32_t kind) {
        raw(0x0202e6a0);
        integer(int32_t(name.size()));
        text(name, uint32_t(name.size()));
        integer(int32_t(points.size() / 3));
        vector(points);
        raw(kind);
        real(0.01f);
    }
};

// A mission's map: its settings, two objects, its scripts and two paths.
void testParse() {
    Writer w("Premonitions");
    w.settings(2);
    w.object("fente", 1, "Enterprise", 5570.5f, 237.5f);
    w.object("mafield", 0, "mafield69", 179.7f, 4010.0f);
    w.integer(400);
    w.text("EmptyMission", 40);
    w.text("Federation1S.dsl", 64);
    w.text("Federation1R.drl", 64);
    w.integer(2);
    w.path("ENTSTART", {5749.25f, 28.3f, 1039.1f}, 0);
    w.path("DOM1PATROL", {1740.8f, 38.5f, 2485.6f, 1795.9f, 38.5f, 2231.3f}, 2);

    BznMap map;
    CHECK(!map.parse(w.out));
    CHECK(map.version == 2051 && map.description == "Premonitions" && map.name == "test.bzn");
    CHECK(map.area[1] == 6400.0f && map.area[2] == -28.5f && map.light[0] == 0.25f);
    CHECK(map.background == "mbgstars.sod" && map.techTree == "fed1.tt" && map.race == "federation");
    CHECK(map.objects.size() == 2);
    const BznMap::Object &e = map.objects[0];
    CHECK(e.odf == "fente" && e.team == 1 && e.label == "Enterprise" && e.position[0] == 5570.5f &&
          e.transform[0] == 1.0f && e.transform[9] == 5570.5f);
    CHECK(e.fieldCount == 9); // its opening seven, the weapon and the float
    CHECK(map.objects[1].odf == "mafield" && map.objects[1].team == 0);
    CHECK((map.scripts == std::vector<std::string>{"Federation1S.dsl", "Federation1R.drl"}));
    CHECK(map.paths.size() == 2 && map.paths[0].name == "ENTSTART" && map.paths[0].points.size() == 1 &&
          map.paths[1].points.size() == 2 && map.paths[1].points[1][2] == 2231.3f && map.paths[1].kind == 2);
}

// What isn't a map, or isn't whole, fails.
void testBroken() {
    BznMap map;
    Writer w("x");
    w.settings(2);
    w.object("fente", 1, "Enterprise", 0, 0);
    CHECK(map.parse(w.out)); // two objects said, one there
    std::vector<uint8_t> cut = w.out;
    cut.resize(cut.size() - 3);
    CHECK(map.parse(cut)); // a field cut short
    const std::string text = "version [1] =\r\n2051\r\nbinarySave [1] =\r\nfalse\r\n";
    CHECK(map.parse(std::vector<uint8_t>(text.begin(), text.end()))); // saved as text
    Writer none("y");
    none.settings(0);
    CHECK(!map.parse(none.out) && map.objects.empty() && map.paths.empty());
}

// How the Maps page draws an object: the model named for its definition,
// none if the definition says not to load one, scaled as it says; placed
// by its transform, the scale about its position.
void testPlacing(const std::filesystem::path &tmp) {
    std::filesystem::create_directories(tmp);
    std::ofstream(tmp / "big.odf") << "[GameObjectClass]\r\nscaleSOD = 2.0\r\n";
    std::ofstream(tmp / "field.odf") << "[GameObjectClass]\r\ndont_load_SOD = 1\r\nscaleSOD = 1.5\r\n";
    std::ofstream(tmp / "afield.odf") << "[GameObjectClass]\r\nclassLabel = \"asteroidfield\"\r\n"
                                         "minAsteroids = 10\r\nmaxAsteroids = 15\r\nfieldRadius = 100\r\n"
                                         "dont_load_SOD = 1\r\nscaleSOD = 1.5\r\n";
    SearchSet files;
    auto folder = std::make_unique<DirectoryArchive>();
    CHECK(!folder->open(tmp, 0));
    files.add(std::move(folder), 0);
    const MapPage::Look big = MapPage::lookOf("big", files);
    CHECK(big.model == "big" && big.scale == 2.0f);
    CHECK(MapPage::lookOf("field", files).model.empty());
    const MapPage::Look none = MapPage::lookOf("ship", files); // no definition: the model by its name
    CHECK(none.model == "ship" && none.scale == 1.0f);

    BznMap::Object o;
    // Turned a quarter about y: right is -z, front is +x; at (100, 0, 50).
    o.transform = {0, 0, -1, 0, 1, 0, 1, 0, 0, 100, 0, 50};
    const gfx::Vec3 p = gfx::transformPoint(MapPage::placeOf(o, 2), {1, 0, 0});
    CHECK(std::fabs(p[0] - 100) < 1e-4f && std::fabs(p[2] - 48) < 1e-4f);

    // An asteroid field: its asteroids within its radius, as many as it
    // says, of the eight models; the same each time for the same object.
    const MapPage::Look field = MapPage::lookOf("afield", files);
    CHECK(field.model.empty() && field.minAsteroids == 10 && field.maxAsteroids == 15 && field.fieldRadius == 100);
    o.handle = 0x80012345;
    o.position = {1000, 38.5f, 2000};
    const auto rocks = MapPage::asteroidsOf(o, field);
    CHECK(rocks.size() >= 10 && rocks.size() <= 15);
    for (const auto &[model, m] : rocks) {
        CHECK(model == "master" ||
              (model.size() == 7 && model.rfind("master", 0) == 0 && model[6] >= '1' && model[6] <= '7'));
        const float dx = m[12] - 1000;
        const float dz = m[14] - 2000;
        CHECK(dx * dx + dz * dz <= 100.0f * 100.0f + 1e-2f);
        CHECK(std::fabs(std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]) - 1.5f) < 1e-4f);
    }
    CHECK(MapPage::asteroidsOf(o, field) == rocks);
    CHECK(MapPage::asteroidsOf(o, big).empty());
}

// The game's overview camera on starting, as its memory held it in
// "Premonitions": over the Camera_1 path's point, 450 up and 300 behind.
void testOverview() {
    const MapPage::Camera c = MapPage::overviewOf({5750.98f, 39.93f, 1042.75f});
    CHECK(std::fabs(c.eye[0] - 5750.98f) < 1e-3f && c.eye[1] == 450 && std::fabs(c.eye[2] - 742.75f) < 1e-3f);
    // Memory's forward: 0, -0.808, 0.589.
    CHECK(std::fabs(c.forward[0]) < 1e-6f && std::fabs(c.forward[1] + 0.808f) < 2e-3f &&
          std::fabs(c.forward[2] - 0.589f) < 2e-3f);
    CHECK(std::fabs(c.forward[0] * c.up[0] + c.forward[1] * c.up[1] + c.forward[2] * c.up[2]) < 1e-5f && c.up[1] > 0);
    CHECK(std::fabs(c.fovY - 0.7853982f) < 1e-6f);
    // Zoomed out to the most, and turned: memory had its eye at (5580.605,
    // 700, 2071.604) over (5469.319, 38.5, 2428.837), turned 5.981 radians.
    const MapPage::Camera far = MapPage::overviewOf({5469.319f, 38.5f, 2428.837f}, 900, 5.981f);
    CHECK(far.eye[1] == 700 && std::fabs(far.eye[0] - 5580.605f) < 0.1f && std::fabs(far.eye[2] - 2071.604f) < 0.1f);
    // Zoomed in: 333.362 up, 258.2 behind (memory's distance 421.666).
    const MapPage::Camera near = MapPage::overviewOf({4601.699f, 38.5f, 2158.552f}, 333.362f, 0);
    CHECK(std::fabs(near.eye[2] - 1900.343f) < 0.1f);
}

// An emitter's particles: a spray's fired each period within the last
// lifespan, moving along its axis and growing; a cloud's within its radius.
void testParticles() {
    SodMeshes::EmitterPart spray;
    spray.particle.world = gfx::identity();
    spray.particle.world[8] = 0, spray.particle.world[9] = 1, spray.particle.world[10] = 0; // fires up
    spray.particle.world[12] = 5;
    spray.period = 0.5f;
    spray.lifespan = 2;
    spray.velocity = 10;
    spray.expansion = 1;
    const auto fired = SodMeshes::particlesAt(spray, 100.25);
    CHECK(fired.size() == 4); // aged 0.25, 0.75, 1.25 and 1.75
    CHECK(std::fabs(fired[0].at[0] - 5) < 1e-4f && std::fabs(fired[0].at[1] - 2.5f) < 1e-3f);
    CHECK(std::fabs(fired[3].at[1] - 17.5f) < 1e-3f && std::fabs(fired[3].size - 2.75f) < 1e-4f);
    CHECK(SodMeshes::particlesAt(spray, 100.25)[2].at == fired[2].at); // the same each time
    spray.spread = 30;
    for (const SodMeshes::Particle &p : SodMeshes::particlesAt(spray, 7.1)) {
        const float len = std::sqrt((p.at[0] - 5) * (p.at[0] - 5) + p.at[1] * p.at[1] + p.at[2] * p.at[2]);
        CHECK(len == 0 || p.at[1] / len >= std::cos(30 * 3.14159265f / 180) - 1e-4f); // within its cone
    }

    SodMeshes::EmitterPart cloud;
    cloud.kind = SodMeshes::EmitterPart::Kind::kCloud;
    cloud.particle.world = gfx::identity();
    cloud.count = 15;
    cloud.radius = 20;
    cloud.lifespan = 4;
    cloud.velocity = 0;
    const auto drifting = SodMeshes::particlesAt(cloud, 3);
    CHECK(drifting.size() == 15);
    for (const SodMeshes::Particle &p : drifting)
        CHECK(p.at[0] * p.at[0] + p.at[1] * p.at[1] + p.at[2] * p.at[2] <= 20.0f * 20.0f + 1e-2f);
}

// A sprite's colour over its animation: linear between keyframes, held by
// a step one, and looping over its length.
void testColours() {
    SpriteTable::Animation a;
    a.kind = "colour";
    a.duration = 10;
    a.interpolation = "linear";
    a.keyframes = {{0, 0, 0, 0}, {2, 0.6, 0, 1}, {8, 0.6, 0, 1}, {9.5, 0, 0, 0}};
    auto near = [](std::array<float, 3> c, float r, float g, float b) {
        return std::fabs(c[0] - r) < 1e-4f && std::fabs(c[1] - g) < 1e-4f && std::fabs(c[2] - b) < 1e-4f;
    };
    CHECK(near(SodMeshes::colourAt(a, 1), 0.3f, 0, 0.5f));
    CHECK(near(SodMeshes::colourAt(a, 5), 0.6f, 0, 1));
    CHECK(near(SodMeshes::colourAt(a, 11), 0.3f, 0, 0.5f)); // round again
    CHECK(near(SodMeshes::colourAt(a, 9.8), 0, 0, 0));      // past the last keyframe
    a.interpolation = "step";
    CHECK(near(SodMeshes::colourAt(a, 1), 0, 0, 0));
}

} // namespace

static const reluva::test::Suite
    kSuite("storm3d.unit.bzn", {
                                   {"parse", [](const std::filesystem::path &) { testParse(); }},
                                   {"broken", [](const std::filesystem::path &) { testBroken(); }},
                                   {"placing", [](const std::filesystem::path &tmp) { testPlacing(tmp / "placing"); }},
                                   {"colours", [](const std::filesystem::path &) { testColours(); }},
                                   {"overview", [](const std::filesystem::path &) { testOverview(); }},
                                   {"particles", [](const std::filesystem::path &) { testParticles(); }},
                               });
