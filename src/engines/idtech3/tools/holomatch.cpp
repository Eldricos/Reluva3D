// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's id Tech 3 commands running Holomatch's game code (Elite Force's
// multiplayer): the server's, the client game's and the menus'.

#include "tools/tool.h"
#include "engines/idtech3/tools/game_folder.h"

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/shader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;
using namespace reluva::idtech3::tool;

int cmdUiRun(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool ui-run <game folder> [-frames N] [-menu M] [-ingame gametype] [-size WxH] [-glwords N] [-trace] [-v]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    namespace ef = reluva::idtech3::eliteforce;
    int frames = 1, menu = 1, width = 640, height = 480, glwords = 5124;
    std::vector<std::pair<int, int>> clicks; // -click x y: in the menus' 640 x 480, one every 20 frames
    std::vector<int32_t> vmCalls, peeks;    // -vmcall instruction: a function run; -peek address: a word shown
    std::vector<std::string> shownCvars, latched; // -cvar name: shown at the end; -latch name: made a Latch variable first
    bool traceCalls = false, verbose = false;
    int ingameType = -1; // -ingame: the in-game menu, over a match of this game type
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "-frames") == 0 && i + 1 < argc)
            frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-menu") == 0 && i + 1 < argc)
            menu = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-size") == 0 && i + 1 < argc) {
            char* end = nullptr;
            width = int(std::strtol(argv[++i], &end, 10));
            height = *end ? int(std::strtol(end + 1, nullptr, 10)) : 0;
            if (width <= 0 || height <= 0) {
                std::fprintf(stderr, "-size takes WIDTHxHEIGHT, e.g. 1280x960\n");
                return 2;
            }
        }
        else if (std::strcmp(argv[i], "-glwords") == 0 && i + 1 < argc)
            glwords = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-trace") == 0)
            traceCalls = true;
        else if (std::strcmp(argv[i], "-v") == 0)
            verbose = true;
        else if (std::strcmp(argv[i], "-vmcall") == 0 && i + 1 < argc)
            vmCalls.push_back(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "-cvar") == 0 && i + 1 < argc)
            shownCvars.emplace_back(argv[++i]);
        else if (std::strcmp(argv[i], "-latch") == 0 && i + 1 < argc)
            latched.emplace_back(argv[++i]);
        else if (std::strcmp(argv[i], "-peek") == 0 && i + 1 < argc)
            peeks.push_back(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "-click") == 0 && i + 2 < argc) {
            clicks.emplace_back(std::atoi(argv[i + 1]), std::atoi(argv[i + 2]));
            i += 2;
        } else if (std::strcmp(argv[i], "-ingame") == 0 && i + 1 < argc)
            ingameType = std::atoi(argv[++i]);
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    q3::CvarStore cvars;
    // As after a match: the server game registered these as Latch
    // variables (g_gametype, the Holomatch modes...).
    for (const std::string& name : latched)
        cvars.create(name, "0", q3::CvarStore::ServerInfo | q3::CvarStore::Latch);
    ef::UiHost ui(set, cvars);
    ui.setModRoot(idTech3Folders(argv[2]).first, idTech3Folders(argv[2]).second);
    if (ingameType >= 0) {
        // As the Play page's in-game menu: connected to a match of that
        // game type, its server info the menus' configstring 0.
        ui.clientState.connState = 8;
        ui.clientState.serverName = "localhost";
        const std::string serverInfo = "\\g_gametype\\" + std::to_string(ingameType) +
                                       "\\mapname\\hm_borg1\\sv_hostname\\Reluva\\sv_maxclients\\8";
        ui.configstring = [serverInfo](int32_t index) { return index == 0 ? serverInfo : std::string(); };
        if (menu == 1)
            menu = 2;
    }
    ui.setScreenSize(width, height);
    ui.setGlconfigWords(glwords);
    ui.print = [](const std::string& text) { std::printf("ui: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : ""); };
    if (traceCalls)
        ui.trace = [](int number, const int32_t* a) {
            std::printf("  syscall %3d (%d, %d, %d, %d)\n", number, a[0], a[1], a[2], a[3]);
        };
    auto step = [&](const char* what, reluva::Error err) {
        std::printf("%s: %s after %llu instructions\n", what, err ? err.message.c_str() : "ok",
                    static_cast<unsigned long long>(ui.vm().executed()));
        return !err;
    };
    if (!step("load and GetApiVersion", ui.load()))
        return 1;
    bool ok = step("Init", ui.call(ef::UiHost::Init));
    if (ok)
        ok = step("SetActiveMenu", ui.call(ef::UiHost::SetActiveMenu, {menu}));
    std::printf("menu %d; key catcher %d (2: the menus take the keys)\n", menu, ui.catcher());
    for (const int32_t at : vmCalls) {
        int32_t result = 0;
        ok = ok && step("function", ui.vm().callAt(at, {}, result));
    }
    // The clicks: the pointer to the corner, then to the point (the
    // menus move it by the steps given, kept on the screen), and the left
    // button; each after 20 frames, the last followed by `frames` more.
    for (size_t c = 0; ok && c <= clicks.size(); ++c) {
        const int count = c < clicks.size() ? 20 : frames;
        for (int f = 0; ok && f < count; ++f) {
            ui.clearPictures();
            static int32_t time = 1000;
            time += 16;
            ok = step("Refresh", ui.refresh(time));
        }
        if (c < clicks.size() && ok) {
            ok = step("MouseEvent", ui.call(ef::UiHost::MouseEvent, {-100000, -100000})) &&
                 step("MouseEvent", ui.call(ef::UiHost::MouseEvent, {clicks[c].first * width / 640,
                                                                      clicks[c].second * height / 480})) &&
                 step("KeyEvent", ui.call(ef::UiHost::KeyEvent, {178, 1})); // K_MOUSE1
            std::printf("clicked %d %d\n", clicks[c].first, clicks[c].second);
        }
    }

    // What was registered, and whether the engine can find it.
    q3::ShaderLibrary shaders;
    shaders.load(set);
    int missing = 0;
    for (int32_t h = 1; h <= int32_t(ui.shaders().size()); ++h) {
        const std::string& name = ui.shaders().name(h);
        if (!shaders.find(name) && q3::findImage(set, name).empty()) {
            std::printf("shader not found: %s\n", name.c_str());
            ++missing;
        }
    }
    std::printf("%zu cvars, %zu shaders (%d not found), %zu models, %zu sounds registered\n", cvars.all().size(),
                ui.shaders().size(), missing, ui.models().size(), ui.sounds().size());
    std::printf("last frame: %zu pictures\n", ui.pictures().size());
    size_t shown = 0;
    for (const auto& p : ui.pictures()) {
        if (!verbose && ++shown > 40) {
            std::printf("  ... (-v lists them all)\n");
            break;
        }
        std::printf("  %7.1f %7.1f %7.1f x %-7.1f st %.3f %.3f %.3f %.3f  rgba %.2f %.2f %.2f %.2f  %s\n", p.x, p.y, p.w,
                    p.h, p.s1, p.t1, p.s2, p.t2, p.color[0], p.color[1], p.color[2], p.color[3],
                    ui.shaders().name(p.shader).c_str());
    }
    for (const std::string& c : ui.commands())
        std::printf("command: %s\n", c.c_str());
    for (const std::string& name : shownCvars)
        std::printf("cvar %s: \"%s\"%s\n", name.c_str(), cvars.string(name).c_str(),
                    cvars.hasLatched(name) ? (" (\"" + cvars.pending(name) + "\" at the next level)").c_str() : "");
    for (const int32_t at : peeks)
        std::printf("word at %d: %d (0x%x)\n", at, ui.vm().readInt(at), unsigned(ui.vm().readInt(at)));
    for (const auto& [number, count] : ui.unhandled())
        std::printf("not implemented: system call %d (%d calls)\n", number, count);
    return ok ? 0 : 1;
}

int cmdGameRun(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool game-run <game folder> <map> [-frames N] [-noclient] [-walk [forward|back|left|right]] [-set name value] [-addbot 'name skill']... [-cgame [-events] [-entities N] [-cgameframes N] [-fire] [-altfire] [-weapon N|all] [-give] [-watch model]] [-buttons] [-trace]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    namespace ef = reluva::idtech3::eliteforce;
    int frames = 10;
    bool traceCalls = false, noClient = false, walk = false, runCgame = false;
    bool probeButtons = false; // -buttons: what holding each button bit does
    // With -cgame: how many client game frames (-cgameframes), and the
    // player holding fire or alt fire (-fire, -altfire) with a weapon
    // (-weapon, the phaser by default) from the second.
    // (-weapon all: each of weapons 1 to 10 in turn, two seconds each.)
    // -give: cheats on and every weapon given first.
    int cgameFrames = 3, fireButtons = 0, fireWeapon = 1;
    bool giveAll = false;
    std::string watchModel; // -watch: print the scene's entities with this model frame by frame
    constexpr int kFramesPerWeapon = 40;
    int dumpEntities = 0; // -entities N: with -cgame, N of the scene's entities word by word
    std::vector<std::pair<std::string, std::string>> settings; // -set name value
    std::vector<std::string> addBots;                           // -addbot "name [skill]"
    bool eventsTest = false; // -events: with -cgame, the player's events against the sounds they bring
    int tapEvery = 0;        // -tap N: fire pressed for a frame every N
    std::string walkDirection = "forward";
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "-frames") == 0 && i + 1 < argc)
            frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-trace") == 0)
            traceCalls = true;
        else if (std::strcmp(argv[i], "-noclient") == 0)
            noClient = true;
        else if (std::strcmp(argv[i], "-cgame") == 0)
            runCgame = true;

        else if (std::strcmp(argv[i], "-tap") == 0 && i + 1 < argc)
            tapEvery = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-fire") == 0)
            fireButtons |= 1;
        else if (std::strcmp(argv[i], "-altfire") == 0)
            fireButtons |= 32;
        else if (std::strcmp(argv[i], "-weapon") == 0 && i + 1 < argc)
            fireWeapon = std::strcmp(argv[i + 1], "all") == 0 ? -1 : std::atoi(argv[i + 1]), ++i;
        else if (std::strcmp(argv[i], "-give") == 0)
            giveAll = true;
        else if (std::strcmp(argv[i], "-watch") == 0 && i + 1 < argc)
            watchModel = argv[++i];
        else if (std::strcmp(argv[i], "-cgameframes") == 0 && i + 1 < argc)
            cgameFrames = std::max(1, std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "-buttons") == 0)
            probeButtons = true;
        else if (std::strcmp(argv[i], "-entities") == 0 && i + 1 < argc)
            dumpEntities = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-walk") == 0) {
            walk = true;
            // An optional direction: forward (the default), back, left, right.
            if (i + 1 < argc && argv[i + 1][0] != '-')
                walkDirection = argv[++i];
        }
        else if (std::strcmp(argv[i], "-set") == 0 && i + 2 < argc) {
            settings.emplace_back(argv[i + 1], argv[i + 2]);
            i += 2;
        }
        else if (std::strcmp(argv[i], "-addbot") == 0 && i + 1 < argc)
            addBots.emplace_back(argv[++i]);
        else if (std::strcmp(argv[i], "-events") == 0)
            eventsTest = true;
        else
            std::fprintf(stderr, "game-run: ignoring %s (not an option it knows)\n", argv[i]);
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::string map = argv[3];
    if (map.size() > 4 && map.compare(map.size() - 4, 4, ".bsp") == 0)
        map.resize(map.size() - 4);
    if (map.rfind("maps/", 0) == 0)
        map = map.substr(5);
    Bytes data;
    auto bspOwned = std::make_shared<q3::Bsp>();
    q3::Bsp& bsp = *bspOwned;
    if (reluva::Error err = set.read("maps/" + map + ".bsp", data); err || (err = q3::parseBsp(data, bsp))) {
        std::fprintf(stderr, "maps/%s.bsp: %s\n", map.c_str(), err.message.c_str());
        return 1;
    }
    q3::CvarStore cvars;
    if (probeButtons || fireButtons || giveAll)
        cvars.set("g_holoIntro", "0"); // the player has to be in the game
    if (fireWeapon < 0 && fireButtons)
        cgameFrames = std::max(cgameFrames, 10 + 10 * kFramesPerWeapon);
    for (const auto& [name, value] : settings)
        cvars.set(name, value);
    ef::GameHost game(set, cvars);
    game.print = [](const std::string& text) {
        std::printf("game: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : "");
    };
    if (traceCalls)
        game.trace = [](int number, const int32_t* a) {
            std::printf("  syscall %3d (%d, %d, %d, %d)\n", number, a[0], a[1], a[2], a[3]);
        };
    auto step = [&](const char* what, reluva::Error err) {
        std::printf("%s: %s after %llu instructions\n", what, err ? err.message.c_str() : "ok",
                    static_cast<unsigned long long>(game.vm().executed()));
        return !err;
    };
    if (reluva::Error err = game.load()) {
        std::printf("load: %s\n", err.message.c_str());
        return 1;
    }
    game.reserveClient(0); // the player's, as the Play page keeps it
    reluva::Error started = game.startLevel(map, bspOwned);
    std::printf("start level (Init and 3 frames): %s\n", started ? started.message.c_str() : "ok");
    bool ok = !started;
    if (ok && !noClient) {
        // A player joins, as on a listen server: client 0.
        const std::string userinfo = "\\name\\Reluva\\model\\munro\\handicap\\100\\sex\\male\\color\\4"
                                     "\\snaps\\20\\rate\\25000\\ip\\localhost";
        std::string refused;
        reluva::Error err = game.connectClient(0, userinfo, refused);
        std::printf("client 0 connects: %s\n", err ? err.message.c_str() : refused.empty() ? "ok" : ("refused: " + refused).c_str());
        ok = !err;
        if (ok && refused.empty()) {
            err = game.beginClient(0);
            std::printf("client 0 begins: %s\n", err ? err.message.c_str() : "ok");
            ok = !err;
            if (ok && giveAll) {
                // The game's own cheat, once it has seen sv_cheats change.
                cvars.set("sv_cheats", "1", true);
                err = game.runFrame();
                if (!err)
                    err = game.clientCommand(0, "give all");
                std::printf("give all: %s\n", err ? err.message.c_str() : "ok");
                ok = !err;
            }
            const auto o = game.entityVec(0, game.layout().currentOrigin);
            std::printf("client 0's entity: %s at %.1f %.1f %.1f\n", game.linked().count(0) ? "linked" : "not linked",
                        o[0], o[1], o[2]);
        }
    }
    // Bots, added at the server's console as the original's addbot.
    for (const std::string& bot : addBots) {
        if (!ok)
            break;
        bool handled = false;
        reluva::Error err = game.serverCommand("addbot " + bot, &handled);
        std::printf("addbot %s: %s\n", bot.c_str(),
                    err ? err.message.c_str() : handled ? "ok" : "the game didn't take the command");
        ok = !err;
    }
    std::map<int32_t, q3::Vec3f> botStart;
    for (const int32_t c : game.botClients())
        botStart[c] = game.entityVec(c, game.layout().currentOrigin);
    const bool player = ok && !noClient && game.linked().count(0);
    const auto startPos = player ? game.entityVec(0, game.layout().currentOrigin) : q3::Vec3f{};
    int restartsSeen = game.restarts();
    for (int f = 0; ok && f < frames; ++f) {
        if (player) {
            // The player does nothing, as a server would see from an idle
            // client: an empty command for the coming frame; with -walk,
            // runs forward (from the third frame, once standing).
            ef::GameHost::UserCommand cmd;
            cmd.serverTime = game.levelTime() + ef::GameHost::kFrameMs;
            if (walk && f >= 2) {
                if (walkDirection == "back")
                    cmd.forward = -127;
                else if (walkDirection == "left")
                    cmd.right = -127;
                else if (walkDirection == "right")
                    cmd.right = 127;
                else
                    cmd.forward = 127;
            }
            // -tap N: fire pressed for a frame every N (the end of a match
            // waits for a press).
            if (tapEvery > 0 && f % tapEvery == 0)
                cmd.buttons = 1;
            if (reluva::Error err = game.clientThink(0, cmd)) {
                step("ClientThink", err);
                ok = false;
                break;
            }
        }
        ok = step("RunFrame", game.runFrame());
        // A level the game asked for (the end of a match: vstr nextmap),
        // reported once.
        if (ok && game.restarts() != restartsSeen) {
            restartsSeen = game.restarts();
            std::printf("frame %d (level time %d): the level restarted (map_restart)\n", f, game.levelTime());
        }
        if (ok && game.levelRequested()) {
            std::printf("frame %d (level time %d): the game asked for level \"%s\"\n", f, game.levelTime(),
                        game.takeRequestedMap().c_str());
            break;
        }
        if (player) {
            // The entity's position, and the player state as Quake III lays
            // it out (commandTime 0, origin 20, velocity 32, groundEntityNum
            // 68), to be checked against Elite Force's.
            const auto o = game.entityVec(0, game.layout().currentOrigin);
            const auto po = game.clientVec(0, 20), pv = game.clientVec(0, 32);
            std::printf("  client 0 at %.1f %.1f %.1f; player state: commandTime %d, origin %.1f %.1f %.1f, "
                        "velocity %.1f %.1f %.1f, ground %d; commands read so far %d\n",
                        o[0], o[1], o[2], game.clientInt(0, 0), po[0], po[1], po[2], pv[0], pv[1], pv[2],
                        game.clientInt(0, 68), (game.callCounts().count(36) ? game.callCounts().at(36) : 0));
            // Quake III's pm_type (4), pm_flags (12), pm_time (16); and the
            // game's copy of the last command (after the player state and
            // pers.connected: client + 464 in 1.0, 472 in 1.2): its time and
            // its weapon (5) and moves (20-22).
            const int32_t cmd = game.layout().playerStateSize + 4;
            const int32_t cmdTime = game.clientInt(0, cmd), cmdMoves = game.clientInt(0, cmd + 20),
                          cmdWeapon = (game.clientInt(0, cmd + 4) >> 8) & 0xff;
            std::printf("    pm_type %d, pm_flags 0x%x, pm_time %d; the game's command: time %d, weapon %d, "
                        "forward %d, right %d, up %d\n",
                        game.clientInt(0, 4), unsigned(game.clientInt(0, 12)), game.clientInt(0, 16), cmdTime,
                        cmdWeapon, int8_t(cmdMoves & 0xff), int8_t((cmdMoves >> 8) & 0xff),
                        int8_t((cmdMoves >> 16) & 0xff));
        }
    }

    if (player && probeButtons && ok) {
        // Each button bit held for a second with the phaser (weapon 1),
        // then let go for a second: which words of the player state it
        // changes, leaving out those that change when nothing is held.
        // Firing shows in its weapon time and ammunition; alt fire should
        // look like firing.
        auto state = [&] {
            std::vector<int32_t> words(ef::GameHost::kPlayerStateSize / 4);
            for (size_t i = 0; i < words.size(); ++i)
                words[i] = game.clientInt(0, int32_t(i * 4));
            return words;
        };
        auto run = [&](int32_t buttons, int frames, std::set<size_t>& changed) {
            for (int f = 0; f < frames && ok; ++f) {
                const auto before = state();
                ef::GameHost::UserCommand cmd;
                cmd.serverTime = game.levelTime() + ef::GameHost::kFrameMs;
                cmd.buttons = buttons;
                cmd.weapon = 1;
                if (reluva::Error err = game.clientThink(0, cmd); err || (err = game.runFrame())) {
                    step("probe", err);
                    ok = false;
                }
                const auto after = state();
                for (size_t i = 1; i < after.size(); ++i) // not the command time
                    if (after[i] != before[i])
                        changed.insert(i * 4);
            }
        };
        std::set<size_t> idle, ignored;
        const int second = 1000 / ef::GameHost::kFrameMs;
        run(0, second, idle);
        for (int bit = 0; bit < 8 && ok; ++bit) { // the buttons are a byte
            std::set<size_t> changed;
            run(1 << bit, second, changed);
            run(0, second, ignored);
            std::string list;
            for (size_t at : changed)
                if (!idle.count(at))
                    list += " " + std::to_string(at);
            std::printf("button bit %2d (%4d): player state words changed:%s\n", bit, 1 << bit,
                        list.empty() ? " none" : list.c_str());
        }
    }
    if (player) {
        const auto o = game.entityVec(0, game.layout().currentOrigin);
        std::printf("client 0 moved %.1f units across and %.1f up over the frames\n",
                    std::sqrt((o[0] - startPos[0]) * (o[0] - startPos[0]) + (o[1] - startPos[1]) * (o[1] - startPos[1])),
                    o[2] - startPos[2]);
    }
    if (player) {
        // What the game's movement code meets: its trace down from where
        // the player is, with a player's box and mask, ignoring the player.
        const auto o = game.entityVec(0, game.layout().currentOrigin);
        const q3::Vec3f mins{-15, -15, -24}, maxs{15, 15, 32}, end{o[0], o[1], o[2] - 64};
        const int32_t mask = q3::contents::Solid | q3::contents::PlayerClip | q3::contents::Body;
        // And sideways: how far a player's box could go along each axis.
        for (int axis = 0; axis < 2; ++axis)
            for (float sign : {1.0f, -1.0f}) {
                q3::Vec3f to = o;
                to[size_t(axis)] += sign * 512;
                to[2] += 1; // just off the floor
                q3::Vec3f from = o;
                from[2] += 1;
                int32_t hit = 0;
                const q3::TraceResult t = game.traceMove(from, mins, maxs, to, 0, mask, hit);
                std::printf("trace %s%c from client 0: %.1f units free, hits %d%s\n", sign > 0 ? "+" : "-",
                            "xy"[axis], t.fraction * 512, hit, t.startSolid ? " (starts inside)" : "");
            }
        for (bool withEntities : {true, false}) {
            int32_t hit = 0;
            const q3::TraceResult t = game.traceMove(o, mins, maxs, end, 0, mask, hit, withEntities);
            std::printf("trace down from client 0 (%s): fraction %.3f, startsolid %d, allsolid %d, hits %d, lands at %.1f\n",
                        withEntities ? "world and entities" : "world only", t.fraction, t.startSolid, t.allSolid, hit,
                        t.endPos[2]);
            if (withEntities && hit >= 0 && hit < 1022) {
                const auto eo = game.entityVec(hit, game.layout().currentOrigin), emin = game.entityVec(hit, game.layout().mins), emax = game.entityVec(hit, game.layout().maxs);
                std::printf("  entity %d: origin %.1f %.1f %.1f, box %.1f %.1f %.1f to %.1f %.1f %.1f, contents %d, "
                            "brush model %d (model %d), owner %d\n",
                            hit, eo[0], eo[1], eo[2], emin[0], emin[1], emin[2], emax[0], emax[1], emax[2],
                            game.entityInt(hit, game.layout().contents), game.entityInt(hit, game.layout().bmodel),
                            game.entityInt(hit, game.layout().modelIndex), game.entityInt(hit, game.layout().ownerNum));
            }
        }
    }
    const auto& gd = game.gameData();
    std::printf("level time %d ms; entity array at %d: %d in use, %d bytes each; clients at %d, %d bytes each\n",
                game.levelTime(), gd.entities, gd.count, gd.entitySize, gd.clients, gd.clientSize);
    {
        // The bots in the game (a solo match's are added by the game itself).
        std::string botList;
        for (const int32_t c : game.botClients())
            botList += " " + std::to_string(c);
        std::printf("bots: %zu%s%s; g_gametype %s\n", game.botClients().size(), botList.empty() ? "" : " (clients",
                    botList.empty() ? "" : (botList + ")").c_str(), cvars.string("g_gametype").c_str());
    }
    std::printf("%zu entities linked into the world, %zu configstrings set, %zu cvars\n", game.linked().size(),
                game.configstrings().size(), cvars.all().size());
    for (const auto& [index, text] : game.configstrings()) {
        std::string shown = text.size() > 100 ? text.substr(0, 100) + "..." : text;
        std::printf("  configstring %4d: %s\n", index, shown.c_str());
    }
    for (const std::string& c : game.consoleCommands())
        std::printf("console command: %s\n", c.c_str());
    if (!addBots.empty() || !game.botClients().empty()) {
        // The bots: where each started and is now, and the bot library's
        // calls the game made, with the first arguments of the first few.
        std::printf("bots: %zu\n", game.botClients().size());
        for (const int32_t c : game.botClients()) {
            const auto o = game.entityVec(c, game.layout().currentOrigin);
            const auto s = botStart.count(c) ? botStart[c] : q3::Vec3f{};
            std::printf("  client %d: %s, at %.1f %.1f %.1f (was %.1f %.1f %.1f)\n", c,
                        game.linked().count(c) ? "linked" : "not linked", o[0], o[1], o[2], s[0], s[1], s[2]);
        }
        const auto& aas = game.botLibrary().aas();
        std::printf("aas: %s\n", aas.loaded() ? (std::to_string(aas.areaCount()) + " areas").c_str() : "not loaded");
        const std::map<int, int> counts = game.callCounts();
        for (const auto& [number, samples] : game.botCallSamples()) {
            const auto counted = counts.find(number);
            std::printf("  bot call %d: %d calls", number, counted == counts.end() ? 0 : counted->second);
            for (const auto& args : samples) {
                std::printf(" (");
                for (size_t i = 0; i < args.size(); ++i)
                    std::printf("%s%d", i ? " " : "", args[i]);
                std::printf(")");
            }
            std::printf("\n");
        }
    }
    for (const auto& [client, text] : game.serverCommands()) {
        std::string shown = text.size() > 120 ? text.substr(0, 120) + "..." : text;
        while (!shown.empty() && shown.back() == '\n')
            shown.pop_back();
        std::printf("to client %d: %s\n", client, shown.c_str());
    }
    for (const auto& [client, reason] : game.dropped())
        std::printf("client %d dropped: %s\n", client, reason.c_str());

    // A check of the collision model on its own: a player's box dropped
    // from each spawn point (raised 9 units, as Quake III's game puts
    // players there, since spawn points often sit exactly a player's
    // height above the floor) should land a little below it, not start
    // in a wall.
    std::vector<q3::Entity> ents;
    if (!q3::parseEntities(bsp.entities, ents)) {
        const q3::CollisionWorld world(bsp);
        const q3::Vec3f mins{-15, -15, -24}, maxs{15, 15, 32};
        int spawns = 0, stuck = 0, falling = 0;
        float shortest = 1e9f, longest = 0;
        for (const q3::Entity& e : ents) {
            float o[3];
            if (e.classname().rfind("info_player", 0) != 0 || !e.vector("origin", o))
                continue;
            ++spawns;
            const q3::Vec3f start{o[0], o[1], o[2] + 9}, end{o[0], o[1], o[2] - 4096};
            const q3::TraceResult t =
                world.trace(start, end, mins, maxs, 0, q3::contents::Solid | q3::contents::PlayerClip);
            if (t.startSolid)
                ++stuck;
            else if (t.fraction == 1)
                ++falling;
            else {
                const float drop = start[2] - t.endPos[2];
                shortest = std::min(shortest, drop);
                longest = std::max(longest, drop);
            }
        }
        if (spawns)
            std::printf("collision check: %d spawn points; a player's box dropped from each lands %.1f to %.1f "
                        "units below; %d start inside something, %d fall out of the map\n",
                        spawns, spawns > stuck + falling ? shortest : 0.0f, longest, stuck, falling);
    }
    for (const auto& [number, count] : game.unhandled())
        std::printf("not implemented: system call %d (%d calls)\n", number, count);

    if (ok && runCgame) {
        // The player's client game, started with what the server has set.
        ef::CgameHost cgame(set, cvars);
        cgame.print = [](const std::string& text) {
            std::printf("cgame: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : "");
        };
        // The client game's calls, counted by frame phase: idle, then
        // (with -fire or -altfire) holding fire; the first arguments of each.
        int phase = 0; // 0 start-up, 1 idle frames, 2 firing frames
        std::array<std::map<int, int>, 3> phaseCalls;
        std::map<int, std::array<int32_t, 4>> firingArgs;
        std::map<std::string, int> soundsStarted; // -events: S_StartSound by name
        cgame.trace = [&](int number, const int32_t* a) {
            if (eventsTest && number == 28)
                ++soundsStarted[cgame.sounds().name(a[3])];
            ++phaseCalls[size_t(phase)][number];
            if (phase == 2 && !firingArgs.count(number))
                firingArgs[number] = {a[0], a[1], a[2], a[3]};
            if (traceCalls)
                std::printf("  cgame syscall %3d (%d, %d, %d, %d)\n", number, a[0], a[1], a[2], a[3]);
        };
        reluva::Error err = cgame.load();
        if (!err) {
            cgame.setConfigstrings(game.configstrings());
            err = cgame.init(0);
        }
        std::printf("cgame Init: %s after %llu instructions\n", err ? err.message.c_str() : "ok",
                    static_cast<unsigned long long>(cgame.vm().executed()));
        std::printf("cgame loaded %s for collision, %s to draw; music %s / %s\n", cgame.collisionMap().c_str(),
                    cgame.worldMap().c_str(), cgame.music().first.c_str(), cgame.music().second.c_str());
        std::printf("cgame registered %zu models, %zu skins, %zu shaders, %zu sounds; added %zu commands\n",
                    cgame.models().size(), cgame.skins().size(), cgame.shaders().size(), cgame.sounds().size(),
                    cgame.commands().size());
        if (!err && eventsTest) {
            // Fire held for three seconds at a time as the Play page runs it
            // (a command each frame, the server a frame ahead, a snapshot
            // each server frame, the client game drawing every frame), at
            // frame lengths from 7 ms to 80 ms: the events the server gave
            // the player (its event sequence, player state word 108) against
            // the sounds the client game started.
            // serverFirst: the server run up to the command's time before it's
            // given the command (as the original's client stamps commands
            // with the snapshots' time), not after.
            auto runFor = [&](int stepMs, bool serverFirst) {
                soundsStarted.clear();
                const int32_t firstEvent = game.clientInt(0, 108);
                const int32_t firstAmmo = game.clientInt(0, 396);
                const int32_t firstCommandTime = game.clientInt(0, 0);
                int32_t clientTime = game.levelTime();
                auto runServer = [&] {
                    while (game.levelTime() <= clientTime) {
                        if (reluva::Error e = game.runFrame()) {
                            std::printf("server: %s\n", e.message.c_str());
                            return false;
                        }
                        std::vector<std::string> commands;
                        for (const auto& [to, text] : game.serverCommands())
                            if (to == 0 || to == -1)
                                commands.push_back(text);
                        cgame.setServerCommands(commands);
                        cgame.setConfigstrings(game.configstrings());
                        cgame.newSnapshot(game.snapshot(0));
                    }
                    return true;
                };
                for (int t = 0; t < 3000 && ok; t += stepMs) {
                    clientTime += stepMs;
                    if (serverFirst && !runServer())
                        return;
                    ef::GameHost::UserCommand cmd;
                    cmd.serverTime = clientTime;
                    cmd.buttons = fireButtons ? fireButtons : 1;
                    cmd.weapon = uint8_t(std::max(fireWeapon, 1));
                    if (reluva::Error e = game.clientThink(0, cmd)) {
                        std::printf("server: %s\n", e.message.c_str());
                        return;
                    }
                    cgame.addUserCommand(cmd);
                    if (!serverFirst && !runServer())
                        return;
                    cgame.clearScene();
                    cgame.clearPictures();
                    int32_t ignored = 0;
                    if (reluva::Error e = cgame.call(ef::CgameHost::DrawActiveFrame, {clientTime, 0, 0}, ignored)) {
                        std::printf("cgame: %s\n", e.message.c_str());
                        return;
                    }
                }
                std::printf("a command every %d ms, %s: the server gave the player %d events (command time %d to %d, "
                            "ammunition word 396: %d to %d); the client game started these sounds:\n",
                            stepMs, serverFirst ? "the server run first" : "the command first",
                            game.clientInt(0, 108) - firstEvent, firstCommandTime, game.clientInt(0, 0), firstAmmo,
                            game.clientInt(0, 396));
                for (const auto& [name, count] : soundsStarted)
                    std::printf("  %4d  %s\n", count, name.c_str());
            };
            // From 144 frames a second down to 12, as a slow frame rate gives,
            // each both ways; 7 ms again at the end, as each run starts where
            // the last stopped.
            for (const auto& [stepMs, serverFirst] : std::vector<std::pair<int, bool>>{
                     {7, false}, {16, false}, {16, true}, {50, false}, {50, true}, {80, true}, {7, false}})
                runFor(stepMs, serverFirst);
        } else if (!err) {
            // A few frames, a new snapshot each, to see what the client game
            // does with them.
            const auto [at208, at212] = game.svFlagsEvidence();
            std::printf("svFlags: %d linked entities have flags at 208, %d at 212; taken to be at %d\n", at208, at212,
                        game.svFlagsOffset());
            // What the frames add besides models, for working out how the
            // client game draws effects: entities by type, polygons by
            // shader, lights; a sample of each.
            // The entities of the last idle frame and the last firing one,
            // to dump those firing adds.
            std::vector<std::vector<uint8_t>> lastIdleEntities, lastFiringEntities;
            // By the weapon fired (0 when not firing) and the type or shader.
            using Key = std::pair<int, int32_t>;
            std::map<Key, int> typeCounts;
            std::map<Key, std::vector<uint8_t>> typeSamples;
            std::map<Key, int> polyCounts;
            std::map<Key, ef::CgameHost::Scene::Poly> polySamples;
            std::map<int, int> lightCounts;
            // Models (handle, custom shader) seen while firing each weapon
            // that never showed when not firing: projectiles and the like.
            std::map<Key, std::set<std::pair<int32_t, int32_t>>> firedModels;
            std::set<std::pair<int32_t, int32_t>> idleModels;
            std::map<int, ef::CgameHost::Scene::Light> lightSamples;
            int weaponNow = 0;
            // With fire held, the first 10 frames are idle, to compare.
            constexpr int kIdleFrames = 10;
            int idleFrames = 0, firingFrames = 0;
            for (int f = 0; f < cgameFrames; ++f) {
                const bool verbose = f < 3 || f == cgameFrames - 1;
                const bool firing = fireButtons && f >= kIdleFrames;
                if (f) {
                    phase = firing ? 2 : 1;
                    (firing ? firingFrames : idleFrames)++;
                }
                ef::GameHost::UserCommand idle;
                if (f) {
                    idle.serverTime = game.levelTime() + ef::GameHost::kFrameMs;
                    weaponNow = !firing          ? 0
                                : fireWeapon < 0 ? 1 + std::min(9, (f - kIdleFrames) / kFramesPerWeapon)
                                                 : fireWeapon;
                    idle.weapon = uint8_t(fireButtons ? (weaponNow ? weaponNow : std::max(fireWeapon, 1)) : 0);
                    idle.buttons = firing ? fireButtons : 0;
                    if (fireButtons)
                        game.clientThink(0, idle);
                    game.runFrame();
                }
                if (fireButtons && (f == 0 || f == kIdleFrames)) {
                    // The player state's start, word by word: the client game
                    // won't draw the phaser's beam unless the word at 48 is 0.
                    std::printf("frame %2d: player state words 0-96:", f);
                    for (int at = 0; at <= 96; at += 4)
                        std::printf(" %d:%d", at, game.clientInt(0, at));
                    std::printf("\n");
                }
                if (fireButtons && f <= 30 && (f % 5 == 0 || f == kIdleFrames)) {
                    // The player state words firing changes (game-run -buttons):
                    // weapon time 44, flags 104, event sequence 108, events 112,
                    // 116, their parameters 120, 124, and 376, 396.
                    std::printf("frame %2d%s: player state 44 %d, 104 0x%x, 108 %d, 112 %d, 116 %d, 120 %d, 124 %d, "
                                "376 %d, 396 %d\n",
                                f, firing ? " (firing)" : "", game.clientInt(0, 44), unsigned(game.clientInt(0, 104)),
                                game.clientInt(0, 108), game.clientInt(0, 112), game.clientInt(0, 116),
                                game.clientInt(0, 120), game.clientInt(0, 124), game.clientInt(0, 376),
                                game.clientInt(0, 396));
                }
                std::vector<std::string> commands;
                for (const auto& [to, text] : game.serverCommands())
                    if (to == 0 || to == -1)
                        commands.push_back(text);
                cgame.setServerCommands(commands);
                idle.serverTime = game.levelTime();
                cgame.addUserCommand(idle);
                cgame.clearScene();
                auto snap = game.snapshot(0);
                std::string sentBots; // the bots it carries (entityState_t starts with the number)
                for (const auto& es : snap.entities)
                    if (es.size() >= 4) {
                        int32_t number;
                        std::memcpy(&number, es.data(), 4);
                        if (game.botClients().count(number))
                            sentBots += " " + std::to_string(number);
                    }
                if (verbose)
                    std::printf("snapshot at %d: %zu entities, %d server commands\n", snap.serverTime,
                                snap.entities.size(), snap.serverCommandSequence);
                cgame.newSnapshot(std::move(snap));
                int32_t ignored = 0;
                const reluva::Error frame =
                    cgame.call(ef::CgameHost::DrawActiveFrame, {game.levelTime(), 0, 0}, ignored);
                if (verbose || frame)
                    std::printf("cgame DrawActiveFrame(%d): %s after %llu instructions, %zu pictures\n", game.levelTime(),
                            frame ? frame.message.c_str() : "ok",
                            static_cast<unsigned long long>(cgame.vm().executed()), cgame.pictures().size());
                if (!watchModel.empty()) {
                    // Which bots the snapshot carries, and how many entities
                    // with the watched model the client game added (a bot
                    // that's sent but not drawn shows here).
                    const std::string& sent = sentBots;
                    int added = 0;
                    for (const auto& e : cgame.scene().entities)
                        if (e.bytes.size() >= 12 && e.bytes[0] == 0) {
                            int32_t model;
                            std::memcpy(&model, &e.bytes[8], 4);
                            added += cgame.models().name(model).find(watchModel) != std::string::npos;
                        }
                    std::printf("time %d: bots in the snapshot:%s; %d entities with %s added\n", game.levelTime(),
                                sent.empty() ? " none" : sent.c_str(), added, watchModel.c_str());
                    // Entities with the watched model: colour, scale (axis
                    // lengths), frames and where.
                    for (const auto& e : cgame.scene().entities) {
                        if (e.bytes.size() < 160)
                            continue;
                        auto word = [&](size_t at) {
                            int32_t v;
                            std::memcpy(&v, &e.bytes[at], 4);
                            return v;
                        };
                        auto real = [&](size_t at) { return q3::Qvm::asFloat(word(at)); };
                        const std::string name = cgame.models().name(word(8));
                        if (word(0) != 0 || name.find(watchModel) == std::string::npos)
                            continue;
                        float len[3];
                        for (int a = 0; a < 3; ++a)
                            len[a] = std::sqrt(real(28 + 12 * size_t(a)) * real(28 + 12 * size_t(a)) +
                                               real(32 + 12 * size_t(a)) * real(32 + 12 * size_t(a)) +
                                               real(36 + 12 * size_t(a)) * real(36 + 12 * size_t(a)));
                        std::printf("  time %d: %s colour %u %u %u %u, axes %.2f %.2f %.2f%s, frame %d (old %d, "
                                    "back %.2f), shader %d%s, shader time %.3f, flags 0x%x, at %.0f %.0f %.0f\n",
                                    game.levelTime(), name.c_str(), e.bytes[116], e.bytes[117], e.bytes[118],
                                    e.bytes[119],
                                    len[0], len[1], len[2], word(64) ? " (not normalized)" : "", word(80), word(96),
                                    real(100), word(112),
                                    word(112) ? (" (" + cgame.shaders().name(word(112)) + ")").c_str() : "",
                                    real(128), unsigned(word(4)), real(68), real(72), real(76));
                    }
                }
                {
                    const auto& scene = cgame.scene();
                    auto& keep = firing ? lastFiringEntities : lastIdleEntities;
                    if (f) {
                        keep.clear();
                        for (const auto& e : scene.entities)
                            keep.push_back(e.bytes);
                    }
                    for (const auto& e : scene.entities) {
                        int32_t type = 0;
                        if (e.bytes.size() >= 4)
                            std::memcpy(&type, e.bytes.data(), 4);
                        if (type != 0 && typeCounts[{weaponNow, type}]++ == 0)
                            typeSamples[{weaponNow, type}] = e.bytes;
                        if (type == 0 && e.bytes.size() >= 116) {
                            int32_t model = 0, shader = 0;
                            std::memcpy(&model, &e.bytes[8], 4);
                            std::memcpy(&shader, &e.bytes[112], 4);
                            if (!weaponNow)
                                idleModels.insert({model, shader});
                            else if (!idleModels.count({model, shader}))
                                firedModels[{weaponNow, 0}].insert({model, shader});
                        }
                    }
                    for (const auto& poly : scene.polys)
                        if (polyCounts[{weaponNow, poly.shader}]++ == 0)
                            polySamples[{weaponNow, poly.shader}] = poly;
                    if (!scene.lights.empty() && !lightCounts[weaponNow])
                        lightSamples[weaponNow] = scene.lights[0];
                    lightCounts[weaponNow] += int(scene.lights.size());
                }
                if (verbose) {
                    // The frame's 3D scene: the entities added (and the sizes of
                    // the memsets that cleared them: refEntity_t's size), and the
                    // view, read as Quake III's refdef_t: x, y, width, height,
                    // fov x, fov y, view origin (24), axes (36), time (72).
                    const auto& scene = cgame.scene();
                    std::map<int32_t, int> sizes;
                    for (const auto& e : scene.entities)
                        ++sizes[e.clearedSize];
                    std::string sizeText;
                    for (const auto& [size, count] : sizes)
                        sizeText += " " + std::to_string(count) + " cleared by " + std::to_string(size);
                    std::printf("  scene: %zu entities (%s), %d polygons, %d lights, rendered %d times\n",
                                scene.entities.size(), sizeText.c_str(), int(scene.polys.size()), int(scene.lights.size()), scene.renders);
                    if (scene.view.size() >= 80) {
                        auto word = [&](int at) {
                            int32_t v;
                            std::memcpy(&v, &scene.view[size_t(at)], 4);
                            return v;
                        };
                        auto real = [&](int at) { return q3::Qvm::asFloat(word(at)); };
                        std::printf("  view: %d,%d %dx%d, fov %.1f x %.1f, eye %.1f %.1f %.1f, forward %.2f %.2f %.2f, time %d\n",
                                    word(0), word(4), word(8), word(12), real(16), real(20), real(24), real(28),
                                    real(32), real(36), real(40), real(44), word(72));
                    }
                }
                if (f == 0 && dumpEntities) {
                    // A few of the scene's entities word by word, for working
                    // out refEntity_t's layout: different models first. Each
                    // word as a whole number and as a float, and what it
                    // names if it's a registered model, skin or shader handle.
                    const auto& scene = cgame.scene();
                    std::vector<size_t> picked;
                    std::set<int32_t> seen;
                    for (int pass = 0; pass < 2; ++pass)
                        for (size_t i = 0; i < scene.entities.size() && picked.size() < size_t(dumpEntities); ++i) {
                            const auto& b = scene.entities[i].bytes;
                            if (b.size() < 160 || std::find(picked.begin(), picked.end(), i) != picked.end())
                                continue;
                            int32_t key = 0;
                            std::memcpy(&key, &b[8], 4);
                            if (pass == 0 && !seen.insert(key).second)
                                continue;
                            picked.push_back(i);
                        }
                    for (size_t i : picked) {
                        const auto& b = scene.entities[i].bytes;
                        std::printf("  scene entity %zu (cleared by %d):\n", i, scene.entities[i].clearedSize);
                        for (int at = 0; at < 160; at += 4) {
                            int32_t v;
                            std::memcpy(&v, &b[size_t(at)], 4);
                            const float fl = q3::Qvm::asFloat(v);
                            std::string names;
                            if (v > 0 && v < 4096) {
                                if (!cgame.models().name(v).empty())
                                    names += " model:" + cgame.models().name(v);
                                if (!cgame.skins().name(v).empty())
                                    names += " skin:" + cgame.skins().name(v);
                                if (!cgame.shaders().name(v).empty())
                                    names += " shader:" + cgame.shaders().name(v);
                            }
                            const bool showFloat = std::isfinite(fl) && std::fabs(fl) > 1e-6f && std::fabs(fl) < 1e7f;
                            char floatText[32] = "";
                            if (showFloat)
                                std::snprintf(floatText, sizeof floatText, " %.4g", fl);
                            std::printf("    %3d: %11d 0x%08x%s%s\n", at, v, unsigned(v), floatText, names.c_str());
                        }
                    }
                }
                if (f == 0) {
                    // What the frame drew in 2D, by shader.
                    std::map<std::string, int> byShader;
                    for (const auto& p : cgame.pictures())
                        ++byShader[cgame.shaders().name(p.shader)];
                    for (const auto& [name, count] : byShader)
                        std::printf("  %4d x %s\n", count, name.empty() ? "(none)" : name.c_str());
                }
                cgame.clearPictures();
                if (frame)
                    break;
            }
            if (fireButtons && idleFrames && firingFrames) {
                // Calls a frame, idle and firing: what changes when firing.
                std::printf("client game calls a frame, idle (%d frames) against firing (%d frames):\n", idleFrames,
                            firingFrames);
                std::set<int> numbers;
                for (int ph = 1; ph <= 2; ++ph)
                    for (const auto& [number, count] : phaseCalls[size_t(ph)])
                        numbers.insert(number);
                for (int number : numbers) {
                    const float idleRate = float(phaseCalls[1][number]) / float(idleFrames);
                    const float fireRate = float(phaseCalls[2][number]) / float(firingFrames);
                    const auto it = firingArgs.find(number);
                    std::printf("  call %3d: %7.2f idle, %7.2f firing%s", number, idleRate, fireRate,
                                std::fabs(idleRate - fireRate) > 0.05f ? "  <- differs" : "");
                    if (it != firingArgs.end())
                        std::printf("  (first while firing: %d, %d, %d, %d)", it->second[0], it->second[1],
                                    it->second[2], it->second[3]);
                    std::printf("\n");
                }
            }
            // The effects seen over the frames.
            auto dumpWords = [&](const std::vector<uint8_t>& b, size_t size) {
                for (size_t at = 0; at + 4 <= std::min(size, b.size()); at += 4) {
                    int32_t v;
                    std::memcpy(&v, &b[at], 4);
                    const float fl = q3::Qvm::asFloat(v);
                    std::string names;
                    if (v > 0 && v < 4096) {
                        if (!cgame.models().name(v).empty())
                            names += " model:" + cgame.models().name(v);
                        if (!cgame.shaders().name(v).empty())
                            names += " shader:" + cgame.shaders().name(v);
                    }
                    char floatText[32] = "";
                    if (std::isfinite(fl) && std::fabs(fl) > 1e-6f && std::fabs(fl) < 1e7f)
                        std::snprintf(floatText, sizeof floatText, " %.4g", fl);
                    std::printf("    %3zu: %11d 0x%08x%s%s\n", at, v, unsigned(v), floatText, names.c_str());
                }
            };
            if (fireButtons && !lastIdleEntities.empty() && !lastFiringEntities.empty()) {
                // Firing's entities: those of the last firing frame with a model
                // handle (byte 8) more often than in the last idle frame.
                std::map<int32_t, int> idleModels;
                for (const auto& b : lastIdleEntities) {
                    int32_t m = 0;
                    std::memcpy(&m, &b[8], 4);
                    ++idleModels[m];
                }
                int shown = 0;
                for (const auto& b : lastFiringEntities) {
                    int32_t m = 0;
                    std::memcpy(&m, &b[8], 4);
                    if (idleModels[m]-- > 0 || shown >= 4)
                        continue;
                    std::printf("an entity firing adds (model %d%s%s):\n", m, m > 0 ? " " : "",
                                m > 0 ? cgame.models().name(m).c_str() : "");
                    dumpWords(b, 160);
                    ++shown;
                }
            }
            // Types the Play page draws: models (0), quads (1), sprites (2),
            // lines (9, 11); samples of the others.
            auto drawn = [](int32_t type) { return type == 1 || type == 2 || type == 9 || type == 11; };
            std::set<int> weapons{0};
            for (const auto& [key, count] : typeCounts)
                weapons.insert(key.first);
            for (const auto& [key, count] : polyCounts)
                weapons.insert(key.first);
            for (const auto& [weapon, count] : lightCounts)
                weapons.insert(weapon);
            for (int weapon : weapons) {
                std::printf("over %d frames, %s: entities that aren't models, by type:", cgameFrames,
                            weapon ? ("firing weapon " + std::to_string(weapon)).c_str() : "not firing");
                for (const auto& [key, count] : typeCounts)
                    if (key.first == weapon)
                        std::printf(" %d (x%d%s)", key.second, count, drawn(key.second) ? "" : ", not drawn yet");
                std::printf("\n");
                for (const auto& [key, count] : typeCounts)
                    if (key.first == weapon && !drawn(key.second)) {
                        std::printf("  type %d, the first:\n", key.second);
                        dumpWords(typeSamples[key], 160);
                    }
                if (const auto it = firedModels.find({weapon, 0}); it != firedModels.end()) {
                    std::printf("  models only while firing:");
                    for (const auto& [model, shader] : it->second)
                        std::printf(" %s%s%s", model > 0 ? cgame.models().name(model).c_str() : "(no model)",
                                    shader > 0 ? " with " : "", shader > 0 ? cgame.shaders().name(shader).c_str() : "");
                    std::printf("\n");
                }
                for (const auto& [key, count] : polyCounts) {
                    if (key.first != weapon)
                        continue;
                    const auto& sample = polySamples[key];
                    std::printf("  polygons with %s (%d): %d, the first with %d vertices, its first two as 24 bytes "
                                "each:\n",
                                cgame.shaders().name(key.second).c_str(), key.second, count, sample.vertexCount);
                    dumpWords(sample.vertices, 48);
                }
                if (const int lights = lightCounts[weapon]) {
                    const auto& l = lightSamples[weapon];
                    std::printf("  lights: %d, the first at %.1f %.1f %.1f, radius %.1f, colour %.2f %.2f %.2f\n",
                                lights, l.origin[0], l.origin[1], l.origin[2], l.radius, l.color[0], l.color[1],
                                l.color[2]);
                }
            }
            const auto& requests = cgame.snapshotRequests();
            for (size_t i = 0; i < requests.size() && i < 6; ++i)
                std::printf("cgame asked for snapshot %d at %d%s\n", requests[i].first, requests[i].second,
                            i ? (" (" + std::to_string(requests[i].second - requests[i - 1].second) + " from the last)").c_str()
                              : "");
        }
        for (const auto& [number, count] : cgame.unhandled()) {
            const auto& a = cgame.firstUnhandled().at(number);
            std::printf("cgame: not implemented: system call %d (%d calls), first (%d, %d, %d, %d, %d, %d)\n", number,
                        count, a[0], a[1], a[2], a[3], a[4], a[5]);
            // Arguments that point at text, which often says what a call is.
            for (int i = 0; i < 6; ++i) {
                if (a[i] <= 0 || !cgame.vm().valid(a[i], 1))
                    continue;
                const std::string text = cgame.vm().string(a[i], 64);
                bool printable = text.size() >= 2;
                for (char c : text)
                    printable = printable && static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) < 127;
                if (printable)
                    std::printf("  argument %d is text: \"%s\"\n", i, text.c_str());
            }
        }
        ok = ok && !err;
    }
    return ok ? 0 : 1;
}

