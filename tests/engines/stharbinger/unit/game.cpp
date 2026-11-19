// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The game run without its real-time loop: the script interpreter, the
// conversations, videos and cursors, the action sequences, the game menu
// with saving and loading, the credits and deck map, playing as the
// original, special functions and animations. Ported from STDS9H's tests.
//
//   reluva-tests stharbinger.unit.game

#include "engines/stharbinger/formats/archive.h"
#include "engines/stharbinger/formats/audio.h"
#include "engines/stharbinger/formats/dialogue.h"
#include "engines/stharbinger/formats/font.h"
#include "engines/stharbinger/formats/graphics.h"
#include "engines/stharbinger/formats/resources.h"
#include "engines/stharbinger/formats/script.h"
#include "engines/stharbinger/game/game.h"
#include "tests/common/data/smacker_videos.h"
#include "tests/engines/stharbinger/fixtures.h"
#include "tests/support/test.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

using namespace reluva;
using namespace reluva::stharbinger;
using namespace reluva::stharbinger::testing;

static void test_script_interpreter(const std::filesystem::path &tmp) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "scripts";

    // Scene S0 (SCENE 0): variables a, b, c; hotspot h0; timer t0; animation spin.
    ScriptBuilder s0;
    s0.cmd(kCmdScene, {sym(0)})
        .cmd(kCmdAnimDef, {sym(0), sym(kAnimModeClear)})
        .cmd(kCmdLet, {num(5), var(0)})         // a = 5
        .cmd(kCmdGosub, {sym(1)})               // a += 1 in the subroutine
        .cmd(kCmdIfE, {var(0), num(6), sym(2)}) // a == 6: skip the next LET
        .cmd(kCmdLet, {num(99), var(1)})
        .cmd(kCmdLabel, {sym(2)})
        .cmd(kCmdSwitch, {var(0), sym(9), sym(9), sym(9), sym(9), sym(9), sym(9), sym(4)})
        .cmd(kCmdLet, {num(77), var(1)}) // skipped by SWITCH (a = 6 -> label 4)
        .cmd(kCmdLabel, {sym(4)})
        .cmd(kCmdSub, {num(7), var(0)}) // 7 > a: a stays 6
        .cmd(kCmdHotspot, {sym(0), num(10), num(10), num(20), num(20), sym(3), sym(kCursorTouch)})
        .cmd(kCmdHotOn, {sym(0)})
        .cmd(kCmdTimer, {sym(0), num(3), sym(5), sym(0)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(1)})
        .cmd(kCmdAdd, {num(1), var(0)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(5)})        // timer handler (one-shot): b += 10,
        .cmd(kCmdAdd, {num(10), var(1)}) // then play the animation once
        .cmd(kCmdAnim, {sym(0), sym(kAnimStart), num(0)})
        .cmd(kCmdAnimSeq, {sym(0), sym(kAnimToEnd), sym(kAnimOn)})
        .cmd(kCmdAnimSuspend, {sym(0)}) // waits for the last frame
        .cmd(kCmdLet, {num(1), var(2)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(3)}) // hotspot handler
        .cmd(kCmdNewScene, {sym(1)});
    // Scene S1 (SCENE 1): its variable 0 is "b", shared with S0 by name.
    ScriptBuilder s1;
    s1.cmd(kCmdScene, {sym(1)}).cmd(kCmdAdd, {num(1), var(0)}).cmd(kCmdReturn);

    writeArchive(dir, "CC", {s0.build(), s1.build()});
    writeArchive(dir, "CV", {nameList({"a", "b", "c"}, "S0.CCG"), nameList({"B"}, "S1.CCG")});
    writeArchive(dir, "CH", {nameList({"h0"}, "S0.CCG"), {}});
    writeArchive(dir, "CT", {nameList({"t0"}, "S0.CCG"), {}});
    writeArchive(dir, "CA", {nameList({"spin"}, "S0.CCG"), {}});
    putFile(dir / "ANIMS" / "SPIN" / "SPIN.RLE", makeRle());

    NullSystem system;

    Platform platform(system, nullptr, {});

    Harbinger engine(&platform, optionsFor(dir));
    engine.initResources();
    CHECK((engine.getScenes().size()) == (size_t(2)));
    CHECK(engine.getScenes()[0].name == "S0");

    Game game(&engine);
    game.start(engine.findScene("S0"));
    CHECK(!game.isScriptRunning());
    CHECK((game.getVariable("a")) == (6));
    CHECK((game.getVariable("b")) == (0));
    CHECK(!game.isAnimVisible("spin"));

    // Timers count 100 ms ticks (5 steps of 20 ms) and fire once.
    game.runSteps(14);
    CHECK((game.getVariable("b")) == (0));
    game.runSteps(1);
    CHECK((game.getVariable("b")) == (10));
    CHECK(game.isScriptRunning()); // waiting for the animation
    CHECK(game.isAnimVisible("spin"));
    CHECK((game.getVariable("c")) == (0));
    game.runSteps(3); // frames 0, 1, 2
    CHECK((game.getVariable("c")) == (1));
    CHECK(!game.isScriptRunning());
    CHECK((game.getAnimFrame("spin")) == (2)); // TOEND: stays on the last frame
    game.runSteps(100);
    CHECK((game.getVariable("b")) == (10));
    CHECK((game.getAnimFrame("spin")) == (2));

    game.click(5, 5); // misses the hotspot
    CHECK(game.getSceneName() == "S0");
    game.click(30, 30); // edges are inclusive
    CHECK(game.getSceneName() == "S1");
    CHECK((game.getVariable("b")) == (11));
}

