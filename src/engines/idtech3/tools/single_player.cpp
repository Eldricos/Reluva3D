// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's id Tech 3 commands running Elite Force's single player: its game
// code and menus (native DLLs, in the x86 interpreter) and its scripts.

#include "devtools/tool.h"
#include "engines/idtech3/tools/game_folder.h"

#include "common/fs.h"
#include "emulation/x86/cpu.h"
#include "engines/idtech3/formats/bsp.h"
#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/sp_ui_host.h"
#include "engines/idtech3/jk2/sp_menus.h"
#include "engines/idtech3/jk2/sp_savegame.h"
#include "engines/idtech3/raven/savegame.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iterator>
#include <optional>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;
using namespace reluva::idtech3::tool;

namespace {

namespace q3 = reluva::idtech3;
namespace ef = reluva::idtech3::eliteforce;

// An entity's text fields (pointers to printable text), quoted.
std::string entityTexts(ef::SpGameHost &game, int32_t n) {
    reluva::x86::Cpu &cpu = game.cpu();
    std::string out;
    for (int32_t off = 0; off + 4 <= game.entitySize() && out.size() < 200; off += 4) {
        const uint32_t p = uint32_t(game.entityInt(n, off));
        const std::string t = cpu.valid(p, 1) ? cpu.string(p, 64) : std::string();
        if (t.size() >= 3 && std::all_of(t.begin(), t.end(), [](char c) { return c > 32 && c < 127; }))
            out += " \"" + t + "\"";
    }
    return out;
}

// Whether one of an entity's text fields is `text` (lower case).
bool entityHasText(ef::SpGameHost &game, int32_t n, const std::string &text) {
    reluva::x86::Cpu &cpu = game.cpu();
    for (int32_t off = 0; off + 4 <= game.entitySize(); off += 4) {
        const uint32_t p = uint32_t(game.entityInt(n, off));
        if (cpu.valid(p, 1) && reluva::fs::toLower(cpu.string(p, 64)) == text)
            return true;
    }
    return false;
}

// For "@face name" and "@nearest name -other... [+distance]": a setviewpos
// putting the player 96 units (or the distance given: an AT-ST's box is
// wider than that) to one side of the living entity with that name
// (the nearest, for @nearest, but those with the other names), facing it;
// the first side not over a trigger_hurt, a pit, which kills through god
// mode, and (given the map) not in a wall. One over a pit (falling into it) isn't followed, nor, in Jedi
// Outcast, a dead one (its corpse stays in use). Empty if there's no such
// entity.
// The player's eye over its origin, near enough to aim by.
constexpr double kEyeHeight = 26;

// The nearest entity with the name (any of its text fields) that's
// living, if the game says (Jedi Outcast's health); -1 if none.
int32_t nearestNamed(ef::SpGameHost &game, const std::string &name);

// Jedi Outcast's entity's health: the game's own field, past what the
// engine shares (a stormtrooper's 30, 0 once dead; the most it can have
// follows it).
constexpr int32_t kJediHealth = 0x218;

int32_t nearestNamed(ef::SpGameHost &game, const std::string &name) {
    const std::string want = reluva::fs::toLower(name);
    const ef::SpEntityLayout &layout = game.entityLayout();
    const q3::Vec3f me = game.entityVec(0, layout.currentOrigin);
    int32_t found = -1;
    float best = 0;
    for (int32_t n = 1; n < game.entityCount(); ++n) {
        if (!game.entityInt(n, layout.inUse) || !entityHasText(game, n, want) ||
            (game.jedi() && game.entityInt(n, kJediHealth) <= 0))
            continue;
        const q3::Vec3f at = game.entityVec(n, layout.currentOrigin);
        const float d =
            (at[0] - me[0]) * (at[0] - me[0]) + (at[1] - me[1]) * (at[1] - me[1]) + (at[2] - me[2]) * (at[2] - me[2]);
        if (found < 0 || d < best) {
            found = n;
            best = d;
        }
    }
    return found;
}

std::string facingCommand(ef::SpGameHost &game, const std::string &words, bool nearest,
                          const q3::CollisionWorld *world = nullptr, int32_t *picked = nullptr) {
    std::istringstream in(reluva::fs::toLower(words));
    std::string want;
    in >> want;
    std::vector<std::string> skip;
    float away = 96;
    for (std::string w; in >> w;)
        if (w.size() > 1 && w[0] == '-')
            skip.push_back(w.substr(1));
        else if (w.size() > 1 && w[0] == '+')
            away = float(std::atof(w.c_str() + 1));
    const ef::SpEntityLayout &layout = game.entityLayout();
    const q3::Vec3f me = game.entityVec(0, layout.currentOrigin);
    std::vector<std::pair<q3::Vec3f, q3::Vec3f>> hurts;
    for (int32_t n = 1; n < game.entityCount(); ++n)
        if (game.entityInt(n, layout.inUse) && entityHasText(game, n, "trigger_hurt"))
            hurts.emplace_back(game.entityVec(n, layout.absMin), game.entityVec(n, layout.absMax));
    // Over one with nothing solid between, given the map (a landing pad
    // over a drop isn't a pit).
    auto overPit = [&hurts, world](float x, float y, float z) {
        return std::any_of(hurts.begin(), hurts.end(), [&](const auto &h) {
            if (!(x > h.first[0] - 32 && x < h.second[0] + 32 && y > h.first[1] - 32 && y < h.second[1] + 32 &&
                  h.first[2] < z + 64))
                return false;
            if (!world || h.second[2] >= z)
                return true;
            const q3::Vec3f zero{};
            return world->trace({x, y, z}, {x, y, h.second[2]}, zero, zero, 0, q3::contents::kSolid).fraction == 1;
        });
    };
    int32_t found = -1;
    float best = 0;
    for (int32_t n = 1; n < game.entityCount(); ++n) {
        if (!game.entityInt(n, layout.inUse))
            continue;
        if ((!nearest && found >= 0) || !entityHasText(game, n, want) ||
            std::any_of(skip.begin(), skip.end(), [&](const std::string &s) { return entityHasText(game, n, s); }))
            continue;
        const q3::Vec3f at = game.entityVec(n, layout.currentOrigin);
        if (nearest && (overPit(at[0], at[1], at[2]) || (game.jedi() && game.entityInt(n, kJediHealth) <= 0)))
            continue;
        const float d =
            (at[0] - me[0]) * (at[0] - me[0]) + (at[1] - me[1]) * (at[1] - me[1]) + (at[2] - me[2]) * (at[2] - me[2]);
        if (found < 0 || d < best) {
            found = n;
            best = d;
        }
    }
    if (found < 0)
        return {};
    if (picked)
        *picked = found;
    const q3::Vec3f at = game.entityVec(found, layout.currentOrigin);
    const float sides[4][3] = {{-away, 0, 0}, {away, 0, 180}, {0, -away, 90}, {0, away, 270}};
    // With the map: a side the player fits in, its eye with a line to
    // the entity's middle (shots from inside a wall stay there).
    auto clear = [&](const float *s) {
        if (!world)
            return true;
        const q3::Vec3f spot{at[0] + s[0], at[1] + s[1], at[2] + 8};
        const q3::Vec3f eye{spot[0], spot[1], spot[2] + float(kEyeHeight)};
        const q3::Vec3f middle{at[0], at[1], at[2] + 8};
        const q3::Vec3f mins{-15, -15, -24};
        const q3::Vec3f maxs{15, 15, 40};
        const q3::Vec3f zero{};
        return !world->trace(spot, spot, mins, maxs, 0, q3::contents::kSolid).startSolid &&
               world->trace(eye, middle, zero, zero, 0, q3::contents::kSolid).fraction == 1;
    };
    const float *side = sides[0];
    for (const float *s : sides) {
        if (!overPit(at[0] + s[0], at[1] + s[1], at[2]) && clear(s)) {
            side = s;
            break;
        }
    }
    char command[128];
    std::snprintf(command, sizeof command, "setviewpos %.0f %.0f %.0f %.0f", double(at[0] + side[0]),
                  double(at[1] + side[1]), double(at[2]) + 8, double(side[2]));
    return command;
}

// -tour: a replay (as -replay reads) playing a level through with cheats.
// At `fromMs` (the opening over), god and undying modes and every weapon;
// then each trigger in the level touched in turn (its middle, a little up
// from its floor) facing its angle, holding use (Jedi Outcast's button
// 5) if it waits for it, and three seconds of fighting the nearest NPC,
// the weapon changing each time; `rounds` times over, for triggers
// switched on by what happens later in the level. Left out: triggers
// firing a target_kill or a misc_camera, trigger_hurt, triggers in or
// over one (nothing solid between) or a target_kill's trigger, and triggers naming a spared
// entity (an ally they'd move into the fighting); spared entities
// aren't shot at. Only direct fire (the saber, pistol, blaster,
// disruptor, bowcaster, repeater): splash would hurt the allies, whose
// death fails the mission. `commands` (console commands, or a route's
// "@" lines) follow the cheats. Last, with `withExits`, the triggers
// that change the level.
std::string tourRoute(const q3::Bsp &bsp, int fromMs, int rounds, const std::vector<std::string> &spare, bool withExits,
                      const std::vector<std::string> &commands, int commandsMs) {
    const q3::CollisionWorld world(bsp);
    std::vector<q3::Entity> entities;
    if (q3::parseEntities(bsp.entities, entities))
        return {};
    auto bounds = [&bsp](const q3::Entity &e, std::array<float, 6> &out) {
        const std::string m = e.get("model");
        if (m.size() < 2 || m[0] != '*')
            return false;
        const int n = std::atoi(m.c_str() + 1);
        if (n <= 0 || size_t(n) >= bsp.models.size())
            return false;
        const q3::BspModel &model = bsp.models[size_t(n)];
        out = {model.mins[0], model.mins[1], model.mins[2], model.maxs[0], model.maxs[1], model.maxs[2]};
        return true;
    };
    // Brush entities that move (doors, platforms): a visit's spot is kept
    // out of their boxes.
    std::vector<std::array<float, 6>> movers;
    for (const q3::Entity &e : entities)
        if (std::array<float, 6> b{}; (e.classname() == "func_door" || e.classname() == "func_plat" ||
                                       e.classname() == "func_train" || e.classname() == "func_bobbing") &&
                                      bounds(e, b))
            movers.push_back(b);
    std::vector<std::array<float, 6>> hurts;
    for (const q3::Entity &e : entities)
        if (std::array<float, 6> b{}; e.classname() == "trigger_hurt" && bounds(e, b))
            hurts.push_back(b);
    std::set<std::string> levelChanges;
    // Targets whose triggers are left out: a target_kill (a drop's trigger
    // kills whoever's in it, god mode or not) and a misc_camera (a
    // security camera's view, which holds the player until left).
    std::set<std::string> kills;
    for (const q3::Entity &e : entities) {
        if (e.classname() == "target_level_change" && e.has("targetname"))
            levelChanges.insert(e.get("targetname"));
        if ((e.classname() == "target_kill" || e.classname() == "misc_camera") && e.has("targetname"))
            kills.insert(e.get("targetname"));
    }
    // A target_kill's trigger kills as a trigger_hurt does: not stood in.
    for (const q3::Entity &e : entities)
        for (const q3::Entity &k : entities)
            if (std::array<float, 6> b{}; k.classname() == "target_kill" && k.has("targetname") &&
                                          k.get("targetname") == e.get("target") && bounds(e, b))
                hurts.push_back(b);
    std::vector<std::array<float, 6>> exits;
    std::string skip;
    for (const std::string &s : spare)
        skip += " -" + s;
    // The cheats and the commands at `commandsMs` (0: as the tour starts),
    // the commands done before it starts (an alarm's runner chased down).
    std::string out =
        "@until " + std::to_string(commandsMs > 0 ? commandsMs : fromMs) + " 0 0\n/god\n/undying\n/give all\n";
    for (const std::string &c : commands)
        out += (c.rfind('@', 0) == 0 ? c : "/" + c) + "\n";
    if (commandsMs > 0)
        out += "@until " + std::to_string(fromMs) + " 0 0\n";
    const int weapons[] = {4, 3, 4, 1, 4, 2, 4, 5, 4, 6}; // mostly the disruptor, which kills at a shot
    int t = fromMs;
    int count = 0;
    std::vector<const q3::Entity *> visits;
    for (const q3::Entity &e : entities) {
        std::array<float, 6> b{};
        const std::string cls = e.classname();
        if (cls.rfind("trigger_", 0) != 0 || cls == "trigger_hurt" || !bounds(e, b))
            continue;
        if (levelChanges.count(e.get("target")))
            exits.push_back(b);
        else if (!kills.count(e.get("target")))
            visits.push_back(&e);
    }
    for (int round = 0; round < rounds; ++round)
        for (const q3::Entity *visit : visits) {
            const q3::Entity &e = *visit;
            std::array<float, 6> b{};
            bounds(e, b);
            const std::string cls = e.classname();
            bool named = false;
            for (const auto &[key, value] : e.pairs)
                for (const std::string &s : spare)
                    named = named || reluva::fs::toLower(value).find(s) != std::string::npos;
            // Its middle, or a quarter of the way to a side if a door or the
            // like stands there: one shut on the player kills, god mode
            // or not (cairn_dock1's *16 and its door).
            float x = (b[0] + b[3]) / 2;
            float y = (b[1] + b[4]) / 2;
            const float z = b[2] + 24;
            const float quarters[5][2] = {{0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0}};
            for (const auto &q : quarters) {
                const float cx = (b[0] + b[3]) / 2 + q[0] * (b[3] - b[0]) / 4;
                const float cy = (b[1] + b[4]) / 2 + q[1] * (b[4] - b[1]) / 4;
                const bool inside = std::any_of(movers.begin(), movers.end(), [&](const std::array<float, 6> &m) {
                    return m[0] - 16 < cx && cx < m[3] + 16 && m[1] - 16 < cy && cy < m[4] + 16 && m[2] - 24 < z &&
                           z < m[5] + 40;
                });
                if (!inside) {
                    x = cx;
                    y = cy;
                    break;
                }
            }
            // In one, or over one with nothing solid between: the player
            // falls in (cairn_dock1's electrified floor under *7).
            const bool hurt = std::any_of(hurts.begin(), hurts.end(), [&](const std::array<float, 6> &h) {
                if (!(h[0] - 48 < x && x < h[3] + 48 && h[1] - 48 < y && y < h[4] + 48 && h[2] - 64 < z))
                    return false;
                if (z < h[5] + 64)
                    return true;
                const q3::Vec3f zero{};
                return world.trace({x, y, z}, {x, y, h[5]}, zero, zero, 0, q3::contents::kSolid).fraction == 1;
            });
            if (named || hurt)
                continue;
            const int w = weapons[count % int(std::size(weapons))];
            char line[160];
            // Facing its angle: some are only set off looking that way.
            std::snprintf(line, sizeof line, "# %d %s\n/setviewpos %.0f %.0f %.0f %d\n", count, cls.c_str(), double(x),
                          double(y), double(z), std::atoi(e.get("angle").c_str()));
            out += line;
            t += 300;
            // Use held only for the triggers that wait for it (a
            // trigger_multiple's 4): it would set off one that overlaps,
            // as a security camera's (kejim_post's *212 over *51).
            const bool multiple = cls == "trigger_multiple" || cls == "trigger_once";
            const int use = !multiple || (std::atoi(e.get("spawnflags").c_str()) & 4) ? 32 : 0;
            out += "@until " + std::to_string(t) + " " + std::to_string(use) + " " + std::to_string(w) + "\n";
            for (int k = 0; k < 6; ++k) {
                out += "@nearest npc" + skip + "\n";
                t += 500;
                // The saber swung and thrown in turn; the guns' main fire.
                const int buttons = w == 1 && k % 2 ? 128 : 1;
                out += "@until " + std::to_string(t) + " " + std::to_string(buttons) + " " + std::to_string(w) + "\n";
            }
            ++count;
        }
    for (const std::array<float, 6> &b : exits) {
        if (!withExits)
            break;
        char line[128];
        std::snprintf(line, sizeof line, "# the level change\n/setviewpos %.0f %.0f %.0f 0\n",
                      double((b[0] + b[3]) / 2), double((b[1] + b[4]) / 2), double(b[2] + 24));
        out += line;
    }
    out += "@until " + std::to_string(t + 3000) + " 0 0\n";
    return out;
}

} // namespace

