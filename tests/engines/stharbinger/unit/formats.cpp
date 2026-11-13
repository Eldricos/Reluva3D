// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The game's file formats: archives, scripts and name lists, animations,
// raw sound, the folders of a disc, dialogue records, fonts and lip-sync. Ported from STDS9H's tests.
//
//   reluva-tests stharbinger.unit.formats

#include "engines/stharbinger/formats/archive.h"
#include "engines/stharbinger/formats/audio.h"
#include "engines/stharbinger/formats/dialogue.h"
#include "engines/stharbinger/formats/font.h"
#include "engines/stharbinger/formats/graphics.h"
#include "engines/stharbinger/formats/resources.h"
#include "engines/stharbinger/formats/script.h"
#include "engines/stharbinger/game/game.h"
#include "tests/common/data/smacker_videos.h"
#include "tests/engines/stharbinger/fixtures.h"
#include "tests/support/test.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

using namespace reluva;
using namespace reluva::stharbinger;
using namespace reluva::stharbinger::testing;

static void test_lst_archive_by_index_and_name(const std::filesystem::path & /*tmp*/) {
    std::vector<uint8_t> all = {'a', 'b', 'c', 'd', 'e'};
    std::vector<uint8_t> lst;
    putU32(lst, 0);
    putU32(lst, 2);
    putU32(lst, 5);
    std::string catText = "2\r\nCC.ALL\r\ncc.lst\r\n\x1a";
    std::vector<uint8_t> cat(catText.begin(), catText.end());
    auto archive = LstArchive::create(all, lst, cat);
    CHECK(archive != nullptr);
    CHECK((archive->size()) == (2u));
    CHECK((archive->entrySize(1)) == (3u));
    CHECK(archive->getName(1) == "cc.lst");
    CHECK(archive->has("CC.LST"));
    auto s = archive->openMember("cc.all");
    CHECK(s && s->size() == 2 && s->readByte() == 'a');

    // Without the end sentinel the size of the .ALL closes the last entry.
    lst.resize(8);
    auto noSentinel = LstArchive::create(all, lst);
    CHECK(noSentinel && noSentinel->size() == 2 && noSentinel->entrySize(1) == 3);

    std::vector<uint8_t> badLst;
    putU32(badLst, 4);
    putU32(badLst, 2);
    CHECK(LstArchive::create(all, badLst) == nullptr);
}

static void test_directive_list(const std::filesystem::path & /*tmp*/) {
    // SCENE #7; BACKGRND @1, b0; LABEL @0; then an unused slot (type 00).
    const uint8_t data[] = {
        0x03, 0x00, 0x68, 0x40, 0x07, 0x00, 0x69, 0x02, 0x01, 0x00, 0x01, 0x00, 0xC8, 0x00,
    };
    uint32_t pos = 0;
    DirectiveList list;
    std::string err;
    CHECK(readDirectiveList(data, sizeof(data), pos, list, &err));
    CHECK((pos) == (uint32_t(sizeof(data))));
    CHECK((list.size()) == (size_t(3)));
    CHECK((list[0].command) == (kCmdScene));
    CHECK((list[0].args[0].value) == (7u));
    CHECK((list[1].getArg(0)->type) == (kArgSymbol));
    CHECK((list[1].getArg(1)->value) == (0u));
    CHECK(list[2].args.empty());
    CHECK(getCommandName(0x69) == "BACKGRND");

    const uint8_t bad[] = {0x01, 0x00, 0x10};
    pos = 0;
    CHECK(!readDirectiveList(bad, sizeof(bad), pos, list, &err));
    CHECK(err.find("illegal command 10") != std::string::npos);
}

static void test_name_and_xref_lists(const std::filesystem::path & /*tmp*/) {
    const char names[] = "TLBACK\0BRIDGE.CCG\0\x01"
                         "X\0Y.CCG\0\x00";
    auto list = readNameList(reinterpret_cast<const uint8_t *>(names), sizeof(names) - 1);
    CHECK((list.size()) == (size_t(2)));
    CHECK(list[0].name == "TLBACK" && list[0].source == "BRIDGE.CCG" && list[0].flag == 1);
    const char xref[] = "door\0\x72\x05";
    auto x = readXrefList(reinterpret_cast<const uint8_t *>(xref), sizeof(xref) - 1);
    CHECK(x.size() == 1 && x[0].symbol == "door" && x[0].command == 0x72 && x[0].slot == 5);
    CHECK(getResourcePath(kListBackgrounds, "brdg0000") == "ART/B/R/BRDG0000.CMP");
    CHECK(getResourcePath(kListAnims, "chdrk1") == "ANIMS/CHDR/CHDRK1.RLE");
}