// Dialogue command numbers used by the test.
const uint8_t kDlgGotoCmd = 0x32, kDlgExitCmd = 0x39, kDlgBackCmd = 0x40;

static void test_conversation(const std::filesystem::path &tmp) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "conversation";

    // Dialogue COM010 (CCB entry 0).
    ScriptBuilder listA, listB, npc0, npc1, pickYes, pickNo, none;
    listB.cmd(kDlgBackCmd, {{kArgByte, 0}});
    pickYes.cmd(kCmdLet, {num(1), var(0)}).cmd(kDlgGotoCmd, {{kArgByte, 1}});
    pickNo.cmd(kDlgExitCmd);
    std::vector<uint8_t> ccb;
    auto append = [&](const std::vector<uint8_t> &v) { ccb.insert(ccb.end(), v.begin(), v.end()); };
    auto u16 = [&](uint16_t v) {
        ccb.push_back(uint8_t(v));
        ccb.push_back(uint8_t(v >> 8));
    };
    append(listA.build());
    append(listB.build());
    // NPC tree: group 0 = line 0, group 1 = line 3.
    ccb.push_back(2);
    for (uint16_t line : {0, 3}) {
        ccb.push_back(1);
        u16(line);
        u16(0xFFFF);
        append(none.build());
        ccb.push_back(0);
    }
    // Choice tree: group 0 = "Yes" (line 1, choice 0), "No" (line 2, choice 1).
    ccb.push_back(1);
    ccb.push_back(2);
    u16(1);
    u16(0);
    append(pickYes.build());
    ccb.push_back(0);
    u16(2);
    u16(1);
    append(pickNo.build());
    ccb.push_back(0);
    writeArchive(dir, "CCB", {ccb});
    std::string ccq = std::string("Hello\0[Yes] Yes.\0[No] No.\0Bye\0", 30);
    writeArchive(dir, "CCQ", {std::vector<uint8_t>(ccq.begin(), ccq.end())});
    writeArchive(dir, "CCF", {bytes("comcom00\r\ncompro01\r\ncompro02\r\ncomcom03\r\n")});
    writeArchive(dir, "CCR", {std::vector<uint8_t>{'Y', 'e', 's', 0, 'N', 'o', 0}});
    // Speech: 100 ms per line; no lip-sync data needed without talkers.
    std::vector<std::vector<uint8_t>> clips(4, std::vector<uint8_t>(2205, 0x80));
    writeArchive(dir / "DS9" / "DIALOGUE" / "COM010", "COM010", clips);
    writeArchive(dir / "DS9" / "PHONEMES" / "COM010", "COM010", {{}, {}, {}, {}});

    // Scene: talk on entry, then d = 5; the hotspot starts a second conversation.
    ScriptBuilder s0;
    s0.cmd(kCmdScene, {sym(0)})
        .cmd(kCmdHotspot, {sym(0), num(0), num(0), num(10), num(10), sym(1), sym(kCursorTalk)})
        .cmd(kCmdHotOn, {sym(0)})
        .cmd(kCmdConverse, {sym(0), sym(0)})
        .cmd(kCmdLet, {num(5), var(1)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(1)})
        .cmd(kCmdConverse, {sym(0), sym(0)})
        .cmd(kCmdAdd, {num(1), var(1)})
        .cmd(kCmdReturn);
    writeArchive(dir, "CC", {s0.build()});
    writeArchive(dir, "CV", {nameList({"c", "d"}, "S0.CCG")});
    writeArchive(dir, "CH", {nameList({"talk"}, "S0.CCG")});
    writeArchive(dir, "CD", {nameList({"com010"}, "com010.ccd")});
    putFile(dir / "DS9.LCC", bytes("S0\r\n"));
    putFile(dir / "DS9.LVM", bytes(std::string("\x02\x00", 2) + std::string("c\0d\0", 4)));
    putFile(dir / "DS9.LDM", bytes(std::string("\x01\x00", 2) + std::string("Com010\0", 7)));
    fs::path saves = tmp / "conversation_saves";
    fs::create_directories(saves);

    NullSystem system;

    Platform platform(system, nullptr, saves);

    Harbinger engine(&platform, optionsFor(dir));
    engine.initResources();
    Game game(&engine);
    game.start(0);

    CHECK(game.isConversationActive());
    CHECK(game.getMenuLabels().empty()); // the NPC is speaking
    game.runSteps(20);                   // 100 ms line + 250 ms pause
    std::vector<std::string> menu = game.getMenuLabels();
    CHECK(menu.size() == 2 && menu[0] == "Yes" && menu[1] == "No");
    game.pickMenuEntry(0);
    CHECK(game.getMenuLabels().empty());
    CHECK((game.getVariable("c")) == (0)); // the player's line comes first
    game.runSteps(20);
    CHECK((game.getVariable("c")) == (1));
    CHECK(game.isConversationActive()); // NPC group 1 speaks
    CHECK((game.getVariable("d")) == (0));
    game.runSteps(20);
    CHECK(!game.isConversationActive()); // no menu for group 1: the end
    CHECK((game.getVariable("d")) == (5));

    // Save and load: the dialogue's state comes back.
    game.rightClick(300, 300);
    CHECK(game.isMenuActive());
    game.pressMenuButton(1);
    game.typeSaveName("Talked");
    game.pressMenuButton(2);
    game.answerMenu(true);
    CHECK(!game.isMenuActive());
    CHECK((game.getVariable("d")) == (5));

    // Again: BACK b0 starts at group 0; "Yes" was said, so "No" is taken at once.
    game.click(5, 5);
    CHECK(game.isConversationActive());
    game.runSteps(20);
    CHECK(game.getMenuLabels().empty());
    game.runSteps(40);
    CHECK(!game.isConversationActive());
    CHECK((game.getVariable("d")) == (6));
}

