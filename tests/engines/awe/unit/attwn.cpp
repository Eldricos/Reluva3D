// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// And Then There Were None: its files as the game names and reads them,
// its scenes, and recognising the game, on files made here.

#include "engines/awe/attwn/attwn.h"
#include "engines/awe/attwn/files.h"
#include "engines/awe/formats/tables.h"
#include "engines/awe/formats/ac2.h"
#include "engines/awe/games.h"
#include "engines/awe/game/conditions.h"
#include "engines/awe/game/dialogue.h"
#include "engines/awe/game/script.h"
#include "engines/awe/render/lighting.h"
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

void testDialogueTables() {
    // Its columns, as ac.exe's loader (0x431160) reads them: the 7th, Tone,
    // a note for the voice, dropped; Duration and Location unread.
    const std::string table =
        "Name\tAct\tConver.\tThread\tNest\tSpeaker\tTone\tLine\tLine Translation\tCondition\t"
        "Repeat\tPrevent\tEvent\tPovent\tDuration\tLocation\n"
        "marston\t01a\ta\ta\ta\tmarston\tsleeping noises\tNarracott!\t0\t0\t0\t0\t0\t0\t2,5\thall\n";
    DialogueBook book;
    book.tables = attwn::game().dialogue;
    book.add(parseTable(table));
    const std::vector<const DialogueLine *> lines = book.conversation("marston");
    CHECK(lines.size() == 1 && lines[0]->speaker == "marston" && lines[0]->text == "Narracott!");
    CHECK(lines.size() == 1 && lines[0]->target.empty() && lines[0]->duration == 0 && lines[0]->optional.empty());
    CHECK(book.tables.wrap == 750 && book.tables.events.size() == 3 &&
          std::string(book.tables.events[0]) == "dialogue/acts/events/");
    // Read as Orient Express's, the 7th would be a target to turn to.
    DialogueBook theirs;
    theirs.add(parseTable(table));
    CHECK(theirs.conversation("marston").size() == 1 && theirs.conversation("marston")[0]->target == "sleeping noises");
    CHECK(theirs.tables.wrap == 944);
}

// No voices, no events: each line lasts its length / 10 seconds, at least
// two (0x4333f7).
struct QuietHost : DialogueHost {
    bool playVoice(const std::string &) override { return false; }
    bool voicePlaying() override { return false; }
    void stopVoice() override {}
    std::string readText(const std::string &) override { return {}; }
    std::string sceneName() override { return "kitchen01"; }
    void talkStarting(const std::string &name) override { started.push_back(name); }
    std::vector<std::string> started;
};

