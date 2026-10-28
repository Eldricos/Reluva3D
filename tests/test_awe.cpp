// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the AWE engine's readers, on small files built here (no game
// data is ever committed).

#include "engines/awe/ac2.h"
#include "engines/awe/bink_texture.h"
#include "engines/awe/camera.h"
#include "engines/awe/conditions.h"
#include "engines/awe/curves.h"
#include "engines/awe/dialogue.h"
#include "engines/awe/game_text.h"
#include "engines/awe/gr2.h"
#include "engines/awe/model.h"
#include "engines/awe/oodle1.h"
#include "engines/awe/particles.h"
#include "engines/awe/picking.h"
#include "engines/awe/script.h"
#include "engines/awe/story.h"
#include "engines/awe/savegame.h"
#include "engines/awe/shadows.h"
#include "engines/awe/lighting.h"
#include "common/image.h"
#include "engines/awe/actor.h"
#include "engines/awe/animator.h"
#include "engines/awe/sprite.h"
#include "engines/awe/tables.h"
#include "engines/awe/walk_grid.h"

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

    // A movie's subtitles: the dialogue tables' columns, Duration "start, length".
    const std::vector<Subtitle> subs = parseSubtitles(decodeText(utf16File(
        "Name\tAct\tSpeaker\tLine\tDuration\r\n"
        "intro\t0\tMoinsieur Bouc \t\"I dined with him, Poirot!\"\t\"26.192, 11.016\"\r\n"
        "intro\t0\tAntoinette\tMonsieur Poirot!\t\"65.387, 1.580\"\r\n"
        "\t\t\t\t\r\n")));
    CHECK(subs.size() == 2);
    if (subs.size() == 2) {
        CHECK(subs[0].name == "intro" && subs[0].speaker == "Moinsieur Bouc" && subs[0].line == "I dined with him, Poirot!");
        CHECK(std::fabs(subs[0].start - 26.192) < 1e-9 && std::fabs(subs[0].end - 37.208) < 1e-9);
        CHECK(subs[1].line == "Monsieur Poirot!");
    }
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
        CHECK(objects[0]->i("lighting") == 1);
        CHECK(objects[0]->field("x") == nullptr); // past its count
        const Ac2Record* sprite = objects[0]->part("SPRT");
        CHECK(sprite && sprite->f("scrollSpeed") == 1000);
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

// A range encoder for Oodle1's decoder, for values that are equally
// likely. The code is kept as its top 7 bits, then a byte for each time
// the range is scaled up.
struct RangeEncoder {
    std::vector<uint8_t> digits{0};
    uint32_t range = 0x80;

    void uniform(uint32_t value, uint32_t n) {
        normalize();
        encode(value, range / n, n);
    }
    void bit(uint32_t value) {
        normalize();
        encode(value, range >> 1, 2);
    }
    std::vector<uint8_t> bytes() const {
        std::vector<uint8_t> out((7 + 8 * (digits.size() - 1) + 7) / 8 + 1, 0);
        size_t at = 0;
        for (size_t i = 0; i < digits.size(); i++)
            for (int b = i ? 7 : 6; b >= 0; b--, at++)
                out[at / 8] |= uint8_t(((digits[i] >> b) & 1) << (7 - at % 8));
        return out;
    }

private:
    void normalize() {
        for (; range <= 0x800000; range <<= 8)
            digits.push_back(0);
    }
    void encode(uint32_t value, uint32_t step, uint32_t n) {
        uint32_t carry = value * step;
        for (size_t i = digits.size(); i-- > 0 && carry; carry >>= 8) {
            carry += digits[i];
            digits[i] = uint8_t(carry);
        }
        range = value + 1 < n ? step : range - value * step;
    }
};

