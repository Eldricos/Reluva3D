// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: Input and settings: the joystick, key bindings, video settings.

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

static void testJoystick() {
    // As the original engines read the joystick (eliteforce-input.md).
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
    event(EventType::JoyButtonDown, 0);
    CHECK(joy.frame(cvars).keys.empty()); // in_joystick 0: nothing
    cvars.set("in_joystick", "1");
    JoystickInput::Frame f = joy.frame(cvars);
    CHECK(f.keys.size() == 1 && f.keys[0] == std::make_pair(185, true)); // JOY1
    // x past the threshold to the left: the left arrow (joy_xbutton 1, the
    // default); y pushed forward all the way: analog, 127 (joy_ybutton 0).
    event(EventType::JoyAxis, 0, -20000);
    event(EventType::JoyAxis, 1, -32768);
    f = joy.frame(cvars);
    CHECK(f.keys.size() == 1 && f.keys[0] == std::make_pair(134, true) && f.x == 0 && f.y == 127);
    // Inside the threshold: let go; analog x when joy_xbutton is 0, right
    // negative; the hat's up is JOY24.
    event(EventType::JoyAxis, 0, 3000);
    f = joy.frame(cvars);
    CHECK(f.keys.size() == 1 && f.keys[0] == std::make_pair(134, false));
    cvars.set("joy_xbutton", "0");
    event(EventType::JoyAxis, 0, 32767);
    event(EventType::JoyHat, joyhat::Up);
    f = joy.frame(cvars);
    CHECK(f.x == -126 && f.keys.size() == 1 && f.keys[0] == std::make_pair(208, true));
    event(EventType::JoyHat, joyhat::Up | joyhat::Right); // no diagonals
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
    CHECK(KeyBindings::keyNumber("MOUSE2") == q3key::Mouse2 && KeyBindings::keyNumber("space") == q3key::Space);
    CHECK(KeyBindings::keyNumber("W") == 'w' && KeyBindings::keyNumber("F12") == q3key::F1 + 11);
    CHECK(KeyBindings::keyNumber("nonsense") == -1 && KeyBindings::keyNumber("0x22") == '"');
    CHECK(KeyBindings::keyName(q3key::WheelUp) == "MWHEELUP" && KeyBindings::keyName('w') == "w");
    CHECK(KeyBindings::keyName(';') == "SEMICOLON" && KeyBindings::keyName(q3key::F1 + 2) == "F3");

    reluva::idtech3::CvarStore cvars;
    KeyBindings b;
    int ignored = 0;
    auto readFile = [](const std::string& name, std::string& out) {
        if (name != "more.cfg")
            return false;
        out = "bind x \"say hi\"\n";
        return true;
    };
    b.execute("unbindall\nbind w +forward // a comment\nbind MOUSE2 \"+altattack\"\n"
              "bind 3 \"weapon 3; echo three\"\nseta sensitivity 7\nset cl_run 0; exec more\n"
              "unbind w\nbind TAB +scores\nconnect somewhere\n",
              cvars, &ignored, readFile);
    CHECK(b.get('w').empty() && b.get(q3key::Mouse2) == "+altattack" && b.get('3') == "weapon 3; echo three");
    CHECK(b.get('x') == "say hi" && b.get(q3key::Tab) == "+scores" && b.changed);
    CHECK(cvars.integer("sensitivity") == 7 && (cvars.find("sensitivity")->flags & cvars.Archive));
    CHECK(cvars.integer("cl_run") == 0 && ignored == 1);
    // Written and read back, the same.
    KeyBindings again;
    again.execute(b.write(), cvars);
    for (int32_t k = 0; k < q3key::Count; ++k)
        CHECK(again.get(k) == b.get(k));
    const auto parts = reluva::idtech3::splitCommands("a \"b;c\"; d\ne // f; g");
    CHECK(parts.size() == 3 && parts[0] == "a \"b;c\"" && reluva::idtech3::splitCommand(parts[0]).size() == 2);
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
    const int coarse = patchLevel(control, 3, 3, 4), fine = patchLevel(control, 3, 3, 0.5f);
    CHECK(coarse >= 2 && fine > coarse && fine <= 16);
}

static const reluva::test::Suite suite("idtech3.unit.input", {
    {"joystick", [](const std::filesystem::path&) { testJoystick(); }},
    {"key bindings", [](const std::filesystem::path&) { testKeyBindings(); }},
    {"video settings", [](const std::filesystem::path&) { testVideoSettings(); }},
});