static void test_videos_and_cursors(const std::filesystem::path &tmp) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "video";

    // ACTIONSEQ #28 plays INTRO/VNM.SMK (here the 4-frame test video, 100 ms per
    // frame); #7 (TRANS/PREPLAN.SMK) is missing and returns 999.
    ScriptBuilder s0;
    s0.cmd(kCmdScene, {sym(0)})
        .cmd(kCmdActionSeq, {num(28), var(0)})
        .cmd(kCmdLet, {num(1), var(1)})
        .cmd(kCmdActionSeq, {num(7), var(2)})
        .cmd(kCmdHotspot, {sym(0), num(100), num(100), num(10), num(10), sym(1), sym(kCursorTouch)})
        .cmd(kCmdHotOn, {sym(0)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(1)})
        .cmd(kCmdLet, {num(1), var(3)})
        .cmd(kCmdReturn);
    writeArchive(dir, "CC", {s0.build()});
    writeArchive(dir, "CV", {nameList({"result", "after", "missing", "clicked"}, "S0.CCG")});
    writeArchive(dir, "CH", {nameList({"spot"}, "S0.CCG")});
    std::vector<uint8_t> smk;
    smk.assign(kSmackerTest2, kSmackerTest2 + sizeof kSmackerTest2);
    putFile(dir / "DS9" / "INTRO" / "VNM.SMK", smk);
    // mouse.ani with 26 cursors of 20x10, transparent 0, hotspot (2, 3).
    std::vector<uint8_t> ani = {26, 0};
    for (int i = 0; i < 26; i++) {
        std::vector<uint8_t> hdr = {0, 0, 10, 0, 20, 0, 3, 0, 2, 0, 0, 0};
        ani.insert(ani.end(), hdr.begin(), hdr.end());
        ani.insert(ani.end(), 200, uint8_t(i + 1));
    }
    putFile(dir / "ART" / "MOUSE.ANI", ani);

    NullSystem system;

    Platform platform(system, nullptr, {});

    Harbinger engine(&platform, optionsFor(dir));
    engine.initResources();
    Game game(&engine);
    game.start(0);
    CHECK(game.isScriptRunning()); // the video plays
    CHECK((game.getVariable("after")) == (0));
    game.runSteps(15); // 300 of 400 ms
    CHECK((game.getVariable("after")) == (0));
    game.runSteps(6);
    CHECK((game.getVariable("result")) == (0)); // played to the end
    CHECK((game.getVariable("after")) == (1));
    CHECK((game.getVariable("missing")) == (999));
    CHECK(!game.isScriptRunning());

    // Hotspots are tested at the cursor's centre: (90, 95) + (10, 5) is inside.
    game.click(85, 95);
    CHECK((game.getVariable("clicked")) == (0));
    game.click(90, 95);
    CHECK((game.getVariable("clicked")) == (1));
}

namespace {

// A scene animation: one w x h frame of one colour at (x, y).
std::vector<uint8_t> makeBlock(int x, int y, int w, int h, uint8_t color) {
    std::vector<uint8_t> f = {0x00, 0x00, 0x01, 0x01, 0x0A, 0xFF, 0x00, 0x00, 0x01, 0x00};
    std::vector<uint8_t> body;
    for (int row = 0; row < h; row++) {
        body.push_back(0x40);
        body.push_back(uint8_t(w));
        body.insert(body.end(), size_t(w), color);
    }
    std::vector<uint8_t> hdr(25, 0);
    hdr[1] = uint8_t(w);
    hdr[3] = uint8_t(h);
    hdr[5] = uint8_t(x);
    hdr[7] = uint8_t(y);
    hdr[21] = uint8_t(body.size());
    f.insert(f.end(), hdr.begin(), hdr.end());
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

} // namespace

namespace {

// An arcade sprite: 8 header bytes, then `frames` 8x8 frames at (x, y) in the
// video rectangle with gun 0 at (4, 4).
std::vector<uint8_t> makeSprite(int start, int hp, int damage, int period, int gun, int id, int frames, int x, int y) {
    std::vector<uint8_t> f = {uint8_t(start), uint8_t(start >> 8), uint8_t(hp),      uint8_t(damage), uint8_t(period),
                              uint8_t(gun),   uint8_t(id),         uint8_t(id >> 8), uint8_t(frames), 0};
    for (int i = 0; i < frames; i++) {
        std::vector<uint8_t> body;
        for (int row = 0; row < 8; row++) {
            body.push_back(0x40);
            body.push_back(0x08);
            body.insert(body.end(), 8, 5);
        }
        std::vector<uint8_t> hdr(25, 0);
        hdr[1] = 8;
        hdr[3] = 8;
        hdr[5] = uint8_t(x);
        hdr[7] = uint8_t(y);
        hdr[9] = 4;  // muzzle 0 x
        hdr[15] = 4; // muzzle 0 y
        hdr[21] = uint8_t(body.size());
        f.insert(f.end(), hdr.begin(), hdr.end());
        f.insert(f.end(), body.begin(), body.end());
    }
    return f;
}

// Runs ACTIONSEQ `type` in a one-scene game with the given sprites; `play`
// drives it. Returns the result variable.
template<class Play>
int runArcade(const std::filesystem::path &tmp, int type, const char *name,
              const std::vector<std::vector<uint8_t>> &sprites, Play play,
              const std::map<std::string, std::vector<uint8_t>> &extraFiles = {}) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "arcade";
    ScriptBuilder s0;
    s0.cmd(kCmdScene, {sym(0)}).cmd(kCmdActionSeq, {num(uint16_t(type)), var(0)}).cmd(kCmdReturn);
    writeArchive(dir, "CC", {s0.build()});
    writeArchive(dir, "CV", {nameList({"result"}, "S0.CCG")});
    std::vector<uint8_t> smk;
    smk.assign(kSmackerLong, kSmackerLong + sizeof kSmackerLong);
    putFile(dir / "DS9" / "MOVIES" / (std::string(name) + ".SMK"), smk);
    if (type == 6) // the plan door: from frame 0, at (20, 20) in the video
        putFile(dir / "DS9" / "SPRITES" / "PDOOR.RLE", makeSprite(0, 1, 0, 0, 0xFF, 0, 40, 20, 20));
    for (size_t i = 0; i < sprites.size(); i++)
        putFile(dir / "DS9" / "SPRITES" / name / (std::string(1, char('A' + i)) + ".RLE"), sprites[i]);
    for (const auto &f : extraFiles)
        putFile(dir / "DS9" / f.first, f.second);

