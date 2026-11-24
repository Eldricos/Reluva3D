// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own and takes the engine
// through what playing does, short of the game's own program, which only
// the game's files hold (the game-files test runs that: gamedata.cmake):
//   - an empty folder, and the install (its files' names, made-up
//     contents) found and planned for import, with a copy of the CD's
//     scripts beside it;
//   - the engine's options, and the settings each makes (playSettings);
//   - frames held to the frame rate, on a made-up clock (FramePacer);
//   - the process set up: its registry, as the installer and the setup
//     (CONFIG.EXE) leave it, then with the settings, and the joystick;
//   - the program loaded, played and run by the engine, each failing as it
//     should on made-up files.
// What it does goes into a record (counts, checksums: tests/support/record.h),
// compared with tests/engines/legoisland/data/engine-record.txt.
//
//   reluva-tests legoisland.engine [--update]
//
// --update writes the run's record as the new one: do that only after a
// change meant to alter behaviour, and look at the record's diff first.

#include "common/headless_system.h"
#include "engines/legoisland/host/game_process.h"
#include "engines/legoisland/host/player.h"
#include "engines/legoisland/play_settings.h"
#include "engines/registry.h"

#include "tests/support/record.h"
#include "tests/support/test.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace reluva;
using namespace reluva::legoisland;

