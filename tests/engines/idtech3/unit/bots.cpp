// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: The bot library: navigation (AAS), scripts, characters, the AI, chat.

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

// A small .aas file: three areas in a row along x (below 100, 100 to
// 200, above 200), split by two planes, walkable both ways between
// neighbours; version 5's header scrambled when `scramble`.
static std::vector<uint8_t> buildAas(bool scramble) {
    std::vector<std::vector<uint8_t>> lumps(14);
    auto i32 = [](std::vector<uint8_t>& d, int32_t v) {
        for (int k = 0; k < 4; ++k)
            d.push_back(uint8_t(uint32_t(v) >> (8 * k)));
    };
    auto f32 = [&](std::vector<uint8_t>& d, float f) {
        int32_t v;
        std::memcpy(&v, &f, 4);
        i32(d, v);
    };
    // Planes 0 (dummy), x = 100, x = 200.
    for (float dist : {0.0f, 100.0f, 200.0f}) {
        f32(lumps[2], 1), f32(lumps[2], 0), f32(lumps[2], 0), f32(lumps[2], dist), i32(lumps[2], 0);
    }
    // Areas 0 (dummy) to 3: number, faces, mins, maxs, center.
    for (int a = 0; a < 4; ++a) {
        i32(lumps[7], a), i32(lumps[7], 0), i32(lumps[7], 0);
        const float x0 = float(a - 1) * 100, x1 = float(a) * 100;
        for (float v : {x0, -50.0f, 0.0f, x1, 50.0f, 50.0f, (x0 + x1) / 2, 0.0f, 25.0f})
            f32(lumps[7], v);
    }
    // Settings: grounded, standing; reachabilities: area 1 -> 2 (index 1),
    // 2 -> 1 and 2 -> 3 (2, 3), 3 -> 2 (4); index 0 a dummy.
    const int first[4] = {0, 1, 2, 4}, count[4] = {0, 1, 2, 1};
    for (int a = 0; a < 4; ++a)
        for (int v : {0, 1, 2, a == 0 ? 0 : 1, 0, count[a], first[a]})
            i32(lumps[8], v);
    struct R {
        int to;
        float x0, x1;
    };
    const R reach[] = {{0, 0, 0}, {2, 95, 105}, {1, 105, 95}, {3, 195, 205}, {2, 205, 195}};
    for (const R& r : reach) {
        i32(lumps[9], r.to), i32(lumps[9], 0), i32(lumps[9], 0);
        for (float v : {r.x0, 0.0f, 0.0f, r.x1, 0.0f, 0.0f})
            f32(lumps[9], v);
        i32(lumps[9], r.to ? 2 : 0); // walking
        lumps[9].push_back(10), lumps[9].push_back(0), lumps[9].push_back(0), lumps[9].push_back(0);
    }
    // Nodes: 0 dummy; 1 splits at x = 100 (front: node 2, back: area 1);
    // 2 at x = 200 (front: area 3, back: area 2).
    for (int v : {0, 0, 0, 1, 2, -1, 2, -3, -2})
        i32(lumps[10], v);
    std::vector<uint8_t> out = {'E', 'A', 'A', 'S'};
    i32(out, scramble ? 5 : 4);
    std::vector<uint8_t> header;
    i32(header, 1234); // the BSP's checksum
    int32_t at = 12 + 14 * 8;
    for (const auto& l : lumps) {
        i32(header, at), i32(header, int32_t(l.size()));
        at += int32_t(l.size());
    }
    if (scramble)
        for (size_t i = 0; i < header.size(); ++i)
            header[i] ^= uint8_t(i * 119);
    out.insert(out.end(), header.begin(), header.end());
    for (const auto& l : lumps)
        out.insert(out.end(), l.begin(), l.end());
    return out;
}

