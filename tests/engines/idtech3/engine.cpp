// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The id Tech 3 engine as a whole, without any game's files: the basic
// engine test, run by ctest and CI.
//
// It makes a small install of its own and takes it through what playing
// Elite Force does:
//   - the game folder mounted (paks, a later pak and a loose file
//     overriding earlier ones) and detected;
//   - its shaders, a map and a model read;
//   - a level started on the server: game code (Reluva's own test QVM,
//     tests/engines/idtech3/data/qvm/fake_game.c);
//   - a player joining over the network (in memory, losing a datagram in
//     four), moving about, map_restart, and leaving;
//   - the client game taking its snapshots and drawing scenes (fake
//     cgame);
//   - the menus starting, drawing and taking keys (fake ui).
//
// The unit tests (tests/engines/idtech3/unit/) check each part's values. This writes
// everything the run produces into a record, one line per step: counts,
// and checksums of states, snapshots and scenes. The record is compared
// with tests/engines/idtech3/data/engine-record.txt, so a change anywhere that
// changes what the engine does (in common/, say) is caught, even where no
// unit test looks.
//
//   reluva-tests idtech3.engine [--update]
//
// --update writes the run's record as the new one. Do that only after a
// change meant to alter behaviour, and look at the record's diff before
// committing it.

#include "common/archive.h"
#include "common/net.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/shader.h"
#include "engines/registry.h"

#include "tests/engines/idtech3/data/qvm/fake_cgame_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_game_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_ui_qvm.h"
#include "tests/engines/idtech3/fixtures.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::idtech3;
namespace ef = reluva::idtech3::eliteforce;

namespace {

// FNV-1a, 64 bits, over everything added: a checksum that's the same on
// every machine.
class Hash {
public:
    Hash& bytes(const void* data, size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            h_ ^= p[i];
            h_ *= 0x100000001b3ull;
        }
        return *this;
    }
    Hash& bytes(const std::vector<uint8_t>& v) { return bytes(v.data(), v.size()); }
    Hash& text(const std::string& s) { return bytes(s.data(), s.size() + 1); }
    Hash& i32(int32_t v) { return bytes(&v, 4); }
    Hash& f32(float v) { return bytes(&v, 4); }
    std::string hex() const {
        char out[17];
        std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h_));
        return out;
    }

private:
    uint64_t h_ = 0xcbf29ce484222325ull;
};

// The run's record: "step: value" lines, in order.
class Record {
public:
    void add(const std::string& step, const std::string& value) { lines_.push_back(step + ": " + value); }
    void add(const std::string& step, int64_t value) { add(step, std::to_string(value)); }
    const std::vector<std::string>& lines() const { return lines_; }
    // A step that couldn't be done at all.
    void fail(const std::string& step, const std::string& why) {
        add(step, "FAILED " + why);
        ++failures;
    }
    int failures = 0;

private:
    std::vector<std::string> lines_;
};

std::vector<uint8_t> bytesOf(const uint8_t* begin, size_t size) { return {begin, begin + size}; }

