// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tests/engines/stgenerations/support.h"

#include "common/bytes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "common/serializer.h"
#include "engines/stgenerations/game/campaign.h"
#include "engines/stgenerations/formats/combatdata.h"
#include "engines/stgenerations/game/combatrender.h"

using namespace reluva;
using namespace reluva::stgenerations;

namespace {

// Not kPi: MSVC only has it with _USE_MATH_DEFINES.
constexpr double kPi = 3.14159265358979323846;

typedef std::vector<uint8_t> Bytes;

void le(Bytes &b, uint32_t v, int n) {
    for (int i = 0; i < n; i++)
        b.push_back((uint8_t)(v >> (8 * i)));
}

void be(Bytes &b, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; i--)
        b.push_back((uint8_t)(v >> (8 * i)));
}

void f32(Bytes &b, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    le(b, v, 4);
}

// A 32 x 32 GIF made for this test, of the pattern (x * y + x) % 7.
const uint8_t kPatternGif[] = {
    0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x20, 0x00, 0x20, 0x00, 0x82, 0x00, 0x00, 0x00, 0x1e, 0x3c, 0x5a, 0x78, 0x96,
    0xb4, 0xd2, 0xf0, 0x0e, 0x2c, 0x4a, 0x68, 0x86, 0xa4, 0xc2, 0xe0, 0xfe, 0x1c, 0x3a, 0x58, 0x00, 0x00, 0x00, 0x2c,
    0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x20, 0x00, 0x40, 0x08, 0xff, 0x00, 0x01, 0x04, 0x10, 0x30, 0x80, 0x40, 0x01,
    0x03, 0x02, 0x09, 0x1a, 0x44, 0x38, 0xb0, 0xe0, 0xc1, 0x84, 0x0e, 0x19, 0x12, 0x04, 0x20, 0x80, 0x80, 0x81, 0x00,
    0x03, 0x0a, 0x50, 0xb4, 0x88, 0x51, 0x63, 0xc5, 0x8b, 0x19, 0x37, 0x82, 0xf4, 0x68, 0x11, 0xc0, 0x00, 0x03, 0x02,
    0x0a, 0x04, 0x20, 0x60, 0x12, 0xa5, 0x4a, 0x96, 0x27, 0x53, 0xae, 0x6c, 0x29, 0x13, 0x26, 0x4a, 0x00, 0x04, 0x02,
    0x14, 0x10, 0x60, 0x60, 0x00, 0x4e, 0x9d, 0x3c, 0x7d, 0xe6, 0xdc, 0xd9, 0xf3, 0x27, 0x51, 0xa1, 0x3a, 0x01, 0x14,
    0x18, 0x10, 0xc0, 0x00, 0x01, 0x01, 0x4a, 0x99, 0x3a, 0x85, 0xba, 0xb4, 0xe9, 0xd3, 0xa8, 0x56, 0xa9, 0x32, 0x05,
    0x60, 0xa0, 0x00, 0x81, 0x01, 0x02, 0x02, 0x70, 0xf5, 0x0a, 0x56, 0x6c, 0xd7, 0xaf, 0x61, 0xc7, 0xa2, 0x35, 0xeb,
    0x15, 0x80, 0xdb, 0xb7, 0x70, 0xe3, 0xca, 0x9d, 0x1b, 0xb7, 0xe1, 0x42, 0x88, 0x77, 0xed, 0x3e, 0xd4, 0x2b, 0xd1,
    0x67, 0xcc, 0x97, 0x34, 0x01, 0xff, 0x9d, 0x39, 0xd8, 0x26, 0x54, 0xba, 0x88, 0x13, 0xc7, 0x1d, 0x1a, 0xd4, 0x68,
    0x63, 0xc6, 0x45, 0x21, 0x23, 0xd5, 0xc8, 0x17, 0xef, 0x5e, 0x85, 0x97, 0x23, 0x42, 0xc4, 0x3a, 0x95, 0xf3, 0xd5,
    0xaa, 0x9d, 0x41, 0x7f, 0xde, 0xfa, 0xb1, 0xa3, 0x48, 0xd3, 0xa5, 0x43, 0xa6, 0x26, 0x89, 0xf0, 0x6c, 0x59, 0xb5,
    0xaf, 0x5d, 0xa7, 0x95, 0xcd, 0xd6, 0xb0, 0x60, 0x97, 0x84, 0x71, 0xdb, 0xce, 0x0d, 0x75, 0xf5, 0x69, 0xd5, 0x1c,
    0x81, 0x8f, 0x14, 0xe9, 0x38, 0x32, 0x50, 0xe3, 0x47, 0x8b, 0x4f, 0x86, 0x3d, 0x9b, 0x6c, 0xf3, 0xb5, 0xcc, 0x6b,
    0x5b, 0xee, 0x9b, 0x17, 0x33, 0xf5, 0xcc, 0x81, 0x79, 0xdf, 0xae, 0x99, 0xdd, 0xb0, 0x67, 0xad, 0x59, 0xbf, 0x8b,
    0x3d, 0xaf, 0xaa, 0xb8, 0x7c, 0x62, 0xdf, 0xe8, 0x83, 0xb3, 0x46, 0x5d, 0x52, 0xb2, 0xf2, 0xf7, 0xee, 0x87, 0x46,
    0x9f, 0x4f, 0x9b, 0x7e, 0xdb, 0xca, 0xf8, 0xad, 0x4f, 0xdf, 0x5c, 0xb8, 0xbb, 0xff, 0xfe, 0x31, 0x8d, 0x27, 0xd5,
    0x68, 0xe1, 0x89, 0x06, 0x9e, 0x79, 0x08, 0xca, 0x95, 0xde, 0x70, 0x0b, 0xb2, 0x87, 0x50, 0x7c, 0xc7, 0x4d, 0xf6,
    0x58, 0x84, 0x46, 0x05, 0x04, 0x00, 0x3b};

} // namespace