void testOodle1() {
    // Every model starts with only its escape, which takes the whole range:
    // the first symbol a model gives is a new one, read as a plain value;
    // the second is a bit (1: one already met) and then which one (or the
    // new value). Each part starts its models afresh, so these are all
    // that is needed for a literal or two and a match per part.
    Bytes in;
    for (int part = 0; part < 3; part++) {
        in.u32(256 | 0x8000 << 9); // 256 literal values, a window of 0x8000
        in.u32(256);               // 256 literals in each model; 1 offset model
        in.u32(0x40404040);        // 64 lengths in each model
    }
    RangeEncoder rc;
    // Part 0: "ab". A length of 0 is a literal; the second time the length
    // model gives the 0 already met.
    rc.uniform(0, 65), rc.uniform('a', 256);
    rc.bit(1), rc.uniform(0, 1), rc.uniform('b', 256);
    // Part 1: "c", then length 3 (4 bytes) from 1 back: "ccccc".
    rc.uniform(0, 65), rc.uniform('c', 256);
    rc.bit(0), rc.uniform(3, 65);
    rc.uniform(0, 4), rc.uniform(0, 1), rc.uniform(0, 1);
    // Part 2: "d", then length 61 (128 bytes) from 1 back.
    rc.uniform(0, 65), rc.uniform('d', 256);
    rc.bit(0), rc.uniform(61, 65);
    rc.uniform(0, 4), rc.uniform(0, 1), rc.uniform(0, 1);
    for (uint8_t b : rc.bytes())
        in.d.push_back(b);

    std::vector<uint8_t> out(2 + 5 + 129);
    CHECK(!oodle1Decompress(in.d, 2, 7, out));
    const std::string expected = "ab" + std::string(5, 'c') + std::string(129, 'd');
    CHECK(std::string(out.begin(), out.end()) == expected);

    // Through Gr2File's default decompressor, and errors.
    std::vector<uint8_t> copy(out.size());
    CHECK(!gr2Decompress(2, in.d, 2, 7, copy) && copy == out);
    CHECK(gr2Decompress(1, in.d, 2, 7, copy));
    CHECK(oodle1Decompress(std::vector<uint8_t>(20), 0, 0, copy));
    // A match from further back than the start.
    RangeEncoder far;
    far.uniform(0, 65), far.uniform('a', 256);
    far.bit(0), far.uniform(1, 65);
    far.uniform(1, 4), far.uniform(0, 1), far.uniform(0, 1);
    Bytes bad;
    bad.d.assign(in.d.begin(), in.d.begin() + 36);
    for (uint8_t b : far.bytes())
        bad.d.push_back(b);
    std::vector<uint8_t> small(3);
    CHECK(oodle1Decompress(bad.d, 3, 3, small).message.find("corrupt") != std::string::npos);
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
    // Looping, the last knot stands for the first one loop on: toward the
    // end the line heads for the first control, not the last.
    Curve ramp = linear;
    ramp.controls = {0, 10, 4};
    CurveLoop loop;
    loop.back = loop.forward = true;
    loop.duration = 2;
    ramp.evaluate(1.5f, 1, &v);
    CHECK(std::fabs(v - 7) < 1e-5f);
    ramp.evaluate(1.5f, 1, &v, loop);
    CHECK(std::fabs(v - 5) < 1e-5f);
    // A looping quadratic meets itself: its value at the end is its value
    // at the start (played once, the end is the last control).
    Curve periodic;
    periodic.degree = 2;
    periodic.knots = {0, 1, 2, 3, 4};
    periodic.controls = {0, 5, 2, 7, 9};
    loop.duration = 4;
    float start = 0, end = 0;
    periodic.evaluate(0, 1, &start, loop);
    periodic.evaluate(4, 1, &end, loop);
    CHECK(std::fabs(start - end) < 1e-5f && std::fabs(end - 9) > 1);
    periodic.evaluate(4, 1, &end);
    CHECK(end == 9);
    // Wrapping round, a quaternion is put on the side of the one before:
    // q and -q are one turn.
    Curve turn;
    turn.degree = 1;
    turn.knots = {0, 1, 2};
    turn.controls = {0, 0, 0, -1, 0, 0, 0.7071068f, 0.7071068f, 0, 0, 0, 1};
    loop.duration = 2;
    float q[4];
    turn.evaluate(1.5f, 4, q, loop); // from the second to the first (as -1)
    CHECK(std::fabs(q[3] - 0.5f * (0.7071068f + 1)) < 1e-5f && std::fabs(q[2] - 0.5f * 0.7071068f) < 1e-5f);
    // All of a scale/shear's nine values.
    Curve scale;
    scale.degree = 1;
    scale.knots = {0, 1};
    scale.controls = {1, 0, 0, 0, 1, 0, 0, 0, 1, 3, 0, 0, 0, 3, 0, 0, 0, 3};
    float s[9];
    scale.evaluate(0.5f, 9, s);
    CHECK(s[0] == 2 && s[4] == 2 && s[8] == 2);

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

// Granny's compressed curve formats, on hand-made data with values that
// come out exact.
void testCompressedCurves() {
    const std::vector<float> identity = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    auto expand = [&](const CurveData& d) {
        Curve c;
        CHECK(!expandCurve(d, identity, c));
        return c;
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-6f; };

    CurveData d;
    d.format = uint8_t(CurveFormat::DaIdentity);
    d.dimension = 9;
    Curve c = expand(d);
    CHECK(c.knots == std::vector<float>{0} && c.controls == identity);

    d = CurveData();
    d.format = uint8_t(CurveFormat::DaConstant32f);
    d.controls = {0.5f, 0.25f, -1, 2};
    c = expand(d);
    CHECK(c.knots == std::vector<float>{0} && c.controls == d.controls);

    d.format = uint8_t(CurveFormat::D3Constant32f);
    d.controls = {1.5f, -33, 246};
    c = expand(d);
    CHECK(c.knots == std::vector<float>{0} && c.controls == d.controls);

    d = CurveData();
    d.format = uint8_t(CurveFormat::DaK32fC32f);
    d.degree = 2;
    d.knots = {0, 0.5f};
    d.controls = {1, 2, 3, 4, 5, 6, 7}; // the 7th isn't a whole set
    c = expand(d);
    CHECK(c.degree == 2 && c.knots == d.knots && c.controls == (std::vector<float>{1, 2, 3, 4, 5, 6}));

    // D3K8uC8u and D3K16uC16u: 1/32 a step for the knots (32.0f is
    // 0x42000000), each control value * scale + offset.
    for (CurveFormat f : {CurveFormat::D3K8uC8u, CurveFormat::D3K16uC16u}) {
        d = CurveData();
        d.format = uint8_t(f);
        d.degree = 1;
        d.packed = 0x4200;
        const float scales[3] = {0.5f, 1, 2}, offsets[3] = {-1, 0, 10};
        std::copy(scales, scales + 3, d.controlScales);
        std::copy(offsets, offsets + 3, d.controlOffsets);
        const uint16_t big = f == CurveFormat::D3K16uC16u ? 1000 : 200;
        d.knotsControls = {0, 16, 2, 3, 4, big, 0, 1};
        c = expand(d);
        CHECK(c.degree == 1);
        CHECK(c.knots == (std::vector<float>{0, 0.5f}));
        CHECK(c.controls == (std::vector<float>{0, 3, 18, big * 0.5f - 1, 0, 12}));
    }

    // D4nK8uC7u and D4nK16uC15u: unit quaternions, three components
    // stored and the fourth rebuilt. Table entry 7 (√½/4 a step from 0)
    // for x, y and w, entry 0 (2√½ over the steps, from -√½) for z.
    for (CurveFormat f : {CurveFormat::D4nK8uC7u, CurveFormat::D4nK16uC15u}) {
        const bool wide = f == CurveFormat::D4nK16uC15u;
        const uint16_t top = wide ? 0x8000 : 0x80, most = wide ? 0x7fff : 0x7f;
        d = CurveData();
        d.format = uint8_t(f);
        d.degree = 2;
        d.packed = 0x7077;
        d.oneOverKnotScale = 4; // 1/4 a step
        d.knotsControls = {0, 4, 6,
                           // z left out (2: the second value's top bit),
                           // stored w x y, all 0: z is 1
                           0, top, 0,
                           // the same, the sign (the first's top bit) set
                           top, top, 0,
                           // y left out (1: the third's top bit), stored
                           // z (the most), w, x
                           most, 0, top};
        c = expand(d);
        CHECK(c.degree == 2);
        CHECK(c.knots == (std::vector<float>{0, 1, 1.5f}));
        CHECK(c.controls.size() == 12);
        if (c.controls.size() == 12) {
            CHECK(c.controls[0] == 0 && c.controls[1] == 0 && c.controls[2] == 1 && c.controls[3] == 0);
            CHECK(c.controls[4] == 0 && c.controls[5] == 0 && c.controls[6] == -1 && c.controls[7] == 0);
            CHECK(c.controls[8] == 0 && c.controls[11] == 0);
            CHECK(near(c.controls[9], std::sqrt(0.5f)) && near(c.controls[10], std::sqrt(0.5f)));
        }
    }

    d = CurveData();
    d.format = uint8_t(CurveFormat::D9I3K8uC8u);
    CHECK(expandCurve(d, identity, c).message.find("D9I3K8uC8u") != std::string::npos);
}

// A Bink texture whose bands are all flat (the codec's shortcut for a
// band of one value): the lowest band's value comes out scaled by the
// transform (1000 -> 125 over a 64x64 texture's four levels), the details
// stay zero.
void testBinkTexture() {
    // Each plane: the sizes of its range-coded part (4 zero bytes; the
    // row flags it holds don't matter with no details) and of its raw
    // bits, least significant first in 32-bit words.
    auto plane = [](uint16_t lowest) {
        std::vector<uint32_t> words;
        int used = 32;
        auto put = [&](uint32_t v, int n) {
            for (int i = 0; i < n; i++, used++) {
                if (used == 32) {
                    words.push_back(0);
                    used = 0;
                }
                words.back() |= ((v >> i) & 1) << used;
            }
        };
        put(1, 1); // flat
        put(lowest, 16);
        for (int band = 0; band < 12; band++) {
            put(1, 16); // quantizer
            put(1, 1);  // flat
            put(0, 16);
        }
        std::vector<uint8_t> out = {4, 0, 0, 0, uint8_t(words.size() * 4), 0, 0, 0, 0, 0, 0, 0};
        for (uint32_t w : words)
            for (int k = 0; k < 4; k++)
                out.push_back(uint8_t(w >> (8 * k)));
        return out;
    };
    std::vector<uint8_t> data = {0, 0, 0, 0};
    for (uint16_t v : {1000, 1000, 0}) {
        const std::vector<uint8_t> p = plane(v);
        data.insert(data.end(), p.begin(), p.end());
    }
    std::vector<uint8_t> rgba;
    CHECK(!decodeBinkTexture(64, 64, 4, data, rgba));
    CHECK(rgba.size() == 64 * 64 * 4);
    // Luma 125, the first difference 125: green 125 - 125 / 4 = 94, red
    // 125 + 94, blue 0 + 94. No alpha plane: 0, as the DLL leaves it.
    bool same = true;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        same = same && rgba[i] == 219 && rgba[i + 1] == 94 && rgba[i + 2] == 94 && rgba[i + 3] == 0;
    CHECK(same);
    // Other kinds aren't decoded.
    CHECK(decodeBinkTexture(64, 64, 0, data, rgba));
    CHECK(decodeBinkTexture(64, 64, 6, data, rgba));
}

// Salon_01's camera, against what granny2.dll's GrannyBuildCameraMatrices
// and GrannyWorldSpaceToWindowSpace give for it (reluva-tool awe-camera).
void testCamera() {
    SceneCamera c;
    c.fovY = 0.645772f, c.nearPlane = 10, c.farPlane = 10000;
    const float pos[3] = {-8.14098f, 121.795f, 515.485f}, ear[3] = {-0.232019f, -0.0174533f, 0},
                off[3] = {-10, 276, 990};
    for (int k = 0; k < 3; k++)
        c.position[k] = pos[k], c.elevAzimRoll[k] = ear[k], c.offset[k] = off[k];
    const float dllView[16] = {0.9998f, 0.0040f,   -0.0170f,   0, 0,       0.9732f, 0.2299f, 0,
                               0.0175f, -0.2299f, 0.9731f,     0, 9.1433f, -275.9854f, -1519.7399f, 1};
    const gfx::Mat4 v = c.view();
    for (int k = 0; k < 16; k++)
        CHECK(std::fabs(v[size_t(k)] - dllView[k]) < (k >= 12 ? 0.05f : 0.001f));
    // The DLL's window y counts from the bottom; these are from the top.
    const float points[3][5] = {{-51.637f, 0, -317.618f, 481.8f, 768 - 256.4f},
                                {-51.637f, 170, -317.618f, 481.2f, 768 - 359.8f},
                                {-325.8f, 0, 216.3f, 236.6f, 768 - 96.1f}};
    for (const auto& p : points) {
        float x = 0, y = 0;
        CHECK(c.project(p, 1024, 768, x, y));
        CHECK(std::fabs(x - p[3]) < 0.5f && std::fabs(y - p[4]) < 0.5f);
    }
    // A model's up (MAX's +Z) is the world's +Y; its front (MAX's -Y) faces
    // +Z, turned by rotY.
    const gfx::Mat4 m = characterMatrix(10, 0, 20, 0);
    const gfx::Vec3 top = gfx::transformPoint(m, {0, 0, 100});
    CHECK(top[0] == 10 && top[1] == 100 && top[2] == 20);
    const gfx::Vec3 front = gfx::transformPoint(m, {0, -1, 0});
    CHECK(front[2] == 21);
    // A click finds the floor point that projects back to it.
    for (const auto& p : points) {
        if (p[1] != 0)
            continue;
        float x = 0, z = 0;
        CHECK(c.floorAt(p[3], p[4], 1024, 768, x, z));
        CHECK(std::fabs(x - p[0]) < 2 && std::fabs(z - p[2]) < 2);
    }
    float x, z;
    CHECK(!c.floorAt(512, 0, 1024, 768, x, z)); // above the horizon
}

void testWalkGrid() {
    // A 20-unit grid, blocked down the middle (a = 32) but for b = 50.
    Ac2Record camr;
    camr.tag = "CAMR";
    Ac2Value size;
    size.type = Ac2Value::Type::U16;
    size.raw = 20;
    Ac2Value grid;
    grid.type = Ac2Value::Type::Bytes;
    grid.raw = 64;
    grid.bytes.assign(64 * 64, 0);
    for (int b = 0; b < 64; b++)
        if (b != 50)
            grid.bytes[size_t(32 * 64 + b)] = 1;
    grid.bytes[size_t(10 * 64 + 10)] = 3;
    camr.fields = {{"cellSize", size}, {"grid", grid}};
    WalkGrid g;
    CHECK(g.load(camr) && g.cellSize() == 20);
    // The game's conversions: x = (a - 32) * 20 + 10, z = (32 - b) * 20 - 10.
    float x, z;
    g.centre({31, 0}, x, z);
    CHECK(x == -10 && z == 630);
    WalkGrid::Cell c = g.cellAt(-10, 630);
    CHECK(c.a == 31 && c.b == 0);
    c = g.cellAt(0, 0); // on boundaries: the later of the two, on both
    CHECK(c.a == 32 && c.b == 32);
    CHECK(!g.cellAt(-700, 0).valid());
    CHECK(g.at(32, 5) == 1 && g.blocked({32, 5}, true) && !g.blocked({32, 50}, false));
    CHECK(g.blocked({10, 10}, false) && !g.blocked({10, 10}, true));
    // Across the wall: through its one gap, at b = 50.
    std::vector<WalkGrid::Cell> path = g.findPath({28, 10}, {36, 10});
    CHECK(path.size() >= 3 && path.front() == (WalkGrid::Cell{28, 10}) && path.back() == (WalkGrid::Cell{36, 10}));
    bool through = false;
    for (size_t i = 0; i + 1 < path.size(); i++)
        through |= (path[i].a <= 32) != (path[i + 1].a <= 32) && path[i].b >= 48 && path[i + 1].b >= 48;
    CHECK(through);
    for (const WalkGrid::Cell& p : path)
        CHECK(!g.blocked(p, false));
    CHECK(g.findPath({20, 20}, {21, 21}).empty()); // too near to walk
    // Open ground: straight there.
    path = g.findPath({40, 0}, {60, 30});
    CHECK(path.size() == 2);
    // Round the 3 the first try avoids: the search's cells are dropped
    // in order while a turn from the last one kept clears the next, so
    // the one beside it stays.
    path = g.findPath({0, 0}, {20, 30});
    CHECK(path.size() == 3 && path[1] == (WalkGrid::Cell{9, 10}));
    std::vector<WalkGrid::Point> points;
    CHECK(g.search({0, 0}, {20, 30}, false, false, points) == 2 && points[0].a == 20.5 && points[0].b == 30.5);
    CHECK(g.search({0, 0}, {20, 30}, true, false, points) == 1); // with 3s open, straight there
    CHECK(g.search({0, 0}, {32, 5}, false, false, points) == 0); // the goal blocked
}

void testConditions() {
    GameState st;
    st.values["moving"] = 1;
    st.act = "06b";
    st.tags = {{"bardraweropen", 0}, {"eavesdrop04a", 3}};
    st.items = {{"icetongs"}};
    CHECK(testCondition("", st));
    CHECK(!testCondition("0", st));
    CHECK(testCondition("moving >0", st));
    CHECK(!testCondition("weather >0", st));
    CHECK(testCondition("!master", st));
    CHECK(!testCondition("debug", st));
    CHECK(testCondition("act 06a| act 06b| act 07a", st));
    CHECK(testCondition("act 06a | act 06b | act 07a", st));
    CHECK(!testCondition("act 07a", st));
    CHECK(testCondition("!has icebucket, act >06a", st));
    CHECK(!testCondition("!has icetongs", st));
    CHECK(testCondition("tag BarDrawerOpen", st)); // set, even to 0
    CHECK(!testCondition("tag micheladmonish", st));
    CHECK(testCondition("tag eavesdrop04a >2, tag eavesdrop04a <4, tag eavesdrop04a 3", st));
    CHECK(!testCondition("tag eavesdrop04a 2", st));
    CHECK(!testCondition("!has sellotape, !tag bardraweropen", st));
    CHECK(testCondition("moving 1, act <07a", st));
    st.act.clear();
    CHECK(!testCondition("act 06a", st));
    CHECK(testCondition("!act 06a", st));
    st.act = "06b";

    // As 0x407ae0 has it: a part in brackets first; a '|' anywhere makes
    // any clause do; words it doesn't know don't hold.
    const char* eavesdrop = "tag eavesdrop04a, (use waterglass | tag bardraweropen nouse)";
    CHECK(testCondition(eavesdrop, st)); // nothing in hand
    st.held = "waterglass";
    CHECK(testCondition(eavesdrop, st));
    st.held = "icetongs";
    CHECK(!testCondition(eavesdrop, st));
    CHECK(testCondition("use icetongs", st) && testCondition("use", st) && !testCondition("use waterglass", st));
    st.held.clear();
    CHECK(testCondition("act 07a, tag bardraweropen | has nothing", st));
    CHECK(testCondition("1", st) && !testCondition("null", st) && testCondition("!null", st));
    CHECK(testCondition("tag missing <5", st) && !testCondition("tag missing 0", st)); // -99
    CHECK(!testCondition("act >06b", st) && testCondition("act >06a", st) && testCondition("act <07a", st));
    // The inventory: has, available, used, has all.
    st.items[0].state = 4;
    CHECK(testCondition("has icetongs", st) && !testCondition("has icetongs available", st));
    CHECK(testCondition("used icetongs", st));
    st.items[0].state = 2;
    CHECK(testCondition("has icetongs available", st) && !testCondition("used icetongs", st));
    CHECK(!testCondition("has all passport", st));
    // Names in the scene, the scene left and the location.
    st.scene = "calais.hall_02_04a.ac2";
    st.lastScene = "Locations\\Calais.16\\Calais.16_02.ac2";
    st.location = "calais.hall";
    CHECK(testCondition("scene Calais.Hall_02", st) && testCondition("lastscene calais.16_02", st));
    CHECK(testCondition("location calais.hall", st) && !testCondition("location athens", st));
    // cantalk asks the dialogue.
    CHECK(!testCondition("cantalk dia_test a+", st));
    st.canTalk = [](const std::vector<std::string>& w) { return w.size() == 2 && w[0] == "dia_test"; };
    CHECK(testCondition("cantalk dia_test a+", st) && testCondition("!cantalk dia_other a", st));
}

void testSprite() {
    // Salon_01's left window: movingbg02.bik mirrored and turned -40
    // degrees about its top-left corner.
    SpriteTransform t;
    t.x = 100, t.y = 54, t.scaleX = -1.5f, t.scaleY = 1.5f, t.roll = -0.698132f;
    float x, y;
    t.apply(0, 0, x, y);
    CHECK(x == 100 && y == 54);
    t.apply(256, 256, x, y);
    CHECK(std::fabs(x - 53.1f) < 0.5f && std::fabs(y - 595.0f) < 0.5f);
    // Drawn: a 2x2 picture scaled by 2 covers 4x4 pixels from (1, 1).
    image::Image img;
    img.w = img.h = 2;
    img.rgba.assign(16, 255);
    Surface dst;
    dst.create(8, 8, PixelFormat::RGBA32);
    std::fill(dst.pixels.begin(), dst.pixels.end(), 0);
    SpriteTransform s2;
    s2.x = 1, s2.y = 1, s2.scaleX = 2, s2.scaleY = 2;
    drawSprite(dst, img, s2, true);
    CHECK(dst.row(1)[1 * 4 + 3] == 255 && dst.row(4)[4 * 4] == 255);
    CHECK(dst.row(0)[0 * 4 + 3] == 0 && dst.row(5)[5 * 4 + 3] == 0);
}

Ac2Value realValue(float f) {
    Ac2Value v;
    v.type = Ac2Value::Type::F32;
    std::memcpy(&v.raw, &f, 4);
    return v;
}

void testParticles() {
    // A SNOW record with a few fields: the rest are the class's defaults.
    Ac2Record snow;
    snow.tag = "SNOW";
    snow.fields = {{"x", realValue(-730)}, {"y", realValue(300)}, {"z", realValue(-330)}, {"depth", realValue(-10500)}};
    Ac2Record syst;
    syst.tag = "SYST";
    syst.fields = {{"velocityY", realValue(-300)}};
    snow.parts.push_back(syst);
    ParticleSettings s = ParticleSettings::fromRecord(snow);
    CHECK(s.mode == 2 && s.alphaBlend && s.condition == "weather >0");
    CHECK(s.velocity[1] == -300 && s.burst == 5 && s.spread == 2000 && s.size == 30);
    CHECK(s.depth == -10500 && s.position[1] == 300 && s.sway == 20);

    // Falling: a burst at once, then down at 300 a second, gone below 0.
    s.spread = 10;
    s.sway = 0;
    ParticleSystem fall(s);
    fall.update(0.01f);
    CHECK(fall.count() == 5);
    std::vector<ParticleSystem::Point> pts;
    fall.points(pts);
    CHECK(pts.size() == 5 && pts[0].position[1] == 300);
    CHECK(pts[0].position[0] >= -740 && pts[0].position[0] < -720 && pts[0].rgba[3] == 255);
    s.interval = 100; // one burst only
    ParticleSystem once(s);
    once.update(0.01f);
    once.update(0.5f);
    pts.clear();
    once.points(pts);
    CHECK(pts.size() == 5 && std::fabs(pts[0].position[1] - 150) < 0.01f);
    once.update(0.6f);
    CHECK(once.count() == 0);

    // Thrown straight up (mode 0, no spread across): gravity brings it back.
    ParticleSettings t;
    t.mode = 0;
    t.velocity[0] = t.velocity[2] = 0;
    t.velocity[1] = 100;
    t.gravity = 100;
    t.signY = 1;
    t.burst = 1;
    t.interval = 100;
    t.position[1] = 10;
    t.bounces = 0;
    t.fadeIn = 1;
    ParticleSystem up(t);
    up.update(0.001f);
    CHECK(up.count() == 1);
    up.update(0.5f);
    pts.clear();
    up.points(pts);
    // y = 10 + v*t - 50*t^2, v in [50, 100]: between 10 + 25 - 12.5 and 10 + 50 - 12.5.
    CHECK(pts.size() == 1 && pts[0].position[1] > 22 && pts[0].position[1] < 48);
    CHECK(pts[0].rgba[3] > 120 && pts[0].rgba[3] < 135); // half faded in
    up.update(2.5f);
    CHECK(up.count() == 0); // landed with no bounces left

    // A breath: the emitter moved and its velocity changed as it goes.
    ParticleSettings b;
    b.mode = 0, b.velocity[1] = 0, b.gravity = 0, b.burst = 1, b.interval = 100, b.spread = 1, b.sway = 0;
    ParticleSystem breath(b);
    const float mouth[3] = {100, 160, -50}, ahead[3] = {0, 0, 50};
    breath.setEmitter(mouth, ahead);
    breath.update(0.001f);
    breath.update(1);
    pts.clear();
    breath.points(pts);
    CHECK(pts.size() == 1 && pts[0].position[1] == 160 && std::fabs(pts[0].position[0] - 100) <= 1);
    CHECK(std::fabs(pts[0].position[2] + 50) <= 1 + 50 * 0.7072f);
}

void testPicking() {
    // Front to back by depth; the later of equal ones first.
    std::vector<PickCandidate> c(3);
    c[0].depth = -10000; // the background: no outline, no rectangle
    c[1].depth = 5000, c[1].rect = true, c[1].clickable = true, c[1].hit = true; // a hotspot
    c[2].depth = 5000, c[2].rect = true, c[2].hit = true;                        // another, after it
    CHECK(pick(c, false) == 2);
    c[2].depth = 260;
    CHECK(pick(c, false) == 1);
    c[1].hit = c[2].hit = false;
    CHECK(pick(c, false) == -1);
    // A character in front of nothing but behind a hotspot: the characters
    // come first, those with neither rectangle nor outline by their bones'
    // boxes, if they can be clicked or an item is in hand.
    PickCandidate npc;
    npc.depth = 400, npc.character = true, npc.clickable = true;
    c[1].hit = true;
    c.push_back(npc);
    CHECK(pick(c, false) == 1); // bones missed: it's behind the hotspot's rectangle
    c[3].boneHit = true;
    CHECK(pick(c, false) == 3);
    c[3].clickable = false;
    CHECK(pick(c, false) == 1);
    CHECK(pick(c, true) == 3); // an item in hand: tried all the same
    // A character with a rectangle (or an outline) goes by it, not its bones.
    c[3].rect = true, c[3].boneHit = true, c[3].hit = false;
    CHECK(pick(c, false) == 1);
    c[3].hit = true;
    CHECK(pick(c, false) == 3);
    // pickFirst objects with a rectangle or an outline come before even
    // the characters.
    PickCandidate first;
    first.depth = -100, first.rect = true, first.pickFirst = true, first.hit = true;
    c.push_back(first);
    CHECK(pick(c, false) == 4);
    c[4].rect = false, c[4].outline = true;
    CHECK(pick(c, false) == 4);

    // Outlines: the pixel's centre against the polygon, by winding.
    const std::vector<std::pair<uint32_t, uint32_t>> square = {{10, 10}, {20, 10}, {20, 20}, {10, 20}};
    CHECK(inPolygon(square, 10, 10) && inPolygon(square, 19, 19) && !inPolygon(square, 20, 15));
    CHECK(!inPolygon(square, 9, 15) && !inPolygon({{0, 0}, {5, 5}}, 1, 1));
    // A star drawn in one stroke: its middle is in, by winding.
    const std::vector<std::pair<uint32_t, uint32_t>> star = {{50, 0}, {79, 90}, {2, 35}, {98, 35}, {21, 90}};
    CHECK(inPolygon(star, 50, 50) && inPolygon(star, 50, 10) && !inPolygon(star, 5, 80));

    // A ray against a box turned and moved: a bone's.
    gfx::Mat4 m = gfx::identity();
    m[0] = 0, m[1] = 1, m[4] = -1, m[5] = 0; // 90 degrees about z
    m[12] = 100, m[13] = 0, m[14] = 0;
    const float lo[3] = {0, -5, -5}, hi[3] = {50, 5, 5}; // along the bone's x: the world's +y
    CHECK(rayHitsBox(m, lo, hi, {100, 25, -100}, {0, 0, 1}));
    CHECK(!rayHitsBox(m, lo, hi, {100, 60, -100}, {0, 0, 1}));
    CHECK(!rayHitsBox(m, lo, hi, {100, 25, -100}, {0, 0, -1})); // behind the ray's start
    CHECK(!rayHitsBox(m, lo, hi, {120, 25, -100}, {0, 0, 1}));

    // The camera's ray through a pixel comes down where floorAt says.
    SceneCamera cam;
    cam.fovY = 0.645772f, cam.nearPlane = 10, cam.farPlane = 10000;
    cam.position[1] = 120, cam.elevAzimRoll[0] = -0.23f, cam.offset[1] = 276, cam.offset[2] = 990;
    gfx::Vec3 from, dir;
    cam.ray(400, 600, 1024, 768, from, dir);
    float x = 0, z = 0;
    CHECK(cam.floorAt(400, 600, 1024, 768, x, z));
    const float k = -from[1] / dir[1];
    CHECK(std::fabs(from[0] + k * dir[0] - x) < 0.01f && std::fabs(from[2] + k * dir[2] - z) < 0.01f);
}

// A world for scripts: walks and fades take until told; the rest is
// instant. It records what it was asked.
struct FakeHost : ScriptHost {
    std::vector<std::string> started;
    bool start(ScriptRunner&, const ScriptEvent& e, bool wait) override {
        std::string s = e.verb();
        for (size_t i = 1; i < e.words.size(); i++)
            s += " " + e.words[i];
        started.push_back(s + (wait ? "" : " (no wait)"));
        return e.verb() == "walk" || e.verb() == "fade" || e.verb() == "talk";
    }
};

void testScript() {
    GameState state;
    FakeHost host;
    ScriptRunner r(state, host);
    r.run("walk 31 0, scene salon_02 1, tag act04a 2, tag act04a +3, add ticket");
    CHECK(!r.idle());
    r.update(0.01f);
    CHECK(host.started.size() == 1 && host.started[0] == "walk 31 0");
    r.update(0.01f); // still walking
    CHECK(host.started.size() == 1);
    r.finished("talk"); // not what it waits for
    r.update(0.01f);
    CHECK(host.started.size() == 1);
    r.finished("walk");
    r.update(0.01f);
    CHECK(host.started.size() == 2 && host.started[1] == "scene salon_02 1");
    for (int i = 0; i < 4; i++)
        r.update(0.01f);
    CHECK(state.tags["act04a"] == 5 && state.itemState("ticket") == 2 && r.idle());

    // The "a" forms don't wait; "wait" counts down; a fade after a walk
    // starts as the walk ends.
    host.started.clear();
    r.run("walka sp1, talka dia_test a, wait 0.5, walk 3 4\nfade out");
    r.update(0.01f);
    CHECK(host.started.size() == 1 && host.started[0] == "walk sp1 (no wait)");
    r.update(0.01f);
    CHECK(host.started.size() == 2 && host.started[1] == "talk dia_test a (no wait)");
    r.update(0.01f); // the wait starts
    r.update(0.3f);
    r.update(0.01f);
    CHECK(host.started.size() == 2);
    r.update(0.3f); // over
    r.update(0.01f);
    CHECK(host.started.size() == 3 && host.started[2] == "walk 3 4");
    r.finished("walk");
    CHECK(host.started.size() == 4 && host.started[3] == "fade out");
    r.finished("fade");
    CHECK(r.idle());
    // removetag, setchar, remove.
    r.run("removetag act04a, setchar debenham, remove ticket");
    for (int i = 0; i < 3; i++)
        r.update(0.01f);
    CHECK(!state.tags.count("act04a") && r.character() == "debenham" && !state.itemState("ticket"));
    // The next script given acts on the player again (0x408cd8).
    r.run("tag after 1");
    CHECK(r.character().empty());
    // A character's own runner goes back to that character (0x40598f).
    r.setCharacter("klaus");
    r.run("setchar antoinette");
    for (int i = 0; i < 3; i++)
        r.update(0.01f);
    CHECK(r.character() == "antoinette");
    r.run("tag after 2");
    CHECK(r.character() == "klaus");
    // Two of a name (0x41bc10 adds one each time): "use" the first in the
    // bag, "restore" the first used up, "remove" the first whatever it is.
    r.setCharacter("");
    state.items.clear();
    r.run("add waterglass, add waterglass, use waterglass");
    for (int i = 0; i < 5; i++)
        r.update(0.01f);
    CHECK(state.items.size() == 2 && state.items[0].state == 4 && state.items[1].state == 2);
    CHECK(testCondition("used waterglass", state) && testCondition("has waterglass available", state));
    r.run("use waterglass, restore waterglass, remove waterglass");
    for (int i = 0; i < 5; i++)
        r.update(0.01f);
    CHECK(state.items.size() == 1 && state.items[0].state == 4 && state.items[0].slot == 1);

    // The door macros, and a script file.
    struct FileHost : FakeHost {
        std::string scriptText(const std::string& name) override {
            return name == "door" ? "walk 40 26\nscript nested" : name == "nested" ? "tag nested 1" : "";
        }
    } files;
    GameState state2;
    ScriptRunner r2(state2, files);
    r2.run("dooropen sp3 L 2, script door");
    for (int i = 0; i < 40; i++) {
        r2.update(0.5f);
        r2.finished("walk");
        r2.finished("fade");
    }
    CHECK(files.started.size() == 9);
    if (files.started.size() == 9) {
        CHECK(files.started[0] == "walk sp3" && files.started[1] == "rotate -sp3");
        CHECK(files.started[2] == "anim doorknobL (no wait)" && files.started[3] == "turn down");
        CHECK(files.started[4] == "fade out" && files.started[5] == "sound dooropen2 (no wait)");
        CHECK(files.started[6] == "turn home" && files.started[7] == "animate 2" && files.started[8] == "walk 40 26");
    }
    CHECK(state2.tags["nested"] == 1 && r2.idle());

    // A condition in brackets decides whether what follows is added, when
    // it's added (0x408c80).
    GameState state3;
    FakeHost host3;
    ScriptRunner r3(state3, host3);
    state3.tags["hardman06bend"] = 4;
    r3.run("tag a, (tag hardman06bend >3, has printhardman), tag b, tag c, (tag hardman06bend 4) tag d, tag e");
    r3.fastForward();
    CHECK(state3.tags.count("a") && !state3.tags.count("b") && !state3.tags.count("c") && state3.tags.count("e"));
    // The inventory's verbs.
    r3.run("add waterglass, add passmasterman, add hammer, use waterglass");
    r3.fastForward();
    CHECK(state3.itemState("waterglass") == 4 && state3.itemState("passmasterman") == 5);
    r3.run("restore waterglass, remove all");
    r3.fastForward();
    CHECK(state3.itemState("waterglass") == 7 && state3.itemState("hammer") == 7 && state3.itemState("passmasterman") == 5);
    state3.tags["securityunlocked"] = 1;
    r3.run("add keysmall, restore all, remove hammer");
    r3.fastForward();
    CHECK(state3.itemState("waterglass") == 2 && state3.itemState("hammer") == 0 && state3.itemState("keysmall") == 0);
    CHECK(!state3.tags.count("securityunlocked"));
    // Clicks are refused while it runs, but during a walk, or after
    // "unlock".
    host3.started.clear();
    r3.run("walk 3 4, wait 1, unlock, wait 1, lock, wait 1");
    r3.update(0.01f);
    CHECK(!r3.locked());
    r3.finished("walk");
    r3.update(0.01f);
    CHECK(r3.locked());
    r3.update(1.0f);
    r3.update(0.01f);
    r3.update(0.01f);
    CHECK(!r3.locked());
    r3.update(1.0f);
    r3.update(0.01f);
    r3.update(0.01f);
    CHECK(r3.locked());
    for (int i = 0; i < 4; i++)
        r3.update(1.0f);
    CHECK(r3.idle() && !r3.locked());
}

void testItemSlots() {
    // Items take the lowest free slot of the inventory window (16 a page)
    // and the window turns to its page; passports and fingerprint cards
    // have their own pages; the item just taken blinks, one at a time
    // (0x41bc10, 0x41bfb0, 0x41ba80).
    GameState s;
    auto slotOf = [&](const std::string& name) {
        const GameState::Item* i = s.findItem(name, {1, 2, 3});
        return i ? i->slot : -1;
    };
    addItem(s, "hammer", 2);
    addItem(s, "pliers", 1);
    addItem(s, "passmichel", 2);
    addItem(s, "printmichel", 1);
    CHECK(slotOf("hammer") == 0 && slotOf("pliers") == 1 && slotOf("passmichel") == -1);
    CHECK(s.itemState("passmichel") == 5 && s.itemState("printmichel") == 6 && s.fresh == "pliers");
    addItem(s, "nails", 1);
    CHECK(s.fresh == "nails" && s.itemState("pliers") == 2 && s.itemState("nails") == 1 && slotOf("nails") == 2);
    s.findItem("hammer")->state = 4; // used up: its slot is free
    addItem(s, "coal", 2);
    CHECK(slotOf("coal") == 0);
    addItem(s, "hammer", 2); // another: the next free slot
    CHECK(slotOf("hammer") == 3 && s.itemPage == 0 && s.itemState("hammer") == 4);
    for (int i = 0; i < 13; i++)
        addItem(s, "thing" + std::to_string(i), 2);
    CHECK(slotOf("thing12") == 16 && s.itemPage == 1);
    CHECK(s.items.size() == 20 && s.items[0].name == "hammer" && s.items[2].name == "passmichel");

    // Two of a name, as the game keeps them: "use" takes the first in the
    // bag, "restore" the first used up, "remove" the first of either.
    GameState g;
    addItem(g, "waterglass", 2);
    addItem(g, "waterglass", 2);
    CHECK(g.items.size() == 2 && g.items[1].slot == 1);
    g.findItem("waterglass", {1, 2})->state = 4;
    CHECK(g.itemAvailable("waterglass") && g.findItem("waterglass", {4}) == &g.items[0]);
    CHECK(g.freeSlot() == 0);
}

void testDialogueBook() {
    const char* table =
        "Name\tAct\tConver.\tThread\tNest\tSpeaker\tTarget\tLine\tLine Translation\tCondition\tRepeat\tPrevent\t"
        "Event\tPovent\tDuration\tOptional\tNotes\n"
        "dia_observation\t02a\ta\t1\ta\tpoirot\t0\tOne must look.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_observation\t02a\ta\ta\ta\tantoinette\tantoinette up\t\"Well, yes.\"\t0\thas ribbon\ta\t"
        "scene restaurant_table.02\tanim no\tscene restaurant_table.07\t0\t1\t0\n"
        "gen_locked\t0\t1\ta\ta\tantoinette\t0\tIt's locked.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "gen_locked\t0\t1\ta\ta\tantoinette\t0\tLocked.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n";
    DialogueBook book;
    book.add(parseTable(table));
    CHECK(book.parts().size() == 2);
    const auto obs = book.conversation("Dia_Observation");
    CHECK(obs.size() == 2);
    if (obs.size() == 2) {
        CHECK(obs[0]->thread == "1" && obs[0]->threadCode == 0x60 && obs[0]->speaker == "poirot" && obs[0]->target.empty());
        CHECK(obs[0]->voiceFile() == "sound/vo/dia_observation02aa1a.wav");
        CHECK(obs[1]->text == "Well, yes." && obs[1]->target == "antoinette up" && obs[1]->condition == "has ribbon");
        CHECK(obs[1]->before == "scene restaurant_table.02" && obs[1]->event == "anim no" &&
              obs[1]->after == "scene restaurant_table.07" && obs[1]->repeatCode == 0 && obs[1]->optional == "1");
    }
    // Part "1" is code 100; a line with the nest before it again is an
    // alternative, its recording numbered.
    DialoguePart* locked = book.find("gen_locked", 100);
    CHECK(locked && locked->threads.size() == 1 && locked->threads[0].lines.size() == 2);
    if (locked && locked->threads[0].lines.size() == 2)
        CHECK(locked->threads[0].lines[1].voiceFile() == "sound/vo/gen_locked01aa_1.wav");
    CHECK(dialoguePartCode("a") == 0 && dialoguePartCode("1") == 100 && dialogueThreadCode("z1") == 26 &&
          dialogueThreadCode("3") == 0x62 && dialogueNestCode("b") == 1);
    // Read by position, whatever the headings (gen_characters.txt's first
    // is a name), with no Optional column.
    DialogueBook gen;
    gen.add(parseTable("gen_globalhardman\tAct\tConver.\tThread\tNest\tSpeaker\tTarget\tLine\n"
                       "gen_arbuthnot_exit\t0\t0\t3\ta\tantoinette\t0\tAu revoir.\t0\t0\t0\t0\t0\t0\t0\tadded\n"));
    const DialoguePart* exit = gen.find("gen_arbuthnot_exit", 99);
    CHECK(exit && exit->threads.size() == 1 && exit->threads[0].code == 0x62);
}

// A conversation played through: no voices, so each line lasts its
// length / 15 seconds (at least one).
struct QuietHost : DialogueHost {
    bool playVoice(const std::string&) override { return false; }
    bool voicePlaying() override { return false; }
    void stopVoice() override {}
    std::string readText(const std::string&) override { return {}; }
    std::string sceneName() override { return "salon_01"; }
    void talkStarting(const std::string& name) override { started.push_back(name); }
    std::vector<std::string> started;
};

void testDialogue() {
    const char* table =
        "Name\tAct\tConver.\tThread\tNest\tSpeaker\tTarget\tLine\tLine Translation\tCondition\tRepeat\tPrevent\t"
        "Event\tPovent\tDuration\tOptional\tNotes\n"
        "dia_obs\t02a\ta\t1\ta\tpoirot\t0\tLook.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_obs\t02a\ta\ta\ta\tantoinette\t0\tThe ladies?\t0\t0\t0\t0\ttag sawladies 1\t0\t0\t0\t0\n"
        "dia_obs\t02a\ta\ta\tb\tpoirot\t0\tYes.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_obs\t02a\ta\tb\ta\tantoinette\t0\tThe man?\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_obs\t02a\ta\tc\ta\tantoinette\t0\tSecret.\t0\ttag never\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_obs\t02a\tb\t1\ta\tpoirot\t0\tGood.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n";
    GameState state;
    FakeHost scripts;
    QuietHost host;
    DialogueManager d(state, scripts, host);
    d.book().add(parseTable(table));
    ScriptRunner main(state, scripts);
    main.run("talk dia_obs a+");
    // The runner waits on "talk": a stand-in starts the dialogue for it.
    struct TalkHost : ScriptHost {
        DialogueManager* d = nullptr;
        bool start(ScriptRunner& r, const ScriptEvent& e, bool) override {
            return e.verb() == "talk" && d->talk(r, e.arg(1), 0, -1, -1, false, -1, true, true);
        }
    } talker;
    talker.d = &d;
    ScriptRunner runner(state, talker);
    runner.run("talk dia_obs a+, tag after 1");
    runner.update(0.01f);
    CHECK(d.active() && !runner.idle());
    // The inventory's changes, once the conversation is found (0x43a670).
    CHECK(host.started.size() == 1 && host.started[0] == "dia_obs");
    auto run = [&](float seconds) {
        for (float t = 0; t < seconds; t += 0.05f) {
            d.update(0.05f);
            runner.update(0.05f);
        }
    };
    run(1.0f); // the box slides in
    CHECK(d.line() && d.line()->text == "Look.");
    run(1.6f); // the opening, then the choices (not "Secret.": its condition fails)
    CHECK(d.choosing() && d.choices().size() == 2);
    if (d.choices().size() == 2) {
        CHECK(d.choices()[0].text == "The ladies?" && d.choices()[1].text == "The man?");
        d.setChoiceAreas({{640, 20}, {664, 20}});
        // The wheel: towards the user to the next, round through nothing
        // (no goodbye); away to the one before. Its choice stays, the
        // cursor hidden, till the mouse moves.
        d.wheel(-1, 100, 300);
        CHECK(d.highlighted() == 0 && d.wheelChoosing());
        d.wheel(-2, 100, 300);
        CHECK(d.highlighted() == -1);
        d.wheel(1, 100, 300);
        CHECK(d.highlighted() == 1);
        d.mouseMove(100, 300);
        CHECK(d.highlighted() == 1 && d.wheelChoosing());
        d.mouseMove(101, 300);
        CHECK(d.highlighted() == -1 && !d.wheelChoosing());
        d.middleClick(); // nothing highlighted: nothing taken
        CHECK(d.choosing());
        d.click(100, 645);
    }
    CHECK(d.line() && d.line()->text == "The ladies?");
    run(1.0f);
    CHECK(state.tags["sawladies"] == 1); // its Event ran
    run(1.6f);
    // Heard to its end: kept for the "talk" condition.
    CHECK(testCondition("talk dia_obs 02a aa", state) && !testCondition("talk dia_obs 02a ab", state));
    // One choice left: played without asking; then part b, its opening.
    CHECK(d.line() && d.line()->text == "The man?");
    run(1.6f);
    CHECK(d.line() && d.line()->text == "Good.");
    run(4.0f); // the last line, the box slides out, the talk is done
    CHECK(!d.active());
    run(0.2f);
    CHECK(state.tags["after"] == 1 && runner.idle());

    // A generic extra with no line to offer (its first line's condition
    // fails, though a later one has none) doesn't count (0x43a470): part a,
    // heard, is exhausted, and "+" goes on to part b.
    d.book().add(parseTable(
        "Name\tAct\tConver.\tThread\tNest\tSpeaker\tTarget\tLine\tLine Translation\tCondition\tRepeat\tPrevent\t"
        "Event\tPovent\tDuration\tOptional\tNotes\n"
        "dia_hub\t06b\ta\ta\ta\thub\t0\tPassport.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_hub\t06b\tb\ta\ta\tantoinette\t0\tWas it a woman?\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "gen_hub_print\t0\ta\ta\ta\tantoinette\t0\tPrints?\t0\ttag never\t0\t0\t0\t0\t0\t0\t0\n"
        "gen_hub_print\t0\ta\ta\tb\thub\t0\tA ribbon?\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"));
    state.talks[talkKey("dia_hub", "06b", 0, 0)] = TalkProgress{1, -1};
    CHECK(d.talk(runner, "dia_hub", 0, -1, -1, true, -1, true, true));
    run(1.0f);
    CHECK(d.line() && d.line()->text == "Was it a woman?");
}

} // namespace