// The install: two paks and a loose file in the game's folder.
void makeInstall(const std::filesystem::path& dir) {
    using test::toBytes;
    std::filesystem::remove_all(dir);
    const std::string shaders = "textures/test/wall\n{\n\tsurfaceparm metalsteps\n\tcull none\n"
                                "\t{\n\t\tmap textures/test/wall.tga\n\t\trgbGen identity\n\t}\n"
                                "\t{\n\t\tmap $lightmap\n\t\tblendFunc filter\n\t}\n}\n"
                                "textures/test/glow\n{\n\tdeformVertexes wave 64 sin 0 4 0 0.5\n"
                                "\t{\n\t\tanimMap 4 textures/test/a.tga textures/test/b.tga\n"
                                "\t\tblendFunc add\n\t\ttcMod scroll 0.5 0\n\t}\n}\n";
    test::writeFile(dir / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/qagame.qvm", bytesOf(kFakeGameQvm, sizeof kFakeGameQvm), {}},
                                    {"vm/cgame.qvm", bytesOf(kFakeCgameQvm, sizeof kFakeCgameQvm), {}},
                                    {"vm/ui.qvm", bytesOf(kFakeUiQvm, sizeof kFakeUiQvm), {}},
                                    {"maps/test.bsp", fixtures::buildBsp(), {}},
                                    {"models/test/box.md3", fixtures::buildMd3(), {}},
                                    {"scripts/test.shader", toBytes(shaders), {}},
                                    {"scripts/test.txt", toBytes("from pak0\n"), {}},
                                    {"scripts/other.txt", toBytes("x"), {}}}));
    // A later pak overrides the first; a loose file overrides both.
    test::writeFile(dir / "BaseEF" / "pak1.pk3",
                    test::buildZip({{"scripts/test.txt", toBytes("hello world\n"), {}},
                                    {"scripts/patched.txt", toBytes("from pak1\n"), {}}}));
    test::writeFile(dir / "BaseEF" / "scripts" / "patched.txt", toBytes("loose\n"));
}

std::string text(const SearchSet& set, const char* name) {
    std::vector<uint8_t> data;
    if (set.read(name, data))
        return "(missing)";
    std::string s(data.begin(), data.end());
    while (!s.empty() && s.back() == '\n')
        s.pop_back();
    return s;
}

void install(const std::filesystem::path& dir, const SearchSet& set, Record& rec) {
    Hash list;
    const auto names = set.list();
    for (const std::string& n : names)
        list.text(n);
    rec.add("install.files", std::to_string(names.size()) + " " + list.hex());
    rec.add("install.override.pak1", text(set, "scripts/test.txt"));
    rec.add("install.override.loose", text(set, "scripts/patched.txt"));
    std::string found;
    for (const DetectedGame& g : detectGames(dir))
        found += (found.empty() ? "" : "; ") + g.engineId + "/" + g.gameId + " " + g.description;
    rec.add("install.detected", found.empty() ? "(none)" : found);
}

void files(const SearchSet& set, Record& rec) {
    ShaderLibrary shaders;
    std::vector<std::string> warnings;
    shaders.load(set, &warnings);
    Hash h;
    for (const ShaderDef& s : shaders.all()) {
        h.text(s.name).i32(int32_t(s.stages.size())).i32(int32_t(s.cull)).i32(int32_t(s.surfaceParms.size()));
        for (const auto& p : s.surfaceParms)
            h.text(p);
    }
    rec.add("shaders", std::to_string(shaders.size()) + " " + std::to_string(warnings.size()) + " warnings " + h.hex());

    std::vector<uint8_t> data;
    Bsp bsp;
    if (set.read("maps/test.bsp", data) || parseBsp(data, bsp)) {
        rec.fail("map", "not read");
    } else {
        Hash m;
        for (const auto& v : bsp.vertices)
            m.bytes(&v, sizeof v);
        for (int32_t i : bsp.indices)
            m.i32(i);
        m.text(bsp.entities);
        rec.add("map", std::to_string(bsp.vertices.size()) + " vertices, " + std::to_string(bsp.indices.size()) +
                           " indices, " + std::to_string(bsp.leafs.size()) + " leafs " + m.hex());
    }

    Md3Model model;
    if (set.read("models/test/box.md3", data) || parseModel(data, model)) {
        rec.fail("model", "not read");
    } else {
        Hash m;
        for (int f = 0; f < 2; ++f)
            if (const auto* tag = model.tag(f, "tag_top"))
                m.f32(tag->origin[0]).f32(tag->origin[1]).f32(tag->origin[2]);
        rec.add("model", std::to_string(model.frames.size()) + " frames " + m.hex());
    }
}

