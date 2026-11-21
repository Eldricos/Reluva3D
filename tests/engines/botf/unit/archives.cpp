// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Birth of the Federation's unit tests: alt.res and its LZSS, the saved
// games, the .snd archives, and finding the game's files.

#include "audio/sound.h"
#include "audio/wav.h"
#include "engines/botf/formats/alt_res.h"
#include "engines/botf/formats/animation.h"
#include "engines/botf/formats/save_game.h"
#include "engines/botf/formats/snd_archive.h"
#include "engines/botf/game_files.h"
#include "image/image.h"

#include "tests/engines/botf/unit/helpers.h"
#include "tests/support/test.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::botf;
using namespace reluva::botf::test;

namespace {

// A small LZSS encoder in alt.res's format, writing the window from
// position 1 as its tool does: runs of a byte copied from 1 byte back,
// repeats of the last 2 bytes (a pixel) from 2 back, and literals for the
// rest. A decoder starting its window anywhere else reads the wrong bytes.
Bytes compress(const Bytes &raw) {
    std::vector<Bytes> items;
    size_t r = 1;
    for (size_t i = 0; i < raw.size();) {
        size_t len = 0;
        size_t back = 0;
        for (size_t d : {size_t(1), size_t(2)}) {
            size_t n = 0;
            if (i >= d)
                while (i + n < raw.size() && n < 17 && raw[i + n] == raw[i - d + n % d])
                    ++n;
            if (n > len)
                len = n, back = d;
        }
        if (len >= 2) {
            const size_t from = (r + 4096 - back) % 4096;
            items.push_back({uint8_t((len - 2) << 4 | from >> 8), uint8_t(from & 0xff)});
            i += len;
            r = (r + len) % 4096;
        } else {
            items.push_back({raw[i]});
            ++i;
            r = (r + 1) % 4096;
        }
    }
    Bytes out;
    for (size_t k = 0; k < items.size(); k += 8) {
        uint8_t flags = 0;
        for (size_t j = 0; j < 8 && k + j < items.size(); ++j)
            if (items[k + j].size() == 1)
                flags |= uint8_t(1 << j);
        out.push_back(flags);
        for (size_t j = 0; j < 8 && k + j < items.size(); ++j)
            out.insert(out.end(), items[k + j].begin(), items[k + j].end());
    }
    return out;
}

// A small 16-bit TGA: runs of colours that change every few pixels, and
// some black, so copies read from the wrong place give the wrong bytes.
Bytes makeTga(int w, int h, uint8_t seed) {
    Bytes t = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    put16(t, uint32_t(w));
    put16(t, uint32_t(h));
    t.push_back(16);
    t.push_back(1);
    for (int i = 0; i < w * h; ++i) {
        const int run = i / (3 + (i / 50) % 9);
        put16(t, run % 5 == 0 ? 0 : uint32_t((run * 2654435761u + seed) >> 17) & 0x7fff);
    }
    return t;
}

void testLzss() {
    // The start of the retail alt.res's first file: a flag byte, literals
    // and two references that fill the TGA header's zeros; the decoded
    // header (800x600, 16-bit) is how the format was worked out.
    const Bytes start = {0xe7, 0x00, 0x00, 0x02, 0x00, 0x01, 0x50, 0x05, 0x20,
                         0x03, 0x58, 0x07, 0x02, 0x10, 0x01, 0x70, 0x04};
    Bytes out;
    size_t used = 0;
    CHECK(!decompressLzss(start.data(), start.size(), 18, out, &used));
    const Bytes header = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0x03, 0x58, 0x02, 0x10, 0x01};
    CHECK(out == header);
    CHECK(used == 15);

    // Round trip through the encoder above.
    const Bytes raw = makeTga(40, 30, 3);
    const Bytes packed = compress(raw);
    CHECK(packed.size() < raw.size());
    CHECK(!decompressLzss(packed.data(), packed.size(), raw.size(), out, &used));
    CHECK(out == raw);
    CHECK(used == packed.size());

    // Too little input fails.
    CHECK(decompressLzss(packed.data(), packed.size() / 2, raw.size(), out));

    // A reference to the window before anything is written there copies
    // what it was filled with: zeros, or a saved game's spaces.
    const Bytes early = {0xfe, 0x10, 0x80};
    CHECK(!decompressLzss(early.data(), early.size(), 3, out) && out == Bytes(3, 0));
    CHECK(!decompressLzss(early.data(), early.size(), 3, out, nullptr, 0x20) && out == Bytes(3, 0x20));
}

