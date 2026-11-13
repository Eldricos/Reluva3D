// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Clue engine as a whole, without the game's files: the basic engine
// test, run by ctest and CI. It makes a small install of its own (a
// stand-in Clue.exe beside the ticker's sentences, a dialogInfo.res of its
// own with the menus the game goes through, a short Cinepak intro with
// sound, and the test board of the unit tests as clue.lvl, its floors
// drawn), finds it as the launcher does, and plays it through the engine:
// the intro, New Game, one player and two computer players set up, the
// game started, saved from the Esc menu, left, and loaded again from the
// main menu. What happens goes into a record (counts and checksums of the
// screens, the titles, the saved games), compared with
// tests/engines/clue/data/engine-record.txt.
//
//   reluva-tests clue.engine [--update]

#include "common/headless_system.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/rules.h"
#include "engines/registry.h"
#include "tests/engines/clue/support.h"
#include "tests/support/record.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::clue;
using namespace reluva::clue::testing;
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

// The dialogs the game goes through, by their numbers in dialogInfo.res
// (frontend.cpp), each a column of buttons named by their numbers: button
// i of a dialog from (40, 40 + 36i) to (240, 70 + 36i). The setup's name
// box (10) and the save window's (10-17) are text boxes beside them.
constexpr int kOk = 10001, kReturn = 10000;
const std::map<int, std::vector<int>> kDialogs = {
    {1, {0, 1, 2, 3, 5, 7}},                        // the main menu: New Game is 1, Load Game 3, Exit 7
    {2, {0, 1, kReturn}},                           // a game here (0) or over a network (1)
    {3, {0, 1, 2, 3, 4, 5, 6, 7, kReturn}},         // Load Game's slots
    {7, {kOk, kReturn}},                            // Exit: Yes, No
    {17, {5, 6, 7, 8, 9, 10, 29, 30}},              // the suspects, Start (29), Back (30)
    {18, {0, 1, 2, 5, 6, 7, 11, 12, kOk, kReturn}}, // Miss Scarlet's setup: Human 0, Computer 1, None 2
    {19, {0, 1, 2, 5, 6, 7, 11, 12, kOk, kReturn}}, // Colonel Mustard's
    {20, {0, 1, 2, 5, 6, 7, 11, 12, kOk, kReturn}}, // Mrs. White's
    {26, {}},                                       // Loading
    {35, {0, 1, 2, 3, 4}},                          // the Esc menu: Save 0, Exit 3, Continue 4
    {36, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}},           // Save Game: Cancel 0, Save 1, the slots 2-9
    {44, {kOk, kReturn}},                           // exit the game: Yes, No
};

int buttonRow(int dialog, int id) {
    const std::vector<int> &ids = kDialogs.at(dialog);
    return int(std::find(ids.begin(), ids.end(), id) - ids.begin());
}

Bytes dialogInfo() {
    Bytes b = {'R', 'E', 'S', 'I', uint8_t(kDialogs.size()), 1};
    put32(b, 6); // the font: largeu
    putResiText(b, "menu:Afont");
    put32(b, 0);
    put32(b, 6);
    for (const auto &[number, ids] : kDialogs) {
        for (int i = 0; i < 9; ++i)
            put32(b, i == 0 ? uint32_t(number) : i == 6 ? 6 : 0);
        putResiText(b, "menu:Abackground");
        const bool textBoxes = (number >= 18 && number <= 20) || number == 36;
        b.push_back(uint8_t(ids.size() + (textBoxes ? (number == 36 ? 8 : 1) : 0)));
        for (size_t i = 0; i < ids.size(); ++i) {
            const int top = 40 + 36 * int(i);
            putControl(b, 3, ids[i], 40, top, 240, top + 30, 1, 6, 0, "Button " + std::to_string(ids[i]),
                       "menu:Abutton");
        }
        if (number >= 18 && number <= 20)
            putControl(b, 5, 10, 300, 40, 500, 70, 0, 6, 0, "", "");
        if (number == 36)
            for (int slot = 0; slot < 8; ++slot)
                putControl(b, 5, 10 + slot, 300, 112 + 36 * slot, 500, 142 + 36 * slot, 0, 6, 1, "", "");
    }
    return b;
}

