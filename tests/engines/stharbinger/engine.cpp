// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Harbinger as a whole, without the game's files: the basic engine test, run
// by ctest and CI. It makes the two CDs of its own in miniature (the scripts
// packed in disc 1's SSE/SSE.ALL as on the real disc, the data under DS9,
// a video on disc 2), finds and imports them as the launcher does, and plays
// the import through the engine: the opening video with its sound, a scene
// with an animation and a sound, a click on a hotspot that changes scene,
// then back to the launcher. What happens goes into a record (counts and
// checksums of the screens, the palette, the sound and the files written),
// compared with tests/engines/stharbinger/data/engine-record.txt.
//
//   reluva-tests stharbinger.engine [--update]

#include "common/headless_system.h"
#include "engines/registry.h"
#include "tests/common/data/smacker_videos.h"
#include "tests/engines/stharbinger/fixtures.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::stharbinger;
using namespace reluva::stharbinger::testing;
using test::Hash;

namespace {

// The headless System with a player: events at given times, and the sound
// pulled as the clock moves (44100 Hz). It keeps a checksum of every screen
// shown, every palette set, and the sound.
class PlayingSystem : public HeadlessSystem {
public:
    struct Input {
        uint64_t at;
        Event event;
    };
    std::vector<Input> inputs;
    Hash screens, palettes, sound;
    int shown = 0, paletteSets = 0;
    int64_t soundFrames = 0, soundPeak = 0;
    std::vector<std::string> titles;

    void updateScreen(const Surface& s) override {
        HeadlessSystem::updateScreen(s);
        screens.bytes(s.pixels);
        shown++;
    }
    void setPalette(const Color* colors, int start, int count) override {
        palettes.i32(start).i32(count).bytes(colors, size_t(count) * sizeof(Color));
        paletteSets++;
    }
    void setWindowTitle(const std::string& t) override {
        if (titles.empty() || titles.back() != t)
            titles.push_back(t);
    }
    bool pollEvent(Event& ev) override {
        if (next_ < inputs.size() && inputs[next_].at <= getMillis()) {
            ev = inputs[next_++].event;
            return true;
        }
        return false;
    }
    void delayMillis(uint32_t ms) override {
        HeadlessSystem::delayMillis(ms);
        if (!audio_)
            return;
        pending_ += int64_t(ms) * 44100;
        const int frames = int(pending_ / 1000);
        pending_ %= 1000;
        std::vector<int16_t> buf(size_t(frames) * 2);
        audio_(buf.data(), frames);
        sound.bytes(buf.data(), buf.size() * 2);
        soundFrames += frames;
        for (int16_t v : buf)
            soundPeak = std::max<int64_t>(soundPeak, v < 0 ? -int64_t(v) : v);
    }
    Error startAudio(AudioCallback cb) override {
        audio_ = std::move(cb);
        return Error::success();
    }
    void stopAudio() override { audio_ = nullptr; }
    int audioRate() const override { return 44100; }

private:
    size_t next_ = 0;
    AudioCallback audio_;
    int64_t pending_ = 0;
};

Event mouse(EventType type, int x, int y) {
    Event e;
    e.type = type;
    e.button = MouseButton::Left;
    e.mouseX = x;
    e.mouseY = y;
    return e;
}

// NAME.ALL, NAME.LST and their bytes, for packing into SSE.ALL.
struct Packed {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    void archive(const std::string& name, const std::vector<std::vector<uint8_t>>& entries) {
        std::vector<uint8_t> all, lst;
        for (const auto& e : entries) {
            putU32(lst, uint32_t(all.size()));
            all.insert(all.end(), e.begin(), e.end());
        }
        putU32(lst, uint32_t(all.size()));
        files.push_back({name + ".ALL", all});
        files.push_back({name + ".LST", lst});
    }
    // SSE.ALL, .LST and .CAT holding them, as disc 1's installer archive.
    void write(const std::filesystem::path& dir) {
        std::vector<uint8_t> all, lst;
        std::string cat = std::to_string(files.size()) + "\r\n";
        for (const auto& [name, data] : files) {
            putU32(lst, uint32_t(all.size()));
            all.insert(all.end(), data.begin(), data.end());
            cat += name + "\r\n";
        }
        putU32(lst, uint32_t(all.size()));
        putFile(dir / "SSE.ALL", all);
        putFile(dir / "SSE.LST", lst);
        putFile(dir / "SSE.CAT", bytes(cat));
    }
};

void makeDiscs(const std::filesystem::path& cd1, const std::filesystem::path& cd2) {
    // Scene S0: the opening video (ACTIONSEQ 28, on disc 2), an animation and
    // a sound, a hotspot to S1. S1 counts its visits.
    ScriptBuilder s0, s1;
    s0.cmd(kCmdScene, {sym(0)})
        .cmd(kCmdActionSeq, {num(28), var(0)})
        .cmd(kCmdAnimDef, {sym(0), sym(kAnimModeClear)})
        .cmd(kCmdAnim, {sym(0), sym(kAnimStart), num(0)})
        .cmd(kCmdAnimSeq, {sym(0), sym(kAnimOn)})
        .cmd(kCmdSound, {sym(0), sym(1)})
        .cmd(kCmdHotspot, {sym(0), num(100), num(100), num(100), num(100), sym(1), sym(kCursorTouch)})
        .cmd(kCmdHotOn, {sym(0)})
        .cmd(kCmdReturn)
        .cmd(kCmdLabel, {sym(1)})
        .cmd(kCmdNewScene, {sym(1)});
    s1.cmd(kCmdScene, {sym(1)}).cmd(kCmdAdd, {num(1), var(0)}).cmd(kCmdReturn);
    Packed sse;
    sse.archive("CC", {s0.build(), s1.build()});
    sse.archive("CV", {nameList({"video", "visits"}, "S0.CCG"), nameList({"visits"}, "S1.CCG")});
    sse.archive("CH", {nameList({"door"}, "S0.CCG"), {}});
    sse.archive("CA", {nameList({"spin"}, "S0.CCG"), {}});
    sse.archive("CS", {nameList({"beep"}, "S0.CCG"), {}});
    sse.write(cd1 / "SSE");

    putFile(cd1 / "DS9" / "TORPEDOS" / "CD", bytes("0\r\n"));
    putFile(cd1 / "DS9" / "ANIMS" / "SPIN" / "SPIN.RLE", makeRle());
    std::vector<uint8_t> beep;
    for (int i = 0; i < 4410; i++)
        beep.push_back(uint8_t(128 + (i / 25 % 2 ? 40 : -40)));
    putFile(cd1 / "DS9" / "SOUND" / "BEEP.RAW", beep);
    std::vector<uint8_t> pal(768);
    for (size_t i = 0; i < pal.size(); i++)
        pal[i] = uint8_t(i / 3 % 64);
    putFile(cd1 / "DS9" / "PALETTES" / "S0.PAL", pal);

    putFile(cd2 / "DS9" / "TORPEDOS" / "CD", bytes("1\r\n"));
    putFile(cd2 / "DS9" / "INTRO" / "VNM.SMK",
            std::vector<uint8_t>(kSmackerTest2, kSmackerTest2 + sizeof kSmackerTest2));
}

std::string listFiles(const std::filesystem::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec))
            names.push_back(it->path().lexically_relative(dir).generic_string() + " " +
                            std::to_string(it->file_size(ec)));
    std::sort(names.begin(), names.end());
    std::string out;
    for (const std::string& n : names)
        out += (out.empty() ? "" : ", ") + n;
    return out.empty() ? "(none)" : out;
}