// Playing a recorded match (demos/<name>.efdemo) as the Play page does,
// on a clock stepped 16 ms a frame: the client game run on its messages,
// reporting the game state, snapshots and how the client game fared.
int cmdDemoRun(int argc, char** argv) {
    namespace q3 = reluva::idtech3;
    namespace ef = reluva::idtech3::eliteforce;
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool demo-run <game folder> <demo> [-nocgame]\n");
        return 1;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    const bool runCgame = !(argc >= 5 && std::strcmp(argv[4], "-nocgame") == 0);
    std::string path = reluva::normalizeArchiveName(std::string("demos/") + argv[3]);
    if (path.size() < 7 || path.compare(path.size() - 7, 7, ".efdemo") != 0)
        path += ".efdemo";
    Bytes data;
    if (reluva::Error e = set.read(path, data)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), e.message.c_str());
        return 1;
    }
    q3::CvarStore cvars;
    ef::NetClient client(std::move(data));
    std::unique_ptr<ef::CgameHost> cgame;
    size_t snapshots = 0, commandsSeen = 0;
    int32_t firstTime = 0, lastTime = 0;
    uint64_t frames = 0;
    for (uint64_t now = 1000; !client.demoFinished(); now += 16, ++frames) {
        client.frame(now);
        if (client.takeNewGamestate()) {
            const auto& cs = client.configstrings();
            std::printf("game state at message %d: %zu configstrings, map %s, commands from %d\n",
                        client.gamestateMessage(), cs.size(),
                        cs.count(0) ? q3::infoValue(cs.at(0), "mapname").c_str() : "?", client.gamestateCommandSequence());
            if (runCgame) {
                cgame = std::make_unique<ef::CgameHost>(set, cvars);
                cgame->print = [](const std::string& text) {
                    std::printf("cgame: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : "");
                };
                reluva::Error err = cgame->load();
                if (!err) {
                    cgame->setConfigstrings(cs);
                    cgame->setServerCommands(client.serverCommands());
                    err = cgame->init(0, client.gamestateMessage(), client.gamestateCommandSequence());
                }
                if (err) {
                    std::fprintf(stderr, "cgame Init: %s\n", err.message.c_str());
                    return 1;
                }
            }
            client.setLoaded();
        }
        for (auto& snap : client.takeSnapshots()) {
            if (!snapshots++)
                firstTime = snap.serverTime;
            lastTime = snap.serverTime;
            if (snapshots == 1 || snapshots % 200 == 0) {
                int32_t clientNum = -1;
                if (snap.playerState.size() >= 160)
                    std::memcpy(&clientNum, snap.playerState.data() + 156, 4);
                std::printf("snapshot %zu: server time %d, %zu entities, player %d, flags %d\n", snapshots, snap.serverTime,
                            snap.entities.size(), clientNum, snap.snapFlags);
            }
            if (cgame)
                cgame->newSnapshot(std::move(snap));
        }
        for (; commandsSeen < client.serverCommands().size(); ++commandsSeen)
            if (commandsSeen < 3 && !client.serverCommands()[commandsSeen].empty())
                std::printf("server command %zu: %s\n", commandsSeen + 1, client.serverCommands()[commandsSeen].c_str());
        if (cgame && client.hasSnapshot()) {
            cgame->setServerCommands(client.serverCommands());
            cgame->clearScene();
            cgame->clearPictures();
            int32_t ignored = 0;
            if (reluva::Error e = cgame->call(ef::CgameHost::DrawActiveFrame, {client.serverTime(), 0, 1}, ignored)) {
                std::fprintf(stderr, "cgame DrawActiveFrame(%d): %s\n", client.serverTime(), e.message.c_str());
                return 1;
            }
            // Whether it drew the match (a 3D view) or only its waiting
            // screen ("Awaiting snapshot...").
            if (frames % 500 == 0)
                std::printf("frame %llu at %d: %zu views, %zu pictures, %zu entities\n",
                            static_cast<unsigned long long>(frames), client.serverTime(), cgame->scene().views.size(),
                            cgame->pictures().size(), cgame->scene().entities.size());
        }
        if (frames > 2000000)
            break;
    }
    std::printf("%zu snapshots, server time %d to %d (%.1f s), played in %.1f s of frames; %zu server commands\n",
                snapshots, firstTime, lastTime, double(lastTime - firstTime) / 1000, double(frames) * 0.016,
                client.serverCommands().size());
    if (cgame) {
        std::printf("client game calls not answered:");
        for (const auto& [number, count] : cgame->unhandled())
            std::printf(" %d x%d", number, count);
        std::printf("\n");
    }
    return snapshots ? 0 : 1;
}