void testStory() {
    int major = 0, minor = 0;
    CHECK(parseActName("06a", major, minor) && major == 6 && minor == 1 && actName(major, minor) == "06a");
    CHECK(!parseActName("a", major, minor));
    const Table t = parseTable("Act\tPreLoad\tLocation to Load\tPost Load\tTime of Day\tWeather\tTrain Moving\t"
                               "Completion Conditions\tEvent\tMusic\tWhat to do\n"
                               "01A\t\"fade out, movie intro\"\tsirkeci.market\t\"scene sirkeci.market_01 1 0, add ticket\"\t"
                               "day\tsnow\tNo\t0\tact 01b\t0\t0\n"
                               "05A\t0\tSalon\tscene salon_01\tnight\tBlizzard\tYes\ttag act05a\tact 05b\t0\t0\n"
                               "05B\t0\tsalon\t0\tday\tnone\tyes\t0\t0\t0\t0\n");
    const std::vector<ActRow> acts = readActs(t);
    CHECK(acts.size() == 3 && acts[0].act == "01a" && acts[0].preLoad == "fade out, movie intro");
    CHECK(acts[0].weather == 1 && acts[0].time == 0 && acts[0].moving == 0 && acts[1].location == "salon");
    CHECK(acts[1].weather == 2 && acts[1].time == 1 && acts[1].moving == 1);

    GameState state;
    FakeHost host;
    ScriptRunner main(state, host);
    Story story(state, main);
    story.setActs(acts);
    std::vector<std::string> loaded;
    story.onLoadLocation = [&](const std::string& l, bool flush) {
        loaded.push_back(l + (flush ? " flush" : ""));
        story.locationLoaded();
    };
    // reset, then a bare act: 01A.
    main.run("tag x 1, timer clock, reset");
    for (int i = 0; i < 4; i++)
        main.update(0.01f);
    CHECK(state.act == "01a" && state.tags.empty() && state.timers.empty());
    CHECK(story.startAct("") && story.loading());
    // PreLoad runs first: the fade waits.
    story.update(0.01f);
    CHECK(loaded.empty());
    main.update(0.01f);
    CHECK(host.started.back() == "fade out");
    story.update(0.01f);
    CHECK(loaded.empty());
    main.finished("fade");
    main.update(0.01f); // the movie (FakeHost: done at once)
    story.update(0.01f);
    CHECK(loaded.size() == 1 && loaded[0] == "sirkeci.market" && state.values["weather"] == 1);
    for (int i = 0; i < 3; i++)
        main.update(0.01f);
    CHECK(host.started.back() == "scene sirkeci.market_01 1 0" && state.itemState("ticket") && main.idle());
    // 05A: its Event once its conditions hold and the queue runs dry.
    story.startAct("05a");
    story.update(0.01f);
    CHECK(loaded.back() == "salon" && state.values["moving"] == 1); // inside the train: kept
    for (int i = 0; i < 3; i++)
        main.update(0.01f);
    host.started.clear();
    main.run("tag act05a 1");
    for (int i = 0; i < 3; i++)
        main.update(0.01f);
    CHECK(!host.started.empty() && host.started[0] == "act 05b");
    // (The page's "act" starts 05B, whose row has no conditions.)
    story.startAct("05b");
    story.update(0.01f);
    main.clear();
    // An act without a row: only the act changes, the row stays.
    CHECK(!story.startAct("05c") && state.act == "05c" && story.row() && story.row()->act == "05b");

    // Timers: a second at a time, "time n", the condition.
    main.run("timer ham, time 50");
    for (int i = 0; i < 3; i++)
        main.update(0.01f);
    story.update(0.6f);
    story.update(0.6f);
    CHECK(testCondition("timer ham >50", state) && !testCondition("timer ham >51", state));
    CHECK(testCondition("timer ham", state) && !testCondition("timer radio", state) && testCondition("timer radio <0", state));
    // Visits and savescene.
    state.scene = "salon_01_06a.ac2";
    state.texts["salon_01_06a.ac2"] = "02a 05c";
    CHECK(testCondition("beenhere this", state) && testCondition("beenthere this ever", state));
    CHECK(testCondition("beenthere salon_01_06a 02a", state) && !testCondition("beenthere salon_02", state));
    state.lastScene = "exterior.forest_11.ac2";
    CHECK(testCondition("lastscene exterior.forest_11", state) && !testCondition("lastscene exterior.forest_13", state));
    main.run("savescene this, savescene calais.hall_03 2 1");
    main.update(0.01f);
    CHECK(state.texts["savedscene"] == "salon_01_06a.ac2 -1 0");
    main.update(0.01f);
    CHECK(state.texts["savedscene"] == "calais.hall_03.ac2 2 1");
}