    NullSystem system;

    Platform platform(system, nullptr, {});

    Harbinger engine(&platform, optionsFor(dir));
    engine.initResources();
    Game game(&engine);
    game.start(0);
    CHECK(game.isArcadeActive());
    play(game);
    int result = game.getVariable("result");
    return result;
}

} // namespace

static void test_arcade_sequences(const std::filesystem::path &tmp) {
    // LAND (type 2): video rectangle at (120, 12); 100 ms per frame = 5 steps.
    // A boss (id 200, 2 hits) shot twice with the phaser: won 11 frames later.
    int r = runArcade(tmp, 2, "LAND", {makeSprite(0, 2, 0, 0, 0xFF, 200, 40, 10, 10)}, [](Game &g) {
        g.runSteps(6);
        g.click(134, 26);
        g.runSteps(5);
        CHECK(g.isArcadeActive());
        g.click(134, 26);
        g.runSteps(5 * 10);
        CHECK(g.isArcadeActive()); // the win counts down
        g.runSteps(5 * 3);
        CHECK(!g.isArcadeActive());
    });
    CHECK((r) == (0x8000));

    // Missing the boss: the video ends with the player alive (0).
    r = runArcade(tmp, 2, "LAND", {makeSprite(0, 2, 0, 0, 0xFF, 200, 40, 10, 10)}, [](Game &g) {
        g.click(300, 200);
        g.runSteps(5 * 41);
    });
    CHECK((r) == (0));

    // SPAC (type 3): an enemy firing every 2 frames for 40 damage kills the
    // player on its third shot.
    r = runArcade(tmp, 3, "SPAC", {makeSprite(0, 9, 40, 2, 0, 0, 40, 10, 10)}, [](Game &g) {
        g.runSteps(5 * 6);
        CHECK(!g.isArcadeActive());
    });
    CHECK((r) == (0xFFFE));

    // Killed: the cockpit's TV static (PANEL/SPACSTAT.RLE) shows before the death
    // video (TRANS/DEATHMIX.SMK).
    std::vector<uint8_t> deathVideo;
    deathVideo.assign(kSmackerTest2, kSmackerTest2 + sizeof kSmackerTest2);
    r = runArcade(tmp, 3, "SPAC", {makeSprite(0, 9, 40, 2, 0, 0, 40, 10, 10)},
                  [](Game &g) {
                      g.runSteps(5 * 6);
                      CHECK(!g.isArcadeActive());
                      CHECK(g.isScriptRunning()); // the static, then the video
                      g.runSteps(10);
                      CHECK((g.getScreen().pixels.data()[205 * 640 + 205]) == (3));
                      g.runSteps(40);
                      CHECK(g.isScriptRunning()); // the video plays
                      g.runSteps(30);
                      CHECK(!g.isScriptRunning());
                  },
                  {{"PANEL/SPACSTAT.RLE", makeBlock(200, 200, 20, 20, 3)}, {"TRANS/DEATHMIX.SMK", deathVideo}});
    CHECK((r) == (0xFFFE));

    // A torpedo volley hits on its fifth frame and counts 2 against a boss.
    r = runArcade(tmp, 2, "LAND", {makeSprite(0, 2, 0, 0, 0xFF, 200, 40, 10, 10)}, [](Game &g) {
        g.runSteps(6);
        g.rightClick(134, 26);
        g.runSteps(5 * 3);
        CHECK(g.isArcadeActive());
        g.runSteps(5 * 15);
        CHECK(!g.isArcadeActive());
    });
    CHECK((r) == (0x8000));

    // PLAN (type 6, rectangle at (80, 62)): ten phaser hits destroy the door; the
    // video ends with a win. Without them the door survives: 65533.
    r = runArcade(tmp, 6, "PLAN", {}, [](Game &g) {
        for (int i = 0; i < 10; i++) {
            g.click(104, 86);
            g.runSteps(5);
        }
        g.runSteps(5 * 31);
        CHECK(!g.isArcadeActive());
    });
    CHECK((r) == (0x8000));
    r = runArcade(tmp, 6, "PLAN", {}, [](Game &g) { g.runSteps(5 * 41); });
    CHECK((r) == (0xFFFD));
}

