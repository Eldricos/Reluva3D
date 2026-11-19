// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Birth of the Federation as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/botf/data/engine-record.txt.
//
//   reluva-tests botf.engine [--update]
//
// The install is a stbof.res holding a main menu, the game setup and
// empire selection layouts (each widget a picture of its own colour) and
// a lexicon.dic; the run goes from the main menu through setup and empire
// selection and back, as starting a new game does, then quits.

#include "common/headless_system.h"
#include "engines/registry.h"
#include "image/image.h"

#include "tests/engines/botf/unit/helpers.h"
#include "tests/support/record.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace reluva;
using namespace reluva::botf::test;

namespace {

// A picture of one colour, with a magenta (see-through) corner.
Bytes picture(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    image::Image img;
    img.w = w;
    img.h = h;
    img.rgba.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t *p = img.pixel(x, y);
            const bool corner = x < 2 && y < 2;
            p[0] = corner ? 255 : r;
            p[1] = corner ? 0 : g;
            p[2] = corner ? 255 : b;
            p[3] = 255;
        }
    return image::encodeTga(img);
}

struct LayoutWidget {
    uint32_t type; // 2 a button, 7 a slider, 13 a picture, 14 a text box
    uint32_t id;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    // A button's pictures, "<name>1.tga" normal and "<name>2.tga" selected; a
    // slider's lit cells; a text box's font.
    std::string picture;
};

// A layout: a window holding the widgets, grouped by type as the files are.
Bytes layout(uint32_t id, const std::string &background, const std::vector<LayoutWidget> &widgets) {
    Bytes f;
    put32(f, 2);
    putWidget(f, 1, id, 0, 0, 800, 600, background);
    std::vector<std::pair<uint32_t, uint32_t>> byType;
    for (const uint32_t type : {2U, 7U, 13U, 14U}) {
        uint32_t n = 0;
        for (const LayoutWidget &w : widgets)
            n += w.type == type ? 1 : 0;
        if (n)
            byType.emplace_back(type, n);
    }
    putWindowCounts(f, byType);
    for (const uint32_t type : {2U, 7U, 13U, 14U})
        for (const LayoutWidget &w : widgets) {
            if (w.type != type)
                continue;
            putWidget(f, w.type, w.id, w.x, w.y, w.w, w.h, w.type == 7 ? w.picture : "none");
            if (w.type == 7) {
                // 41 cells to 127, as the game's volumes are.
                put16(f, 0);
                for (const uint32_t v : {127U, 41U, w.w, 0U})
                    put32(f, v);
                for (const char *name : {"none", w.picture.c_str(), "none", "none"})
                    putName(f, name);
                for (int i = 0; i < 3; ++i)
                    put32(f, 0);
                continue;
            }
            if (w.type == 14) {
                for (const uint32_t v : {0U, 0U, w.w, w.h})
                    put32(f, v);
                putFont(f, 0, 0, 0, w.picture);
                continue;
            }
            put32(f, 0xffffffff);
            if (w.type == 2) {
                f.push_back(0);
                put32(f, 0);
                put32(f, 0);
                putName(f, w.picture + "1.tga");
                putName(f, w.picture + "2.tga");
            }
        }
    return f;
}