void engineTest(const std::filesystem::path& tmp) {
    test::Record rec;
    const MetaEngine* engine = findEngine("stharbinger");
    if (!engine)
        return test::fail("stharbinger isn't registered");
    makeDiscs(tmp / "cd1", tmp / "cd2");

    // Found on the CDs as the launcher finds them: disc 1 plays alone (its
    // scenes, not disc 2's), disc 2 doesn't.
    auto found = engine->detect(tmp / "cd1");
    rec.add("detect.disc1", found.empty() ? "(none)" : found[0].gameId + " " + found[0].description);
    rec.add("detect.disc2", int64_t(engine->detect(tmp / "cd2").size()));

    // Imported from disc 1, with disc 2 added, into the library.
    auto plans = engine->planImport(tmp / "cd1");
    if (plans.size() != 1)
        return rec.fail("import.plan", std::to_string(plans.size()) + " plans"), rec.check(
            RELUVA_SOURCE_DIR "/tests/engines/stharbinger/data/engine-record.txt", "stharbinger.engine",
            "tests/engines/stharbinger/engine.cpp");
    std::string message;
    const bool added = engine->addToImport(plans[0], tmp / "cd2", message);
    Hash planned;
    for (const auto& f : plans[0].files)
        planned.text(f.to).i32(int32_t(f.size));
    rec.add("import.plan", std::to_string(plans[0].files.size()) + " files " + planned.hex() +
                               (added ? " +disc2" : " " + message));
    const std::filesystem::path library = tmp / "library" / "ds9harbinger";
    if (Error err = runImport(plans[0], library, [](uint64_t, uint64_t) { return true; }))
        rec.fail("import.copy", err.message);
    found = engine->detect(library);
    rec.add("detect.library", found.empty() ? "(none)" : found[0].gameId + " " + found[0].description);

    // Played: straight into S0 (the developer setting), the video plays
    // with its sound, then a click on the hotspot after 2 s takes it to S1;
    // back to the launcher after 3 s.
    GameTarget target{"ds9harbinger", "stharbinger", "ds9harbinger", "Harbinger", library, tmp / "saves",
                      {{"start_scene", "S0"}, {"music_volume", "100"}}};
    PlayingSystem sys;
    sys.inputs = {{2000, mouse(EventType::MouseMove, 150, 150)},
                  {2020, mouse(EventType::MouseDown, 150, 150)},
                  {3000, Event{EventType::ReturnToLauncher}}};
    auto game = engine->create(sys, target);
    const Error err = game->run();
    rec.add("run.result", err ? "error: " + err.message : std::string("ok"));
    rec.add("run.size", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
    rec.add("run.screens", std::to_string(sys.shown) + " " + sys.screens.hex());
    rec.add("run.palettes", std::to_string(sys.paletteSets) + " " + sys.palettes.hex());
    rec.add("run.sound", std::to_string(sys.soundFrames) + " frames, peak " + std::to_string(sys.soundPeak) + " " +
                             sys.sound.hex());
    std::string titles;
    for (const std::string& t : sys.titles)
        titles += (titles.empty() ? "" : " / ") + t;
    rec.add("run.titles", titles);
    rec.add("run.files", listFiles(tmp / "saves"));

    rec.check(RELUVA_SOURCE_DIR "/tests/engines/stharbinger/data/engine-record.txt", "stharbinger.engine",
              "tests/engines/stharbinger/engine.cpp");
}

} // namespace

static const reluva::test::Suite suite("stharbinger.engine", {
    {"play through", [](const std::filesystem::path& tmp) { engineTest(tmp); }},
});