static void test_menu_save_and_load(const std::filesystem::path &tmp) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "save";
    fs::path saves = tmp / "saves";
    fs::create_directories(saves);

    // Two scenes; S0 sets a variable, defines hotspots and a timer; clicking
    // "go" adds 1 to $count, clicking "away" moves to S1.
    ScriptBuilder s0, s1;
    s0.cmd(kCmdScene, {sym(0)})
        .cmd(kCmdLet, {num(7), var(0)})
        .cmd(kCmdHotspot, {sym(0), num(0), num(0), num(10), num(10), sym(1), sym(kCursorTouch)})
        .cmd(kCmdHotspot, {sym(1), num(50), num(0), num(10), num(10), sym(2), sym(kCursorDoor)})
        .cmd(kCmdHotOn, {sym(0), sym(1)})
        .cmd(kCmdTimer, {sym(0), num(100), sym(1), sym(0)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(1)})
        .cmd(kCmdAdd, {num(1), var(1)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(2)})
        .cmd(kCmdNewScene, {sym(1)});
    s1.cmd(kCmdScene, {sym(1)}).cmd(kCmdReturn);
    writeArchive(dir, "CC", {s0.build(), s1.build()});
    writeArchive(dir, "CV", {nameList({"seven", "count"}, "S0.CCG"), nameList({"count"}, "S1.CCG")});
    writeArchive(dir, "CH", {nameList({"go", "away"}, "S0.CCG"), {}});
    writeArchive(dir, "CT", {nameList({"later"}, "S0.CCG"), {}});
    // The linker files: scene order, global variables, dialogue slots.
    putFile(dir / "DS9.LCC", bytes("S1\r\nS0\r\n"));
    std::string lvm = std::string("\x03\x00", 2) + std::string("other\0count\0seven\0", 18);
    putFile(dir / "DS9.LVM", bytes(lvm));
    putFile(dir / "DS9.LDM", {0, 0});
    // Cursors: 26 of 4x4 in colour 9.
    std::vector<uint8_t> ani = {26, 0};
    for (int i = 0; i < 26; i++) {
        std::vector<uint8_t> hdr = {0, 0, 4, 0, 4, 0, 0, 0, 0, 0, 0, 0};
        ani.insert(ani.end(), hdr.begin(), hdr.end());
        ani.insert(ani.end(), 16, 9);
    }
    putFile(dir / "ART" / "MOUSE.ANI", ani);
    putFile(dir / "ART" / "SHORT.FNT", {1, 0, 2, 3, 2, 0, 0}); // one blank glyph

    NullSystem system;

    Platform platform(system, nullptr, saves);

    Harbinger engine(&platform, optionsFor(dir));
    engine.initResources();
    Game game(&engine);
    game.start(engine.findScene("S0"));
    game.click(5, 5);
    game.click(5, 5);
    CHECK((game.getVariable("count")) == (2));

    // Right click: the menu, with its font before any conversation. Save as "First".
    game.rightClick(300, 300);
    CHECK(game.isMenuActive());
    CHECK(game.hasMenuFont());
    game.pressMenuButton(1);
    game.typeSaveName("First");
    CHECK((game.getSaveCount()) == (1));
    CHECK(fs::exists(saves / "savegame" / "00000000.ds9"));
    // Original layout: 2+4+4+4+4 + "ds9\0" + "First\0" + picture + palette ...
    size_t size = size_t(fs::file_size(saves / "savegame" / "00000000.ds9"));
    CHECK(size > 18 + 4 + 6 + 307200 + 768);
    game.pressMenuButton(4); // resume
    CHECK(!game.isMenuActive());

    // The cursor leaves no trail over the menu.
    game.rightClick(300, 300);
    game.moveMouse(400, 400);
    game.runSteps(1);
    CHECK((game.getScreen().pixels.data()[401 * 640 + 401]) == (9)); // cursor colour
    game.moveMouse(450, 420);
    game.runSteps(1);
    CHECK(game.getScreen().pixels.data()[401 * 640 + 401] != 9);
    game.pressMenuButton(4);

    // Change things: more clicks, then leave for S1.
    game.click(5, 5);
    CHECK((game.getVariable("count")) == (3));
    game.click(55, 5);
    CHECK(game.getSceneName() == "S1");

    // Load: back in S0 with count 2, hotspots working, the timer still set.
    game.rightClick(300, 300);
    CHECK(game.isMenuActive());
    game.pressMenuButton(2);
    game.answerMenu(true);
    CHECK(!game.isMenuActive());
    CHECK(game.getSceneName() == "S0");
    CHECK((game.getVariable("count")) == (2));
    CHECK((game.getVariable("seven")) == (7));
    CHECK(!game.isScriptRunning()); // idle, as when saved
    game.click(5, 5);
    CHECK((game.getVariable("count")) == (3));
    game.runSteps(5 * 100); // the timer fires after 10 s
    CHECK((game.getVariable("count")) == (4));

    // Delete.
    game.rightClick(300, 300);
    CHECK(game.isMenuActive());
    CHECK(!game.isScriptRunning());
    game.pressMenuButton(5);
    game.answerMenu(true);
    CHECK((game.getSaveCount()) == (0));
}