// The install: the main menu's buttons down the right; the options
// screen's volumes, settings and timers' arrows down the middle; the setup
// screen's buttons in a row along the top, the empire screen's names down
// the left; Accept and Cancel at the bottom.
void makeInstall(const std::filesystem::path &dir) {
    std::vector<test::ZipFile> files;
    auto add = [&](const std::string &name, Bytes data) { files.push_back({name, std::move(data), {}}); };
    // The main menu's nine buttons, where the game has them: Continue, New
    // Single Player Game ... Quit Game, 47 pixels apart.
    std::vector<LayoutWidget> menu;
    uint32_t y = 100;
    for (const uint32_t id : {139U, 140U, 141U, 142U, 355U, 356U, 144U, 9600U, 143U}) {
        menu.push_back({2, id, 621, y, 164, 40, "mc"});
        y += 47;
    }
    add("opening.wdf", layout(138, "menu.tga", menu));
    std::vector<LayoutWidget> options = {{2, 8100, 488, 504, 124, 36, "ok"}, {2, 8101, 16, 504, 124, 36, "no"}};
    y = 195;
    for (const uint32_t id : {8102U, 8103U, 8104U, 8105U}) {
        options.push_back({7, id, 310, y, 287, 19, "vol-bar.tga"});
        y += 27;
    }
    y = 308;
    for (const uint32_t id : {8107U, 8106U, 8108U, 8109U}) {
        options.push_back({2, id, 227, y, 234, 24, "bt"});
        y += 31;
    }
    for (const uint32_t id : {8160U, 8161U, 8162U, 8163U})
        options.push_back({14, id, 463, 309 + 31 * (id - 8160), 131, 22, "human20.fnt"});
    // The title, the volumes' names, and the timers' names and values.
    for (const auto &[id, x, ty] : std::vector<std::tuple<uint32_t, uint32_t, uint32_t>>{{8122, 226, 153},
                                                                                         {8123, 226, 193},
                                                                                         {8124, 226, 220},
                                                                                         {8125, 226, 247},
                                                                                         {8126, 226, 275},
                                                                                         {8116, 227, 442},
                                                                                         {8117, 406, 442},
                                                                                         {8120, 227, 472},
                                                                                         {8121, 406, 472}})
        options.push_back({14, id, x, ty, 70, 22, "human20.fnt"});
    // The timers' arrows: less, then more.
    for (const auto &[id, x, ty] : std::vector<std::tuple<uint32_t, uint32_t, uint32_t>>{
             {8115, 379, 443}, {8114, 466, 443}, {8119, 379, 473}, {8118, 466, 473}})
        options.push_back({2, id, x, ty, 16, 19, "bt"});
    add("options.wdf", layout(43, "opt.tga", options));
    std::vector<LayoutWidget> setup = {{2, 9201, 453, 502, 159, 36, "ok"}, {2, 9202, 16, 502, 159, 36, "no"}};
    std::vector<LayoutWidget> empires = {{2, 9110, 452, 502, 160, 36, "ok"}, {2, 9111, 16, 502, 160, 36, "no"}};
    uint32_t x = 0;
    for (const uint32_t id : {9207U, 9209U, 9211U, 9212U, 9214U, 9215U, 9217U, 9219U, 9221U, 9223U, 9225U, 9227U, 9229U,
                              9231U, 9233U, 9234U}) {
        setup.push_back({2, id, x, 100, 30, 30, "bt"});
        x += 40;
    }
    x = 0;
    for (const uint32_t id :
         {9208U, 9210U, 9213U, 9216U, 9218U, 9220U, 9222U, 9224U, 9226U, 9228U, 9230U, 9232U, 9205U, 9206U}) {
        setup.push_back({14, id, x, 140, 120, 17, "human15.fnt"});
        x += 50;
    }
    for (uint32_t i = 0; i < 5; ++i)
        empires.push_back({14, 9114 + i, 89, 190 + 70 * i, 150, 17, "human15.fnt"});
    setup.push_back({13, 9204, 100, 200, 60, 50, ""});
    setup.push_back({13, 9203, 200, 200, 60, 50, ""});
    add("gmesetup.wdf", layout(9200, "set.tga", setup));

    for (uint32_t i = 0; i < 5; ++i)
        empires.push_back({2, 9105 + i, 89, 146 + 70 * i, 272, 36, "em"});
    empires.push_back({14, 9112, 368, 146, 240, 340, "human20.fnt"});
    add("emppick.wdf", layout(9100, "emp.tga", empires));
    add("menu.tga", picture(800, 600, 10, 10, 60));
    add("set.tga", picture(800, 600, 20, 60, 20));
    add("emp.tga", picture(800, 600, 60, 20, 20));
    add("opt.tga", picture(800, 600, 30, 30, 30));
    add("vol-bar.tga", picture(287, 19, 240, 210, 90));
    add("volbarof.tga", picture(287, 19, 80, 30, 60));
    const std::vector<std::tuple<std::string, uint8_t, uint8_t, uint8_t>> buttons = {
        {"mc", 200, 100, 0}, {"ok", 0, 200, 0}, {"no", 200, 0, 0}, {"bt", 0, 100, 200}, {"em", 200, 200, 0}};
    for (const auto &[name, r, g, b] : buttons) {
        add(name + "1.tga", picture(40, 40, r, g, b));
        add(name + "2.tga", picture(40, 40, 255, 255, 255));
    }
    add("cc_gsir.tga", picture(60, 50, 90, 90, 90));
    add("cc_gzmd.tga", picture(60, 50, 150, 150, 150));
    add("human15.fnt", makeFont(16));
    add("human20.fnt", makeFont(21));
    add("lexicon.dic", makeLexicon({"word"}));
    writeFile(dir / "stbof.res", test::buildZip(files));
    writeFile(dir / "sfx.snd", Bytes(4, 0));
}

Event pointAt(int x, int y) {
    Event e;
    e.type = EventType::kMouseMove;
    e.mouseX = x;
    e.mouseY = y;
    return e;
}

Event click(int x, int y, bool down, uint16_t mods = 0) {
    Event e;
    e.type = down ? EventType::kMouseDown : EventType::kMouseUp;
    e.button = MouseButton::kLeft;
    e.mods = mods;
    e.mouseX = x;
    e.mouseY = y;
    return e;
}

/**
 * A headless system that gives the engine one scripted event a frame, and
 * records a checksum of the screen before each event that has a label.
 */