// A saved game's galaxy: a 3x2 grid, a system in the middle of the top row
// (the player's, with one planet and so a production queue) and a nebula
// below it, laid out as docs/engines/botf/formats/sav.md says.
Bytes makeGalaxySave(int16_t systems, bool withQueue) {
    auto put16At = [](Bytes &b, size_t at, uint16_t v) {
        b[at] = uint8_t(v);
        b[at + 1] = uint8_t(v >> 8);
    };
    Bytes info(48, 0);
    set32(info, 0x08, 1);
    put16At(info, 0x0C, uint16_t(systems));
    put16At(info, 0x0E, 1);
    put16At(info, 0x14, 6);
    Bytes sectors;
    for (uint32_t i = 0; i < 6; ++i) {
        Bytes e(104, 0);
        set32(e, 0, i / 3);
        set32(e, 4, i % 3);
        put16At(e, 8, 0xFFFF);
        put16At(e, 10, i == 4 ? 0 : 0xFFFF);
        put16At(e, 12, i == 1 ? 0 : 0xFFFF);
        put16At(e, 14, i == 1 ? 0x10 : 0);
        set32(e, 40, i < 3 ? 0x10 : 0);
        sectors.insert(sectors.end(), e.begin(), e.end());
    }
    Bytes system(0x328, 0);
    set32(system, 4, 2);
    const std::string name = "Romulus";
    const std::string star = "s-r-";
    std::copy(name.begin(), name.end(), system.begin() + 0x08);
    system[0x31] = 4;
    std::copy(star.begin(), star.end(), system.begin() + 0x32);
    set32(system, 0x40, 355);
    put16At(system, 0x44, 4);
    set32(system, 0x4C, 4);
    put16At(system, 0x6A, 1);
    put16At(system, 0x6E, 3);
    for (uint32_t i = 0; i < 5; ++i)
        set32(system, 0x54 + 4 * i, 10 * (i + 1));
    set32(system, 0x80, 1);
    put16At(system, 0x88, 154);
    put16At(system, 0x90, 30);
    set32(system, 0xBC, 3350);
    set32(system, 0x78, 7);
    set32(system, 0x7C, 2);
    system[0x308] = 1; // one trade route coming in
    Bytes planet(76, 0);
    const std::string pname = "Romulus II\n";
    const std::string pani = "atol.ani";
    std::copy(pname.begin(), pname.end(), planet.begin());
    std::copy(pani.begin(), pani.end(), planet.begin() + 0x28);
    put16At(planet, 0x38, 2);
    planet[0x48] = 4;
    planet[0x49] = 10;
    system.insert(system.end(), planet.begin(), planet.end());
    system.insert(system.end(), 4 + 4, 0); // the planet's word, the trade route's
    if (withQueue) {
        system.insert(system.end(), 108, 0);
        system.back() = 1; // the first item bought
    }
    Bytes stellar(88, 0);
    set32(stellar, 0, 2);
    const std::string sname = "Nebula B2";
    std::copy(sname.begin(), sname.end(), stellar.begin() + 4);
    set32(stellar, 68, 7);
    set32(stellar, 72, 7);
    return makeLzssArchive("game0.sav",
                           {{"galInfo", info}, {"sector.lst", sectors}, {"systInfo", system}, {"stellInfo", stellar}});
}

void testSavedGalaxy(const std::filesystem::path &tmp) {
    writeFile(tmp / "galaxy.sav", makeGalaxySave(1, true));
    SavedGalaxy g;
    CHECK(!readSavedGalaxy(tmp / "galaxy.sav", g));
    CHECK(g.size == 1 && g.width == 3 && g.height == 2 && g.sectors.size() == 6);
    CHECK(g.at(1, 0).system == 0 && g.at(1, 0).claims == 0x10 && g.at(1, 0).explored == 0x10);
    CHECK(g.at(1, 1).stellarObject == 0 && g.at(1, 1).system == -1 && g.at(0, 0).starbase == -1);
    CHECK(g.systems.size() == 1 && g.stellarObjects.size() == 1);
    const SavedSystem &s = g.systems[0];
    CHECK(s.name == "Romulus" && s.starAnimation == "s-r-" && s.starType == 4 && s.control == 2);
    CHECK(s.population == 355 && s.residentRace == 4 && s.controllingRace == 4 && s.dilithium == 3);
    CHECK(s.mapX == 7 && s.mapY == 2 && s.planets.size() == 1 && s.maxPopulation == 335);
    CHECK(s.workers == (std::array<int, 5>{10, 20, 30, 40, 50}) && s.buildOrder == 1 && s.building == 154);
    CHECK(s.ship == 30 && s.buying);
    // The planet's name without the line break the game ends it with.
    CHECK(s.planets[0].name == "Romulus II" && s.planets[0].animation == "atol.ani");
    CHECK(s.planets[0].size == 2 && s.planets[0].type == 4 && s.planets[0].atmosphere == 10);
    CHECK(g.stellarObjects[0].type == 2 && g.stellarObjects[0].name == "Nebula B2" && g.stellarObjects[0].mapY == 7);
    // Counts that don't agree, and a system cut short (the player's without
    // its production queue), aren't read.
    writeFile(tmp / "bad.sav", makeGalaxySave(2, true));
    CHECK(bool(readSavedGalaxy(tmp / "bad.sav", g)));
    writeFile(tmp / "short.sav", makeGalaxySave(1, false));
    CHECK(bool(readSavedGalaxy(tmp / "short.sav", g)));
    // Nor a save without them.
    writeFile(tmp / "none.sav", makeSave("x", "", 0));
    CHECK(bool(readSavedGalaxy(tmp / "none.sav", g)));
}

