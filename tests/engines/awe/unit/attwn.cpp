// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// And Then There Were None: its files as the game names and reads them,
// its scenes, and recognising the game, on files made here.

#include "engines/awe/attwn/attwn.h"
#include "engines/awe/attwn/files.h"
#include "engines/awe/formats/ac2.h"
#include "engines/awe/games.h"
#include "engines/awe/game/conditions.h"
#include "engines/awe/game/script.h"
#include "tests/engines/awe/fixtures.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::awe;

namespace {

// Lines as ac.exe writes them to an .act file: a count, then each line's
// length and its bytes XORed with 0x30.
std::vector<uint8_t> scramble(const std::vector<std::string> &lines) {
    std::vector<uint8_t> out = {uint8_t(lines.size()), uint8_t(lines.size() >> 8)};
    for (const std::string &l : lines) {
        out.push_back(uint8_t(l.size()));
        out.push_back(uint8_t(l.size() >> 8));
        for (char c : l)
            out.push_back(uint8_t(c) ^ 0x30);
    }
    return out;
}

std::vector<uint8_t> bytes(const std::string &s) {
    return {s.begin(), s.end()};
}

void putFile(const std::filesystem::path &p, const std::vector<uint8_t> &data) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
}

void testUnscramble() {
    std::vector<uint8_t> text;
    // An event file as the install has them ("wait .7"), its line endings kept.
    CHECK(!attwn::unscrambleText(scramble({"wait .7\n", "talk brent a\n"}), text));
    CHECK(text == bytes("wait .7\ntalk brent a\n"));
    // No lines, and a byte 0x30 itself (a 0 once unscrambled).
    CHECK(!attwn::unscrambleText({0, 0}, text) && text.empty());
    CHECK(!attwn::unscrambleText({1, 0, 1, 0, 0x30}, text) && text == std::vector<uint8_t>{0});
    // Cut short: a line missing, a line's bytes missing, no count.
    std::vector<uint8_t> act = scramble({"one\n", "two\n"});
    CHECK(attwn::unscrambleText(std::vector<uint8_t>(act.begin(), act.end() - 6), text));
    CHECK(attwn::unscrambleText(std::vector<uint8_t>(act.begin(), act.end() - 1), text));
    CHECK(attwn::unscrambleText({2}, text));
}

void testNames() {
    // The extension's first two letters made "ac" (0x403bd0), for the kinds
    // the game opens so; any case.
    CHECK(attwn::acName("Text/itemlist.txt") == "Text/itemlist.act");
    CHECK(attwn::acName("interface/cursor.TGA") == "interface/cursor.aca");
    CHECK(attwn::acName("x.jpg") == "x.acg" && attwn::acName("sound/vo/a.wav") == "sound/vo/a.acv");
    CHECK(attwn::acName("movie.bik").empty() && attwn::acName("scene.ac1").empty() && attwn::acName("txt").empty());
    CHECK(attwn::plainName("Text/Chapter01.act") == "Text/Chapter01.txt");
    CHECK(attwn::plainName("a.ACA") == "a.tga" && attwn::plainName("a.acg") == "a.jpg");
    CHECK(attwn::plainName("a.acv") == "a.wav" && attwn::plainName("scene.ac1").empty());
}

