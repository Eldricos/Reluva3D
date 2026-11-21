// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: Input and settings: the joystick, key bindings, video settings.

#include "common/fs.h"
#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/client/console_drawer.h"
#include "engines/idtech3/client/console_state.h"
#include "engines/idtech3/render/world_effects.h"
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
#include "engines/idtech3/raven/random.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/server/server_world.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/jk2/patched_keys.h"
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

static void testJoystick() {
    // As the original engines read the joystick (docs/engines/idtech3/games/stvef/systems/input.md).
    CvarStore cvars;
    JoystickInput::registerCvars(cvars);
    JoystickInput joy;
    auto event = [&](EventType type, int a = 0, int value = 0) {
        Event ev;
        ev.type = type;
        ev.joyButton = ev.joyAxis = a;
        ev.joyValue = value;
        ev.joyHat = uint8_t(a);
        joy.event(ev);
    };
    event(EventType::kJoyButtonDown, 0);
    CHECK(joy.frame(cvars).keys.empty()); // in_joystick 0: nothing
    cvars.set("in_joystick", "1");
    JoystickInput::Frame f = joy.frame(cvars);
    CHECK(f.keys.size() == 1 && f.keys[0] == std::make_pair(185, true)); // JOY1
    // x past the threshold to the left: the left arrow (joy_xbutton 1, the
    // default); y pushed forward all the way: analog, 127 (joy_ybutton 0).
    event(EventType::kJoyAxis, 0, -20000);
    event(EventType::kJoyAxis, 1, -32768);
    f = joy.frame(cvars);
    CHECK(f.keys.size() == 1 && f.keys[0] == std::make_pair(134, true) && f.x == 0 && f.y == 127);
    // Inside the threshold: let go; analog x when joy_xbutton is 0, right
    // negative; the hat's up is JOY24.
    event(EventType::kJoyAxis, 0, 3000);
    f = joy.frame(cvars);
    CHECK(f.keys.size() == 1 && f.keys[0] == std::make_pair(134, false));
    cvars.set("joy_xbutton", "0");
    event(EventType::kJoyAxis, 0, 32767);
    event(EventType::kJoyHat, joyhat::kUp);
    f = joy.frame(cvars);
    CHECK(f.x == -126 && f.keys.size() == 1 && f.keys[0] == std::make_pair(208, true));
    event(EventType::kJoyHat, joyhat::kUp | joyhat::kRight); // no diagonals
    f = joy.frame(cvars);
    CHECK(f.keys.size() == 1 && f.keys[0] == std::make_pair(208, false));
    // Off again: everything held let go.
    cvars.set("in_joystick", "0");
    f = joy.frame(cvars);
    CHECK(f.keys.size() == 1 && f.keys[0] == std::make_pair(185, false) && f.x == 0 && f.y == 0);
    CHECK(KeyBindings::keyName(185) == "JOY1" && KeyBindings::keyName(216) == "JOY32" &&
          KeyBindings::keyName(232) == "AUX16");

    // The player: y forward moves forward, added to the keys' and held to
    // 127; x turns at a hundredth of cl_yawspeed per step (right: negative).
    PlayerInput input;
    input.setJoystick(-127, 127);
    input.turn(cvars, 1000);
    const GameInfo game = eliteforce::gameInfo();
    const PlayerInput::Command c = input.command(cvars, game);
    CHECK(c.forward == 127 && c.right == 0 && std::fabs(input.yaw + 140 * 1.27f) < 0.01f);
}