static void combat_gif_textures() {
    // 3 x 2, four colours.
    const uint8_t small[] = {0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x03, 0x00, 0x02, 0x00, 0x81, 0x00,
                             0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
                             0xff, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x08,
                             0x08, 0x00, 0x01, 0x04, 0x10, 0x30, 0x40, 0x60, 0x40, 0x00, 0x3b};
    MemoryReadStream s(std::vector<uint8_t>(small, small + sizeof small));
    CombatImage image;
    CHECK(image.loadGif(s));
    CHECK_EQUAL(image.w, 3);
    CHECK_EQUAL(image.h, 2);
    CHECK_EQUAL(image.get(0, 0), 0xFFFF0000u);
    CHECK_EQUAL(image.get(1, 0), 0xFF00FF00u);
    CHECK_EQUAL(image.get(2, 0), 0xFF0000FFu);
    CHECK_EQUAL(image.get(0, 1), 0xFFFFFFFFu);
    CHECK_EQUAL(image.get(2, 1), 0xFF00FF00u);

    // Long enough for the codes to grow.
    MemoryReadStream p(std::vector<uint8_t>(kPatternGif, kPatternGif + sizeof kPatternGif));
    CombatImage pattern;
    CHECK(pattern.loadGif(p));
    CHECK_EQUAL(pattern.w, 32);
    bool same = true;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int k = (x * y + x) % 7;
            uint32_t want = 0xFF000000u | (uint32_t)(3 * k * 30 % 256) << 16 | (uint32_t)((3 * k + 1) * 30 % 256) << 8 |
                            (uint32_t)((3 * k + 2) * 30 % 256);
            same &= pattern.get(x, y) == want;
        }
    }
    CHECK(same);
}