namespace {

// A minimal DEEP.EXE: an MZ stub pointing to an LE header with one data object
// at 0x50000 whose table at 0x55464 points to 269 lines.
std::vector<uint8_t> makeExe(const std::vector<std::string> &lines) {
    std::vector<uint8_t> exe(0x40, 0);
    exe[0] = 'M';
    exe[1] = 'Z';
    exe[0x3C] = 0x40; // e_lfanew
    std::vector<uint8_t> le(0x100, 0);
    le[0] = 'L';
    le[1] = 'E';
    auto put32 = [](std::vector<uint8_t> &v, size_t o, uint32_t x) {
        for (int i = 0; i < 4; i++)
            v[o + i] = uint8_t(x >> (8 * i));
    };
    put32(le, 0x28, 4096);        // page size
    put32(le, 0x40, 0x84);        // object table (relative to LE)
    put32(le, 0x44, 1);           // one object
    put32(le, 0x80, 0x200);       // data pages (relative to the MZ)
    put32(le, 0x84 + 0, 0x8000);  // object size
    put32(le, 0x84 + 4, 0x50000); // base
    put32(le, 0x84 + 12, 1);      // first page
    exe.insert(exe.end(), le.begin(), le.end());
    exe.resize(0x200, 0);
    std::vector<uint8_t> obj(0x8000, 0);
    uint32_t pos = 0x6000;
    for (int i = 0; i < 269; i++) {
        const std::string &l = lines[i % lines.size()];
        put32(obj, 0x5464 + i * 4, pos);
        std::copy(l.begin(), l.end(), obj.begin() + pos);
        pos += uint32_t(l.size()) + 1;
    }
    exe.insert(exe.end(), obj.begin(), obj.end());
    return exe;
}

} // namespace

static void test_credits_and_deck_map(const std::filesystem::path &tmp) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "extras";
    ScriptBuilder s0;
    s0.cmd(kCmdScene, {sym(0)}).cmd(kCmdActionSeq, {num(0xA015), var(0)}).cmd(kCmdReturn);
    writeArchive(dir, "CC", {s0.build()});
    writeArchive(dir, "CV", {nameList({"ret"}, "S0.CCG")});
    putFile(dir / "DEEP.EXE", makeExe({"*Heading", "A name", ""}));
    putFile(dir / "DS9" / "MAPS" / "LOWER.DAT", std::vector<uint8_t>(393 * 767, 1));

    NullSystem system;

    Platform platform(system, nullptr, {});

    Harbinger engine(&platform, optionsFor(dir));
    engine.initResources();
    std::vector<std::string> credits = engine.getResources().loadCredits();
    CHECK((credits.size()) == (size_t(269)));
    CHECK(credits[0] == "*Heading" && credits[1] == "A name" && credits[2].empty());

    Game game(&engine);
    game.start(0);
    CHECK(!game.isScriptRunning());
    // Location 21 is on the lower decks: M shows the map, Esc closes it.
    game.pressKey('m');
    CHECK(game.isDeckMapOpen());
    game.runSteps(3);
    game.pressKey(key::Escape);
    CHECK(!game.isDeckMapOpen());

    // Credits from the menu: they scroll to the end, or Esc ends them.
    game.rightClick(300, 300);
    CHECK(game.isMenuActive());
    game.pressMenuButton(10);
    CHECK(game.isCreditsActive());
    game.runSteps(100);
    CHECK(game.isCreditsActive());
    game.pressKey(key::Escape);
    game.runSteps(30);
    CHECK(!game.isCreditsActive());
    CHECK(game.isMenuActive()); // back in the menu
    game.pressMenuButton(10);
    game.runSteps(50 + 2 * (432 + 269 * 24) / 4 + 50);
    CHECK(!game.isCreditsActive());
}

static void test_original_mode(const std::filesystem::path &tmp) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "original";
    ScriptBuilder s0;
    s0.cmd(kCmdScene, {sym(0)}).cmd(kCmdActionSeq, {num(0xA015), var(0)}).cmd(kCmdReturn);
    writeArchive(dir, "CC", {s0.build()});
    writeArchive(dir, "CV", {nameList({"ret"}, "S0.CCG")});
    putFile(dir / "DS9" / "MAPS" / "LOWER.DAT", std::vector<uint8_t>(393 * 767, 1));

    for (bool original : {false, true}) {
        NullSystem system;
        Platform platform(system, nullptr, {});
        Options options = optionsFor(dir);
        options.original = original;
        Harbinger engine(&platform, options);
        engine.initResources();
        Game game(&engine);
        game.start(0);

        // Esc opens the menu only with the conveniences; right click always.
        game.pressKey(key::Escape);
        CHECK(game.isMenuActive() == !original);
        if (!game.isMenuActive())
            game.rightClick(300, 300);
        CHECK(game.isMenuActive());
        // Confirmations: keys only with the conveniences; the mouse always.
        game.pressMenuButton(3); // quit?
        game.pressKey('n');
        game.rightClick(300, 300);               // original: right button = yes
        CHECK(game.isMenuActive() == !original); // "n" said no; else quit was confirmed
        if (game.isMenuActive()) {
            // Esc resumes only with the conveniences.
            game.pressKey(key::Escape);
            CHECK(!game.isMenuActive());
        }

        // The deck map: Esc closes it; M too only with the conveniences.
        Game game2(&engine);
        game2.start(0);
        game2.pressKey('m');
        CHECK(game2.isDeckMapOpen());
        game2.pressKey('m');
        CHECK(game2.isDeckMapOpen() == original);
        game2.pressKey(key::Escape);
        CHECK(!game2.isDeckMapOpen());
    }
}