namespace {

// What a frame of single-player menus drew: each shader's pictures.
void printPictures(int frame, const reluva::idtech3::SpMenuHost &ui, bool fullscreen) {
    std::map<std::string, int> shown;
    for (const auto &p : ui.pictures())
        ++shown[ui.shaders().name(p.shader)];
    std::printf("[frame %d] %zu pictures, %zu scenes, catcher %d, fullscreen %d:\n", frame, ui.pictures().size(),
                ui.scene().views.size(), ui.catcher(), fullscreen ? 1 : 0);
    for (const auto &[name, count] : shown)
        std::printf("  %4d x %s\n", count, name.empty() ? "(none)" : name.c_str());
}

// Jedi Outcast's single-player menus, which its engine runs from the
// menu scripts: as cmdSpMenus, with the mouse moved too, and the menus
// open and the focus reported as they change.
int runJediMenus(const reluva::SearchSet &set, reluva::idtech3::CvarStore &cvars,
                 reluva::idtech3::KeyBindings &bindings, const std::string &menu, int frames,
                 const std::vector<std::pair<int, int32_t>> &keys,
                 const std::vector<std::pair<int, std::array<int32_t, 2>>> &moves, const std::string &picturesPath) {
    namespace q3 = reluva::idtech3;
    q3::jk2::SpMenus ui(set, cvars);
    ui.bindings = &bindings;
    ui.print = [](const std::string &text) {
        std::printf("menus: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : "");
    };
    ui.startLocalSound = [](const std::string &sound, int channel) {
        std::printf("sound: %s (channel %d)\n", sound.c_str(), channel);
    };
    if (reluva::Error e = ui.load()) {
        std::printf("Loading the menus failed: %s\n", e.message.c_str());
        return 1;
    }
    std::string names;
    for (const std::string &n : ui.menuNames())
        names += " " + n;
    std::printf("%zu menus read:%s\n", ui.menuNames().size(), names.c_str());
    std::vector<std::string> open;
    std::string focus;
    auto report = [&](int f) {
        for (const std::string &command : ui.takeCommands())
            std::printf("[frame %d] command: %s\n", f, command.c_str());
        if (ui.openMenus() != open || ui.focusedItem() != focus) {
            open = ui.openMenus();
            focus = ui.focusedItem();
            std::string list;
            for (const std::string &n : open)
                list += " " + n;
            std::printf("[frame %d] open:%s; focus: %s\n", f, list.empty() ? " none" : list.c_str(),
                        focus.empty() ? "none" : focus.c_str());
        }
    };
    if (reluva::Error e = ui.setActiveMenu(menu)) {
        std::printf("SetActiveMenu failed: %s\n", e.message.c_str());
        return 1;
    }
    report(0);
    for (int f = 0; f < frames; ++f) {
        for (const auto &[at, d] : moves)
            if (at == f) {
                ui.mouseEvent(d[0], d[1]);
                std::printf("[frame %d] mouse to %.0f %.0f\n", f, double(ui.cursor()[0]), double(ui.cursor()[1]));
                report(f);
            }
        for (const auto &[at, k] : keys)
            if (at == f) {
                std::printf("[frame %d] key %s (%d)\n", f, q3::KeyBindings::keyName(k).c_str(), k);
                ui.keyEvent(k);
                report(f);
            }
        ui.refresh(f * 50);
        if (f == 0 || f == frames - 1)
            printPictures(f, ui, ui.fullscreen());
    }
    if (!picturesPath.empty()) {
        // The last frame's pictures, a line each: "x y w h s1 t1 s2 t2 r g b a shader".
        std::ofstream out(picturesPath);
        for (const auto &p : ui.pictures())
            out << p.x << ' ' << p.y << ' ' << p.w << ' ' << p.h << ' ' << p.s1 << ' ' << p.t1 << ' ' << p.s2 << ' '
                << p.t2 << ' ' << p.color[0] << ' ' << p.color[1] << ' ' << p.color[2] << ' ' << p.color[3] << ' '
                << ui.shaders().name(p.shader) << '\n';
    }
    return 0;
}

} // namespace

// Runs Elite Force's single-player menus (efuix86.dll, in the x86
// interpreter): GetUIAPI, Init, the main menu, then frames, with keys
// pressed along the way; reports what they drew and the commands they ran.
// Without the DLL, Jedi Outcast's, if the folder is its (runJediMenus).
int cmdSpMenus(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: reluva-tool idtech3 sp-menus <game folder> [-dll efuix86.dll] [-frames N] "
                     "[-menu name] [-key frame key]... [-mouse frame dx dy]... [-set name value]... [-fixedclock] "
                     "[-skippak pak]...\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    namespace ef = reluva::idtech3::eliteforce;
    int frames = 10;
    std::vector<std::string> skipPaks; // -skippak name: a pak left out (the Expansion Pack's, say)
    bool fixedClock = false;           // -fixedclock: a clock that only moves with the frames, for runs that repeat
    int clockFrame = 0;
    std::string dllPath;
    std::string menu = "main";
    std::vector<std::pair<int, int32_t>> keys;                 // (frame, key)
    std::vector<std::pair<int, std::array<int32_t, 2>>> moves; // (frame, (dx, dy)): Jedi Outcast's
    std::string picturesPath; // -pictures file: Jedi Outcast's last frame's pictures written there
    std::vector<std::pair<std::string, std::string>> settings;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "-frames") == 0 && i + 1 < argc)
            frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-dll") == 0 && i + 1 < argc)
            dllPath = argv[++i];
        else if (std::strcmp(argv[i], "-menu") == 0 && i + 1 < argc)
            menu = argv[++i];
        else if (std::strcmp(argv[i], "-key") == 0 && i + 2 < argc) {
            // A key's number, or its name ("ENTER", "MOUSE1", "DOWNARROW").
            int32_t k = q3::KeyBindings::keyNumber(argv[i + 2]);
            if (k < 0)
                k = std::atoi(argv[i + 2]);
            keys.emplace_back(std::atoi(argv[i + 1]), k);
            i += 2;
        } else if (std::strcmp(argv[i], "-pictures") == 0 && i + 1 < argc) {
            picturesPath = argv[++i];
        } else if (std::strcmp(argv[i], "-mouse") == 0 && i + 3 < argc) {
            moves.push_back({std::atoi(argv[i + 1]), {std::atoi(argv[i + 2]), std::atoi(argv[i + 3])}});
            i += 3;
        } else if (std::strcmp(argv[i], "-skippak") == 0 && i + 1 < argc) {
            skipPaks.push_back(reluva::fs::toLower(argv[++i]));
        } else if (std::strcmp(argv[i], "-fixedclock") == 0) {
            fixedClock = true;
        } else if (std::strcmp(argv[i], "-set") == 0 && i + 2 < argc) {
            settings.emplace_back(argv[i + 1], argv[i + 2]);
            i += 2;
        } else
            std::fprintf(stderr, "sp-menus: ignoring %s\n", argv[i]);
    }
    if (dllPath.empty()) {
        std::filesystem::path folder = std::filesystem::path(argv[2]).lexically_normal();
        if (!folder.has_filename())
            folder = folder.parent_path();
        for (const std::filesystem::path &p : {folder.parent_path() / "efuix86.dll", folder / "efuix86.dll"})
            if (std::filesystem::exists(p)) {
                dllPath = p.string();
                break;
            }
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set, skipPaks))
        return 1;
    q3::CvarStore cvars;
    for (const auto &[name, value] : settings)
        cvars.set(name, value);
    q3::KeyBindings bindings;
    if (dllPath.empty() && set.has("ui/menus.txt"))
        return runJediMenus(set, cvars, bindings, menu, frames, keys, moves, picturesPath);
    if (dllPath.empty()) {
        std::fprintf(stderr, "efuix86.dll isn't beside %s; give it with -dll\n", argv[2]);
        return 1;
    }
    Bytes dll;
    if (!readFile(dllPath.c_str(), dll))
        return 1;
    ef::SpUiHost ui(set, cvars);
    std::tie(ui.modInstall, ui.modBaseDir) = idTech3Folders(argv[2]);
    ui.bindings = &bindings;
    ui.print = [](const std::string &text) {
        std::printf("menus: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : "");
    };
    ui.startLocalSound = [](const std::string &sound, int channel) {
        std::printf("sound: %s (channel %d)\n", sound.c_str(), channel);
    };
    auto failed = [&](const char *what, const reluva::Error &e) {
        std::printf("%s failed: %s\n", what, e.message.c_str());
        std::printf("instructions run: %llu, at 0x%08x\n", (unsigned long long)ui.cpu().executed(), ui.cpu().faultEip);
        return 1;
    };
    if (fixedClock)
        ui.clock = [&clockFrame] { return 10000u + uint32_t(clockFrame) * 50u; };
    if (reluva::Error e = ui.load(dll))
        return failed("Loading efuix86.dll", e);
    std::printf("efuix86.dll loaded; %zu shaders, %zu sounds registered\n", ui.shaders().size(), ui.sounds().size());
    if (reluva::Error e = ui.setActiveMenu(menu))
        return failed("SetActiveMenu", e);
    for (int f = 0; f < frames; ++f) {
        clockFrame = f;
        for (const auto &[at, k] : keys)
            if (at == f) {
                std::printf("[frame %d] key %s (%d)\n", f, q3::KeyBindings::keyName(k).c_str(), k);
                if (reluva::Error e = ui.keyEvent(k))
                    return failed("KeyEvent", e);
            }
        if (reluva::Error e = ui.refresh(f * 50))
            return failed("Refresh", e);
        for (const std::string &command : ui.takeCommands())
            std::printf("[frame %d] command: %s\n", f, command.c_str());
        if (f == 0 || f == frames - 1)
            printPictures(f, ui, ui.fullscreen());
    }
    if (!ui.unhandled().empty()) {
        std::printf("engine functions not answered yet:");
        for (const auto &[slot, count] : ui.unhandled())
            std::printf(" %d (x%d)", slot, count);
        std::printf("\n");
    }
    return 0;
}

