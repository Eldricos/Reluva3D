// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Birth of the Federation's unit tests: the .tdb text tables, the data tables and
// lexicon.dic.

#include "engines/botf/formats/data_table.h"
#include "engines/botf/formats/lexicon.h"
#include "engines/botf/formats/text_table.h"

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

// A .tdb as docs/engines/botf/formats/tdb.md describes: count, a leftover value,
// the longest length, 0; per string key, length, a leftover value, offset;
// the strings, stored in another order than the records.
Bytes makeTable(const std::vector<std::pair<uint32_t, std::string>> &strings) {
    Bytes text;
    std::vector<uint32_t> offsets(strings.size());
    for (size_t i = strings.size(); i-- > 0;) { // last string first
        offsets[i] = uint32_t(text.size());
        text.insert(text.end(), strings[i].second.begin(), strings[i].second.end());
        text.push_back(0);
    }
    Bytes f;
    uint32_t longest = 0;
    for (const auto &s : strings)
        longest = std::max(longest, uint32_t(s.second.size() + 1));
    put32(f, uint32_t(strings.size()));
    put32(f, 0x005a0628);
    put32(f, longest);
    put32(f, 0);
    for (size_t i = 0; i < strings.size(); ++i) {
        put32(f, strings[i].first);
        put32(f, uint32_t(strings[i].second.size() + 1));
        put32(f, 0x005a0a02 + uint32_t(i));
        put32(f, offsets[i]);
    }
    f.insert(f.end(), text.begin(), text.end());
    return f;
}

void testTextTable() {
    TextTable table;
    const Bytes data = makeTable({{0x00410008, " Greetings from %1.\n"},
                                  {0x00481307, " %s1 and %2 have formed an alliance.\n"},
                                  {0x00410018, " Gr\xf6\xdf"
                                               "e \x85\n"}});
    CHECK(!table.load(data));
    CHECK(table.entries().size() == 3 && table.longest() == 38);
    CHECK(table.entries()[0].key == 0x00410008 && table.entries()[0].text == " Greetings from %1.\n");
    CHECK(table.find(0x00481307) == &table.entries()[1]);
    CHECK(table.find(0x00410018)->text == " Gr\xc3\xb6\xc3\x9f"
                                          "e \xe2\x80\xa6\n"); // Windows-1252
    CHECK(!table.find(1));
    CHECK(displayText(table.entries()[0].text) == "Greetings from %1.");
    CHECK(displayText("") == "" && displayText(" \n") == "");

    // Damaged tables fail: a count too large, a string past the end, a
    // string without its zero byte.
    Bytes bad = data;
    set32(bad, 0, 1000);
    CHECK(table.load(bad));
    bad = data;
    set32(bad, 16 + 12, 5000);
    CHECK(table.load(bad));
    bad = data;
    bad.pop_back();
    CHECK(table.load(bad));
    CHECK(table.load(Bytes(8, 0)));
    CHECK(!table.load(makeTable({})) && table.entries().empty());
}