void testGameFiles(const std::filesystem::path &tmp) {
    const std::filesystem::path dir = tmp / "attwn";
    putFile(dir / "Text" / "itemlist.act", scramble({"Item\tItem Name\r\n", "Z0Record\trecord\r\n"}));
    putFile(dir / "Interface" / "cursor.aca", bytes("TGA"));
    putFile(dir / "Sound" / "VO" / "line.acv", bytes("RIFF"));
    putFile(dir / "FMV" / "intro.bik", bytes("BIKi"));
    putFile(dir / "Text" / "plain.txt", bytes("plain\n"));
    attwn::GameFiles files;
    CHECK(!files.open(dir));
    // Listed by their ordinary names; other kinds as they are.
    std::vector<std::string> names = files.list();
    std::sort(names.begin(), names.end());
    CHECK((names == std::vector<std::string>{"FMV/intro.bik", "Interface/cursor.tga", "Sound/VO/line.wav",
                                             "Text/itemlist.txt", "Text/plain.txt"}));
    // Read by them: the text unscrambled, the rest as it is.
    std::vector<uint8_t> data;
    CHECK(files.has("text/ITEMLIST.txt") && !files.read("text/ITEMLIST.txt", data));
    CHECK(data == bytes("Item\tItem Name\r\nZ0Record\trecord\r\n"));
    CHECK(!files.read("interface/cursor.tga", data) && data == bytes("TGA"));
    CHECK(!files.read("Sound/VO/line.wav", data) && data == bytes("RIFF"));
    CHECK(!files.read("Text/plain.txt", data) && data == bytes("plain\n"));
    // By its own name, an .act is as the install has it.
    CHECK(!files.read("Text/itemlist.act", data) && data.size() > 2 && data[2] == 16);
    CHECK(!files.has("text/missing.txt") && files.read("text/missing.txt", data));
    // A damaged .act says which file.
    putFile(dir / "Text" / "broken.act", {5, 0, 3});
    attwn::GameFiles again;
    CHECK(!again.open(dir));
    const Error err = again.read("Text/broken.txt", data);
    CHECK(err && err.message.find("broken.act") != std::string::npos);
}

// An .ac1 scene's bytes: tags as numbers, text a byte a character in
// fixed buffers.
struct Ac1 {
    std::vector<uint8_t> d;
    void u16(uint16_t v) { d.push_back(uint8_t(v)), d.push_back(uint8_t(v >> 8)); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; i++)
            d.push_back(uint8_t(v >> (8 * i)));
    }
    void f32(float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        u32(v);
    }
    void tag(const char *t) { u32(uint32_t(t[0]) << 24 | uint32_t(t[1]) << 16 | uint32_t(t[2]) << 8 | uint32_t(t[3])); }
    void text(const std::string &s, size_t chars) {
        for (size_t i = 0; i < chars; i++)
            d.push_back(i < s.size() ? uint8_t(s[i]) : 0xcd);
        d[d.size() - chars + std::min(s.size(), chars - 1)] = 0;
    }
};

void testScenes() {
    Ac1 b;
    b.tag("SCEN");
    b.text("Locations\\Kitchen\\Act01b\\kitchen01.ac1", 256);
    b.tag("BASE");
    b.text("Locations\\Kitchen\\Base\\kitchen01.ac1", 256);
    // A camera of 21 fields: its 18, the grid's size and its bytes, and
    // 1240 (ac.exe 0x4085e0), without Orient Express's whiteout.
    b.tag("CAMR");
    b.u16(21);
    for (int i = 0; i < 18; i++)
        i < 15 ? b.f32(float(i)) : b.u16(uint16_t(i));
    b.u16(2);
    for (uint8_t c : {0, 1, 2, 3})
        b.d.push_back(c);
    b.u32(7);
    // A picture, its name in Windows-1252 ("café"), and an OLDS: the
    // common fields' first two, then a SPRT of four.
    for (const char *t : {"2DOB", "OLDS"}) {
        b.tag(t);
        b.u16(2);
        b.text("Locations\\Kitchen\\Base\\Backgrounds\\kitchen01.jpg", 256);
        b.text("caf\xe9", 256);
        b.tag("SPRT");
        b.u16(4);
        b.u16(0);
        b.f32(1000);
        b.u16(1);
        b.u16(0);
    }
    b.tag("SHOW");
    b.u16(1);
    b.tag("DONE");
    Ac2Scene scene;
    CHECK(!parseAc1(b.d, scene) && scene.end == b.d.size());
    CHECK(scene.path == "Locations\\Kitchen\\Act01b\\kitchen01.ac1");
    CHECK(scene.records.size() == 7 && scene.records[1].tag == "BASE");
    CHECK(scene.records[1].text("scene") == "Locations\\Kitchen\\Base\\kitchen01.ac1");
    const Ac2Record *cam = scene.camera();
    CHECK(cam && cam->f("ambientG") == 1 && cam->field("grid") && cam->field("grid")->bytes.size() == 4);
    CHECK(cam && cam->i("1240") == 7 && !cam->field("whiteoutStart"));
    const std::vector<const Ac2Record *> objects = scene.objects();
    CHECK(objects.size() == 2 && objects[1]->tag == "OLDS" && objects[1]->text("name") == "caf\xc3\xa9");
    CHECK(objects.size() == 2 && objects[1]->part("SPRT") && objects[1]->part("SPRT")->f("scrollSpeed") == 1000);
    // Cut short: an error, not a read past the end.
    CHECK(parseAc1(std::vector<uint8_t>(b.d.begin(), b.d.begin() + 300), scene));
}