static void testAas() {
    using namespace reluva::idtech3::botlib;
    for (bool scramble : {false, true}) {
        AasWorld aas;
        CHECK(!aas.load(buildAas(scramble)));
        CHECK(aas.loaded() && aas.areaCount() == 4 && aas.bspChecksum() == 1234);
        CHECK(aas.pointArea({50, 0, 10}) == 1 && aas.pointArea({150, 0, 10}) == 2 && aas.pointArea({250, 0, 10}) == 3);
        // Area 1 to 3: two reachabilities (10 each), crossing area 2 from
        // 105 to 195 (90 units walked, 0.33 a unit: 29), plus 1 for the goal.
        int time = 0, reach = 0;
        CHECK(aas.route(1, nullptr, 3, tfl::Default, time, reach));
        CHECK(reach == 1 && time == 1 + 10 + 10 + 29);
        // From a point: plus walking to the reachability's start.
        const Vec3f at{35, 0, 0};
        CHECK(aas.travelTime(1, at, 3, tfl::Default) == time + 19);
        CHECK(aas.route(3, nullptr, 1, tfl::Default, time, reach) && reach == 4);
        CHECK(aas.route(2, nullptr, 2, tfl::Default, time, reach) && time == 1);
        // Without walking, no way.
        CHECK(!aas.route(1, nullptr, 3, tfl::Jump | tfl::Air, time, reach));
        // Area 2 turned off: no way through.
        aas.enableArea(2, false);
        CHECK(!aas.route(1, nullptr, 3, tfl::Default, time, reach));
        aas.enableArea(2, true);
        CHECK(aas.route(1, nullptr, 3, tfl::Default, time, reach));
        std::vector<int> areas;
        std::vector<Vec3f> points;
        CHECK(aas.traceAreas({50, 0, 0}, {250, 0, 0}, areas, &points, 10) == 3);
        CHECK(areas == std::vector<int>({1, 2, 3}) && std::fabs(points[1][0] - 100) < 0.01f);
        CHECK(aas.nextReachability(2, 0) == 2 && aas.nextReachability(2, 2) == 3 && aas.nextReachability(2, 3) == 0);
    }
}

static void testBotScript() {
    using namespace reluva::idtech3::botlib;
    const std::map<std::string, std::string> files = {
        {"defs.h", "#define TWO 2\n#define SQUARE(x) ((x) * (x))\n#define NAME \"bot\" \"name\"\n"},
        {"main.c", "#include \"defs.h\"\n"
                   "#ifdef TWO\nfirst TWO\n#else\nnot this\n#endif\n"
                   "#if TWO > 3 || !defined(TWO)\nnor this\n#elif SQUARE(TWO) == 4\nsquare SQUARE(3)\n#endif\n"
                   "#ifndef THREE\n#define THREE 3\n#endif\n"
                   "value $evalint(TWO * THREE + 1) $evalfloat(1 / 2.0) NAME -1.5\n"
                   "/* a comment */ end // another\n"}};
    BotScript script([&](const std::string& path, std::string& text) {
        const auto it = files.find(path);
        if (it == files.end())
            return false;
        text = it->second;
        return true;
    });
    CHECK(script.open("main.c"));
    std::vector<std::string> words;
    BotToken t;
    while (script.next(t))
        words.push_back(t.text);
    if (!script.error().empty())
        std::fprintf(stderr, "%s\n", script.error().c_str());
    CHECK(script.error().empty());
    const std::vector<std::string> expected = {"first", "2", "square", "(", "(", "3", ")", "*", "(", "3", ")", ")",
                                               "value", "7.000000", "0.500000", "botname", "-", "1.5", "end"};
    CHECK(words == expected);
    BotScript bad([](const std::string&, std::string&) { return false; });
    CHECK(!bad.open("missing.c") && !bad.error().empty());
}