void testKeyBindings() {
    using reluva::idtech3::KeyBindings;
    namespace q3key = reluva::idtech3::q3key;
    CHECK(KeyBindings::keyNumber("MOUSE2") == q3key::kMouse2 && KeyBindings::keyNumber("space") == q3key::kSpace);
    CHECK(KeyBindings::keyNumber("W") == 'w' && KeyBindings::keyNumber("F12") == q3key::kF1 + 11);
    CHECK(KeyBindings::keyNumber("nonsense") == -1 && KeyBindings::keyNumber("0x22") == '"');
    CHECK(KeyBindings::keyName(q3key::kWheelUp) == "MWHEELUP" && KeyBindings::keyName('w') == "w");
    CHECK(KeyBindings::keyName(';') == "SEMICOLON" && KeyBindings::keyName(q3key::kF1 + 2) == "F3");
    // Keys Quake III hasn't, by Reluva's own numbers: named as Jedi
    // Outcast's patches name them, a character by its byte.
    CHECK(KeyBindings::keyName(q3key::kCharacters + 0x80) == "EURO" &&
          KeyBindings::keyName(q3key::kScrollLock) == "SCROLLLOCK");
    CHECK(KeyBindings::keyName(q3key::kCharacters + 0xfc) == "\xfc" &&
          KeyBindings::keyNumber("\xfc") == q3key::kCharacters + 0xfc);
    CHECK(KeyBindings::keyNumber("circumflex") == q3key::kCharacters + 0x88);
    CHECK(toQ3Key(reluva::key::kScrollLock) == q3key::kScrollLock &&
          toQ3Key(reluva::key::kCapsLock) == q3key::kCapsLock);
    CHECK(toQ3Key(reluva::key::kCharacter + 0x20ac) == q3key::kCharacters + 0x80 &&
          toQ3Key(reluva::key::kCharacter + 0x3b1) == 0);
    CHECK(toQ3Key(reluva::key::kKp0 + 7) == 160 && toQ3Key(reluva::key::kKp0) == 170 &&
          toQ3Key(reluva::key::kKpPeriod) == 171);
    reluva::Event under;
    under.key = '^';
    under.consoleKey = true;
    CHECK(toQ3Key(under) == '`');

    reluva::idtech3::CvarStore cvars;
    KeyBindings b;
    int ignored = 0;
    auto readFile = [](const std::string &name, std::string &out) {
        if (name != "more.cfg")
            return false;
        out = "bind x \"say hi\"\n";
        return true;
    };
    b.execute("unbindall\nbind w +forward // a comment\nbind MOUSE2 \"+altattack\"\n"
              "bind 3 \"weapon 3; echo three\"\nseta sensitivity 7\nset cl_run 0; exec more\n"
              "unbind w\nbind TAB +scores\nconnect somewhere\n",
              cvars, &ignored, readFile);
    CHECK(b.get('w').empty() && b.get(q3key::kMouse2) == "+altattack" && b.get('3') == "weapon 3; echo three");
    CHECK(b.get('x') == "say hi" && b.get(q3key::kTab) == "+scores" && b.changed);
    CHECK(cvars.integer("sensitivity") == 7 && (cvars.find("sensitivity")->flags & cvars.kArchive));
    CHECK(cvars.integer("cl_run") == 0 && ignored == 1);
    // Written and read back, the same.
    KeyBindings again;
    again.execute(b.write(), cvars);
    for (int32_t k = 0; k < q3key::kCount; ++k)
        CHECK(again.get(k) == b.get(k));
    const auto parts = reluva::idtech3::splitCommands("a \"b;c\"; d\ne // f; g");
    CHECK(parts.size() == 3 && parts[0] == "a \"b;c\"" && reluva::idtech3::splitCommand(parts[0]).size() == 2);

    // Jedi Outcast 1.03's and 1.04's numbers, and back: every key Quake III
    // has a name for that they have too.
    using reluva::idtech3::jk2::fromPatchedKey;
    using reluva::idtech3::jk2::toPatchedKey;
    CHECK(toPatchedKey(q3key::kMouse1) == 141 && toPatchedKey(q3key::kEnter) == 10 && toPatchedKey(q3key::kUp) == 170);
    CHECK(toPatchedKey('5') == '5' && toPatchedKey(q3key::kCharFlag | 'W') == (q3key::kCharFlag | 'W'));
    CHECK(toPatchedKey(185) == 256 && toPatchedKey(217) == 288 && toPatchedKey(q3key::kF1 + 12) == 0);
    // Letters as capitals; a character past ASCII as its byte, even where
    // that's another key's number ("\x84" is F5's).
    CHECK(toPatchedKey('a') == 'A' && fromPatchedKey('A') == 'a' && toPatchedKey(';') == 59);
    CHECK(toPatchedKey(q3key::kCharacters + 0x80) == 128 && toPatchedKey(q3key::kCharacters + 0x88) == 136);
    CHECK(toPatchedKey(q3key::kCharacters + 0x84) == 132 && fromPatchedKey(132) == q3key::kF1 + 4);
    CHECK(toPatchedKey('`') == 26 && fromPatchedKey(26) == '`' && toPatchedKey(q3key::kScrollLock) == 6);
    for (int32_t k = 1; k < q3key::kCount; ++k)
        if (const int32_t p = toPatchedKey(k); p != 0)
            CHECK(fromPatchedKey(p) == k || p == k - q3key::kCharacters || (k >= 'A' && k <= 'Z'));
}