// The map's solid parts: boxes and points swept across it, straight
// through the engine's collision (every value as computed), and through
// the game code's calls (as it sees them: what's hit, where, in eighths).
void world(ef::GameHost& game, const Bsp& bsp, Record& rec) {
    CollisionWorld collision(bsp);
    Hash direct;
    int hits = 0;
    for (int x = -320; x <= 320; x += 40)
        for (int y = -96; y <= 96; y += 48)
            for (const float size : {0.f, 8.f, 15.f}) {
                const Vec3f start{float(x) + 0.25f, float(y), 300}, end{float(x) - 3.5f, float(y) + 1, -200};
                const Vec3f mins{-size, -size, -size}, maxs{size, size, size};
                for (int model : {0, 1}) {
                    const TraceResult t = collision.trace(start, end, mins, maxs, model, -1);
                    hits += t.fraction < 1;
                    direct.f32(t.fraction).f32(t.endPos[0]).f32(t.endPos[1]).f32(t.endPos[2]);
                    direct.f32(t.normal[0]).f32(t.normal[1]).f32(t.normal[2]).i32(t.contents);
                    direct.i32(t.allSolid).i32(t.startSolid);
                }
                const TraceResult turned = collision.transformedTrace(start, end, mins, maxs, 1, -1, {200, 0, 0},
                                                                      {0, float(x) / 4, 0});
                direct.f32(turned.fraction).f32(turned.endPos[0]).f32(turned.endPos[2]);
                direct.i32(collision.pointContents({float(x), float(y), -size}, 0));
            }
    rec.add("world.collision", std::to_string(hits) + " hits " + direct.hex());

    Hash viaGame;
    int32_t r = 0;
    for (int x = -320; x <= 320; x += 40)
        for (int size : {0, 16}) {
            for (int call : {24, 25, 26}) {
                const std::vector<int32_t> args = call == 24 ? std::vector<int32_t>{x, 0, 500, size, -1}
                                                             : std::vector<int32_t>{};
                viaGame.i32(game.call(call, args, r) ? -999999 : r);
            }
            viaGame.i32(game.call(28, {x, 0, -size}, r) ? -999999 : r);
            viaGame.i32(game.call(29, {x, 0, 16}, r) ? -999999 : r);
        }
    for (int e = 0; e < 4; ++e)
        viaGame.i32(game.call(27, {e}, r) ? -999999 : r).i32(game.call(30, {e}, r) ? -999999 : r);
    rec.add("world.game", viaGame.hex());
}