// Cinepak, big-endian.
void putBe16(Bytes &b, uint32_t v) {
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v));
}
void putBe24(Bytes &b, uint32_t v) {
    b.push_back(uint8_t(v >> 16));
    putBe16(b, v & 0xffff);
}

// A 160x120 Cinepak frame of one strip: a V1 codebook of two colours that
// change with `n`, and every block from it, in a check pattern.
Bytes cinepakFrame(int n) {
    constexpr int kW = 160;
    constexpr int kH = 120;
    Bytes codebook;
    for (int e = 0; e < 2; ++e) {
        const uint8_t y = uint8_t(e ? 200 - 20 * n : 40 + 20 * n);
        codebook.insert(codebook.end(), {y, y, y, y, uint8_t(e ? 20 : 0), uint8_t(e ? 0 : 30)});
    }
    Bytes blocks;
    for (int by = 0; by < kH / 4; ++by)
        for (int bx = 0; bx < kW / 4; ++bx)
            blocks.push_back(uint8_t((bx / 4 + by / 4 + n) % 2));
    Bytes strip;
    strip.push_back(0x22); // the V1 codebook
    putBe24(strip, uint32_t(codebook.size() + 4));
    strip.insert(strip.end(), codebook.begin(), codebook.end());
    strip.push_back(0x32); // every block V1
    putBe24(strip, uint32_t(blocks.size() + 4));
    strip.insert(strip.end(), blocks.begin(), blocks.end());
    Bytes f;
    f.push_back(0);
    putBe24(f, uint32_t(10 + 12 + strip.size()));
    putBe16(f, kW);
    putBe16(f, kH);
    putBe16(f, 1);
    putBe16(f, 0x1000); // a key strip
    putBe16(f, uint32_t(12 + strip.size()));
    putBe16(f, 0);
    putBe16(f, 0);
    putBe16(f, kH);
    putBe16(f, kW);
    f.insert(f.end(), strip.begin(), strip.end());
    return f;
}