static void testBotCharacter() {
    using namespace reluva::idtech3::botlib;
    const std::map<std::string, std::string> files = {
        {"bots/default_c.c", "skill 1 { 0 \"Default\" 1 0.5 2 7 }\nskill 4 { 0 \"Default\" 1 0.8 2 9 }\n"
                             "skill 5 { 0 \"Default\" 1 1.0 2 9 }\n"},
        {"bots/test_c.c", "#define NAME 0\nskill 1 { NAME \"Test\" 1 0.25 }\nskill 4 { NAME \"Test\" 1 0.75 }\n"}};
    const BotScript::Loader load = [&](const std::string& path, std::string& text) {
        const auto it = files.find(path);
        if (it == files.end())
            return false;
        text = it->second;
        return true;
    };
    BotCharacters chars;
    const int a = chars.load(load, "bots/test_c.c", 1);
    CHECK(a && chars.string(a, 0) == "Test" && chars.floatValue(a, 1) == 0.25f);
    CHECK(chars.integer(a, 2) == 7); // from the default character
    // Between 1 and 4: floats blended.
    const int b = chars.load(load, "bots/test_c.c", 2.5f);
    CHECK(b && std::fabs(chars.floatValue(b, 1) - 0.5f) < 1e-5f && chars.integer(b, 2) == 7);
    // Skill 5 isn't in the file: the default character's.
    const int c = chars.load(load, "bots/test_c.c", 5);
    CHECK(c && chars.string(c, 0) == "Default" && chars.floatValue(c, 1) == 1.0f);
    CHECK(chars.boundedFloat(a, 1, 0.5f, 1) == 0.5f && chars.boundedInteger(a, 2, 0, 5) == 5);
    CHECK(!chars.load(load, "bots/none_c.c", 1) == false); // falls back to the default character
}