void testScriptWords() {
    GameState state;
    state.game = &attwn::game();
    state.act = "02a";
    testing::FakeHost host;
    ScriptRunner r(state, host);
    // inc: the act's counter, when the act is one named (any case);
    // "needed" or else "possible".
    r.run("inc needed 01a 02A, inc needed 02a, inc possible 02a, inc possible 03a");
    for (int i = 0; i < 8 && !r.idle(); i++)
        r.update(0.1f);
    CHECK(state.tags["02a_neededcounter"] == 2 && state.tags["02a_possiblecounter"] == 1);
    CHECK(!state.tags.count("03a_possiblecounter") && host.started.empty());
    // suspicion: the tag suspicion_<who>, 0 if unset, against a number.
    state.tags["suspicion_wargrave"] = 2;
    CHECK(testCondition("suspicion wargrave 2", state) && !testCondition("suspicion wargrave 1", state));
    CHECK(testCondition("suspicion wargrave >1", state) && testCondition("suspicion Wargrave <3", state));
    CHECK(testCondition("suspicion blore 0", state) && testCondition("!suspicion blore >0", state));
    // Orient Express has neither word: "suspicion" doesn't hold there, and
    // "inc" goes to the host as a verb it doesn't know.
    GameState other;
    other.tags["suspicion_wargrave"] = 2;
    CHECK(!testCondition("suspicion wargrave 2", other));
    other.act = "02a";
    ScriptRunner r2(other, host);
    r2.run("inc needed 02a");
    for (int i = 0; i < 8 && !r2.idle(); i++)
        r2.update(0.1f);
    CHECK(!other.tags.count("02a_neededcounter") && host.started.size() == 1 && host.started[0] == "inc needed 02a");
}

void testDetection(const std::filesystem::path &tmp) {
    // Its files, whatever their contents: the fallback.
    const std::filesystem::path dir = tmp / "detect";
    putFile(dir / "ac.exe", bytes("MZ"));
    putFile(dir / "granny2.dll", bytes("MZ"));
    putFile(dir / "Text" / "itemlist.act", scramble({"Item\n"}));
    const std::vector<DetectedGame> found = detectGames(dir);
    CHECK(found.size() == 1 && found[0].gameId == "attwn");
    CHECK(found.size() == 1 && found[0].description.find("unknown release") != std::string::npos);
    // Opened as the game opens its files.
    std::unique_ptr<Archive> files;
    const GameInfo *game = nullptr;
    CHECK(!openInstall(dir, files, &game) && game && std::string(game->id) == "attwn" && !game->playable);
    CHECK(files && files->has("text/itemlist.txt"));
    // Orient Express's files are still Orient Express.
    const std::filesystem::path motoe = tmp / "motoe";
    putFile(motoe / "motoe.exe", bytes("MZ"));
    putFile(motoe / "granny2.dll", bytes("MZ"));
    putFile(motoe / "Text" / "itemlist.txt", bytes("Item\n"));
    const std::vector<DetectedGame> other = detectGames(motoe);
    CHECK(other.size() == 1 && other[0].gameId == "motoe");
    CHECK(!openInstall(motoe, files, &game) && game && std::string(game->id) == "motoe" && game->playable);
    CHECK(findGame("attwn") && findGame("motoe") && !findGame("euts"));
}

} // namespace

static const reluva::test::Suite kSuite("awe.unit.attwn",
                                        {
                                            {"unscrambling", [](const std::filesystem::path &) { testUnscramble(); }},
                                            {"names", [](const std::filesystem::path &) { testNames(); }},
                                            {"its files", [](const std::filesystem::path &tmp) { testGameFiles(tmp); }},
                                            {"scenes (.ac1)", [](const std::filesystem::path &) { testScenes(); }},
                                            {"script words", [](const std::filesystem::path &) { testScriptWords(); }},
                                            {"detection", [](const std::filesystem::path &tmp) { testDetection(tmp); }},
                                        });