void testSavedTaskForces(const std::filesystem::path &tmp) {
    // Room for three, two in use: one at column 10, row 8, ordered to
    // sector 113 (column 9 of 13, row 8); one with no orders.
    Bytes head(40, 0);
    set32(head, 0x10, 108);
    set32(head, 0x14, 3);
    set32(head, 0x18, 2);
    Bytes list(3 * 108, 0);
    for (uint32_t i = 0; i < 2; ++i) {
        const size_t at = i * 108;
        list[at] = uint8_t(7 + i);
        set32(list, at + 0x04, 1);
        set32(list, at + 0x08, 7 + i);
        set32(list, at + 0x2C, 6 - i);
        set32(list, at + 0x30, i == 0 ? 113 : 0xFFFFFFFF);
        set32(list, at + 0x34, 1);
        set32(list, at + 0x38, 8);
        set32(list, at + 0x3C, 10);
    }
    writeFile(tmp / "fleet.sav", makeLzssArchive("game0.sav", {{"GWTForceHd", head}, {"GWTForce", list}}));
    std::vector<SavedTaskForce> t;
    CHECK(!readSavedTaskForces(tmp / "fleet.sav", 13, t) && t.size() == 2);
    CHECK(t[0].id == 7 && t[0].owner == 1 && t[0].ships == 1 && t[0].x == 10 && t[0].y == 8 && t[0].mission == 6);
    CHECK(t[0].destinationX == 9 && t[0].destinationY == 8 && t[1].destinationX == -1 && t[1].mission == 5);
    CHECK(t[0].shipIds == std::vector<int>{7} && t[1].shipIds == std::vector<int>{8});
    // More in use than the list holds isn't read.
    set32(head, 0x18, 4);
    writeFile(tmp / "fleet.sav", makeLzssArchive("game0.sav", {{"GWTForceHd", head}, {"GWTForce", list}}));
    CHECK(bool(readSavedTaskForces(tmp / "fleet.sav", 13, t)));

    // The empires: the Federation's, second of five 444-byte entries.
    Bytes emps(5 * 444, 0);
    const std::string empire = "the United Federation of Planets";
    const std::string leader = "the Federation President";
    std::copy(empire.begin(), empire.end(), emps.begin() + 444);
    std::copy(leader.begin(), leader.end(), emps.begin() + 444 + 40);
    set32(emps, 444 + 0x54, 1835);
    set32(emps, 444 + 0x58, 70);
    set32(emps, 444 + 0x60, 1);
    set32(emps, 444 + 0x68, 34);
    set32(emps, 444 + 0x6C, 310);
    writeFile(tmp / "emps.sav", makeLzssArchive("game0.sav", {{"empsInfo", emps}}));
    std::array<SavedEmpire, 5> e;
    CHECK(!readSavedEmpires(tmp / "emps.sav", e));
    CHECK(e[1].name == empire && e[1].leader == leader && e[1].credits == 1835 && e[1].creditsPerTurn == 70);
    CHECK(e[1].dilithiumPerTurn == 1 && e[1].researchPoints == 34 && e[1].population == 310 && e[0].name.empty());
    emps.resize(444);
    writeFile(tmp / "emps.sav", makeLzssArchive("game0.sav", {{"empsInfo", emps}}));
    CHECK(bool(readSavedEmpires(tmp / "emps.sav", e)));

    // Research: the Federation's shares, after the count and 35 races' index.
    Bytes tech(2 + 70 + 5 * 20, 0);
    const uint16_t shares[6] = {20, 17, 15, 15, 16, 17};
    for (size_t f = 0; f < 6; ++f)
        tech[2 + 70 + 20 + 6 + 2 * f] = uint8_t(shares[f]);
    writeFile(tmp / "tech.sav", makeLzssArchive("game0.sav", {{"techinfo", tech}}));
    SavedResearch r;
    CHECK(!readSavedResearch(tmp / "tech.sav", r));
    CHECK(r[1] == (std::array<int, 6>{20, 17, 15, 15, 16, 17}) && r[0] == (std::array<int, 6>{}));
    tech.resize(100);
    writeFile(tmp / "tech.sav", makeLzssArchive("game0.sav", {{"techinfo", tech}}));
    CHECK(bool(readSavedResearch(tmp / "tech.sav", r)));
}