const Commands commands("idtech3", {
    {"ui-run", "<game folder> [-frames N] [-menu M] [-ingame gametype] [-size WxH] [-glwords N] [-trace] [-v]",
     "run Elite Force's menus (vm/ui.qvm) with the engine's system calls: Init, SetActiveMenu, "
     "Refresh; report what they print, register and draw",
     cmdUiRun},
    {"game-run", "<game folder> <map> [-frames N] [-noclient] [-walk [forward|back|left|right]] [-set name value] [-addbot 'name skill']... [-cgame [-events] [-entities N] [-cgameframes N] [-fire] [-altfire] [-weapon N|all] [-give] [-watch model]] [-buttons] [-trace]",
     "start a level with Elite Force's game logic (vm/qagame.qvm): Init with the map's entities, then "
     "frames, with a player (client 0) joining unless -noclient (-walk: it runs forward); report what "
     "it prints, sets and links; -cgame: then start the client game (vm/cgame.qvm) for the player "
     "(-entities N: dump N of its scene's entities word by word; -cgameframes N frames, -fire, "
     "-altfire held with -weapon N (all: each in turn), -give: every weapon first; and the effects "
     "seen reported); -buttons: hold each button bit in turn and report what it changes",
     cmdGameRun},
    {"demo-run", "<game folder> <demo> [-nocgame]",
     "play a recorded match (demos/<name>.efdemo) as the Play page does, and report how the client "
     "game fared",
     cmdDemoRun},
});

} // namespace