// Runs Elite Force's single-player game logic (efgamex86.dll, in the x86
// interpreter) on a map: GetGameAPI, Init with the map's entities, the
// player joining, then frames; reports what the game did.
int cmdSpRun(int argc, char **argv) {
    // The map can be left out with -load (the saved game's).
    const int firstOption = argc >= 4 && argv[3][0] != '-' ? 4 : 3;
    if (argc < 4) {
        std::fprintf(
            stderr,
            "usage: reluva-tool idtech3 sp-run <game folder> <map> [-dll efgamex86.dll] [-frames N] "
            "[-set name value]... [-trace] [-noclient] [-cgame] [-watch] [-find text]... [-icarus] [-saveload] [-next] [-fixedclock] [-skippak pak]...\n"
            "       [-replay <name>.input | -tour <ms> [-rounds n] [-spare name]...]\n"
            "       reluva-tool idtech3 sp-run <game folder> -load <saved game> [-replay <name>.input] [options as above]\n"
            "A replay's lines: \"time buttons weapon pitch yaw roll forward right up\" as the Play page records them;\n"
            "\"/command\" typed at the console; \"@until ms [buttons] [weapon] [pitch]\" the same command every 50 ms until\n"
            "then (\"+ms\": that long from now), looking pitch degrees down (up negative); \"@aim name\" (\"@aim x y z\") the view turned to that entity (point) for the @until lines after (\"@aim none\" level again);\n"
            "\"@face name\", \"@nearest name -other...\" the player put beside that entity, facing it;\n"
            "\"@onto name [+height]\" the player on top of that entity (or that high in it); \"@where [name]\" where the player is, and that entity; \"@aimfrom x y z [yaw pitch]\" @aim measured from there\n"
            "(and turning from those angles: a panel turret; none: the eye); \"@repeat n\" ... \"@end\" those lines n times.\n"
            "-tour: from then, every trigger touched and the nearest NPCs fought, with cheats (set helpUsObi 1);\n"
            "-dumproute file: the tour's route written there; -cmd text: typed as the tour starts, after the cheats\n"
            "(a route's line if it starts with @); -before ms: those and the cheats then, before the tour;\n"
            "with -replay, the replay's lines after the tour in place of its level change.\n"
            "-next: then the level the game changed to, started as the Play page starts it.\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    namespace ef = reluva::idtech3::eliteforce;
    int frames = 20;
    bool traceCalls = false;
    bool noClient = false;
    bool runCgame = false;
    bool watch = false;
    std::vector<std::string> finds;    // -find text: entities with a text field containing it
    bool icarusNames = false;          // -icarus: the script system's table of entity names
    std::vector<std::string> skipPaks; // -skippak name: a pak left out (the Expansion Pack's, say)
    bool fixedClock = false;           // -fixedclock: the game's clock moves with its level time, for runs that repeat
    bool saveLoad = false;             // -saveload: save at the end, load into a fresh game
    bool followNext = false;           // -next: the level change the game asked for, followed
    std::string loadName;              // -load save: a saved game (its name in saves/, or a file)
    std::string dumpRoute;             // -dumproute file: the tour's route written there
    std::vector<std::string> tourCommands; // -cmd text: typed as the tour starts, or a route's "@" line
    int commandsFrom = 0;                  // -before ms: the cheats and -cmd lines then, before the tour
    int tourFrom = 0;                      // -tour ms: from then, a route through the level's triggers (tourRoute)
    int tourRounds = 1;                    // -rounds n: the route that many times
    std::vector<std::string> spare;        // -spare name: not shot at, nor its triggers touched, on a tour
    std::string replayPath;                // -replay file: the player's commands, as the Play page records them,
                                           // "/command" lines typed at the console and "@" lines (see below)
    std::string dllPath;
    std::vector<std::pair<std::string, std::string>> settings;
    for (int i = firstOption; i < argc; ++i) {
        if (std::strcmp(argv[i], "-frames") == 0 && i + 1 < argc)
            frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-dll") == 0 && i + 1 < argc)
            dllPath = argv[++i];
        else if (std::strcmp(argv[i], "-trace") == 0)
            traceCalls = true;
        else if (std::strcmp(argv[i], "-noclient") == 0)
            noClient = true;
        else if (std::strcmp(argv[i], "-cgame") == 0)
            runCgame = true;
        else if (std::strcmp(argv[i], "-watch") == 0)
            watch = runCgame = true;
        else if (std::strcmp(argv[i], "-find") == 0 && i + 1 < argc)
            finds.push_back(reluva::fs::toLower(argv[++i]));
        else if (std::strcmp(argv[i], "-icarus") == 0)
            icarusNames = true;
        else if (std::strcmp(argv[i], "-skippak") == 0 && i + 1 < argc)
            skipPaks.push_back(reluva::fs::toLower(argv[++i]));
        else if (std::strcmp(argv[i], "-fixedclock") == 0)
            fixedClock = true;
        else if (std::strcmp(argv[i], "-saveload") == 0)
            saveLoad = true;
        else if (std::strcmp(argv[i], "-next") == 0)
            followNext = true;
        else if (std::strcmp(argv[i], "-load") == 0 && i + 1 < argc)
            loadName = argv[++i];
        else if (std::strcmp(argv[i], "-tour") == 0 && i + 1 < argc) {
            tourFrom = std::atoi(argv[++i]);
            runCgame = true;
        } else if (std::strcmp(argv[i], "-rounds") == 0 && i + 1 < argc) {
            tourRounds = std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "-spare") == 0 && i + 1 < argc)
            spare.push_back(reluva::fs::toLower(argv[++i]));
        else if (std::strcmp(argv[i], "-dumproute") == 0 && i + 1 < argc)
            dumpRoute = argv[++i];
        else if (std::strcmp(argv[i], "-cmd") == 0 && i + 1 < argc)
            tourCommands.emplace_back(argv[++i]);
        else if (std::strcmp(argv[i], "-before") == 0 && i + 1 < argc)
            commandsFrom = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-replay") == 0 && i + 1 < argc) {
            replayPath = argv[++i];
            runCgame = true; // the commands go through the client game, as recorded
        } else if (std::strcmp(argv[i], "-set") == 0 && i + 2 < argc) {
            settings.emplace_back(argv[i + 1], argv[i + 2]);
            i += 2;
        } else
            std::fprintf(stderr, "sp-run: ignoring %s\n", argv[i]);
    }
    // The DLL sits beside the game's folder (the install's), or in it:
    // Elite Force's, or Jedi Outcast's.
    if (dllPath.empty()) {
        std::filesystem::path folder = std::filesystem::path(argv[2]).lexically_normal();
        if (!folder.has_filename())
            folder = folder.parent_path();
        for (const char *name : {"efgamex86.dll", "jk2gamex86.dll"})
            for (const std::filesystem::path &p : {folder.parent_path() / name, folder / name})
                if (dllPath.empty() && std::filesystem::exists(p))
                    dllPath = p.string();
        if (dllPath.empty()) {
            std::fprintf(stderr, "Neither efgamex86.dll nor jk2gamex86.dll is beside %s; give it with -dll\n", argv[2]);
            return 1;
        }
    }
    const bool jedi = reluva::fs::toLower(std::filesystem::path(dllPath).filename().string()) == "jk2gamex86.dll";
    Bytes dll;
    if (!readFile(dllPath.c_str(), dll))
        return 1;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set, skipPaks))
        return 1;
    q3::CvarStore cvars;
    for (const auto &[name, value] : settings)
        cvars.set(name, value);
    // -load: the saved game's header read first (it names the map and sets
    // the cvars it kept); the rest as the level starts.
    std::unique_ptr<ef::SaveReader> save;
    ef::SaveHeader saveHeader;
    if (!loadName.empty()) {
        Bytes file;
        bool found = false;
        if (std::filesystem::exists(loadName))
            found = readFile(loadName.c_str(), file);
        else
            found = !set.read("saves/" + loadName + ".sav", file);
        if (!found) {
            std::fprintf(stderr, "no saved game %s (a file, or saves/%s.sav in the game's files)\n", loadName.c_str(),
                         loadName.c_str());
            return 1;
        }
        save = std::make_unique<ef::SaveReader>(std::move(file));
        if (reluva::Error err = ef::readSaveHeader(*save, cvars, saveHeader)) {
            std::fprintf(stderr, "%s: %s\n", loadName.c_str(), err.message.c_str());
            return 1;
        }
        // The command line's settings win over the saved game's.
        for (const auto &[name, value] : settings)
            cvars.set(name, value);
        std::printf("saved game \"%s\": map %s%s\n", saveHeader.comment.c_str(), saveHeader.map.c_str(),
                    saveHeader.autosave ? " (autosave)" : "");
    } else if (firstOption == 3) {
        std::fprintf(stderr, "sp-run: give a map, or a saved game with -load\n");
        return 2;
    }
    const int32_t loadMode = save ? (saveHeader.autosave ? 2 : 1) : 0;
    std::string map = save ? saveHeader.map : argv[3];
    if (map.size() > 4 && map.compare(map.size() - 4, 4, ".bsp") == 0)
        map.resize(map.size() - 4);
    if (map.rfind("maps/", 0) == 0)
        map = map.substr(5);
    Bytes data;
    auto bsp = std::make_shared<q3::Bsp>();
    if (reluva::Error err = set.read("maps/" + map + ".bsp", data); err || (err = q3::parseBsp(data, *bsp))) {
        std::fprintf(stderr, "maps/%s.bsp: %s\n", map.c_str(), err.message.c_str());
        return 1;
    }
    const q3::CollisionWorld mapWorld(*bsp);
    ef::SpGameHost game(set, cvars);
    if (fixedClock)
        game.clock = [&game] { return 10000u + uint32_t(game.levelTime()); };
    std::tie(game.modInstall, game.modBaseDir) = idTech3Folders(argv[2]);
    game.print = [](const std::string &text) {
        std::printf("game: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : "");
    };
    game.voiceLog = [](const std::string &text) { std::printf("voice: %s\n", text.c_str()); };
    std::printf("(voices logged as they start and end)\n");
    // For a failure's report: the last engine calls, and (set up once the
    // DLL is loaded) the last instructions run.
    std::vector<std::pair<int, uint32_t>> recentCalls(16);
    size_t callsAt = 0;
    game.trace = [&](int slot, uint32_t from) {
        recentCalls[callsAt++ % recentCalls.size()] = {slot, from};
        if (traceCalls)
            std::printf("  engine[%d] from 0x%08x\n", slot, from);
    };
    std::vector<uint32_t> recent(64);
    size_t recentAt = 0;
    auto failed = [&](const char *what, const reluva::Error &e) {
        reluva::x86::Cpu &cpu = game.cpu();
        std::printf("%s failed: %s\n", what, e.message.c_str());
        std::printf("instructions run: %llu\n", (unsigned long long)cpu.executed());
        // Addresses as the DLL's own (at its preferred base), to look up.
        const uint32_t base = game.module() ? game.module()->base : 0;
        const uint32_t preferred = game.module() ? uint32_t(game.module()->image.imageBase) : 0;
        const uint32_t size = game.module() ? game.module()->size : 0;
        auto dll = [&](uint32_t a) {
            char b[32];
            if (a >= base && a < base + size)
                std::snprintf(b, sizeof b, "%08x", a - base + preferred);
            else
                std::snprintf(b, sizeof b, "(%08x)", a);
            return std::string(b);
        };
        std::printf("last instructions (at the DLL's own addresses):");
        for (size_t i = recentAt > recent.size() ? recentAt - recent.size() : 0, k = 0; i < recentAt; ++i, ++k)
            std::printf("%s%s", k % 8 ? " " : "\n  ", dll(recent[i % recent.size()]).c_str());
        std::printf("\nlast engine calls:");
        for (size_t i = callsAt > recentCalls.size() ? callsAt - recentCalls.size() : 0; i < callsAt; ++i)
            std::printf(" %d@%s", recentCalls[i % recentCalls.size()].first,
                        dll(recentCalls[i % recentCalls.size()].second).c_str());
        std::printf("\nregisters: eax %08x ecx %08x edx %08x ebx %08x esp %08x ebp %08x esi %08x edi %08x\n",
                    cpu.faultReg[0], cpu.faultReg[1], cpu.faultReg[2], cpu.faultReg[3], cpu.faultReg[4],
                    cpu.faultReg[5], cpu.faultReg[6], cpu.faultReg[7]);
        // Registers pointing into an entity: which, its owner and its text.
        if (const uint32_t first = game.entities(); first && game.entitySize() > 0)
            for (int r = 0; r < 8; ++r) {
                const uint32_t v = cpu.faultReg[size_t(r)];
                const uint32_t size = uint32_t(game.entitySize());
                if (v < first || v >= first + size * uint32_t(game.entityCount()))
                    continue;
                const int32_t n = int32_t((v - first) / size);
                std::printf("  register %d: entity %d (+0x%x), owner %08x:%s\n", r, n, (v - first) % size,
                            uint32_t(game.entityInt(n, game.entityLayout().owner)), entityTexts(game, n).c_str());
            }
        // Return addresses on the stack: words pointing into the DLL's code.
        std::printf("stack (words pointing into the DLL):");
        int shown = 0;
        const uint32_t sp = cpu.faultReg[reluva::x86::Cpu::ESP];
        for (uint32_t a = sp; shown < 24 && cpu.valid(a, 4) && a < sp + 0x4000; a += 4) {
            const uint32_t v = cpu.read32(a);
            if (v >= base && v < base + 0xbb000) { // .text
                std::printf("%s%s", shown % 8 ? " " : "\n  ", dll(v).c_str());
                ++shown;
            }
        }
        std::printf("\n");
        return 1;
    };
    const auto started = std::chrono::steady_clock::now();
    if (reluva::Error e = game.load(dll, jedi))
        return failed("loading", e);
    std::printf("%s loaded at 0x%08x\n", jedi ? "jk2gamex86.dll" : "efgamex86.dll", game.module()->base);
    game.cpu().trace = [&](reluva::x86::Cpu &c) { recent[recentAt++ % recent.size()] = c.eip; };
    game.setSaveStream(nullptr, save.get());
    if (reluva::Error e = game.startLevel(map, bsp, ef::SpGameHost::mapChecksum(data), 0, loadMode))
        return failed("Init", e);
    if (save) {
        if (reluva::Error e = ef::readSaveLevel(*save, game, saveHeader.autosave))
            return failed("reading the saved game", e);
        std::printf("saved game read: level time %d\n", game.levelTime());
    }
    game.setSaveStream(nullptr, nullptr);
    std::printf("Init done: %d entities (%d bytes each), %zu linked, %zu configstrings\n", game.entityCount(),
                game.entitySize(), game.linked().size(), game.configstrings().size());
    // The player as the Play page describes them (the sex's models).
    const bool male = reluva::fs::toLower(cvars.string("sex")).rfind('f', 0) != 0;
    const std::string userinfo = std::string("\\name\\Munro\\snaps\\20\\sex\\") + (male ? "male" : "female") +
                                 "\\headModel\\" + (male ? "munro/default" : "alexandria/default") + "\\torsoModel\\" +
                                 (male ? "hazard/default" : "hazardfemale/default") + "\\legsModel\\" +
                                 (male ? "hazard/default" : "hazardfemale/default") + "\\handicap\\100";
    if (!noClient) {
        std::string refused;
        if (reluva::Error e = game.connectPlayer(userinfo, refused, loadMode))
            return failed("the player joining", e);
        if (!refused.empty())
            std::printf("the game refused the player: %s\n", refused.c_str());
        else
            std::printf("player joined\n");
    }
    if (runCgame && !noClient) {
        const uint64_t before = game.cpu().executed();
        if (reluva::Error e = game.startClientGame())
            return failed("the client game's start-up", e);
        std::printf(
            "client game started: %llu instructions; %zu models, %zu shaders, %zu skins, %zu sounds registered\n",
            (unsigned long long)(game.cpu().executed() - before), game.models().size(), game.shaders().size(),
            game.skins().size(), game.sounds().size());
    }
    // The player's command for a moment, then the client game's frame: the
    // Play page's order. Empty: none to report; else what failed.
    auto clientStep = [&](const ef::SpGameHost::UserCommand &cmd, int f) -> int {
        if (reluva::Error e = game.clientThink(cmd))
            return failed(("command " + std::to_string(f + 1)).c_str(), e);
        if (reluva::Error e = game.drawClientFrame(cmd.serverTime))
            return failed(("client frame " + std::to_string(f + 1)).c_str(), e);
        // Every five seconds of the level, or a quarter second while a
        // scripted camera runs: the camera, the view, the player.
        reluva::idtech3::Vec3f cam{};
        const bool camera = watch && game.cameraPosition(cam);
        if (watch && (game.levelTime() % 5000 == 0 || (camera && game.levelTime() % 250 == 0))) {
            reluva::x86::Cpu &cpu = game.cpu();
            auto f = [&cpu](uint32_t at) {
                const uint32_t u = cpu.read32(at);
                float v;
                std::memcpy(&v, &u, 4);
                return v;
            };
            auto bytesFloat = [](const std::vector<uint8_t> &b, size_t at) {
                float v = 0;
                if (at + 4 <= b.size())
                    std::memcpy(&v, b.data() + at, 4);
                return v;
            };
            std::string view = "no view";
            if (!game.scene().views.empty()) {
                const auto &r = game.scene().views[0].refdef;
                char b[96];
                std::snprintf(b, sizeof b, "view at %.0f %.0f %.0f", bytesFloat(r, 24), bytesFloat(r, 28),
                              bytesFloat(r, 32));
                view = b;
            }
            const uint32_t ps = uint32_t(game.entityInt(0, game.entityLayout().client));
            std::printf("[%5d ms] camera %s (%.0f %.0f %.0f); %s; player at %.0f %.0f %.0f, pm_type %d; "
                        "%zu scene entities, %zu pictures; cg_thirdPerson %s\n",
                        game.levelTime(), camera ? "on" : "off", cam[0], cam[1], cam[2], view.c_str(), f(ps + 20),
                        f(ps + 24), f(ps + 28), int(cpu.read32(ps + 4)), game.scene().entities.size(),
                        game.pictures().size(), cvars.string("cg_thirdPerson").c_str());
            static size_t commandsShown = 0;
            for (; commandsShown < game.consoleCommands().size(); ++commandsShown)
                std::printf("           console command: %s", game.consoleCommands()[commandsShown].c_str());
        }
        return 0;
    };
    if (!replayPath.empty() || tourFrom > 0) {
        // -replay: the recorded commands in order, the server's frames run
        // up to each as the Play page ran them. -tour: a route made here.
        // Both: the tour without its level change, then the route (the
        // way out a level only gives by a ride, as ns_streets's car).
        std::ifstream file;
        std::string tour =
            tourFrom > 0 ? tourRoute(*bsp, tourFrom, tourRounds, spare, replayPath.empty(), tourCommands, commandsFrom)
                         : "";
        // -dumproute: the tour's route written out, to replay part of it.
        if (!dumpRoute.empty()) {
            std::ofstream out(dumpRoute);
            out << tour;
        }
        if (!replayPath.empty()) {
            file.open(replayPath);
            if (!file) {
                std::fprintf(stderr, "can't read %s\n", replayPath.c_str());
                return 1;
            }
            if (tourFrom > 0)
                tour += std::string(std::istreambuf_iterator<char>(file), {});
        }
        std::istringstream route(tour);
        std::istream &in = tourFrom > 0 ? static_cast<std::istream &>(route) : file;
        std::string line;
        int f = 0;
        int lastButtons = 0;
        // The commands for "@until": empty but for the buttons and weapon
        // given, every 50 ms up to that level time.
        std::vector<std::string> generated;
        size_t nextGenerated = 0;
        int32_t lastTime = 0;
        // "@aim name": the command's angles (65536 a turn) that turn the
        // view to it, kept for the "@until" lines after; 0 again once the
        // view is set some other way.
        int aimPitch = 0;
        int aimYaw = 0;
        // "@aimfrom x y z [yaw pitch]": the place @aim measures from
        // instead of the player's eye, and with angles, what the command's
        // angles turn from instead of the view's: a panel turret's facing,
        // as its aim is that plus the command's angles since it was taken
        // (with them 0 then). "@aimfrom none" the eye again.
        std::optional<q3::Vec3f> aimFrom;
        std::optional<std::array<float, 2>> aimBase;
        // "@repeat n" ... "@end": the lines between, n times over.
        std::deque<std::string> repeated;
        auto nextLine = [&](std::string &out) {
            if (nextGenerated < generated.size()) {
                out = generated[nextGenerated++];
                return true;
            }
            generated.clear();
            nextGenerated = 0;
            for (;;) {
                if (!repeated.empty()) {
                    out = repeated.front();
                    repeated.pop_front();
                } else if (!std::getline(in, out)) {
                    return false;
                }
                if (out.rfind("@repeat ", 0) != 0)
                    return true;
                const int times = std::atoi(out.c_str() + 8);
                std::vector<std::string> block;
                for (std::string l; std::getline(in, l) && l != "@end";)
                    block.push_back(l);
                for (int k = 0; k < times; ++k)
                    repeated.insert(repeated.end(), block.begin(), block.end());
            }
        };
        while (nextLine(line)) {
            if (line.empty() || line[0] == '#')
                continue;
            if (line.rfind("@until ", 0) == 0) {
                int until = 0;
                int buttons = 0;
                int weapon = 0;
                float pitch = 0;
                std::sscanf(line.c_str() + 7, "%d %d %d %f", &until, &buttons, &weapon, &pitch);
                // "@until +ms": that long from now, for a route following a tour.
                if (line[7] == '+')
                    until += std::max(lastTime, game.levelTime());
                // The pitch in the command's units (65536 a turn), up negative.
                const int pitchUnits = (aimPitch + int(std::lround(pitch * 65536 / 360))) & 0xffff;
                for (int32_t t = std::max(lastTime + 50, game.levelTime()); t < until; t += 50)
                    generated.push_back(std::to_string(t) + " " + std::to_string(buttons) + " " +
                                        std::to_string(weapon) + " " + std::to_string(pitchUnits) + " " +
                                        std::to_string(aimYaw & 0xffff) + " 0 0 0 0");
                continue;
            }
            // "@face name": the player put beside the living entity with
            // that name (any of its text fields), facing it. "@nearest name
            // -other...": the nearest such, but those with the other names.
            // "@where [name]": where the player is, with its health, and
            // each entity with that name (living or not: a switch), with
            // its server flags and frame (a switch's setting).
            if (line.rfind("@where", 0) == 0) {
                const ef::SpEntityLayout &layout = game.entityLayout();
                const auto show = [&](int32_t n) {
                    const q3::Vec3f at = game.entityVec(n, layout.currentOrigin);
                    std::printf("[%5d ms] entity %d at %.0f %.0f %.0f, health %d, flags 0x%x, frame %d\n",
                                game.levelTime(), n, double(at[0]), double(at[1]), double(at[2]),
                                jedi ? game.entityInt(n, kJediHealth) : 0, uint32_t(game.entityInt(n, layout.svFlags)),
                                game.entityInt(n, layout.modelIndex + 16));
                };
                show(0);
                if (line.size() > 7) {
                    const std::string want = reluva::fs::toLower(line.substr(7));
                    for (int32_t n = 1; n < game.entityCount(); ++n)
                        if (game.entityInt(n, layout.inUse) && entityHasText(game, n, want))
                            show(n);
                }
                continue;
            }
            if (line.rfind("@aimfrom ", 0) == 0) {
                q3::Vec3f from{};
                std::array<float, 2> base{};
                const int n =
                    std::sscanf(line.c_str() + 9, "%f %f %f %f %f", &from[0], &from[1], &from[2], &base[0], &base[1]);
                aimFrom.reset();
                aimBase.reset();
                if (n >= 3)
                    aimFrom = from;
                if (n == 5)
                    aimBase = base;
                continue;
            }
            if (line == "@aim none") {
                // Level again: needed before a setviewpos, which turns the
                // view from the command's angles as they are.
                aimPitch = aimYaw = 0;
                continue;
            }
            if (line.rfind("@aim ", 0) == 0) {
                // From the player's eye (Jedi Outcast's player state: the
                // origin at 0x14, delta_angles at 0x48) to the nearest
                // living entity with that name.
                const ef::SpEntityLayout &layout = game.entityLayout();
                const uint32_t ps = uint32_t(game.entityInt(0, layout.client));
                // Or a point, "@aim x y z": a switch has no health to be found by.
                q3::Vec3f point{};
                const bool atPoint = std::sscanf(line.c_str() + 5, "%f %f %f", &point[0], &point[1], &point[2]) == 3;
                const int32_t target = atPoint ? -1 : nearestNamed(game, line.substr(5));
                if (ps && (atPoint || target >= 0)) {
                    q3::Vec3f eye{};
                    for (uint32_t i = 0; i < 3; ++i) {
                        const uint32_t bits = game.cpu().read32(ps + 0x14 + 4 * i);
                        std::memcpy(&eye[i], &bits, 4);
                    }
                    if (aimFrom)
                        eye = {(*aimFrom)[0], (*aimFrom)[1], (*aimFrom)[2] - float(kEyeHeight)};
                    const q3::Vec3f lo = atPoint ? point : game.entityVec(target, layout.absMin);
                    const q3::Vec3f hi = atPoint ? point : game.entityVec(target, layout.absMax);
                    const double dx = (lo[0] + hi[0]) / 2 - eye[0];
                    const double dy = (lo[1] + hi[1]) / 2 - eye[1];
                    // Three quarters up its box: an AT-ST's middle is between its legs.
                    const double dz = lo[2] + (hi[2] - lo[2]) * 0.75 - (eye[2] + kEyeHeight);
                    const double yaw = std::atan2(dy, dx) * 180 / 3.14159265358979;
                    const double pitch = -std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 180 / 3.14159265358979;
                    const int32_t basePitch = aimBase ? int32_t(std::lround((*aimBase)[1] * 65536 / 360))
                                                      : int32_t(game.cpu().read32(ps + 0x48));
                    const int32_t baseYaw = aimBase ? int32_t(std::lround((*aimBase)[0] * 65536 / 360))
                                                    : int32_t(game.cpu().read32(ps + 0x4c));
                    aimPitch = int(std::lround(pitch * 65536 / 360)) - basePitch;
                    aimYaw = int(std::lround(yaw * 65536 / 360)) - baseYaw;
                }
                if (watch)
                    std::printf(
                        "[%5d ms] %s: entity %d, health %d, at %.0f %.0f %.0f\n", game.levelTime(), line.c_str(),
                        target, target >= 0 && jedi ? game.entityInt(target, kJediHealth) : 0,
                        target >= 0 ? double(game.entityVec(target, game.entityLayout().currentOrigin)[0]) : 0.0,
                        target >= 0 ? double(game.entityVec(target, game.entityLayout().currentOrigin)[1]) : 0.0,
                        target >= 0 ? double(game.entityVec(target, game.entityLayout().currentOrigin)[2]) : 0.0);
                continue;
            }
            // "@onto name [+height]": the player stood on top of the entity
            // with that name (living or not: a moving platform), or that
            // high above its bottom (inside a lift car with a roof).
            if (line.rfind("@onto ", 0) == 0) {
                const ef::SpEntityLayout &layout = game.entityLayout();
                std::istringstream words(reluva::fs::toLower(line.substr(6)));
                std::string want;
                std::string above;
                words >> want >> above;
                for (int32_t n = 1; n < game.entityCount(); ++n) {
                    if (!game.entityInt(n, layout.inUse) || !entityHasText(game, n, want))
                        continue;
                    const q3::Vec3f lo = game.entityVec(n, layout.absMin);
                    const q3::Vec3f hi = game.entityVec(n, layout.absMax);
                    char command[128];
                    const double z = above.size() > 1 && above[0] == '+' ? double(lo[2]) + std::atof(above.c_str() + 1)
                                                                         : double(hi[2]) + 25;
                    std::snprintf(command, sizeof command, "setviewpos %.0f %.0f %.0f 0", double((lo[0] + hi[0]) / 2),
                                  double((lo[1] + hi[1]) / 2), z);
                    aimPitch = aimYaw = 0;
                    if (watch)
                        std::printf("[%5d ms] %s: %s (entity %d)\n", game.levelTime(), line.c_str(), command, n);
                    if (reluva::Error e = game.clientConsoleCommand(command))
                        return failed(("command " + line).c_str(), e);
                    break;
                }
                continue;
            }
            const bool nearest = line.rfind("@nearest ", 0) == 0;
            if (line.rfind("@face ", 0) == 0 || nearest) {
                int32_t picked = -1;
                const std::string command =
                    facingCommand(game, line.substr(nearest ? 9 : 6), nearest, &mapWorld, &picked);
                aimPitch = aimYaw = 0;
                if (watch)
                    std::printf("[%5d ms] %s: %s (entity %d, health %d)\n", game.levelTime(), line.c_str(),
                                command.empty() ? "none" : command.c_str(), picked,
                                picked >= 0 && jedi ? game.entityInt(picked, kJediHealth) : 0);
                if (!command.empty())
                    if (reluva::Error e = game.clientConsoleCommand(command))
                        return failed(("command " + line).c_str(), e);
                continue;
            }
            // "/text": typed at the console then (cheats to test a level
            // with: god, give all, setviewpos...).
            if (line[0] == '/') {
                if (line.rfind("/setviewpos", 0) == 0)
                    aimPitch = aimYaw = 0;
                if (watch)
                    std::printf("[%5d ms] %s\n", game.levelTime(), line.c_str());
                if (reluva::Error e = game.clientConsoleCommand(line.substr(1)))
                    return failed(("command " + line).c_str(), e);
                continue;
            }
            ef::SpGameHost::UserCommand cmd;
            int weapon = 0;
            int forward = 0;
            int right = 0;
            int up = 0;
            if (std::sscanf(line.c_str(), "%d %d %d %d %d %d %d %d %d", &cmd.serverTime, &cmd.buttons, &weapon,
                            &cmd.angles[0], &cmd.angles[1], &cmd.angles[2], &forward, &right, &up) != 9)
                continue;
            lastTime = cmd.serverTime;
            cmd.weapon = uint8_t(weapon);
            cmd.forward = int8_t(forward);
            cmd.right = int8_t(right);
            cmd.up = int8_t(up);
            bool ran = false;
            while (game.levelTime() <= cmd.serverTime) {
                if (reluva::Error e = game.runFrame(false))
                    return failed(("frame at " + std::to_string(game.levelTime())).c_str(), e);
                ran = true;
            }
            if (ran)
                game.sendSnapshot(); // one for the frames run, as the Play page
            // A level change ends the replay there, as the engine changes
            // level at once (what -next carries over is the player then).
            bool changing = false;
            for (const std::string &c : game.consoleCommands()) {
                const std::string word = reluva::fs::toLower(c.substr(0, c.find(' ')));
                changing = changing || word == "maptransition" || word == "map";
            }
            if (changing)
                break;
            if (cmd.buttons != lastButtons && watch)
                std::printf("[%5d ms] buttons %d\n", cmd.serverTime, cmd.buttons);
            lastButtons = cmd.buttons;
            game.setWeaponSelect(cmd.weapon);
            if (game.clientGameRunning())
                if (int r = clientStep(cmd, f))
                    return r;
            ++f;
        }
        std::printf("replayed %d commands, to level time %d\n", f, game.levelTime());
    }
    for (int f = 0; replayPath.empty() && tourFrom <= 0 && f < frames; ++f) {
        if (reluva::Error e = game.runFrame())
            return failed(("frame " + std::to_string(f + 1)).c_str(), e);
        if (game.clientGameRunning()) {
            ef::SpGameHost::UserCommand cmd;
            cmd.serverTime = game.levelTime();
            if (int r = clientStep(cmd, f))
                return r;
        }
    }
    if (game.clientGameRunning()) {
        const q3::Scene &scene = game.scene();
        std::printf(
            "last client frame: %zu scene entities, %zu polygons, %zu lights, %zu scenes rendered, %zu pictures\n",
            scene.entities.size(), scene.polys.size(), scene.lights.size(), scene.views.size(), game.pictures().size());
        std::map<std::string, int> byModel;
        for (const q3::SceneEntity &e : scene.entities) {
            const int32_t m = ef::SpGameHost::entityModel(e);
            ++byModel[m ? game.models().name(m) : std::string("(no model)")];
        }
        for (const auto &[name, count] : byModel)
            std::printf("  %3d x %s\n", count, name.c_str());
        std::map<std::string, int> byShader;
        for (const q3::Picture &p : game.pictures())
            ++byShader[game.shaders().name(p.shader)];
        for (const auto &[name, count] : byShader)
            std::printf("  picture %3d x %s\n", count, name.c_str());
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (replayPath.empty() && tourFrom <= 0)
        std::printf("%d frames run (level time %d ms): ", frames, game.levelTime());
    else
        std::printf("played to level time %d ms: ", game.levelTime());
    std::printf("%d entities, %zu linked; %llu instructions in %.2f s\n", game.entityCount(), game.linked().size(),
                (unsigned long long)game.cpu().executed(), seconds);
    // -saveload: the game's own part of a saved game (WriteLevel), then a
    // fresh copy of the game started on the level as for loading it
    // (ReadLevel), as a check that what it writes reads back.
    if (saveLoad) {
        namespace efs = reluva::idtech3::eliteforce;
        namespace jk2 = reluva::idtech3::jk2;
        efs::SaveWriter w;
        reluva::Error e;
        if (game.jedi()) {
            // The whole saved game, as the engine writes it.
            jk2::SaveHeader header;
            header.comment = "---> " + map;
            header.map = map;
            jk2::writeSaveHeader(w, cvars, header);
            e = jk2::writeSaveLevel(w, game, false);
        } else {
            game.setSaveStream(&w, nullptr);
            e = game.writeLevel(false);
            game.setSaveStream(nullptr, nullptr);
        }
        if (e)
            return failed("WriteLevel", e), 1;
        std::printf("saved: %zu bytes from the game\n", w.bytes().size());
        {
            // Its chunks: each id in the order first written, how many and
            // their lengths (one, or the smallest and largest).
            const Bytes &out = w.bytes();
            std::vector<std::string> order;
            std::map<std::string, std::tuple<int, uint32_t, uint32_t>> seen;
            for (size_t at = 0; at + 12 <= out.size();) {
                uint32_t id;
                int32_t stored;
                std::memcpy(&id, out.data() + at, 4);
                std::memcpy(&stored, out.data() + at + 4, 4);
                // A compressed chunk: its length negated, then the length
                // compressed, which is what follows.
                uint32_t length = uint32_t(stored);
                size_t data = at + 12;
                if (stored < 0) {
                    length = uint32_t(-stored);
                    std::memcpy(&stored, out.data() + at + 8, 4);
                    data = at + 16;
                }
                const std::string name = reluva::idtech3::raven::chunkName(id);
                auto [it, added] = seen.try_emplace(name, 0, length, length);
                if (added)
                    order.push_back(name);
                auto &[count, low, high] = it->second;
                ++count;
                low = std::min(low, length);
                high = std::max(high, length);
                at = data + (data == at + 16 ? uint32_t(stored) : length) + 4;
            }
            std::printf("chunks:");
            for (const std::string &name : order) {
                const auto &[count, low, high] = seen[name];
                std::printf(" %s", name.c_str());
                if (count > 1)
                    std::printf("x%d", count);
                std::printf(low == high ? "(%u)" : "(%u-%u)", low, high);
            }
            std::printf("\n");
        }
        ef::SpGameHost loaded(set, cvars);
        if (fixedClock)
            loaded.clock = [&loaded] { return 10000u + uint32_t(loaded.levelTime()); };
        loaded.print = game.print;
        efs::SaveReader r(w.bytes());
        jk2::SaveHeader header;
        if (game.jedi() && (e = jk2::readSaveHeader(r, cvars, header)))
            return failed("Reading the saved game's header", e), 1;
        if ((e = loaded.load(dll, jedi)))
            return failed("Loading the DLL again", e), 1;
        loaded.setSaveStream(nullptr, &r);
        if ((e = loaded.startLevel(map, bsp, ef::SpGameHost::mapChecksum(data), 0, 1)))
            return failed("Starting the level to load", e), 1;
        e = game.jedi() ? jk2::readSaveLevel(r, loaded, false) : loaded.readLevel(false);
        if (e)
            return failed("ReadLevel", e), 1;
        loaded.setSaveStream(nullptr, nullptr);
        std::printf("loaded: %s\n", r.atEnd() ? "every chunk read back" : "chunks left over");
        std::string refused;
        if ((e = loaded.connectPlayer("\\name\\Munro\\model\\munro/main", refused, 1)))
            return failed("The player joining the loaded game", e), 1;
        for (int f = 0; f < 20; ++f)
            if ((e = loaded.runFrame()))
                return failed("A frame of the loaded game", e), 1;
        std::printf("the loaded game ran 20 frames: level time %d ms, %d entities\n", loaded.levelTime(),
                    loaded.entityCount());
    }
    // Entities with a text field (a pointer to text) containing what was
    // asked for: each such field's offset and text, and the entity's other
    // text fields, for telling what they are.
    if (!finds.empty()) {
        reluva::x86::Cpu &cpu = game.cpu();
        const int32_t size = game.entitySize();
        for (int32_t n = 0; n < game.entityCount(); ++n) {
            std::vector<std::pair<int32_t, std::string>> texts;
            bool match = false;
            for (int32_t off = 0; off + 4 <= size; off += 4) {
                const uint32_t p = uint32_t(game.entityInt(n, off));
                if (!cpu.valid(p, 1))
                    continue;
                const std::string t = cpu.string(p, 64);
                if (t.size() < 3 || !std::all_of(t.begin(), t.end(), [](char c) { return c > 32 && c < 127; }))
                    continue;
                texts.emplace_back(off, t);
                for (const std::string &f : finds)
                    match = match || reluva::fs::toLower(t).find(f) != std::string::npos;
            }
            if (!match)
                continue;
            std::printf("entity %d (in use %d, linked %d):", n, game.entityInt(n, game.entityLayout().inUse),
                        game.entityInt(n, game.entityLayout().linked));
            for (const auto &[off, t] : texts)
                std::printf(" +0x%x=\"%s\"", off, t.c_str());
            std::printf("\n");
        }
    }
    // The script system's names (efgamex86.dll 1.2: a VC6 std::map<string,
    // int> at 0x2027d760, its head node at +4): each node's left, parent and
    // right links, then the key (a VC6 string: its text pointer at +4, its
    // length at +8) and the entity number.
    if (icarusNames) {
        reluva::x86::Cpu &cpu = game.cpu();
        const uint32_t map = 0x2027d760 - uint32_t(game.module()->image.imageBase) + game.module()->base;
        const uint32_t head = cpu.read32(map + 4);
        // A node's key, or none for the tree's sentinel (no valid text).
        auto key = [&](uint32_t node, std::string &out) {
            if (!cpu.valid(node, 36) || node == head)
                return false;
            const uint32_t text = cpu.read32(node + 16);
            const uint32_t length = cpu.read32(node + 20);
            if (!cpu.valid(text, 1) || length > 255)
                return false;
            out.assign(reinterpret_cast<const char *>(cpu.memory().data() + text), length);
            return true;
        };
        // In order (left, node, right), checking each key is after the last.
        std::vector<std::pair<std::string, int32_t>> inOrder;
        std::set<uint32_t> visited;
        auto walk = [&](uint32_t node, int depth, auto &self) -> void {
            std::string k;
            if (depth > 64 || !key(node, k) || !visited.insert(node).second)
                return;
            self(cpu.read32(node), depth + 1, self);
            inOrder.emplace_back(k, int32_t(cpu.read32(node + 28)));
            self(cpu.read32(node + 8), depth + 1, self);
        };
        const uint32_t root = cpu.read32(head + 4);
        walk(root, 0, walk);
        std::printf("script names (map at %08x, head %08x, root %08x, size %u, %zu found in order):\n", map, head, root,
                    cpu.read32(map + 12), inOrder.size());
        for (size_t i = 0; i < inOrder.size(); ++i)
            std::printf("  %s -> %d%s\n", inOrder[i].first.c_str(), inOrder[i].second,
                        i && !(inOrder[i - 1].first < inOrder[i].first) ? "   <-- OUT OF ORDER" : "");
        // The search std::map::find makes, for each name asked for.
        for (const std::string &f : finds) {
            std::string want = f;
            for (char &c : want)
                c = char(std::toupper(static_cast<unsigned char>(c)));
            uint32_t node = root;
            uint32_t best = 0;
            std::string k;
            std::string path;
            while (key(node, k)) {
                path += " " + k;
                if (k < want) {
                    node = cpu.read32(node + 8);
                } else {
                    best = node;
                    node = cpu.read32(node);
                }
            }
            std::string bestKey;
            const bool found = best && key(best, bestKey) && bestKey == want;
            std::printf("  find %s: %s (path:%s)\n", want.c_str(), found ? "found" : "NOT FOUND", path.c_str());
        }
    }
    std::printf("configstrings:\n");
    int shown = 0;
    for (const auto &[index, text] : game.configstrings())
        if (shown++ < 40)
            std::printf("  %4d %s\n", index, text.substr(0, 100).c_str());
    for (const std::string &c : game.consoleCommands())
        std::printf("console command: %s", c.c_str());
    std::printf("server commands to the player: %zu\n", game.serverCommands().size());
    int commandsShown = 0;
    for (const auto &[client, text] : game.serverCommands())
        if (commandsShown++ < 60)
            std::printf("  to %d: %s\n", client, text.substr(0, 100).c_str());
    std::printf("engine calls:");
    for (const auto &[slot, count] : game.callCounts())
        std::printf(" %d:%d", slot, count);
    std::printf("\nclient game calls:");
    for (const auto &[n, count] : game.cgameCallCounts())
        std::printf(" %d:%d%s", n, count, game.cgameUnhandled().count(n) ? "?" : "");
    std::printf("  (? not answered yet)\n");
    if (!followNext)
        return 0;
    // -next: the last level change the game asked for, as the Play page
    // follows it: the player stored for a maptransition (forgotten for a
    // map), the next level started, the player joining, 100 frames run.
    std::string next;
    std::string target;
    bool carry = false;
    for (const std::string &c : game.consoleCommands()) {
        std::istringstream words(c);
        std::string command;
        std::string first;
        std::string second;
        words >> command >> first >> second;
        command = reluva::fs::toLower(command);
        if ((command == "maptransition" || command == "map") && !first.empty()) {
            next = reluva::fs::toLower(first);
            carry = command == "maptransition";
            target = carry ? second : std::string();
        }
    }
    if (next.empty()) {
        std::printf("next: the game asked for no level change\n");
        return 1;
    }
    if (carry)
        game.savePlayer();
    else
        cvars.set("playersave", "");
    std::printf("next: %s%s, the player carried over: %s |%s |%s |%s\n", next.c_str(),
                target.empty() ? "" : (" at " + target).c_str(), cvars.string("playersave").c_str(),
                cvars.string("playerammo").c_str(), cvars.string("playerinv").c_str(),
                cvars.string("playerfplvl").c_str());
    Bytes nextData;
    auto nextBsp = std::make_shared<q3::Bsp>();
    if (reluva::Error err = set.read("maps/" + next + ".bsp", nextData);
        err || (err = q3::parseBsp(nextData, *nextBsp)))
        return std::printf("maps/%s.bsp: %s\n", next.c_str(), err.message.c_str()), 1;
    ef::SpGameHost arrived(set, cvars);
    if (fixedClock)
        arrived.clock = [&arrived] { return 10000u + uint32_t(arrived.levelTime()); };
    std::tie(arrived.modInstall, arrived.modBaseDir) = idTech3Folders(argv[2]);
    arrived.print = game.print;
    arrived.spawnTarget = target;
    // Elite Force's Expansion Pack tells its game of the change; Jedi
    // Outcast's only for loadtransition (0x435293).
    arrived.loadTransition = carry && !jedi;
    std::string refused;
    if (reluva::Error e = arrived.load(dll, jedi))
        return std::printf("next: loading the DLL failed: %s\n", e.message.c_str()), 1;
    if (reluva::Error e = arrived.startLevel(next, nextBsp, ef::SpGameHost::mapChecksum(nextData), 0, 0))
        return std::printf("next: Init failed: %s\n", e.message.c_str()), 1;
    if (reluva::Error e = arrived.connectPlayer(userinfo, refused, 0))
        return std::printf("next: the player joining failed: %s\n", e.message.c_str()), 1;
    if (runCgame)
        if (reluva::Error e = arrived.startClientGame())
            return std::printf("next: the client game's start-up failed: %s\n", e.message.c_str()), 1;
    for (int f = 0; f < 100; ++f) {
        if (reluva::Error e = arrived.runFrame())
            return std::printf("next: frame %d failed: %s\n", f + 1, e.message.c_str()), 1;
        if (arrived.clientGameRunning()) {
            ef::SpGameHost::UserCommand cmd;
            cmd.serverTime = arrived.levelTime();
            if (reluva::Error e = arrived.clientThink(cmd))
                return std::printf("next: command %d failed: %s\n", f + 1, e.message.c_str()), 1;
            if (reluva::Error e = arrived.drawClientFrame(cmd.serverTime))
                return std::printf("next: client frame %d failed: %s\n", f + 1, e.message.c_str()), 1;
        }
    }
    // The player as they stand now, stored the same way, to set beside
    // what was carried over.
    arrived.savePlayer();
    std::printf("next: %s ran 100 frames (level time %d ms), %d entities; the player: %s |%s |%s |%s\n", next.c_str(),
                arrived.levelTime(), arrived.entityCount(), cvars.string("playersave").c_str(),
                cvars.string("playerammo").c_str(), cvars.string("playerinv").c_str(),
                cvars.string("playerfplvl").c_str());
    return 0;
}

// Prints a compiled ICARUS script (.IBI, Elite Force's single player
// scripts): its blocks (commands) and their members, the block ids named as
// the format's GPL release in Jedi Outcast numbers them.
int cmdIbiDump(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: reluva-tool idtech3 ibi-dump <game folder> <script, e.g. real_scripts/borg1/intro>\n");
        return 2;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::string name = argv[3];
    if (name.size() < 4 || reluva::fs::toLower(name.substr(name.size() - 4)) != ".ibi")
        name += ".IBI";
    Bytes d;
    if (reluva::Error err = set.read(name, d)) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), err.message.c_str());
        return 1;
    }
    if (d.size() < 8 || std::memcmp(d.data(), "IBI", 4) != 0) {
        std::fprintf(stderr, "%s isn't an IBI script\n", name.c_str());
        return 1;
    }
    auto i32 = [&](size_t at) {
        int32_t v = 0;
        if (at + 4 <= d.size())
            std::memcpy(&v, d.data() + at, 4);
        return v;
    };
    float version;
    std::memcpy(&version, d.data() + 4, 4);
    std::printf("%s: IBI version %.2f, %zu bytes\n", name.c_str(), version, d.size());
    static const char *const kIds[] = {"affect",  "sound",   "move",   "rotate", "wait",       "{",    "}",    "set",
                                       "loop",    "loopend", "print",  "use",    "flush",      "run",  "kill", "remove",
                                       "camera",  "get",     "random", "if",     "else",       "rem",  "task", "do",
                                       "declare", "free",    "dowait", "signal", "waitsignal", "play", "tag",  "eof"};
    static const char *const kTokens[] = {"undefined",  "comment", "eol", "char", "string", "int", "float",
                                          "identifier", "{",       "}",   "<",    ">",      "(",   ")",
                                          "vector",     ">",       "<",   "=",    "!"};
    auto idName = [&](int32_t id) -> std::string {
        if (id >= 19 && id < 19 + int32_t(sizeof kIds / sizeof kIds[0]))
            return kIds[id - 19];
        if (id >= 0 && id < int32_t(sizeof kTokens / sizeof kTokens[0]))
            return kTokens[id];
        return "#" + std::to_string(id);
    };
    size_t at = 8;
    int depth = 0;
    while (at + 9 <= d.size()) {
        const int32_t id = i32(at);
        const int32_t members = i32(at + 4);
        const uint8_t flags = d[at + 8];
        at += 9;
        if (id == 25) // block end
            depth = std::max(0, depth - 1);
        std::string line(size_t(2 * depth), ' ');
        line += idName(id) + "[" + std::to_string(id) + "]";
        if (flags)
            line += " flags " + std::to_string(flags);
        line += " (";
        for (int32_t m = 0; m < members && at + 8 <= d.size(); ++m) {
            const int32_t mid = i32(at);
            const int32_t size = i32(at + 4);
            at += 8;
            if (size < 0 || at + size_t(size) > d.size())
                break;
            std::string v;
            if (mid == 4 || mid == 7 || mid == 3) {
                v = "\"" +
                    std::string(reinterpret_cast<const char *>(d.data() + at),
                                strnlen(reinterpret_cast<const char *>(d.data() + at), size_t(size))) +
                    "\"";
            } else if (mid == 6 && size == 4) {
                float f;
                std::memcpy(&f, d.data() + at, 4);
                char b[32];
                std::snprintf(b, sizeof b, "%g", f);
                v = b;
            } else if (mid == 5 && size == 4) {
                v = std::to_string(i32(at));
            } else {
                v = idName(mid);
                if (size == 4)
                    v += "=" + std::to_string(i32(at));
                else if (size)
                    v += "(" + std::to_string(size) + " bytes)";
            }
            line += (m ? ", " : "") + v;
            at += size_t(size);
        }
        line += ")";
        std::printf("%s\n", line.c_str());
        if (id == 24) // block start
            ++depth;
    }
    return 0;
}

const Commands kCommands(
    "idtech3",
    {
        {"sp-menus",
         "<game folder> [-dll efuix86.dll] [-frames N] [-menu name] [-key frame key]... [-set name value]... [-fixedclock] [-skippak pak]...",
         "run Elite Force's single-player menus (efuix86.dll, in the x86 interpreter), with keys pressed; "
         "report what they draw and run",
         cmdSpMenus},
        {"sp-run",
         "<game folder> <map | -load saved game> [-dll efgamex86.dll] [-frames N] [-set name value]... [-trace] [-noclient] [-cgame] [-watch] [-find text]... [-icarus] [-saveload] [-replay file.input | -tour ms [-spare name]...] [-fixedclock] [-skippak pak]...",
         "run Elite Force's or Jedi Outcast's single-player game code (efgamex86.dll, jk2gamex86.dll, in the "
         "x86 interpreter) on a map, with a player; report what it does",
         cmdSpRun},
        {"ibi-dump", "<game folder> <script>",
         "print a compiled ICARUS script (.IBI): its blocks (commands) and their members", cmdIbiDump},
    });

} // namespace
