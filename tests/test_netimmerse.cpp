// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the NetImmerse engine's readers against files built here,
// following docs/engines/netimmerse/formats/, so no game data is needed.

#include "engines/netimmerse/data.h"
#include "engines/netimmerse/ddf.h"
#include "engines/netimmerse/fin.h"
#include "engines/netimmerse/pac_archive.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::netimmerse;

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

struct PacFile {
    std::string name;
    std::string data;
    uint64_t modified = 0;
    uint32_t flags = 0;
};

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}
void set32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

// An archive laid out as the retail ones: header, directory, 4 zero
// bytes, then the files in the directory's order.
std::vector<uint8_t> buildPac(const std::vector<PacFile>& files) {
    std::vector<uint8_t> dir;
    std::vector<size_t> offsetAt;
    for (const PacFile& f : files) {
        dir.insert(dir.end(), f.name.begin(), f.name.end());
        dir.push_back(0);
        put32(dir, f.flags);
        offsetAt.push_back(dir.size());
        put32(dir, 0); // offset, set below
        put32(dir, uint32_t(f.data.size()));
        put32(dir, uint32_t(f.modified));
        put32(dir, uint32_t(f.modified >> 32));
    }
    put32(dir, 0);
    const uint32_t dataStart = uint32_t(20 + dir.size());
    uint32_t at = dataStart;
    for (size_t i = 0; i < files.size(); ++i) {
        set32(dir, offsetAt[i], at);
        at += uint32_t(files[i].data.size());
    }
    std::vector<uint8_t> out = {'P', 'A', 'C', 'K'};
    put32(out, at); // the archive's size
    put32(out, 0);
    put32(out, dataStart);
    put32(out, uint32_t(files.size()));
    out.insert(out.end(), dir.begin(), dir.end());
    for (const PacFile& f : files)
        out.insert(out.end(), f.data.begin(), f.data.end());
    return out;
}

std::filesystem::path writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return path;
}

std::string asString(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }

// 2000-08-21 12:00:00 UTC.
constexpr uint64_t kFileTime = 116444736000000000ULL + 966859200ULL * 10000000ULL;

const std::vector<PacFile> kFiles = {
    {"ABADI MT CONDENSED.DDF", "FNT2Abadi", kFileTime, 0},
    {"EMPTY.TXT", "", 0, 0},
    {"M0020.PUZ", "PUZVER 1\r\nNEARFAR 20 10000\r\n", kFileTime + 10000000, 0},
};

void testRead(const std::filesystem::path& tmp) {
    const auto path = writeFile(tmp / "test.pac", buildPac(kFiles));
    PacArchive pac;
    CHECK(!pac.open(path));
    CHECK(pac.entries().size() == 3);
    CHECK(pac.list() == std::vector<std::string>({"ABADI MT CONDENSED.DDF", "EMPTY.TXT", "M0020.PUZ"}));
    CHECK(pac.dataStart() == 20 + (23 + 20) + (10 + 20) + (10 + 20) + 4);

    const PacArchive::Entry* e = pac.entry("abadi mt condensed.ddf");
    CHECK(e && e->offset == pac.dataStart() && e->size == 9 && e->modified == kFileTime);
    CHECK(PacArchive::unixTime(kFileTime) == 966859200);
    CHECK(PacArchive::unixTime(0) == 0);

    std::vector<uint8_t> data;
    CHECK(!pac.read("m0020.puz", data) && asString(data) == kFiles[2].data);
    CHECK(!pac.read("Empty.txt", data) && data.empty());
    CHECK(pac.has("ABADI MT CONDENSED.DDF") && !pac.has("missing.tga"));
    CHECK(pac.read("missing.tga", data));
}

