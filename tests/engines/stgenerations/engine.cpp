// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Generations as a whole, without the game's files: the basic
// engine test, run by ctest and CI. It makes a small game of its own, an
// install and two CDs (archives with a menu, a story and a movie), imports
// it as the launcher does, then plays it as the launcher runs it: the
// title, the captain's menu, a new game (its movie, an automatic save, the
// menu again), a saved game, the settings, loading the save, and quitting.
// What happens goes into a record (counts and checksums:
// tests/support/record.h), compared with
// tests/engines/stgenerations/data/engine-record.txt.
//
//   reluva-tests stgenerations.engine [--update]

#include "common/fs.h"
#include "common/stream.h"
#include "engines/registry.h"
#include "engines/stgenerations/game/generations.h"
#include "engines/stgenerations/game/random.h"
#include "engines/stgenerations/platform.h"
#include "engines/stgenerations/tools/scripted_system.h"
#include "tests/engines/stgenerations/support.h"
#include "tests/support/record.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <map>

using namespace reluva;
using namespace reluva::stgenerations;

namespace {

using Bytes = std::vector<uint8_t>;

void be(Bytes &b, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i)
        b.push_back(uint8_t(v >> (8 * i)));
}
void le(Bytes &b, uint32_t v, int n) {
    for (int i = 0; i < n; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}

// A Cinepak frame, 8 x 4, of one strip: every block V1 from codebook entry
// `entry`, the codebook's entry `entry` grey `luma`.
Bytes cinepakFrame(uint8_t luma) {
    auto chunk = [](int id, const Bytes &body) {
        Bytes b{uint8_t(id)};
        be(b, uint32_t(body.size()) + 4, 3);
        b.insert(b.end(), body.begin(), body.end());
        return b;
    };
    Bytes v1 = {luma, luma, luma, luma, 0, 0};
    Bytes image = {0, 0}; // both blocks from entry 0 (0x32: every block V1)
    Bytes chunks;
    for (const Bytes &c : {chunk(0x22, v1), chunk(0x32, image)})
        chunks.insert(chunks.end(), c.begin(), c.end());
    Bytes strip{0x10};
    be(strip, 12 + uint32_t(chunks.size()), 3);
    be(strip, 0, 2);
    be(strip, 0, 2);
    be(strip, 4, 2);
    be(strip, 8, 2);
    strip.insert(strip.end(), chunks.begin(), chunks.end());
    Bytes f{0};
    be(f, 10 + uint32_t(strip.size()), 3);
    be(f, 8, 2);
    be(f, 4, 2);
    be(f, 1, 2);
    f.insert(f.end(), strip.begin(), strip.end());
    return f;
}

// A movie as the game's: Cinepak frames (10 a second) and 16-bit PCM.
Bytes makeMovie(int frames) {
    auto tag = [](Bytes &b, const char *t) { b.insert(b.end(), t, t + 4); };
    auto riff = [&](Bytes &b, const char *t, const Bytes &body) {
        tag(b, t);
        le(b, uint32_t(body.size()), 4);
        b.insert(b.end(), body.begin(), body.end());
        if (body.size() & 1)
            b.push_back(0);
    };
    auto list = [&](const char *type, const Bytes &body) {
        Bytes b;
        tag(b, type);
        b.insert(b.end(), body.begin(), body.end());
        return b;
    };
    Bytes avih;
    Bytes vstrh;
    Bytes vstrf;
    Bytes astrh;
    Bytes astrf;
    Bytes hdrl;
    Bytes vstrl;
    Bytes astrl;
    Bytes movi;
    Bytes avi;
    le(avih, 100000, 4);
    avih.resize(56);
    tag(vstrh, "vids");
    tag(vstrh, "cvid");
    vstrh.resize(56);
    le(vstrf, 40, 4);
    le(vstrf, 8, 4);
    le(vstrf, 4, 4);
    vstrf.resize(40);
    tag(astrh, "auds");
    astrh.resize(56);
    le(astrf, 1, 2);
    le(astrf, 2, 2);
    le(astrf, 22050, 4);
    le(astrf, 88200, 4);
    le(astrf, 4, 2);
    le(astrf, 16, 2);
    riff(vstrl, "strh", vstrh);
    riff(vstrl, "strf", vstrf);
    riff(astrl, "strh", astrh);
    riff(astrl, "strf", astrf);
    riff(hdrl, "avih", avih);
    riff(hdrl, "LIST", list("strl", vstrl));
    riff(hdrl, "LIST", list("strl", astrl));
    for (int i = 0; i < frames; i++) {
        riff(movi, i ? "00id" : "00db", cinepakFrame(uint8_t(40 + i * 20)));
        riff(movi, "01wb", Bytes(2205 * 4, uint8_t(i))); // a tenth of a second of sound
    }
    riff(avi, "LIST", list("hdrl", hdrl));
    riff(avi, "LIST", list("movi", movi));
    Bytes file;
    riff(file, "RIFF", list("AVI ", avi));
    return file;
}

// A Windows executable naming the game.
Bytes makeExe() {
    Bytes exe(0x200, 0);
    exe[0] = 'M', exe[1] = 'Z', exe[0x3C] = 0x80, exe[0x80] = 'P', exe[0x81] = 'E';
    const std::string title = "Star Trek Generations";
    std::copy(title.begin(), title.end(), exe.begin() + 0x100);
    return exe;
}

// The story: a new game's movie, an automatic save, then the bridge.
const char *const kStory = "[Start]\r\n"
                           "Intro\r\n"
                           "Jump NewGame\r\n"
                           "[NewGame]\r\n"
                           "Movie 1\r\n"
                           "AutoSave\r\n"
                           "ShowStellar\r\n";

// The star map: one star, as UNIVERSE.INI lays them out.
const char *const kUniverse = "[Universe]\r\n"
                              "NumSuns=1\r\n"
                              "Sun1=Amargosa\r\n"
                              "[Amargosa]\r\n"
                              "Location=0,0,0\r\n"
                              "NumPlanets=0\r\n";

// A sound of `ms` milliseconds, as the game's WAVs are (22050 Hz, mono).
Bytes makeWav(int ms) {
    Bytes pcm(size_t(22050) * ms / 1000 * 2, 0);
    for (size_t i = 0; i < pcm.size(); i += 2)
        pcm[i + 1] = uint8_t((i / 50) & 1 ? 0x20 : 0xE0);
    Bytes w = {'R', 'I', 'F', 'F'};
    le(w, uint32_t(36 + pcm.size()), 4);
    for (char c : std::string("WAVEfmt "))
        w.push_back(uint8_t(c));
    le(w, 16, 4);
    le(w, 1, 2);
    le(w, 1, 2);
    le(w, 22050, 4);
    le(w, 44100, 4);
    le(w, 2, 2);
    le(w, 16, 2);
    for (char c : std::string("data"))
        w.push_back(uint8_t(c));
    le(w, uint32_t(pcm.size()), 4);
    w.insert(w.end(), pcm.begin(), pcm.end());
    return w;
}

// A picture of the menu's, `frames` 24 rows high; each name a pattern of
// its own.
Bytes menuPicture(const std::string &name, int frames, int width = 32) {
    const int h = 24 * frames;
    Bytes px(size_t(width) * h, 0);
    // A seed from the name, the same on every system (unlike std::hash).
    size_t seed = 0;
    for (char c : name)
        seed = seed * 31 + uint8_t(c);
    for (size_t i = 0; i < px.size(); i++)
        px[i] = uint8_t(((i % size_t(width)) * 3 + (i / size_t(width)) + seed) % 5 == 0);
    return stgtest::makeBMP8(width, h, px, false);
}

// The game, installed, and its two CDs, in `root`.
void makeGame(const std::filesystem::path &root) {
    auto write = [&](const std::string &name, const Bytes &data) {
        const std::filesystem::path p = root / name;
        std::filesystem::create_directories(p.parent_path());
        writeFile(p, data);
    };
    const Bytes empty = test::buildZip({});
    for (const char *name :
         {"AMARGOS1.DAT", "AMARGOS2.DAT", "ANTILIOS.DAT", "ARVADA1.DAT", "BERSUS.DAT", "ENTERP.DAT", "EPSION.DAT",
          "GALORN1.DAT", "GALORN2.DAT", "HALEE1.DAT", "QURASH.DAT", "VERID1.DAT", "COMMON.DAT"})
        write(std::string("install/DATA/") + name, empty);
    // The menu's picture (two colours) and the title's.
    Bytes captain(64 * 48, 0);
    for (size_t i = 0; i < captain.size(); i++)
        captain[i] = uint8_t((i / 64 + i % 64) % 7 == 0);
    std::vector<test::ZipFile> options = {{"CAPTAINS.BMP", stgtest::makeBMP8(64, 48, captain, false), {}},
                                          {"INTRO1.BMP", stgtest::makeBMP8(4, 4, Bytes(16, 1), false), {}}};
    const std::pair<const char *, int> pictures[] = {
        {"LOAD.BMP", 3},     {"SAVE.BMP", 3},     {"NEW.BMP", 3},      {"QUIT.BMP", 3},     {"SETTINGS.BMP", 3},
        {"CONTROLS.BMP", 3}, {"RETURN.BMP", 3},   {"ACCEPT.BMP", 2},   {"CANCEL.BMP", 2},   {"AUTOSAVE.BMP", 1},
        {"DIFFICUL.BMP", 3}, {"DIF-OPS.BMP", 3},  {"WINDOW.BMP", 2},   {"WIN-OPS.BMP", 2},  {"SUBTITL.BMP", 2},
        {"SUB-OPS.BMP", 2},  {"STARBTTN.BMP", 2}, {"AWAYBTTN.BMP", 2}, {"LKSPRING.BMP", 2}, {"LKSONOFF.BMP", 2},
        {"OPTSAVE.BMP", 1},  {"OPTSET.BMP", 1},   {"OPTNEW.BMP", 1},   {"OPTQUIT.BMP", 1},  {"AWAY.BMP", 1},
        {"STARSHIP.BMP", 1}, {"NAMEBAR.BMP", 1},  {"VOL-ON.BMP", 1},   {"KEYHIGH.BMP", 1},  {"SAVELOAD.BMP", 4}};
    for (const auto &[name, frames] : pictures)
        options.push_back({name, menuPicture(name, frames), {}});
    // Its sounds: the clicks (X_1000..X_1003) and the menu's music (M_2001).
    for (const char *name : {"X_1000.WAV", "X_1001.WAV", "X_1002.WAV", "X_1003.WAV", "M_2001.WAV"})
        options.push_back({name, makeWav(name[0] == 'M' ? 3000 : 120), {}});
    write("install/DATA/OPTIONS.DAT", test::buildZip(options));
    write("install/sgens.exe", makeExe());
    write("install/sgens.ini", test::toBytes("[Settings]\r\n"));
    // The CDs: their setups, the story, the music archives, the movie.
    for (int disc = 1; disc <= 2; disc++) {
        const std::string d = "cd" + std::to_string(disc) + "/";
        write(d + "SETUP.EXE", test::toBytes("setup"));
        write(d + "GENS/COMMON.DAT", empty);
        write(d + "GENS/COMBAT.DAT", empty);
        write(d + "GENS/STELLAR.DAT",
              test::buildZip({{"DESIGN/STATES.INI", test::toBytes(kStory), {}},
                              {"DESIGN/UNIVERSE.INI", test::toBytes(kUniverse), {}},
                              {"GUI/GUI-BACK.BMP", stgtest::makeBMP8(8, 8, Bytes(64, 1), false), {}}}));
        write(d + "GENS/MUSIC." + std::to_string(disc), empty);
    }
    write("cd1/GENS/MOVIE/MOV_01.AVI", makeMovie(5));
}

std::string hashOf(const Surface &s) {
    return test::Hash().bytes(s.pixels).hex();
}

std::string fileHash(const std::filesystem::path &p) {
    Bytes data;
    return readFile(p, data) ? test::Hash().bytes(data).hex() + " (" + std::to_string(data.size()) + " bytes)"
                             : std::string("none");
}

void engineTest(const std::filesystem::path &tmp) {
    test::Record rec;
    const MetaEngine *engine = findEngine("stgenerations");
    if (!engine)
        return test::fail("stgenerations isn't registered");
    makeGame(tmp / "src");

    // Found and imported as the launcher does: the CDs side by side with
    // the install.
    rec.add("detect.source", int64_t(engine->detect(tmp / "src").size()));
    rec.add("detect.install", engine->detect(tmp / "src/install").empty()
                                  ? std::string("none")
                                  : engine->detect(tmp / "src/install").front().description);
    std::vector<ImportPlan> plans = engine->planImport(tmp / "src");
    if (plans.size() != 1)
        return rec.fail("import.plan", std::to_string(plans.size()) + " plans");
    test::Hash planHash;
    for (const ImportPlan::File &f : plans[0].files)
        planHash.text(f.to).i32(int32_t(f.size));
    rec.add("import.files", int64_t(plans[0].files.size()));
    rec.add("import.list", planHash.hex());
    rec.add("import.notes", plans[0].notes.empty() ? std::string() : plans[0].notes.front());
    const std::filesystem::path game = tmp / "library/stg";
    if (Error err = runImport(plans[0], game, [](uint64_t, uint64_t) { return true; }))
        return rec.fail("import.run", err.message);
    const std::vector<DetectedGame> found = engine->detect(game);
    if (found.size() != 1)
        return rec.fail("detect.imported", std::to_string(found.size()) + " games");
    rec.add("detect.imported", found[0].gameId + ": " + found[0].description);

    // Played as the launcher runs it.
    // Saves dated the same whenever the test runs.
    GameTarget target{"stg",
                      found[0].engineId,
                      found[0].gameId,
                      found[0].description,
                      game,
                      tmp / "saves",
                      {{"fixed_save_date", "01/01 00:00"}}};
    ScriptedSystem sys;
    std::map<uint64_t, std::string> marks; // what each moment shows
    std::vector<std::pair<uint64_t, std::string>> seen;
    int frames = 0;
    std::string last;
    std::string cursorSeen = "never shown";
    sys.onFrame = [&](const Surface &s, uint64_t ms) {
        ++frames;
        last = hashOf(s);
        while (!marks.empty() && marks.begin()->first <= ms) {
            if (marks.begin()->second == "menu")
                cursorSeen = sys.cursor().empty()
                                 ? std::string("the system's")
                                 : std::to_string(sys.cursor().w) + "x" + std::to_string(sys.cursor().h) + " " +
                                       test::Hash().bytes(sys.cursor().rgba).hex();
            seen.push_back({marks.begin()->first, marks.begin()->second + ": " + last});
            marks.erase(marks.begin());
        }
    };
    marks[2000] = "title";
    sys.key(3000, key::Space); // the title skipped: the menu
    marks[3500] = "menu";
    sys.click(4000, 165, 101); // New
    marks[4500] = "new game?";
    sys.click(5000, 418, 404); // Accept: the story starts, with its movie
    marks[5150] = "movie, frame 1";
    marks[5450] = "movie, frame 4";
    marks[7000] = "bridge";
    sys.click(7500, 575, 446); // OPTIONS
    marks[8000] = "menu over the bridge";
    sys.click(8500, 165, 71); // Save
    sys.click(9000, 280, 73); // its first slot
    sys.type(9500, "Kirk");
    marks[10000] = "save, named";
    sys.click(10500, 417, 400); // Accept: saved, back on the bridge
    marks[11000] = "saved";
    sys.click(11500, 575, 446); // OPTIONS
    sys.click(12000, 165, 161); // Settings
    marks[12500] = "settings";
    sys.click(13000, 227 + 190, 29 + 195); // the difficulty, a step round
    sys.click(13500, 418, 404);            // Accept
    sys.click(14000, 165, 41);             // Load
    sys.click(14500, 280, 73);             // the first slot
    marks[15000] = "load";
    sys.click(15500, 418, 404); // Accept: back on the bridge as saved
    marks[17000] = "loaded";
    sys.click(17500, 575, 446); // OPTIONS
    sys.click(18000, 165, 131); // Quit
    sys.click(18500, 418, 404); // Accept: the credits, then out
    sys.quit(30000);

    RandomSource::setFixedSeed(1);
    std::unique_ptr<Engine> run = engine->create(sys, target);
    const Error err = run->run();
    run.reset();
    RandomSource::setFixedSeed(0);
    rec.add("run", err ? "failed: " + err.message : std::string("ok"));
    rec.add("run.frames", frames);
    rec.add("run.size", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
    rec.add("run.title", sys.title());
    for (const auto &[at, what] : seen)
        rec.add("screen." + std::to_string(at), what);
    rec.add("cursor.menu", cursorSeen);
    rec.add("cursor.after", sys.cursor().empty() ? std::string("the system's") : std::string("the game's"));
    rec.add("run.until", int64_t(sys.getMillis()));
    for (const char *name : {"stg.s00", "stg.s09"})
        rec.add(std::string("save.") + name, fileHash(tmp / "saves" / name));
    rec.add("settings", fileHash(tmp / "saves/settings.ini"));

    // The resource viewer: the settings' volumes reach the mixer there too.
    {
        ScriptedSystem viewer;
        audio::Mixer mixer(viewer.audioRate());
        viewer.attachMixer(&mixer);
        Platform platform(viewer, &mixer, tmp / "viewer-saves");
        Options o;
        o.gameDir = game;
        o.viewer = true;
        o.musicVolume = 128;
        o.speechVolume = 64;
        viewer.quit(500);
        Generations g(&platform, o);
        const Error verr = g.run();
        rec.add("viewer", verr ? "failed: " + verr.message : std::string("ok"));
        rec.add("viewer.volumes", std::to_string(mixer.typeVolume(audio::SoundType::Music)) + " music, " +
                                      std::to_string(mixer.typeVolume(audio::SoundType::Speech)) + " speech, " +
                                      std::to_string(mixer.typeVolume(audio::SoundType::Effects)) + " effects");
    }

    rec.check(RELUVA_SOURCE_DIR "/tests/engines/stgenerations/data/engine-record.txt", "stgenerations.engine",
              "tests/engines/stgenerations/engine.cpp");
}

} // namespace

static const reluva::test::Suite
    suite("stgenerations.engine", {
                                      {"import and play", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                  });