void testDialogueManager() {
    const std::string head = "Name\tAct\tConver.\tThread\tNest\tSpeaker\tTone\tLine\tLine Translation\tCondition\t"
                             "Repeat\tPrevent\tEvent\tPovent\n";
    auto row = [](const std::string &cells) { return cells + "\t0\t0\t0\t0\t0\t0\n"; };
    std::string table =
        head + row("armstrong\t01a\ta\t1\ta\tarmstrong\t0\tInspector.") +
        row("armstrong\t01a\ta\ta\ta\tnarracott\t0\tYour name?") +
        row("armstrong\t01a\ta\ta\tb\tarmstrong\t0\tArmstrong.") + row("armstrong\t01a\ta\ta\tc\tblore\t0\tBlore.") +
        row("armstrong\t01a\ta\tb\ta\tnarracott\t0\tWhere were you?") +
        row("armstronggen\t0\ta\ta\ta\tnarracott\t0\tThe record?") +
        row("blore\t01a\ta\ta\ta\tnarracott\t0\tYour name?") + row("blore\t01a\ta\tb\ta\tnarracott\t0\tAnd you?") +
        row("armstronggenx\t0\ta\ta\ta\tnarracott\t0\tNot his.") +
        row("narracottgen\t0\t5\ta\ta\tnarracott\t0\tNothing more to say to her.") +
        row("narracottgen\t0\t6\ta\ta\tnarracott\t0\tNothing more to say to him.");
    for (int r = 0; r < 5; r++)
        table += row("armstrongexit\t0\t" + std::to_string(r) + "\t3\ta\tnarracott\t0\tGood day, Doctor.");
    GameState state;
    testing::FakeHost scripts;
    QuietHost host;
    DialogueManager d(state, scripts, host);
    d.setGame(attwn::game());
    d.book().add(parseTable(table));
    ScriptRunner runner(state, scripts);
    auto run = [&](float seconds) {
        for (float t = 0; t < seconds; t += 0.05f)
            d.update(0.05f);
    };
    // A click on him: the opening, then his own conversation's lines, the
    // one named exactly armstronggen (0x4324f4), and a goodbye: four rows,
    // so the box is 115 pixels, the second window (0x4328b6). It slides in
    // from the foot of the 800x600 frame. The inventory is left as it is.
    CHECK(d.talk(runner, "armstrong", 0, -1, -1, true, -1, false, true));
    CHECK(host.started.empty() && d.boxHeight() == 115 && d.window() == 1 && d.boxTop() == 600);
    run(0.5f);
    CHECK(d.boxTop() == 485 && d.line() && d.line()->text == "Inspector." && d.colour() == 9);
    run(2.5f);
    CHECK(d.choosing() && d.choices().size() == 4);
    if (d.choices().size() == 4)
        CHECK(d.choices()[0].text == "Your name?" && d.choices()[2].text == "The record?" &&
              d.choices()[3].text == "Good day, Doctor.");
    // Narracott's colour; then the others alternate, starting again after
    // each of his lines (0x433254).
    CHECK(d.choose(0) && d.line() && d.colour() == 8);
    run(2.5f);
    CHECK(d.line() && d.line()->text == "Armstrong." && d.colour() == 9);
    run(2.5f);
    CHECK(d.line() && d.line()->text == "Blore." && d.colour() == 10);
    run(2.5f);
    // Leaving: the goodbye, then the box slides away.
    CHECK(d.choosing() && d.choices().size() == 3 && d.choose(2));
    run(5.0f);
    CHECK(!d.active() && runner.idle());
    // Again, not by a click: no regreet, no visit tag; the opening, heard,
    // isn't played. The goodbye the click made stays in the conversation
    // (0x432c70), as the next click's replaces it. Two rows: 90 pixels.
    CHECK(d.talk(runner, "armstrong", 0, -1, -1, false, -1, false, true));
    CHECK(state.tags.empty());
    run(0.5f);
    CHECK(d.choosing() && d.choices().size() == 2 && d.boxHeight() == 90 && d.window() == 0);
    CHECK(d.choose(0));
    run(2.5f);
    // Only the goodbye left: it's said without asking.
    CHECK(!d.choosing() && d.line() && d.line()->text == "Good day, Doctor.");
    run(5.0f);
    CHECK(!d.active());
    // A choice left alone, not a goodbye, is offered, not played
    // (0x432ec1).
    CHECK(d.talk(runner, "blore", 0, -1, -1, false, -1, false, true));
    run(0.5f);
    CHECK(d.choosing() && d.choices().size() == 2 && d.choose(0));
    run(2.5f);
    CHECK(d.choosing() && d.choices().size() == 1 && d.choices()[0].text == "And you?");
    // The talk command: one option, no nogreet, no "+" (0x40d5f0).
    const TalkArgs t = parseTalkArgs({"armstrong", "a", "nogen", "3"}, true, attwn::game().talk);
    CHECK(!t.gen && t.number == -1 && t.greet && !t.plus);
    const TalkArgs plus = parseTalkArgs({"armstrong", "a+", "nogreet"}, true, attwn::game().talk);
    CHECK(!plus.plus && plus.greet && plus.number == 0 && plus.thread == '+' - 'A');
}