static void test_rle_decode(const std::filesystem::path & /*tmp*/) {
    std::vector<uint8_t> file = {0x00, 0x00, 0x01, 0x01, 0x0A, 0xFF, 0x00, 0x00, 0x01, 0x00};
    // Frame 3x2 at (1, 1): row 0 = literal 5 6, row 1 = skip 1, run 2 x 9.
    std::vector<uint8_t> body = {0x40, 0x02, 5, 6, 0x41, 0x82, 9};
    std::vector<uint8_t> hdr(25, 0);
    hdr[1] = 3;
    hdr[3] = 2;
    hdr[5] = 1;
    hdr[7] = 1;
    hdr[21] = uint8_t(body.size());
    file.insert(file.end(), hdr.begin(), hdr.end());
    file.insert(file.end(), body.begin(), body.end());

    RleAnimation anim;
    CHECK(anim.load(file.data(), uint32_t(file.size())));
    CHECK((anim.frameCount()) == (1));
    Surface s;
    draw::create(s, 5, 4, 1);
    anim.drawFrame(0, s);
    CHECK((*draw::at(s, 1, 1)) == (5));
    CHECK((*draw::at(s, 2, 1)) == (6));
    CHECK((*draw::at(s, 3, 1)) == (1)); // not written
    CHECK((*draw::at(s, 1, 2)) == (1)); // skipped
    CHECK((*draw::at(s, 2, 2)) == (9));
    CHECK((*draw::at(s, 3, 2)) == (9));
    CHECK((*draw::at(s, 4, 2)) == (1)); // past the frame width: clipped
}

static void test_raw_format_detection(const std::filesystem::path & /*tmp*/) {
    std::vector<uint8_t> s16;
    std::vector<uint8_t> u8;
    std::vector<uint8_t> zeroLow;
    for (int i = 0; i < 20000; i++) {
        int v = int(8000 * std::sin(i * 0.05)) + (i * 7919 % 97);
        s16.push_back(uint8_t(v & 0xFF));
        s16.push_back(uint8_t((v >> 8) & 0xFF));
        u8.push_back(uint8_t(128 + int(60 * std::sin(i * 0.05))));
        zeroLow.push_back(0);
        zeroLow.push_back(uint8_t(int(60 * std::sin(i * 0.05))));
    }
    CHECK((detectRawFormat(s16)) == (kRawS16LE));
    CHECK((detectRawFormat(u8)) == (kRawU8));
    CHECK((detectRawFormat(zeroLow)) == (kRawS16LE));
    std::vector<uint8_t> be(s16);
    for (size_t i = 0; i + 1 < be.size(); i += 2)
        std::swap(be[i], be[i + 1]);
    CHECK((detectRawFormat(be)) == (kRawS16BE));
}

static void test_resources_disc_layout(const std::filesystem::path &tmp) {
    // Disc 1 as on the real CD: ART/ART.ALL at the top (the installer's), the
    // data under DS9/, the scripts inside SSE/SSE.ALL. Lower-case names, as
    // some Linux CD mounts show them.
    namespace fs = std::filesystem;
    fs::path root = tmp / "disc";
    putFile(root / "art" / "art.all", bytes("x"));
    putFile(root / "art" / "art.lst", {0, 0, 0, 0, 1, 0, 0, 0});
    putFile(root / "ds9" / "sound" / "beep.raw", bytes("abcd"));
    putFile(root / "ds9" / "palettes" / "x.pal", std::vector<uint8_t>(768, 0));
    // SSE.ALL holding CC.ALL ("ab") and CC.LST.
    putFile(root / "sse" / "sse.all", {'a', 'b', 0, 0, 0, 0, 2, 0, 0, 0});
    putFile(root / "sse" / "sse.lst", {0, 0, 0, 0, 2, 0, 0, 0, 10, 0, 0, 0});
    putFile(root / "sse" / "sse.cat", bytes("2\r\nCC.ALL\r\nCC.LST\r\n"));

    Resources res;
    res.addGamePath(root.string());
    CHECK(res.hasFile("SOUND/BEEP.RAW"));
    CHECK(res.hasFile("PALETTES/X.PAL"));
    CHECK(res.hasFile("CC.ALL"));
    auto cc = res.getArchive("CC");
    CHECK(cc && cc->size() == 1 && cc->entrySize(0) == 2);

    // The DS9 directory given directly.
    Resources ds9;
    ds9.addGamePath((root / "ds9").string());
    CHECK(ds9.hasFile("SOUND/BEEP.RAW"));
}