// A skeleton of four bones: Bip01, its spine, the head, the jaw's master.
Model testSkeleton() {
    Model m;
    const char* names[] = {"Bip01", "Bip01 Spine", "Bip01 Head", "Jaw_Bones_Master"};
    for (int i = 0; i < 4; i++) {
        Bone b;
        b.name = names[i];
        b.parent = i - 1;
        b.inverseWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        m.bones.push_back(b);
    }
    return m;
}

// An animation holding each bone turned `degrees` about Z, `seconds`
// long, with a track mask.
std::shared_ptr<Animation> testTurn(float degrees, float seconds, std::map<std::string, float> mask) {
    auto a = std::make_shared<Animation>();
    a->duration = seconds;
    a->mask = std::move(mask);
    const float h = degrees / 57.29578f / 2;
    for (const char* bone : {"Bip01", "Bip01 Spine", "Bip01 Head", "Jaw_Bones_Master"}) {
        AnimationTrack t;
        t.bone = bone;
        t.orientation.knots = {0};
        t.orientation.controls = {0, 0, std::sin(h), std::cos(h)};
        a->tracks.push_back(t);
    }
    return a;
}

// A bone's turn about Z in its parent's axes, in degrees.
float boneTurn(const Pose& p, size_t bone) {
    const Mat4& w = p.world[bone];
    float parent = 0;
    if (bone > 0)
        parent = std::atan2(p.world[bone - 1][1], p.world[bone - 1][0]);
    return std::remainder(std::atan2(w[1], w[0]) - parent, 6.2831853f) * 57.29578f;
}

