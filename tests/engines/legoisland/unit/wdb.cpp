// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island's unit tests: WORLD.WDB (docs/engines/legoisland/formats/wdb.md),
// read from one built here, laid out as the CD's is.

#include "common/stream.h"
#include "engines/legoisland/formats/wdb.h"

#include "tests/support/test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::legoisland;

namespace {

using Bytes = std::vector<uint8_t>;

void put8(Bytes &b, uint8_t v) {
    b.push_back(v);
}

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

// A string after its length; the worlds' strings count a NUL at the end.
void putStr(Bytes &b, const std::string &s, bool nul = false) {
    put32(b, uint32_t(s.size() + (nul ? 1 : 0)));
    b.insert(b.end(), s.begin(), s.end());
    if (nul)
        b.push_back(0);
}

void putImage(Bytes &b, uint32_t w, uint32_t h, uint8_t colors) {
    put32(b, w);
    put32(b, h);
    put32(b, colors);
    for (uint8_t c = 0; c < colors; c++) {
        put8(b, c);
        put8(b, uint8_t(c * 2));
        put8(b, uint8_t(c * 3));
    }
    for (uint32_t i = 0; i < w * h; i++)
        put8(b, uint8_t(i % colors));
}

void append(Bytes &b, const Bytes &more) {
    b.insert(b.end(), more.begin(), more.end());
}

// One LOD: a square of 4 points, 2 normals and 4 texture coordinates, as
// two triangles sharing 2 corners, textured; and an untextured mesh.
Bytes lod(uint32_t flags) {
    Bytes b;
    put32(b, flags);
    put32(b, 2); // meshes
    put16(b, 4); // vertices
    put16(b, 4); // twice the 2 normals
    put32(b, 4); // texture coordinates
    const float square[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    for (const auto &v : square)
        for (float f : v)
            putF(b, f);
    for (int n = 0; n < 2; n++) {
        putF(b, 0);
        putF(b, 0);
        putF(b, n ? -1.0f : 1.0f);
    }
    for (const auto &v : square) {
        putF(b, v[0]);
        putF(b, v[1]);
    }
    // Two triangles: corners 0, 1, 2 new, then 3 new and back to 0 and 2.
    put16(b, 2);
    put16(b, 4);
    const uint16_t entries[6][2] = {{0, 0x8000}, {1, 0x8000}, {2, 0x8000}, {3, 0x8001}, {0, 0}, {2, 2}};
    for (const auto &e : entries) {
        put16(b, e[0]);
        put16(b, e[1]);
    }
    // Texture coordinates for each triangle corner: the new corners' own,
    // the back-references' corners again.
    put32(b, 6);
    for (uint32_t t : {3u, 2u, 1u, 0u, 0u, 2u})
        put32(b, t);
    for (uint8_t c : {255, 255, 255})
        put8(b, c);
    putF(b, 0);
    put8(b, 1); // smooth
    for (uint8_t u : {0, 1, 0})
        put8(b, u);
    putStr(b, "TEX.GIF");
    putStr(b, "PIZZA PIE");

    put16(b, 1);
    put16(b, 3);
    for (uint16_t v : {0, 1, 3}) {
        put16(b, v);
        put16(b, 0x8000);
    }
    put32(b, 0);
    for (uint8_t c : {0x92, 0, 0})
        put8(b, c);
    putF(b, 0.6f);
    put8(b, 0); // flat
    for (uint8_t u : {10, 0, 0})
        put8(b, u);
    putStr(b, "");
    putStr(b, "INH LEGO RED F");
    return b;
}

// A part of one ROI, its two LODs, and a texture.
Bytes part(const std::string &roi) {
    Bytes rois;
    putStr(rois, roi);
    put32(rois, 2); // LODs
    Bytes lods;
    append(lods, lod(8));
    append(lods, lod(0));
    // The ROIs' end, from the part's start: after its offset to the
    // textures, its count of ROIs, and this offset.
    const uint32_t end = uint32_t(8 + rois.size() + 4 + lods.size());
    put32(rois, end);
    append(rois, lods);
    Bytes b;
    put32(b, end);
    put32(b, 1);
    append(b, rois);
    put32(b, 1);
    putStr(b, "TEX.GIF");
    putImage(b, 4, 2, 3);
    return b;
}

void putKey(Bytes &b, uint32_t time, uint8_t flags, std::initializer_list<float> values) {
    put32(b, time | uint32_t(flags) << 24);
    for (float v : values)
        putF(b, v);
}

// A model: an animation placing a child, and ROIs: its own, one with a
// LOD of no meshes, and one taking the part's.
Bytes model() {
    Bytes b;
    put32(b, 19);
    put32(b, 0); // the textures' offset, below
    put32(b, 1);
    put32(b, 0);
    put32(b, 0);
    // The animation: ROOT, and CHILD moved and turned, shown at 500 ms.
    putStr(b, "ROOT");
    for (int k = 0; k < 4; k++)
        put16(b, 0);
    put32(b, 1);
    putStr(b, "CHILD");
    put16(b, 2);
    putKey(b, 0, 1, {1, 2, 3});
    putKey(b, 1000, 1, {4, 5, 6});
    put16(b, 1);
    putKey(b, 0, 1, {1, 0, 0, 0});
    put16(b, 0);
    put16(b, 1);
    put32(b, 500);
    put8(b, 1);
    put32(b, 0);

    // The ROIs: "root", nothing of its own, with three children.
    auto roiHead = [&](const std::string &name, float radius, const std::string &indirect, bool usesPart) {
        putStr(b, name);
        for (float f : {0.0f, 0.0f, 0.0f, radius, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f})
            putF(b, f);
        putStr(b, indirect);
        put8(b, usesPart);
    };
    roiHead("root", 0.001f, "", false);
    put32(b, 0); // LODs
    put32(b, 3); // children
    roiHead("child", 1.5f, "indir-f-child", false);
    put32(b, 1);
    const Bytes own = lod(0);
    put32(b, uint32_t(b.size() + 4 + own.size()));
    append(b, own);
    put32(b, 0);
    roiHead("cam", 0.001f, "", false);
    put32(b, 1);
    put32(b, uint32_t(b.size() + 4 + 8));
    put32(b, 0); // an empty LOD: its flags and no meshes
    put32(b, 0);
    put32(b, 0);
    roiHead("wheel01", 0.5f, "", true);
    put32(b, 0);

    const uint32_t textures = uint32_t(b.size());
    std::memcpy(&b[4], &textures, 4);
    put32(b, 1);
    put32(b, 0);
    putStr(b, "^ROCK.GIF");
    putImage(b, 2, 2, 2);
    putImage(b, 4, 4, 5);
    return b;
}

Bytes buildWdb() {
    // The directory first, its offsets filled in once the rest is laid out.
    auto directory = [](uint32_t partAt, uint32_t partSize, uint32_t modelAt, uint32_t modelSize) {
        Bytes b;
        put32(b, 1);
        putStr(b, "ISLE", true);
        put32(b, 1);
        putStr(b, "wheel", true);
        put32(b, partSize);
        put32(b, partAt);
        put32(b, 1);
        putStr(b, "car", true);
        put32(b, modelSize);
        put32(b, modelAt);
        putStr(b, "LegoEntityPresenter", true);
        for (float f : {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f})
            putF(b, f);
        put8(b, 1);
        return b;
    };
    Bytes shared;
    Bytes texList;
    put32(texList, 2);
    putStr(texList, "face.gif");
    putImage(texList, 2, 2, 4);
    putStr(texList, "^chest.gif");
    putImage(texList, 2, 1, 2);
    putImage(texList, 4, 2, 3);
    put32(shared, uint32_t(texList.size()));
    append(shared, texList);
    const Bytes sharedPart = part("BODY");
    put32(shared, uint32_t(sharedPart.size()));
    append(shared, sharedPart);

    const Bytes m = model();
    const size_t dirSize = directory(0, 0, 0, 0).size();
    const size_t partAt = dirSize + shared.size();
    const Bytes p = part("WHEEL");
    Bytes file = directory(uint32_t(partAt), uint32_t(p.size()), uint32_t(partAt + p.size()), uint32_t(m.size()));
    append(file, shared);
    append(file, p);
    append(file, m);
    return file;
}

void checkLod(const WdbLod &lod) {
    CHECK(lod.vertices.size() == 4 && lod.normals.size() == 2 && lod.texCoords.size() == 4);
    CHECK(lod.meshes.size() == 2);
    if (lod.meshes.size() != 2)
        return;
    const WdbMesh &tex = lod.meshes[0];
    CHECK(tex.corners.size() == 4 && tex.triangles == std::vector<uint16_t>({0, 1, 2, 3, 0, 2}));
    CHECK(tex.corners.size() == 4 && tex.corners[3].vertex == 3 && tex.corners[3].normal == 1);
    CHECK(tex.corners.size() == 4 && tex.corners[0].texCoord == 3 && tex.corners[3].texCoord == 0);
    CHECK(tex.texture == "TEX.GIF" && tex.material == "PIZZA PIE" && tex.shading == 1 && tex.unknown[1] == 1);
    const WdbMesh &plain = lod.meshes[1];
    CHECK(plain.triangles.size() == 3 && plain.corners[2].texCoord == WdbCorner::kNone);
    CHECK(plain.color[0] == 0x92 && plain.transparency > 0.59f && plain.shading == 0 && plain.unknown[0] == 10);
    CHECK(plain.texture.empty() && plain.material == "INH LEGO RED F");
}

void testWorlds(const std::filesystem::path &tmp) {
    CHECK(writeFile(tmp / "WORLD.WDB", buildWdb()));
    WdbFile wdb;
    CHECK(!wdb.open(tmp / "WORLD.WDB"));
    CHECK(wdb.worlds().size() == 1);
    if (wdb.worlds().size() != 1)
        return;
    const WdbWorld &w = wdb.worlds()[0];
    CHECK(w.name == "ISLE" && w.parts.size() == 1 && w.models.size() == 1);
    CHECK(w.parts[0].name == "wheel" && w.models[0].name == "car" && w.models[0].presenter == "LegoEntityPresenter");
    CHECK(w.models[0].location[2] == 3.0f && w.models[0].direction[2] == 1.0f && w.models[0].up[1] == 1.0f);
    CHECK(w.models[0].flag == 1);

    CHECK(wdb.sharedTextures().size() == 2);
    if (wdb.sharedTextures().size() == 2) {
        const WdbTexture &face = wdb.sharedTextures()[0];
        CHECK(face.name == "face.gif" && face.images.size() == 1 && face.images[0].palette.size() == 12);
        // A `^` name has a second picture.
        const WdbTexture &chest = wdb.sharedTextures()[1];
        CHECK(chest.images.size() == 2 && chest.images[1].width == 4 && chest.images[1].pixels.size() == 8);
        std::vector<uint8_t> rgba;
        face.images[0].toRGBA(rgba);
        CHECK(rgba.size() == 16 && rgba[4 * 3 + 1] == 6 && rgba[4 * 3 + 3] == 255);
    }
    CHECK(wdb.sharedPart().rois.size() == 1 && wdb.sharedPart().textures.size() == 1);
    CHECK(wdb.allParts().size() == 1 && wdb.allModels().size() == 1);
}

void testPart(const std::filesystem::path &tmp) {
    CHECK(writeFile(tmp / "WORLD.WDB", buildWdb()));
    WdbFile wdb;
    if (wdb.open(tmp / "WORLD.WDB") || wdb.worlds().empty())
        return;
    WdbPart part;
    CHECK(!wdb.readPart(wdb.worlds()[0].parts[0], part));
    CHECK(part.rois.size() == 1 && part.textures.size() == 1);
    if (part.rois.size() != 1)
        return;
    const WdbRoi &roi = part.rois[0];
    CHECK(roi.name == "WHEEL" && roi.lods.size() == 2 && roi.children.empty());
    if (roi.lods.size() == 2) {
        CHECK(roi.lods[0].flags == 8 && roi.lods[1].flags == 0);
        checkLod(roi.lods[1]);
    }
}

void testModel(const std::filesystem::path &tmp) {
    CHECK(writeFile(tmp / "WORLD.WDB", buildWdb()));
    WdbFile wdb;
    if (wdb.open(tmp / "WORLD.WDB") || wdb.worlds().empty())
        return;
    WdbModel m;
    CHECK(!wdb.readModel(wdb.worlds()[0].models[0], m));
    CHECK(m.version == 19 && m.animHeader[0] == 1);
    CHECK(m.animation.name == "ROOT" && m.animation.children.size() == 1);
    if (m.animation.children.size() == 1) {
        const WdbAnimNode &child = m.animation.children[0];
        CHECK(child.translation.size() == 2 && child.translation[1].time == 1000 && child.translation[1].flags == 1);
        CHECK(child.translation.size() == 2 && child.translation[1].value[2] == 6.0f);
        CHECK(child.rotation.size() == 1 && child.rotation[0].value[0] == 1.0f);
        CHECK(child.morph.size() == 1 && child.morph[0].time == 500 && child.morph[0].value[0] == 1.0f);
    }
    CHECK(m.roi.name == "root" && m.roi.lods.empty() && m.roi.children.size() == 3);
    if (m.roi.children.size() == 3) {
        const WdbRoi &child = m.roi.children[0];
        CHECK(child.indirectName == "indir-f-child" && child.sphereRadius == 1.5f && child.boxMax[0] == 1.0f);
        CHECK(child.lods.size() == 1);
        if (child.lods.size() == 1)
            checkLod(child.lods[0]);
        // A LOD of no meshes has no counts of points.
        CHECK(m.roi.children[1].lods.size() == 1 && m.roi.children[1].lods[0].meshes.empty());
        const WdbRoi &wheel = m.roi.children[2];
        CHECK(wheel.usesPart && wheel.lods.empty() && wheel.partName() == "wheel");
    }
    CHECK(m.textures.size() == 1 && m.textures[0].name == "^ROCK.GIF" && m.textures[0].images.size() == 2);
}

void testBroken(const std::filesystem::path &tmp) {
    Bytes file = buildWdb();
    // Cut short: the shared part runs past the end.
    Bytes cut(file.begin(), file.begin() + std::ptrdiff_t(file.size() / 3));
    CHECK(writeFile(tmp / "cut.wdb", cut));
    WdbFile wdb;
    CHECK(bool(wdb.open(tmp / "cut.wdb")));
    CHECK(bool(wdb.open(tmp / "missing.wdb")));

    // A model whose ROIs don't end where it says its textures start.
    CHECK(!wdb.load(file));
    if (wdb.worlds().empty())
        return;
    WdbModelRef ref = wdb.worlds()[0].models[0];
    file[ref.offset + 4] += 1;
    CHECK(!wdb.load(file));
    WdbModel m;
    CHECK(bool(wdb.readModel(ref, m)));
    ref.size += 100;
    CHECK(bool(wdb.readModel(ref, m)));
}

} // namespace

static const reluva::test::Suite kSuite("legoisland.unit.wdb",
                                        {
                                            {"worlds", [](const std::filesystem::path &t) { testWorlds(t); }},
                                            {"part", [](const std::filesystem::path &t) { testPart(t); }},
                                            {"model", [](const std::filesystem::path &t) { testModel(t); }},
                                            {"broken", [](const std::filesystem::path &t) { testBroken(t); }},
                                        });