void testSavedResults(const std::filesystem::path &tmp) {
    // result.lst: chunks of a 12-byte header (address, size, size stored)
    // and LZSS; the count first, then each result's 40-byte header and its
    // content.
    Bytes lst;
    auto chunk = [&](const Bytes &raw) {
        const Bytes packed = lzssLiterals(raw);
        put32(lst, uint32_t(lst.size() + 12));
        put32(lst, uint32_t(raw.size()));
        put32(lst, uint32_t(packed.size()));
        lst.insert(lst.end(), packed.begin(), packed.end());
    };
    Bytes count;
    put32(count, 2);
    chunk(count);
    auto result = [&](int16_t race, int16_t kind, int32_t value, int16_t content) {
        Bytes h(40, 0);
        h[4] = uint8_t(race);
        h[8] = uint8_t(kind);
        h[9] = uint8_t(kind >> 8);
        set32(h, 16, uint32_t(value));
        h[22] = uint8_t(content);
        chunk(h);
        chunk(Bytes(16, 0));
    };
    result(1, 0x300, -1, 4);
    result(3, 0x100, 7, 31);
    const auto path = tmp / "results.sav";
    writeFile(path, makeLzssArchive("results", {{"result.lst", lst}}, {"result.lst"}));
    std::vector<SavedResult> r;
    CHECK(!readSavedResults(path, r) && r.size() == 2);
    CHECK(r[0].race == 1 && r[0].kind == 0x300 && r[0].eventValue == -1 && r[0].content == 4);
    CHECK(r[1].race == 3 && r[1].kind == 0x100 && r[1].eventValue == 7 && r[1].content == 31);
    // A save without result.lst has no results; one whose chunk runs past
    // its end is an error.
    writeFile(path, makeSave("none", "x", 1));
    CHECK(!readSavedResults(path, r) && r.empty());
    lst.resize(lst.size() - 3);
    writeFile(path, makeLzssArchive("results", {{"result.lst", lst}}, {"result.lst"}));
    CHECK(readSavedResults(path, r));
}

void testSavedOrders(const std::filesystem::path &tmp) {
    // An order: a 32-byte head (id, turn, race, type at 0x10, the rest's
    // size at 0x1C), then the rest.
    auto order = [](uint32_t type, const Bytes &rest) {
        Bytes o(32, 0);
        set32(o, 0, 0x2AAAAB00 + type);
        o[8] = 1; // the Federation
        set32(o, 0x10, type);
        set32(o, 0x1C, uint32_t(rest.size()));
        o.insert(o.end(), rest.begin(), rest.end());
        return o;
    };
    Bytes build(52, 0);
    set32(build, 4, 0x8000001B); // Sol, and the flag the game sets
    build[8] = 154;
    set32(build, 16, 1);
    build[20] = 0xFF;
    build[21] = 0xFF;
    Bytes move(52, 0);
    set32(move, 4, 8);
    set32(move, 8, 9);
    move[28] = 7;
    Bytes labor;
    for (uint32_t v : {27U, 140U, 40U, 100U, 20U, 0U})
        put32(labor, v);
    Bytes research;
    for (uint32_t v : {20U, 17U, 15U, 15U, 16U, 17U})
        put16(research, v);
    Bytes all = order(0, build);
    for (const Bytes &o : {order(2, move), order(7, labor), order(1, research), order(5, Bytes(10, 0))})
        all.insert(all.end(), o.begin(), o.end());
    writeFile(tmp / "orders.sav", makeLzssArchive("game0.sav", {{"ordInfo", all}}));
    std::vector<SavedOrder> o;
    CHECK(!readSavedOrders(tmp / "orders.sav", o) && o.size() == 5);
    CHECK(o[0].type == 0 && o[0].race == 1 && o[0].system == 27 && o[0].buy == 0 && o[0].buildOrder == 1);
    CHECK(o[0].building == 154 && o[0].ship == -1);
    CHECK(o[1].type == 2 && o[1].taskForce == 7 && o[1].destinationX == 9 && o[1].destinationY == 8 &&
          o[1].mission == 0);
    CHECK(o[2].type == 7 && o[2].system == 27 && o[2].values == (std::array<int, 6>{140, 40, 100, 20, 0, 0}));
    CHECK(o[3].type == 1 && o[3].values == (std::array<int, 6>{20, 17, 15, 15, 16, 17}) && o[4].type == 5);
    // An order running past the file's end isn't read.
    all.pop_back();
    writeFile(tmp / "orders.sav", makeLzssArchive("game0.sav", {{"ordInfo", all}}));
    CHECK(bool(readSavedOrders(tmp / "orders.sav", o)));
}

