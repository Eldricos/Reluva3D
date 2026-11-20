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
    CHECK(toPatchedKey('w') == 'w' && toPatchedKey(q3key::kCharFlag | 'W') == (q3key::kCharFlag | 'W'));
    CHECK(toPatchedKey(185) == 256 && toPatchedKey(217) == 288 && toPatchedKey(q3key::kF1 + 12) == 0);
    for (int32_t k = 1; k < q3key::kCount; ++k)
        if (toPatchedKey(k) != 0)
            CHECK(fromPatchedKey(toPatchedKey(k)) == k);
    CHECK(fromPatchedKey(26) == 0); // CONSOLE: Quake III has no such key
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
    c.handle(keyDown(key::kPageUp), true);
    CHECK(c.scroll() == 4);
    c.print("more\n", false, 0);
    CHECK(c.scroll() == 5);
    CHECK(c.handle(keyDown(key::kEscape), true) == ConsoleState::Action::kClose);
    CHECK(c.handle(keyDown('`'), true) == ConsoleState::Action::kClose);
    CHECK(c.handle(keyDown('`'), false) == ConsoleState::Action::kNone);

    // Dropping down at 3 screens a second: half open in a sixth of one.
    CHECK(c.slide(1000, true, 3) == 0);
    CHECK(c.slide(1100, true, 3) > 0.29f && c.slide(1200, true, 3) == 0.5f);
    CHECK(c.slide(1400, false, 3) == 0);
}

static void testWorldEffects() {
    // The rain: started by its command, set by others, faded in only in
    // the open and out of water, its drops falling and starting again.
    WorldEffects w;
    CHECK(!w.command("rain alpha 0.3")); // no rain yet
    CHECK(!w.command("snow init 100"));
    CHECK(w.command("rain init 50") && w.raining());
    CHECK(w.command("rain alpha 0.3") && w.command("rain fall -100 -90") && !w.command("rain sideways"));
    std::vector<gfx::Vertex> tris;
    w.update(0.1f, 0);
    w.triangles({0, 0, 0}, {0, 1, 0}, tris);
    CHECK(tris.empty() && w.fade() == 0); // indoors: none
    w.update(1, WorldEffects::kContentsOutside | WorldEffects::kContentsWater);
    CHECK(w.fade() == 0);
    w.update(1, WorldEffects::kContentsOutside);
    CHECK(w.fade() == 0.5f);
    w.triangles({100, 200, 300}, {0, 1, 0}, tris);
    CHECK(tris.size() == 150);
    // Each a drop, across by a fifth of the left axis, then 5 down; round
    // the eye (within 20 across, 20 under to 40 over); no more opaque than
    // the alpha times the fade.
    bool near = true;
    bool faint = true;
    for (size_t i = 0; i < tris.size(); i += 3) {
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
        w.update(0.05f, WorldEffects::kContentsOutside);
    tris.clear();
    w.triangles({0, 0, 0}, {0, 1, 0}, tris);
    CHECK(tris.size() == 150 && w.fade() == 1);
    CHECK(std::all_of(tris.begin(), tris.end(), [](const gfx::Vertex &v) { return v.pos[2] > -26 && v.pos[2] < 41; }));
    CHECK(w.command("rain remove") && !w.raining());
}

static const reluva::test::Suite
    kSuite("idtech3.unit.input", {
                                     {"console", [](const std::filesystem::path &) { testConsole(); }},
                                     {"console state", [](const std::filesystem::path &) { testConsoleState(); }},
                                     {"world effects", [](const std::filesystem::path &) { testWorldEffects(); }},
                                     {"joystick", [](const std::filesystem::path &) { testJoystick(); }},
                                     {"key bindings", [](const std::filesystem::path &) { testKeyBindings(); }},
                                     {"video settings", [](const std::filesystem::path &) { testVideoSettings(); }},
                                 });
