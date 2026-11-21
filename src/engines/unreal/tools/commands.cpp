// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for Unreal Engine 1 games (Star Trek: Deep Space
// Nine - The Fallen): see docs/engines/unreal/README.md.

#include "devtools/tool.h"

//                                                  as the file it holds

#include "audio/decode.h"
#include "audio/wav.h"
#include "graphics/font.h"
#include "common/fs.h"

#include "engines/unreal/formats/game_save.h"
#include "engines/unreal/formats/actors.h"
#include "engines/unreal/formats/class_defaults.h"
#include "engines/unreal/formats/mesh.h"
#include "engines/unreal/games.h"
#include "engines/unreal/render/lighting.h"
#include "engines/unreal/render/procedural.h"
#include "engines/unreal/vm/script.h"
#include "engines/unreal/vm/script_text.h"
#include "engines/unreal/vm/vm.h"
#include "engines/unreal/vm/vm_level.h"
#include "engines/unreal/pages/viewers.h"
#include "engines/unreal/tools/soft_renderer.h"
#include "engines/unreal/game/collision.h"
#include "engines/unreal/game/zones.h"
#include "engines/unreal/formats/model.h"
#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/formats/properties.h"
#include "engines/unreal/formats/sound.h"
#include "engines/unreal/formats/texture.h"
#include <map>
#include <optional>
#include <memory>
#include <set>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using reluva::tool::Bytes;
using reluva::tool::hexdump;
using reluva::tool::parseNumber;
using reluva::tool::readFile;

bool openUnrealPackage(const char *file, reluva::unreal::Package &pkg) {
    if (reluva::Error err = pkg.open(file)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    return true;
}

// An export by number, or by name (its own or its dotted path).
// Classes and packages (texture groups) share names with what's in them.
bool isContainerClass(const std::string &cls) {
    const std::string c = reluva::fs::toLower(cls);
    return c == "class" || c == "package";
}

int findUnrealExport(const reluva::unreal::Package &pkg, const std::string &which) {
    if (!which.empty() && std::all_of(which.begin(), which.end(), [](char c) { return std::isdigit(uint8_t(c)); }))
        return std::atoi(which.c_str()) < int(pkg.exports().size()) ? std::atoi(which.c_str()) : -1;
    const std::string wanted = reluva::fs::toLower(which);
    // By path or name: a class and its mesh are often named alike, even in
    // one package (ARD_AmmoCrate), so a class (or package) is taken only if
    // nothing else has the name.
    int classIndex = -1;
    for (size_t i = 0; i < pkg.exports().size(); ++i) {
        if (reluva::fs::toLower(pkg.path(int32_t(i) + 1)) != wanted &&
            reluva::fs::toLower(pkg.objectName(int32_t(i) + 1)) != wanted)
            continue;
        if (!isContainerClass(pkg.className(int32_t(i) + 1)))
            return int(i);
        if (classIndex < 0)
            classIndex = int(i);
    }
    return classIndex >= 0 ? classIndex : pkg.findExport(which);
}

// A saved game of the original's (a .dsa package): its Level object's
// extras (ghosts, dialogue said, sounds playing) and its C3D mesh states.
int cmdUeSave(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool unreal save <saved game .dsa>\n");
        return 2;
    }
    reluva::unreal::Package pkg;
    if (!openUnrealPackage(argv[2], pkg))
        return 1;
    reluva::unreal::LevelExtras x;
    if (reluva::Error err = reluva::unreal::readLevelExtras(pkg, x)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    int said = 0;
    for (const auto &l : x.lines)
        said += l.said > 0;
    std::printf("map %s, portal %s; %zu actors, %zu ghosts; %zu dialogue lines (%d said); %zu sounds playing\n",
                x.urlMap.c_str(), x.urlPortal.c_str(), x.actors.size(), x.ghosts.size(), x.lines.size(), said,
                x.sounds.size());
    for (const auto &l : x.lines)
        if (l.said > 0)
            std::printf("  said %dx: %s\n", l.said, l.key.c_str());
    std::printf("dialogue under way: %zu lines; next thread %d, %d cameras\n", x.talks.size(), int(x.nextThread),
                x.cameras);
    for (const auto &t : x.talks)
        std::printf("  thread %d: line %d%s, back %d, done at %.2f, speaker %d\n", int(t.thread), t.line,
                    t.queued ? " (waiting)" : "", t.back, double(t.end), int(t.speaker));
    for (const auto &s : x.sounds)
        std::printf("  sound %s on %s, id %d, volume %.2f, radius %.0f, pitch %.2f, from %.2f s\n", s.path.c_str(),
                    pkg.path(s.actor).c_str(), s.id, s.volume, s.radius, s.pitch, s.start);
    int states = 0;
    int failed = 0;
    for (int i : pkg.exportsOfClass("C3DMESHSTATE")) {
        reluva::unreal::MeshState m;
        if (reluva::unreal::readMeshState(pkg, i, m)) {
            ++failed;
            continue;
        }
        ++states;
        if (m.groups.size() > 1) {
            std::printf("  %s (%s):", pkg.path(i + 1).c_str(), m.mesh.c_str());
            for (const auto &g : m.groups)
                std::printf(" %s=%s@%.3f/t%d/s%d/w%.2f", g.group.c_str(), g.sequence.c_str(), g.frame, g.type, g.state,
                            g.weight);
            std::printf("\n");
        }
    }
    std::printf("%d mesh states read, %d not\n", states, failed);
    return 0;
}

int cmdUeInfo(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool unreal info <package>\n");
        return 2;
    }
    reluva::unreal::Package pkg;
    if (!openUnrealPackage(argv[2], pkg))
        return 1;
    std::printf("%s: version %d, licensee %d, flags %08x, %llu bytes\n", argv[2], pkg.version(), pkg.licensee(),
                pkg.flags(), static_cast<unsigned long long>(pkg.fileSize()));
    std::printf("%zu names, %zu imports, %zu exports\n", pkg.names().size(), pkg.imports().size(),
                pkg.exports().size());
    std::map<std::string, std::pair<int, long long>> classes;
    for (size_t i = 0; i < pkg.exports().size(); ++i) {
        auto &c = classes[pkg.className(int32_t(i) + 1)];
        ++c.first;
        c.second += pkg.exports()[i].serialSize;
    }
    std::printf("\nExports by class:\n");
    for (const auto &[cls, n] : classes)
        std::printf("  %6d  %-28s %12lld bytes\n", n.first, cls.c_str(), n.second);
    std::map<std::string, int> from;
    for (size_t i = 0; i < pkg.imports().size(); ++i)
        ++from[pkg.importPackage(int(i))];
    std::printf("\nImports by package:\n");
    for (const auto &[p, n] : from)
        std::printf("  %6d  %s\n", n, p.c_str());
    return 0;
}

int cmdUeExports(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: reluva-tool unreal exports <package> [class or name filter]\n");
        return 2;
    }
    reluva::unreal::Package pkg;
    if (!openUnrealPackage(argv[2], pkg))
        return 1;
    const std::string filter = argc == 4 ? reluva::fs::toLower(argv[3]) : "";
    for (size_t i = 0; i < pkg.exports().size(); ++i) {
        const auto &e = pkg.exports()[i];
        const std::string cls = pkg.className(int32_t(i) + 1);
        const std::string path = pkg.path(int32_t(i) + 1);
        if (!filter.empty() && reluva::fs::toLower(cls + " " + path).find(filter) == std::string::npos)
            continue;
        std::printf("%6zu  %-24s %-48s %9d @ %-9d flags %08x\n", i, cls.c_str(), path.c_str(), e.serialSize,
                    e.serialOffset, e.flags);
    }
    return 0;
}

int cmdUeProps(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool unreal props <package> <export number | name | Group.Name>\n");
        return 2;
    }
    using namespace reluva::unreal;
    Package pkg;
    if (!openUnrealPackage(argv[2], pkg))
        return 1;
    const int index = findUnrealExport(pkg, argv[3]);
    if (index < 0) {
        std::fprintf(stderr, "No export %s\n", argv[3]);
        return 1;
    }
    std::vector<uint8_t> data;
    if (reluva::Error err = pkg.readExport(index, data)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const Export &e = pkg.exports()[size_t(index)];
    std::printf("%d: %s %s, %d bytes at %d, flags %08x\n", index, pkg.className(index + 1).c_str(),
                pkg.path(index + 1).c_str(), e.serialSize, e.serialOffset, e.flags);
    // A class: its own layout (script.md), its defaults read with it.
    if (e.classRef == 0) {
        ScriptObject o;
        if (reluva::Error err = readScriptObject(pkg, index, o)) {
            std::printf("Class: %s\n", err.message.c_str());
            return 1;
        }
        std::printf("  class flags %08x, within %s, config %s; defaults:\n", o.classFlags,
                    o.within ? pkg.path(o.within).c_str() : "-", o.configName ? pkg.name(o.configName).c_str() : "-");
        for (const Property &p : o.defaults.items)
            std::printf("  %s%s (%s) = %s\n", p.name.c_str(),
                        p.arrayIndex ? ("[" + std::to_string(p.arrayIndex) + "]").c_str() : "",
                        p.structName.empty() ? typeName(p.type) : p.structName.c_str(), describe(pkg, p).c_str());
        return 0;
    }
    Reader r(data, size_t(e.serialOffset));
    PropertyList props;
    if (reluva::Error err = beginObject(pkg, index, r, props))
        std::printf("Properties: %s\n", err.message.c_str());
    for (const Property &p : props.items)
        std::printf("  %s%s (%s) = %s\n", p.name.c_str(),
                    p.arrayIndex ? ("[" + std::to_string(p.arrayIndex) + "]").c_str() : "",
                    p.structName.empty() ? typeName(p.type) : p.structName.c_str(), describe(pkg, p).c_str());
    // The class's own data, from where the properties end.
    const size_t start = r.pos() - size_t(e.serialOffset);
    std::printf("Then %zu bytes of the class's own data, from file offset %zu:\n", data.size() - start, r.pos());
    for (size_t row = start; row < data.size() && row < start + 128; row += 16) {
        std::printf("  %08zx ", row + size_t(e.serialOffset));
        for (size_t k = row; k < row + 16 && k < data.size(); ++k)
            std::printf(" %02x", data[k]);
        std::printf("\n");
    }
    return 0;
}