void testSavedShips(const std::filesystem::path &tmp) {
    // Room for two, one in use: the Oberth class Science Vessel the
    // Federation starts with.
    Bytes head(40, 0);
    set32(head, 0x10, 116);
    set32(head, 0x14, 2);
    set32(head, 0x18, 1);
    Bytes list(2 * 116, 0);
    const std::string name = "USS Martynov A";
    std::copy(name.begin(), name.end(), list.begin());
    list[0x28] = 1;
    list[0x2A] = 7;
    set32(list, 0x38, 110);
    set32(list, 0x3C, 19);
    set32(list, 0x40, 350);
    list[0x50] = 7;
    set32(list, 0x52, 35);
    writeFile(tmp / "ships.sav", makeLzssArchive("game0.sav", {{"GShipHead", head}, {"GShipList", list}}));
    std::vector<SavedShip> s;
    CHECK(!readSavedShips(tmp / "ships.sav", s) && s.size() == 1);
    CHECK(s[0].name == name && s[0].race == 1 && s[0].id == 7 && s[0].shipClass == 35 && s[0].taskForce == 7);
    CHECK(s[0].shields == 110 && s[0].hull == 19 && s[0].experience == 350);
    set32(head, 0x10, 108);
    writeFile(tmp / "ships.sav", makeLzssArchive("game0.sav", {{"GShipHead", head}, {"GShipList", list}}));
    CHECK(bool(readSavedShips(tmp / "ships.sav", s)));
}

void testSaveSummary(const std::filesystem::path &tmp) {
    writeFile(tmp / "game0.sav", makeSave("Tutorial #1", "Irregular galaxy \xe9", 41));
    SaveSummary save;
    CHECK(!readSaveSummary(tmp / "game0.sav", save));
    CHECK(save.name == "Tutorial #1");
    CHECK(save.description == "Irregular galaxy \xc3\xa9"); // Windows-1252 to UTF-8
    CHECK(save.turn == 41 && !save.multiplayer && save.empire == 1);
    // An empire past the Romulans isn't one.
    writeFile(tmp / "game3.sav", makeSave("x", "", 0, 7));
    CHECK(!readSaveSummary(tmp / "game3.sav", save) && save.empire == -1);
    // Without gameInfo, or not an archive, it isn't read.
    writeFile(tmp / "game1.sav", makeLzssArchive("x", {{"version", Bytes(8, 1)}}));
    CHECK(bool(readSaveSummary(tmp / "game1.sav", save)));
    writeFile(tmp / "game2.sav", Bytes(10, 0));
    CHECK(bool(readSaveSummary(tmp / "game2.sav", save)));
}

void testAltRes(const std::filesystem::path &tmp) {
    // Two files, stored last first as in the retail alt.res.
    const Bytes a = makeTga(16, 8, 1);
    const Bytes b = makeTga(20, 10, 2);
    const Bytes pa = compress(a);
    const Bytes pb = compress(b);
    Bytes file(24, 0);
    std::memcpy(file.data(), "alt.res", 7);
    const uint32_t offB = 24;
    const uint32_t offA = uint32_t(24 + pb.size());
    file.insert(file.end(), pb.begin(), pb.end());
    file.insert(file.end(), pa.begin(), pa.end());
    set32(file, 16, uint32_t(file.size()));
    set32(file, 20, 2);
    auto entry = [&](const char *name, uint32_t off, uint32_t index, const Bytes &raw, const Bytes &packed) {
        Bytes e(16, 0);
        std::memcpy(e.data(), name, std::strlen(name));
        put32(e, off);
        put32(e, index);
        put32(e, uint32_t(raw.size()));
        put32(e, uint32_t(packed.size()));
        put32(e, 1);
        put32(e, 0x1234);
        file.insert(file.end(), e.begin(), e.end());
    };
    entry("a.tga", offA, 0, a, pa);
    entry("b.tga", offB, 1, b, pb);
    file.resize(file.size() + 100, 0); // padding, as at the retail file's end
    writeFile(tmp / "alt.res", file);

    AltResArchive alt;
    CHECK(!alt.open(tmp / "alt.res"));
    CHECK(alt.entries().size() == 2);
    CHECK(alt.list() == std::vector<std::string>({"a.tga", "b.tga"}));
    CHECK(alt.has("A.TGA") && !alt.has("c.tga"));
    CHECK(alt.entries()[1].offset == offB && alt.entries()[1].check == 0x1234);
    Bytes out;
    CHECK(!alt.read("a.tga", out) && out == a);
    CHECK(!alt.read("B.tga", out) && out == b);
    CHECK(alt.read("c.tga", out));

    // A directory that doesn't fit is refused.
    set32(file, 20, 1000);
    writeFile(tmp / "bad.res", file);
    CHECK(alt.open(tmp / "bad.res"));
}