// A data table laid out as TableLayout describes: here a u16 count, then
// 12-byte records (a name, a U8 pair, a U32 description offset), and a
// companion file of descriptions.
void testDataTable() {
    using F = FieldType;
    const TableLayout layout = {"test.tbl",
                                "testdesc.tbl",
                                TableLayout::Count::kU16AtStart,
                                2,
                                12,
                                {{"name", 0, F::kText, 6}, {"pair", 6, F::kU8, 2}, {"description", 8, F::kU32}},
                                0,
                                2};
    Bytes data = {2, 0};
    auto record = [&](const char *name, uint8_t a, uint8_t b, uint32_t desc) {
        Bytes r(6, 0xcc); // leftover bytes after the name's zero
        std::memcpy(r.data(), name, std::strlen(name) + 1);
        r.push_back(a);
        r.push_back(b);
        put32(r, desc);
        data.insert(data.end(), r.begin(), r.end());
    };
    record("Farm", 1, 2, 6);
    record("Gr\xf6\xdf", 30, 40, 0);
    const std::string descs("Large\0Small\0", 12);
    DataTable table;
    CHECK(!table.load(layout, data, Bytes(descs.begin(), descs.end())));
    CHECK(table.count() == 2);
    CHECK(table.name(0) == "Farm" && table.name(1) == "Gr\xc3\xb6\xc3\x9f");
    CHECK(table.value(0, layout.fields[1]) == "1 2" && table.value(1, layout.fields[2]) == "0");
    CHECK(table.description(0) == "Small" && table.description(1) == "Large");
    CHECK(table.unknownRanges().empty());

    // Without descriptions, and with fields that leave gaps.
    const TableLayout gaps = {
        "gaps.tbl",
        nullptr,
        TableLayout::Count::kFileSize,
        0,
        8,
        {{"value", 2, F::kU16}, {"real", 4, F::kF32}, {"pair", 2, F::kU16, 3}, {"words", 0, F::kU32, 2}},
        0,
        -1};
    Bytes two(16, 0);
    two[2] = 0x34;
    two[3] = 0x12;
    const float half = 0.5f;
    std::memcpy(&two[4], &half, 4);
    CHECK(!table.load(gaps, two));
    CHECK(table.count() == 2 && table.value(0, gaps.fields[0]) == "4660" && table.value(0, gaps.fields[1]) == "0.5");
    CHECK(table.value(0, gaps.fields[2]) == "4660 0 16128");         // a U16 array: 0x1234, then the float's bytes
    CHECK(table.value(0, gaps.fields[3]) == "305397760 1056964608"); // 0x12340000, 0x3f000000
    CHECK(table.description(0).empty());
    CHECK(table.unknownRanges().empty()); // "words" covers bytes 0-7
    const TableLayout signedPair = {"gaps.tbl", nullptr, TableLayout::Count::kFileSize, 0, 8, {{"pair", 0, F::kS16, 2}},
                                    0,          -1};
    Bytes negative = {0xff, 0xff, 0x07, 0x00, 0, 0, 0, 0};
    CHECK(!table.load(signedPair, negative));
    CHECK(table.value(0, signedPair.fields[0]) == "-1 7");
    const TableLayout signedWord = {"gaps.tbl", nullptr, TableLayout::Count::kFileSize, 0, 8, {{"word", 0, F::kS32}},
                                    -1,         -1};
    CHECK(!table.load(signedWord, Bytes{0xfe, 0xff, 0xff, 0xff, 0, 0, 0, 0}));
    CHECK(table.value(0, signedWord.fields[0]) == "-2");
    CHECK(table.name(0) == "0"); // no name field: the record's number

    // Record names from a fixed list, or from the header (.wtf), as 4-byte strings at 2.
    const TableLayout listed = {"gaps.tbl", nullptr, TableLayout::Count::kFileSize, 0, 4, {}, -1, -1, {"First"}};
    CHECK(!table.load(listed, Bytes(8, 0)));
    CHECK(table.name(0) == "First" && table.name(1) == "1");
    const TableLayout headed = {"gaps.tbl", nullptr, TableLayout::Count::kFileSize, 10, 4, {}, -1, -1, {}, 2, 4};
    Bytes withNames = {9, 9, 'A', 'b', 0, 0, 'C', 'd', 'e', 'f', 1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(!table.load(headed, withNames));
    CHECK(table.count() == 2 && table.name(0) == "Ab" && table.name(1) == "Cdef");
    const TableLayout partial = {"gaps.tbl", nullptr, TableLayout::Count::kFileSize, 0, 8, {{"value", 2, F::kU16}},
                                 0,          -1};
    CHECK(!table.load(partial, two));
    CHECK(table.unknownRanges() == (std::vector<std::pair<uint32_t, uint32_t>>{{0, 2}, {4, 8}}));

    // Counts that don't fit, and sizes that aren't whole records, fail.
    data[0] = 3;
    CHECK(table.load(layout, data));
    CHECK(table.count() == 0);
    CHECK(table.load(gaps, Bytes(12, 0)));

    // Every built-in layout's fields fit inside its records.
    for (const TableLayout &l : tableLayouts())
        for (const Field &f : l.fields)
            CHECK(f.offset + (f.type == F::kText || f.type == F::kU8   ? f.size
                              : f.type == F::kF64                      ? 8u
                              : f.type == F::kU16 || f.type == F::kS16 ? 2u * f.size
                              : f.type == F::kU32 || f.type == F::kS32 ? 4u * f.size
                                                                       : 4u) <=
                  l.recordSize);
}

void testLexicon() {
    Lexicon lex;
    CHECK(!lex.load(makeLexicon({"ADVANCED", "Gro\xdf", ""})));
    CHECK(lex.size() == 3 && lex[0] == "ADVANCED" && lex[1] == "Gro\xc3\x9f" && lex[2].empty() && lex[99].empty());
    Bytes bad = makeLexicon({"a"});
    bad[0] = 0xff; // the table past the end
    CHECK(lex.load(bad));
    CHECK(lex.load(Bytes(2, 0)));
}

} // namespace

static const reluva::test::Suite kSuite("botf.unit.tables",
                                        {
                                            {"text tables", [](const std::filesystem::path &) { testTextTable(); }},
                                            {"data tables", [](const std::filesystem::path &) { testDataTable(); }},
                                            {"lexicon", [](const std::filesystem::path &) { testLexicon(); }},
                                        });