// The level on a server, a player joining over the network, the client
// game drawing what it's sent.
void play(const SearchSet& set, Record& rec) {
    CvarStore cvars;
    ef::GameHost game(set, cvars);
    if (Error e = game.load())
        return rec.fail("server.load", e.message);
    auto map = fixtures::collisionMap();
    map->entities = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n"
                    "\"origin\" \"200 0 0\"\n}\n{\n\"classname\" \"item_health\"\n\"origin\" \"-200 0 16\"\n}\n"
                    "{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"0 0 300\"\n}\n";
    if (Error e = game.startLevel("borg2", map, 1234))
        return rec.fail("server.start", e.message);
    {
        Hash cs;
        for (const auto& [index, value] : game.configstrings())
            cs.i32(index).text(value);
        rec.add("server.start", "level time " + std::to_string(game.levelTime()) + ", " +
                                    std::to_string(game.linked().size()) + " linked, configstrings " + cs.hex());
    }
    world(game, *map, rec);

    // The network, in memory: every fourth datagram each way lost once the
    // player is in.
    auto network = net::LoopbackNetwork::create();
    cvars.create("sv_lanForceRate", "1", 0);
    auto serverOwned = std::make_unique<ef::NetServer>(game, cvars, network->open(27960));
    ef::NetServer& server = *serverOwned;
    server.levelStarted();
    ef::NetClient client(network->open(0), {0x7f000001u, 27960}, "\\name\\Tester\\snaps\\20", 77);
    uint64_t now = 1000;
    auto tick = [&] {
        client.frame(now);
        if (Error e = server.receive(now))
            rec.fail("server.receive", e.message);
        if (Error e = game.runFrame())
            rec.fail("server.frame", e.message);
        if (Error e = server.frame(now))
            rec.fail("server.send", e.message);
        now += 50;
    };
    int ticks = 0;
    for (; ticks < 40 && client.state() != ef::NetClient::State::Primed; ++ticks)
        tick();
    if (client.state() != ef::NetClient::State::Primed || !client.takeNewGamestate())
        return rec.fail("client.connect", "no game state after " + std::to_string(ticks) + " frames");
    {
        Hash cs;
        for (const auto& [index, value] : client.configstrings())
            cs.i32(index).text(value);
        rec.add("client.connect", std::to_string(ticks) + " frames, configstrings " + cs.hex());
    }
    client.setLoaded();

    // The client game, given the game state the client received.
    CvarStore clientCvars;
    ef::CgameHost cgame(set, clientCvars);
    if (Error e = cgame.load())
        return rec.fail("cgame.load", e.message);
    cgame.setConfigstrings(client.configstrings());
    int32_t r = 0;
    if (Error e = cgame.call(ef::CgameHost::Init, {0, 0, 0}, r))
        return rec.fail("cgame.init", e.message);

    network->setDropFilter([](const net::Address&, const net::Address&, size_t index) { return index % 4 == 3; });
    // A stretch of play: the player runs forward, then strafes; every tenth
    // frame, the server's and the client's states go into the record.
    auto playFor = [&](const std::string& step, int frames, int8_t forward, int8_t right) {
        Hash server, snapshots, scenes;
        int got = 0;
        for (int i = 0; i < frames; ++i) {
            ef::GameHost::UserCommand cmd;
            cmd.serverTime = game.levelTime();
            cmd.forward = forward;
            cmd.right = right;
            client.addCommand(cmd);
            cgame.addUserCommand(cmd);
            tick();
            for (const int32_t e : game.linked())
                server.i32(e).bytes(game.entityState(e));
            for (auto& snap : client.takeSnapshots()) {
                ++got;
                snapshots.i32(snap.serverTime).i32(snap.snapFlags).bytes(snap.playerState);
                for (const auto& e : snap.entities)
                    snapshots.bytes(e);
                cgame.newSnapshot(snap);
            }
            cgame.setServerCommands(client.serverCommands());
            // The client game's view of it: the latest snapshot, then a scene.
            for (int call : {25, 26, 27})
                if (!cgame.call(call, {}, r))
                    scenes.i32(r);
            if (!cgame.call(31, {}, r)) {
                for (const auto& e : cgame.scene().entities)
                    scenes.bytes(e.bytes);
                for (const auto& v : cgame.scene().views)
                    scenes.bytes(v.refdef);
            }
        }
        rec.add(step + ".server", server.hex());
        rec.add(step + ".snapshots", std::to_string(got) + " " + snapshots.hex());
        rec.add(step + ".cgame", scenes.hex());
    };
    playFor("play.forward", 40, 127, 0);
    playFor("play.strafe", 30, 0, -127);
    const Vec3f where = game.entityVec(4, 272);
    rec.add("play.player", std::to_string(int(where[0])) + " " + std::to_string(int(where[1])) + " " +
                               std::to_string(int(where[2])));
    std::string announced = "no";
    for (const std::string& c : client.serverCommands())
        if (c.find("entered the game") != std::string::npos)
            announced = "yes";
    rec.add("play.announced", announced);

    // map_restart: the level starts over in place, the player back in it.
    bool handled = false;
    if (Error e = game.serverCommand("map_restart 0", &handled))
        return rec.fail("play.restart", e.message);
    rec.add("play.restart", std::string(handled ? "handled" : "not handled") + ", restarts " +
                                std::to_string(game.restarts()));
    playFor("play.after-restart", 20, 127, 0);

    // Leaving.
    network->setDropFilter(nullptr);
    client.disconnect();
    for (int i = 0; i < 4; ++i)
        tick();
    rec.add("play.left", std::to_string(server.players().size()) + " players, slot 0 " +
                             (game.clientSlotFree(0) ? "free" : "taken"));
    serverOwned.reset();
}

