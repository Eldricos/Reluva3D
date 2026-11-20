// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: The hosts that run the games' code: the server game, the client game, the menus, saved games.

#include "common/fs.h"
#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/client/player_input.h"
#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_library.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"
#include "engines/idtech3/formats/bsp.h"
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
#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/jk2/sp_menus.h"
#include "engines/idtech3/jk2/sp_savegame.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/formats/lexer.h"
#include "engines/idtech3/formats/marks.h"
#include "engines/idtech3/render/light_grid.h"
#include "engines/idtech3/formats/md3.h"
#include "engines/idtech3/render/model_cache.h"
#include "engines/idtech3/wire/message.h"
#include "engines/idtech3/wire/netchan.h"
#include "engines/idtech3/render/patch.h"
#include "engines/idtech3/formats/player_animation.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/vm/qvm.h"
#include "engines/idtech3/client/server_browser.h"
#include "backends/networking/sockets/socket_net.h"
#include "engines/idtech3/formats/shader.h"
#include "engines/idtech3/raven/ambient_sets.h"
#include "engines/idtech3/audio/sound_system.h"
#include "engines/idtech3/audio/sounds.h"
#include "engines/idtech3/client/page.h"
#include "audio/wav.h"
#include "common/md4.h"
#include "common/zip.h"
#include "engines/registry.h"
#include "tests/engines/idtech3/data/qvm/fake_cgame_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_game_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_ui_qvm.h"
#include "tests/engines/idtech3/data/qvm/test_vm_qvm.h"
#include "engines/idtech3/vm/vm_services.h"
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