class ScriptedSystem : public HeadlessSystem {
public:
    ScriptedSystem(test::Record &rec, std::vector<std::pair<std::string, Event>> script)
        : _rec(rec), _script(std::move(script)) {}

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        _screen = s;
    }

    bool pollEvent(Event &ev) override {
        if (frames() == _lastFrame || _next >= _script.size())
            return false;
        _lastFrame = frames();
        const auto &[label, scripted] = _script[_next++];
        if (!label.empty())
            _rec.add(label, test::Hash().bytes(_screen.pixels).hex());
        ev = scripted;
        return true;
    }

private:
    test::Record &_rec;
    std::vector<std::pair<std::string, Event>> _script;
    size_t _next = 0;
    int _lastFrame = 0; // the first event waits for the first frame
    Surface _screen;
};

void engineTest(const std::filesystem::path &tmp) {
    const std::filesystem::path dir = tmp / "install";
    std::filesystem::create_directories(dir);
    test::Record rec;
    const MetaEngine *engine = findEngine("botf");
    if (!engine)
        return test::fail("botf isn't registered");
    makeInstall(dir);
    const auto found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    if (found.size() == 1) {
        rec.add("detect.description", found[0].description);
        // The setup screen's buttons are 40 pixels apart from x 0; each
        // click is a press and a release on it.
        const auto setupButton = [](int n, bool down, uint16_t mods = 0) {
            return click(n * 40 + 15, 115, down, mods);
        };
        // The main menu's buttons are 47 pixels apart from y 100.
        const auto menuButton = [](int n, bool down) { return click(700, 100 + 47 * n + 20, down); };
        const std::vector<std::pair<std::string, Event>> script = {
            {"menu.shown", pointAt(700, 100 + 47 + 20)},
            {"menu.new game pointed at", menuButton(0, true)}, // Continue, which can't be used yet
            {"menu.continue pressed", menuButton(0, false)},
            {"", menuButton(1, true)}, // New Single Player Game
            {"", menuButton(1, false)},
            {"setup.shown", setupButton(1, true)}, // difficulty
            {"setup.difficulty pressed", setupButton(1, false)},
            {"setup.difficulty changed", setupButton(10, true)}, // the second empire's level
            {"", setupButton(10, false)},
            {"setup.level changed", setupButton(9, true, keymod::kShift)}, // every empire's level
            {"", setupButton(9, false, keymod::kShift)},
            {"setup.levels all set", click(460, 510, true)}, // Accept
            {"", click(460, 510, false)},
            {"empires.shown", click(100, 300, true)}, // the third empire
            {"empires.third chosen", click(100, 300, false)},
            {"", click(30, 510, true)}, // Cancel: back to the setup
            {"", click(30, 510, false)},
            {"setup.again", click(30, 510, true)}, // its Cancel: back to the menu
            {"", click(30, 510, false)},
            {"menu.again", menuButton(1, true)}, // a new game again, this time accepted
            {"", menuButton(1, false)},
            {"", click(460, 510, true)},
            {"", click(460, 510, false)},
            {"", click(100, 230, true)}, // the second empire
            {"", click(100, 230, false)},
            {"empires.second chosen", click(460, 510, true)}, // Accept: no game yet, the menu
            {"", click(460, 510, false)},
            {"menu.after a new game", menuButton(5, true)}, // Options
            {"", menuButton(5, false)},
            {"options.shown", click(300, 345, true)}, // tactical combat
            {"", click(300, 345, false)},
            {"options.combat changed", click(400, 200, true)}, // a volume, dragged down
            {"options.volume pressed", pointAt(330, 200)},
            {"options.volume dragged", click(330, 200, false)},
            {"", click(30, 510, true)}, // Cancel: nothing kept
            {"", click(30, 510, false)},
            {"menu.options cancelled", menuButton(5, true)},
            {"", menuButton(5, false)},
            {"options.again", click(300, 345, true)}, // tactical combat, kept this time
            {"", click(300, 345, false)},
            {"", click(500, 510, true)}, // Accept
            {"", click(500, 510, false)},
            {"menu.options accepted", menuButton(1, true)}, // a new game, combat automatic
            {"", menuButton(1, false)},
            {"setup.with the options", click(30, 510, true)},
            {"", click(30, 510, false)},
            {"menu.last", menuButton(8, true)}, // Quit Game: the end
            {"", menuButton(8, false)},
        };
        ScriptedSystem sys(rec, script);
        GameTarget target;
        target.engineId = "botf";
        target.gameId = found[0].gameId;
        target.description = found[0].description;
        target.path = dir;
        const Error err = engine->create(sys, target)->run();
        rec.add("run", err ? "FAILED " + err.message : std::string("ok"));
        rec.add("run.screen", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
    }
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/botf/data/engine-record.txt", "botf.engine",
              "tests/engines/botf/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("botf.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
