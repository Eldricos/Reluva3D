// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/dictionary.h"
#include "tests/support/test.h"
#include "tests/support/pe_resources_fixture.h"
#include <algorithm>

using namespace reluva;
using namespace reluva::boggle;
namespace {
void u32(std::vector<uint8_t>& out, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(uint8_t(n >> (8 * i)));
}
std::vector<uint8_t> sample() {
    // Entirely invented tree: A, BE, BAR, BOX, CAT, plus a duplicate BAR.
    std::vector<uint8_t> out;
    u32(out, 0x80000001); u32(out, (1u << 1) | (1u << 2));
    u32(out, 1u << 4); u32(out, (1u << 0) | (1u << 14));
    const std::string suffixes("OX\0AR\0AR\0\0", 10);
    u32(out, uint32_t(suffixes.size())); out.insert(out.end(), suffixes.begin(), suffixes.end());
    u32(out, 0x80000000); u32(out, 1);
    u32(out, 1u << 19); u32(out, 0);
    return out;
}
void lookup() {
    Dictionary d;
    CHECK(!d.read(sample()));
    CHECK(d.entries() == 6 && d.words().size() == 5);
    for (auto word : {"A", "BAR", "BE", "BOX", "CAT"}) CHECK(d.contains(word));
    for (auto word : {"", "B", "CA", "CATS", "bar", "BAT", "ZA"}) CHECK(!d.contains(word));
    for (auto prefix : {"", "B", "BA", "BAR", "C", "CA"}) CHECK(d.hasPrefix(prefix));
    CHECK(!d.hasPrefix("CB")); CHECK(!d.hasPrefix("CATX"));
    std::vector<uint8_t> empty(8);
    CHECK(!d.read(empty)); CHECK(d.words().empty()); CHECK(!d.hasPrefix(""));
}
void encoding() {
    std::vector<uint8_t> data;
    u32(data, 1u << 26); u32(data, 1);
    u32(data, 4); data.insert(data.end(), {'A', uint8_t(0xf1), 0, 0});
    Dictionary d; CHECK(!d.read(data));
    CHECK(d.contains(std::string(1, char(0xd1))));
    CHECK(d.contains(std::string("A") + char(0xf1)));
    CHECK(!d.contains(std::string("A") + char(0xd1)));
}
void malformed() {
    Dictionary d; const auto good = sample();
    for (size_t size = 0; size < good.size(); ++size) {
        auto data = good; data.resize(size);
        CHECK(bool(d.read(data))); CHECK(d.words().empty() && d.entries() == 0);
    }
    for (auto [at, value] : std::vector<std::pair<size_t, uint32_t>>{
             {0, 0x10000000}, {4, 0xffffffff}, {16, 0xffffffff}, {4, 0}, {16, 3}}) {
        auto data = good; test::fixtures::put32(data, at, value);
        CHECK(bool(d.read(data))); CHECK(d.words().empty());
    }
    auto data = good; data.push_back(0); CHECK(bool(d.read(data)));
    data = good; data[20] = '1'; CHECK(bool(d.read(data)));
    data = good; data[29] = 'A'; CHECK(bool(d.read(data)));
    // Deep tree rejected before unbounded recursion/word growth.
    data.clear(); for (size_t i = 0; i < 70; ++i) { u32(data, 0x80000000); u32(data, 1); }
    u32(data, 1); u32(data, 0); CHECK(bool(d.read(data)));
}
}
static const test::Suite suite("boggle.unit.dictionary", {
    {"terminal, branch, unsorted leaf and duplicate words", [](const auto&) { lookup(); }},
    {"serialized legacy bytes are preserved", [](const auto&) { encoding(); }},
    {"truncations, masks, lengths and depth limits", [](const auto&) { malformed(); }},
});