// Saved games' chunks: written and read back in order, checked.
// Jedi Outcast's chunk compression: the encoder's choices (the
// original's, 0x439220) and a compressed chunk read back.
static void testCompressedSaveChunks() {
    using namespace reluva::idtech3::raven;
    auto rle = [](std::string_view text) {
        return compressRle(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    };
    CHECK((rle("AAAAB") == std::vector<uint8_t>{4, 'A', 0xff, 'B'}));
    CHECK((rle("ABC") == std::vector<uint8_t>{0xfd, 'A', 'B', 'C'}));
    // Bytes as they are stop before three equal ones, which make a run.
    CHECK((rle("ABBB") == std::vector<uint8_t>{0xff, 'A', 3, 'B'}));
    CHECK((rle("ABB") == std::vector<uint8_t>{0xfd, 'A', 'B', 'B'}));
    // Runs and stretches of at most 127.
    const std::vector<uint8_t> zeros(300, 0);
    CHECK((compressRle(zeros.data(), zeros.size()) == std::vector<uint8_t>{127, 0, 127, 0, 46, 0}));
    std::vector<uint8_t> mixed;
    for (int i = 0; i < 1000; ++i)
        mixed.push_back(uint8_t(i % 7 == 0 ? 9 : (i * 37) % 251));
    mixed.insert(mixed.end(), 50, 3);
    const std::vector<uint8_t> packed = compressRle(mixed.data(), mixed.size());
    std::vector<uint8_t> back;
    CHECK(decompressRle(packed.data(), packed.size(), mixed.size(), back) && back == mixed);
    CHECK(!decompressRle(packed.data(), packed.size() - 1, mixed.size(), back));

    SaveWriter w;
    w.compress = true;
    w.add(chunkId("ZERO"), zeros);
    w.addInt(chunkId("GAME"), 7); // longer compressed: stored as it is
    // id, minus the length, the stored length, checksum, data, marker.
    CHECK(w.bytes().size() == (20 + 6) + (16 + 4));
    CHECK(w.bytes()[4] == uint8_t(-300 & 0xff) && w.bytes()[8] == 6);
    SaveReader r(w.bytes());
    std::vector<uint8_t> data;
    int32_t v = 0;
    CHECK(!r.next(chunkId("ZERO"), data, 300) && data == zeros);
    CHECK(!r.nextInt(chunkId("GAME"), v) && v == 7 && r.atEnd());
}

static void testSaveChunks() {
    using namespace reluva::idtech3::raven;
    CHECK(chunkId("COMM") == 0x434f4d4d && chunkName(chunkId("SHOT")) == "SHOT");
    SaveWriter w;
    w.addText(chunkId("COMM"), "borg1", 128);
    w.addInt(chunkId("GAME"), 7);
    w.add(chunkId("STRP"), nullptr, 0);
    w.addText(chunkId("CSDA"), "text");
    // id, length, checksum, data, marker; the id's bytes little-endian.
    CHECK(w.bytes().size() == (16 + 128) + (16 + 4) + 16 + (16 + 5));
    CHECK(w.bytes()[0] == 'M' && w.bytes()[3] == 'C' && w.bytes()[4] == 128);
    CHECK(w.bytes()[12 + 128] == 0xcd && w.bytes()[15 + 128] == 0x12);
    SaveReader r(w.bytes());
    std::string text;
    int32_t v = 0;
    std::vector<uint8_t> data;
    CHECK(r.peek(chunkId("COMM")) && !r.peek(chunkId("GAME")));
    CHECK(!r.nextText(chunkId("COMM"), text, 128) && text == "borg1");
    CHECK(r.nextInt(chunkId("TIME"), v)); // not the next one
    CHECK(!r.nextInt(chunkId("GAME"), v) && v == 7);
    CHECK(r.next(chunkId("STRP"), data, 4)); // wrong length
    CHECK(!r.next(chunkId("STRP"), data) && data.empty());
    CHECK(!r.nextText(chunkId("CSDA"), text) && text == "text" && r.atEnd());
    CHECK(r.next(chunkId("CSDA"), data)); // past the end
    std::vector<uint8_t> damaged = w.bytes();
    damaged[12 + 2] ^= 1;
    SaveReader bad(damaged);
    CHECK(bad.next(chunkId("COMM"), data));
}

// Which game code wrote a saved game: by the length of its first GCLI.
static void testSaveKind() {
    using namespace reluva::idtech3::raven;
    using reluva::idtech3::eliteforce::SaveKind;
    using reluva::idtech3::eliteforce::saveKind;
    auto save = [](uint32_t clientLength) {
        SaveWriter w;
        w.addText(chunkId("COMM"), "borg1", 128);
        w.addInt(chunkId("GAME"), 0);
        const std::vector<uint8_t> client(clientLength);
        w.add(chunkId("GCLI"), client.data(), clientLength);
        w.add(chunkId("GCLI"), client.data(), 4);
        return w.bytes();
    };
    CHECK(saveKind(save(2720)) == SaveKind::kOriginal);
    CHECK(saveKind(save(3364)) == SaveKind::kExpansion);
    CHECK(!saveKind(save(100)));
    std::vector<uint8_t> noClient = save(2720);
    noClient.resize(16 + 128);
    CHECK(!saveKind(noClient) && !saveKind(std::vector<uint8_t>{1, 2, 3}));
}

// The game logic starting a level: its entities handed over token by
// token, as the original server does, by a QVM the Quake III compiler
// made from tests/engines/idtech3/data/qvm/fake_game.c.
static void testMapCommands() {
    // devmap and spdevmap turn cheats on for the level; map and spmap off.
    using eliteforce::GameHost;
    CvarStore cvars;
    CHECK(GameHost::applyMapCommand(cvars, "devmap") && cvars.integer("sv_cheats") == 1);
    CHECK(!GameHost::applyMapCommand(cvars, "map") && cvars.integer("sv_cheats") == 0);
    GameHost::applyMapCommand(cvars, "spdevmap");
    CHECK(cvars.integer("sv_cheats") == 1 && cvars.integer("g_gametype") == 2);
    GameHost::applyMapCommand(cvars, "spmap");
    CHECK(cvars.integer("sv_cheats") == 0);
}

static void testGameHost(const std::filesystem::path &tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(
        tmp / "BaseEF" / "pak0.pk3",
        test::buildZip(
            {{"vm/qagame.qvm", std::vector<uint8_t>(std::begin(kFakeGameQvm), std::end(kFakeGameQvm)), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::GameHost game(set, cvars);
    CHECK(!game.load());
    const std::string entities = "{\n\"classname\" \"worldspawn\"\n\"message\" \"Borg Cube, deck 4\"\n}\n"
                                 "// a comment\n{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n"
                                 "\"origin\" \"200 0 0\"\n}\n/* another */ {\n\"classname\" \"item_health\"\n"
                                 "\"origin\" \"-200 0 16\"\n}\n{\n\"classname\" \"info_player_deathmatch\"\n"
                                 "\"origin\" \"0 0 300\"\n}\n";
    auto map = collisionMap();
    map->entities = entities;
    const Error err = game.startLevel("borg2", map);
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return;
    }
    CHECK(game.log() == "------- Game Initialization -------\n");
    CHECK(game.gameData().count == 4 && game.gameData().entitySize == 352 && game.gameData().clientSize == 740);
    CHECK((game.linked() == std::set<int32_t>{1, 2, 3})); // the world unlinked
    CHECK(game.configstrings().count(12) && game.configstrings().at(12) == "borg2");
    // The server info, as the client game finds the map's name.
    CHECK(game.configstrings().count(0) && game.configstrings().at(0).find("\\mapname\\borg2") != std::string::npos);
    CHECK(game.levelTime() == 3 * ef::GameHost::kFrameMs);
    int32_t r = 0;
    CHECK(!game.call(23, {}, r) && r == 3); // the three settling frames
    CHECK(!game.call(21, {2}, r) && r == 'i' * 1000 + 't');
    CHECK(!game.call(21, {0}, r) && r == 'w' * 1000 + 'o');
    CHECK(!game.call(22, {}, r) && r == 803); // sv_maxclients 8, ceil(2.25)

    // SetBrushModel: the door's model, box and contents, linked.
    CHECK(!game.call(30, {1}, r) && r == 111);
    CHECK(game.entityVec(1, 232)[2] == 128 && game.entityInt(1, 244) == -1);
    // Linking fills in the box in the world, a unit larger: the item's z -16..16 around 16.
    CHECK(!game.call(27, {2}, r) && r == -1 * 1000 + 33);
    // Traces down onto the floor, the door (entity 1) and the item (2), in eighths.
    CHECK(!game.call(24, {0, 0, 100, 0, -1}, r) && r == 1022);
    CHECK(!game.call(25, {}, r) && r == 1);
    CHECK(!game.call(26, {}, r) && r == 1000); // the floor's normal points up
    CHECK(!game.call(24, {200, 0, 500, 0, -1}, r) && r == 1);
    CHECK(!game.call(25, {}, r) && r == 128 * 8 + 1);
    CHECK(!game.call(24, {-200, 0, 500, 0, -1}, r) && r == 2);
    CHECK(!game.call(25, {}, r) && r == 32 * 8 + 1);
    CHECK(!game.call(24, {-200, 0, 500, 0, 2}, r) && r == 1022);                               // ignoring the item
    CHECK(!game.call(24, {0, 0, 100, 16, -1}, r) && !game.call(25, {}, r) && r == 16 * 8 + 1); // a box
    CHECK(!game.call(24, {0, 0, -10, 0, -1}, r) && !game.call(26, {}, r) && r == 101);         // starting in the floor
    CHECK(!game.call(24, {1000, 0, 100, 0, -1}, r) && r == 1023);                              // nothing there
    CHECK(!game.call(28, {0, 0, -32}, r) && r == contents::kSolid);
    CHECK(!game.call(28, {200, 0, 64}, r) && r == -1); // inside the door
    CHECK(!game.call(28, {0, 0, 64}, r) && r == 0);
    CHECK(!game.call(29, {-200, 0, 16}, r) && r == 102);
    CHECK(!game.call(29, {0, 0, 300}, r) && r == 103); // the spawn point

    // A player joins: refused without a name, then let in and placed.
    std::string refused;
    CHECK(!game.connectClient(0, "", refused) && refused == "No name given");
    CHECK(!game.connectClient(0, "\\name\\Reluva\\model\\munro", refused) && refused.empty());
    CHECK(!game.beginClient(0));
    CHECK(!game.call(31, {}, r) && r == 4 && game.linked().count(4));
    CHECK(game.entityVec(4, 272)[0] == 0 && game.entityVec(4, 272)[2] == 309); // the spawn point, 9 up
    // Its commands, field by field in Elite Force's usercmd_t.
    ef::GameHost::UserCommand cmd;
    cmd.serverTime = 400;
    cmd.angles = {1000, 16384, 0};
    cmd.buttons = 5;
    cmd.weapon = 3;
    cmd.forward = 127;
    cmd.right = -127;
    cmd.up = 10;
    CHECK(!game.clientThink(0, cmd));
    CHECK(!game.call(32, {}, r) && r == 4005);
    CHECK(!game.call(33, {}, r) && r == 1000 + 16384 * 100000);
    CHECK(!game.call(34, {}, r) && r == 3000000 + 255 * 10000 + 1 * 100 + 60);
    CHECK(game.entityVec(4, 272)[0] == 127); // moved by it
    // Its snapshot: every linked entity (the door, the item, the spawn
    // point, the player's box at 4: the stand-in's player isn't entity 0),
    // each the first 204 bytes of the game's entity.
    const auto snap = game.snapshot(0);
    CHECK(snap.serverTime == 3 * ef::GameHost::kFrameMs && snap.entities.size() == 4 && snap.entities[0].size() == 204);
    CHECK(snap.entities[0][0] == 1 && snap.entities[3][0] == 4); // s.number
    CHECK(snap.serverCommandSequence == 1);
    // The player state, copied but for the word the original doesn't send.
    {
        const int32_t client = game.gameData().clients;
        CHECK(game.gameData().clientSize >= 460);
        game.vm().writeInt(client + 44, 66);
        game.vm().writeInt(client + 48, 166);
        const auto withTimer = game.snapshot(0);
        int32_t w44 = 0;
        int32_t w48 = -1;
        std::memcpy(&w44, &withTimer.playerState[44], 4);
        std::memcpy(&w48, &withTimer.playerState[48], 4);
        CHECK(w44 == 66 && w48 == 0 && game.clientInt(0, 48) == 166);
    }
    CHECK(game.serverCommands().size() == 1 && game.serverCommands()[0].first == -1 &&
          game.serverCommands()[0].second == "print \"Reluva entered the game\n\"");
    CHECK(game.unhandled().empty());

    // Broken entity text: the game's own error stops it.
    ef::GameHost broken(set, cvars);
    CHECK(!broken.load());
    auto brokenMap = std::make_shared<Bsp>();
    brokenMap->entities = "{ \"classname\" \"worldspawn\" ";
    const Error e2 = broken.startLevel("x", brokenMap);
    CHECK(e2 && e2.message.find("broken entity text") != std::string::npos);
    std::filesystem::remove_all(tmp);
}

// The client game starting: the server's configstrings handed over as
// its game state (Quake III's gamestate_t), by a QVM the Quake III
// compiler made from tests/engines/idtech3/data/qvm/fake_cgame.c.
static void testCgameHost(const std::filesystem::path &tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(
        tmp / "BaseEF" / "pak0.pk3",
        test::buildZip({{"vm/cgame.qvm", std::vector<uint8_t>(std::begin(kFakeCgameQvm), std::end(kFakeCgameQvm)), {}},
                        {"models/test/box.md3", buildMd3(), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::CgameHost cgame(set, cvars);
    CHECK(!cgame.load());
    cgame.setConfigstrings({{12, "baseef-1"}, {289, "sound/player/fry.wav"}, {544, "n\\Reluva\\t\\0"}, {5, ""}});
    int32_t r = 0;
    CHECK(!cgame.call(ef::CgameHost::kInit, {0, 0, 3}, r) && r == 3);
    CHECK(cgame.log() == "fake cgame\n");
    CHECK(!cgame.call(20, {12}, r) && r == 'b' * 1000 + 'a');
    CHECK(!cgame.call(21, {289}, r) && r == 20);
    CHECK(!cgame.call(20, {544}, r) && r == 'n' * 1000 + '\\');
    CHECK(!cgame.call(21, {5}, r) && r == 0);   // empty
    CHECK(!cgame.call(21, {100}, r) && r == 0); // never set
    CHECK(!cgame.call(22, {}, r) && r == 1 + 9 + 21 + 13);
    CHECK(!cgame.call(23, {}, r) && r == 640480);
    CHECK(!cgame.call(24, {}, r) && r == 101);
    CHECK((cgame.commands() == std::vector<std::string>{"+zoom", "weapnext"}));
    CHECK(cgame.music().first == "music/intro" && cgame.music().second == "music/loop");

    // A snapshot, in Elite Force's snapshot_t (52740 bytes).
    CHECK(!cgame.call(25, {}, r) && r == -1); // none yet
    ef::GameHost::Snapshot snap;
    snap.serverTime = 1300;
    snap.playerState.assign(460, 0);
    snap.playerState[0] = 7;   // the first word: 7
    snap.playerState[456] = 2; // the last: 2
    for (int e = 0; e < 3; ++e) {
        std::vector<uint8_t> state(204, 0);
        state[0] = uint8_t(10 + e); // s.number
        state[200] = uint8_t(e);    // the last word
        snap.entities.push_back(state);
    }
    snap.serverCommandSequence = 2;
    cgame.newSnapshot(snap);
    CHECK(!cgame.call(25, {}, r) && r == 1300);
    CHECK(!cgame.call(26, {}, r) && r == 3002);
    CHECK(!cgame.call(27, {}, r) && r == 9);
    CHECK(!cgame.call(28, {2}, r) && r == 12 * 1000 + 2);
    CHECK(cgame.snapshotRequests().size() == 2 && cgame.snapshotRequests()[1].first == 1);
    // Server commands, split into words.
    cgame.setServerCommands({"print \"Reluva entered the game\n\"", "cs 544 \"n\\Reluva\""});
    CHECK(!cgame.call(29, {1, 1}, r) && r == 2000 + 'R');
    CHECK(!cgame.call(29, {2, 0}, r) && r == 3000 + 'c');
    CHECK(!cgame.call(29, {3, 0}, r) && r == -1);

    // The player's commands, for prediction.
    CHECK(!cgame.call(30, {}, r) && r == -1); // none yet
    ef::GameHost::UserCommand cmd;
    cmd.forward = 100;
    cgame.addUserCommand(cmd);
    cgame.addUserCommand(cmd);
    CHECK(!cgame.call(30, {}, r) && r == 2100);
    // A scene: its entities kept with the size that cleared them, the view.
    CHECK(!cgame.call(31, {}, r));
    CHECK(cgame.scene().entities.size() == 3 && cgame.scene().entities[0].clearedSize == 160);
    CHECK(cgame.scene().renders == 2 && cgame.scene().view.size() == 112 && cgame.scene().view[8] == 0x80);
    // Each render with the entities added since the last.
    CHECK(cgame.scene().views.size() == 2 && cgame.scene().views[0].entityCount == 2 &&
          cgame.scene().views[1].firstEntity == 2 && cgame.scene().views[1].entityCount == 1 &&
          cgame.scene().views[0].refdef[76] == 0 && cgame.scene().views[1].refdef[76] == 1);
    bool handled = false;
    CHECK(!cgame.consoleCommand("weapnext", handled) && handled);
    CHECK(!cgame.consoleCommand("weapon 3", handled) && !handled);
    // A tag: tag_top half way between the box's two frames.
    int32_t box = 0;
    CHECK(!cgame.call(33, {}, box) && box > 0);
    Md3Model boxModel;
    CHECK(!parseMd3(buildMd3(), boxModel));
    const float halfway = (boxModel.tag(0, "tag_top")->origin[2] + boxModel.tag(1, "tag_top")->origin[2]) / 2;
    CHECK(!cgame.call(32, {box}, r) && r == int(halfway) * 10 + 1);
    CHECK(cgame.unhandled().empty());
    std::filesystem::remove_all(tmp);
}

// The menus' system calls, called by a QVM the Quake III compiler made
// from tests/engines/idtech3/data/qvm/fake_ui.c.
static void testUiHost(const std::filesystem::path &tmp) {
    using test::toBytes;
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(
        tmp / "BaseEF" / "pak0.pk3",
        test::buildZip({{"vm/ui.qvm", std::vector<uint8_t>(std::begin(kFakeUiQvm), std::end(kFakeUiQvm)), {}},
                        {"scripts/test.txt", toBytes("hello world\n"), {}},
                        {"scripts/other.txt", toBytes("x"), {}},
                        {"scripts/sub/deep.txt", toBytes("x"), {}},
                        {"scripts/a.shader", toBytes("x"), {}},
                        {"models/test/box.md3", buildMd3(), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::UiHost ui(set, cvars);
    Error err = ui.load();
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return;
    }
    int32_t r = 0;
    CHECK(!ui.call(ef::UiHost::kInit, {}, r) && r == 1202); // 12 bytes, 2 .txt files directly in scripts/
    CHECK(ui.log() == "fake ui\n");
    CHECK(cvars.string("ui_test") == "7" && cvars.find("ui_test")->flags == CvarStore::kArchive);
    CHECK(!ui.call(20, {}, r) && r == 7);
    cvars.set("ui_test", "9");
    CHECK(!ui.call(20, {}, r) && r == 7); // the program's copy changes on Cvar_Update
    CHECK(!ui.call(21, {}, r) && r == 9);
    CHECK(!ui.call(25, {}, r) && r == 2);
    CHECK(!ui.call(22, {}, r) && r == 640480);

    CHECK(!ui.call(ef::UiHost::kSetActiveMenu, {1}));
    CHECK(!ui.call(ef::UiHost::kRefresh, {1234}, r) && r == 1234);
    CHECK(ui.pictures().size() == 3); // two, then the pointer
    if (ui.pictures().size() == 3) {
        const auto &p = ui.pictures()[0];
        CHECK(p.x == 10 && p.y == 20 && p.w == 320 && p.h == 30 && p.s1 == 0 && p.t2 == 0.5f);
        CHECK(p.color[0] == 1 && p.color[1] == 0.5f && p.color[2] == 0 && p.color[3] == 1);
        CHECK(ui.shaders().name(p.shader) == "menu/test");
        CHECK(ui.pictures()[1].color == (std::array<float, 4>{1, 1, 1, 1}));
    }
    CHECK(!ui.call(23, {}, r) && r == 'h' * 1000000 + 'e' * 1000); // "hel", then NUL padding
    CHECK(cvars.string("ui_text") == "hello world\n");
    cvars.set("ui_number", "16");
    CHECK(!ui.call(24, {}, r) && r == 40);
    err = ui.call(26, {}, r);
    CHECK(err && err.message.find("boom") != std::string::npos);
    CHECK(!ui.call(20, {}, r) && r == 9); // usable again after the program's error
    // Typed characters come as keys with 1024 added; plain keys don't type.
    CHECK(!ui.call(ef::UiHost::kKeyEvent, {'q', 1}));
    CHECK(!ui.call(ef::UiHost::kKeyEvent, {'Q' | 1024, 1}));
    CHECK(!ui.call(27, {}, r) && r == 'Q' * 1000 + 1);

    // The CD key: taken if it has 18 letters and digits, kept in the key
    // file, read back by a new run, which then counts it as checked.
    ui.setKeyFile(tmp / "save" / "cdkey.txt");
    CHECK(!ui.call(28, {1}, r) && r == 0 && !std::filesystem::exists(tmp / "save" / "cdkey.txt"));
    CHECK(!ui.call(28, {0}, r) && r == 1 && std::filesystem::exists(tmp / "save" / "cdkey.txt"));
    // Call 58, the model bounds the main menu asks for: frame 0's box.
    Md3Model box;
    CHECK(!parseMd3(buildMd3(), box));
    const auto &f0 = box.frames[0];
    const int boxCode = int(f0.mins[0] * 10) + int(f0.mins[1] * 100) + int(f0.mins[2] * 1000) +
                        int(f0.maxs[0] * 10000) + int(f0.maxs[1] * 100000) + int(f0.maxs[2] * 1000000);
    CHECK(boxCode != 0);
    CHECK(!ui.call(29, {}, r) && r == boxCode);
    CHECK(!ui.call(29, {99}, r) && r == 0); // no such model: zeros
    CvarStore fresh;
    ef::UiHost again(set, fresh);
    again.setKeyFile(tmp / "save" / "cdkey.txt");
    CHECK(!again.load() && !again.call(ef::UiHost::kInit) && fresh.string("ui_cdkeychecked") == "1");

    // A frame with no menu open opens the main one, as the original
    // engine does while not connected; with one open, it's left alone.
    CHECK(!ui.call(31, {}, r) && r == 1);
    ui.clearPictures();
    CHECK(!ui.refresh(2000) && !ui.call(31, {}, r) && r == 1 && ui.pictures().size() == 4);
    CHECK(!ui.call(30));
    ui.clearPictures();
    CHECK(!ui.refresh(2016) && !ui.call(31, {}, r) && r == 2 && ui.pictures().size() == 4);

    // A menu sound: passed on by name with its channel.
    std::vector<std::pair<std::string, int>> played;
    ui.startLocalSound = [&](const std::string &name, int channel) { played.emplace_back(name, channel); };
    CHECK(!ui.call(33));
    CHECK(played.size() == 1 && played[0].first == "sound/interface/button1.wav" && played[0].second == 6);

    CHECK(ui.unhandled().empty());
    std::filesystem::remove_all(tmp);
}

// Jedi Outcast's single-player menus, run from menu scripts: a main menu
// whose button opens a second, its scripts, focus, keys and the mouse.
static void testJediMenus(const std::filesystem::path &tmp) {
    namespace jk2 = reluva::idtech3::jk2;
    std::filesystem::remove_all(tmp);
    const char *list = "{\n loadMenu { \"ui/main.menu\" }\n loadMenu { \"ui/newgame.menu\" }\n}\n";
    // One keyword a line, as the game's files have them: a keyword's
    // arguments are the rest of its line.
    const char *mainMenu = R"({
  assetGlobalDef {
    cursor "cursor"
    itemFocusSound "sound/roam.wav"
  }
  menuDef {
    name "mainMenu"
    fullScreen 1
    rect 0 0 640 480
    visible 1
    onOpen { exec "music music/theme" ; setfocus newgamebutton }
    onESC { close mainMenu ; open quitMenu }
    itemDef {
      name back
      style WINDOW_STYLE_SHADER
      rect 0 0 640 480
      background "gfx/menus/menu1"
      visible 1
      decoration
    }
    itemDef {
      name loadglow
      style WINDOW_STYLE_SHADER
      rect 245 115 130 24
      background "gfx/glow"
      visible 0
      decoration
    }
    itemDef {
      name newgamebutton
      type ITEM_TYPE_BUTTON
      rect 115 115 130 24
      text "New"
      visible 1
      action { play "sound/button.wav" ; close all ; open newgameMenu }
    }
    itemDef {
      name loadbutton
      type ITEM_TYPE_BUTTON
      rect 245 115 130 24
      text "Load"
      visible 1
      mouseEnter { show loadglow }
      mouseExit { hide loadglow }
    }
  }
})";
    const char *newGame = R"({
  menuDef {
    name "newgameMenu"
    fullScreen 1
    rect 0 0 640 480
    onOpen { setcvar g_spskill 1 }
    onESC { close newgameMenu ; open mainMenu }
    itemDef {
      name easy
      type ITEM_TYPE_BUTTON
      rect 250 200 250 40
      text "Easy"
      visible 1
      action
      {
        play "sound/button.wav"
        setcvar g_spskill 0
      }
    }
    itemDef {
      name hard
      type ITEM_TYPE_BUTTON
      rect 250 280 250 40
      text "Hard"
      visible 1
      action { setcvar g_spskill 2 ; setitemcolor easy forecolor 1 0 0 1 }
    }
    itemDef {
      name begin
      type ITEM_TYPE_BUTTON
      rect 435 444 130 24
      text "Begin"
      visible 1
      action { uiScript startgame }
    }
  }
})";
    test::writeFile(tmp / "base" / "assets0.pk3", test::buildZip({{"ui/menus.txt", test::toBytes(list), {}},
                                                                  {"ui/main.menu", test::toBytes(mainMenu), {}},
                                                                  {"ui/newgame.menu", test::toBytes(newGame), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "base", 0, warnings));
    CvarStore cvars;
    jk2::SpMenus menus(set, cvars);
    std::vector<std::string> sounds;
    menus.startLocalSound = [&](const std::string &name, int) { sounds.push_back(name); };
    Error err = menus.load();
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return;
    }
    CHECK((menus.menuNames() == std::vector<std::string>{"mainMenu", "newgameMenu"}));
    CHECK(menus.openMenus().empty());

    // The main menu, its onOpen run: the music, the focus on New.
    CHECK(!menus.setActiveMenu("main"));
    CHECK((menus.openMenus() == std::vector<std::string>{"mainMenu"}));
    CHECK(menus.catcher() & jk2::SpMenus::kCatchUi);
    CHECK(menus.fullscreen());
    CHECK(menus.focusedItem() == "newgamebutton");
    CHECK((menus.takeCommands() == std::vector<std::string>{"music music/theme"}));
    CHECK(!menus.refresh(0));
    auto drawn = [&](const std::string &shader) {
        return std::count_if(menus.pictures().begin(), menus.pictures().end(),
                             [&](const Picture &p) { return menus.shaders().name(p.shader) == shader; });
    };
    CHECK(drawn("gfx/menus/menu1") == 1 && drawn("gfx/glow") == 0 && drawn("cursor") == 1);

    // The mouse onto Load (from the middle): its glow shown, the focus
    // and its sound; off it again, the glow hidden and the focus kept.
    sounds.clear();
    CHECK(!menus.mouseEvent(0, -115));
    CHECK(menus.cursor()[0] == 320 && menus.cursor()[1] == 125);
    CHECK(menus.focusedItem() == "loadbutton");
    CHECK((sounds == std::vector<std::string>{"sound/roam.wav"}));
    CHECK(!menus.refresh(50) && drawn("gfx/glow") == 1);
    CHECK(!menus.mouseEvent(0, 100));
    CHECK(!menus.refresh(100) && drawn("gfx/glow") == 0);
    CHECK(menus.focusedItem() == "loadbutton");

    // Up to New, Enter: the new game menu opens in its place.
    CHECK(!menus.keyEvent(q3key::kUp));
    CHECK(menus.focusedItem() == "newgamebutton");
    sounds.clear();
    CHECK(!menus.keyEvent(q3key::kEnter));
    CHECK((menus.openMenus() == std::vector<std::string>{"newgameMenu"}));
    CHECK(!sounds.empty() && sounds[0] == "sound/button.wav");
    CHECK(cvars.string("g_spskill") == "1");

    // Esc back to the main menu, and again to the new game menu.
    CHECK(!menus.keyEvent(q3key::kEscape));
    CHECK((menus.openMenus() == std::vector<std::string>{"mainMenu"}));
    menus.takeCommands();
    CHECK(!menus.keyEvent(q3key::kEnter));
    CHECK((menus.openMenus() == std::vector<std::string>{"newgameMenu"}));

    // Clicking Easy: its script without ";" between commands.
    CHECK(!menus.mouseEvent(0, 220 - int32_t(menus.cursor()[1])));
    CHECK(menus.focusedItem() == "easy");
    CHECK(!menus.keyEvent(q3key::kMouse1));
    CHECK(cvars.string("g_spskill") == "0");
    // Down to Hard and Enter; a click away from any item does nothing.
    CHECK(!menus.keyEvent(q3key::kDown));
    CHECK(!menus.keyEvent(q3key::kEnter));
    CHECK(cvars.string("g_spskill") == "2");
    CHECK(!menus.mouseEvent(0, -200));
    CHECK(!menus.keyEvent(q3key::kMouse1));
    CHECK(cvars.string("g_spskill") == "2");

    // Begin: the menus closed, the first level started.
    CHECK(!menus.keyEvent(q3key::kDown));
    CHECK(menus.focusedItem() == "begin");
    CHECK(!menus.keyEvent(q3key::kEnter));
    CHECK(menus.openMenus().empty());
    CHECK(!(menus.catcher() & jk2::SpMenus::kCatchUi));
    CHECK((menus.takeCommands() == std::vector<std::string>{"map kejim_post"}));

    // The datapad only opens in a game; the command is theirs either way.
    bool handled = false;
    CHECK(!menus.consoleCommand("datapad", &handled) && handled);
    CHECK(menus.openMenus().empty());
    CHECK(!menus.consoleCommand("god", &handled) && !handled);
    std::filesystem::remove_all(tmp);
}

// Jedi Outcast's menus' other items: the saved games' list and the scripts
// that save, load and delete; a key binding; an edit field; the datapad's
// owner draw; a script put off by "defer".
static void testJediMenuItems(const std::filesystem::path &tmp) {
    namespace jk2 = reluva::idtech3::jk2;
    std::filesystem::remove_all(tmp);
    const char *list = "{\n loadMenu { \"ui/items.menu\" }\n}\n";
    const char *items = R"({
  menuDef {
    name "itemsMenu"
    rect 0 0 640 480
    onOpen { uiScript ReadSaveDirectory }
    itemDef {
      name savelist
      type ITEM_TYPE_LISTBOX
      rect 100 100 300 100
      elementheight 16
      feeder FEEDER_SAVEGAMES
      columns 2 2 35 150 155 25 100
      visible 1
      doubleclick { uiScript loadgame }
    }
    itemDef {
      name bind
      type ITEM_TYPE_BIND
      text "Jump:"
      cvar "+moveup"
      rect 100 250 200 20
      visible 1
    }
    itemDef {
      name desc
      type ITEM_TYPE_EDITFIELD
      text "Desc:"
      cvar ui_gameDesc
      maxChars 6
      rect 100 300 200 20
      visible 1
    }
    itemDef {
      name pad
      rect 0 0 640 400
      ownerdraw 203
      visible 1
      decoration
    }
    itemDef {
      name save
      type ITEM_TYPE_BUTTON
      text "Save"
      rect 100 350 200 20
      visible 1
      action { defer VideoSetup warnMenu ; uiScript savegame }
    }
    itemDef {
      name delete
      type ITEM_TYPE_BUTTON
      text "Delete"
      rect 100 400 200 20
      visible 1
      action { uiScript deletegame }
    }
  }
  menuDef {
    name "warnMenu"
    rect 0 0 640 480
    itemDef {
      name ok
      type ITEM_TYPE_BUTTON
      text "OK"
      rect 100 100 200 20
      visible 1
      action { close warnMenu ; rundeferred }
    }
  }
})";
    test::writeFile(tmp / "base" / "assets0.pk3", test::buildZip({{"ui/menus.txt", test::toBytes(list), {}},
                                                                  {"ui/items.menu", test::toBytes(items), {}}}));
    // Two saved games (just their engine chunks: the menus read no more),
    // the older first, and an autosave.
    const std::filesystem::path saveDir = tmp / "saves";
    std::filesystem::create_directories(saveDir);
    auto writeSave = [&](const std::string &name, const std::string &comment, int32_t time) {
        raven::SaveWriter w;
        jk2::SaveHeader header;
        header.comment = comment;
        header.time = time;
        header.map = "kejim_post";
        CvarStore none;
        jk2::writeSaveHeader(w, none, header);
        test::writeFile(saveDir / (name + ".sav"), w.bytes());
    };
    writeSave("jkii00", "older", 1000);
    writeSave("jkii01", "newer", 2000);
    writeSave("auto", "---> kejim_post", 3000);
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "base", 0, warnings));
    CvarStore cvars;
    KeyBindings bindings;
    jk2::SpMenus menus(set, cvars);
    menus.bindings = &bindings;
    menus.saveDir = saveDir;
    menus.readSave = [&](const std::string &name, std::vector<uint8_t> &file) {
        std::ifstream in(saveDir / (name + ".sav"), std::ios::binary);
        file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return bool(in) || !file.empty();
    };
    // The datapad's pages, as a client game would draw them.
    HandleTable gameShaders;
    gameShaders.add("gfx/hud/datapad");
    std::vector<int32_t> pages;
    menus.drawDataPad = [&](int32_t command, std::vector<Picture> &out) -> const HandleTable * {
        pages.push_back(command);
        out.push_back({0, 0, 640, 400, 0, 0, 1, 1, 1, {1, 1, 1, 1}});
        return &gameShaders;
    };
    CHECK(!menus.load());
    CHECK(!menus.setActiveMenu("itemsMenu"));

    // The list: newest first, the autosave left out but making Resume
    // possible; the first chosen.
    CHECK(menus.saves().size() == 2);
    CHECK(menus.saves()[0].name == "jkii01" && menus.saves()[0].comment == "newer");
    CHECK(menus.saves()[1].name == "jkii00");
    CHECK(cvars.string("ui_ResumeOK") == "1");
    CHECK(cvars.string("ui_SelectionOK") == "1" && cvars.string("ui_gameDesc") == "newer");
    CHECK(!menus.refresh(0));
    CHECK((pages == std::vector<int32_t>{11, 12}));
    CHECK(std::any_of(menus.pictures().begin(), menus.pictures().end(),
                      [&](const Picture &p) { return menus.shaders().name(p.shader) == "gfx/hud/datapad"; }));

    // The second row clicked (the mouse from the middle to its row), then
    // clicked again soon: loaded.
    CHECK(!menus.mouseEvent(-100, 125 - 240));
    CHECK(menus.focusedItem() == "savelist");
    CHECK(!menus.keyEvent(q3key::kMouse1));
    CHECK(cvars.string("ui_gameDesc") == "older");
    CHECK(!menus.refresh(100));
    CHECK(!menus.keyEvent(q3key::kMouse1));
    CHECK((menus.takeCommands() == std::vector<std::string>{"load jkii00"}));
    CHECK(menus.openMenus().empty());

    // A binding: Enter, then the key; Backspace unbinds.
    CHECK(!menus.setActiveMenu("itemsMenu"));
    CHECK(!menus.mouseEvent(0, 260 - int32_t(menus.cursor()[1])));
    CHECK(menus.focusedItem() == "bind");
    CHECK(!menus.keyEvent(q3key::kEnter) && menus.waitingForKey());
    CHECK(!menus.keyEvent(q3key::kSpace) && !menus.waitingForKey());
    CHECK(bindings.get(q3key::kSpace) == "+moveup");
    CHECK(!menus.keyEvent(q3key::kEnter));
    CHECK(!menus.keyEvent(q3key::kEscape) && !menus.waitingForKey());
    CHECK(bindings.get(q3key::kSpace) == "+moveup");
    CHECK(menus.openMenus().size() == 1);
    CHECK(!menus.keyEvent(q3key::kEnter));
    CHECK(!menus.keyEvent(q3key::kBackspace));
    CHECK(bindings.get(q3key::kSpace).empty());

    // The edit field: typed onto what it holds (the chosen game's
    // description), at most 6 letters.
    cvars.set("ui_gameDesc", "B");
    CHECK(!menus.mouseEvent(0, 50));
    CHECK(menus.focusedItem() == "desc");
    CHECK(!menus.keyEvent(q3key::kEnter) && menus.editing());
    for (const char c : std::string("espin!!"))
        CHECK(!menus.keyEvent(int32_t(uint8_t(c)) | q3key::kCharFlag));
    CHECK(!menus.keyEvent(q3key::kBackspace));
    CHECK(!menus.keyEvent(q3key::kEnter) && !menus.editing());
    CHECK(cvars.string("ui_gameDesc") == "Bespi");

    // Save: put off while the video settings were changed, the warning's
    // OK running the rest; into the first free slot, with the description.
    cvars.set("ui_r_modified", "1");
    CHECK(!menus.mouseEvent(0, 50));
    CHECK(menus.focusedItem() == "save");
    CHECK(!menus.keyEvent(q3key::kEnter));
    CHECK((menus.openMenus() == std::vector<std::string>{"itemsMenu", "warnMenu"}));
    CHECK(menus.takeCommands().empty());
    CHECK(!menus.mouseEvent(0, 110 - int32_t(menus.cursor()[1])));
    CHECK(!menus.keyEvent(q3key::kEnter));
    CHECK((menus.takeCommands() == std::vector<std::string>{"save jkii02"}));
    CHECK(menus.saveComment == "Bespi");

    // Delete: the one chosen.
    CHECK(!menus.mouseEvent(0, 410 - int32_t(menus.cursor()[1])));
    CHECK(menus.focusedItem() == "delete");
    CHECK(!menus.keyEvent(q3key::kEnter));
    CHECK((menus.takeCommands() == std::vector<std::string>{"wipe jkii00"}));
    std::filesystem::remove_all(tmp);
}

static void testJediMenuMotion(const std::filesystem::path &tmp) {
    // Fading, a transition, an orbit and appearance slots, each a step as
    // the menu's clock passes its time; and "update"'s presets.
    namespace jk2 = reluva::idtech3::jk2;
    std::filesystem::remove_all(tmp);
    const char *list = "{\n loadMenu { \"ui/motion.menu\" }\n}\n";
    const char *motion = R"({
  assetGlobalDef {
    fadeClamp 1.0
    fadeCycle 10
    fadeAmount 0.25
  }
  menuDef {
    name "motionMenu"
    rect 0 0 640 480
    appearanceIncrement 100
    onOpen {
      fadeout fader ; transition mover 0 300 10 10 30 300 10 10 50 3 ; orbit orb 100 0 0 0 50 ;
      setcvar ui_r_glCustom 2 ; uiScript update ui_r_glCustom ;
      setcvar ui_r_colorbits 32 ; uiScript update ui_r_colorbits
    }
    itemDef {
      name fader
      style WINDOW_STYLE_FILLED
      background "gfx/fader"
      backcolor 1 1 1 1
      rect 0 200 10 10
      visible 1
    }
    itemDef {
      name mover
      style WINDOW_STYLE_FILLED
      backcolor 1 1 1 1
      rect 0 0 10 10
      visible 0
    }
    itemDef {
      name orb
      style WINDOW_STYLE_FILLED
      backcolor 1 1 1 1
      rect 0 0 20 20
      visible 0
    }
    itemDef {
      name late
      style WINDOW_STYLE_FILLED
      backcolor 1 1 1 1
      rect 0 400 10 10
      appearance_slot 2
      visible 1
    }
  }
})";
    test::writeFile(tmp / "base" / "assets0.pk3", test::buildZip({{"ui/menus.txt", test::toBytes(list), {}},
                                                                  {"ui/motion.menu", test::toBytes(motion), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "base", 0, warnings));
    CvarStore cvars;
    jk2::SpMenus menus(set, cvars);
    CHECK(!menus.load());
    CHECK(!menus.setActiveMenu("motionMenu"));
    // "update": the third quality preset, then 32-bit colour's depth.
    CHECK(cvars.string("ui_r_picmip") == "2" && cvars.string("ui_r_subdivisions") == "12");
    CHECK(cvars.string("ui_r_texturemode") == "GL_LINEAR_MIPMAP_NEAREST");
    CHECK(cvars.string("ui_r_depthbits") == "24");
    auto find = [&](auto &&match) -> const Picture * {
        for (const Picture &p : menus.pictures())
            if (match(p))
                return &p;
        return nullptr;
    };
    auto fader = [&] { return find([](const Picture &p) { return p.y == 200; }); };
    auto mover = [&] { return find([](const Picture &p) { return p.y == 300; }); };
    auto orb = [&] { return find([](const Picture &p) { return p.w == 20; }); };
    auto late = [&] { return find([](const Picture &p) { return p.y == 400; }); };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    // The orbit: the item's middle (110, 10 from the centre) turned 3
    // degrees a step.
    auto orbited = [&](int steps) {
        float x = 100;
        float y = 0;
        const double a = 3 * 3.14159265358979323846 / 180;
        for (int i = 0; i < steps; ++i) {
            const float rx = x + 10;
            const float ry = y + 10;
            x = rx * float(std::cos(a)) - ry * float(std::sin(a)) - 10;
            y = rx * float(std::sin(a)) + ry * float(std::cos(a)) - 10;
        }
        const Picture *p = orb();
        return p && near(p->x, x) && near(p->y, y);
    };

    CHECK(!menus.refresh(10));
    CHECK(fader() && near(fader()->color[3], 0.75f));
    CHECK(mover() && near(mover()->x, 10));
    CHECK(orbited(1));
    CHECK(!late()); // its slot comes 100 ms after the first
    CHECK(!menus.refresh(70));
    CHECK(fader() && near(fader()->color[3], 0.5f));
    CHECK(mover() && near(mover()->x, 20));
    CHECK(orbited(2));
    CHECK(!late());
    CHECK(!menus.refresh(130));
    CHECK(fader() && near(fader()->color[3], 0.25f));
    CHECK(mover() && near(mover()->x, 30));
    CHECK(late());
    CHECK(!menus.refresh(190));
    CHECK(fader() && near(fader()->color[3], 0)); // faded out: hidden after this frame
    CHECK(mover() && near(mover()->x, 30));
    CHECK(!menus.refresh(250));
    CHECK(!fader());
    std::filesystem::remove_all(tmp);
}

static const reluva::test::Suite kSuite(
    "idtech3.unit.game-code",
    {
        {"server game", [](const std::filesystem::path &tmp) { testGameHost(tmp / "game"); }},
        {"client game", [](const std::filesystem::path &tmp) { testCgameHost(tmp / "cgame"); }},
        {"menus", [](const std::filesystem::path &tmp) { testUiHost(tmp / "ui"); }},
        {"Jedi Outcast's menus", [](const std::filesystem::path &tmp) { testJediMenus(tmp / "jk2menus"); }},
        {"Jedi Outcast's menu items", [](const std::filesystem::path &tmp) { testJediMenuItems(tmp / "jk2items"); }},
        {"Jedi Outcast's menu motion", [](const std::filesystem::path &tmp) { testJediMenuMotion(tmp / "jk2motion"); }},
        {"map commands", [](const std::filesystem::path &) { testMapCommands(); }},
        {"save chunks", [](const std::filesystem::path &) { testSaveChunks(); }},
        {"compressed save chunks", [](const std::filesystem::path &) { testCompressedSaveChunks(); }},
        {"save kind", [](const std::filesystem::path &) { testSaveKind(); }},
    });