static void combat_ras_and_model() {
    // A 3 x 1 rasterfile; its second colour is the see-through one.
    Bytes ras;
    be(ras, 0x59A66A95, 4);
    be(ras, 3, 4);
    be(ras, 1, 4);
    be(ras, 8, 4);
    be(ras, 4, 4);
    be(ras, 1, 4);
    be(ras, 1, 4);
    be(ras, 6, 4);
    ras.insert(ras.end(), {10, 255, 20, 0, 30, 163}); // R, G, B planes of two colours
    ras.insert(ras.end(), {0, 1, 0, 0});
    MemoryReadStream rs(ras);
    CombatImage console;
    CHECK(console.loadRas(rs));
    CHECK_EQUAL(console.get(0, 0), 0xFF0A141Eu);
    CHECK_EQUAL(console.get(1, 0) >> 24, 0u);

    // One textured triangle.
    Bytes block(0x2C, 0);
    block[4] = 1;
    uint32_t parts = (uint32_t)block.size();
    block[8] = (uint8_t)parts;
    Bytes part(44, 0);
    Bytes verts, uvs, faces, normals;
    const float v[3][3] = {{0, 0, 0}, {10, 0, 0}, {0, 20, 5}};
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++)
            f32(verts, v[i][k]);
        le(uvs, i * 0x8000, 4);
        le(uvs, 0x10000, 4);
    }
    le(faces, 2, 2);
    le(faces, 0, 2);
    le(faces, 1, 2);
    le(faces, 2, 2);
    f32(normals, 0);
    f32(normals, 0);
    f32(normals, -1);
    uint32_t base = parts + 44;
    auto put = [&](int at, uint32_t value) {
        for (int i = 0; i < 4; i++)
            part[at + i] = (uint8_t)(value >> (8 * i));
    };
    put(0, 3);
    put(4, 1);
    put(8, 2);
    put(0x0C, base);
    put(0x10, base + 36);
    put(0x1C, base + 60);
    put(0x24, base + 68);
    block.insert(block.end(), part.begin(), part.end());
    block.insert(block.end(), verts.begin(), verts.end());
    block.insert(block.end(), uvs.begin(), uvs.end());
    block.insert(block.end(), faces.begin(), faces.end());
    block.insert(block.end(), normals.begin(), normals.end());
    Bytes file;
    le(file, (uint32_t)block.size(), 4);
    le(file, 110, 2);
    le(file, 3, 2);
    le(file, 1, 2);
    file.resize(30, 0);
    file.insert(file.end(), block.begin(), block.end());
    MemoryReadStream ms(file);
    Model3D model;
    CHECK(model.load(ms));
    CHECK_EQUAL(model.parts.size(), (size_t)1);
    CHECK_EQUAL(model.parts[0].triangles.size(), (size_t)1);
    CHECK_EQUAL(model.parts[0].triangles[0].material, 2);
    CHECK(model.parts[0].vertices[2].y == 20.0f);
    CHECK(model.parts[0].vertices[1].u == 0.5f);
    CHECK(model.parts[0].triangles[0].nz == -1.0f);
}

static void combat_ship_classes_and_missions() {
    // A made-up class: a header, 27 systems, two shields' leak tables,
    // the phasers, the photons and the torpedo cones.
    std::string text = "# comment\nxyz 1000 80 50 3000 40 2 1 2 50\n";
    for (int s = 0; s < 27; s++) {
        bool shield = s == 3 || s == 4;
        int max = s == 1 || s == 2 ? -1 : shield ? 100 : 40;
        text += text::format("\t%d 30 30 90 %s /* SYS */\n", max,
                             s >= 13 ? (s == 13 ? "0145" : "") : (s >= 5 && s <= 12 ? "20 50" : "0 0"));
    }
    text += "  0 0 0 0 0 0 0 0 5 30 50 80 100\n  0 0 0 0 0 0 0 0 0 0 50 80 100\n";
    text += "  60 25000 30\n  90 35000 12 20\n  40 40 30 30 20 20 10 10 5 5\n";
    std::vector<ShipClass> classes;
    CHECK(ShipClass::loadAll(text, classes));
    CHECK_EQUAL(classes.size(), (size_t)1);
    const ShipClass &c = classes[0];
    CHECK_EQUAL(c.token, std::string("xyz"));
    CHECK_EQUAL(c.maxSpeed, 1000);
    CHECK_EQUAL(c.shields, 2);
    CHECK_EQUAL(c.systems[1].max, -1);
    CHECK_EQUAL(c.systems[3].max, 100);
    CHECK_EQUAL(c.systems[5].aimed, 50);
    CHECK_EQUAL(c.systems[13].arcs, 0x33); // octants 0, 1, 4, 5
    CHECK_EQUAL(c.bleed[3][8], 5);
    CHECK_EQUAL(c.bleed[4][10], 50);
    CHECK_EQUAL(c.phaserRange, 25000);
    CHECK_EQUAL(c.photonDamage, 12);
    CHECK_EQUAL(c.cone[9], 5);

    std::vector<MissionShip> ships;
    CHECK(loadCombatMission(":mission 0\n:wave 1\n#id type\n  0 player 0 0 100 0 0 500 standard p 0 0\n"
                            "  1 sm_klingon 10 -20 30 180 0 982 soran y 0 1\n:wave 2\n"
                            "  2 romulan 5 6 7 0 0 0 standard-7 n 0 0\n$ triggered X 0 null\n",
                            ships));
    CHECK_EQUAL(ships.size(), (size_t)3);
    CHECK_EQUAL(ships[1].type, std::string("sm_klingon"));
    CHECK_EQUAL(ships[1].y, -20);
    CHECK_EQUAL(ships[1].ai, std::string("soran"));
    CHECK_EQUAL(ships[2].wave, 2);
    CHECK_EQUAL(ships[2].ai, std::string("standard-7"));
}