void testVideoSettings() {
    using namespace reluva::idtech3;
    CvarStore cvars;
    VideoSettings v = videoSettings(cvars);
    CHECK(v.width == 960 && v.height == 720 && !v.fullscreen && v.mipmaps && v.subdivisions == 4);
    cvars.set("r_mode", "3");
    cvars.set("r_fullscreen", "1");
    cvars.set("r_picmip", "9");
    cvars.set("r_texturemode", "GL_LINEAR_MIPMAP_LINEAR");
    cvars.set("r_subdivisions", "999");
    cvars.set("r_lodBias", "1");
    cvars.set("r_fastsky", "1");
    cvars.set("r_ext_texture_filter_anisotropic", "1");
    v = videoSettings(cvars);
    CHECK(v.anisotropic);
    CHECK(v.width == 640 && v.height == 480 && v.fullscreen);
    CHECK(v.picmip == 4 && !v.nearest && v.mipmaps && !v.mipNearest);
    CHECK(v.subdivisions == 80 && v.lodBias == 1 && v.fastSky);
    cvars.set("r_mode", "-1");
    cvars.set("r_customwidth", "1920");
    cvars.set("r_customheight", "1080");
    cvars.set("r_texturemode", "GL_NEAREST");
    v = videoSettings(cvars);
    CHECK(v.width == 1920 && v.height == 1080 && v.nearest && !v.mipmaps);

    // A 3x3 patch: flat cuts once, a bend cuts finer for a smaller error.
    BspVertex control[9];
    for (int i = 0; i < 9; ++i)
        control[i].pos = {float(i % 3) * 64, float(i / 3) * 64, 0};
    CHECK(patchLevel(control, 3, 3, 4) == 1);
    control[4].pos[2] = 64;
    const int coarse = patchLevel(control, 3, 3, 4);
    const int fine = patchLevel(control, 3, 3, 0.5f);
    CHECK(coarse >= 2 && fine > coarse && fine <= 16);
}

