// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Textures: palettes and formats, compressed ones, procedural ones (fire,
// water, wave, wet, ice) and animated ones.
//
//   reluva-tests unreal.unit.textures

#include "tests/engines/unreal/support.h"

using namespace reluva;
using namespace reluva::unreal;
using namespace reluva::unreal::test_data;

namespace {

void testTextures() {
    PackageSet set;
    set.addMemory("Walls", ".utx", texturePackage());
    const Package *p = set.get("walls");
    CHECK(p != nullptr);
    if (!p)
        return;
    image::Image img;
    TextureInfo info;
    CHECK(!decodeTexture(set, *p, p->findExport("Brick"), img, &info));
    CHECK(img.w == 4 && img.h == 2 && info.masked && info.format == 0 && info.mipCount == 1);
    CHECK(img.pixel(0, 0)[3] == 0); // colour 0 is see-through
    CHECK(img.pixel(1, 0)[0] == 1 && img.pixel(1, 0)[1] == 254 && img.pixel(1, 0)[3] == 255);
    CHECK(img.pixel(3, 1)[0] == 7 && img.pixel(3, 1)[2] == 7);

    // A map importing it: resolved through the set, by group-less path.
    PackageWriter map;
    const int32_t walls = map.addImport("Core", "Package", 0, "Walls");
    const int32_t ref = map.addImport("Engine", "Texture", walls, "Brick");
    map.addImport("Engine", "Texture", walls, "Missing");
    set.addMemory("Map", ".unr", map.build());
    const Package *m = set.get("Map");
    CHECK(m && m->exports().empty());
    if (!m)
        return;
    const ObjectRef found = set.resolve(*m, ref);
    CHECK(found && found.package == p && found.exportIndex == p->findExport("Brick"));
    CHECK(!set.resolve(*m, -3));
    CHECK(!set.resolve(*m, 0) && !set.resolve(*m, 5));
    CHECK(set.get("Nothing") == nullptr && set.names(".utx") == std::vector<std::string>{"Walls"});
}

void testCompressedTexture() {
    // An empty P8 mipmap, and the pixels in the bHasComp list as DXT1.
    PackageWriter w;
    const int32_t cls = w.addImport("Engine", "Class", 0, "Texture");
    Bytes o;
    tag(o, w, "bHasComp", PropertyType::kBool, 0);
    o.b.back() |= 0x80;
    tag(o, w, "CompFormat", PropertyType::kByte, 1);
    o.u8(3);
    endProps(o, w);
    o.index(1);
    o.i32(0), o.index(0), o.i32(4), o.i32(4), o.u8(2), o.u8(2);
    o.index(1);
    o.i32(0), o.index(8);
    o.u16(0xf800), o.u16(0x001f), o.u32(0x55555555); // red and blue; every pixel index 1 (blue)
    o.i32(4), o.i32(4), o.u8(2), o.u8(2);
    w.addExport(cls, 0, "Shiny", o.b);
    // One with no pixels at all.
    Bytes empty;
    endProps(empty, w);
    empty.index(1);
    empty.i32(0), empty.index(0), empty.i32(256), empty.i32(256), empty.u8(8), empty.u8(8);
    w.addExport(cls, 0, "Blank", empty.b);
    PackageSet set;
    set.addMemory("Comp", ".utx", w.build());
    const Package *p = set.get("Comp");
    CHECK(p != nullptr);
    if (!p)
        return;
    image::Image img;
    TextureInfo info;
    CHECK(!decodeTexture(set, *p, 0, img, &info));
    CHECK(img.w == 4 && info.format == 3 && img.pixel(2, 3)[2] == 255 && img.pixel(2, 3)[0] == 0);
    const Error err = decodeTexture(set, *p, 1, img);
    CHECK(err && err.message.find("no pixels stored") != std::string::npos);
}

void testProceduralTextures() {
    // A FireTexture and a WetTexture that store no pixels, over the
    // masked P8 texture and palette of texturePackage().
    PackageWriter w;
    const int32_t walls = w.addImport("Core", "Package", 0, "Walls");
    const int32_t brick = w.addImport("Engine", "Texture", walls, "Brick");
    const int32_t pal = w.addImport("Engine", "Palette", walls, "BrickPal");
    const int32_t fire = w.addImport("Fire", "Class", 0, "FireTexture");
    const int32_t wet = w.addImport("Fire", "Class", 0, "WetTexture");
    auto empty = [&](int32_t ref, const char *prop, int width, int height) {
        Bytes o;
        tag(o, w, prop, PropertyType::kObject, 1);
        o.index(ref);
        endProps(o, w);
        o.index(1);
        o.i32(0), o.index(0), o.i32(width), o.i32(height), o.u8(0), o.u8(0);
        return o.b;
    };
    w.addExport(fire, 0, "Flames", empty(pal, "Palette", 2, 4));
    w.addExport(wet, 0, "Ripples", empty(brick, "SourceTexture", 8, 4));
    w.addExport(fire, 0, "Nothing", empty(0, "Palette", 2, 2));
    PackageSet set;
    set.addMemory("Walls", ".utx", texturePackage());
    set.addMemory("Fx", ".utx", w.build());
    const Package *p = set.get("Fx");
    CHECK(p != nullptr);
    if (!p)
        return;
    image::Image img;
    TextureInfo info;
    CHECK(!decodeTexture(set, *p, 0, img, &info));
    CHECK(img.w == 2 && img.h == 4 && info.standIn.find("FireTexture") == 0);
    CHECK(img.pixel(0, 0)[0] == 0 && img.pixel(1, 3)[0] == 255 && img.pixel(1, 3)[3] == 255); // dark to bright
    CHECK(!decodeTexture(set, *p, 1, img, &info));
    CHECK(img.w == 8 && img.h == 4 && info.standIn.find("Walls.Brick") != std::string::npos);
    CHECK(img.pixel(2, 0)[0] == 1 && img.pixel(7, 3)[0] == 7); // Brick's pixels, scaled up
    const Error err = decodeTexture(set, *p, 2, img, &info);
    CHECK(err && err.message.find("no stand-in") != std::string::npos);
}

void testAnimatedTextures() {
    // A FliTexture of two 4x2 frames and the ring frame back to the
    // first, a FireTexture with one spark, and a WaterTexture with a drop,
    // all through texturePackage()'s palette (red = the index).
    PackageWriter w;
    const int32_t walls = w.addImport("Core", "Package", 0, "Walls");
    const int32_t pal = w.addImport("Engine", "Palette", walls, "BrickPal");
    const int32_t fli = w.addImport("DS9", "Class", 0, "FliTexture");
    const int32_t fire = w.addImport("Fire", "Class", 0, "FireTexture");
    const int32_t water = w.addImport("Fire", "Class", 0, "WaterTexture");
    const int32_t ice = w.addImport("Fire", "Class", 0, "IceTexture");
    const int32_t brick = w.addImport("Engine", "Texture", walls, "Brick");
    auto start = [&](Bytes &o) {
        tag(o, w, "Palette", PropertyType::kObject, 1);
        o.index(pal);
    };
    auto mips = [](Bytes &o, int width, int height) {
        o.index(1);
        o.i32(0), o.index(0), o.i32(width), o.i32(height), o.u8(0), o.u8(0);
    };
    auto frame = [](Bytes &out, const std::vector<std::pair<int, std::vector<uint8_t>>> &chunks) {
        Bytes f;
        for (const auto &[type, data] : chunks) {
            f.u32(uint32_t(data.size() + 6));
            f.u16(type);
            for (uint8_t b : data)
                f.u8(b);
        }
        out.u32(uint32_t(f.b.size() + 16 + 2)); // padded by 2, as some of The Fallen's are
        out.u16(0xf1fa);
        out.u16(int(chunks.size()));
        for (int i = 0; i < 8; ++i)
            out.u8(0);
        out.b.insert(out.b.end(), f.b.begin(), f.b.end());
        out.u16(0);
    };
    Bytes flc;
    frame(flc, {{16, {0, 1, 2, 3, 4, 5, 6, 7}}}); // the picture as is
    frame(flc, {{7, {1, 0, 1, 0, 1, 1, 9, 9}}});  // line 0: skip 1, a word of 9s
    frame(flc, {{7, {1, 0, 1, 0, 1, 1, 1, 2}}});  // and back
    Bytes f;
    start(f);
    endProps(f, w);
    mips(f, 4, 2);
    f.f32(2.0f / 30), f.f32(1), f.u16(2), f.u16(4), f.u16(2), f.u16(0), f.u32(uint32_t(flc.b.size()));
    f.b.insert(f.b.end(), flc.b.begin(), flc.b.end());
    w.addExport(fli, 0, "Blink", f.b);
    Bytes fb;
    start(fb);
    endProps(fb, w);
    mips(fb, 8, 8);
    fb.index(1);
    for (int b : {2, 200, 3, 4, 0, 0, 0, 0}) // a Pulse with D 0: 200 at its spot each step
        fb.u8(b);
    w.addExport(fire, 0, "Flame", fb.b);
    Bytes wb;
    start(wb);
    tag(wb, w, "NumDrops", PropertyType::kInt, 4);
    wb.i32(1);
    tag(wb, w, "Drops", PropertyType::kStruct, 8, 0, "ADrop");
    for (int b : {1, 200, 2, 2, 0, 0, 0, 16}) // a spot rising and falling
        wb.u8(b);
    endProps(wb, w);
    mips(wb, 8, 8);
    w.addExport(water, 0, "Pool", wb.b);
    // Brick through itself, the source sliding 2 x (132 - 128) pixels a
    // game second; frame-rate synced, each frame a 120th of one: 2 pixels a
    // second at 30 frames.
    Bytes ib;
    tag(ib, w, "SourceTexture", PropertyType::kObject, 1);
    ib.index(brick);
    tag(ib, w, "GlassTexture", PropertyType::kObject, 1);
    ib.index(brick);
    tag(ib, w, "HorizPanSpeed", PropertyType::kByte, 1);
    ib.u8(132);
    tag(ib, w, "MoveIce", PropertyType::kBool, 0);
    ib.b.back() |= 0x80;
    endProps(ib, w);
    mips(ib, 4, 2);
    w.addExport(ice, 0, "Frost", ib.b);
    PackageSet set;
    set.addMemory("Walls", ".utx", texturePackage());
    set.addMemory("Fx", ".utx", w.build());
    const Package *p = set.get("Fx");
    CHECK(p != nullptr);
    if (!p)
        return;
    std::string err;
    auto blink = openAnimatedTexture(set, *p, 0, &err);
    CHECK(blink && err.empty());
    if (blink) {
        CHECK(std::string(blink->kind()) == "FLC" && blink->image().w == 4 && blink->image().h == 2);
        CHECK(blink->image().pixel(1, 0)[0] == 1 && blink->image().pixel(3, 1)[0] == 7);
        CHECK(!blink->advance(0.01f)); // a frame lasts 1/30 s
        CHECK(blink->advance(0.03f) && blink->image().pixel(1, 0)[0] == 9 && blink->image().pixel(2, 0)[0] == 9);
        CHECK(blink->image().pixel(3, 0)[0] == 3);
        CHECK(blink->advance(1.0f / 30) && blink->image().pixel(1, 0)[0] == 1); // the ring frame: the first again
        CHECK(blink->advance(1.0f / 30) && blink->image().pixel(1, 0)[0] == 9);
    }
    auto flame = openAnimatedTexture(set, *p, 1, &err);
    CHECK(flame != nullptr);
    if (flame) {
        CHECK(std::string(flame->kind()) == "fire");
        // Cold until it steps (no warm-up, as the original).
        int warm = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                warm += flame->image().pixel(x, y)[0] > 0;
        CHECK(warm == 0);
        // The spark's heat spread (four pixels averaged, a little taken
        // off) after it's drawn, as Fire.dll steps: warm near its spot,
        // nowhere hotter than it.
        CHECK(flame->advance(0.1f));
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                warm += flame->image().pixel(x, y)[0] > 0;
                CHECK(flame->image().pixel(x, y)[0] <= 200);
            }
        CHECK(warm > 0);
    }
    auto pool = openAnimatedTexture(set, *p, 2, &err);
    CHECK(pool != nullptr);
    if (pool) {
        CHECK(std::string(pool->kind()) == "water" && pool->image().w == 8);
        // Flat until it first steps (Fire.dll starts the field at 128).
        std::vector<uint8_t> before = pool->image().rgba;
        bool flat = true;
        for (size_t i = 4; i < before.size(); ++i)
            flat = flat && before[i] == before[i % 4];
        CHECK(flat);
        CHECK(pool->advance(0.1f) && pool->image().rgba != before);
    }
    auto frost = openAnimatedTexture(set, *p, 3, &err);
    CHECK(frost != nullptr);
    if (frost) {
        CHECK(std::string(frost->kind()) == "ice" && frost->image().w == 4 && frost->image().h == 2);
        auto run = [&](float seconds) {
            for (int k = 0; k < int(seconds * 30 + 0.5f); ++k)
                frost->advance(1.0f / 30);
        };
        run(0.5f);
        const std::vector<uint8_t> start = frost->image().rgba;
        run(1);
        CHECK(frost->image().rgba != start); // slid a pixel
        run(3);
        CHECK(frost->image().rgba == start); // the whole width: as it was
    }
    // A plain texture isn't one.
    CHECK(!isAnimatedTextureClass("Texture") && isAnimatedTextureClass("WetTexture"));
    // As a still picture, the FLC's first frame.
    image::Image img;
    TextureInfo info;
    CHECK(!decodeTexture(set, *p, 0, img, &info));
    CHECK(img.w == 4 && img.pixel(1, 0)[0] == 1 && info.standIn.find("FLC") != std::string::npos);
}

} // namespace

static const reluva::test::Suite
    kSuite("unreal.unit.textures",
           {
               {"textures", [](const std::filesystem::path &) { testTextures(); }},
               {"compressed texture", [](const std::filesystem::path &) { testCompressedTexture(); }},
               {"procedural textures", [](const std::filesystem::path &) { testProceduralTextures(); }},
               {"animated textures", [](const std::filesystem::path &) { testAnimatedTextures(); }},
           });