static void combat_projection() {
    CombatRenderer r;
    r.setViewport(200, 100, 90);
    r.setCamera(Vec3(0, 0, 0), Mat3());
    double x, y, depth;
    CHECK(r.project(Vec3(0, 0, 100), x, y, depth));
    CHECK(x == 100 && y == 50);
    CHECK(r.project(Vec3(100, 50, 100), x, y, depth));
    CHECK(std::fabs(x - 200) < 1e-6 && std::fabs(y) < 1e-6);
    CHECK(!r.project(Vec3(0, 0, -100), x, y, depth));
    // Turning right brings the forward axis round to +x.
    Mat3 m;
    m.rotate(kPi / 2, 0, 0);
    CHECK(std::fabs(m.forward.x - 1) < 1e-6);
}

namespace {

// A bridge that only remembers what it's asked.
struct FakeBridge : public CampaignWorld {
    std::vector<std::string> commands;
    bool shown = false, audioEmpty = true, warping = false;
    double stardate = 100;
    void bridgeCommand(const std::string &command, const std::vector<std::string> &args) override {
        commands.push_back(command + (args.empty() ? "" : " " + args[0]));
        if (command != "move" && command != "setfleetflag" && command != "fleetalert")
            shown = true;
    }
    bool isBridgeShown() const override { return shown; }
    bool isEnterpriseAt(const std::string &sun) const override { return sun == "halee"; }
    bool getObjectFlag(const std::string &, int) const override { return false; }
    double getStardate() const override { return stardate; }
    bool isAudioQueueEmpty() const override { return audioEmpty; }
    bool isWarping() const override { return warping; }
    bool isZooming() const override { return false; }
    bool isEnterpriseIdle() const override { return !warping; }
};

const char *const kBridgeScript =
    "[Start]\nJump NewGame\n"
    "[NewGame]\nMove Enterprise Halee\nShowStellar\nQueAudio a.wav\nWait QueAudio\nOnTransport Down\n"
    "SDTimer 1.0\nWait Timer\nIfEnterpriseAt Halee Jump Late\n"
    "[Down]\nActionGame 3 Won Lost\n[Won]\nOnTactical Fight\nShowStellar\n"
    "[Late]\nSpaceCombat 4 Won Lost\n[Lost]\nOptions\n"
    "[Fight]\nSpaceCombat 2 Won Lost\n"
    "[FightFleet13]\nSpaceCombat 13 Back Back\n[Back]\nPop\n"
    "[EnterpriseDestroyed]\nScrollText 1\nOptions\n";

} // namespace

static void campaign_drives_the_bridge() {
    typedef Campaign::Action A;
    FakeBridge bridge;
    Campaign c;
    CHECK(c.load(kBridgeScript));
    c.setWorld(&bridge);
    c.start("Start");

    // Moving doesn't bring the bridge up; then one command a frame.
    CHECK_EQUAL(c.next().type, A::kBridge); // ShowStellar
    CHECK_EQUAL(bridge.commands[0], std::string("move Enterprise"));
    bridge.audioEmpty = false;
    CHECK_EQUAL(c.next().type, A::kBridge); // QueAudio
    CHECK_EQUAL(c.next().type, A::kBridge); // Wait QueAudio: held
    CHECK_EQUAL(c.next().type, A::kBridge); // still held
    bridge.audioEmpty = true;
    CHECK_EQUAL(c.next().type, A::kBridge); // released
    CHECK_EQUAL(c.next().type, A::kBridge); // OnTransport
    CHECK_EQUAL(c.next().type, A::kBridge); // SDTimer
    CHECK_EQUAL(c.next().type, A::kBridge); // Wait Timer: a stardate to go
    CHECK_EQUAL(c.getSection(), std::string("newgame"));

    // The player beams down in time.
    c.transport();
    Campaign::Action a = c.next();
    CHECK_EQUAL(a.type, A::kActionGame);
    CHECK_EQUAL(a.number, 3);
    c.missionEnded(true, false);
    CHECK_EQUAL(c.next().type, A::kBridge); // OnTactical
    CHECK_EQUAL(c.next().type, A::kBridge); // ShowStellar
    CHECK_EQUAL(c.next().type, A::kBridge); // the end of the section: waiting

    // A roaming fleet attacks, and the script picks up where it was.
    c.startCombat(13);
    a = c.next();
    CHECK_EQUAL(a.type, A::kSpaceCombat);
    CHECK_EQUAL(a.number, 13);
    c.combatEnded(Campaign::kCombatWon);
    CHECK_EQUAL(c.next().type, A::kBridge); // Pop
    CHECK_EQUAL(c.getSection(), std::string("won"));
    CHECK(std::find(bridge.commands.begin(), bridge.commands.end(), std::string("setfleetflag 13")) !=
          bridge.commands.end());
    CHECK(std::find(bridge.commands.begin(), bridge.commands.end(), std::string("fleetalert 13")) !=
          bridge.commands.end());

    // Engaging the fleet OnTactical names; the Enterprise is destroyed.
    c.startCombat(-1);
    a = c.next();
    CHECK_EQUAL(a.type, A::kSpaceCombat);
    CHECK_EQUAL(a.number, 2);
    c.combatEnded(Campaign::kEnterpriseDestroyed);
    CHECK_EQUAL(c.next().type, A::kScrollText);

    // Timers: the deadline passes as the stardate climbs.
    c.start("NewGame");
    for (int i = 0; i < 7; i++)
        c.next();
    CHECK_EQUAL(c.getSection(), std::string("newgame"));
    bridge.stardate += 1.0;
    c.next(); // Wait Timer, over
    c.next(); // IfEnterpriseAt Halee Jump Late
    a = c.next();
    CHECK_EQUAL(a.type, A::kSpaceCombat);
    CHECK_EQUAL(a.number, 4);
}