static void testConsole() {
    // Wrapping: a word that would reach the edge moves down, one as long
    // as a line is cut, and the colour carries over. The original counts
    // the word from each character, so the rest of a cut word that no
    // longer fits moves down too.
    CHECK((wrapConsoleLine("abc ^2defg hi", 8) == std::vector<std::string>{"abc ^2", "^2defg hi"}));
    CHECK((wrapConsoleLine("abcdefghij", 4) == std::vector<std::string>{"abcd", "efg", "hij"}));
    CHECK((wrapConsoleLine("", 10) == std::vector<std::string>{""}));

    // Half open on a 640 x 480 screen: the background down to 238, the
    // line under it, the version at the foot's right, the newest line
    // 48 pixels up from the foot and the input line 32 up.
    std::deque<std::string> lines{"one", "^1two"};
    ConsoleFrame f;
    f.frac = 0.5f;
    f.version = "V1";
    f.lines = &lines;
    f.typing = true;
    f.input = "ab";
    f.cursor = 2;
    const ConsoleShaders shaders{1, 2, 3};
    std::vector<Picture> p = consolePictures(f, shaders);
    CHECK(p.size() >= 2 && p[0].shader == 2 && p[0].h == 238 && p[1].shader == 3 && p[1].y == 238 && p[1].h == 2);
    auto find = [&](float x, float y) -> const Picture * {
        for (const Picture &q : p)
            if (q.shader == 1 && q.x == x && q.y == y)
                return &q;
        return nullptr;
    };
    const Picture *v = find(640 - 16, 240 - 24);
    CHECK(v && v->w == 8 && v->h == 16 && v->color[0] < 0.6f);
    // 'V' (86): column 6, row 5 of the grid, the cell's left half.
    CHECK(v && v->s1 == 6 * 0.0625f && v->t1 == 5 * 0.0625f && v->s2 == v->s1 + 0.03125f);
    // Each x has the original's 640 / width scale added before rounding:
    // 1 here.
    const Picture *two = find(9, 240 - 48);
    CHECK(two && two->color[0] == 1 && two->color[1] == 0);
    CHECK(find(9, 240 - 64) != nullptr);                                          // "one", above it
    CHECK(find(9, 240 - 32) != nullptr && find(17 + 2 * 8, 240 - 32) != nullptr); // ']', the cursor
    // Shut: the notify lines from the top, then the chat line in big
    // characters with their shadows.
    f.frac = 0;
    f.notify = {"hi"};
    f.chat = ConsoleFrame::Chat::kSay;
    f.chatInput = "x";
    f.chatCursor = 1;
    p = consolePictures(f, shaders);
    CHECK(find(9, 0) != nullptr);
    CHECK(find(8, 16) != nullptr && find(10, 18) != nullptr && find(8, 16)->w == 16);
}

