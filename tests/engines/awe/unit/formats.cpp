// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The game's files read: text tables, scenes (.ac2), Granny files and their
// curves, Oodle1 and Bink textures.

#include "tests/engines/awe/fixtures.h"

#include <filesystem>

using namespace reluva;
using namespace reluva::awe;
using namespace reluva::awe::testing;

namespace {

void testText() {
    CHECK(decodeText(utf16File("Act\tWhat")) == "Act\tWhat");
    CHECK(decodeText({'c', 'a', 'f', 0xe9}) == "caf\xc3\xa9");

    const Table t = parseTable(decodeText(utf16File("Act\tPreLoad\tLocation to Load\r\n"
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
    const std::vector<Subtitle> subs = parseSubtitles(
        decodeText(utf16File("Name\tAct\tSpeaker\tLine\tDuration\r\n"
                             "intro\t0\tMoinsieur Bouc \t\"I dined with him, Poirot!\"\t\"26.192, 11.016\"\r\n"
                             "intro\t0\tAntoinette\tMonsieur Poirot!\t\"65.387, 1.580\"\r\n"
                             "\t\t\t\t\r\n")));
    CHECK(subs.size() == 2);
    if (subs.size() == 2) {
        CHECK(subs[0].name == "intro" && subs[0].speaker == "Moinsieur Bouc" &&
              subs[0].line == "I dined with him, Poirot!");
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
    const std::vector<const Ac2Record *> objects = scene.objects();
    CHECK(objects.size() == 2);
    if (objects.size() == 2) {
        CHECK(objects[0]->text("file") == "locations\\test\\backgrounds\\test_01.jpg");
        CHECK(objects[0]->i("lighting") == 1);
        CHECK(objects[0]->field("x") == nullptr); // past its count
        const Ac2Record *sprite = objects[0]->part("SPRT");
        CHECK(sprite && sprite->f("scrollSpeed") == 1000);
        CHECK(objects[1]->f("y") == 639);
        CHECK(objects[1]->i("right") == 322);
        CHECK(objects[1]->text("script") == "yesno quit2 0");
        const Ac2Value *listA = objects[1]->field("listA");
        CHECK(listA && listA->pairs.size() == 1 && listA->pairs[0].second == "b");
        const Ac2Record *hot = objects[1]->part("HOTS");
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
    Bytes s;   // the section
    s.u32(0);  // 0: Name -> "hello"
    s.u32(42); // 4: Value
    s.u32(2);  // 8: Floats count
    s.u32(0);  // 12: Floats -> 16
    s.f32(1.5f);
    s.f32(2.5f);
    const size_t strings = s.d.size(); // 24
    for (const char *str : {"Name", "Value", "Floats", "Real32", "hello"})
        s.d.insert(s.d.end(), str, str + std::strlen(str) + 1);
    while (s.d.size() % 8)
        s.d.push_back(0);
    const size_t type = s.d.size(), floatType = type + 4 * 32;
    auto member = [&](uint32_t t) {
        s.u32(t);
        for (int i = 0; i < 7; i++)
            s.u32(0);
    };
    member(8);  // String
    member(19); // Int32
    member(3);  // ReferenceToArray
    member(0);
    member(10); // Real32
    member(0);
    // Fixups: where in the section, and what it points at.
    const std::vector<std::pair<size_t, size_t>> fixups = {{0, strings + 25},
                                                           {12, 16},
                                                           {type + 4, strings},
                                                           {type + 36, strings + 5},
                                                           {type + 68, strings + 11},
                                                           {type + 72, floatType},
                                                           {floatType + 4, strings + 18}};

    Bytes f;
    for (uint8_t m : {0xb8, 0x67, 0xb0, 0xca, 0xf8, 0x6d, 0xb1, 0x0f, 0x84, 0x72, 0x8c, 0x7e, 0x5e, 0x19, 0x00, 0x1e})
        f.d.push_back(m);
    const uint32_t headerSize = 0x58 + 44;
    f.u32(headerSize);
    f.u32(0), f.u32(0), f.u32(0);
    f.u32(6);    // 0x20: format
    f.u32(0);    // total size, set below
    f.u32(0);    // CRC
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
    for (const auto &[at, to] : fixups)
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
    auto expand = [&](const CurveData &d) {
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
// and GrannyWorldSpaceToWindowSpace give for it (reluva-tool awe camera-check).

} // namespace

static const reluva::test::Suite
    suite("awe.unit.formats", {
                                  {"text", [](const std::filesystem::path &) { testText(); }},
                                  {"ac2", [](const std::filesystem::path &) { testAc2(); }},
                                  {"gr2", [](const std::filesystem::path &) { testGr2(); }},
                                  {"oodle1", [](const std::filesystem::path &) { testOodle1(); }},
                                  {"curves", [](const std::filesystem::path &) { testCurves(); }},
                                  {"compressed curves", [](const std::filesystem::path &) { testCompressedCurves(); }},
                                  {"bink textures", [](const std::filesystem::path &) { testBinkTexture(); }},
                              });