void testNothingToSay() {
    // A talk that finds nothing, whatever started it: Narracott's line,
    // "her" or "him" by the last character talked to (0x4325a0).
    std::string table = "Name\tAct\tConver.\tThread\tNest\tSpeaker\tTone\tLine\n";
    for (const char *line :
         {"armstrong\t01a\ta\ta\ta\tarmstrong\t0\tYes?", "claythorne\t01a\ta\ta\ta\tclaythorne\t0\tYes?",
          "narracottgen\t0\t5\ta\ta\tnarracott\t0\tTo her.", "narracottgen\t0\t6\ta\ta\tnarracott\t0\tTo him."})
        table += std::string(line) + "\t0\t0\t0\t0\t0\t0\n";
    GameState state;
    testing::FakeHost scripts;
    QuietHost host;
    DialogueManager d(state, scripts, host);
    d.setGame(attwn::game());
    d.book().add(parseTable(table));
    ScriptRunner runner(state, scripts);
    CHECK(attwn::game().talk.noTalkAlways);
    // No one talked to yet: the list's "contains" finds the empty name.
    CHECK(d.noTalk(runner) && d.line() && d.line()->text == "To her.");
    for (int i = 0; i < 200 && d.active(); i++)
        d.update(0.05f);
    CHECK(d.talk(runner, "armstrong", 0, -1, -1, false, -1, false, true));
    for (int i = 0; i < 200 && d.active(); i++)
        d.update(0.05f);
    CHECK(d.noTalk(runner) && d.line() && d.line()->text == "To him.");
}

void testLighting() {
    // A base scene's two lights and an act's version's one, merged.
    auto num = [](float v) {
        Ac2Value x;
        x.type = Ac2Value::Type::kF32;
        std::memcpy(&x.raw, &v, 4);
        return x;
    };
    auto whole = [](uint32_t v) {
        Ac2Value x;
        x.type = Ac2Value::Type::kU16;
        x.raw = v;
        return x;
    };
    auto light = [&](float y) { return Ac2Record{"LITE", {{"type", whole(3)}, {"y", num(y)}, {"dirY", num(-1)}}, {}}; };
    Ac2Scene scene;
    scene.records.push_back(light(1));
    scene.records.push_back(light(2));
    scene.actFrom = scene.records.size();
    scene.records.push_back(light(3));
    GameState state;
    state.game = &attwn::game();
    state.values["time"] = 2;
    // Night indoors: the base's lights off, the act's own on (0x41d760).
    SceneLighting lit = sceneLighting(scene, state);
    CHECK(lit.lights.size() == 1 && lit.lights[0].position[1] == 3);
    // A light the scene leaves at its defaults: this game's constructor's
    // (0x41d5a0), white and barely attenuated.
    CHECK(lit.lights.size() == 1 && lit.lights[0].diffuse[0] == 1 && lit.lights[0].attenuation[0] == 0);
    // Orient Express's: every light off at night.
    state.game = nullptr;
    CHECK(sceneLighting(scene, state).lights.empty());
    // Outdoors, this game's lights all stay on at night.
    state.game = &attwn::game();
    scene.records.push_back({"OUTD", {{"value", whole(1)}}, {}});
    CHECK(sceneLighting(scene, state).lights.size() == 3);
    // Without a camera, the ambient is the constructor's 0.5 grey
    // (0x408510); by day.
    state.values["time"] = 0;
    lit = sceneLighting(scene, state);
    CHECK(lit.lights.size() == 3 && lit.ambient[0] == 127 / 255.0f);
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

static const reluva::test::Suite
    kSuite("awe.unit.attwn", {
                                 {"unscrambling", [](const std::filesystem::path &) { testUnscramble(); }},
                                 {"names", [](const std::filesystem::path &) { testNames(); }},
                                 {"its files", [](const std::filesystem::path &tmp) { testGameFiles(tmp); }},
                                 {"scenes (.ac1)", [](const std::filesystem::path &) { testScenes(); }},
                                 {"script words", [](const std::filesystem::path &) { testScriptWords(); }},
                                 {"dialogue tables", [](const std::filesystem::path &) { testDialogueTables(); }},
                                 {"dialogue manager", [](const std::filesystem::path &) { testDialogueManager(); }},
                                 {"nothing to say", [](const std::filesystem::path &) { testNothingToSay(); }},
                                 {"lighting", [](const std::filesystem::path &) { testLighting(); }},
                                 {"detection", [](const std::filesystem::path &tmp) { testDetection(tmp); }},
                             });