// One mono IMA ADPCM block of `size` bytes.
void testAnimations() {
    // Three frames 30 wide and 10 high, two across: white, black, red.
    Animation a;
    CHECK(!readAnimation(makeAnimation(30, 10, {0x7FFF, 0, 0x7C00}), a));
    CHECK(a.frameWidth == 30 && a.frameHeight == 10 && a.frames.size() == 3);
    CHECK(a.frames[0].w == 30 && a.frames[0].h == 10);
    // Without an alpha bit, black isn't drawn.
    CHECK(a.frames[0].pixel(29, 9)[0] == 248 && a.frames[0].pixel(29, 9)[3] == 255);
    CHECK(a.frames[1].pixel(0, 0)[3] == 0);
    CHECK(a.frames[2].pixel(5, 5)[0] == 248 && a.frames[2].pixel(5, 5)[1] == 0);
    // With one, a pixel's top bit says it's drawn...
    CHECK(!readAnimation(makeAnimation(4, 4, {0x8000, 0x7FFF}, true), a));
    CHECK(a.frames[0].pixel(0, 0)[3] == 255 && a.frames[1].pixel(0, 0)[3] == 0);
    // ...unless no pixel has it set: then black isn't drawn, as without.
    CHECK(!readAnimation(makeAnimation(4, 4, {0, 0x7FFF}, true), a));
    CHECK(a.frames[0].pixel(0, 0)[3] == 0 && a.frames[1].pixel(0, 0)[3] == 255);
    // A header that doesn't fit its picture.
    Bytes bad = makeAnimation(4, 4, {0, 0});
    bad[4] = 9;
    CHECK(readAnimation(bad, a));
}

Bytes imaBlock(size_t size, int16_t first, uint8_t seed) {
    Bytes b;
    put16(b, uint16_t(first));
    b.push_back(10);
    b.push_back(0);
    for (size_t i = 4; i < size; ++i)
        b.push_back(uint8_t(seed + i * 37));
    return b;
}

