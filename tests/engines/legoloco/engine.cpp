// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own (install.h), takes
// the engine through what it does so far, and writes what happens into a
// record (counts, checksums: tests/support/record.h), compared with
// tests/engines/legoloco/data/engine-record.txt.
//
//   reluva-tests legoloco.engine [--update]
//
// So far that's detection and the data viewer: each page shown, and a
// key or two pressed on it.

#include "common/headless_system.h"
#include "engines/registry.h"
#include "engines/viewer_registry.h"

#include "tests/engines/legoloco/install.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

#include <string>
#include <vector>

using namespace reluva;

namespace {

// A headless system that is given keys at set times, and keeps what's shown.
class PlayingSystem : public HeadlessSystem {
public:
    struct Input {
        uint64_t at;
        Event event;
    };
    std::vector<Input> inputs;
    test::Hash screens;
    int shown = 0;
    std::vector<std::string> titles;

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        screens.bytes(s.pixels);
        ++shown;
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
    Event ev{EventType::kKeyDown};
    ev.key = k;
    return ev;
}

void engineTest(const std::filesystem::path &tmp) {
    const std::filesystem::path dir = tmp / "install";
    legoloco::fixture::makeInstall(dir);
    test::Record rec;
    const MetaEngine *engine = findEngine("legoloco");
    if (!engine)
        return test::fail("legoloco isn't registered");
    // The fixture's files aren't the game's, so it isn't taken for it.
    rec.add("detect.games", static_cast<int64_t>(engine->detect(dir).size()));

    // The viewer: the boards (the next one, the squares shown), the
    // pictures (the frame set played for a while), the definitions
    // (scrolled), the sounds (played, with no device), then Esc.
    const MetaViewer *viewer = findViewer("legoloco");
    if (!viewer)
        return test::fail("legoloco has no viewer");
    PlayingSystem sys;
    uint64_t at = 1000;
    for (const uint32_t k :
         {uint32_t(key::kRight), uint32_t('s'), uint32_t(key::kTab), uint32_t(key::kSpace), uint32_t(key::kTab),
          uint32_t(key::kDown), uint32_t(key::kTab), uint32_t(key::kSpace), uint32_t(key::kTab)}) {
        sys.inputs.push_back({at, keyDown(k)});
        at += 500;
    }
    sys.inputs.push_back({at, keyDown(key::kEscape)});
    // Esc should have ended it: this only stops a run that didn't.
    const uint64_t stop = at + 5000;
    sys.inputs.push_back({stop, Event{EventType::kReturnToLauncher}});
    const GameTarget target{"legoloco", "legoloco", "legoloco", "LEGO Loco", dir, tmp / "save", {}};
    const Error err = viewer->create(sys, target)->run();
    rec.add("viewer.result", err ? "error: " + err.message : std::string("ok"));
    rec.add("viewer.left", sys.getMillis() < stop ? "by Esc" : "not by Esc");
    rec.add("viewer.screens", std::to_string(sys.shown) + " " + sys.screens.hex());
    std::string titles;
    for (const std::string &title : sys.titles)
        titles += (titles.empty() ? "" : " / ") + title;
    rec.add("viewer.titles", titles);
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/legoloco/data/engine-record.txt", "legoloco.engine",
              "tests/engines/legoloco/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("legoloco.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