void testAnimator() {
    const Model m = testSkeleton();
    Animator an(m);
    // A base animation (0.01 on Bip01, all bones following) and a gesture
    // (0.99 on the spine and what's under it, but not the jaw's master).
    auto idle = testTurn(10, 2, {{"Bip01", 0.01f}});
    auto gesture = testTurn(50, 1, {{"Bip01 Spine", 0.99f}});
    CHECK(an.play("idle1", idle, Animator::Body, true, 0, 0));
    CHECK(an.play("agree", gesture, Animator::Body, false, 1, 0.25f));
    const Animator::Slot* g = an.find("agree");
    CHECK(g && std::abs(g->mask[1] - 0.99f) < 1e-6f && std::abs(g->mask[2] - 0.99f) < 1e-6f && g->mask[3] == 0);
    CHECK(std::abs(an.find("idle1")->mask[3] - 0.01f) < 1e-6f);
    std::vector<std::string> ended, finished;
    an.onEnding = [&](const Animator::Slot& s) { ended.push_back(s.name); };
    an.onFinished = [&](const Animator::Slot& s) { finished.push_back(s.name); };
    an.update(0.5f);
    Pose p;
    an.pose(p);
    // The root as the base; the spine almost the gesture's; the jaw's
    // master the base's.
    CHECK(std::abs(boneTurn(p, 0) - 10) < 0.1f);
    CHECK(std::abs(boneTurn(p, 1) - 50) < 1.0f && std::abs(boneTurn(p, 3) - 10) < 0.1f);
    an.update(0.3f); // within its ease of the end
    CHECK(ended.size() == 1 && ended[0] == "agree" && finished.empty());
    an.update(0.3f);
    CHECK(finished.size() == 1 && finished[0] == "agree" && !an.find("agree"));
    // A new base replaces the last, which stops without telling.
    CHECK(an.play("walk", testTurn(0, 1, {{"Bip01", 0.01f}}), Animator::Body, true, 0, 0.3f));
    CHECK(!an.find("idle1") && an.base() && an.base()->name == "walk");
    an.update(1.0f);
    CHECK(an.slots().size() == 1 && finished.size() == 1);
    // A finite base handing over to idle1 as it ends ends as it was, and
    // is told as done ("anim standup" waits on it).
    Animator standing(m);
    std::vector<std::string> done;
    standing.onEnding = [&](const Animator::Slot& s) {
        if (s.base)
            standing.play("idle1", idle, Animator::Body, true, 0, 0);
    };
    standing.onFinished = [&](const Animator::Slot& s) { done.push_back(s.name); };
    CHECK(standing.play("standup", testTurn(0, 1, {{"Bip01", 0.01f}}), Animator::Body, true, 1, 0.3f));
    standing.update(0.8f);
    CHECK(standing.find("idle1") && done.empty());
    standing.update(0.3f);
    CHECK(done.size() == 1 && done[0] == "standup" && standing.base() && standing.base()->name == "idle1");
    // A weight replaces the mask's: a light gesture.
    CHECK(an.play("fanger", gesture, Animator::Body, false, 1, 0, 0.02f));
    CHECK(std::abs(an.find("fanger")->mask[1] - 0.02f) < 1e-6f);
    // ... before the bones inherit: a base played at a weight leaves the
    // jaw's master out, as its parent's is no longer 0.01.
    CHECK(an.play("walk", testTurn(0, 1, {{"Bip01", 0.01f}}), Animator::Body, true, 0, 0.3f, 0.5f));
    CHECK(an.find("walk")->mask[2] == 0.5f && an.find("walk")->mask[3] == 0);
    // The pose's mask: the model's, inherited down; it multiplies what
    // plays over the base, not the base.
    Model seated = m;
    seated.masks["sit"] = {{"Bip01 Spine", 1.0f}, {"Bip01 Head", 0.0f}};
    const std::vector<float> sit = Animator::modelMask(seated, "SIT");
    CHECK(sit.size() == 4 && sit[0] == 0 && sit[1] == 1 && sit[2] == 1 && sit[3] == 1);
    CHECK(Animator::modelMask(seated, "head").empty());
    Animator sitting(seated);
    sitting.setPoseMask(sit);
    CHECK(sitting.play("sitidle1", idle, Animator::Body, true, 0, 0));
    CHECK(sitting.play("bow", testTurn(30, 1, {{"Bip01", 0.99f}}), Animator::Body, false, 1, 0));
    CHECK(sitting.find("sitidle1")->mask[0] == 0.01f);
    CHECK(sitting.find("bow")->mask[0] == 0 && std::abs(sitting.find("bow")->mask[1] - 0.99f) < 1e-6f);
    // The head's layer: pulled toward its pose by mask times ease.
    Animator head(m);
    head.play("idle1", testTurn(0, 2, {{"Bip01", 0.01f}}), Animator::Body, true, 0, 0);
    head.play("headleft", testTurn(90, 1, {{"Bip01 Head", 0.99f}}), Animator::Head, false, 0, 0.5f, 0.5f);
    head.update(1.0f);
    head.pose(p);
    CHECK(std::abs(boneTurn(p, 2) - 45) < 1.0f && std::abs(boneTurn(p, 1)) < 0.1f);
    // Ten at most.
    Animator full(m);
    for (int i = 0; i < Animator::kSlots; i++)
        CHECK(full.play("g" + std::to_string(i), gesture, Animator::Body, false, 0, 0));
    CHECK(!full.play("more", gesture, Animator::Body, false, 0, 0));
}