static void test_special_functions(const std::filesystem::path &tmp) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "special";
    // Countdown: stage 1 at location 0 (25 s + 1 + 2 for normal difficulty = 28 s,
    // returned as 280 timer ticks); stop it; start stage 5 at location 9 (0 s).
    // Then 100 enemy shots from hotspot 0, counting hits; and a player phaser.
    ScriptBuilder s0;
    s0.cmd(kCmdScene, {sym(0)})
        .cmd(kCmdActionSeq, {num(0xF100), var(0)})
        .cmd(kCmdActionSeq, {num(0xF000), var(1)})
        .cmd(kCmdLet, {num(0xF509), var(2)})
        .cmd(kCmdActionSeq, {var(2), var(2)}) // through a variable, as the game does
        .cmd(kCmdHotspot, {sym(0), num(300), num(100), num(40), num(40), sym(9), sym(0)})
        .cmd(kCmdSetRect, {sym(0), num(300), num(100), num(40), num(40)})
        .cmd(kCmdLet, {num(0), var(3)})
        .cmd(kCmdLabel, {sym(1)})
        .cmd(kCmdActionSeq, {num(0xC200), var(4)})
        .cmd(kCmdIfNE, {var(4), num(65534), sym(2)})
        .cmd(kCmdAdd, {num(1), var(5)})
        .cmd(kCmdLabel, {sym(2)})
        .cmd(kCmdAdd, {num(1), var(3)})
        .cmd(kCmdIfLT, {var(3), num(100), sym(1)})
        .cmd(kCmdActionSeq, {num(0xC000), var(6)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(9)})
        .cmd(kCmdReturn);
    writeArchive(dir, "CC", {s0.build()});
    writeArchive(dir, "CV", {nameList({"cd", "off", "near", "i", "ret", "hits", "phaser"}, "S0.CCG")});
    writeArchive(dir, "CH", {nameList({"zap"}, "S0.CCG")});

    NullSystem system;

    Platform platform(system, nullptr, {});

    Harbinger engine(&platform, optionsFor(dir));
    engine.initResources();
    Game game(&engine);
    game.start(0);
    CHECK((game.getVariable("cd")) == (280));
    CHECK((game.getVariable("off")) == (999));
    CHECK((game.getVariable("near")) == (30)); // a zero entry: 0 + 1 + 2 = 3 s
    int hits = game.getVariable("hits");
    CHECK(hits > 15 && hits < 55); // about a third
    CHECK((game.getVariable("phaser")) == (999));
    game.runSteps(10);
}

static void test_anim_composit(const std::filesystem::path &tmp) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "composit";
    // A: 4x4 colour 5 at (0, 0). B: 4x4 colour 7 at (2, 2), drawn on top. After
    // one step, ANIMCOMPOSIT A copies the screen in A's rectangle into the
    // background; with both turned off, that part stays.
    ScriptBuilder s0;
    s0.cmd(kCmdScene, {sym(0)})
        .cmd(kCmdAnimDef, {sym(0), sym(kAnimModeClear)})
        .cmd(kCmdAnimDef, {sym(1), sym(kAnimModeClear)})
        .cmd(kCmdAnim, {sym(0), sym(kAnimStart)})
        .cmd(kCmdAnim, {sym(1), sym(kAnimStart)})
        .cmd(kCmdAnimPrio, {sym(1), num(1)})
        .cmd(kCmdTimer, {sym(0), num(1), sym(1), sym(0)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(1)})
        .cmd(kCmdAnimComposit, {sym(0)})
        .cmd(kCmdAnim, {sym(0), sym(kAnimOff)})
        .cmd(kCmdAnim, {sym(1), sym(kAnimOff)})
        .cmd(kCmdReturn);
    writeArchive(dir, "CC", {s0.build()});
    writeArchive(dir, "CA", {nameList({"blka", "blkb"}, "S0.CCG")});
    writeArchive(dir, "CT", {nameList({"t"}, "S0.CCG")});
    putFile(dir / "ANIMS" / "BLKA" / "BLKA.RLE", makeBlock(0, 0, 4, 4, 5));
    putFile(dir / "ANIMS" / "BLKB" / "BLKB.RLE", makeBlock(2, 2, 4, 4, 7));

    NullSystem system;

    Platform platform(system, nullptr, {});

    Harbinger engine(&platform, optionsFor(dir));
    engine.initResources();
    Game game(&engine);
    game.start(0);
    game.runSteps(20);
    const uint8_t *p = game.getScreen().pixels.data();
    CHECK(!game.isAnimVisible("blka") && !game.isAnimVisible("blkb"));
    CHECK((p[1 * 640 + 1]) == (5)); // A, baked
    CHECK((p[3 * 640 + 3]) == (7)); // B over A, baked
    CHECK((p[5 * 640 + 5]) == (0)); // B outside A's rectangle: gone
}

