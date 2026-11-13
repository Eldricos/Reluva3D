// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The AWE engine as a whole, without the game's files: the basic engine
// test, run by ctest and CI. It makes a small install of its own (stand-ins
// for motoe.exe and granny2.dll, an acts.txt of one act, the main menu's
// scene with its Play hotspot, two scenes of a place of its own and their
// pictures), finds it as the launcher does, and plays it through the
// engine: a new game, Play, a click on a hotspot that sets a tag and goes
// to the next scene, a quick save, then back to the launcher. What happens
// goes into a record (counts and checksums of the screens, the titles and
// the files written), compared with tests/engines/awe/data/engine-record.txt.
//
//   reluva-tests awe.engine [--update]

#include "common/headless_system.h"
#include "image/image.h"
#include "engines/awe/formats/savegame.h"
#include "engines/registry.h"
#include "tests/engines/awe/fixtures.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::awe::testing;
using test::Hash;

namespace {

// The headless System with a player: events at given times. It keeps a
// checksum of every screen shown.
class PlayingSystem : public HeadlessSystem {
public:
    struct Input {
        uint64_t at;
        Event event;
    };
    std::vector<Input> inputs;
    Hash screens;
    int shown = 0;
    std::vector<std::string> titles;

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        screens.bytes(s.pixels);
        shown++;
    }
    void setWindowTitle(const std::string &t) override {
        // The frames a second after the title go with the clock: the
        // titles themselves are what's kept.
        const std::string title = t.substr(0, t.find("  -  "));
        if (titles.empty() || titles.back() != title)
            titles.push_back(title);
    }
    bool pollEvent(Event &ev) override {
        if (_next < inputs.size() && inputs[_next].at <= getMillis()) {
            ev = inputs[_next++].event;
            return true;
        }
        return false;
    }

private:
    size_t _next = 0;
};

Event key(uint32_t k) {
    Event e;
    e.type = EventType::kKeyDown;
    e.key = k;
    return e;
}

Event mouse(EventType type, int x, int y) {
    Event e;
    e.type = type;
    e.button = MouseButton::kLeft;
    e.mouseX = x;
    e.mouseY = y;
    e.clicks = type == EventType::kMouseDown ? 1 : 0;
    return e;
}

void putFile(const std::filesystem::path &p, const std::vector<uint8_t> &data) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
}

// A picture of its own: stripes of `colour`, `w` by `h`.
std::vector<uint8_t> picture(int w, int h, Color colour) {
    image::Image img;
    img.w = w, img.h = h;
    img.rgba.resize(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t *p = &img.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
            const bool stripe = (x / 16 + y / 16) % 2;
            p[0] = stripe ? colour.r : uint8_t(colour.r / 2);
            p[1] = stripe ? colour.g : uint8_t(colour.g / 2);
            p[2] = stripe ? colour.b : uint8_t(colour.b / 2);
            p[3] = 255;
        }
    return image::encodeTga(img);
}

// A scene: its picture at the top left, and a hotspot over most of the
// screen running `script` (named `name`, as the menu's Play is).
std::vector<uint8_t> scene(const std::string &path, const std::string &picture, const std::string &name,
                           const std::string &script) {
    Bytes b;
    b.tag("SCEN");
    b.text(path, 256);
    b.tag("LOAD");
    b.u16(1);
    b.tag("2DOB");
    b.u16(4); // file, name, two numbers
    b.text(picture, 256);
    b.text("", 256);
    b.u32(0);
    b.u32(1);
    if (!script.empty()) {
        b.tag("SPOT");
        b.u16(28); // the common fields up to the rectangle and the script
        b.text("", 256);
        b.text(name, 256);
        b.u32(0);
        b.u32(1);
        for (float v : {0.0f, 0.0f, 5000.0f})
            b.f32(v);
        for (int i = 0; i < 13; i++)
            b.f32(1);
        b.f32(0);
        for (uint32_t v : {200u, 200u, 824u, 600u})
            b.u32(v);
        b.text(script, 256);
        b.u16(0); // listA
        b.u16(0); // listB
    }
    b.tag("DONE");
    return b.d;
}