namespace {

using test::Hash;
using test::Record;

void writeText(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// `text` with `dir` taken out, as the record mustn't hold where the test ran.
std::string withoutDir(std::string text, const std::filesystem::path &dir) {
    const std::string d = dir.string();
    for (size_t at = text.find(d); at != std::string::npos; at = text.find(d, at))
        text.replace(at, d.size(), "<dir>");
    return text;
}

// An install as the installer leaves it, by its files' names: what
// detection and the import look for, none of it the game's.
void makeInstall(const std::filesystem::path &dir) {
    writeText(dir / "ISLE.EXE", "not a program");
    writeText(dir / "LEGO1.DLL", "not a library");
    writeText(dir / "lego" / "data" / "WORLD.WDB", "not a world");
}

// The CD's scripts, the two the game starts with, beside the install.
void makeScripts(const std::filesystem::path &cd) {
    writeText(cd / "LEGO" / "SCRIPTS" / "INTRO.SI", "not a script");
    writeText(cd / "LEGO" / "SCRIPTS" / "ISLE" / "ISLE.SI", "not a script either");
}

void found(const MetaEngine &engine, const std::filesystem::path &tmp, Record &rec) {
    const std::filesystem::path empty = tmp / "empty";
    std::filesystem::create_directories(empty);
    rec.add("detect.empty", int64_t(engine.detect(empty).size()));
    rec.add("import.empty", int64_t(engine.planImport(empty).size()));

    const std::filesystem::path install = tmp / "install";
    makeInstall(install);
    const std::vector<DetectedGame> games = engine.detect(install);
    rec.add("detect.install", int64_t(games.size()));
    for (const DetectedGame &g : games)
        rec.add("detect.install.game", g.gameId + " " + g.description + " " + g.language + " " + g.platform);

    const std::filesystem::path both = tmp / "both";
    makeInstall(both / "install");
    makeScripts(both / "cd");
    for (const ImportPlan &plan : engine.planImport(both)) {
        rec.add("import.game", plan.game.gameId + " " + plan.game.description);
        Hash files;
        for (const ImportPlan::File &f : plan.files)
            files.text(f.to).text(std::to_string(f.size));
        rec.add("import.files",
                std::to_string(plan.files.size()) + " " + std::to_string(plan.totalBytes()) + " " + files.hex());
        for (const std::string &note : plan.notes)
            rec.add("import.note", withoutDir(note, tmp));
    }
}

std::string describe(const PlaySettings &s) {
    std::string out = std::to_string(s.frameRate) + (s.turningByTime ? " time" : " frame") +
                      (s.joystick ? " joystick" : " -") + (s.gpu ? " gpu" : "");
    for (const auto &[name, value] : s.registry)
        out += ", " + name + "=" + value;
    return out;
}

void options(const MetaEngine &engine, Record &rec) {
    const std::vector<GameOption> options = engine.engineOptions();
    rec.add("options", int64_t(options.size()));
    // Each one's first choice is its default: what the game is played
    // with when it's set nowhere, which playSettings has to agree with.
    std::map<std::string, std::string> defaults;
    for (const GameOption &o : options) {
        std::string line = o.label;
        if (o.choices.empty()) {
            line += o.defaultOn ? " [on]" : " [off]";
            defaults[o.key] = o.defaultOn ? "1" : "0";
        } else {
            defaults[o.key] = o.choices.front().first;
            for (const auto &[value, label] : o.choices)
                line += " | " + value + " " + label;
        }
        rec.add("options." + o.key, line);
    }
    const std::string unset = describe(playSettings({}));
    rec.add("settings.unset", unset);
    rec.add("settings.defaults", describe(playSettings(defaults)));
    PlaySettings fromDefaults = playSettings(defaults);
    fromDefaults.registry.clear();
    PlaySettings fromNothing = playSettings({});
    if (describe(fromDefaults) != describe(fromNothing))
        test::fail("the options' defaults aren't what the game plays with unset: " + describe(fromDefaults) +
                   " against " + unset);
    rec.add("settings.original",
            describe(playSettings({{"turning", "frame"}, {"framerate", "20"}, {"drawing", "processor"}})));
    rec.add("settings.changed", describe(playSettings({{"framerate", "0"},
                                                       {"drawing", "gpu"},
                                                       {"device", "software"},
                                                       {"islandquality", "1"},
                                                       {"islandtexture", "0"},
                                                       {"music", "0"},
                                                       {"sound3d", "1"},
                                                       {"drawcursor", "1"},
                                                       {"usejoystick", "1"}})));
    rec.add("settings.unreadable", describe(playSettings({{"framerate", "fast"}, {"turning", "sideways"}})));
    rec.add("settings.negative", describe(playSettings({{"framerate", "-30"}})));
}

void pacing(Record &rec) {
    using std::chrono::microseconds;
    const FramePacer::Clock::time_point start{};
    // Frames ready at these times (microseconds), and when each is shown.
    auto run = [&](int rate, const std::vector<int> &ready) {
        FramePacer pacer;
        pacer.setRate(rate);
        std::string out;
        for (const int at : ready) {
            const auto shown = pacer.next(start + microseconds(at));
            out += (out.empty() ? "" : " ") +
                   std::to_string(std::chrono::duration_cast<microseconds>(shown - start).count());
        }
        return out;
    };
    // At once, then held one a 60th apart; a late one shown when it's
    // ready, the next a 60th after it, not sooner to catch up.
    rec.add("pacing.60", run(60, {0, 0, 1000, 20000, 100000, 101000, 200000}));
    rec.add("pacing.20", run(20, {0, 10000, 20000, 30000}));
    rec.add("pacing.unlimited", run(0, {0, 0, 1000, 1000}));
}

void process(const std::filesystem::path &tmp, Record &rec) {
    const std::filesystem::path install = tmp / "install";
    GameProcess game(install, 16);
    x86::Win32Process &p = game.process();
    const std::string key = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Mindscape\\LEGO Island";
    auto registry = [&] {
        std::string out;
        for (const char *name :
             {"diskpath", "cdpath", "savepath", "3D Device ID", "3D Device Name", "Display Bit Depth", "Flip Surfaces",
              "Full Screen", "Back Buffers in Video RAM", "Wide View Angle", "Island Quality", "Island Texture",
              "Music", "3DSound", "Draw Cursor", "UseJoystick", "JoystickIndex"}) {
            const auto v = p.registryValue(key, name);
            std::string text = v ? std::string(v->begin(), v->end()) : "(unset)";
            if (!text.empty() && text.back() == '\0')
                text.pop_back();
            out += std::string(out.empty() ? "" : ", ") + name + "=" + text;
        }
        return out;
    };
    rec.add("process.registry", registry());
    rec.add("process.joystick", p.joystick.present ? "yes" : "no");
    const PlaySettings settings =
        playSettings({{"device", "software"}, {"islandquality", "0"}, {"music", "0"}, {"usejoystick", "1"}});
    for (const auto &[name, value] : settings.registry)
        game.setSetting(name, value);
    p.joystick.present = settings.joystick;
    rec.add("process.registry.set", registry());
    rec.add("process.joystick.set", p.joystick.present ? "yes" : "no");

    rec.add("process.load", withoutDir(game.load().message, tmp));
    rec.add("process.turning", game.turningByTime() ? "by time" : "by frame");
    GameProcess none(tmp / "empty", 16);
    rec.add("process.load.empty", withoutDir(none.load().message, tmp));
}

void play(const MetaEngine &engine, const std::filesystem::path &tmp, Record &rec) {
    // The player: its thread loads the program, which fails, and it stops.
    {
        Player player(tmp / "install", tmp / "save");
        player.setFrameRate(60);
        player.start();
        bool ended = false;
        std::string error;
        const auto give = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!player.finished(ended, error) && std::chrono::steady_clock::now() < give)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        rec.add("player.ended", ended ? "yes" : "no");
        rec.add("player.error", withoutDir(error, tmp));
        rec.add("player.frames", int64_t(player.framesShown()));
    }
    // The engine, as the launcher runs it: without the program it says so,
    // and with made-up files it stops with the player's error.
    auto run = [&](const std::filesystem::path &dir) {
        HeadlessSystem sys;
        GameTarget target;
        target.engineId = engine.id();
        target.gameId = "legoisland";
        target.description = "LEGO Island";
        target.path = dir;
        target.savePath = tmp / "engine-save";
        target.settings = {{"framerate", "0"}};
        const std::unique_ptr<Engine> e = engine.create(sys, target);
        const Error error = e->run();
        return withoutDir(error.message, tmp) + " (" + std::to_string(sys.frames()) + " frames shown, title \"" +
               sys.title() + "\")";
    };
    rec.add("engine.empty", run(tmp / "empty"));
    rec.add("engine.install", run(tmp / "install"));
}

void engineTest(const std::filesystem::path &tmp) {
    Record rec;
    const MetaEngine *engine = findEngine("legoisland");
    if (!engine)
        return test::fail("legoisland isn't registered");
    rec.add("engine", std::string(engine->id()) + " " + engine->name());
    found(*engine, tmp, rec);
    options(*engine, rec);
    pacing(rec);
    process(tmp, rec);
    play(*engine, tmp, rec);
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/legoisland/data/engine-record.txt", "legoisland.engine",
              "tests/engines/legoisland/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("legoisland.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