void testActor() {
    CHECK(Actor::isBase("idle1") && Actor::isBase("sitidle1") && !Actor::isBase("talkidle1"));
    CHECK(Actor::isBase("walk") && Actor::isBase("rotatel90") && !Actor::isBase("agree") && !Actor::isBase("idle2"));
    Model m = testSkeleton();
    m.masks["sit"] = {{"Bip01 Spine", 1.0f}};
    std::map<std::string, std::shared_ptr<Animation>> files = {
        {"idle1", testTurn(0, 2, {{"Bip01", 0.01f}})},
        {"bow", testTurn(30, 1, {{"Bip01", 0.99f}})},
        {"sitidle1", testTurn(5, 2, {{"Bip01", 0.01f}})},
        {"worried1", testTurn(20, 1, {{"Bip01 Spine", 0.99f}})},
        {"headleft", testTurn(90, 1, {{"Bip01 Head", 0.99f}})},
        {"headright", testTurn(-90, 1, {{"Bip01 Head", 0.99f}})},
    };
    std::vector<std::string> asked;
    auto loader = [&](const std::string& n) -> std::shared_ptr<const Animation> {
        asked.push_back(n);
        const auto it = files.find(n);
        return it == files.end() ? nullptr : it->second;
    };
    // A renamed animation (ANIM's list) and MODL's prefix.
    Ac2Record npc{"NPCX", {}, {}};
    Ac2Record anim{"ANIM", {}, {}};
    Ac2Value named;
    named.type = Ac2Value::Type::TextPairs;
    named.pairs.push_back({"", "worried01 / worried1"});
    anim.fields.push_back({"named", named});
    npc.parts.push_back(anim);
    Ac2Record modl{"MODL", {}, {}};
    Ac2Value sit;
    sit.type = Ac2Value::Type::Text;
    sit.text = "sit";
    modl.fields.push_back({"anim2", sit});
    modl.fields.push_back({"anim", sit});
    Ac2Value when;
    when.type = Ac2Value::Type::Text;
    when.text = "seated";
    modl.fields.push_back({"condition", when});
    npc.parts.push_back(modl);
    bool seated = false;
    Actor a(m, &npc, loader, [&](const std::string& c) { return c.empty() || (c == "seated" && seated); });
    CHECK(a.play("worried01", 1, 0) == "worried1");
    CHECK(a.play("idle1", 0, 0) == "idle1");
    seated = true;
    CHECK(a.play("idle1", 0, 0) == "sitidle1");
    // Seated, what plays over the base leaves the legs (MODL's sit mask).
    CHECK(a.play("bow", 1, 0) == "bow" && a.animator().find("bow")->mask[0] == 0);
    seated = false;
    CHECK(a.play("idle1", 0, 0) == "idle1" && a.animator().poseMask().empty());
    CHECK(a.play("bow", 1, 0) == "bow");
    CHECK(std::abs(a.animator().slots().back().mask[0] - 0.99f) < 1e-6f);
    // anim: what it plays as, and its end.
    std::vector<std::string> done;
    a.onAnimDone = [&](const std::string& n) { done.push_back(n); };
    CHECK(a.anim("nothing", 0, 1, 0).empty() && a.anim("worried01", 0, 1, 0) == "worried1");
    float heading = 0;
    for (int i = 0; i < 20; i++)
        a.update(0.1f, heading);
    CHECK(done.size() == 1 && done[0] == "worried1");
    // The head: the shorter way, a degree in 90 of weight; nothing within 2.
    a.turnHead(30, 0, 0);
    const Animator::Slot* h = a.animator().find("headleft");
    CHECK(h && std::abs(h->mask[2] - 30.0f / 90) < 1e-4f);
    a.turnHead(300, 0, 0);
    CHECK(a.animator().find("headright") && !a.animator().find("headleft"));
    a.headHome();
    CHECK(!a.animator().playing("head"));
    a.turnHead(1, 0, 0);
    CHECK(!a.animator().playing("head"));
    // Rotating without turning animations: 200 degrees a second, the
    // shorter way.
    bool turned = false;
    a.onRotateDone = [&] { turned = true; };
    CHECK(!a.rotateTo(1, 0) && a.rotateTo(270, 0));
    a.update(0.25f, heading);
    CHECK(std::abs(heading - 310) < 0.5f && !turned);
    a.update(0.25f, heading);
    CHECK(turned && heading == 270 && !a.rotating());
}