void testSearchSet(const std::filesystem::path& tmp) {
    auto low = std::make_unique<PacArchive>(), high = std::make_unique<PacArchive>();
    CHECK(!low->open(writeFile(tmp / "low.pac", buildPac({{"A.TXT", "low"}, {"B.TXT", "only low"}}))));
    CHECK(!high->open(writeFile(tmp / "high.pac", buildPac({{"A.TXT", "high"}}))));
    SearchSet set;
    set.add(std::move(low), 0);
    set.add(std::move(high), 1);
    std::vector<uint8_t> data;
    CHECK(!set.read("a.txt", data) && asString(data) == "high");
    CHECK(!set.read("b.txt", data) && asString(data) == "only low");
}

void testPlaceholder(const std::filesystem::path& tmp) {
    // An entry with offset and size 0, as Ambient.pac and LMap.pac list
    // themselves: accepted, and read as empty.
    std::vector<uint8_t> bytes = buildPac({{"SELF.PAC", "", 0, 0}, {"E0000.DDS", "sound", kFileTime, 0}});
    set32(bytes, 20 + 9 + 4, 0);
    PacArchive pac;
    CHECK(!pac.open(writeFile(tmp / "self.pac", bytes)));
    std::vector<uint8_t> data{1};
    CHECK(!pac.read("self.pac", data) && data.empty());
    CHECK(!pac.read("e0000.dds", data) && asString(data) == "sound");

    // Offset 0 with data isn't a placeholder: it points into the header.
    std::vector<uint8_t> bad = buildPac({{"A.TXT", "abc"}});
    set32(bad, 20 + 6 + 4, 0);
    CHECK(pac.open(writeFile(tmp / "bad0.pac", bad)));
}

void testFlags(const std::filesystem::path& tmp) {
    // Listed, but not read, since what the flags mean isn't known.
    PacArchive pac;
    CHECK(!pac.open(writeFile(tmp / "flags.pac", buildPac({{"PACKED.DAT", "xyz", 0, 1}}))));
    CHECK(pac.entries().size() == 1 && pac.entries()[0].flags == 1);
    std::vector<uint8_t> data;
    CHECK(pac.read("packed.dat", data));
}

void testRejectsDamage(const std::filesystem::path& tmp) {
    const std::vector<uint8_t> good = buildPac(kFiles);
    auto fails = [&](std::vector<uint8_t> bytes) {
        PacArchive pac;
        return bool(pac.open(writeFile(tmp / "bad.pac", bytes)));
    };
    CHECK(!fails(good));
    CHECK(fails({}));
    CHECK(fails({'P', 'A', 'C', 'K'}));

    std::vector<uint8_t> b = good;
    b[0] = 'X';
    CHECK(fails(b)); // magic

    b = good;
    b.pop_back();
    CHECK(fails(b)); // shorter than the header says

    b = good;
    set32(b, 12, uint32_t(good.size() + 1));
    CHECK(fails(b)); // data start past the end

    b = good;
    set32(b, 16, 1000);
    CHECK(fails(b)); // more files than the directory can hold

    b = good;
    set32(b, 16, 4);
    CHECK(fails(b)); // the fourth entry would be read from the zero bytes and data

    b = good;
    set32(b, 20 + 23 + 4, 5);
    CHECK(fails(b)); // first file's data inside the directory

    b = good;
    set32(b, 20 + 23 + 8, uint32_t(good.size()));
    CHECK(fails(b)); // first file's data past the end

    // A directory whose last name has no terminating zero.
    std::vector<uint8_t> noZero = {'P', 'A', 'C', 'K'};
    put32(noZero, 20 + 30);
    put32(noZero, 0);
    put32(noZero, 20 + 30);
    put32(noZero, 0);
    noZero.resize(noZero.size() + 30, 'A');
    CHECK(!fails(noZero)); // no files: the name isn't looked at
    set32(noZero, 16, 1);
    CHECK(fails(noZero));
}

void putFloat(std::vector<uint8_t>& b, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(b, u);
}