static void testBotAi() {
    using namespace reluva::idtech3::botlib;
    const std::map<std::string, std::string> files = {
        {"botfiles/w.c", "weight \"a\" { switch(1) { case 2: return 10; case 5: { switch(2) { default: return 30; } }"
                         " default: return balance(50, 40, 60); } }\nweight \"b\" return 7;\n"
                         "weight \"item_armor\" return 100;\nweight \"Phaser\" { switch(3) { case 1: return 0; default: return 20; } }\n"},
        {"botfiles/items.c", "iteminfo \"item_armor\" { name \"Armor\" model \"m.md3\" modelindex 3 type 1 index 2"
                             " respawntime 25 mins {-15,-15,-15} maxs {15,15,15} }\n"},
        {"botfiles/weapons.c", "projectileinfo { name \"bolt\" damage 20 radius 1.5 }\n"
                               "weaponinfo { number 2 name \"Phaser\" projectile \"bolt\" speed 0 offset {0, 5, -10} }\n"}};
    auto read = [&](const std::string& path, std::vector<uint8_t>& data) {
        if (path == "maps/test.aas") {
            data = buildAas(true);
            return true;
        }
        const auto it = files.find(path);
        if (it == files.end())
            return false;
        data.assign(it->second.begin(), it->second.end());
        return true;
    };
    BotLibrary lib;
    lib.readFile = read;
    // Fuzzy weights: a case while the count is below it.
    auto w = readWeightConfig(lib.scriptLoader(), "w.c");
    CHECK(w && w->weights.size() == 4);
    std::vector<int> inv(256, 0);
    CHECK(w->evaluate(w->find("a"), inv) == 10);
    inv[1] = 3;
    CHECK(w->evaluate(w->find("a"), inv) == 30);
    inv[1] = 9;
    CHECK(w->evaluate(w->find("a"), inv) == 50 && w->evaluate(w->find("b"), inv) == 7);
    std::vector<WeaponInfo> weapons;
    CHECK(readWeaponConfig(lib.scriptLoader(), "weapons.c", weapons) && weapons[2].valid);
    CHECK(weapons[2].proj.damage == 20 && weapons[2].offset[2] == -10 && weapons[2].proj.radius == 1.5f);

    // A level with an armour in area 2, its entity seen twice in place.
    lib.entityCount = 1;
    lib.entityKey = [](int e, const std::string& key, std::string& value) {
        if (e != 1)
            return false;
        if (key == "classname")
            value = "item_armor";
        else if (key == "origin")
            value = "150 0 10";
        else
            return false;
        return true;
    };
    CHECK(lib.setup() == blerr::None && lib.loadMap("test") == blerr::None);
    BotEntityState item;
    item.type = 2;
    item.modelIndex = 3;
    item.origin = {150, 0, 10};
    for (float t : {0.1f, 0.2f}) {
        lib.startFrame(t);
        lib.updateEntity(5, &item);
    }
    lib.updateEntityItems();
    const int gs = lib.allocGoalState(0);
    CHECK(lib.loadItemWeights(gs, "w.c") == blerr::None);
    CHECK(lib.chooseLtgItem(gs, {50, 0, 0}, inv, tfl::Default));
    BotGoal goal;
    CHECK(lib.topGoal(gs, goal) && goal.area == 2 && goal.entity == 5 && (goal.flags & goalflag::Item));
    CHECK(lib.goalName(goal.number) == "Armor");
    // Avoided for a while once chosen.
    lib.popGoal(gs);
    CHECK(!lib.chooseLtgItem(gs, {50, 0, 0}, inv, tfl::Default));
    const int ws = lib.allocWeaponState();
    CHECK(lib.loadWeaponWeights(ws, "w.c") == blerr::None);
    inv[3] = 1;
    CHECK(lib.chooseBestFightWeapon(ws, inv) == 2);

    // Moving: from area 1 towards area 3, walking along +x to the first
    // reachability's start.
    const int ms = lib.allocMoveState();
    BotInitMove init;
    init.origin = {30, 0, 0};
    init.client = 1;
    init.presence = presence::Normal;
    init.orMoveFlags = moveflag::OnGround;
    lib.initMoveState(ms, init);
    BotGoal far;
    far.area = 3;
    far.origin = {250, 0, 0};
    const BotMoveResult r = lib.moveToGoal(ms, far, tfl::Default);
    CHECK(!r.failure && (r.travelType & travel::TypeMask) == travel::Walk);
    const BotInput in = lib.getInput(1, 0.1f);
    CHECK(in.dir[0] > 0.99f && in.speed == 400);
    // In the goal's area: straight to it, slower when close.
    lib.resetInput(1);
    init.origin = {240, 0, 0};
    lib.initMoveState(ms, init);
    lib.moveToGoal(ms, far, tfl::Default);
    CHECK(lib.getInput(1, 0.1f).speed == 40);

    // Predicting a move: a floor where a standing player's origin is 24
    // above z = 0.
    lib.trace = [](const Vec3f& start, const Vec3f& mins, const Vec3f&, const Vec3f& end, int, int) {
        BotLibrary::Trace t;
        const float floor = -mins[2];
        t.end = end;
        if (end[2] < floor && start[2] >= floor) {
            t.fraction = (start[2] - floor) / (start[2] - end[2]);
            for (int i = 0; i < 3; ++i)
                t.end[size_t(i)] = start[size_t(i)] + (end[size_t(i)] - start[size_t(i)]) * t.fraction;
            t.entity = 1022;
            t.normal = {0, 0, 1};
        }
        return t;
    };
    BotClientMove cm;
    CHECK(lib.predictClientMovement(cm, 1, {50, 0, 200}, presence::Normal, false, {}, {}, 0, 50, 0.1f,
                                    stopevent::HitGround, 0));
    // (On the ground counts from 10 units above it, as the original's.)
    CHECK(cm.stopEvent == stopevent::HitGround && cm.endPos[2] >= 24 && cm.endPos[2] <= 34 && cm.frames > 3);
    // Running along +x for half a second on the floor.
    CHECK(lib.predictClientMovement(cm, 1, {50, 0, 24}, presence::Normal, true, {}, {400, 0, 0}, 5, 5, 0.1f, 0, 0));
    CHECK(cm.stopEvent == stopevent::None && cm.endPos[0] > 100 && cm.velocity[0] > 250 &&
          std::fabs(cm.endPos[2] - 24) < 1);
    // Entering area 3 on the way.
    CHECK(lib.predictClientMovement(cm, 1, {150, 0, 24}, presence::Normal, true, {320, 0, 0}, {400, 0, 0}, 20, 20,
                                    0.1f, stopevent::EnterArea, 3));
    CHECK(cm.stopEvent == stopevent::EnterArea && cm.endArea == 3 && std::fabs(cm.endPos[0] - 200) < 1);
}

