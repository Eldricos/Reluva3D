// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island's unit tests: the worlds' *INF.DTA files
// (docs/engines/legoisland/formats/dta.md), from one built here.

#include "common/stream.h"
#include "engines/legoisland/formats/dta.h"

#include "tests/support/test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::legoisland;

namespace {

using Bytes = std::vector<uint8_t>;

void put16(Bytes &b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

void put32(Bytes &b, uint32_t v) {
    put16(b, uint16_t(v));
    put16(b, uint16_t(v >> 16));
}

void putF(Bytes &b, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    put32(b, bits);
}

void putName(Bytes &b, const std::string &s) {
    b.push_back(uint8_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

void putModel(Bytes &b, const std::string &name, uint8_t kind, float x, uint8_t flag) {
    putName(b, name);
    b.push_back(kind);
    for (float f : {x, 1.0f, -2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f})
        putF(b, f);
    b.push_back(flag);
}

// Two animations: one on the island with two models, one with none.
Bytes buildDta() {
    Bytes b;
    put32(b, 3);
    put16(b, 2);
    putName(b, "sjs012in");
    put32(b, 500);
    put16(b, 25);
    for (uint8_t f : {0, 1, 0x1f, 1})
        b.push_back(f);
    for (float f : {11.9f, -2.3f, -58.5f, 11.0f})
        putF(b, f);
    b.push_back(2);
    putModel(b, "infoman", 2, 13.0f, 1);
    putModel(b, "*capdb01", 3, 12.5f, 0);
    putName(b, "igs001d3");
    put32(b, 501);
    put16(b, 0xFFFF);
    for (uint8_t f : {1, 0, 4, 0})
        b.push_back(f);
    for (int i = 0; i < 4; i++)
        putF(b, 0);
    b.push_back(0);
    return b;
}

void testRead(const std::filesystem::path &tmp) {
    CHECK(writeFile(tmp / "TESTINF.DTA", buildDta()));
    DtaFile dta;
    CHECK(!dta.read(tmp / "TESTINF.DTA"));
    CHECK(dta.version == 3 && dta.animations.size() == 2);
    if (dta.animations.size() != 2)
        return;
    const DtaAnimation &a = dta.animations[0];
    CHECK(a.name == "sjs012in" && a.id == 500 && a.spot == 25 && a.flags[2] == 0x1f && a.flags[3] == 1);
    CHECK(a.center[2] == -58.5f && a.radius == 11.0f && a.models.size() == 2);
    if (a.models.size() == 2) {
        CHECK(a.models[0].name == "infoman" && a.models[0].kind == uint8_t(DtaModelKind::kCharacter));
        CHECK(a.models[0].location[0] == 13.0f && a.models[0].direction[2] == 1.0f && a.models[0].up[1] == 1.0f);
        CHECK(a.models[1].name == "*capdb01" && a.models[1].kind == uint8_t(DtaModelKind::kCopy) && !a.models[1].flag);
    }
    const DtaAnimation &b = dta.animations[1];
    CHECK(b.id == 501 && b.spot == 0xFFFF && b.models.empty() && b.radius == 0);
}

void testBroken(const std::filesystem::path &) {
    const Bytes good = buildDta();
    DtaFile dta;
    CHECK(!dta.parse(good));
    // Cut short, too long, and another version.
    CHECK(bool(dta.parse(Bytes(good.begin(), good.end() - 3))));
    Bytes longer = good;
    longer.push_back(0);
    CHECK(bool(dta.parse(longer)));
    Bytes other = good;
    other[0] = 2;
    CHECK(bool(dta.parse(other)));
    // The two worlds with none: a version and a count of 0.
    CHECK(!dta.parse(Bytes{3, 0, 0, 0, 0, 0}) && dta.animations.empty());
}

} // namespace

static const reluva::test::Suite kSuite("legoisland.unit.dta",
                                        {
                                            {"read", [](const std::filesystem::path &t) { testRead(t); }},
                                            {"broken", [](const std::filesystem::path &t) { testBroken(t); }},
                                        });