// Runs a level's scripts (vm_level.h): its actors made, the game type
// spawned, the start-up events sent, then `seconds` of ticks at 30 a
// second. Reports what the scripts did and what's missing: errors,
// natives not written yet, None accessed. -all: every map, totalled.
int cmdUeRun(int argc, char **argv) {
    if (argc < 4 || argc > 5) {
        std::fprintf(stderr, "usage: reluva-tool unreal run <game folder> <map | -all> [seconds]\n");
        return 2;
    }
    using namespace reluva::unreal;
    PackageSet set;
    if (reluva::Error err = set.open(argv[2], "DS9.ini")) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const float seconds = argc == 5 ? float(std::atof(argv[4])) : 5.0f;
    const bool all = std::strcmp(argv[3], "-all") == 0;
    std::vector<std::string> maps = all ? set.mapNames() : std::vector<std::string>{argv[3]};
    std::map<std::string, int> missing;
    std::map<std::string, int> errors;
    std::map<std::string, int> noneAt;
    int placedGhosts = 0;
    int ghostsBack = 0;
    int localizedSet = 0;
    int ran = 0;
    int failed = 0;
    uint64_t calls = 0;
    uint64_t statements = 0;
    int accessedNone = 0;
    ScriptLevel::MovementStats moved;
    ScriptLevel::PathStats paths;
    ScriptLevel::DialogStats said;
    for (const std::string &name : maps) {
        const Package *map = set.get(name);
        if (!map) {
            std::printf("%s: %s\n", name.c_str(), set.error(name).c_str());
            ++failed;
            continue;
        }
        Vm vm(set);
        std::vector<std::string> logs;
        vm.logger = [&](const std::string &line) { logs.push_back(line); };
        ScriptLevel level(vm, *map);
        const auto t0 = std::chrono::steady_clock::now();
        if (reluva::Error err = level.load()) {
            std::printf("%s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        const size_t before = level.actors().size();
        std::string loginError;
        VmObject *player = nullptr;
        if (level.savedByGame()) { // a saved game of the original's
            player = level.resumeSavedGame(iniSetting(set, "URL", "Name"), loginError);
        } else {
            level.beginPlay(defaultGameType(set));
            player = level.login(iniSetting(set, "URL", "Class"), iniSetting(set, "URL", "Name"), loginError);
        }
        const auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < int(seconds * 30); ++i)
            level.tick(1.0f / 30);
        const auto t2 = std::chrono::steady_clock::now();
        ++ran;
        // What changed that shows: actors hidden or gone, lights out.
        int hidden = 0;
        int dark = 0;
        for (VmObject *a : level.actors()) {
            if (Value *v = a->find(vm, "DrawType"); v && v->i == 0)
                ++hidden;
            if (vm.isA(a, "Light"))
                if (Value *v = a->find(vm, "LightBrightness"); v && v->i == 0)
                    ++dark;
        }
        // The player as it is after the run: its state and HUD.
        std::string who = "none: " + loginError;
        if (player) {
            const Value *hud = player->find(vm, "myHUD");
            who = player->cls->name + " (state " + (player->state ? player->state->name : std::string("none")) +
                  ", HUD " + (hud && hud->asObject() ? hud->o->cls->name : std::string("none")) + ")";
        }
        std::printf("%-20s player %s; ", name.c_str(), who.c_str());
        std::printf(
            "%5zu actors (%zu after %.0f s), game %s; %d drawn as nothing, %d lights at brightness 0; "
            "started in %.3f s, %.0f s run in %.3f s; %llu calls, %d None accessed, %zu errors, %zu natives missing\n",
            before, level.actors().size(), double(seconds), level.game() ? level.game()->cls->name.c_str() : "none",
            hidden, dark, std::chrono::duration<double>(t1 - t0).count(), double(seconds),
            std::chrono::duration<double>(t2 - t1).count(), (unsigned long long)vm.stats.calls, vm.stats.accessedNone,
            vm.stats.errors.size(), vm.stats.missingNatives.size());
        // Movement against the level: how actors moved, and where the player is.
        const auto &m = level.movement;
        std::string where;
        if (player)
            if (const Value *phys = player->find(vm, "Physics"))
                where = ", the player's physics " + std::to_string(phys->asInt());
        std::printf("%-20s movement: %d moves, blocked %d by the world and %d by actors, %d steps, %d landings, "
                    "%d touches, %d zone changes, %d placings refused (%d spawns), %d moves to a goal given up%s\n",
                    "", m.moves, m.blockedByWorld, m.blockedByActors, m.steps, m.landings, m.touches, m.zoneChanges,
                    m.refused, m.spawnsRefused, m.gaveUp, where.c_str());
        const auto &pf = level.paths;
        if (pf.straight + pf.found + pf.failed > 0)
            std::printf("%-20s paths: %zu navigation points, %zu reaches; %d ways found straight, %d along paths, "
                        "%d not (%d with no point near, %d none near the goal, %d no way between)\n",
                        "", level.navigationPoints().size(), level.reachSpecs().size(), pf.straight, pf.found,
                        pf.failed, pf.noStart, pf.noEnd, pf.noRoute);
        if (level.noise.made > 0)
            std::printf("%-20s noise: %d made, heard %d times\n", "", level.noise.made, level.noise.heard);
        const auto &dg = level.dialog;
        if (dg.started + dg.notFound + dg.barks > 0)
            std::printf("%-20s dialogue: %zu lines read; %d started (%d lines said), %d with no line to say, "
                        "%d conditions not known, %d of the AI's own\n",
                        "", level.dialogLines().size(), dg.started, dg.lines, dg.notFound, dg.unknownConditions,
                        dg.barks);
        placedGhosts += level.placedGhosts();
        localizedSet += level.localizedSet();
        ghostsBack += level.placedGhosts() - int(std::count_if(level.ghosts().begin(), level.ghosts().end(),
                                                               [](const VmObject *g) { return !g->deleted; }));
        said.started += dg.started;
        said.lines += dg.lines;
        said.notFound += dg.notFound;
        said.barks += dg.barks;
        paths.straight += pf.straight;
        paths.found += pf.found;
        paths.failed += pf.failed;
        moved.moves += m.moves;
        moved.blockedByWorld += m.blockedByWorld;
        moved.blockedByActors += m.blockedByActors;
        moved.steps += m.steps;
        moved.landings += m.landings;
        moved.touches += m.touches;
        moved.zoneChanges += m.zoneChanges;
        moved.refused += m.refused;
        moved.spawnsRefused += m.spawnsRefused;
        moved.gaveUp += m.gaveUp;
        calls += vm.stats.calls;
        statements += vm.stats.statements;
        accessedNone += vm.stats.accessedNone;
        for (const auto &[k, v] : vm.stats.missingNatives)
            missing[k] += v;
        for (const auto &[k, v] : vm.stats.errors)
            errors[k] += v;
        for (const auto &[k, v] : vm.stats.noneAt)
            noneAt[k] += v;
        if (!all) {
            // The scripts' log, repeated lines counted.
            std::vector<std::pair<std::string, int>> lines;
            for (const std::string &l : logs) {
                auto it = std::find_if(lines.begin(), lines.end(), [&](const auto &e) { return e.first == l; });
                if (it == lines.end())
                    lines.push_back({l, 1});
                else
                    ++it->second;
            }
            for (size_t k = 0; k < lines.size() && k < 40; ++k)
                std::printf("  log: %s%s\n", lines[k].first.c_str(),
                            lines[k].second > 1 ? (" (x" + std::to_string(lines[k].second) + ")").c_str() : "");
            if (lines.size() > 40)
                std::printf("  log: ... %zu more lines\n", lines.size() - 40);
        }
    }
    auto top = [](const std::map<std::string, int> &m, size_t n) {
        std::vector<std::pair<int, std::string>> v;
        for (const auto &[k, c] : m)
            v.push_back({c, k});
        std::sort(v.rbegin(), v.rend());
        if (v.size() > n)
            v.resize(n);
        return v;
    };
    std::printf("\n%d maps ran, %d didn't load; %llu calls, %llu statements, None accessed %d times\n", ran, failed,
                (unsigned long long)calls, (unsigned long long)statements, accessedNone);
    std::printf("movement: %d moves, blocked %d by the world and %d by actors, %d steps, %d landings, %d touches, "
                "%d zone changes, %d placings refused (%d spawns), %d moves to a goal given up\n",
                moved.moves, moved.blockedByWorld, moved.blockedByActors, moved.steps, moved.landings, moved.touches,
                moved.zoneChanges, moved.refused, moved.spawnsRefused, moved.gaveUp);
    std::printf("paths: %d ways found straight, %d along paths, %d not\n", paths.straight, paths.found, paths.failed);
    std::printf("ghosts: %d placed as ghosts set aside at the start, about %d brought back since\n", placedGhosts,
                std::max(0, ghostsBack));
    std::printf("actors' strings from the maps' .int: %d set\n", localizedSet);
    std::printf("dialogue: %d started (%d lines said), %d with no line to say, %d of the AI's own\n", said.started,
                said.lines, said.notFound, said.barks);
    std::printf("natives not written yet (%zu, by calls):\n", missing.size());
    for (const auto &[n, k] : top(missing, all ? 60 : 40))
        std::printf("  %7d %s\n", n, k.c_str());
    // Script errors in full; None accessed (mostly scripts looking for a
    // player, as none has joined) the most common.
    std::map<std::string, int> none;
    std::map<std::string, int> other;
    for (const auto &[k, n] : errors)
        (k.rfind("Accessed None", 0) == 0 ? none : other)[k] = n;
    std::printf("errors (%zu kinds):\n", other.size());
    for (const auto &[n, k] : top(other, 1000))
        std::printf("  %7d %s\n", n, k.c_str());
    std::printf("None accessed (%zu places, the most):\n", none.size());
    for (const auto &[n, k] : top(none, all ? 15 : 10))
        std::printf("  %7d %s\n", n, k.c_str());
    // Where in them (the context expression's offset, as script lists the code).
    std::printf("None accessed, by place in the code (the most):\n");
    for (const auto &[n, k] : top(noneAt, all ? 20 : 10))
        std::printf("  %7d %s\n", n, k.c_str());
    return failed ? 1 : 0;
}

// The Maps page played without a window: a System whose 3D is the software
// renderer, so a page's frames can be saved and looked at.
class HeadlessSystem : public reluva::System {
public:
    explicit HeadlessSystem(reluva::tools::SoftRenderer3D &renderer) : _renderer(renderer) {}
    reluva::Error initGraphics(int, int, reluva::PixelFormat) override { return reluva::Error::success(); }
    void setPalette(const reluva::Color *, int, int) override {}
    void updateScreen(const reluva::Surface &) override {}
    void setWindowTitle(const std::string &title) override { _title = title; }
    reluva::Error init3D(int width, int height, reluva::gfx::Renderer3D *&out) override {
        if (width != _renderer.width() || height != _renderer.height())
            return reluva::Error::fail("the software renderer is another size");
        out = &_renderer;
        return reluva::Error::success();
    }
    bool pollEvent(reluva::Event &) override { return false; }
    uint64_t getMillis() const override { return 0; }
    void delayMillis(uint32_t) override {}
    reluva::Error startAudio(AudioCallback) override { return reluva::Error::fail("no audio"); }
    void stopAudio() override {}
    int audioRate() const override { return 0; }
    const std::string &title() const { return _title; }

private:
    reluva::tools::SoftRenderer3D &_renderer;
    std::string _title;
};

// play <game folder> <map> [step...]: the Maps page on that map, driven
// by steps, its frames drawn in software. Steps: play (Enter), wait <s>,
// travel <url> (to "Map/Portal" and playing, as the game's ClientTravel;
// "?load=Save1.dss" a saved game), saves <dir> (where games are saved),
// config <dir> (where settings are written), savegame <name> (the level
// saved now: QuickSave, else named as the game names its saves), wav
// <file.wav> (the sound heard since the last, mixed offline), sounds
// (what's playing), hold <key> <s> (w a s d left right up down space
// enter escape f), down <key> and up <key> (held across other steps),
// press <key>, watch <actor> (the camera let loose in front of it),
// teleport <actor> <dx> <dy> <dz> <yaw> (the player put by it, facing yaw
// degrees), where (the player's place, state, physics and animation),
// camera <x> <y> <z> <yaw> <pitch> (the fly camera put there, not
// playing), places <class part> (the live actors of such classes and
// where they are), prop <actor> <property> and call <actor> <function>
// ("player": the player's pawn), drag <dx> <dy>, click <x> <y> (the left
// button, there), shot <file.ppm> (the next frame, saved).
int cmdUePlay(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(
            stderr,
            "usage: reluva-tool unreal play <game folder> <map> [play | wait <s> | hold <key> <s> | "
            "teleport <actor> <dx> <dy> <dz> <yaw> | camera <x> <y> <z> <yaw> <pitch> | places <class> | where | view | lines | actor <name> | prop <actor> <property> | call <actor> <function> | console <command> | down <key> | up <key> | press <key> | travel <url> [items] | saves <dir> | config <dir> | watch <actor> | "
            "drag <dx> <dy> | click <x> <y> | shot <file.ppm>]...\n");
        return 2;
    }
    using namespace reluva::unreal;
    PackageSet set;
    if (reluva::Error err = set.open(argv[2], "DS9.ini")) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::tools::SoftRenderer3D renderer(960, 720);
    renderer.drawing = false;
    HeadlessSystem sys(renderer);
    reluva::Surface screen;
    screen.create(960, 720, reluva::PixelFormat::kRGBA32);
    std::vector<std::string> warnings;
    const GameInfo *game = findGame("ds9fallen");
    // The sound mixed offline, a frame's worth each frame, for wav.
    reluva::audio::Mixer mixer(22050);
    std::vector<int16_t> heard;
    ViewContext ctx{sys, screen, set, *game, warnings, &mixer};
    LevelView page(ctx);
    if (!page.selectMap(argv[3])) {
        std::fprintf(stderr, "no map %s\n", argv[3]);
        return 1;
    }
    page.activate();
    uint64_t now = 1000;
    auto frame = [&] {
        now += 33;
        page.update(now);
        const size_t at = heard.size();
        heard.resize(at + 735 * 2); // a thirtieth of a second at 22050 Hz, stereo
        mixer.mix(heard.data() + at, 735);
    };
    auto keyOf = [](const std::string &k) -> uint32_t {
        if (k == "left")
            return reluva::key::kLeft;
        if (k == "right")
            return reluva::key::kRight;
        if (k == "up")
            return reluva::key::kUp;
        if (k == "down")
            return reluva::key::kDown;
        if (k == "space")
            return reluva::key::kSpace;
        if (k == "enter")
            return reluva::key::kReturn;
        if (k == "escape")
            return reluva::key::kEscape;
        if (k.size() >= 2 && (k[0] == 'f' || k[0] == 'F') && std::isdigit(uint8_t(k[1])))
            return reluva::key::kF1 + uint32_t(std::atoi(k.c_str() + 1) - 1);
        return k.empty() ? 0 : uint32_t(k[0]);
    };
    auto send = [&](reluva::EventType type, uint32_t key) {
        reluva::Event ev;
        ev.type = type;
        ev.key = key;
        page.handleEvent(ev);
    };
    frame();
    for (int i = 4; i < argc; ++i) {
        const std::string step = argv[i];
        const auto t0 = std::chrono::steady_clock::now();
        if (step == "play") {
            send(reluva::EventType::kKeyDown, reluva::key::kReturn);
            send(reluva::EventType::kKeyUp, reluva::key::kReturn);
            std::printf("play: %s\n", page.playing() ? "playing" : "didn't start");
        } else if (step == "wait" && i + 1 < argc) {
            const float s = float(std::atof(argv[++i]));
            for (int f = 0; f < int(s * 30); ++f)
                frame();
        } else if (step == "hold" && i + 2 < argc) {
            const uint32_t key = keyOf(argv[++i]);
            const float s = float(std::atof(argv[++i]));
            send(reluva::EventType::kKeyDown, key);
            for (int f = 0; f < int(s * 30); ++f)
                frame();
            send(reluva::EventType::kKeyUp, key);
        } else if (step == "sounds") {
            for (const std::string &s : page.sounds())
                std::printf("  %s\n", s.c_str());
        } else if (step == "wav" && i + 1 < argc) {
            // What was heard since the last wav (or the start), as a WAVE
            // file, and how loud each second of it was.
            const std::string file = argv[++i];
            std::FILE *out = std::fopen(file.c_str(), "wb");
            if (!out) {
                std::fprintf(stderr, "can't write %s\n", file.c_str());
                return 1;
            }
            auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, out); };
            auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, out); };
            const uint32_t bytes = uint32_t(heard.size() * 2);
            std::fwrite("RIFF", 1, 4, out);
            u32(36 + bytes);
            std::fwrite("WAVEfmt ", 1, 8, out);
            u32(16), u16(1), u16(2), u32(22050), u32(22050 * 4), u16(4), u16(16);
            std::fwrite("data", 1, 4, out);
            u32(bytes);
            std::fwrite(heard.data(), 2, heard.size(), out);
            std::fclose(out);
            std::printf("wav %s: %.1f s; loudest (RMS) each second:", file.c_str(), double(heard.size()) / 2 / 22050);
            for (size_t s = 0; s < heard.size(); s += 22050 * 2) {
                double sum = 0;
                const size_t end = std::min(heard.size(), s + 22050 * 2);
                for (size_t k = s; k < end; ++k)
                    sum += double(heard[k]) * heard[k];
                std::printf(" %.0f", std::sqrt(sum / double(end - s)));
            }
            std::printf("\n");
            heard.clear();
        } else if (step == "savegame" && i + 1 < argc) {
            const reluva::Error err = page.saveGame(argv[++i]);
            std::printf("savegame: %s\n", err ? err.message.c_str() : "saved");
        } else if (step == "saves" && i + 1 < argc) {
            ctx.saveFolder = argv[++i];
        } else if (step == "config" && i + 1 < argc) {
            ctx.configFolder = argv[++i];
        } else if (step == "travel" && i + 1 < argc) {
            const std::string url = argv[++i];
            // "items" after it: with the player's things (bItems).
            const bool items = i + 1 < argc && std::strcmp(argv[i + 1], "items") == 0;
            if (items)
                ++i;
            page.travel(url, items);
            std::printf("travel: %s\n", page.playing() ? "playing" : "didn't start");
            frame();
        } else if (step == "watch" && i + 1 < argc) {
            const std::string who = argv[++i];
            std::printf("watch %s: %s\n", who.c_str(), page.watchActor(who) ? "looking at it" : "no such actor");
            frame();
        } else if (step == "teleport" && i + 5 < argc) {
            const std::string who = argv[++i];
            const reluva::unreal::Vec3f off{float(std::atof(argv[i + 1])), float(std::atof(argv[i + 2])),
                                            float(std::atof(argv[i + 3]))};
            const float yaw = float(std::atof(argv[i + 4]));
            i += 4;
            std::printf("teleport %s: %s\n", who.c_str(),
                        page.teleportPlayer(who, off, yaw) ? "there" : "no such actor");
            frame();
        } else if (step == "camera" && i + 5 < argc) {
            const reluva::unreal::Vec3f at{float(std::atof(argv[i + 1])), float(std::atof(argv[i + 2])),
                                           float(std::atof(argv[i + 3]))};
            page.placeCamera(at, float(std::atof(argv[i + 4])), float(std::atof(argv[i + 5])));
            i += 5;
            frame();
        } else if (step == "places" && i + 1 < argc) {
            for (const auto &[name, at] : page.actorPlaces(argv[++i]))
                std::printf("  place %s %.1f %.1f %.1f\n", name.c_str(), double(at[0]), double(at[1]), double(at[2]));
        } else if (step == "console" && i + 1 < argc) {
            const std::string command = argv[++i];
            std::printf("console %s: %s\n", command.c_str(), page.consoleCommand(command).c_str());
            frame();
        } else if (step == "call" && i + 2 < argc) {
            const std::string who = argv[++i];
            const std::string what = argv[++i];
            std::printf("call %s.%s: %s\n", who.c_str(), what.c_str(),
                        page.callFunction(who, what) ? "done" : "no such actor");
            frame();
        } else if (step == "actor" && i + 1 < argc) {
            const std::string who = argv[++i];
            std::printf("actor %s: %s\n", who.c_str(), page.describeActor(who).c_str());
        } else if (step == "prop" && i + 2 < argc) {
            const std::string who = argv[++i];
            const std::string what = argv[++i];
            std::printf("prop %s.%s: %s\n", who.c_str(), what.c_str(), page.actorProperty(who, what).c_str());
        } else if (step == "view") {
            std::printf("view: %s\n", page.describeView().c_str());
        } else if (step == "lines") {
            for (const std::string &l : page.linesSaid())
                std::printf("  line %s\n", l.c_str());
        } else if (step == "where") {
            std::printf("where: %s\n", page.describePlayer().c_str());
        } else if ((step == "down" || step == "up") && i + 1 < argc) {
            send(step == "down" ? reluva::EventType::kKeyDown : reluva::EventType::kKeyUp, keyOf(argv[++i]));
        } else if (step == "press" && i + 1 < argc) {
            const uint32_t key = keyOf(argv[++i]);
            send(reluva::EventType::kKeyDown, key);
            frame();
            send(reluva::EventType::kKeyUp, key);
        } else if (step == "click" && i + 2 < argc) {
            reluva::Event ev;
            ev.mouseX = std::atoi(argv[++i]);
            ev.mouseY = std::atoi(argv[++i]);
            ev.button = reluva::MouseButton::kLeft;
            for (const reluva::EventType type :
                 {reluva::EventType::kMouseMove, reluva::EventType::kMouseDown, reluva::EventType::kMouseUp}) {
                ev.type = type;
                page.handleEvent(ev);
                frame();
            }
        } else if (step == "drag" && i + 2 < argc) {
            const int dx = std::atoi(argv[++i]);
            const int dy = std::atoi(argv[++i]);
            reluva::Event ev;
            ev.type = reluva::EventType::kMouseDown;
            ev.button = reluva::MouseButton::kLeft;
            ev.mouseX = 480;
            ev.mouseY = 360;
            page.handleEvent(ev);
            for (int k = 1; k <= 10; ++k) { // over a third of a second
                ev.type = reluva::EventType::kMouseMove;
                ev.mouseX = 480 + dx * k / 10;
                ev.mouseY = 360 + dy * k / 10;
                page.handleEvent(ev);
                frame();
            }
            ev.type = reluva::EventType::kMouseUp;
            page.handleEvent(ev);
        } else if (step == "shot" && i + 1 < argc) {
            const std::string file = argv[++i];
            renderer.drawing = true;
            frame();
            renderer.drawing = false;
            const reluva::Surface &f = renderer.frame();
            std::FILE *out = std::fopen(file.c_str(), "wb");
            if (!out) {
                std::fprintf(stderr, "can't write %s\n", file.c_str());
                return 1;
            }
            std::fprintf(out, "P6\n%d %d\n255\n", f.w, f.h);
            for (int y = 0; y < f.h; ++y)
                for (int x = 0; x < f.w; ++x)
                    std::fwrite(f.row(y) + x * 4, 1, 3, out);
            std::fclose(out);
            std::printf("shot %s at %.3f s: %d draws, %d triangles, drawn in %.3f s\n", file.c_str(),
                        double(page.playTime()), renderer.lastDraws(), renderer.lastTriangles(),
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            continue;
        } else {
            std::fprintf(stderr, "unknown step %s\n", step.c_str());
            return 2;
        }
        // (Timings as " in <n> s", which the game-files test takes out.)
        std::printf("%s done in %.3f s\n", step.c_str(),
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    page.deactivate();
    return 0;
}

// Compiled UnrealScript as text (script_text.h): one class, struct,
// function or state, or (-all) every class of every package written to
// <dir>/<package>/<class>.uc.
int cmdUeScript(int argc, char **argv) {
    const bool all = argc == 5 && std::strcmp(argv[3], "-all") == 0;
    if (argc != 4 && !all) {
        std::fprintf(stderr, "usage: reluva-tool unreal script <game folder> <class | Class.Function | -all <dir>>\n");
        return 2;
    }
    using namespace reluva::unreal;
    PackageSet set;
    if (reluva::Error err = set.open(argv[2], "DS9.ini")) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const NativeTable natives(set);
    if (all) {
        int written = 0;
        int failed = 0;
        for (const std::string &name : set.names()) {
            const Package *p = set.get(name);
            if (!p)
                continue;
            for (size_t i = 0; i < p->exports().size(); ++i) {
                if (p->exports()[i].classRef != 0)
                    continue;
                const std::filesystem::path dir = std::filesystem::path(argv[4]) / name;
                std::filesystem::create_directories(dir);
                const std::string text = scriptText(*p, int(i), natives);
                std::FILE *f = std::fopen((dir / (p->objectName(int32_t(i) + 1) + ".uc")).string().c_str(), "wb");
                if (!f) {
                    ++failed;
                    continue;
                }
                std::fwrite(text.data(), 1, text.size(), f);
                std::fclose(f);
                ++written;
            }
        }
        std::printf("%d classes written to %s (%zu native functions known)%s\n", written, argv[4], natives.size(),
                    failed ? (", " + std::to_string(failed) + " couldn't be").c_str() : "");
        return failed ? 1 : 0;
    }
    // The export by its path; a class over anything else of the name.
    const Package *found = nullptr;
    int index = -1;
    for (const std::string &name : set.names()) {
        const Package *p = set.get(name);
        const int at = p ? findUnrealExport(*p, argv[3]) : -1;
        if (at < 0 || !isScriptClass(p->className(at + 1)))
            continue;
        if (!found || p->exports()[size_t(at)].classRef == 0) {
            found = p;
            index = at;
        }
        if (p->exports()[size_t(at)].classRef == 0)
            break;
    }
    if (!found) {
        std::fprintf(stderr, "No class, struct, function or state %s\n", argv[3]);
        return 1;
    }
    std::printf("// %s, in %s\n%s", found->path(index + 1).c_str(), found->file().string().c_str(),
                scriptText(*found, index, natives).c_str());
    return 0;
}

// Maps an export's data after its properties: where lazy arrays sit (each
// starts with the file offset just past it, so it can be told anywhere),
// what the mesh reader makes of it, and the bytes at its start and end.
int cmdUeProbe(int argc, char **argv) {
    const bool all = argc == 5 && std::strcmp(argv[4], "-all") == 0;
    // -at <offset> <bytes>: just those bytes, offsets from the properties' end.
    const bool range = argc == 7 && std::strcmp(argv[4], "-at") == 0;
    if (argc != 4 && !all && !range) {
        std::fprintf(
            stderr, "usage: reluva-tool unreal probe <package | game folder> <export> [-all | -at <offset> <bytes>]\n");
        return 2;
    }
    const size_t rangeAt = range ? size_t(std::strtoull(argv[5], nullptr, 10)) : 0;
    const size_t rangeBytes = range ? size_t(std::strtoull(argv[6], nullptr, 10)) : 0;
    using namespace reluva::unreal;
    // A package file, or a game folder whose packages are searched for the export.
    Package own;
    PackageSet set;
    const Package *found = nullptr;
    int index = -1;
    if (std::filesystem::is_directory(argv[2])) {
        if (reluva::Error err = set.open(argv[2], "DS9.ini")) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        // A class found is kept only until something else of the name is
        // (the mesh ARD_AmmoCrate is in another package than its class).
        for (const std::string &name : set.names()) {
            const Package *p = set.get(name);
            const int at = p ? findUnrealExport(*p, argv[3]) : -1;
            if (at < 0 || (found && isContainerClass(p->className(at + 1))))
                continue;
            found = p;
            index = at;
            if (!isContainerClass(p->className(at + 1)))
                break;
        }
    } else if (openUnrealPackage(argv[2], own)) {
        found = &own;
        index = findUnrealExport(own, argv[3]);
    }
    if (!found || index < 0) {
        std::fprintf(stderr, "No export %s\n", argv[3]);
        return 1;
    }
    const Package &pkg = *found;
    std::printf("In %s\n", pkg.file().string().c_str());
    std::vector<uint8_t> data;
    if (reluva::Error err = pkg.readExport(index, data)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const Export &e = pkg.exports()[size_t(index)];
    const size_t base = size_t(e.serialOffset);
    const size_t end = base + data.size();
    Reader r(data, base);
    PropertyList props;
    if (reluva::Error err = beginObject(pkg, index, r, props))
        std::printf("properties: %s\n", err.message.c_str());
    const size_t start = r.pos();
    std::printf("%d: %s %s, version %d, %zu bytes at %zu; %zu properties, then %zu bytes from file offset %zu "
                "(+0 below)\n",
                index, pkg.className(index + 1).c_str(), pkg.path(index + 1).c_str(), pkg.version(), data.size(), base,
                props.items.size(), end - start, start);
    for (const Property &p : props.items)
        std::printf("  %s (%s) = %s\n", p.name.c_str(), p.structName.empty() ? typeName(p.type) : p.structName.c_str(),
                    describe(pkg, p).c_str());

    std::printf("\nLazy arrays (i32 end offset, compact count, elements):\n");
    for (size_t p = start; p + 5 <= end; ++p) {
        Reader a(data.data() + (p - base), end - p, p);
        const size_t stop = a.u32();
        const int32_t n = a.index();
        const size_t first = a.pos();
        if (!a.ok() || n < 0 || stop < first || stop > end)
            continue;
        if (n == 0 ? stop != first
                   : ((stop - first) % size_t(n) != 0 || (stop - first) / size_t(n) > 64 || stop == first))
            continue;
        std::printf("  +%zu: %d x %zu bytes, ends at +%zu\n", p - start, n, n ? (stop - first) / size_t(n) : 0,
                    stop - start);
    }

    if (isMeshClass(pkg.className(index + 1))) {
        Mesh m;
        const reluva::Error err = readMesh(pkg, index, m);
        std::printf("\nMesh reader: %s\n", err ? err.message.c_str() : "reads");
        if (!err)
            std::printf("  %d vertices x %d frames, %zu triangles, %zu animations (%d left out); import block %zu "
                        "bytes, %s\n",
                        m.frameVerts, m.animFrames, m.tris.size(), m.anims.size(), m.droppedAnims, m.importBlock,
                        m.importAxes ? "turned from C3D's axes" : "no turn");
        if (!err && m.keyFrames)
            std::printf("  %d of its %d frames of vertices found (the first is shown for all)\n", m.keyFrames,
                        m.animFrames);
        if (!err && m.framesFromClips) {
            // How many of its frames differ from the first: a keyframed
            // mesh filled from its clips moves.
            int moving = 0;
            const size_t n = size_t(m.frameVerts);
            for (int f = 1; f < m.animFrames; ++f)
                for (size_t v = 0; v < n; ++v)
                    if (m.verts[size_t(f) * n + v] != m.verts[v]) {
                        ++moving;
                        break;
                    }
            std::printf("  %d frames filled from its clips by name, %d of them unlike the first\n", m.framesFromClips,
                        moving);
        }
        if (!err && m.bones)
            std::printf("  posed from %d bones (%s)\n", m.bones,
                        m.bonesAnimated ? "every frame, from their animation keys"
                                        : "the pose they're bound in, for all frames");
        if (!range && !(!err && m.bones))
            for (const std::string &l : describeMeshLayout(pkg, index))
                std::printf("%s\n", l.c_str());
        if (!range)
            for (const std::string &l : describeBones(pkg, index))
                std::printf("%s\n", l.c_str());
    }

    auto dump = [&](size_t from, size_t to) {
        for (size_t row = from; row < to; row += 16) {
            std::printf("  +%-6zu %08zx ", row - start, row);
            for (size_t k = row; k < row + 16 && k < to; ++k)
                std::printf(" %02x", data[k - base]);
            std::printf("\n");
        }
    };
    if (range) {
        const size_t from = std::min(end, start + rangeAt);
        std::printf("\nBytes +%zu to +%zu:\n", from - start, std::min(end, from + rangeBytes) - start);
        dump(from, std::min(end, from + rangeBytes));
        return 0;
    }
    if (all) {
        std::printf("\nEvery byte after the properties:\n");
        dump(start, end);
        return 0;
    }
    std::printf("\nThe first bytes after the properties:\n");
    dump(start, std::min(end, start + 512));
    if (end > start + 512) {
        std::printf("\nThe last bytes:\n");
        dump(std::max(start + 512, end - 256), end);
    }
    return 0;
}

// Checks how attachment points on triangles (type 1) are placed and
// turned against the places The Fallen's meshes keep worked out in
// advance (`<Animation><Point>Loc`, in the skeleton's units): each
// animation posed through its frames, the point where it comes nearest
// the place (placed in the triangle's frame as Engine.dll's PointMatrix
// does: from corner 0, X to corner 2, Y the normal), and its axes there
// taken in that frame or in the frame of the bone the triangle moves with
// (mesh.md). Each place's line printed if `print`.
struct PointsCheck {
    int places = 0;
    int within5 = 0;
    int triangleX = 0;
    int boneX = 0;
    double miss = 0; // units, all together
};
PointsCheck checkAttachPoints(const reluva::unreal::PackageSet &set, bool print) {
    using namespace reluva::unreal;
    PointsCheck out;
    const std::vector<AnimationSet> sets = findAnimationSets(set);
    auto sub = [](Vec3f a, Vec3f b) { return Vec3f{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto dot = [](Vec3f a, Vec3f b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto cross = [](Vec3f a, Vec3f b) {
        return Vec3f{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    auto unit = [&](Vec3f v) {
        const float l = std::sqrt(dot(v, v));
        return l > 0 ? Vec3f{v[0] / l, v[1] / l, v[2] / l} : v;
    };
    // A point's axes (rows) in a frame (rows), and how well they match
    // others: the rows' dot products summed, 3 for the same.
    auto inFrame = [&](const std::array<Vec3f, 3> &axes, const std::array<Vec3f, 3> &frame) {
        std::array<Vec3f, 3> out{};
        for (size_t r = 0; r < 3; ++r) {
            for (size_t j = 0; j < 3; ++j)
                for (size_t q = 0; q < 3; ++q)
                    out[r][q] += axes[r][j] * frame[j][q];
            out[r] = unit(out[r]);
        }
        return out;
    };
    auto match = [&](const std::array<Vec3f, 3> &a, const std::array<Vec3f, 3> &b) {
        return dot(a[0], b[0]) + dot(a[1], b[1]) + dot(a[2], b[2]);
    };
    // The axes as one (rows' dot products summed: 3 the same) and the X
    // axis alone, either way (the barrel's line, for a gun's tip).
    for (const std::string &name : set.names()) {
        const Package *pkg = set.get(name);
        if (!pkg)
            continue;
        for (size_t i = 0; i < pkg->exports().size(); ++i) {
            if (!isMeshClass(pkg->className(int32_t(i) + 1)))
                continue;
            Mesh m;
            if (readMesh(*pkg, int(i), m) || m.precomputedPoints.empty() || !m.bones)
                continue;
            const auto poser = LayeredPoser::open(*pkg, int(i), sets);
            if (!poser)
                continue;
            auto skeletonOf = [&](const Vec3f &packed) {
                return Vec3f{(packed[0] - m.origin[0]) * m.scale[0], (packed[1] - m.origin[1]) * m.scale[1],
                             (packed[2] - m.origin[2]) * m.scale[2]};
            };
            for (const MeshAttachPoint &pre : m.precomputedPoints) {
                // <Animation><Point>Loc: the longest point name that ends it.
                if (pre.name.size() < 4 || pre.name.substr(pre.name.size() - 3) != "Loc")
                    continue;
                const std::string stem = pre.name.substr(0, pre.name.size() - 3);
                const MeshAttachPoint *point = nullptr;
                for (const MeshAttachPoint &p : m.attachPoints)
                    if (p.type == 1 && p.triangle >= 0 && size_t(p.triangle) < m.tris.size() &&
                        stem.size() > p.name.size() && stem.substr(stem.size() - p.name.size()) == p.name &&
                        (!point || p.name.size() > point->name.size()))
                        point = &p;
                if (!point)
                    continue;
                const std::string anim = stem.substr(0, stem.size() - point->name.size());
                const MeshTri &t = m.tris[size_t(point->triangle)];
                const int bone = poser->mainBone(t.vertex);
                // The frame where the point comes nearest the place.
                float best = 1e30f;
                float at = 0;
                Vec3f miss{};
                std::vector<Vec3f> verts;
                // Engine.dll's frame (TriFrame): from corner 0, X to corner 2,
                // Y the normal, Z = X x Y; the point's offset along those, its
                // turn after (a kind 7 point is no turn and no offset).
                auto engineFrame = [&](const std::vector<Vec3f> &vs, Vec3f &origin) {
                    Vec3f c[3];
                    for (size_t k = 0; k < 3; ++k)
                        c[k] = skeletonOf(vs[size_t(t.vertex[k])]);
                    const Vec3f x = unit(sub(c[2], c[0]));
                    const Vec3f y = unit(cross(sub(c[1], c[0]), sub(c[2], c[0])));
                    const std::array<Vec3f, 3> f{x, y, cross(x, y)};
                    origin = c[0];
                    if (point->kind != 7)
                        for (size_t j = 0; j < 3; ++j)
                            for (size_t q = 0; q < 3; ++q)
                                origin[q] += point->offset[j] * f[j][q];
                    return f;
                };
                for (int f = 0; f <= 60; ++f) {
                    if (!poser->pose(anim, float(f) / 60, {}, verts))
                        break;
                    Vec3f placed{};
                    engineFrame(verts, placed);
                    const Vec3f d = sub(placed, pre.offset);
                    if (dot(d, d) < best) {
                        best = dot(d, d);
                        at = float(f) / 60;
                        miss = d;
                    }
                }
                if (best >= 1e30f) {
                    if (print)
                        std::printf("%-24s %-24s no animation %s\n", pkg->objectName(int32_t(i) + 1).c_str(),
                                    pre.name.c_str(), anim.c_str());
                    continue;
                }
                poser->pose(anim, at, {}, verts);
                Vec3f origin{};
                const std::array<Vec3f, 3> ef = engineFrame(verts, origin);
                const std::array<Vec3f, 3> ident{Vec3f{1, 0, 0}, Vec3f{0, 1, 0}, Vec3f{0, 0, 1}};
                const std::array<Vec3f, 3> triangleAxes = inFrame(point->kind == 7 ? ident : point->axes, ef);
                const float byTriangle = match(triangleAxes, pre.axes);
                const float xTriangle = std::fabs(dot(triangleAxes[0], pre.axes[0]));
                float byBone = -3;
                float xBone = 0;
                std::vector<std::array<float, 12>> worlds;
                if (bone >= 0 && poser->skeleton(anim, at, {}, worlds) && size_t(bone) < worlds.size()) {
                    std::array<Vec3f, 3> frame{};
                    for (size_t r = 0; r < 3; ++r)
                        frame[r] = unit({worlds[size_t(bone)][r * 3], worlds[size_t(bone)][r * 3 + 1],
                                         worlds[size_t(bone)][r * 3 + 2]});
                    const std::array<Vec3f, 3> boneAxes = inFrame(point->axes, frame);
                    byBone = match(boneAxes, pre.axes);
                    xBone = std::fabs(dot(boneAxes[0], pre.axes[0]));
                }
                ++out.places;
                out.within5 += std::sqrt(best) <= 5;
                out.miss += std::sqrt(best);
                out.triangleX += xTriangle >= 0.95f;
                out.boneX += xBone >= 0.95f;
                if (print)
                    std::printf(
                        "%-22s %-20s %5.1f off (%5.1f %5.1f %5.1f) at %.2f; axes by the triangle %5.2f (X %.2f), "
                        "by %s %5.2f (X %.2f)\n",
                        pkg->objectName(int32_t(i) + 1).c_str(), pre.name.c_str(), std::sqrt(best), miss[0], miss[1],
                        miss[2], at, byTriangle, xTriangle, poser->boneName(bone).c_str(), byBone, xBone);
            }
        }
    }
    return out;
}

void printPointsCheck(const PointsCheck &c) {
    if (c.places)
        std::printf("attachment points against %d precomputed places: %.1f units off on average, %d within 5; the X "
                    "axis along the place's (within 0.95) taken in the triangle's axes %d, in its bone's %d\n",
                    c.places, c.miss / c.places, c.within5, c.triangleX, c.boneX);
}

// trace <game folder> <map> x y z x2 y2 z2 [ex ey ez]: a box (half
// sizes, none for a line) swept through a level's BSP: where it stops.
int cmdUeTrace(int argc, char **argv) {
    if (argc != 10 && argc != 13) {
        std::fprintf(stderr, "usage: reluva-tool unreal trace <game folder> <map> x y z x2 y2 z2 [ex ey ez]\n");
        return 2;
    }
    using namespace reluva::unreal;
    PackageSet set;
    if (reluva::Error err = set.open(argv[2], "")) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const Package *pkg = set.get(argv[3]);
    Model model;
    std::string error;
    if (!pkg || findLevelModel(*pkg, &model, &error) < 0) {
        std::fprintf(stderr, "no level model in %s\n", argv[3]);
        return 1;
    }
    auto f = [&](int i) { return float(std::atof(argv[i])); };
    const Vec3f a{f(4), f(5), f(6)};
    const Vec3f b{f(7), f(8), f(9)};
    const Vec3f e = argc == 13 ? Vec3f{f(10), f(11), f(12)} : Vec3f{};
    LevelCollision col(model);
    const auto h = col.trace(a, b, e);
    std::printf(
        "hit %d startSolid %d time %.4f at %.2f %.2f %.2f normal %.3f %.3f %.3f node %d; start solid box %d, end %d\n",
        h.hit(), h.startSolid, double(h.time), double(h.location[0]), double(h.location[1]), double(h.location[2]),
        double(h.normal[0]), double(h.normal[1]), double(h.normal[2]), h.node, col.solid(a, e), col.solid(b, e));
    // The node hit: its flags, and its surface's polygon flags and brush.
    if (h.node >= 0 && size_t(h.node) < model.nodes.size()) {
        const BspNode &n = model.nodes[size_t(h.node)];
        const bool surf = n.surf >= 0 && size_t(n.surf) < model.surfs.size();
        std::printf("node %d: flags %02x, surface %d, poly flags %08x, brush %s\n", h.node, unsigned(n.flags), n.surf,
                    surf ? model.surfs[size_t(n.surf)].polyFlags : 0u,
                    surf && model.surfs[size_t(n.surf)].actor > 0 ? pkg->path(model.surfs[size_t(n.surf)].actor).c_str()
                                                                  : "-");
    }
    return 0;
}

// ledges <game folder> <map> [max]: places in a level's BSP where the
// player could pull up onto a ledge (a wall straight ahead, its flat top
// 30 to 110 units over the floor, room over it) or catch one in a jump
// (115 to 160 up): each with the place to stand (Unreal's coordinates)
// and the way to face, for trying them with play's teleport.
int cmdUeLedges(int argc, char **argv) {
    if (argc < 4 || argc > 6) {
        std::fprintf(stderr,
                     "usage: reluva-tool unreal ledges <game folder> <map> [max | ladders | lights <effect>]\n");
        return 2;
    }
    using namespace reluva::unreal;
    PackageSet set;
    if (reluva::Error err = set.open(argv[2], "")) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const Package *pkg = set.get(argv[3]);
    Model model;
    std::string error;
    if (!pkg || findLevelModel(*pkg, &model, &error) < 0) {
        std::fprintf(stderr, "no level model in %s %s\n", argv[3], error.c_str());
        return 1;
    }
    // The water zones (their ZoneInfo's class a water one), for walls to
    // swim into.
    ZoneVisibility zv;
    zv.build(model);
    uint64_t water = 0;
    for (size_t z = 0; z < model.zones.size() && z < 64; ++z) {
        const int32_t ref = model.zones[z].actor;
        if (ref <= 0)
            continue;
        std::string cls = pkg->className(ref);
        for (char &ch : cls)
            ch = char(std::tolower(static_cast<unsigned char>(ch)));
        if (cls.find("h20zone") != std::string::npos || cls.find("waterzone") != std::string::npos ||
            cls.find("lavazone") != std::string::npos)
            water |= uint64_t(1) << z;
    }
    auto wet = [&](const Vec3f &p) {
        bool solid = false;
        const int z = zv.zoneAt(p, &solid);
        return !solid && z >= 0 && z < 64 && ((water >> z) & 1);
    };
    // "lights <effect>": the level's lights with that LightEffect.
    if (argc == 6 && std::string(argv[4]) == "lights") {
        const int want = std::atoi(argv[5]);
        const LevelLights lights = gatherLights(set, *pkg, model);
        int count = 0;
        for (const LightSource &l : lights.unique)
            if (l.effect == want) {
                ++count;
                std::printf("light %s at %.1f %.1f %.1f radius %.0f colour %.2f %.2f %.2f%s\n",
                            l.ref > 0 ? pkg->path(l.ref).c_str() : "?", double(l.position[0]), double(l.position[1]),
                            double(l.position[2]), double(l.radius), double(l.color[0]), double(l.color[1]),
                            double(l.color[2]), l.isStatic ? "" : ", not static");
            }
        // Any actor set so, lit or not (LightType 0 lights nothing).
        for (size_t i = 0; i < pkg->exports().size(); ++i) {
            std::vector<uint8_t> data;
            if (pkg->readExport(int(i), data))
                continue;
            Reader r(data, size_t(pkg->exports()[i].serialOffset));
            PropertyList props;
            if (beginObject(*pkg, int(i), r, props))
                continue;
            const Property *e = props.find("LightEffect");
            if (e && !e->data.empty() && e->data[0] == want) {
                const Property *t = props.find("LightType");
                std::printf("actor %s (%s): LightType %d\n", pkg->path(int(i) + 1).c_str(),
                            pkg->className(int(i) + 1).c_str(), t && !t->data.empty() ? t->data[0] : 0);
            }
        }
        std::printf("%d found\n", count);
        return 0;
    }
    // "ladders": each upright polygon with a ladder texture (TexTag
    // TT_Ladder), its width along the wall.
    if (argc == 5 && std::string(argv[4]) == "ladders") {
        std::map<std::pair<const Package *, int>, bool> isLadder;
        int count = 0;
        for (const BspNode &n : model.nodes) {
            if (std::fabs(n.plane[2]) > 0.1f || n.vertexCount < 3 || n.surf < 0 || size_t(n.surf) >= model.surfs.size())
                continue;
            const ObjectRef t = set.resolve(*pkg, model.surfs[size_t(n.surf)].texture);
            if (!t)
                continue;
            auto [it, fresh] = isLadder.try_emplace({t.package, t.exportIndex}, false);
            if (fresh) {
                std::vector<uint8_t> data;
                if (!t.package->readExport(t.exportIndex, data)) {
                    Reader r(data, size_t(t.package->exports()[size_t(t.exportIndex)].serialOffset));
                    PropertyList props;
                    if (!beginObject(*t.package, t.exportIndex, r, props))
                        if (const Property *tag = props.find("TexTag"); tag && !tag->data.empty())
                            it->second = tag->data[0] == 1; // a byte
                }
            }
            if (!it->second)
                continue;
            Vec3f c{};
            float lo = 1e30f;
            float hi = -1e30f;
            float zlo = 1e30f;
            float zhi = -1e30f;
            const Vec3f side{-n.plane[1], n.plane[0], 0};
            for (int k = 0; k < n.vertexCount; ++k) {
                const Vec3f &q = model.points[size_t(model.verts[size_t(n.vertPool + k)].point)];
                for (size_t a = 0; a < 3; ++a)
                    c[a] += q[a] / float(n.vertexCount);
                const float d = q[0] * side[0] + q[1] * side[1];
                lo = std::min(lo, d), hi = std::max(hi, d), zlo = std::min(zlo, q[2]), zhi = std::max(zhi, q[2]);
            }
            ++count;
            const bool inWater = wet({c[0] + n.plane[0] * 20, c[1] + n.plane[1] * 20, zlo + 10});
            std::printf("ladder %.1f %.1f %.1f normal %.2f %.2f width %.1f from %.1f to %.1f%s\n", double(c[0]),
                        double(c[1]), double(c[2]), double(n.plane[0]), double(n.plane[1]), double(hi - lo),
                        double(zlo), double(zhi), inWater ? ", its foot in water" : "");
        }
        std::printf("%d found\n", count);
        return 0;
    }
    // "steps": low steps (8 to 24 up) to stand across instead.
    const bool steps = argc == 5 && std::string(argv[4]) == "steps";
    const int most = argc == 5 && !steps ? std::atoi(argv[4]) : 40;
    LevelCollision col(model);
    const float R = 17;
    const float H = 49.5f;
    const Vec3f stand{R, R, H};
    int found = 0;
    std::set<std::tuple<int, int, int>> seen;
    for (const BspNode &n : model.nodes) {
        if (n.plane[2] < 0.9f || n.vertexCount < 3 || found >= most)
            continue;
        Vec3f c{};
        for (int k = 0; k < n.vertexCount; ++k) {
            const int32_t pi = model.verts[size_t(n.vertPool + k)].point;
            for (size_t q = 0; q < 3; ++q)
                c[q] += model.points[size_t(pi)][q] / float(n.vertexCount);
        }
        const Vec3f at{c[0], c[1], c[2] + H + 2};
        if (col.solid(at, stand))
            continue;
        // On the floor (just above it).
        const auto down = col.trace(at, {at[0], at[1], at[2] - 8}, stand);
        if (!down.hit() || down.normal[2] < 0.7f)
            continue;
        const Vec3f foot = down.location;
        const float feet = foot[2] - H;
        for (int d = 0; d < 8 && found < most; ++d) {
            const float yaw = float(d) * 3.14159265f / 4;
            const Vec3f dir{std::cos(yaw), std::sin(yaw), 0};
            const Vec3f far{foot[0] + dir[0] * 80, foot[1] + dir[1] * 80, foot[2]};
            const auto wall = col.trace(foot, far, stand);
            // A low gap: the ducked box from the feet goes on where the
            // standing one can't, onto floor.
            if (wall.hit() && !wall.startSolid) {
                const float D = 32.5f;
                const Vec3f low{foot[0], foot[1], feet + D + 1};
                const Vec3f lowFar{low[0] + dir[0] * 120, low[1] + dir[1] * 120, low[2]};
                const auto under = col.trace(low, lowFar, {R, R, D + 1});
                const auto floorThere = col.trace(lowFar, {lowFar[0], lowFar[1], lowFar[2] - 20}, {R, R, D + 1});
                const float gap = std::sqrt((wall.location[0] - foot[0]) * (wall.location[0] - foot[0]) +
                                            (wall.location[1] - foot[1]) * (wall.location[1] - foot[1]));
                if (!under.hit() && !under.startSolid && floorThere.hit() && floorThere.normal[2] > 0.7f && gap < 60 &&
                    seen.insert(std::make_tuple(int(foot[0] / 64), int(foot[1] / 64), 100 + d)).second) {
                    ++found;
                    std::printf("duck %.1f %.1f %.1f yaw %d wall %.1f\n", double(foot[0]), double(foot[1]),
                                double(foot[2]), d * 45, double(gap));
                    continue;
                }
            }
            if (!wall.hit() || std::fabs(wall.normal[2]) > 0.1f ||
                -(wall.normal[0] * dir[0] + wall.normal[1] * dir[1]) < 0.9f)
                continue;
            // The top: down from high over the wall, just past its face.
            const Vec3f past{wall.location[0] + dir[0] * (R + 12), wall.location[1] + dir[1] * (R + 12), feet + 175};
            const auto top = col.trace(past, {past[0], past[1], feet + 20}, {4, 4, 1});
            if (!top.hit() || top.normal[2] < 0.9f || top.startSolid)
                continue;
            const float rise = top.location[2] - 1 - feet;
            // In water, its top out of it: swum into.
            if (wet(foot) && !wet({past[0], past[1], top.location[2] + 4}) && rise > 30 &&
                seen.insert(std::make_tuple(int(foot[0] / 64), int(foot[1] / 64), 200 + d)).second) {
                ++found;
                float surface = feet;
                while (surface < top.location[2] + 8 && wet({foot[0], foot[1], surface + 1}))
                    surface += 1;
                const Vec3f overWater{foot[0], foot[1], surface + 20};
                const bool open = !col.solid(overWater, {R, R, 18});
                std::printf("swim %.1f %.1f %.1f yaw %d rise %.1f over water %.1f%s\n", double(foot[0]),
                            double(foot[1]), double(foot[2]), d * 45, double(rise), double(top.location[2] - surface),
                            open ? "" : " (no room over the water)");
                continue;
            }
            if (steps) {
                if (rise >= 8 && rise <= 24 &&
                    seen.insert(std::make_tuple(int(foot[0] / 64), int(foot[1] / 64), 300 + d)).second) {
                    ++found;
                    std::printf("step %.1f %.1f %.1f yaw %d rise %.1f wall at %.1f %.1f\n", double(foot[0]),
                                double(foot[1]), double(foot[2]), d * 45, double(rise), double(wall.location[0]),
                                double(wall.location[1]));
                }
                continue;
            }
            const bool clamber = rise > 30 && rise < 110;
            const bool hang = rise > 115 && rise < 160;
            if (!clamber && !hang)
                continue;
            // Room to stand on it.
            const Vec3f onTop{past[0] + dir[0] * 16, past[1] + dir[1] * 16, top.location[2] + H + 2};
            if (col.solid(onTop, stand))
                continue;
            const auto key = std::make_tuple(int(foot[0] / 64), int(foot[1] / 64), d);
            if (!seen.insert(key).second)
                continue;
            ++found;
            std::printf("%s %.1f %.1f %.1f yaw %d rise %.1f\n", clamber ? "clamber" : "hang", double(foot[0]),
                        double(foot[1]), double(foot[2]), d * 45, double(rise));
        }
    }
    std::printf("%d found\n", found);
    return 0;
}

int cmdUePoints(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool unreal points <game folder>\n");
        return 2;
    }
    reluva::unreal::PackageSet set;
    if (reluva::Error err = set.open(argv[2], "")) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    printPointsCheck(checkAttachPoints(set, true));
    return 0;
}

int cmdUeExtract(int argc, char **argv) {
    if (argc != 5 && argc != 6) {
        std::fprintf(
            stderr,
            "usage: reluva-tool unreal extract <game folder> <package name> <export> [steps] (writes <export>.tga or .wav...;\n"
            "  a fire or water texture as it is after [steps] frames at 35 a second, a second in by default)\n");
        return 2;
    }
    using namespace reluva::unreal;
    PackageSet set;
    if (reluva::Error err = set.open(argv[2], "")) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const Package *pkg = set.get(argv[3]);
    if (!pkg) {
        std::fprintf(stderr, "%s\n", set.error(argv[3]).c_str());
        return 1;
    }
    const int index = findUnrealExport(*pkg, argv[4]);
    if (index < 0) {
        std::fprintf(stderr, "No export %s\n", argv[4]);
        return 1;
    }
    const std::string cls = pkg->className(index + 1);
    const std::string base = pkg->name(pkg->exports()[size_t(index)].objectName);
    if (isTextureClass(cls)) {
        reluva::image::Image img;
        if (reluva::Error err = decodeTexture(set, *pkg, index, img)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        // One that plays (fire, water, a flipbook...): as it is a second
        // in, or after the frames asked for.
        if (isAnimatedTexture(set, *pkg, index))
            if (auto anim = openAnimatedTexture(set, *pkg, index)) {
                if (argc == 6)
                    for (int i = std::atoi(argv[5]); i > 0; --i)
                        anim->advance(1.0f / 35);
                else
                    anim->advance(1);
                img = anim->image();
            }
        // An uncompressed 32-bit TGA, rows top to bottom.
        std::vector<uint8_t> tga(18, 0);
        tga[2] = 2;
        tga[12] = uint8_t(img.w), tga[13] = uint8_t(img.w >> 8);
        tga[14] = uint8_t(img.h), tga[15] = uint8_t(img.h >> 8);
        tga[16] = 32, tga[17] = 0x28;
        for (size_t i = 0; i < img.rgba.size(); i += 4) {
            tga.push_back(img.rgba[i + 2]);
            tga.push_back(img.rgba[i + 1]);
            tga.push_back(img.rgba[i]);
            tga.push_back(img.rgba[i + 3]);
        }
        std::ofstream(base + ".tga", std::ios::binary)
            .write(reinterpret_cast<const char *>(tga.data()), std::streamsize(tga.size()));
        std::printf("Wrote %s.tga (%dx%d)\n", base.c_str(), img.w, img.h);
        return 0;
    }
    if (reluva::fs::toLower(cls) == "sound" || reluva::fs::toLower(cls) == "music") {
        EmbeddedFile f;
        if (reluva::Error err = readEmbeddedFile(*pkg, index, f)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        if (f.isPlaceholder()) {
            const auto mp3 = streamedFile(set, *pkg, index);
            std::printf("%s holds no sound: it's a placeholder for %s\n", base.c_str(),
                        mp3 ? mp3->string().c_str() : ("mp3/" + base + ".mp3, which isn't there").c_str());
            return 0;
        }
        const std::string name = base + "." + reluva::fs::toLower(f.format);
        std::ofstream(name, std::ios::binary)
            .write(reinterpret_cast<const char *>(f.data.data()), std::streamsize(f.data.size()));
        std::printf("Wrote %s (%zu bytes)\n", name.c_str(), f.data.size());
        return 0;
    }
    std::fprintf(stderr, "Can't extract a %s yet\n", cls.c_str());
    return 1;
}

// Opens every package on a game's search path and decodes every texture,
// sound and level model, reporting what fails: the check to run on a new
// game or release.
int cmdUeCheck(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: reluva-tool unreal check <game folder> [-v]\n");
        return 2;
    }
    using namespace reluva::unreal;
    const bool verbose = argc == 4 && std::strcmp(argv[3], "-v") == 0;
    std::vector<std::string> warnings;
    PackageSet set;
    std::string ini;
    for (const reluva::DetectedGame &g : detect("unreal", argv[2]))
        if (const GameInfo *info = findGame(g.gameId)) {
            ini = info->iniName;
            std::printf("Detected: %s\n", g.description.c_str());
        }
    if (reluva::Error err = set.open(argv[2], ini, &warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("Search paths from %s:\n", set.iniFile().empty() ? "(defaults)" : set.iniFile().string().c_str());
    for (const std::string &p : set.searchPaths())
        std::printf("  %s\n", p.c_str());
    for (const std::string &w : warnings)
        std::printf("warning: %s\n", w.c_str());

    std::map<std::string, int> versions;
    struct Tally {
        int ok = 0;
        int failed = 0;
        std::vector<std::string> examples;
    };
    std::map<std::string, Tally> tallies;
    std::map<std::string, bool> meshReads; // placed actors' meshes: whether they read
    std::map<std::string, int> meshBones;  // and how many bones they're posed from
    std::map<std::string, int> animPlayed; // "mesh anim": 0 not found, 1 its own, 2 borrowed
    std::optional<std::vector<AnimationSet>> animationSets;
    std::map<std::string, int> unreadPlaced; // those that don't: how many actors use them
    auto record = [&](const std::string &what, const reluva::Error &err) {
        Tally &t = tallies[what];
        if (!err) {
            ++t.ok;
            return;
        }
        ++t.failed;
        if (verbose || t.examples.size() < 5)
            t.examples.push_back(err.message);
    };
    int unopened = 0;
    long long lightTotals[4] = {};
    ClassDefaults classDefaults(set);
    std::map<std::string, int> spriteTextures; // placed sprites by texture
    for (const std::string &name : set.names()) {
        const Package *pkg = set.get(name);
        if (!pkg) {
            ++unopened;
            std::printf("can't open %s: %s\n", name.c_str(), set.error(name).c_str());
            continue;
        }
        ++versions[std::to_string(pkg->version()) + "/" + std::to_string(pkg->licensee())];
        for (size_t i = 0; i < pkg->exports().size(); ++i) {
            const std::string cls = reluva::fs::toLower(pkg->className(int32_t(i) + 1));
            // Compiled UnrealScript (script.h): every field reads, its
            // bytecode ending where its size says, nothing left over.
            if (isScriptClass(cls)) {
                ScriptObject o;
                reluva::Error err = readScriptObject(*pkg, int(i), o);
                if (!err && o.trailing)
                    err = reluva::Error::fail(pkg->path(int32_t(i) + 1) + ": " + std::to_string(o.trailing) +
                                              " bytes left over");
                const bool coded = o.kind == ScriptObject::Kind::kFunction || o.kind == ScriptObject::Kind::kState ||
                                   o.kind == ScriptObject::Kind::kClass || o.kind == ScriptObject::Kind::kStruct;
                record(coded ? "scripts (classes, structs, functions, states)"
                             : "scripts (properties, enums, constants)",
                       err);
                if (!err && coded && !o.code.statements.empty())
                    ++tallies["scripts with bytecode"].ok;
            }
            if (isTextureClass(cls)) {
                reluva::image::Image img;
                TextureInfo info;
                const reluva::Error err = decodeTexture(set, *pkg, int(i), img, &info);
                if (!err && info.fromComp)
                    ++tallies["textures (from their compressed copy)"].ok;
                if (!err && info.standIn.empty())
                    ++tallies["textures in format " + std::to_string(int(info.format))].ok;
                if (!err && !info.standIn.empty())
                    ++tallies["textures (procedural, stand-in shown)"].ok;
                else
                    record("textures", err);
                // Fire, water, wet, ice and FLC textures: whether they play
                // (procedural.h), ten seconds of each (some FLCs show a frame for seconds).
                if (isAnimatedTextureClass(cls)) {
                    std::string why;
                    auto anim = openAnimatedTexture(set, *pkg, int(i), &why);
                    int changes = 0;
                    for (int k = 0; anim && k < 60; ++k)
                        changes += anim->advance(1.0f / 6);
                    const std::string kind = anim ? anim->kind() : cls;
                    record("animated textures (" + kind + ")",
                           !anim          ? reluva::Error::fail(why)
                           : changes == 0 ? reluva::Error::fail(pkg->path(int32_t(i) + 1) + ": doesn't change")
                                          : reluva::Error::success());
                }
            } else if (cls == "sound") {
                reluva::audio::Sound s;
                std::string from;
                reluva::Error err = loadSound(set, *pkg, int(i), s, &from);
                if (err)
                    err.message = pkg->path(int32_t(i) + 1) + ": " + err.message;
                record(from.empty() ? "sounds" : "sounds (streamed from mp3/)", err);
                // What Miles' positional provider refuses (stereo, not
                // PCM: only ever played flat), and the 8-bit ones.
                if (!err && from.empty()) {
                    if (s.channels == 2)
                        ++tallies["sounds in stereo"].ok;
                    if (s.format != 1)
                        ++tallies["sounds not PCM (IMA ADPCM)"].ok;
                    if (s.sourceBits == 8)
                        ++tallies["sounds of 8 bits"].ok;
                }
                // A WAV's own loop (its smpl chunk, with a loop in it).
                if (EmbeddedFile f; !readEmbeddedFile(*pkg, int(i), f) && f.data.size() > 12) {
                    for (size_t at = 12; at + 8 <= f.data.size();) {
                        const uint32_t size = uint32_t(f.data[at + 4]) | uint32_t(f.data[at + 5]) << 8 |
                                              uint32_t(f.data[at + 6]) << 16 | uint32_t(f.data[at + 7]) << 24;
                        if (std::memcmp(&f.data[at], "smpl", 4) == 0 && size >= 36 && at + 8 + 36 <= f.data.size()) {
                            const uint8_t *q = &f.data[at + 8 + 28];
                            const uint32_t loops =
                                uint32_t(q[0]) | uint32_t(q[1]) << 8 | uint32_t(q[2]) << 16 | uint32_t(q[3]) << 24;
                            if (loops > 0)
                                ++tallies["sounds with a loop of their own (smpl)"].ok;
                            break;
                        }
                        at += 8 + size + (size & 1);
                    }
                }
            } else if (cls == "music") {
                // Read, and played through (the tracker player) to its loop.
                EmbeddedFile f;
                reluva::Error err = readEmbeddedFile(*pkg, int(i), f);
                reluva::audio::Sound s;
                if (!err)
                    err = decodeEmbeddedSound(f, s);
                if (err)
                    err.message = pkg->path(int32_t(i) + 1) + ": " + err.message;
                record("music", err);
            } else if (cls == "palette") {
                Palette p;
                record("palettes", readPalette(*pkg, int(i), p));
            } else if (isMeshClass(cls)) {
                Mesh m;
                reluva::Error err = readMesh(*pkg, int(i), m);
                if (!err && m.trailingBytes)
                    err = reluva::Error::fail(pkg->path(int32_t(i) + 1) + ": " + std::to_string(m.trailingBytes) +
                                              " bytes after the last field read");
                // The Fallen's shared animations are Meshes without vertices.
                AnimationSet animations;
                if (err && cls == "mesh" && !readAnimationSet(*pkg, int(i), animations))
                    record("animation sets (skeleton and clips, no vertices)", reluva::Error::success());
                else
                    record(cls == "lodmesh" ? "meshes (LodMesh)" : "meshes (Mesh)", err);
                if (!err && m.vertexBytes != 4)
                    ++tallies["meshes with 8-byte vertices"].ok;
                if (!err && m.importBlock) {
                    if (m.keyFrames)
                        ++tallies["meshes keyframed (first frame shown)"].ok;
                    if (m.bones)
                        ++tallies[m.bonesAnimated ? "meshes posed from bones (animated)"
                                                  : "meshes posed from bones (bound pose shown)"]
                              .ok;
                }
            }
        }
        if (set.isMap(name)) {
            Model model;
            std::string error;
            const int at = findLevelModel(*pkg, &model, &error);
            reluva::Error err;
            if (at < 0)
                err = reluva::Error::fail(name + ": " + (error.empty() ? "no model with BSP nodes" : error));
            record("levels", err);
            if (at >= 0) {
                std::printf("%-24s %6zu nodes %6zu surfaces %7zu points  %zu polygons drawn\n", name.c_str(),
                            model.nodes.size(), model.surfs.size(), model.points.size(), levelPolygons(model).size());
                // The lighting: whether it reads, and how the lightmaps'
                // sample grids lined up with their surfaces (lighting.md).
                reluva::Error lerr;
                if (!model.hasLighting()) {
                    lerr = reluva::Error::fail(name + ": " + model.lightingError);
                    std::printf("    lighting: %s\n", model.lightingError.c_str());
                } else {
                    const LevelLights lights = gatherLights(set, *pkg, model);
                    const LightmapAtlas atlas = buildLightmaps(model, lights);
                    const LightmapAtlas::Stats &st = atlas.stats;
                    size_t animatedTexels = 0;
                    for (const AnimatedLightmap &m : atlas.animated)
                        animatedTexels += size_t(m.w) * size_t(m.h);
                    std::printf("    changing lights: %zu (pulse, flicker...) over %zu lightmaps, %zu texels\n",
                                lights.animated.size(), atlas.animated.size(), animatedTexels);
                    std::printf(
                        "    lighting: %zu zones, %zu lightmaps (%d used, %d lit), %zu light bytes, %zu lights "
                        "(%d unreadable), %zu leaves, %zu bytes after; grids placed by bounds %d, off fit %d, too "
                        "large %d; %zu pages\n",
                        model.zones.size(), model.lightMaps.size(), st.lightmaps, st.lit, model.lightBits.size(),
                        model.lights.size(), lights.unreadable, model.leaves.size(), model.trailingBytes, st.fromBounds,
                        st.badFit, st.tooLarge, atlas.pages.size());
                    lightTotals[0] += st.lightmaps;
                    lightTotals[1] += st.lit;
                    lightTotals[2] += st.fromBounds;
                    lightTotals[3] += st.badFit;
                    if (model.trailingBytes)
                        lerr = reluva::Error::fail(name + ": " + std::to_string(model.trailingBytes) +
                                                   " bytes after the model's last field");
                }
                record("lighting", lerr);
                // Movers and the sky zone (actors.md).
                std::vector<std::string> moverErrors;
                const std::vector<Mover> movers = findMovers(set, *pkg, &moverErrors);
                size_t moverPolys = 0;
                for (const Mover &m : movers)
                    moverPolys += m.polys.size();
                const auto skyAt = findSkyZone(set, *pkg);
                std::printf("    movers: %zu (%zu polygons), %zu unreadable; sky zone: %s\n", movers.size(), moverPolys,
                            moverErrors.size(), skyAt ? "yes" : "none");
                tallies["movers"].ok += int(movers.size());
                MeshActorStats meshStats;
                // Placed actors whose mesh doesn't read: which meshes matter most.
                int meshless = 0;
                int skeletal = 0;
                for (const MeshActor &a : findMeshActors(set, *pkg, classDefaults, &meshStats)) {
                    if (!a.mesh)
                        continue;
                    const std::string mesh = a.mesh.package->path(a.mesh.exportIndex + 1);
                    auto it = meshReads.find(mesh);
                    if (it == meshReads.end()) {
                        Mesh m;
                        it = meshReads.emplace(mesh, !readMesh(*a.mesh.package, a.mesh.exportIndex, m)).first;
                        meshBones[mesh] = m.bones;
                    }
                    skeletal += meshBones[mesh] > 0;
                    if (!it->second) {
                        ++meshless;
                        ++unreadPlaced[mesh];
                    } else if (meshBones[mesh] && !a.animSequence.empty()) {
                        // A character's animation: its own, or one borrowed
                        // from the shared animation sets.
                        const std::string key = mesh + " " + a.animSequence;
                        auto played = animPlayed.find(key);
                        if (played == animPlayed.end()) {
                            Mesh m;
                            readMesh(*a.mesh.package, a.mesh.exportIndex, m);
                            int how = m.findAnim(a.animSequence) ? 1 : 0;
                            if (!how) {
                                if (!animationSets)
                                    animationSets = findAnimationSets(set);
                                how = borrowNamedAnimation(*a.mesh.package, a.mesh.exportIndex, *animationSets,
                                                           a.animSequence, m)
                                          ? 2
                                          : 0;
                                const std::string lower = reluva::fs::toLower(a.animSequence);
                                if (!how && std::none_of(m.otherAnims.begin(), m.otherAnims.end(), [&](const auto &s) {
                                        return reluva::fs::toLower(s.name) == lower;
                                    }))
                                    how = 3; // its mesh doesn't list it at all
                            }
                            played = animPlayed.emplace(key, how).first;
                        }
                        record(played->second == 1   ? "characters' animations (their own)"
                               : played->second == 2 ? "characters' animations (from shared sets)"
                               : played->second == 3 ? "characters' animations (not in their mesh's list; idle shown)"
                                                     : "characters' animations",
                               played->second ? reluva::Error::success()
                                              : reluva::Error::fail(name + ": " + a.name + " (" + key + ")"));
                    }
                }
                std::printf("    mesh actors: %d (%d with the mesh from class defaults), %d hidden, %d unreadable, "
                            "%d whose mesh doesn't read, %d posed from bones\n",
                            meshStats.meshes, meshStats.fromDefaults, meshStats.hidden, meshStats.unreadable, meshless,
                            skeletal);
                tallies["mesh actors"].ok += meshStats.meshes;
                // Sprites, by the texture they show.
                SpriteActorStats spriteStats;
                for (const SpriteActor &a : findSpriteActors(set, *pkg, classDefaults, &spriteStats)) {
                    reluva::image::Image img;
                    const reluva::Error terr = decodeTexture(set, *a.texture.package, a.texture.exportIndex, img);
                    ++spriteTextures[a.texture.package->path(a.texture.exportIndex + 1) + " (" +
                                     a.texture.package->className(a.texture.exportIndex + 1) +
                                     (terr ? ", doesn't decode" : "") + ")"];
                    record("sprites", terr ? reluva::Error::fail(name + ": " + a.name + ": " + terr.message)
                                           : reluva::Error::success());
                }
                std::printf("    sprites: %d (%d editor icons, %d hidden)\n", spriteStats.sprites,
                            spriteStats.editorOnly, spriteStats.hidden);
                for (const std::string &e : moverErrors)
                    record("movers", reluva::Error::fail(name + ": " + e));
            }
        }
    }
    std::printf("\n%zu packages (%d wouldn't open); versions/licensee:", set.names().size(), unopened);
    for (const auto &[v, n] : versions)
        std::printf(" %s x%d", v.c_str(), n);
    std::printf("\n");
    if (lightTotals[0])
        std::printf("lightmap grids: %lld, %lld lit; placed by bounds %lld; off fit %lld\n", lightTotals[0],
                    lightTotals[1], lightTotals[2], lightTotals[3]);
    std::printf("class defaults (for placed actors' classes and their parents): found %d, not found %d\n",
                classDefaults.found(), classDefaults.missing());
    if (!spriteTextures.empty()) {
        std::printf("sprites' textures:");
        for (const auto &[texture, n] : spriteTextures)
            std::printf(" %s x%d;", texture.c_str(), n);
        std::printf("\n");
    }
    if (!unreadPlaced.empty()) {
        std::vector<std::pair<int, std::string>> byUse;
        for (const auto &[mesh, n] : unreadPlaced)
            byUse.push_back({n, mesh});
        std::sort(byUse.rbegin(), byUse.rend());
        std::printf("meshes placed in levels that don't read, most used first:");
        for (size_t k = 0; k < byUse.size() && k < 15; ++k)
            std::printf("%s %s (%d)", k ? "," : "", byUse[k].second.c_str(), byUse[k].first);
        std::printf("%s\n", byUse.size() > 15 ? (", and " + std::to_string(byUse.size() - 15) + " more").c_str() : "");
    }
    printPointsCheck(checkAttachPoints(set, false));
    for (const auto &[what, t] : tallies) {
        std::printf("%-9s %6d decoded, %6d failed\n", what.c_str(), t.ok, t.failed);
        for (const std::string &ex : t.examples)
            std::printf("    %s\n", ex.c_str());
    }
    return 0;
}

} // namespace

const reluva::tool::Commands kCommands(
    "unreal",
    {
        {"check", "<game folder> [-v]",
         "read every package of a game: textures, sounds, meshes, models, scripts; and the levels' attachment points",
         cmdUeCheck},
        {"info", "<package>", "a package's header, names, imports and exports by class", cmdUeInfo},
        {"save", "<saved game .dsa>", "what a saved game holds", cmdUeSave},
        {"exports", "<package> [filter]", "a package's exports, those whose class or name has the filter",
         cmdUeExports},
        {"props", "<package> <export>", "an export's properties, and the bytes of its class's own data", cmdUeProps},
        {"extract", "<game folder> <package name> <export> [steps]",
         "writes an export as a file: a texture as .tga (a procedural one after some steps), a sound as .wav",
         cmdUeExtract},
        {"points", "<game folder>", "every mesh's attachment points checked against its bones", cmdUePoints},
        {"probe", "<package | game folder> <export> [-all | -at <offset> <bytes>]",
         "an export's data read as each layout it might have", cmdUeProbe},
        {"script", "<game folder> <class | Class.Function | -all <dir>>",
         "compiled UnrealScript as text: a class, a function, or every class into a folder", cmdUeScript},
        {"run", "<game folder> <map | -all> [seconds]",
         "runs a level's scripts (or every level's) for some seconds, with what they did and what failed", cmdUeRun},
        {"play", "<game folder> <map> [step...]",
         "plays a level without a window, driven by steps (play, wait <s>, press <key>, shot <file.ppm>...)",
         cmdUePlay},
        {"trace", "<game folder> <map> x y z x2 y2 z2 [ex ey ez]",
         "a box moved through a level's BSP: where it stops, and the surface it hit", cmdUeTrace},
        {"ledges", "<game folder> <map> [max | ladders | lights <effect>]",
         "places in a level where the player could climb or hang, to try with play's teleport", cmdUeLedges},
    });