static void testConsoleState() {
    // Printing: whole lines, each stream keeping its unfinished one; the
    // notify lines the last four, shown a while.
    ConsoleState c;
    c.print("one\ntw", true, 1000);
    c.print("three\n", false, 1000, 1);
    c.print("o\n", true, 2000);
    CHECK((c.lines() == std::deque<std::string>{"one", "three", "two"}));
    CHECK((c.notify(2500, 3000) == std::vector<std::string>{"one", "two"}));
    CHECK((c.notify(4500, 3000) == std::vector<std::string>{"two"}));
    for (int i = 0; i < 5; ++i)
        c.print("n\n", true, 3000);
    CHECK(c.notifyLines().size() == 4);
    c.expireNotify(7000, 3000);
    CHECK(c.notifyLines().empty());

    // Typing: the opening key's character left out, the console key too;
    // editing; Enter keeps the line (quotes made apostrophes) in the
    // history and the console.
    auto text = [](const char *t) {
        Event e;
        e.type = EventType::kTextInput;
        std::snprintf(e.text, sizeof e.text, "%s", t);
        return e;
    };
    auto keyDown = [](uint32_t k) {
        Event e;
        e.type = EventType::kKeyDown;
        e.key = k;
        return e;
    };
    c.beginTyping('`');
    CHECK(c.handle(text("`go~d\"x"), true) == ConsoleState::Action::kNone);
    CHECK(c.field() == "god\"x");
    c.handle(keyDown(key::kBackspace), true);
    c.handle(keyDown(key::kLeft), true);
    c.handle(keyDown(key::kDelete), true);
    CHECK(c.field() == "god" && c.cursor() == 3);
    c.handle(text(" 1"), true);
    CHECK(c.handle(keyDown(key::kReturn), true) == ConsoleState::Action::kSubmit);
    CHECK(c.submit(true) == "god 1" && c.field().empty());
    CHECK(c.lines().back() == "]god 1");
    c.handle(text("say \"hi\""), true);
    CHECK(c.submit(true) == "say 'hi'");
    // The history, back and forth; PageUp scrolls back; Esc and the
    // console key close.
    c.handle(keyDown(key::kUp), true);
    CHECK(c.field() == "say 'hi'");
    c.handle(keyDown(key::kUp), true);
    CHECK(c.field() == "god 1");
    c.handle(keyDown(key::kDown), true);
    c.handle(keyDown(key::kDown), true);
    CHECK(c.field().empty());
    // Two rows a page key; scrolled back, the view stays on its lines as
    // more come; Ctrl+End to the newest, Ctrl+Home to the oldest.
    c.handle(keyDown(key::kPageUp), true);
    CHECK(c.scroll() == 2);
    c.print("more\n", false, 0);
    CHECK(c.scroll() == 3);
    Event ctrlEnd = keyDown(key::kEnd);
    ctrlEnd.mods = keymod::kCtrl;
    c.handle(ctrlEnd, true);
    CHECK(c.scroll() == 0);
    Event ctrlHome = keyDown(key::kHome);
    ctrlHome.mods = keymod::kCtrl;
    c.handle(ctrlHome, true);
    CHECK(c.scroll() == int(c.lines().size()) - 1);
    // At a width, a line counts as the rows it wraps to.
    c.handle(ctrlEnd, true);
    c.setWrapWidth(10);
    c.handle(keyDown(key::kPageUp), true);
    c.print("a line of twenty-one\n", false, 0);
    CHECK(c.scroll() == 2 + int(wrapConsoleLine("a line of twenty-one", 10).size()));
    CHECK(c.handle(keyDown(key::kEscape), true) == ConsoleState::Action::kClose);
    CHECK(c.handle(keyDown('`'), true) == ConsoleState::Action::kClose);
    CHECK(c.handle(keyDown('`'), false) == ConsoleState::Action::kNone);

    // Each print starts white, and its colour goes on to its next line; a
    // line carried on from an earlier print goes back to white.
    ConsoleState colours;
    colours.print("^1red\nstill red\n", false, 0);
    colours.print("^2green, then", false, 0);
    colours.print(" white\n", false, 0);
    CHECK((colours.lines() == std::deque<std::string>{"^1red", "^1still red", "^2green, then^7 white"}));

    // The field's scroll, as the original keeps it (a field 5 wide): on
    // a character as the cursor passes its width, back one as Left passes
    // its start; Home moves the cursor only; Ctrl+A goes back to the start.
    ConsoleState f;
    f.beginTyping(0, 5);
    f.handle(text("abcdefg"), false);
    CHECK(f.cursor() == 7 && f.fieldScroll() == 3);
    f.handle(keyDown(key::kHome), false);
    CHECK(f.cursor() == 0 && f.fieldScroll() == 3);
    for (int i = 0; i < 3; ++i)
        f.handle(keyDown(key::kRight), false);
    f.handle(keyDown(key::kLeft), false);
    f.handle(keyDown(key::kLeft), false);
    CHECK(f.cursor() == 1 && f.fieldScroll() == 1);
    Event ctrlA = keyDown('a');
    ctrlA.mods = keymod::kCtrl;
    f.handle(ctrlA, false);
    CHECK(f.cursor() == 0 && f.fieldScroll() == 0);
    // Drawn, a scroll past where the line ends is pulled back and kept.
    f.handle(keyDown(key::kEnd), false);
    for (int i = 0; i < 6; ++i)
        f.handle(keyDown(key::kBackspace), false);
    CHECK(f.field() == "a" && f.fieldScroll() == 0);
    // Insert: typing over.
    f.handle(keyDown(key::kInsert), false);
    f.handle(keyDown(key::kHome), false);
    f.handle(text("xy"), false);
    CHECK(f.overstrike() && f.field() == "xy");
    // Pasting (Ctrl+V, Shift+Insert): the clipboard's text to its first
    // \n, \r or \b, in the Windows code page; what's past ASCII dropped,
    // control letters as their keys (^A to the start).
    f.handle(keyDown(key::kInsert), false);
    f.handle(keyDown(key::kEnd), false);
    std::string clip = "\r\nk\xc3\xa9y \x01!\x08gone\nsecond line";
    f.clipboard = [&] { return clip; };
    Event ctrlV = keyDown('v');
    ctrlV.mods = keymod::kCtrl;
    f.handle(ctrlV, false);
    CHECK(f.field() == "!xyky " && f.cursor() == 1);
    Event shiftIns = keyDown(key::kInsert);
    shiftIns.mods = keymod::kShift;
    clip = "Z";
    f.handle(shiftIns, false);
    CHECK(f.field() == "!Zxyky " && !f.overstrike());

    // Dropping down at 3 screens a second: half open in a sixth of one.
    CHECK(c.slide(1000, true, 3) == 0);
    CHECK(c.slide(1100, true, 3) > 0.29f && c.slide(1200, true, 3) == 0.5f);
    CHECK(c.slide(1400, false, 3) == 0);
}