void testSaveGame() {
    SaveGame s;
    s.description = "TEST";
    s.time = "10:41";
    s.date = "11-02-06";
    s.weather = 1;
    s.location = "calais.hall";
    s.scene = "Locations\\Calais.Hall\\Calais.Hall_03_06b.ac2";
    s.actMajor = 6, s.actMinor = 2;
    s.act.postLoad = "scene calais.hall_03 1 0, fade in";
    s.player = SaveGame::Placement{{1, 2, 3}, {0, 1.5f, 0}};
    s.tags = {{"poirotleave01", 0}, {"Sirkeci.Market_02.ac2 dia_cleaver 0", 2}};
    s.timers = {{"hamtimera", 298}};
    s.texts = {{"savedscene", "calais.hall_03.ac2 -1 0"}};
    s.positions.push_back({"Sirkeci.Market_01.ac2", "klaus_model.gr2", {{-1121.6f, 0, -1513.8f}, {0, 2.27f, 0}}});
    s.talks.push_back({"dia_fountain", "01a", 1, 96, 1, 0});
    s.items.push_back({"ticket", 2, 0, 0});
    const std::vector<uint8_t> data = writeSaveGame(s);
    // The tags as the game writes them: "DESC" is the bytes "CSED".
    CHECK(data.size() > 4 && std::memcmp(data.data(), "CSED", 4) == 0);
    SaveGame back;
    CHECK(!readSaveGame(data, back));
    CHECK(back.description == "TEST" && back.date == "11-02-06" && back.weather == 1 && back.actMinor == 2);
    CHECK(back.scene == s.scene && back.act.postLoad == s.act.postLoad && back.player && !back.follower);
    CHECK(back.player->rotation[1] == 1.5f && back.tags.size() == 2 && back.tags[1].second == 2);
    CHECK(back.timers[0].second == 298 && back.texts[0].second == "calais.hall_03.ac2 -1 0");
    CHECK(back.positions.size() == 1 && back.positions[0].character == "klaus_model.gr2");
    CHECK(back.talks.size() == 1 && back.talks[0].thread == 96 && back.talks[0].nest == 0);
    CHECK(back.items.size() == 1 && back.items[0].name == "ticket");
    std::string d, date, time;
    CHECK(!readSaveHeader(data, d, date, time) && d == "TEST" && time == "10:41" && date == "11-02-06");
    // Cut short, or a record it doesn't know: an error.
    CHECK(readSaveGame(std::vector<uint8_t>(data.begin(), data.end() - 3), back));
    std::vector<uint8_t> bad = data;
    std::memcpy(bad.data(), "XXXX", 4);
    CHECK(readSaveGame(bad, back));
    // The thumbnail's BMP decodes again.
    std::vector<uint8_t> rgba(4 * 4 * 4, 0);
    rgba[0] = 255;
    image::Image img;
    CHECK(!image::decode(encodeBmp32(4, 4, rgba), img) && img.w == 4 && img.h == 4 && img.pixel(0, 0)[0] == 255);
}