Bytes wavFile(uint16_t format, int channels, int rate, int bits, int blockAlign, const Bytes &data) {
    Bytes w = {'R', 'I', 'F', 'F'};
    put32(w, uint32_t(36 + data.size()));
    w.insert(w.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(w, 16);
    put16(w, format);
    put16(w, uint32_t(channels));
    put32(w, uint32_t(rate));
    put32(w, uint32_t(rate * channels * 2));
    put16(w, uint32_t(blockAlign));
    put16(w, uint32_t(bits));
    w.insert(w.end(), {'d', 'a', 't', 'a'});
    put32(w, uint32_t(data.size()));
    w.insert(w.end(), data.begin(), data.end());
    return w;
}

void testSnd(const std::filesystem::path &tmp) {
    // sfx.snd: WAV files, then the directory.
    Bytes pcm;
    for (int i = 0; i < 100; ++i)
        put16(pcm, uint32_t(i * 50));
    const Bytes wav = wavFile(1, 1, 22050, 16, 2, pcm);
    Bytes sfx(4, 0);
    sfx.insert(sfx.end(), wav.begin(), wav.end());
    sfx.insert(sfx.end(), wav.begin(), wav.end());
    set32(sfx, 0, uint32_t(sfx.size()));
    for (uint32_t i = 0; i < 2; ++i) {
        put32(sfx, uint32_t(4 + i * wav.size()));
        put32(sfx, uint32_t(wav.size()));
        put32(sfx, 0x0040ca9a);
        put32(sfx, i * 2);
    }
    writeFile(tmp / "sfx.snd", sfx);
    SndArchive snd;
    CHECK(!snd.open(tmp / "sfx.snd", false));
    CHECK(snd.entries().size() == 2 && snd.entries()[1].flags == 2 && !snd.isMusic());
    audio::Sound s;
    CHECK(!snd.decode(1, 11025, s));
    CHECK(s.rate == 22050 && s.frames() == 100 && s.format == 1 && s.samples[3] == 150);
    CHECK(snd.decode(2, 22050, s));

    // english.snd: bare mono ADPCM, its length from the decoded size.
    const Bytes clip = imaBlock(512, 7, 3);
    Bytes speech(4, 0);
    speech.insert(speech.end(), clip.begin(), clip.end());
    set32(speech, 0, uint32_t(speech.size()));
    put32(speech, 4);
    put32(speech, uint32_t(clip.size()));
    put32(speech, 44 + 2 * 1000);
    put32(speech, 0);
    writeFile(tmp / "english.snd", speech);
    CHECK(!snd.open(tmp / "english.snd", false));
    CHECK(!snd.decode(0, 22050, s));
    CHECK(s.format == 0x11 && s.channels == 1 && s.rate == 22050 && s.frames() == 1000 && s.samples[0] == 7);

    // music.snd: 16 lists after the segments; two pieces of stereo blocks.
    Bytes music(128, 0);
    Bytes stereo = imaBlock(1024, 5, 4);
    stereo[4] = 9; // the right channel's header: sample 9, index 0
    stereo[5] = 0;
    stereo[6] = 0;
    stereo[7] = 0;
    const uint32_t segA = uint32_t(music.size());
    music.insert(music.end(), stereo.begin(), stereo.end());
    const uint32_t segB = uint32_t(music.size());
    music.insert(music.end(), stereo.begin(), stereo.end());
    const uint32_t lists = uint32_t(music.size());
    auto segment = [&](uint32_t off, uint32_t frames) {
        put32(music, off);
        put32(music, uint32_t(stereo.size()));
        put32(music, 44 + 4 * frames);
        put32(music, 3);
    };
    segment(segA, 1017); // piece 0: two segments
    segment(segB, 500);
    segment(segB, 1017); // piece 1: one
    set32(music, 0, 2);
    set32(music, 4, lists);
    set32(music, 8, 1);
    set32(music, 12, lists + 32);
    for (int p = 2; p < 16; ++p)
        set32(music, size_t(p) * 8 + 4, uint32_t(music.size())); // empty lists
    writeFile(tmp / "music.snd", music);
    CHECK(!snd.open(tmp / "music.snd", true));
    CHECK(snd.isMusic() && snd.entries().size() == 3 && snd.pieces().size() == 16);
    CHECK(snd.pieces()[0] == std::vector<size_t>({0, 1}) && snd.pieces()[1] == std::vector<size_t>({2}));
    CHECK(snd.pieces()[2].empty() && snd.entries()[2].piece == 1);
    CHECK(!snd.decodePiece(0, 22050, s));
    CHECK(s.channels == 2 && s.frames() == 1517 && s.samples[0] == 5 && s.samples[1] == 9);
    CHECK(s.samples[1017 * 2] == 5); // the second segment starts afresh
    CHECK(snd.decodePiece(16, 22050, s));
}

void testGameFiles(const std::filesystem::path &tmp) {
    const auto ini = parseStbofIni("CDPATH=D:\\\r\nHDPATH=C:\\botf\\\r\nlanguage = German \r\nINSTALL=MINIMUM\r\n"
                                   "DIRECT X NOT 6.0 INSTALLED BY BOTF\r\n");
    CHECK(ini.at("CDPATH") == "D:\\" && ini.at("LANGUAGE") == "German" && ini.at("INSTALL") == "MINIMUM");
    CHECK(ini.size() == 4);

    // A game folder with the CD's language folder copied in, and a CD
    // folder named by Stbof.ini.
    const auto game = tmp / "game";
    const auto cd = tmp / "cd";
    writeFile(game / "STBOF.RES", {1});
    writeFile(game / "German" / "german.snd", {2});
    writeFile(cd / "MUSIC.SND", {3});
    const std::string text = "CDPATH=" + cd.generic_string() + "\r\nLANGUAGE=GERMAN\r\n";
    writeFile(game / "Stbof.ini", Bytes(text.begin(), text.end()));
    GameFiles files(game);
    CHECK(files.language() == "GERMAN" && files.speechFile() == "german.snd");
    CHECK(files.cdDir().has_value());
    CHECK(files.find("stbof.res") == game / "STBOF.RES");
    CHECK(files.find("german.snd") == game / "German" / "german.snd");
    CHECK(files.find("music.snd") == cd / "MUSIC.SND");
    CHECK(!files.find("alt.res"));

    // Without Stbof.ini: English, no CD.
    GameFiles plain(tmp / "cd");
    CHECK(plain.language() == "ENGLISH" && plain.speechFile() == "english.snd" && !plain.cdDir());
    CHECK(plain.find("music.snd").has_value());
}

void testStbofIniWritten() {
    // As the game left it after a game: setting the values it holds changes
    // nothing, byte for byte.
    const std::string played =
        "CDPATH=D:\\\r\nHDPATH=c:\\games\\botf\\\r\nCURRENTGAME=0\r\nLANGUAGE=ENGLISH\r\n"
        "VICTORY=KILLALL\r\nINSTALL=MAXIMUM\r\n3D=SOFTWARE\r\nTACTICAL=ABSTRACT\r\nSOUNDON=ON\r\n"
        "BORG=ON\r\nMOVIEON=ON\r\nAUTOSAVE=ON\r\nTOOLTIPS=ON\r\nRANDOMEVENTS=OFF\r\n"
        "TURNSTATUS=OFF\r\nTIMELIMIT=0\r\nTACTIMELIMIT=0\r\nSHAPE=1\r\nSIZE=0\r\nRACENUM=0\r\n"
        "DIFFICULTY=1\r\nCIVLEVEL1=0\r\nMUSICVOLUME=100\r\nFXVOLUME=100\r\nVIDEOVOLUME=127\r\n"
        "VOICEVOLUME=100\r\nDIRECT X NOT INSTALLED BY BOF\r\nVersion=1.0.2\r\n";
    CHECK(updateStbofIni(played, parseStbofIni(played)) == played);
    // A value changed keeps its line and place.
    const std::string changed = updateStbofIni(played, {{"MUSICVOLUME", "64"}, {"TOOLTIPS", "OFF"}});
    CHECK(changed.find("TOOLTIPS=OFF\r\nRANDOMEVENTS") != std::string::npos &&
          changed.find("\r\nMUSICVOLUME=64\r\nFXVOLUME") != std::string::npos && changed.size() == played.size());

    // As the installer writes it: the keys it hasn't got go where the game
    // puts them, after the last one before them in its order; the rest,
    // lines without '=' and the ^Z at the end, stay.
    const std::string fresh = "CDPATH=D:\\\r\nLANGUAGE=ENGLISH\r\nMOVIEON=ON\r\nMUSICVOLUME=100\r\nMONO=YES\r\n"
                              "INSTALL=MINIMUM\r\nDIRECT X NOT 6.0 INSTALLED BY BOTF\r\n\x1a";
    const std::string updated = updateStbofIni(fresh, {{"MUSICVOLUME", "90"}, {"AUTOSAVE", "OFF"}, {"CIVLEVEL2", "3"}});
    // (INSTALL comes before both new keys in the game's order.)
    CHECK(updated == "CDPATH=D:\\\r\nLANGUAGE=ENGLISH\r\nMOVIEON=ON\r\nMUSICVOLUME=90\r\nMONO=YES\r\n"
                     "INSTALL=MINIMUM\r\nAUTOSAVE=OFF\r\nCIVLEVEL2=3\r\nDIRECT X NOT 6.0 INSTALLED BY BOTF\r\n\x1a");
    // An empty file gets the keys, in the game's order, with \r\n.
    CHECK(updateStbofIni("", {{"SIZE", "1"}, {"SHAPE", "2"}}) == "SHAPE=2\r\nSIZE=1\r\n");
}

} // namespace

static const reluva::test::Suite kSuite(
    "botf.unit.archives",
    {
        {"lzss", [](const std::filesystem::path &) { testLzss(); }},
        {"alt.res", [](const std::filesystem::path &tmp) { testAltRes(tmp); }},
        {"saved games", [](const std::filesystem::path &tmp) { testSaveSummary(tmp); }},
        {"saved galaxy", [](const std::filesystem::path &tmp) { testSavedGalaxy(tmp); }},
        {"saved ships", [](const std::filesystem::path &tmp) { testSavedShips(tmp); }},
        {"animations", [](const std::filesystem::path &) { testAnimations(); }},
        {"saved results", [](const std::filesystem::path &tmp) { testSavedResults(tmp); }},
        {"saved orders", [](const std::filesystem::path &tmp) { testSavedOrders(tmp); }},
        {"saved empires, fleets and research", [](const std::filesystem::path &tmp) { testSavedTaskForces(tmp); }},
        {"snd", [](const std::filesystem::path &tmp) { testSnd(tmp); }},
        {"game files", [](const std::filesystem::path &tmp) { testGameFiles(tmp); }},
        {"Stbof.ini written", [](const std::filesystem::path &) { testStbofIniWritten(); }},
    });
