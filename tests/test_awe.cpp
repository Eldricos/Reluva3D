// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the AWE engine's readers, on small files built here (no game
// data is ever committed).

#include "engines/awe/ac2.h"
#include "engines/awe/gr2.h"
#include "engines/awe/model.h"
#include "engines/awe/tables.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::awe;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

struct Bytes {
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
    void tag(const char* t) { u32(uint32_t(t[0]) << 24 | uint32_t(t[1]) << 16 | uint32_t(t[2]) << 8 | uint32_t(t[3])); }
    // UTF-16 in a fixed buffer, the rest filled as the game's files are.
    void text(const std::string& s, size_t chars) {
        for (char c : s)
            u16(uint8_t(c));
        u16(0);
        for (size_t i = s.size() + 1; i < chars; i++)
            d.push_back(0xcd), d.push_back(0xcd);
    }
    void put32(size_t at, uint32_t v) {
        for (int i = 0; i < 4; i++)
            d[at + size_t(i)] = uint8_t(v >> (8 * i));
    }
};

std::vector<uint8_t> utf16File(const std::string& s) {
    Bytes b;
    b.d = {0xff, 0xfe};
    for (char c : s)
        b.u16(uint8_t(c));
    return b.d;
}

void testText() {
    CHECK(decodeText(utf16File("Act\tWhat")) == "Act\tWhat");
    CHECK(decodeText({'c', 'a', 'f', 0xe9}) == "caf\xc3\xa9");

    const Table t = parseTable(decodeText(utf16File(
        "Act\tPreLoad\tLocation to Load\r\n"
        "01A\t\"fade out, movie teaser\"\tsirkeci.market\r\n"
        "\t\t\r\n"
        "02A\t\"say \"\"hello\"\"\"\trestaurant\r\n")));
    CHECK(t.headings.size() == 3);
    CHECK(t.rows.size() == 2); // the empty row is left out
    CHECK(t.column("location to load") == 2);
    CHECK(t.cell(0, 1) == "fade out, movie teaser");
    CHECK(t.cell(1, 1) == "say \"hello\"");
    CHECK(t.cell(1, 7).empty());
    CHECK(t.find("02a") == 1);

    const std::vector<Command> c = parseCommands("scene sirkeci.market_01 1 0, fade in,talk dia_x a+");
    CHECK(c.size() == 3);
    CHECK(c[0].words.size() == 4 && c[0].verb() == "scene" && c[0].words[3] == "0");
    CHECK(c[1].words.size() == 2 && c[1].words[1] == "in");
    CHECK(c[2].words.size() == 3 && c[2].words[2] == "a+");
    CHECK(parseCommands("0").empty());
    CHECK(parseCommands("look left\r\nwait 1.6\r\n").size() == 2);
}

void testAc2() {
    Bytes b;
    b.tag("SCEN");
    b.text("Locations\\Test\\Test_01.ac2", 256);
    b.tag("LOAD");
    b.u16(1);
    b.tag("2DOB");
    // Four fields of the common ones: file, name and two numbers.
    b.u16(4);
    b.text("locations\\test\\backgrounds\\test_01.jpg", 256);
    b.text("", 256);
    b.u32(0);
    b.u32(1);
    b.tag("SPRT");
    b.u16(2);
    b.u16(0);
    b.f32(1000);
    b.tag("SPOT");
    // Common fields up to the hotspot rectangle and the script.
    b.u16(28);
    b.text("", 256);
    b.text("", 256);
    b.u32(0);
    b.u32(1);
    for (float v : {273.0f, 639.0f, 5000.0f})
        b.f32(v);
    for (int i = 0; i < 13; i++)
        b.f32(1);
    b.f32(0);
    for (uint32_t v : {258u, 630u, 322u, 679u})
        b.u32(v);
    b.text("yesno quit2 0", 256);
    b.u16(1); // listA: one pair
    b.text("a", 256);
    b.text("b", 256);
    b.u16(0); // listB
    b.tag("HOTS");
    b.u16(1);
    b.text("exitover", 256);
    b.tag("DONE");

    Ac2Scene scene;
    const Error err = parseAc2(b.d, scene);
    CHECK(!err);
    CHECK(scene.path == "Locations\\Test\\Test_01.ac2");
    CHECK(scene.records.size() == 5);
    const std::vector<const Ac2Record*> objects = scene.objects();
    CHECK(objects.size() == 2);
    if (objects.size() == 2) {
        CHECK(objects[0]->text("file") == "locations\\test\\backgrounds\\test_01.jpg");
        CHECK(objects[0]->i("b0c") == 1);
        CHECK(objects[0]->field("x") == nullptr); // past its count
        const Ac2Record* sprite = objects[0]->part("SPRT");
        CHECK(sprite && sprite->f("layer") == 1000);
        CHECK(objects[1]->f("y") == 639);
        CHECK(objects[1]->i("right") == 322);
        CHECK(objects[1]->text("script") == "yesno quit2 0");
        const Ac2Value* listA = objects[1]->field("listA");
        CHECK(listA && listA->pairs.size() == 1 && listA->pairs[0].second == "b");
        const Ac2Record* hot = objects[1]->part("HOTS");
        CHECK(hot && hot->text("name") == "exitover");
    }

    // A tag nobody knows stops the reading, naming it.
    Bytes bad;
    bad.tag("SCEN");
    bad.text("x", 256);
    bad.tag("WHAT");
    CHECK(parseAc2(bad.d, scene).message.find("WHAT") != std::string::npos);
    CHECK(scene.records.size() == 1);
}

