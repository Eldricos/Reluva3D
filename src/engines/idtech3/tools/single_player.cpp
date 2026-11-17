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
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/sp_ui_host.h"
#include "engines/idtech3/jk2/sp_savegame.h"
#include "engines/idtech3/raven/savegame.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
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

// For "@face name" and "@nearest name -other...": a setviewpos putting
// the player 96 units to one side of the living entity with that name
// (the nearest, for @nearest, but those with the other names), facing it;
// the first side not over a trigger_hurt, a pit, which kills through god
// mode. Empty if there's no such entity.
std::string facingCommand(ef::SpGameHost &game, const std::string &words, bool nearest) {
    std::istringstream in(reluva::fs::toLower(words));
    std::string want;
    in >> want;
    std::vector<std::string> skip;
    for (std::string w; in >> w;)
        if (w.size() > 1 && w[0] == '-')
            skip.push_back(w.substr(1));
    const ef::SpEntityLayout &layout = game.entityLayout();
    const q3::Vec3f me = game.entityVec(0, layout.currentOrigin);
    int32_t found = -1;
    float best = 0;
    std::vector<std::pair<q3::Vec3f, q3::Vec3f>> hurts;
    for (int32_t n = 1; n < game.entityCount(); ++n) {
        if (!game.entityInt(n, layout.inUse))
            continue;
        if (entityHasText(game, n, "trigger_hurt"))
            hurts.emplace_back(game.entityVec(n, layout.absMin), game.entityVec(n, layout.absMax));
        if ((!nearest && found >= 0) || !entityHasText(game, n, want) ||
            std::any_of(skip.begin(), skip.end(), [&](const std::string &s) { return entityHasText(game, n, s); }))
            continue;
        const q3::Vec3f at = game.entityVec(n, layout.currentOrigin);
        const float d =
            (at[0] - me[0]) * (at[0] - me[0]) + (at[1] - me[1]) * (at[1] - me[1]) + (at[2] - me[2]) * (at[2] - me[2]);
        if (found < 0 || d < best) {
            found = n;
            best = d;
        }
    }
    if (found < 0)
        return {};
    const q3::Vec3f at = game.entityVec(found, layout.currentOrigin);
    const float sides[4][3] = {{-96, 0, 0}, {96, 0, 180}, {0, -96, 90}, {0, 96, 270}};
    const float *side = sides[0];
    for (const float *s : sides) {
        const float x = at[0] + s[0];
        const float y = at[1] + s[1];
        const bool over = std::any_of(hurts.begin(), hurts.end(), [&](const auto &h) {
            return x > h.first[0] - 32 && x < h.second[0] + 32 && y > h.first[1] - 32 && y < h.second[1] + 32 &&
                   h.first[2] < at[2] + 64;
        });
        if (!over) {
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
// from its floor), and three seconds of fighting the nearest NPC, the
// weapon changing each time. Left out: trigger_hurt, triggers in or over
// one, and triggers naming a spared entity (an ally they'd move into
// the fighting); spared entities aren't shot at. Only direct fire (the
// saber, pistol, blaster, disruptor, bowcaster, repeater): splash would
// hurt the allies, whose death fails the mission. Last, the triggers
// that change the level.
std::string tourRoute(const q3::Bsp &bsp, int fromMs, const std::vector<std::string> &spare) {
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
    std::vector<std::array<float, 6>> hurts;
    for (const q3::Entity &e : entities)
        if (std::array<float, 6> b{}; e.classname() == "trigger_hurt" && bounds(e, b))
            hurts.push_back(b);
    std::set<std::string> levelChanges;
    for (const q3::Entity &e : entities)
        if (e.classname() == "target_level_change" && e.has("targetname"))
            levelChanges.insert(e.get("targetname"));
    std::vector<std::array<float, 6>> exits;
    std::string skip;
    for (const std::string &s : spare)
        skip += " -" + s;
    std::string out = "@until " + std::to_string(fromMs) + " 0 0\n/god\n/undying\n/give all\n";
    const int weapons[] = {4, 3, 4, 1, 4, 2, 4, 5, 4, 6}; // mostly the disruptor, which kills at a shot
    int t = fromMs;
    int count = 0;
    for (const q3::Entity &e : entities) {
        std::array<float, 6> b{};
        const std::string cls = e.classname();
        if (cls.rfind("trigger_", 0) != 0 || cls == "trigger_hurt" || !bounds(e, b))
            continue;
        if (levelChanges.count(e.get("target"))) {
            exits.push_back(b);
            continue;
        }
        bool named = false;
        for (const auto &[key, value] : e.pairs)
            for (const std::string &s : spare)
                named = named || reluva::fs::toLower(value).find(s) != std::string::npos;
        const float x = (b[0] + b[3]) / 2;
        const float y = (b[1] + b[4]) / 2;
        const float z = b[2] + 24;
        const bool hurt = std::any_of(hurts.begin(), hurts.end(), [&](const std::array<float, 6> &h) {
            return h[0] - 48 < x && x < h[3] + 48 && h[1] - 48 < y && y < h[4] + 48 && h[2] - 64 < z && z < h[5] + 64;
        });
        if (named || hurt)
            continue;
        const int w = weapons[count % int(std::size(weapons))];
        char line[160];
        std::snprintf(line, sizeof line, "# %d %s\n/setviewpos %.0f %.0f %.0f 0\n", count, cls.c_str(), double(x),
                      double(y), double(z));
        out += line;
        t += 300;
        out += "@until " + std::to_string(t) + " 0 " + std::to_string(w) + "\n";
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
        char line[128];
        std::snprintf(line, sizeof line, "# the level change\n/setviewpos %.0f %.0f %.0f 0\n",
                      double((b[0] + b[3]) / 2), double((b[1] + b[4]) / 2), double(b[2] + 24));
        out += line;
    }
    out += "@until " + std::to_string(t + 3000) + " 0 0\n";
    return out;
}

} // namespace

// Runs Elite Force's single-player menus (efuix86.dll, in the x86
// interpreter): GetUIAPI, Init, the main menu, then frames, with keys
// pressed along the way; reports what they drew and the commands they ran.
int cmdSpMenus(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool idtech3 sp-menus <game folder> [-dll efuix86.dll] [-frames N] "
                             "[-menu name] [-key frame key]... [-set name value]... [-fixedclock] [-skippak pak]...\n");
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
    std::vector<std::pair<int, int32_t>> keys; // (frame, key)
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
        if (dllPath.empty()) {
            std::fprintf(stderr, "efuix86.dll isn't beside %s; give it with -dll\n", argv[2]);
            return 1;
        }
    }
    Bytes dll;
    if (!readFile(dllPath.c_str(), dll))
        return 1;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set, skipPaks))
        return 1;
    q3::CvarStore cvars;
    for (const auto &[name, value] : settings)
        cvars.set(name, value);
    q3::KeyBindings bindings;
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
        if (f == 0 || f == frames - 1) {
            std::map<std::string, int> shown;
            for (const auto &p : ui.pictures())
                ++shown[ui.shaders().name(p.shader)];
            std::printf("[frame %d] %zu pictures, %zu scenes, catcher %d, fullscreen %d:\n", f, ui.pictures().size(),
                        ui.scene().views.size(), ui.catcher(), ui.fullscreen() ? 1 : 0);
            for (const auto &[name, count] : shown)
                std::printf("  %4d x %s\n", count, name.empty() ? "(none)" : name.c_str());
        }
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
            "[-set name value]... [-trace] [-noclient] [-cgame] [-watch] [-find text]... [-icarus] [-saveload] [-fixedclock] [-skippak pak]...\n"
            "       [-replay <name>.input | -tour <ms> [-spare name]...]\n"
            "       reluva-tool idtech3 sp-run <game folder> -load <saved game> [-replay <name>.input] [options as above]\n"
            "A replay's lines: \"time buttons weapon pitch yaw roll forward right up\" as the Play page records them;\n"
            "\"/command\" typed at the console; \"@until ms [buttons] [weapon]\" the same command every 50 ms until\n"
            "then; \"@face name\", \"@nearest name -other...\" the player put beside that entity, facing it.\n"
            "-tour: from then, every trigger touched and the nearest NPCs fought, with cheats (set helpUsObi 1).\n");
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
    std::string loadName;              // -load save: a saved game (its name in saves/, or a file)
    int tourFrom = 0;                  // -tour ms: from then, a route through the level's triggers (tourRoute)
    std::vector<std::string> spare;    // -spare name: not shot at, nor its triggers touched, on a tour
    std::string replayPath;            // -replay file: the player's commands, as the Play page records them,
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
        else if (std::strcmp(argv[i], "-load") == 0 && i + 1 < argc)
            loadName = argv[++i];
        else if (std::strcmp(argv[i], "-tour") == 0 && i + 1 < argc) {
            tourFrom = std::atoi(argv[++i]);
            runCgame = true;
        } else if (std::strcmp(argv[i], "-spare") == 0 && i + 1 < argc)
            spare.push_back(reluva::fs::toLower(argv[++i]));
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
    if (!noClient) {
        std::string refused;
        // The player as the Play page describes them (the sex's models).
        const bool male = reluva::fs::toLower(cvars.string("sex")).rfind('f', 0) != 0;
        const std::string userinfo = std::string("\\name\\Munro\\snaps\\20\\sex\\") + (male ? "male" : "female") +
                                     "\\headModel\\" + (male ? "munro/default" : "alexandria/default") +
                                     "\\torsoModel\\" + (male ? "hazard/default" : "hazardfemale/default") +
                                     "\\legsModel\\" + (male ? "hazard/default" : "hazardfemale/default") +
                                     "\\handicap\\100";
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
        std::ifstream file;
        std::istringstream route(tourFrom > 0 ? tourRoute(*bsp, tourFrom, spare) : std::string());
        if (tourFrom <= 0) {
            file.open(replayPath);
            if (!file) {
                std::fprintf(stderr, "can't read %s\n", replayPath.c_str());
                return 1;
            }
        }
        std::istream &in = tourFrom > 0 ? static_cast<std::istream &>(route) : file;
        std::string line;
        int f = 0;
        int lastButtons = 0;
        // The commands for "@until": empty but for the buttons and weapon
        // given, every 50 ms up to that level time.
        std::vector<std::string> generated;
        size_t nextGenerated = 0;
        int32_t lastTime = 0;
        auto nextLine = [&](std::string &out) {
            if (nextGenerated < generated.size()) {
                out = generated[nextGenerated++];
                return true;
            }
            generated.clear();
            nextGenerated = 0;
            return bool(std::getline(in, out));
        };
        while (nextLine(line)) {
            if (line.empty() || line[0] == '#')
                continue;
            if (line.rfind("@until ", 0) == 0) {
                int until = 0;
                int buttons = 0;
                int weapon = 0;
                std::sscanf(line.c_str() + 7, "%d %d %d", &until, &buttons, &weapon);
                for (int32_t t = std::max(lastTime + 50, game.levelTime()); t < until; t += 50)
                    generated.push_back(std::to_string(t) + " " + std::to_string(buttons) + " " +
                                        std::to_string(weapon) + " 0 0 0 0 0 0");
                continue;
            }
            // "@face name": the player put beside the living entity with
            // that name (any of its text fields), facing it. "@nearest name
            // -other...": the nearest such, but those with the other names.
            const bool nearest = line.rfind("@nearest ", 0) == 0;
            if (line.rfind("@face ", 0) == 0 || nearest) {
                const std::string command = facingCommand(game, line.substr(nearest ? 9 : 6), nearest);
                if (watch)
                    std::printf("[%5d ms] %s: %s\n", game.levelTime(), line.c_str(),
                                command.empty() ? "none" : command.c_str());
                if (!command.empty())
                    if (reluva::Error e = game.clientConsoleCommand(command))
                        return failed(("command " + line).c_str(), e);
                continue;
            }
            // "/text": typed at the console then (cheats to test a level
            // with: god, give all, setviewpos...).
            if (line[0] == '/') {
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