static void combat_shield_materials() {
    // One triangle facing the camera, then the same seen from behind.
    Model3D model;
    model.parts.resize(1);
    Model3D::Part &part = model.parts[0];
    part.vertices = {{-50, -50, 0, 0, 0}, {50, -50, 0, 0, 0}, {0, 50, 0, 0, 0}};
    part.triangles.push_back({0x8000, 0, 1, 2, 0, 0, -1});
    std::vector<CombatMaterial> materials(1);
    materials[0].colour = 0x7C00; // red
    materials[0].flags = 3;
    auto none = [](const std::string &) { return (const CombatImage *)nullptr; };

    Canvas screen;
    screen.create(40, 40, Canvas::kArgb32);
    auto clear = [&](uint32_t colour) {
        for (int y = 0; y < 40; y++)
            for (int x = 0; x < 40; x++)
                *(uint32_t *)screen.getBasePtr(x, y) = colour;
    };
    auto middle = [&]() { return *(const uint32_t *)screen.getBasePtr(20, 20) & 0xFFFFFF; };
    CombatRenderer r;
    r.setViewport(40, 40, 90);
    r.setCamera(Vec3(0, 0, -100), Mat3());

    // The face's top bit is a flag, not part of the material's index.
    clear(0xFF000000u);
    r.begin(screen, 0, 0);
    r.drawModel(model, materials, none, Vec3(), Mat3());
    CHECK_EQUAL(middle() >> 16, 0xFFu * 55 / 100);

    // A shield's material is see-through, lit by itself: half and half.
    materials[0].param = 11000;
    materials[0].flags = 0;
    clear(0xFF0000FFu);
    r.begin(screen, 0, 0);
    r.drawModel(model, materials, none, Vec3(), Mat3());
    CHECK_EQUAL(middle(), 0x7F007Fu);

    // Without the top bit it has one side only.
    part.triangles[0].material = 0;
    part.triangles[0].nz = 1;
    clear(0xFF0000FFu);
    r.begin(screen, 0, 0);
    r.drawModel(model, materials, none, Vec3(), Mat3());
    CHECK_EQUAL(middle(), 0x0000FFu);
}

static const reluva::test::Suite suite(
    "stgenerations.unit.combat",
    {
        {"combat gif textures", [](const std::filesystem::path &) { combat_gif_textures(); }},
        {"combat ras and model", [](const std::filesystem::path &) { combat_ras_and_model(); }},
        {"combat ship classes and missions", [](const std::filesystem::path &) { combat_ship_classes_and_missions(); }},
        {"combat projection", [](const std::filesystem::path &) { combat_projection(); }},
        {"campaign drives the bridge", [](const std::filesystem::path &) { campaign_drives_the_bridge(); }},
        {"combat shield materials", [](const std::filesystem::path &) { combat_shield_materials(); }},
    });
