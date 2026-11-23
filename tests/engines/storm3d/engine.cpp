// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/storm3d/data/engine-record.txt.
//
//   reluva-tests storm3d.engine [--update]
//
// So far: its install is found as Star Trek: Armada, and the data viewer
// shows it page by page. Add steps as the engine learns to run its games
// (see tests/engines/idtech3/engine.cpp), then --update and review the
// record.

#include "common/headless_system.h"
#include "engines/registry.h"
#include "engines/viewer_registry.h"
#include "image/image.h"

#include "tests/support/record.h"
#include "tests/support/test.h"

#include <fstream>
#include <string>
#include <vector>

using namespace reluva;

namespace {

using test::Hash;

// The headless System with a player: keys pressed at given times. It keeps
// a checksum of every screen shown and each window title.
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
        HeadlessSystem::setWindowTitle(t);
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

Event keyDown(uint32_t key) {
    Event e;
    e.type = EventType::kKeyDown;
    e.key = key;
    return e;
}

std::string joined(const std::vector<std::string> &lines) {
    std::string out;
    for (const std::string &l : lines)
        out += (out.empty() ? "" : " | ") + l;
    return out.empty() ? "(none)" : out;
}

void writeFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}

// A w x h TGA of a diagonal gradient, its alpha falling to the right.
std::vector<uint8_t> makeTga(int w, int h) {
    image::Image img;
    img.w = w;
    img.h = h;
    img.rgba.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t *p = img.pixel(x, y);
            p[0] = uint8_t(x * 255 / w);
            p[1] = uint8_t(y * 255 / h);
            p[2] = uint8_t((x + y) * 127 / (w + h));
            p[3] = uint8_t(255 - x * 255 / w);
        }
    return image::encodeTga(img, true);
}

// Armada's files by name, so an unknown release, and a few textures.
void makeInstall(const std::filesystem::path &dir) {
    for (const char *name : {"Armada.exe", "assets.zfs", "objtxt.zfs"})
        writeFile(dir / name, {'x'});
    writeFile(dir / "Textures/RGB/Bbase.tga", makeTga(64, 64));
    writeFile(dir / "Textures/RGB/Bbase.1", makeTga(32, 32));
    writeFile(dir / "Textures/Index8/Xspark01", makeTga(16, 16));
}

void engineTest(const std::filesystem::path &tmp) {
    test::Record rec;
    const MetaEngine *engine = findEngine("storm3d");
    const MetaViewer *viewer = findViewer("storm3d");
    if (!engine || !viewer)
        return test::fail("storm3d or its viewer isn't registered");

    const std::filesystem::path dir = tmp / "install";
    makeInstall(dir);
    const std::vector<DetectedGame> found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    for (const DetectedGame &game : found)
        rec.add("detect.game", game.gameId + ": " + game.description);

    // The data viewer, its pages in turn. The archives are only names here,
    // so the viewer reads the install's folders without them.
    std::filesystem::remove(dir / "assets.zfs");
    std::filesystem::remove(dir / "objtxt.zfs");
    GameTarget target{"armada", "storm3d", "armada", "Star Trek: Armada", dir, tmp / "saves", {}};
    PlayingSystem sys;
    sys.inputs = {
        {100, keyDown(key::kRight)},  // RGB/Bbase.tga, after Index8's one picture
        {150, keyDown(key::kRight)},  // its smaller copy, Bbase.1
        {200, keyDown('a')},          // its alpha over a checkerboard
        {250, keyDown('f')},          // the next folder: back to the first
        {300, keyDown('i')},          // no status line
        {350, keyDown(key::kTab)},    // the next page with something to show
        {450, keyDown(key::kEscape)}, // to reluva-viewer's list
    };
    const Error err = viewer->create(sys, target)->run();
    rec.add("run.result", err ? "error: " + err.message : std::string("ok"));
    rec.add("run.size", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
    rec.add("run.screens", std::to_string(sys.shown) + " " + sys.screens.hex());
    rec.add("run.titles", joined(sys.titles));
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/storm3d/data/engine-record.txt", "storm3d.engine",
              "tests/engines/storm3d/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