static void testBotChat() {
    using namespace reluva::idtech3::botlib;
    const std::map<std::string, std::string> files = {
        {"syn.c", "1 { [(\"hello\", 1), (\"hi\", 1)] }\n"},
        {"rnd.c", "greeting = { \"hey\"; \"yo\"; }\n"},
        {"match.c", "2 { \"hello \", 0 = (5, 1);\n 0, \" says: \", 1 = (6, 0); }\n"},
        {"rchat.c", "[\"hello\", !\"bye\"] = 5 { \"hello yourself, \", 0, \"!\"; }\n"},
        {"bots/test_t.c", "chat \"other\" { type \"x\" { \"no\"; } }\n"
                          "chat \"test\" { type \"game_enter\" { \"hi all, I'm \", 0, \".\"; greeting, \" there\"; } }\n"}};
    const BotScript::Loader load = [&](const std::string& path, std::string& text) {
        const auto it = files.find(path);
        if (it == files.end())
            return false;
        text = it->second;
        return true;
    };
    BotChat chat;
    std::string log;
    chat.setup(load, nullptr, &log);
    CHECK(log.find("1 synonym lists, 1 random lists, 2 match templates, 1 replies") != std::string::npos);
    BotMatch m;
    CHECK(chat.findMatch("Bob says: good game\n", m, 2) && m.type == 6);
    CHECK(BotChat::matchVariable(m, 0) == "Bob" && BotChat::matchVariable(m, 1) == "good game");
    CHECK(!chat.findMatch("Bob says: hi", m, 4)); // not in that context
    std::string text = "  a  ^7b !c ";
    BotChat::unifyWhiteSpaces(text);
    CHECK(text == "a 7b c");
    text = "hi there, hit it";
    chat.replaceSynonyms(text, 1);
    CHECK(text == "hello there, hit it");

    std::vector<std::string> said;
    chat.command = [&](int client, const std::string& c) { said.push_back(std::to_string(client) + " " + c); };
    const int cs = chat.allocState();
    CHECK(chat.loadChatFile(cs, load, "bots/test_t.c", "TEST") && chat.numInitialChats(cs, "game_enter") == 2);
    chat.setName(cs, "Munro");
    std::vector<const char*> vars(8, nullptr);
    vars[0] = "Munro";
    chat.initialChat(cs, "game_enter", 0, vars);
    std::string line = chat.takeChatMessage(cs);
    CHECK(line == "hi all, I'm Munro." || line == "hey there" || line == "yo there");
    vars[0] = "Bob";
    CHECK(chat.replyChat(cs, "hello Munro", 0, 0, vars));
    chat.enterChat(cs, 3, 0);
    CHECK(said.size() == 1 && said[0] == "3 say hello yourself, Bob!");
    CHECK(!chat.replyChat(cs, "hello and bye", 0, 0, vars)); // a ! key present
    chat.queueConsoleMessage(cs, 1, "first");
    chat.queueConsoleMessage(cs, 1, "second");
    CHECK(chat.numConsoleMessages(cs) == 2 && chat.nextConsoleMessage(cs)->text == "first");
    chat.removeConsoleMessage(cs, chat.nextConsoleMessage(cs)->handle);
    CHECK(chat.nextConsoleMessage(cs)->text == "second");
}

static const reluva::test::Suite suite("idtech3.unit.bots", {
    {"aas", [](const std::filesystem::path&) { testAas(); }},
    {"bot script", [](const std::filesystem::path&) { testBotScript(); }},
    {"bot character", [](const std::filesystem::path&) { testBotCharacter(); }},
    {"bot ai", [](const std::filesystem::path&) { testBotAi(); }},
    {"bot chat", [](const std::filesystem::path&) { testBotChat(); }},
});