void putChunk(Bytes &b, const char *id, const Bytes &data) {
    b.insert(b.end(), id, id + 4);
    put32(b, uint32_t(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    if (data.size() & 1)
        b.push_back(0);
}

Bytes list(const char *type, const Bytes &body) {
    Bytes l(type, type + 4);
    l.insert(l.end(), body.begin(), body.end());
    return l;
}

// An AVI as Clue's are: Cinepak at 15 frames a second, 16-bit mono PCM
// at 22,050 Hz; `frames` frames and their sound, a tone.
Bytes movie(int frames) {
    Bytes avih;
    put32(avih, 66666); // microseconds a frame
    avih.resize(56, 0);
    Bytes vstrh = {'v', 'i', 'd', 's', 'c', 'v', 'i', 'd'};
    vstrh.resize(56, 0);
    Bytes vstrf;
    put32(vstrf, 40);
    put32(vstrf, 160);
    put32(vstrf, 120);
    put16(vstrf, 1);
    put16(vstrf, 24);
    vstrf.insert(vstrf.end(), {'c', 'v', 'i', 'd'});
    vstrf.resize(40, 0);
    Bytes astrh = {'a', 'u', 'd', 's', 0, 0, 0, 0};
    astrh.resize(56, 0);
    Bytes astrf;
    put16(astrf, 1);
    put16(astrf, 1);
    put32(astrf, 22050);
    put32(astrf, 44100);
    put16(astrf, 2);
    put16(astrf, 16);
    Bytes vstrl;
    Bytes astrl;
    Bytes hdrl;
    Bytes movi;
    putChunk(vstrl, "strh", vstrh);
    putChunk(vstrl, "strf", vstrf);
    putChunk(astrl, "strh", astrh);
    putChunk(astrl, "strf", astrf);
    putChunk(hdrl, "avih", avih);
    putChunk(hdrl, "LIST", list("strl", vstrl));
    putChunk(hdrl, "LIST", list("strl", astrl));
    for (int n = 0; n < frames; ++n) {
        putChunk(movi, "00dc", cinepakFrame(n));
        Bytes sound;
        for (int i = 0; i < 22050 / 15; ++i)
            put16(sound, uint32_t(int16_t(((i / 25) % 2) ? 3000 : -3000)) & 0xffff);
        putChunk(movi, "01wb", sound);
    }
    Bytes body = {'A', 'V', 'I', ' '};
    putChunk(body, "LIST", list("hdrl", hdrl));
    putChunk(body, "LIST", list("movi", movi));
    Bytes riff = {'R', 'I', 'F', 'F'};
    put32(riff, uint32_t(body.size()));
    riff.insert(riff.end(), body.begin(), body.end());
    return riff;
}

// The test board (support.h) as clue.lvl: its objects' names and their
// placements, in the order given.
Bytes boardFile() {
    const Level level = boardLevel();
    std::vector<const char *> names;
    for (const std::string &n : level.objects)
        names.push_back(n.c_str());
    std::vector<TestPlacement> placements;
    for (const Placement &p : level.placements)
        placements.push_back({0x40, 0, int(p.object), int(p.x), int(p.y), -1, int(p.layer), {}});
    return levelFile(names, placements);
}

void makeInstall(const std::filesystem::path &dir) {
    std::filesystem::remove_all(dir);
    // Found by its files, whatever their contents: no release's
    // signatures match the stand-ins.
    test::writeFile(dir / "Clue.exe", {'M', 'Z'});
    const std::string ticker = "Miss Scarlet suggests ...\r\n";
    const auto classic = dir / "Data" / "Classic";
    test::writeFile(classic / "Text" / "TickerStrings.txt", Bytes(ticker.begin(), ticker.end()));
    test::writeFile(dir / "dialogInfo.res", dialogInfo());
    // The menus' pictures: a background, a button and the font, all one
    // animation; the floors, two.
    test::writeFile(classic / "Resource" / "test.res", pack(drive({{"Ia", image200()}, {"Ib", image100()}})));
    test::writeFile(classic / "Resource" / "menu.a", pack(drive({{"Abackground", animation(false, -1, 64, 64)},
                                                                 {"Abutton", animation(false)},
                                                                 {"Afont", animation(true)}})));
    test::writeFile(classic / "Resource" / "floors.a",
                    pack(drive({{"Ahw", animation(false, -1, 64, 64)}, {"Awood", animation(true, -1, 64, 64)}})));
    test::writeFile(classic / "Resource" / "r_hwfloor.o",
                    drive({{"Ohwfloor", objectRecord("r_hwfloor:Ohwfloor", 1, {{0, "floors:Ahw"}})}}));
    test::writeFile(classic / "Resource" / "r_woodfloor.o",
                    drive({{"Owoodfloor", objectRecord("r_woodfloor:Owoodfloor", 1, {{0, "floors:Awood"}})}}));
    test::writeFile(classic / "Levels" / "clue.lvl", boardFile());
    test::writeFile(classic / "Video" / "Intro.avi", movie(12));
    test::writeFile(classic / "Video" / "deal.avi", movie(6));
}

Event key(uint32_t k) {
    Event e;
    e.type = EventType::KeyDown;
    e.key = k;
    return e;
}

Event mouse(EventType type, int x, int y) {
    Event e;
    e.type = type;
    e.button = MouseButton::Left;
    e.mouseX = x;
    e.mouseY = y;
    e.clicks = type == EventType::MouseDown ? 1 : 0;
    return e;
}

Event typed(const char *text) {
    Event e;
    e.type = EventType::TextInput;
    std::snprintf(e.text, sizeof e.text, "%s", text);
    return e;
}

// The saved games written: each slot's file and the name it holds.
std::string listSaves(const std::filesystem::path &dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) {
            std::string what = it->path().lexically_relative(dir).generic_string();
            // A saved game: its name, and a checksum of the rest (the
            // game as it was).
            std::ifstream in(it->path(), std::ios::binary);
            std::string tag;
            std::string name;
            if (it->path().extension() == ".sav" && std::getline(in, tag) && saveio::getString(in, name)) {
                const std::vector<uint8_t> rest((std::istreambuf_iterator<char>(in)), {});
                what += " (" + tag + ", \"" + name + "\", " + Hash().bytes(rest).hex() + ")";
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
    const MetaEngine *engine = findEngine("clue");
    if (!engine)
        return test::fail("clue isn't registered");
    const std::filesystem::path install = tmp / "clue";
    makeInstall(install);

    const auto found = engine->detect(install);
    rec.add("detect", found.empty() ? "(none)" : found[0].gameId + " " + found[0].description);

    // Played: the intro (0.8 s), then New Game, a game here, Miss Scarlet a
    // person and Colonel Mustard and Mrs. White computer players, Start; the deal; the
    // game, saved in the first slot as "Test" from the Esc menu, and left;
    // the saved game loaded from the main menu; back to the launcher.
    GameTarget target{"clue", "clue", "clue", "Clue: Murder at Boddy Mansion", install, tmp / "save", {}};
    PlayingSystem sys;
    uint64_t t = 2000;
    const auto click = [&](int dialog, int id, int dx = 0, int dy = 0) {
        const int x = dx + 140;
        const int y = dy + 55 + 36 * buttonRow(dialog, id);
        sys.inputs.push_back({t, mouse(EventType::MouseMove, x, y)});
        sys.inputs.push_back({t + 20, mouse(EventType::MouseDown, x, y)});
        sys.inputs.push_back({t + 40, mouse(EventType::MouseUp, x, y)});
        t += 1500;
    };
    // the Esc menu's windows, over the game
    constexpr int kOverX = 205;
    constexpr int kOverY = 11;
    click(1, 1);  // New Game
    click(2, 0);  // here
    click(17, 5); // Miss Scarlet
    click(18, 0); // a person
    click(18, kOk);
    click(17, 6); // Colonel Mustard
    click(19, 1); // a computer player
    click(19, kOk);
    click(17, 7); // Mrs. White
    click(20, 1); // a computer player
    click(20, kOk);
    click(17, 29); // Start
    t += 3000;     // loading, the deal
    sys.inputs.push_back({t, key(key::Escape)});
    t += 1000;
    click(35, 0, kOverX, kOverY); // Save
    click(36, 2, kOverX, kOverY); // the first slot
    sys.inputs.push_back({t, typed("Test")});
    t += 500;
    click(36, 1, kOverX, kOverY); // Save
    sys.inputs.push_back({t, key(key::Escape)});
    t += 1000;
    click(35, 3, kOverX, kOverY); // Exit
    click(44, kOk, kOverX, kOverY);
    click(1, 3); // Load Game
    click(3, 0); // the first slot
    t += 3000;
    sys.inputs.push_back({t, Event{EventType::ReturnToLauncher}});

    auto game = engine->create(sys, target);
    const Error err = game->run();
    rec.add("run.result", err ? "error: " + err.message : std::string("ok"));
    rec.add("run.size", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
    rec.add("run.screens", std::to_string(sys.shown) + " " + sys.screens.hex());
    std::string titles;
    for (const std::string &title : sys.titles)
        titles += (titles.empty() ? "" : " / ") + title;
    rec.add("run.titles", titles);
    rec.add("run.files", listSaves(tmp / "save"));

    rec.check(RELUVA_SOURCE_DIR "/tests/engines/clue/data/engine-record.txt", "clue.engine",
              "tests/engines/clue/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("clue.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
