// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: The hosts that run the games' code: the server game, the client game, the menus, saved games.

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

// Saved games' chunks: written and read back in order, checked.
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
    CHECK(saveKind(save(2720)) == SaveKind::Original);
    CHECK(saveKind(save(3364)) == SaveKind::Expansion);
    CHECK(!saveKind(save(100)));
    std::vector<uint8_t> noClient = save(2720);
    noClient.resize(16 + 128);
    CHECK(!saveKind(noClient) && !saveKind(std::vector<uint8_t>{1, 2, 3}));
}

// The game logic starting a level: its entities handed over token by
// token, as the original server does, by a QVM the Quake III compiler
// made from tests/engines/idtech3/data/qvm/fake_game.c.
static void testGameHost(const std::filesystem::path& tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/qagame.qvm",
                                     std::vector<uint8_t>(std::begin(kFakeGameQvm), std::end(kFakeGameQvm)), {}}}));
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
    CHECK(!game.call(24, {-200, 0, 500, 0, 2}, r) && r == 1022); // ignoring the item
    CHECK(!game.call(24, {0, 0, 100, 16, -1}, r) && !game.call(25, {}, r) && r == 16 * 8 + 1); // a box
    CHECK(!game.call(24, {0, 0, -10, 0, -1}, r) && !game.call(26, {}, r) && r == 101); // starting in the floor
    CHECK(!game.call(24, {1000, 0, 100, 0, -1}, r) && r == 1023); // nothing there
    CHECK(!game.call(28, {0, 0, -32}, r) && r == contents::Solid);
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
        int32_t w44 = 0, w48 = -1;
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
static void testCgameHost(const std::filesystem::path& tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/cgame.qvm",
                                     std::vector<uint8_t>(std::begin(kFakeCgameQvm), std::end(kFakeCgameQvm)), {}},
                                    {"models/test/box.md3", buildMd3(), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::CgameHost cgame(set, cvars);
    CHECK(!cgame.load());
    cgame.setConfigstrings({{12, "baseef-1"}, {289, "sound/player/fry.wav"}, {544, "n\\Reluva\\t\\0"}, {5, ""}});
    int32_t r = 0;
    CHECK(!cgame.call(ef::CgameHost::Init, {0, 0, 3}, r) && r == 3);
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
static void testUiHost(const std::filesystem::path& tmp) {
    using test::toBytes;
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
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
    CHECK(!ui.call(ef::UiHost::Init, {}, r) && r == 1202); // 12 bytes, 2 .txt files directly in scripts/
    CHECK(ui.log() == "fake ui\n");
    CHECK(cvars.string("ui_test") == "7" && cvars.find("ui_test")->flags == CvarStore::Archive);
    CHECK(!ui.call(20, {}, r) && r == 7);
    cvars.set("ui_test", "9");
    CHECK(!ui.call(20, {}, r) && r == 7); // the program's copy changes on Cvar_Update
    CHECK(!ui.call(21, {}, r) && r == 9);
    CHECK(!ui.call(25, {}, r) && r == 2);
    CHECK(!ui.call(22, {}, r) && r == 640480);

    CHECK(!ui.call(ef::UiHost::SetActiveMenu, {1}));
    CHECK(!ui.call(ef::UiHost::Refresh, {1234}, r) && r == 1234);
    CHECK(ui.pictures().size() == 3); // two, then the pointer
    if (ui.pictures().size() == 3) {
        const auto& p = ui.pictures()[0];
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
    CHECK(!ui.call(ef::UiHost::KeyEvent, {'q', 1}));
    CHECK(!ui.call(ef::UiHost::KeyEvent, {'Q' | 1024, 1}));
    CHECK(!ui.call(27, {}, r) && r == 'Q' * 1000 + 1);

    // The CD key: taken if it has 18 letters and digits, kept in the key
    // file, read back by a new run, which then counts it as checked.
    ui.setKeyFile(tmp / "save" / "cdkey.txt");
    CHECK(!ui.call(28, {1}, r) && r == 0 && !std::filesystem::exists(tmp / "save" / "cdkey.txt"));
    CHECK(!ui.call(28, {0}, r) && r == 1 && std::filesystem::exists(tmp / "save" / "cdkey.txt"));
    // Call 58, the model bounds the main menu asks for: frame 0's box.
    Md3Model box;
    CHECK(!parseMd3(buildMd3(), box));
    const auto& f0 = box.frames[0];
    const int boxCode = int(f0.mins[0] * 10) + int(f0.mins[1] * 100) + int(f0.mins[2] * 1000) +
                        int(f0.maxs[0] * 10000) + int(f0.maxs[1] * 100000) + int(f0.maxs[2] * 1000000);
    CHECK(boxCode != 0);
    CHECK(!ui.call(29, {}, r) && r == boxCode);
    CHECK(!ui.call(29, {99}, r) && r == 0); // no such model: zeros
    CvarStore fresh;
    ef::UiHost again(set, fresh);
    again.setKeyFile(tmp / "save" / "cdkey.txt");
    CHECK(!again.load() && !again.call(ef::UiHost::Init) && fresh.string("ui_cdkeychecked") == "1");

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
    ui.startLocalSound = [&](const std::string& name, int channel) { played.emplace_back(name, channel); };
    CHECK(!ui.call(33));
    CHECK(played.size() == 1 && played[0].first == "sound/interface/button1.wav" && played[0].second == 6);

    CHECK(ui.unhandled().empty());
    std::filesystem::remove_all(tmp);
}

static const reluva::test::Suite suite("idtech3.unit.game-code", {
    {"server game", [](const std::filesystem::path& tmp) { testGameHost(tmp / "game"); }},
    {"client game", [](const std::filesystem::path& tmp) { testCgameHost(tmp / "cgame"); }},
    {"menus", [](const std::filesystem::path& tmp) { testUiHost(tmp / "ui"); }},
    {"save chunks", [](const std::filesystem::path&) { testSaveChunks(); }},
    {"save kind", [](const std::filesystem::path&) { testSaveKind(); }},
});