// The menus: started, a menu open and drawn, keys typed.
void menus(const SearchSet& set, Record& rec) {
    CvarStore cvars;
    ef::UiHost ui(set, cvars);
    if (Error e = ui.load())
        return rec.fail("menus.load", e.message);
    int32_t r = 0;
    if (Error e = ui.call(ef::UiHost::Init, {}, r))
        return rec.fail("menus.init", e.message);
    rec.add("menus.init", std::to_string(r) + ", printed " + std::to_string(ui.log().size()) + " characters");
    ui.call(ef::UiHost::SetActiveMenu, {1});
    Hash pictures;
    for (int frame = 0; frame < 5; ++frame) {
        ui.call(ef::UiHost::MouseEvent, {7, 3});
        ui.call(ef::UiHost::Refresh, {1000 + frame * 50}, r);
        for (const auto& p : ui.pictures())
            pictures.f32(p.x).f32(p.y).f32(p.w).f32(p.h).f32(p.s1).f32(p.t1).f32(p.s2).f32(p.t2)
                .text(ui.shaders().name(p.shader));
    }
    rec.add("menus.drawn", pictures.hex());
    for (int key : {'R' | 1024, 'e' | 1024, 'l' | 1024})
        ui.call(ef::UiHost::KeyEvent, {key, 1});
    ui.call(27, {}, r);
    rec.add("menus.typed", r);
    cvars.set("ui_number", "16");
    ui.call(24, {}, r);
    rec.add("menus.cvar", r);
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);)
        if (!line.empty() && line[0] != '#')
            out.push_back(line);
    return out;
}

// The run, its record against the committed one (or, with --update,
// written as the new one).
void engineTest(const std::filesystem::path& tmp) {
    const std::filesystem::path recordPath = RELUVA_SOURCE_DIR "/tests/engines/idtech3/data/engine-record.txt";
    const std::filesystem::path dir = tmp / "install";
    makeInstall(dir);
    Record rec;
    SearchSet set;
    std::vector<std::string> warnings;
    if (Error e = mountGame(set, dir, {}, "BaseEF", "", warnings))
        return test::fail("mounting the test install: " + e.message);
    install(dir, set, rec);
    files(set, rec);
    play(set, rec);
    menus(set, rec);
    for (const std::string& line : rec.lines())
        if (line.find(": FAILED ") != std::string::npos)
            test::fail(line);

    if (test::option("update")) {
        std::filesystem::create_directories(recordPath.parent_path());
        std::ofstream out(recordPath);
        out << "# The id Tech 3 engine test's record (tests/engines/idtech3/engine.cpp).\n"
               "# Written by `reluva-tests idtech3.engine --update`; review its diff before committing.\n";
        for (const std::string& line : rec.lines())
            out << line << "\n";
        std::printf("Record written to %s (%zu lines)\n", recordPath.string().c_str(), rec.lines().size());
        return;
    }

    const std::vector<std::string> want = readLines(recordPath);
    if (want.empty())
        return test::fail("no record at " + recordPath.string() + ": make one with reluva-tests idtech3.engine --update");
    int differ = 0;
    const size_t n = std::max(want.size(), rec.lines().size());
    for (size_t i = 0; i < n; ++i) {
        const std::string got = i < rec.lines().size() ? rec.lines()[i] : "(nothing)";
        const std::string expected = i < want.size() ? want[i] : "(nothing)";
        if (got == expected) {
            std::printf("  ok    %s\n", got.c_str());
            continue;
        }
        ++differ;
        std::printf("  DIFF  %s\n        was %s\n", got.c_str(), expected.c_str());
    }
    if (differ)
        test::fail(std::to_string(differ) + " line(s) differ from " + recordPath.string() +
                   ". If the change is meant, run reluva-tests idtech3.engine --update and review the record's diff.");
}

} // namespace

static const reluva::test::Suite suite("idtech3.engine", {
    {"play through", [](const std::filesystem::path& tmp) { engineTest(tmp); }},
});