void testDds() {
    // 22,050 Hz stereo: a WAVEFORMATEX, then two frames.
    std::vector<uint8_t> d = {1, 0, 2, 0};
    put32(d, 22050);
    put32(d, 22050 * 4);
    d.insert(d.end(), {4, 0, 16, 0, 0, 0});
    for (int16_t s : {int16_t(1), int16_t(-2), int16_t(300), int16_t(-32768)}) {
        d.push_back(uint8_t(s));
        d.push_back(uint8_t(uint16_t(s) >> 8));
    }
    audio::Sound sound;
    CHECK(!decodeDds(d, sound));
    CHECK(sound.rate == 22050 && sound.channels == 2 && sound.frames() == 2);
    CHECK(sound.samples == std::vector<int16_t>({1, -2, 300, -32768}));

    std::vector<uint8_t> bad = d;
    bad[0] = 2; // not PCM
    CHECK(decodeDds(bad, sound));
    bad = d;
    bad[14] = 8; // 8-bit
    CHECK(decodeDds(bad, sound));
    bad = d;
    bad[8] = 0; // bytes per second disagrees
    CHECK(decodeDds(bad, sound));
    CHECK(decodeDds({1, 0, 1, 0}, sound)); // short
}

void testLocaleText() {
    const auto e = parseLocaleText("####\r\n# Assorted UI Strings\r\n####\r\n\r\n==MenuExitButton==\r\nExit Game\r\n\r\n"
                                   "==Brief==\r\n\r\nLine one\r\n\r\nLine two\r\n\r\n\r\n==Empty==\r\n==Last==\r\nend");
    CHECK(e.size() == 4);
    CHECK(e[0].first == "MenuExitButton" && e[0].second == "Exit Game");
    CHECK(e[1].first == "Brief" && e[1].second == "Line one\n\nLine two");
    CHECK(e[2].first == "Empty" && e[2].second.empty());
    CHECK(e[3].first == "Last" && e[3].second == "end");
}