void testShadows() {
    // The light: the lights' mean, raised by 1500; only those marked
    // shadowOnly if any are.
    Ac2Scene scene;
    auto light = [](float x, float y, float z, uint32_t marked) {
        Ac2Record r{"LITE", {}, {}};
        auto f = [](float v) {
            Ac2Value a;
            a.type = Ac2Value::Type::F32;
            std::memcpy(&a.raw, &v, 4);
            return a;
        };
        r.fields = {{"x", f(x)}, {"y", f(y)}, {"z", f(z)}};
        Ac2Value m;
        m.raw = marked;
        r.fields.push_back({"shadowOnly", m});
        return r;
    };
    scene.records = {light(5, 655, 450, 0), light(-19, 558, -330, 0)};
    std::array<float, 3> l = shadowLight(scene);
    CHECK(std::abs(l[0] + 7) < 1e-3f && std::abs(l[1] - 2106.5f) < 1e-3f && std::abs(l[2] - 60) < 1e-3f);
    scene.records.push_back(light(100, 500, 0, 1));
    l = shadowLight(scene);
    CHECK(l[0] == 100 && l[1] == 2000 && l[2] == 0);
    CHECK(shadowLight(Ac2Scene())[1] == 1500);
    // Flattened onto the floor along the line from the light.
    const float head[3] = {100, 1000, 0}, foot[3] = {30, 0, 40};
    const std::array<float, 3> p = flattenOntoFloor({0, 2000, 0}, head);
    CHECK(std::abs(p[0] - 200) < 1e-3f && p[1] == 0 && p[2] == 0);
    const std::array<float, 3> q = flattenOntoFloor({0, 2000, 0}, foot);
    CHECK(q[0] == 30 && q[2] == 40);
    // The mask: pixel centres inside, either winding, its box kept.
    CoverageMask m(16, 16);
    const float a[2] = {0, 0}, b[2] = {8, 0}, c[2] = {0, 8};
    m.fillTriangle(a, c, b);
    CHECK(m.at(1, 1) && m.at(6, 0) && !m.at(7, 7) && !m.at(9, 1));
    CHECK(m.minX() == 0 && m.minY() == 0 && m.maxX() <= 8 && !m.empty());
    m.clear();
    CHECK(m.empty() && !m.at(1, 1));
    // A rigid mesh's: the light moved by its translation (added to the
    // sum, not the count), flattened onto the plane at its height.
    scene.records.pop_back();
    const float origin[3] = {10, 100, 20};
    l = shadowLight(scene, origin);
    CHECK(std::abs(l[0] + 2) < 1e-3f && std::abs(l[1] - 2156.5f) < 1e-3f && std::abs(l[2] - 70) < 1e-3f);
    const std::array<float, 3> r = flattenOntoPlane({0, 2000, 0}, 100, head);
    CHECK(std::abs(r[0] - 190) < 1e-3f && r[1] == 100 && r[2] == 0);
    // The stencil: depth-tested, shadows writing no depth, bodies their
    // own value and depth, pictures taking it back to 0.
    ShadowStencil st(8, 8);
    auto rect = [&](float x0, float x1, float z, uint8_t ref, bool depth) {
        const float a0[3] = {x0, 0, z}, a1[3] = {x1, 0, z}, a2[3] = {x1, 8, z}, a3[3] = {x0, 8, z};
        st.fillTriangle(a0, a1, a2, ref, depth);
        st.fillTriangle(a0, a2, a3, ref, depth);
    };
    rect(0, 4, 0.2f, 0, true);  // an earlier character's body, on the left
    rect(0, 8, 0.5f, 1, false); // a later shadow, further away
    CHECK(st.at(1, 1) == 0 && st.at(6, 1) == 1);
    rect(0, 8, 0.1f, 1, false); // nearer: on that body
    CHECK(st.at(1, 1) == 1);
    rect(6, 8, 0.3f, 2, true); // the player's body
    CHECK(st.at(7, 1) == 2 && st.at(5, 1) == 1);
    std::vector<uint8_t> cover(64, 0);
    for (int i = 0; i < 32; i++)
        cover[size_t(i)] = 1; // a picture over the top half
    st.reset(cover);
    CHECK(st.at(5, 1) == 0 && st.at(5, 6) == 1 && st.at(7, 6) == 2);
    CoverageMask shadows(8, 8);
    st.select(1, shadows);
    CHECK(shadows.at(5, 6) && shadows.at(1, 6) && !shadows.at(7, 6) && !shadows.at(5, 1));
    st.clear();
    CHECK(st.at(5, 6) == 0);
}

void testLighting() {
    auto num = [](float v) {
        Ac2Value a;
        a.type = Ac2Value::Type::F32;
        std::memcpy(&a.raw, &v, 4);
        return a;
    };
    auto whole = [](uint32_t v) {
        Ac2Value a;
        a.raw = v;
        return a;
    };
    Ac2Scene scene;
    scene.records.push_back(
        {"CAMR", {{"ambientR", num(0.309804f)}, {"ambientG", num(0.5f)}, {"ambientB", num(1)}}, {}});
    // A directional light straight down, a point light out of range, and
    // two that don't light: off, and only for the shadows.
    auto light = [&](uint32_t type, float y, float diffuse) {
        return Ac2Record{"LITE",
                         {{"type", whole(type)}, {"y", num(y)}, {"dirX", num(0)}, {"dirY", num(-2)}, {"dirZ", num(0)},
                          {"diffuseR", num(diffuse)}, {"diffuseG", num(diffuse)}, {"diffuseB", num(diffuse)},
                          {"range", num(50)}, {"att0", num(1)}, {"att1", num(0)}, {"att2", num(0)}},
                         {}};
    };
    scene.records.push_back(light(3, 0, 0.5f));
    scene.records.push_back(light(1, 100, 1));
    scene.records.push_back(light(3, 0, 1));
    scene.records.back().fields.push_back({"on", whole(0)});
    scene.records.push_back(light(3, 0, 1));
    scene.records.back().fields.push_back({"shadowOnly", whole(1)});
    GameState state;
    SceneLighting lit = sceneLighting(scene, state);
    CHECK(lit.lights.size() == 2 && std::abs(lit.ambient[0] - 79 / 255.0f) < 1e-6f && lit.ambient[1] == 127 / 255.0f);
    Material material;
    material.ambient = {0.5f, 0.5f, 0.5f};
    const float origin[3] = {0, 0, 0}, up[3] = {0, 1, 0}, down[3] = {0, -1, 0};
    Rgb c = lightVertex(lit, material, origin, up);
    CHECK(std::abs(c[0] - (79 / 510.0f + 0.5f)) < 1e-5f && c[2] == 1);
    c = lightVertex(lit, material, origin, down);
    CHECK(std::abs(c[0] - 79 / 510.0f) < 1e-5f);
    // The point light in range: 1 / att0 of it.
    const float near[3] = {0, 60, 0};
    c = lightVertex(lit, material, near, up);
    CHECK(std::abs(c[0] - (79 / 510.0f + 0.5f + 1)) < 1e-5f || c[0] == 1);
    // At night outdoors: no lights, a blue ambient.
    scene.records.push_back({"OUTD", {{"value", whole(1)}}, {}});
    state.values["time"] = 1;
    lit = sceneLighting(scene, state);
    CHECK(lit.lights.empty() && lit.ambient[0] == 69 / 255.0f && lit.ambient[2] == 139 / 255.0f);
    CHECK(!nightLighting(scene, 0));
    // The material from a character's fields.
    const Ac2Record npc{"NPCX", {{"ambientR", num(0.498039f)}, {"emissiveB", num(0.2f)}}, {}};
    material = materialOf(npc);
    CHECK(material.ambient[0] == 0.498039f && material.diffuse[1] == 1 && material.emissive[2] == 0.2f && material.lit);
}

int main() {
    testText();
    testAc2();
    testGr2();
    testOodle1();
    testCurves();
    testCompressedCurves();
    testBinkTexture();
    testCamera();
    testWalkGrid();
    testConditions();
    testSprite();
    testParticles();
    testPicking();
    testScript();
    testItemSlots();
    testDialogueBook();
    testDialogue();
    testStory();
    testAnimator();
    testActor();
    testSaveGame();
    testShadows();
    testLighting();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
