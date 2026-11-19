// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Angel Game Engine as a whole, without the game's files: the basic
// engine test, run by ctest and CI. It makes a small install of its own (a
// stand-in Midtown2.exe beside an mm2core.ar holding a city's and a car's
// files, a texture and a text file), finds it as the launcher does, and
// runs the game: with no 3D renderer here, the race screen says it needs
// one, and Esc goes back. Then its data viewer, as reluva-viewer runs it:
// Tab past the 3D pages to the textures and the files, a file opened and
// closed, and Esc to leave. What happens goes into a record (counts and
// checksums of the screens, the titles), compared with
// tests/engines/age/data/engine-record.txt.
//
//   reluva-tests age.engine [--update]

#include "common/headless_system.h"
#include "engines/registry.h"
#include "engines/viewer_registry.h"
#include "tests/engines/age/support.h"
#include "tests/support/record.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::age::test_support;
using test::Hash;

namespace {

// The headless System with a player: events at given times. It keeps a
// checksum of every screen shown and the titles in turn.
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
        if (titles.empty() || titles.back() != t)
            titles.push_back(t);
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

Event keyDown(uint32_t k) {
    Event ev;
    ev.type = EventType::kKeyDown;
    ev.key = k;
    return ev;
}

void makeInstall(const std::filesystem::path &dir) {
    std::filesystem::remove_all(dir);
    // Found by its files, whatever their contents: no release's signatures
    // match the stand-ins.
    test::writeFile(dir / "Midtown2.exe", {'M', 'Z'});
    std::vector<uint8_t> tex = texHeader(4, 4, texformat::kRgb, 1, 0);
    for (int i = 0; i < 4 * 4; ++i)
        tex.insert(tex.end(), {uint8_t(i * 16), uint8_t(255 - i * 16), 128});
    const std::vector<ArFile> files = {
        {"tune/testcity.cinfo", toBytes("MapName=testcity\r\nLocalizedName=Test City\r\n"), {}},
        {"city/testcity.psdl", toBytes("PSD0"), {}},
        {"tune/vptest.info", toBytes("BaseName=vptest\r\nDescription=Test Car\r\nColors=Red|Blue\r\n"), {}},
        {"tune/vehicle/vptest.vehCarSim", toBytes("type: a\r\n"), {}},
        {"geometry/vptest.pkg", toBytes("PKG3"), {}},
        {"texture/test_sky.tex", tex, {}},
        {"readme.txt", toBytes("A test install.\r\nIts second line.\r\n"), {}},
    };
    test::writeFile(dir / "mm2core.ar", buildAr(files));
}

std::string joined(const std::vector<std::string> &titles) {
    std::string out;
    for (const std::string &title : titles)
        out += (out.empty() ? "" : " / ") + title;
    return out;
}

void engineTest(const std::filesystem::path &tmp) {
    test::Record rec;
    const MetaEngine *engine = findEngine("age");
    if (!engine)
        return test::fail("age isn't registered");
    const std::filesystem::path install = tmp / "mm2";
    makeInstall(install);

    const auto found = engine->detect(install);
    rec.add("detect", found.empty() ? "(none)" : found[0].gameId + " " + found[0].description);

    // The game: the race screen, which can't draw without 3D and says so;
    // Esc goes back to the launcher.
    GameTarget target{"mm2", "age", "mm2", "Midtown Madness 2", install, tmp / "save", {}};
    {
        PlayingSystem sys;
        sys.inputs.push_back({1000, keyDown(key::kEscape)});
        sys.inputs.push_back({5000, Event{EventType::kReturnToLauncher}});
        auto game = engine->create(sys, target);
        const Error err = game->run();
        rec.add("run.result", err ? "error: " + err.message : std::string("ok"));
        rec.add("run.size", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
        rec.add("run.screens", std::to_string(sys.shown) + " " + sys.screens.hex());
        rec.add("run.titles", joined(sys.titles));
    }

    // The viewer: the race screen first, then Tab on (the 3D pages have
    // nothing to show without a renderer's city and are passed over or say
    // so) to the textures, the next one, then the files: down, the file
    // opened, back to the list; Tab round, and Esc leaves.
    if (const MetaViewer *viewer = findViewer("age")) {
        PlayingSystem vsys;
        uint64_t vt = 1000;
        const auto press = [&](uint32_t k) {
            vsys.inputs.push_back({vt, keyDown(k)});
            vt += 500;
        };
        for (const uint32_t k : {uint32_t(key::kTab), uint32_t(key::kTab), uint32_t(key::kTab), uint32_t(key::kTab),
                                 uint32_t(key::kRight), uint32_t(key::kTab), uint32_t(key::kDown),
                                 uint32_t(key::kReturn), uint32_t(key::kEscape), uint32_t(key::kTab)})
            press(k);
        press(key::kEscape);
        // Esc should have ended it: this only stops a run that didn't.
        vsys.inputs.push_back({vt + 5000, Event{EventType::kReturnToLauncher}});
        auto run = viewer->create(vsys, target);
        const Error err = run->run();
        rec.add("viewer.result", err ? "error: " + err.message : std::string("ok"));
        rec.add("viewer.left", vsys.getMillis() < vt + 5000 ? "by Esc" : "by the launcher");
        rec.add("viewer.screens", std::to_string(vsys.shown) + " " + vsys.screens.hex());
        rec.add("viewer.titles", joined(vsys.titles));
    } else {
        rec.fail("viewer", "age's viewer isn't registered");
    }

    rec.check(RELUVA_SOURCE_DIR "/tests/engines/age/data/engine-record.txt", "age.engine",
              "tests/engines/age/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("age.engine",
                                        {
                                            {"engine", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