static void test_dialogue_record(const std::filesystem::path & /*tmp*/) {
    ScriptBuilder a;
    ScriptBuilder b;
    ScriptBuilder npc;
    ScriptBuilder pick;
    ScriptBuilder sub;
    a.cmd(kCmdNop);
    npc.cmd(kCmdLet, {num(1), var(0)});
    pick.cmd(0x39); // EXIT
    std::vector<uint8_t> data;
    auto append = [&](const std::vector<uint8_t> &v) { data.insert(data.end(), v.begin(), v.end()); };
    auto u16 = [&](uint16_t v) {
        data.push_back(uint8_t(v));
        data.push_back(uint8_t(v >> 8));
    };
    append(a.build());
    append(b.build());
    // NPC tree: 1 non-empty group, preceded by an empty one that does not count.
    data.push_back(1);
    data.push_back(0);
    data.push_back(1);
    u16(2);
    u16(0xFFFF);
    append(npc.build());
    data.push_back(1); // one sub-item
    u16(5);
    u16(6);
    append(sub.build());
    // Choice tree: 1 group with 2 items.
    data.push_back(1);
    data.push_back(2);
    for (uint16_t line : {3, 4}) {
        u16(line);
        u16(uint16_t(line - 3));
        append(pick.build());
        data.push_back(0);
    }

    Dialogue d;
    std::string err;
    CHECK(readDialogue(data.data(), uint32_t(data.size()), d, &err));
    CHECK((d.listA.size()) == (size_t(1)));
    CHECK((d.npcLines.size()) == (size_t(2)));
    CHECK(d.npcLines[0].empty());
    CHECK((d.npcLines[1][0].line) == (2));
    CHECK((d.npcLines[1][0].subItems.size()) == (size_t(1)));
    CHECK((d.npcLines[1][0].subItems[0].b) == (6));
    CHECK((d.choices.size()) == (size_t(1)));
    CHECK((d.choices[0].size()) == (size_t(2)));
    CHECK((d.choices[0][1].choice) == (1));
    data.pop_back();
    CHECK(!readDialogue(data.data(), uint32_t(data.size()), d, &err));

    const char text[] = "Hello {note}there\0 [Ops] Go to {x}Ops.\0";
    auto lines = splitStrings(reinterpret_cast<const uint8_t *>(text), sizeof(text) - 1, '\0');
    CHECK((lines.size()) == (size_t(2)));
    d.text = lines;
    CHECK(d.getSpokenText(0) == "Hello there");
    CHECK(d.getMenuLabel(1) == "Ops");
    CHECK(d.getSpokenText(1) == "Go to Ops.");
    const char clips[] = "abcpro00\r\nabcdax01\r\n";
    auto names = splitStrings(reinterpret_cast<const uint8_t *>(clips), sizeof(clips) - 1, '\n');
    CHECK(names.size() == 2 && names[1] == "abcdax01");
    CHECK((findDialogue("com010")) == (0));
    CHECK(std::string(getDialogueName(71)) == "YAR020");
}

static void test_font_and_phonemes(const std::filesystem::path & /*tmp*/) {
    // Two glyphs: space (3x2, blank, advance 3) and '!' (1 wide, 2 high, both rows set).
    std::vector<uint8_t> fnt = {2, 0, 2, 3, 2, 0, 0, 2, 1, 2, 0x80, 0x80};
    MemoryReadStream stream(fnt);
    Font font;
    CHECK(font.load(stream));
    CHECK((font.getLineHeight()) == (2));
    CHECK((font.getStringWidth(" !")) == (4));
    Surface s;
    draw::create(s, 6, 4);
    font.drawString(s, 0, 0, " !", 9, 7);
    CHECK((*draw::at(s, 3, 0)) == (9));
    CHECK((*draw::at(s, 3, 1)) == (9));
    CHECK((*draw::at(s, 4, 1)) == (7)); // shadow right
    CHECK((*draw::at(s, 3, 2)) == (7)); // shadow below
    CHECK((*draw::at(s, 2, 0)) == (0));

    // 2205 samples per unit = 100 ms; nodes: code 3 for 32/64 units, code 5 for 0 (one unit).
    std::vector<uint8_t> ph = {0x9D, 0x08, 0, 0, 3, 32, 'h', 'i', 0, 5, 0, 0, 0xFF};
    Phonemes p;
    CHECK(readPhonemes(ph.data(), uint32_t(ph.size()), p));
    CHECK((p.unitMillis) == (100u));
    CHECK((p.nodes.size()) == (size_t(2)));
    CHECK((p.getNodeMillis(0)) == (50u));
    CHECK((p.getNodeMillis(1)) == (100u));
}

static const reluva::test::Suite
    suite("stharbinger.unit.formats",
          {
              {"lst archive by index and name",
               [](const std::filesystem::path &tmp) { test_lst_archive_by_index_and_name(tmp); }},
              {"directive list", [](const std::filesystem::path &tmp) { test_directive_list(tmp); }},
              {"name and xref lists", [](const std::filesystem::path &tmp) { test_name_and_xref_lists(tmp); }},
              {"rle decode", [](const std::filesystem::path &tmp) { test_rle_decode(tmp); }},
              {"raw format detection", [](const std::filesystem::path &tmp) { test_raw_format_detection(tmp); }},
              {"resources disc layout", [](const std::filesystem::path &tmp) { test_resources_disc_layout(tmp); }},
              {"dialogue record", [](const std::filesystem::path &tmp) { test_dialogue_record(tmp); }},
              {"font and phonemes", [](const std::filesystem::path &tmp) { test_font_and_phonemes(tmp); }},
          });