// A Granny file of one uncompressed section: an object with a name, a
// number and an array of floats, described by its type.
std::vector<uint8_t> makeGr2() {
    Bytes s; // the section
    s.u32(0); // 0: Name -> "hello"
    s.u32(42); // 4: Value
    s.u32(2); // 8: Floats count
    s.u32(0); // 12: Floats -> 16
    s.f32(1.5f);
    s.f32(2.5f);
    const size_t strings = s.d.size(); // 24
    for (const char* str : {"Name", "Value", "Floats", "Real32", "hello"})
        s.d.insert(s.d.end(), str, str + std::strlen(str) + 1);
    while (s.d.size() % 8)
        s.d.push_back(0);
    const size_t type = s.d.size(), floatType = type + 4 * 32;
    auto member = [&](uint32_t t) {
        s.u32(t);
        for (int i = 0; i < 7; i++)
            s.u32(0);
    };
    member(8); // String
    member(19); // Int32
    member(3); // ReferenceToArray
    member(0);
    member(10); // Real32
    member(0);
    // Fixups: where in the section, and what it points at.
    const std::vector<std::pair<size_t, size_t>> fixups = {
        {0, strings + 25},  {12, 16},           {type + 4, strings},      {type + 36, strings + 5},
        {type + 68, strings + 11}, {type + 72, floatType}, {floatType + 4, strings + 18}};

    Bytes f;
    for (uint8_t m : {0xb8, 0x67, 0xb0, 0xca, 0xf8, 0x6d, 0xb1, 0x0f, 0x84, 0x72, 0x8c, 0x7e, 0x5e, 0x19, 0x00, 0x1e})
        f.d.push_back(m);
    const uint32_t headerSize = 0x58 + 44;
    f.u32(headerSize);
    f.u32(0), f.u32(0), f.u32(0);
    f.u32(6); // 0x20: format
    f.u32(0); // total size, set below
    f.u32(0); // CRC
    f.u32(0x38); // section table, from 0x20
    f.u32(1);
    f.u32(0), f.u32(uint32_t(type)); // root type: section 0
    f.u32(0), f.u32(0);              // root object
    f.u32(0x80000013);
    f.u32(0), f.u32(0), f.u32(0), f.u32(0);
    const uint32_t fixupAt = headerSize, dataAt = fixupAt + uint32_t(fixups.size()) * 12;
    for (uint32_t v : {0u, dataAt, uint32_t(s.d.size()), uint32_t(s.d.size()), 4u, 0u, 0u, fixupAt,
                       uint32_t(fixups.size()), dataAt, 0u})
        f.u32(v);
    for (const auto& [at, to] : fixups)
        f.u32(uint32_t(at)), f.u32(0), f.u32(uint32_t(to));
    f.d.insert(f.d.end(), s.d.begin(), s.d.end());
    f.put32(0x24, uint32_t(f.d.size()));
    return f.d;
}

void testGr2() {
    Gr2File file;
    const Error err = file.open(makeGr2());
    CHECK(!err);
    const Gr2Object root = file.root();
    CHECK(root.members().size() == 3);
    CHECK(root.string("Name") == "hello");
    CHECK(root.number("Value") == 42);
    CHECK(root.count("Floats") == 2);
    CHECK(root.element("Floats", 1).number("Real32") == 2.5);
    const Gr2Object::Array a = root.array("Floats");
    CHECK(a.count == 2 && a.stride == 4);
    CHECK(file.typeSize(root.type()) == 16);
    CHECK(root.number("Missing") == 0);

    std::vector<uint8_t> bad = makeGr2();
    bad[0] ^= 1;
    CHECK(file.open(bad).message.find("Granny") != std::string::npos);
}

void testCurves() {
    Curve linear;
    linear.degree = 1;
    linear.knots = {0, 1, 2};
    linear.controls = {0, 10, 20};
    float v = 0;
    linear.evaluate(0.5f, 1, &v);
    CHECK(std::fabs(v - 5) < 1e-5f);
    linear.evaluate(5, 1, &v);
    CHECK(v == 20); // past the end: the last control
    // A B-spline's weights add up to one: constant controls, constant curve.
    Curve quadratic;
    quadratic.degree = 2;
    quadratic.knots = {0, 0.1f, 0.3f, 0.6f, 1};
    quadratic.controls = {3, 3, 3, 3, 3};
    for (float t : {0.05f, 0.2f, 0.5f, 0.9f}) {
        quadratic.evaluate(t, 1, &v);
        CHECK(std::fabs(v - 3) < 1e-5f);
    }

    // A bone turned a quarter round Z, moved along X.
    BoneTransform t;
    t.position[0] = 5;
    t.orientation[2] = std::sqrt(0.5f);
    t.orientation[3] = std::sqrt(0.5f);
    const Mat4 m = toMatrix(t);
    // (1, 0, 0) -> (0, 1, 0) + (5, 0, 0)
    CHECK(std::fabs(m[0] * 1 + m[12] - 5) < 1e-5f);
    CHECK(std::fabs(m[1] * 1 + m[13] - 1) < 1e-5f);
}

} // namespace

int main() {
    testText();
    testAc2();
    testGr2();
    testCurves();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