static void testRandom() {
    // The C library's rand() from its default seed, as both programs'
    // (seeded 1 its first draws are 41, 18467, 6334, 26500); jk2mp.exe's
    // Q_flrand and Q_irand from theirs, 0x89abcdef.
    reluva::idtech3::raven::CrtRandom crt;
    CHECK(crt.next() == 41 && crt.next() == 18467 && crt.next() == 6334 && crt.next() == 26500);
    reluva::idtech3::raven::QRandom q;
    CHECK(std::fabs(q.flrand(0, 1) - 0.436065674f) < 1e-6f && q.irand(200, 450) == 261);
    for (int i = 0; i < 1000; ++i) {
        const int32_t r = q.irand(3, 5);
        CHECK(r >= 3 && r <= 5);
    }
}

static void testWorldEffects() {
    using Batches = std::vector<WorldEffects::Batch>;
    auto vertices = [](const Batches &batches, WorldEffects::Look look) {
        std::vector<gfx::Vertex> out;
        for (const WorldEffects::Batch &b : batches)
            if (b.look == look)
                out.insert(out.end(), b.vertices.begin(), b.vertices.end());
        return out;
    };
    // The rain: started by its command, set by others, faded in only in
    // the open and out of water, its drops falling and starting again.
    WorldEffects w;
    int32_t here = 0;
    w.contents = [&](const std::array<float, 3> &) { return here; };
    auto frame = [&](WorldEffects &e, float seconds, const std::array<float, 3> &eye, Batches &out) {
        WorldEffects::ViewParms view;
        view.eye = eye;
        out.clear();
        e.update(seconds, view);
        e.draw(view, out);
    };
    CHECK(!w.command("rain alpha 0.3").understood); // no rain yet
    CHECK(w.command("rain init 50").understood && w.raining() && !w.snowing());
    CHECK(w.command("rain alpha 0.3").understood && w.command("rain fall ( -100 -90 )").understood &&
          !w.command("rain sideways").understood);
    Batches batches;
    frame(w, 0.1f, {0, 0, 0}, batches);
    CHECK(vertices(batches, WorldEffects::Look::kRain).empty() && w.rainFade() == 0); // indoors: none
    here = WorldEffects::kContentsOutside | WorldEffects::kContentsWater;
    frame(w, 1, {0, 0, 0}, batches);
    CHECK(w.rainFade() == 0 && !w.fallingHere());
    here = WorldEffects::kContentsOutside;
    frame(w, 1, {100, 200, 300}, batches);
    CHECK(w.rainFade() == 0.5f && w.fallingHere());
    std::vector<gfx::Vertex> tris = vertices(batches, WorldEffects::Look::kRain);
    CHECK(tris.size() == 150);
    // Each a drop, across by a fifth of the left axis, then 5 down; round
    // the eye (within 20 across, 20 under to 40 over); no more opaque than
    // the alpha times the fade.
    bool near = true;
    bool faint = true;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const gfx::Vertex &a = tris[i];
        const gfx::Vertex &b = tris[i + 1];
        const gfx::Vertex &c = tris[i + 2];
        near = near && std::abs(b.pos[1] - a.pos[1] - 0.2f) < 1e-3f && std::abs(c.pos[2] - b.pos[2] + 5) < 1e-3f &&
               a.pos[2] >= 300 - 20.1f && a.pos[2] <= 340.1f;
        faint = faint && a.color[3] <= uint8_t(0.3f * 0.5f * 255 + 1);
    }
    CHECK(near && faint);
    // Falling for a while, they stay round the eye; "rain remove" stops it.
    for (int i = 0; i < 100; ++i)
        frame(w, 0.05f, {0, 0, 0}, batches);
    tris = vertices(batches, WorldEffects::Look::kRain);
    CHECK(tris.size() == 150 && w.rainFade() == 1);
    CHECK(std::all_of(tris.begin(), tris.end(), [](const gfx::Vertex &v) { return v.pos[2] > -26 && v.pos[2] < 41; }));
    float speed = 0;
    CHECK(w.windSpeed(speed) && speed == 75);
    CHECK(w.command("rain remove").understood && !w.raining());

    // Snow: flakes in a box round the eye, drawn only in its open cells
    // (here, below 100: the cells from -168.75 up, 56.25 high, open up to
    // 112.5); "exec" is multiplayer's alone.
    WorldEffects s(WorldEffects::Engine::kMultiplayer);
    s.contents = [](const std::array<float, 3> &p) { return p[2] < 100 ? WorldEffects::kContentsOutside : 0; };
    CHECK(s.command("snow init 200").understood && s.snowing());
    for (int i = 0; i < 5; ++i)
        frame(s, 0.05f, {0, 0, 0}, batches);
    std::vector<gfx::Vertex> flakes = vertices(batches, WorldEffects::Look::kPlain);
    CHECK(!flakes.empty() && flakes.size() % 6 == 0 && s.fallingHere());
    CHECK(std::all_of(flakes.begin(), flakes.end(), [](const gfx::Vertex &v) {
        return v.pos[2] > -201 && v.pos[2] < 113 && std::abs(v.pos[0]) < 601 && v.color[0] == 204;
    }));
    std::array<float, 3> wind{};
    CHECK(s.windVector(wind) && std::abs(wind[0] * wind[0] + wind[1] * wind[1] - 1) < 1e-4f && !s.windSpeed(speed));
    // A wind's box, and the gust's round the eye, shown by "debug wind".
    CHECK(s.command("snow wind ( 0 0 0 ) ( 10 0 0 ) ( 100 100 100 )").understood);
    CHECK(s.command("debug wind").understood);
    frame(s, 0.05f, {0, 0, 0}, batches);
    flakes = vertices(batches, WorldEffects::Look::kPlain);
    CHECK(std::count_if(flakes.begin(), flakes.end(), [](const gfx::Vertex &v) { return v.color[3] == 128; }) == 36);
    // The misty fog: its picture missing (as in every release), the
    // command ends the level; with one, a grid over the view.
    WorldEffects::Outcome o = s.command("snow fog");
    CHECK(o.error == "Could not load gfx/world/fog2");
    s.loadPicture = [](const std::string &name, WeatherPicture &out) {
        out.width = out.height = 4;
        out.rgba.assign(64, 255);
        return name == "gfx/world/fog2";
    };
    o = s.command("snow fog");
    CHECK(o.understood && o.error.empty() && s.command("snow fog density 0.5").understood);
    frame(s, 1, {0, 0, 0}, batches);
    const std::vector<gfx::Vertex> fog = vertices(batches, WorldEffects::Look::kFog);
    CHECK(fog.size() == 29 * 29 * 6 &&
          std::any_of(fog.begin(), fog.end(), [](const gfx::Vertex &v) { return v.color[3] > 0 && v.pos[2] == -10; }));
    CHECK(s.command("exec say hi").execute == " say hi" && w.command("exec say hi").execute.empty());
    CHECK(s.command("snow remove").understood && !s.snowing());
}

static const reluva::test::Suite
    kSuite("idtech3.unit.input", {
                                     {"console", [](const std::filesystem::path &) { testConsole(); }},
                                     {"console state", [](const std::filesystem::path &) { testConsoleState(); }},
                                     {"world effects", [](const std::filesystem::path &) { testWorldEffects(); }},
                                     {"random", [](const std::filesystem::path &) { testRandom(); }},
                                     {"joystick", [](const std::filesystem::path &) { testJoystick(); }},
                                     {"key bindings", [](const std::filesystem::path &) { testKeyBindings(); }},
                                     {"video settings", [](const std::filesystem::path &) { testVideoSettings(); }},
                                 });
