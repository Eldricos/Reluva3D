// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The NetImmerse engine's readers, against files built here following
// docs/engines/netimmerse/games/legoalphateam/formats/, so no game data is needed: .pac
// archives, .DDS sounds, the locale's text, .FIN models and their
// animation, .DDF fonts.
//
//   reluva-tests netimmerse.unit.formats

#include "engines/netimmerse/formats/data.h"
#include "engines/netimmerse/formats/ddf.h"
#include "engines/netimmerse/formats/fin.h"
#include "engines/netimmerse/formats/pac_archive.h"
#include "tests/engines/netimmerse/fixtures.h"
#include "tests/support/test.h"

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
using namespace reluva::netimmerse::testing;


namespace {



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

void testOffsetHighAndSubdirectories(const std::filesystem::path& tmp) {
    // The word before the offset is its high half (DDFileAttr, as
    // MapViewOfFile takes it): 1 puts the data 4 GB on, past this archive.
    PacArchive pac;
    CHECK(pac.open(writeFile(tmp / "high.pac", buildPac({{"PACKED.DAT", "xyz", 0, 1}}))));
    // After the files, the subdirectories' count: none in any archive, so
    // one is refused rather than taken for data.
    std::vector<uint8_t> sub = buildPac({{"A.TXT", "abc"}});
    set32(sub, 20 + 6 + 20, 1);
    CHECK(pac.open(writeFile(tmp / "sub.pac", sub)));
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
    put32(noZero, 0); // no files, and no subdirectories
    noZero.resize(noZero.size() + 26, 'A');
    CHECK(!fails(noZero)); // no files: the name isn't looked at
    set32(noZero, 16, 1);
    CHECK(fails(noZero));
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


void testDdf() {
    DdfFont font;
    CHECK(!font.parse(ddfFile(0)));
    CHECK(font.name == "Arial");
    CHECK(font.first == 'A' && font.last == 'B' && font.pages == 1);
    CHECK(font.lineHeight == 5 && font.descent == 1 && font.ascent() == 4 && font.spaceAdvance == 1);
    CHECK(font.glyph(' ') == nullptr && font.glyph('A') && font.glyph('B')->height() == 2);
    CHECK(font.glyph('A')->left == 1 && font.glyph('A')->right == 3 && font.glyph('A')->width() == 2);
    CHECK(font.measure("A B") == 3 + 1 + 2);
    // A second page at twice the size: its glyphs scaled by half to the line.
    CHECK(!font.parse(ddfFile(1, 2)));
    CHECK(font.pages == 2 && font.pageAscent[1] == 8 && font.pageDescent[1] == 2 && font.pageScale(1) == 0.5);
    CHECK(font.lineHeight == 5 && font.measure("AB") == 3 + 1);
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

static const test::Suite suite("netimmerse.unit.formats", {
    {"pac read", [](const std::filesystem::path& tmp) { testRead(tmp); }},
    {"pac search set", [](const std::filesystem::path& tmp) { testSearchSet(tmp); }},
    {"pac placeholder", [](const std::filesystem::path& tmp) { testPlaceholder(tmp); }},
    {"pac offset high half and subdirectories", [](const std::filesystem::path& tmp) { testOffsetHighAndSubdirectories(tmp); }},
    {"pac damage refused", [](const std::filesystem::path& tmp) { testRejectsDamage(tmp); }},
    {"dds sounds", [](const std::filesystem::path&) { testDds(); }},
    {"locale text", [](const std::filesystem::path&) { testLocaleText(); }},
    {"fin models", [](const std::filesystem::path&) { testFin(); }},
    {"fin animation", [](const std::filesystem::path&) { testFinAnimation(); }},
    {"ddf fonts", [](const std::filesystem::path&) { testDdf(); }},
});