// A model written as docs/engines/netimmerse/formats/fin.md describes: a LOD node
// (roots) over a node with a collision box, holding a textured triangle.
struct FinWriter {
    std::vector<uint8_t> b;
    void str(const std::string& s) {
        put32(b, uint32_t(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    }
    void object(const std::string& type, uint32_t id) {
        str(type);
        put32(b, id);
    }
    void objectNet(const std::string& name, uint8_t flags = 0) {
        str(name);
        put32(b, 0); // no extra data
        b.push_back(flags);
    }
    void avObject(const std::string& name, std::array<float, 3> t, std::vector<uint32_t> props, bool box,
                  uint8_t flags = 0) {
        objectNet(name, flags);
        for (float f : t)
            putFloat(b, f);
        for (float f : {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 2.f}) // rotation, scale 2
            putFloat(b, f);
        for (int i = 0; i < 3; ++i)
            putFloat(b, 0); // velocity
        put32(b, uint32_t(props.size()));
        for (uint32_t p : props)
            put32(b, p);
        put32(b, 2);
        put32(b, box ? 0x1000 : 0);
        if (box) {
            // A union of a box and a half-space.
            put32(b, 4);
            put32(b, 2);
            put32(b, 1);
            for (int i = 0; i < 15; ++i)
                putFloat(b, 1);
            b.push_back(0);
            put32(b, 5);
            for (int i = 0; i < 4; ++i)
                putFloat(b, 0);
        }
    }
    void nodeTail(std::vector<uint32_t> children) {
        put32(b, 2);
        put32(b, 0);
        b.push_back(1);
        put32(b, uint32_t(children.size()));
        for (uint32_t c : children)
            put32(b, c);
        put32(b, 0); // effects
    }
};

std::vector<uint8_t> buildFin() {
    FinWriter w;
    const std::string header = "Dweezil 23\n";
    w.b.assign(header.begin(), header.end());
    w.object("NiImage", 0x500);
    w.objectNet("");
    w.str("Brick.tga");
    put32(w.b, 7);
    w.object("NiTextureProperty", 0x400);
    w.objectNet("");
    put32(w.b, 0);
    put32(w.b, 1);
    put32(w.b, 0x500);
    w.object("NiMaterialProperty", 0x300);
    w.objectNet("");
    for (int i = 0; i < 12; ++i)
        putFloat(w.b, 0.5f);
    putFloat(w.b, 0.25f);
    putFloat(w.b, 0.75f);
    w.object("NiTriShape", 0x200);
    w.avObject("Tri", {0, 0, 0}, {0x300, 0x400}, false);
    w.b.insert(w.b.end(), {3, 0}); // vertices
    put32(w.b, 0xaaaa);
    for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
        putFloat(w.b, f);
    put32(w.b, 0); // no normals
    for (float f : {0.3f, 0.3f, 0.f, 0.8f})
        putFloat(w.b, f);
    w.b.insert(w.b.end(), {1, 0, 1, 0}); // a triangle, one uv set
    put32(w.b, 0xbbbb);
    for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
        putFloat(w.b, f);
    put32(w.b, 0); // no colours
    put32(w.b, 0xcccc);
    for (int i = 0; i < 4; ++i)
        putFloat(w.b, 0);
    w.b.insert(w.b.end(), {0, 0, 1, 0, 2, 0});
    // A skin: a shape with two vertices on bone 0x100 and one with no
    // influence (as no retail file has, but allowed), no triangles.
    w.object("Ni3dsSkin", 0x600);
    w.avObject("Skin", {0, 0, 0}, {}, false);
    w.b.insert(w.b.end(), {3, 0});
    put32(w.b, 0xdddd);
    for (int i = 0; i < 9; ++i)
        putFloat(w.b, 0);
    put32(w.b, 0); // no normals
    for (int i = 0; i < 4; ++i)
        putFloat(w.b, 0);
    w.b.insert(w.b.end(), {0, 0, 0, 0}); // no triangles, no uv sets
    put32(w.b, 0);
    put32(w.b, 0);
    put32(w.b, 0);
    w.b.insert(w.b.end(), {1, 1});
    for (int v = 0; v < 3; ++v) {
        const uint16_t n = v < 2 ? 1 : 0;
        w.b.insert(w.b.end(), {uint8_t(n), 0});
        if (n) {
            putFloat(w.b, 1);
            for (float f : {float(v), 2.f, 3.f})
                putFloat(w.b, f);
            put32(w.b, 0x100);
        }
    }
    w.object("NiNode", 0x100);
    w.avObject("Holder", {5, 0, 0}, {}, true, 1); // hidden
    w.nodeTail({0x200, 0x600});
    w.str("Top Level Object");
    w.object("NiLODNode", 0x50);
    w.avObject("Lod", {0, 0, 0}, {}, false);
    w.nodeTail({0x100});
    w.b.insert(w.b.end(), 5, 0);
    put32(w.b, 1);
    w.b.insert(w.b.end(), 20 + 1, 0);
    w.str("End Of File");
    return w.b;
}

void testFin() {
    const std::vector<uint8_t> data = buildFin();
    FinFile f;
    CHECK(!f.parse(data));
    CHECK(f.roots() == std::vector<uint32_t>({0x50}));
    CHECK(f.objects().size() == 7);
    for (const auto& o : f.objects())
        CHECK(o.read && o.exact);
    CHECK(f.nodes.size() == 2 && f.nodes[0x50].levelsOfDetail && f.nodes[0x50].children == std::vector<uint32_t>({0x100}));
    CHECK(f.nodes[0x100].name == "Holder" && f.nodes[0x100].translation[0] == 5 && f.nodes[0x100].scale == 2);
    CHECK(f.nodes[0x100].hidden && !f.nodes[0x50].hidden && !f.shapes[0x200].hidden);
    const FinSkin& skin = f.skins[0x600];
    CHECK(skin.vertexCount == 3 && skin.firstInfluence == std::vector<uint32_t>({0, 1, 2, 2}));
    CHECK(skin.influences.size() == 2 && skin.influences[1].bone == 0x100 && skin.influences[1].offset[0] == 1 &&
          skin.influences[1].weight == 1);
    const FinShape& s = f.shapes[0x200];
    CHECK(s.vertexCount == 3 && s.vertices.size() == 9 && s.normals.empty() && s.uvSets == 1 && s.uvw.size() == 9);
    CHECK(s.triangles == std::vector<uint16_t>({0, 1, 2}) && s.properties == std::vector<uint32_t>({0x300, 0x400}));
    CHECK(f.materials[0x300].glossiness == 0.25f && f.materials[0x300].alpha == 0.75f);
    CHECK(f.textures[0x400] == std::vector<uint32_t>({0x500}) && f.images[0x500] == "Brick.tga");
    CHECK(f.typeOf(0x400) == "NiTextureProperty" && f.typeOf(0x999).empty());

    // A wrong header, a missing end, a repeated id.
    std::vector<uint8_t> bad = data;
    bad[0] = 'X';
    CHECK(f.parse(bad));
    bad = data;
    bad.resize(bad.size() - 3);
    CHECK(f.parse(bad));
    bad = data;
    const std::string first = "NiImage", second = "NiTextureProperty";
    const size_t id1 = std::string("Dweezil 23\n").size() + 4 + first.size();
    const size_t id2 = std::string(data.begin(), data.end()).find(second) + second.size();
    std::memcpy(&bad[id2], &bad[id1], 4);
    CHECK(f.parse(bad));

    // A triangle pointing past the vertices: the shape isn't exact.
    bad = data;
    bad[std::string(data.begin(), data.end()).find("Ni3dsSkin") - 4 - 2] = 9;
    CHECK(!f.parse(bad) && !f.objects()[3].exact);
}

} // namespace

// A font with "A" and "B" on one 4x4 page: "A" 2x2 at the top-left, sitting
// on the baseline, drawn one pixel right of the pen (its edges 1 and 3).
std::vector<uint8_t> ddfFile(int bPage) {
    std::vector<uint8_t> d(86 + 2 * 18);
    std::memcpy(d.data(), "FNT2Arial", 9);
    auto s16 = [&](size_t at, int v) {
        d[at] = uint8_t(v);
        d[at + 1] = uint8_t(v >> 8);
    };
    const int metrics[9] = {4, 4, 'A', 'B', 1, 5, 1, 4, 2};
    for (int i = 0; i < 9; ++i)
        s16(68 + size_t(i) * 2, metrics[i]);
    const int glyphs[2][9] = {{0, 0, 2, 0, -1, 3, 3, 0, 0}, {2, 0, 3, -1, 0, 2, 2, 0, bPage}};
    for (int g = 0; g < 2; ++g)
        for (int i = 0; i < 9; ++i)
            s16(86 + size_t(g) * 18 + size_t(i) * 2, glyphs[g][i]);
    return d;
}

void testDdf() {
    DdfFont font;
    CHECK(!font.parse(ddfFile(0)));
    CHECK(font.name == "Arial");
    CHECK(font.first == 'A' && font.last == 'B' && font.pages == 1);
    CHECK(font.lineHeight == 5 && font.descent == 1 && font.ascent() == 4 && font.spaceAdvance == 2);
    CHECK(font.glyph(' ') == nullptr && font.glyph('A') && font.glyph('B')->height() == 2);
    CHECK(font.glyph('A')->left == 1 && font.glyph('A')->right == 3 && font.glyph('A')->width() == 2);
    CHECK(font.measure("A B") == 3 + 2 + 2);
    CHECK(DdfFont::pageName("ARIAL.DDF", 1) == "ARIAL_1.TGA");
    CHECK(font.parse(ddfFile(1))); // "B" on a page it doesn't have
    std::vector<uint8_t> cut = ddfFile(0);
    cut.resize(cut.size() - 1);
    CHECK(font.parse(cut));

    CHECK(!font.parse(ddfFile(0)));
    image::Image page;
    page.w = page.h = 4;
    page.rgba.assign(4 * 4 * 4, 255);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            page.pixel(x, y)[3] = (x < 2 && y < 2) ? 255 : 0;
    Surface screen;
    screen.create(8, 8, PixelFormat::RGBA32);
    CHECK(font.draw(screen, {page}, 0, 0, "A", {200, 0, 0}) == 3);
    // The baseline is 4 down, so "A" (2 above it) fills rows 2-3, columns 1-2.
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            CHECK(screen.row(y)[x * 4] == ((x == 1 || x == 2) && (y == 2 || y == 3) ? 200 : 0));
}

// An animated model: a 3ds node over a bone, a flip-book, an alpha
// animator and the game's actor data with a track for each node.
std::vector<uint8_t> buildAnimatedFin() {
    FinWriter w;
    const std::string header = "Dweezil 23\n";
    w.b.assign(header.begin(), header.end());
    auto floats = [&](std::initializer_list<float> fs) {
        for (float f : fs)
            putFloat(w.b, f);
    };
    w.object("NiFlipTextures", 0x700);
    w.objectNet("", 2); // the first byte of the out-of-bounds type (clamp)
    w.b.insert(w.b.end(), 3, 0);
    floats({0.25f, 160, 1920});
    put32(w.b, 0x400);
    w.object("Ni3dsAlphaAnimator", 0x710);
    w.objectNet("");
    w.b.insert(w.b.end(), {0, 0, 0, 1, 1, 0, 1});
    put32(w.b, 2); // cycle
    put32(w.b, 0);
    floats({1, 0, 0, 3200});
    put32(w.b, 0x300);
    put32(w.b, 2); // two keys of type 1: time, alpha
    put32(w.b, 1);
    floats({0, 1, 3200, 0});
    w.object("DDActorSharedData", 0x720);
    w.str("U0001");
    w.str("");
    w.str("Thing");
    w.str("");
    w.b.insert(w.b.end(), {1, 0});
    floats({0.7f, 0});
    put32(w.b, 1); // a clip, with a sound
    w.str("spin");
    floats({1, 2});
    w.b.push_back(1);
    w.str("whirr");
    // No node; looping, placed, muted beyond its maximum distance; no delay.
    w.b.insert(w.b.end(), {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0});
    floats({0.5f, 20, 1000, 300}); // volume, falloff, distances
    put32(w.b, 2); // its lists: two numbers, then none, none
    w.b.insert(w.b.end(), {0, 0, 1, 0});
    put32(w.b, 0);
    put32(w.b, 0);
    put32(w.b, 2); // tracks
    // The node: a rotation key (type 1: time, angle, axis, quaternion, two
    // numbers), two position keys (type 1), no scale or visibility keys.
    put32(w.b, 1);
    put32(w.b, 1);
    floats({4800, 3.14159f, 0, 0, 1, 0, 0, 0, 1, 0, 0});
    put32(w.b, 2);
    put32(w.b, 1);
    floats({4800, 1, 2, 3, 9600, 4, 5, 6});
    put32(w.b, 0);
    put32(w.b, 0);
    // The bone: Euler angles (one key, type 4, 46 bytes, then a track per
    // axis), a scale key, a visibility key.
    put32(w.b, 1);
    put32(w.b, 4);
    w.b.insert(w.b.end(), 46, 0);
    put32(w.b, 1);
    put32(w.b, 2); // X: one key of type 2 (time, value, two tangents)
    floats({0, 0.5f, 0, 0});
    put32(w.b, 0);
    put32(w.b, 0);
    put32(w.b, 0); // no position keys
    put32(w.b, 1);
    put32(w.b, 2); // a scale key of type 2: time, value, tangents in and out
    floats({0, 2, 0.5f, 0.25f});
    put32(w.b, 1);
    putFloat(w.b, 4800);
    w.b.push_back(0);
    put32(w.b, 1); // the footprint: one corner
    floats({1, 2, 0.1f});
    // A point light: location, direction, switch, spot angle and
    // exponent, dimmer, three colours, attenuation distance, curve and
    // switch, type, no nodes.
    w.object("NiLight", 0x730);
    w.avObject("Lamp", {0, 0, 0}, {}, false);
    floats({0, 0, 0, 0, 0, 0});
    w.b.push_back(1);
    floats({-0.5f, 0, 3, 0.1f, 0.1f, 0.1f, 1, 0.5f, 0, 1, 0.5f, 0, 32000, 0});
    w.b.push_back(1);
    put32(w.b, 2);
    put32(w.b, 0);
    // A morph shape of one vertex: two targets, two keys.
    w.object("Ni3dsMorphShape", 0x740);
    w.avObject("Weed", {0, 0, 0}, {}, false);
    w.b.insert(w.b.end(), {1, 0});
    put32(w.b, 0xeeee);
    floats({0, 0, 0});
    put32(w.b, 0); // no normals
    floats({0, 0, 0, 1});
    w.b.insert(w.b.end(), {0, 0, 0, 0}); // no triangles or uv sets
    put32(w.b, 0);
    put32(w.b, 0);
    put32(w.b, 0);
    w.b.insert(w.b.end(), {1, 0, 0, 0, 1, 0, 0, 1}); // animation type, flags
    put32(w.b, 2);
    put32(w.b, 0);
    floats({1, 0, 0, 3200});
    w.b.insert(w.b.end(), {0, 1}); // normals updated
    put32(w.b, 2); // targets
    put32(w.b, 2); // keys
    floats({0, 0, 0, 1}); // its bounding sphere
    put32(w.b, 5); // NiBaryMorphKey
    for (float t : {0.f, 3200.f}) {
        floats({t, 1, 0.5f, 0, 0, -1, -1});
        put32(w.b, 2);
        floats({1, t > 0 ? 1.f : 0.f, 0, 0.5f, 0, 0.5f}); // weights, in, out
    }
    floats({1, 2, 3, 0, 0, 1}); // the targets' vertices
    w.object("DDUnit", 0x750);
    w.avObject("Unit", {0, 0, 0}, {}, false);
    w.nodeTail({});
    put32(w.b, 0x720);
    w.object("Ni3dsBone", 0x20);
    w.avObject("Bone", {0, 0, 0}, {}, false);
    w.nodeTail({});
    w.b.insert(w.b.end(), 48, 0);
    w.str("Top Level Object");
    w.object("Ni3dsAnimationNode", 0x10);
    w.avObject("Root", {0, 0, 0}, {}, false);
    w.nodeTail({0x20});
    w.b.insert(w.b.end(), 48, 0);
    w.str("End Of File");
    return w.b;
}

void testFinAnimation() {
    FinFile f;
    CHECK(!f.parse(buildAnimatedFin()));
    for (const auto& o : f.objects())
        CHECK(o.read && o.exact);
    CHECK(f.animatedNodes() == std::vector<uint32_t>({0x10, 0x20}));
    const FinFlip& flip = f.flips[0x700];
    CHECK(flip.outOfBounds == FinFlip::Clamp && flip.rate == 0.25f && flip.startTime == 160 && flip.duration == 1920 &&
          flip.target == 0x400);
    // Images as Act gives them: 1920 ticks over 4 images, from 160.
    CHECK(flip.image(0, 4) == 0 && flip.image(160 + 480, 4) == 1 && flip.image(5000, 4) == 3);
    FinFlip loop = flip;
    loop.outOfBounds = FinFlip::Loop;
    CHECK(loop.image(160 + 1920 + 960, 4) == 2 && loop.image(160 - 480, 4) == 3);
    FinFlip reverse = flip;
    reverse.outOfBounds = FinFlip::Reverse;
    CHECK(reverse.image(160 + 1920 + 480, 4) == 3 && reverse.image(160 + 480, 4) == 1);
    const FinMaterialAnimator& a = f.materialAnimators[0x710];
    CHECK(a.alpha && a.target == 0x300 && a.stop == 3200 && a.keys.size() == 2 && a.keys[1].time == 3200 &&
          a.keys[0].value[0] == 1);
    const FinActor& actor = f.actors[0x720];
    CHECK(actor.id == "U0001" && actor.name == "Thing");
    CHECK(actor.clips.size() == 1 && actor.clips[0].name == "spin" && actor.clips[0].start == 1 &&
          actor.clips[0].sound == "whirr" && actor.clips[0].loop && actor.clips[0].volume == 0.5f &&
          actor.clips[0].falloff == 20 && actor.clips[0].maxDistance == 1000 && actor.clips[0].minDistance == 300);
    CHECK(actor.clips[0].soundNode.empty() && !actor.clips[0].unplaced && actor.clips[0].muteBeyondMax &&
          actor.clips[0].delay == 0);
    CHECK(actor.clips[0].tracks == std::vector<uint16_t>({0, 1}) && actor.clips[0].flips.empty() &&
          actor.clips[0].morphs.empty());
    const FinMorph& weed = f.morphs[0x740];
    CHECK(weed.keys.size() == 2 && weed.targets.size() == 2 && weed.keys[1].time == 3200 &&
          weed.keys[0].value == 1 && weed.keys[0].tension == 0.5f && weed.keys[0].derived[0] == -1 &&
          weed.keys[1].weights[1] == 1 && weed.keys[0].in[1] == 0.5f && weed.keys[0].out[1] == 0.5f &&
          weed.targets[1][2] == 1);
    CHECK(f.nodes[0x750].actor == 0x720);
    const FinLight& lamp = f.lights[0x730];
    CHECK(lamp.type == FinLight::Point && lamp.on && lamp.dimmer == 3 && lamp.diffuse[0] == 1 &&
          lamp.attenuated && lamp.attenuationDistance == 32000 && lamp.attenuationCurve == 0 &&
          lamp.illuminated.empty());
    CHECK(actor.tracks.size() == 2 && actor.footprint.size() == 1 && actor.footprint[0][1] == 2);
    const FinActor::Track& node = actor.tracks[0];
    CHECK(node.rotations.size() == 1 && node.rotations[0].time == 4800 && node.rotations[0].value[0] == 0 &&
          node.rotations[0].value[3] == 1);
    CHECK(node.positions.size() == 2 && node.positions[1].value[2] == 6 && node.scales.empty() && node.visible.empty());
    const FinActor::Track& bone = actor.tracks[1];
    CHECK(bone.rotations.empty() && bone.euler[0].size() == 1 && bone.euler[0][0].value == 0.5f && bone.euler[1].empty());
    CHECK(bone.scales.size() == 1 && bone.scales[0].value == 2 && bone.scales[0].curve && bone.scales[0].in == 0.5f &&
          bone.scales[0].out == 0.25f);
    CHECK(!node.positions[0].curve && !node.rotations[0].curve);
    CHECK(bone.visible.size() == 1 && bone.visible[0].time == 4800 && !bone.visible[0].value);
}

int main() {
    const auto tmp = std::filesystem::temp_directory_path() / "reluva_netimmerse_tests";
    std::filesystem::create_directories(tmp);
    testRead(tmp);
    testSearchSet(tmp);
    testPlaceholder(tmp);
    testFlags(tmp);
    testRejectsDamage(tmp);
    testDds();
    testLocaleText();
    testFin();
    testFinAnimation();
    testDdf();
    std::filesystem::remove_all(tmp);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