void makeInstall(const std::filesystem::path &dir) {
    putFile(dir / "motoe.exe", {'M', 'Z', 0, 0});
    putFile(dir / "granny2.dll", {'M', 'Z', 0, 0});
    putFile(dir / "Text" / "itemlist.txt", utf16File("Item Name\tIn-game Item Description\n"));
    // One act: its place, then its first scene.
    putFile(dir / "Text" / "acts.txt",
            utf16File("Act\tPreLoad\tLocation to Load\tPost Load\tTime of Day\tWeather\tTrain Moving\t"
                      "Completion Conditions\tEvent\tMusic\tWhat to do\n"
                      "01A\t0\ttest\t\"scene test_01 1 0\"\tday\tnone\tNo\t0\t0\t0\t0\n"));
    putFile(dir / "Locations" / "Menu.Main" / "menu.main_01.ac2",
            scene("Locations\\Menu.Main\\menu.main_01.ac2", "locations\\menu.main\\menu.tga", "playover", "act 01a"));
    putFile(dir / "Locations" / "Menu.Main" / "menu.tga", picture(320, 240, {200, 160, 60}));
    putFile(
        dir / "Locations" / "Test" / "test_01.ac2",
        scene("Locations\\Test\\test_01.ac2", "locations\\test\\one.tga", "door", "tag opened 1, scene test_02 1 0"));
    putFile(dir / "Locations" / "Test" / "test_02.ac2",
            scene("Locations\\Test\\test_02.ac2", "locations\\test\\two.tga", "", ""));
    putFile(dir / "Locations" / "Test" / "one.tga", picture(320, 240, {60, 160, 200}));
    putFile(dir / "Locations" / "Test" / "two.tga", picture(320, 240, {160, 200, 60}));
}

// Every file under `dir`, and for a saved game what it holds (not its
// bytes: it keeps the date and time it was made).
std::string listFiles(const std::filesystem::path &dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) {
            std::ifstream in(it->path(), std::ios::binary);
            const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
            std::string what = it->path().lexically_relative(dir).generic_string();
            awe::SaveGame save;
            if (it->path().extension() == ".ac2" && !awe::readSaveGame(data, save)) {
                what += " (" + save.location + ", " + save.scene + "; tags";
                for (const auto &[tag, value] : save.tags)
                    what += " " + tag + "=" + std::to_string(value);
                what += ")";
            }
            names.push_back(what);
        }
    std::sort(names.begin(), names.end());
    std::string out;
    for (const std::string &n : names)
        out += (out.empty() ? "" : ", ") + n;
    return out.empty() ? "(none)" : out;
}

void engineTest(const std::filesystem::path &tmp) {
    test::Record rec;
    const MetaEngine *engine = findEngine("awe");
    if (!engine)
        return test::fail("awe isn't registered");
    const std::filesystem::path install = tmp / "motoe";
    makeInstall(install);

    // Found as the launcher finds it: by its files, whatever their contents
    // (no release's signatures match the stand-ins).
    const auto found = engine->detect(install);
    rec.add("detect", found.empty() ? "(none)" : found[0].gameId + " " + found[0].description);

    // Played: a new game (N) and its main menu; Play; the act's place and
    // its first scene; a click on the door, which sets a tag and goes to
    // the next scene; a quick save (F2); back to the launcher.
    GameTarget target{"motoe", "awe", "motoe", "Murder on the Orient Express", install, tmp / "saves", {}};
    PlayingSystem sys;
    sys.inputs = {{500, key('n')},
                  {3000, mouse(EventType::kMouseMove, 500, 400)},
                  {3020, mouse(EventType::kMouseDown, 500, 400)},
                  {3040, mouse(EventType::kMouseUp, 500, 400)},
                  {6000, mouse(EventType::kMouseMove, 510, 410)},
                  {6020, mouse(EventType::kMouseDown, 510, 410)},
                  {6040, mouse(EventType::kMouseUp, 510, 410)},
                  {9000, key(key::kF1 + 1)},
                  {10000, Event{EventType::kReturnToLauncher}}};
    auto game = engine->create(sys, target);
    const Error err = game->run();
    rec.add("run.result", err ? "error: " + err.message : std::string("ok"));
    rec.add("run.size", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
    rec.add("run.screens", std::to_string(sys.shown) + " " + sys.screens.hex());
    std::string titles;
    for (const std::string &t : sys.titles)
        titles += (titles.empty() ? "" : " / ") + t;
    rec.add("run.titles", titles);
    rec.add("run.files", listFiles(tmp / "saves"));

    rec.check(RELUVA_SOURCE_DIR "/tests/engines/awe/data/engine-record.txt", "awe.engine",
              "tests/engines/awe/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("awe.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