static void test_anim_leftovers(const std::filesystem::path &tmp) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "leftovers";
    // The renderer only erases clear animations and ones turned off. A (not
    // clear) and B (clear) move 10 pixels right: A leaves its old picture, B
    // does not. C is shown for one frame and stays; D is turned off and goes.
    ScriptBuilder s0;
    s0.cmd(kCmdScene, {sym(0)})
        .cmd(kCmdAnimDef, {sym(0), sym(kAnimModeOnTheFly)})
        .cmd(kCmdAnimDef, {sym(1), sym(kAnimModeClear)})
        .cmd(kCmdAnimDef, {sym(2), sym(kAnimModeClear)})
        .cmd(kCmdAnimDef, {sym(3), sym(kAnimModeOnTheFly)})
        .cmd(kCmdAnim, {sym(0), sym(kAnimStart)})
        .cmd(kCmdAnim, {sym(1), sym(kAnimStart)})
        .cmd(kCmdAnim, {sym(2), sym(kAnimOneFrame)})
        .cmd(kCmdAnim, {sym(3), sym(kAnimStart)})
        .cmd(kCmdTimer, {sym(0), num(1), sym(1), sym(0)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(1)})
        .cmd(kCmdAnimPos, {sym(0), num(10), num(0)})
        .cmd(kCmdAnimPos, {sym(1), num(10), num(0)})
        .cmd(kCmdAnim, {sym(3), sym(kAnimOff)})
        .cmd(kCmdReturn);
    writeArchive(dir, "CC", {s0.build()});
    writeArchive(dir, "CA", {nameList({"blka", "blkb", "blkc", "blkd"}, "S0.CCG")});
    writeArchive(dir, "CT", {nameList({"t"}, "S0.CCG")});
    putFile(dir / "ANIMS" / "BLKA" / "BLKA.RLE", makeBlock(0, 0, 4, 4, 5));
    putFile(dir / "ANIMS" / "BLKB" / "BLKB.RLE", makeBlock(0, 20, 4, 4, 7));
    putFile(dir / "ANIMS" / "BLKC" / "BLKC.RLE", makeBlock(0, 40, 4, 4, 9));
    putFile(dir / "ANIMS" / "BLKD" / "BLKD.RLE", makeBlock(0, 60, 4, 4, 11));

    NullSystem system;

    Platform platform(system, nullptr, {});

    Harbinger engine(&platform, optionsFor(dir));
    engine.initResources();
    Game game(&engine);
    game.start(0);
    game.runSteps(2);
    const uint8_t *p = game.getScreen().pixels.data();
    CHECK((p[1 * 640 + 1]) == (5));
    CHECK((p[21 * 640 + 1]) == (7));
    CHECK((p[41 * 640 + 1]) == (9));
    CHECK((p[61 * 640 + 1]) == (11));
    game.runSteps(20);
    p = game.getScreen().pixels.data();
    CHECK(!game.isAnimVisible("blkc"));
    CHECK(!game.isAnimVisible("blkd"));
    CHECK((p[1 * 640 + 11]) == (5));  // A moved
    CHECK((p[1 * 640 + 1]) == (5));   // and left a trail
    CHECK((p[21 * 640 + 11]) == (7)); // B moved
    CHECK((p[21 * 640 + 1]) == (0));  // and was erased
    CHECK((p[41 * 640 + 1]) == (9));  // C stays after its one frame
    CHECK((p[61 * 640 + 1]) == (0));  // D erased when turned off
}

static void test_beam_timing(const std::filesystem::path &tmp) {
    namespace fs = std::filesystem;
    fs::path dir = tmp / "beams";
    // One enemy shot (colours 1..3) from hotspot 0. As in the original (0x22BD4):
    // drawn on the screen at once, copied into the work buffer once on the next
    // render, left there on the second and wiped on the third.
    ScriptBuilder s0;
    s0.cmd(kCmdScene, {sym(0)})
        .cmd(kCmdHotspot, {sym(0), num(300), num(100), num(40), num(40), sym(9), sym(0)})
        .cmd(kCmdSetRect, {sym(0), num(300), num(100), num(40), num(40)})
        .cmd(kCmdActionSeq, {num(0xC200), var(0)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(9)})
        .cmd(kCmdReturn);
    writeArchive(dir, "CC", {s0.build()});
    writeArchive(dir, "CV", {nameList({"ret"}, "S0.CCG")});
    writeArchive(dir, "CH", {nameList({"zap"}, "S0.CCG")});

    NullSystem system;

    Platform platform(system, nullptr, {});

    Harbinger engine(&platform, optionsFor(dir));
    engine.initResources();
    Game game(&engine);
    auto beamPixels = [&game]() {
        int n = 0;
        for (uint8_t c : game.getScreen().pixels)
            n += c >= 1 && c <= 3;
        return n;
    };
    game.start(0);
    CHECK(beamPixels() > 0); // drawn straight onto the screen
    game.runSteps(1);
    int first = beamPixels();
    CHECK(first > 0); // redrawn into the work buffer
    game.runSteps(1);
    CHECK((beamPixels()) == (first)); // left as it is: no second jittered copy
    game.runSteps(1);
    CHECK((beamPixels()) == (0)); // wiped
}

static const reluva::test::Suite
    suite("stharbinger.unit.game",
          {
              {"script interpreter", [](const std::filesystem::path &tmp) { test_script_interpreter(tmp); }},
              {"conversation", [](const std::filesystem::path &tmp) { test_conversation(tmp); }},
              {"videos and cursors", [](const std::filesystem::path &tmp) { test_videos_and_cursors(tmp); }},
              {"arcade sequences", [](const std::filesystem::path &tmp) { test_arcade_sequences(tmp); }},
              {"menu save and load", [](const std::filesystem::path &tmp) { test_menu_save_and_load(tmp); }},
              {"credits and deck map", [](const std::filesystem::path &tmp) { test_credits_and_deck_map(tmp); }},
              {"original mode", [](const std::filesystem::path &tmp) { test_original_mode(tmp); }},
              {"special functions", [](const std::filesystem::path &tmp) { test_special_functions(tmp); }},
              {"anim composit", [](const std::filesystem::path &tmp) { test_anim_composit(tmp); }},
              {"anim leftovers", [](const std::filesystem::path &tmp) { test_anim_leftovers(tmp); }},
              {"beam timing", [](const std::filesystem::path &tmp) { test_beam_timing(tmp); }},
          });
