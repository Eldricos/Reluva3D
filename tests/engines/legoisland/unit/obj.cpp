// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island's unit tests: the SI files' 3D objects
// (docs/engines/legoisland/formats/obj.md), built here.

#include "engines/legoisland/formats/gph.h"
#include "engines/legoisland/formats/si.h"
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

void putStr(Bytes &b, const std::string &s) {
    put32(b, uint32_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

// A node with one move key and `children` empty children.
void putNode(Bytes &b, const std::string &name, uint32_t children) {
    putStr(b, name);
    put16(b, 1);
    put32(b, 2000 | 1u << 24);
    for (float f : {1.0f, 2.0f, 3.0f})
        putF(b, f);
    for (int k = 0; k < 3; k++)
        put16(b, 0);
    put32(b, children);
    for (uint32_t i = 0; i < children; i++) {
        putStr(b, name + std::to_string(i));
        for (int k = 0; k < 4; k++)
            put16(b, 0);
        put32(b, 0);
    }
}

Bytes animation(bool camera) {
    Bytes b;
    put32(b, 17);
    for (float f : {11.0f, 12.0f, -2.0f, -58.0f})
        putF(b, f);
    put32(b, camera);
    put32(b, 1);
    put32(b, 2);
    putStr(b, "INFOMAN");
    put32(b, 2);
    putStr(b, "2X4RED");
    put32(b, 1);
    put32(b, 22500);
    if (camera) {
        for (int list = 0; list < 2; list++) {
            put16(b, 1);
            put32(b, 1u << 24);
            for (float f : {4.0f, 5.0f, 6.0f})
                putF(b, f);
        }
        put16(b, 0);
    }
    putNode(b, "-SJS012IN", 2);
    return b;
}

void testAnimation(const std::filesystem::path &) {
    WdbAnimation a;
    CHECK(!parseAnimation(animation(false), a));
    CHECK(a.version == 17 && a.radius == 11.0f && a.center[2] == -58.0f && !a.hasCamera);
    CHECK(a.actors.size() == 2 && a.actors[0].name == "INFOMAN" && a.actors[0].kind == 2 && a.duration == 22500);
    CHECK(a.root.name == "-SJS012IN" && a.root.children.size() == 2 && a.root.translation.size() == 1);
    CHECK(a.root.translation.size() == 1 && a.root.translation[0].time == 2000);

    CHECK(!parseAnimation(animation(true), a));
    CHECK(a.hasCamera && a.cameraEye.size() == 1 && a.cameraTarget.size() == 1 && a.cameraRoll.empty());
    CHECK(a.cameraTarget.size() == 1 && a.cameraTarget[0].value[1] == 5.0f && a.root.children.size() == 2);

    Bytes longer = animation(false);
    longer.push_back(0);
    CHECK(bool(parseAnimation(longer, a)));
}

void testTextures(const std::filesystem::path &) {
    // A name in a field of 16 bytes: the name, a NUL, and old bytes after it.
    Bytes b;
    put32(b, 1);
    put32(b, 16);
    const char field[16] = {'r', 'c', 't', 'a', 'i', 'l', '.', 'g', 'i', 'f', 0, 'x', 2, 0, 'h', 'h'};
    b.insert(b.end(), field, field + 16);
    put32(b, 2);
    put32(b, 2);
    put32(b, 2);
    for (uint8_t v : {10, 20, 30, 40, 50, 60})
        put8(b, v);
    for (uint8_t v : {0, 1, 1, 0})
        put8(b, v);
    std::vector<WdbTexture> textures;
    CHECK(!parseTextures(b, textures));
    CHECK(textures.size() == 1 && textures[0].name == "rctail.gif" && textures[0].images[0].pixels.size() == 4);
    b.pop_back();
    CHECK(bool(parseTextures(b, textures)));
}

void testModel(const std::filesystem::path &) {
    Bytes b;
    put32(b, 19);
    put32(b, 0); // the textures' offset, below
    for (uint32_t v : {1u, 0u, 0u})
        put32(b, v);
    putNode(b, "SHARK", 0);
    putStr(b, "shark");
    for (int i = 0; i < 10; i++)
        putF(b, 0);
    putStr(b, "");
    put8(b, 0);
    put32(b, 0); // LODs
    put32(b, 1); // a child taking a part's
    putStr(b, "shkbody");
    for (int i = 0; i < 10; i++)
        putF(b, 0);
    putStr(b, "");
    put8(b, 1);
    put32(b, 0);
    const uint32_t textures = uint32_t(b.size());
    std::memcpy(&b[4], &textures, 4);
    put32(b, 0);
    put32(b, 0);
    WdbModel m;
    CHECK(!parseModel(b, m));
    CHECK(m.version == 19 && m.roi.name == "shark" && m.roi.children.size() == 1);
    CHECK(m.roi.children.size() == 1 && m.roi.children[0].usesPart && m.roi.children[0].partName() == "shkbody");
}

// A square of two faces sharing an edge, a trigger on one.
Bytes path() {
    Bytes b;
    put16(b, 1); // kinds of trigger
    put16(b, 6); // points
    put16(b, 7); // edges
    put16(b, 2); // faces
    put8(b, 3);
    b.insert(b.end(), {'T', 'R', 'C'});
    put32(b, 0x11FFFFFF);
    const float pts[6][2] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}};
    for (const auto &p : pts) {
        putF(b, p[0]);
        putF(b, 0);
        putF(b, p[1]);
    }
    // Edges: the shared one (1-4) bounds both faces.
    const uint16_t edges[7][3] = {{0, 1, 0}, {1, 4, 0}, {4, 3, 0}, {3, 0, 0}, {1, 2, 1}, {2, 5, 1}, {5, 4, 1}};
    for (int i = 0; i < 7; i++) {
        const bool shared = i == 1;
        put16(b, shared ? 15 : 4);
        put16(b, edges[i][0]);
        put16(b, edges[i][1]);
        put16(b, edges[i][2]);
        put16(b, 0);
        put16(b, 0);
        if (shared) {
            put16(b, 1);
            put16(b, 4);
            put16(b, 6);
        }
        for (float f : {1.0f, 0.0f, 0.0f, 1.0f})
            putF(b, f);
    }
    for (int f = 0; f < 2; f++) {
        put8(b, 4);
        for (uint16_t e : f == 0 ? std::vector<uint16_t>{0, 1, 2, 3} : std::vector<uint16_t>{4, 5, 6, 1})
            put16(b, e);
        put8(b, 3);
        put8(b, 2);
        const std::string name = "EDG10_0" + std::to_string(f + 1);
        put8(b, uint8_t(name.size()));
        b.insert(b.end(), name.begin(), name.end());
        for (int i = 0; i < 4 + 16 + 4; i++)
            putF(b, 0);
        put8(b, f == 0 ? 1 : 0);
        if (f == 0) {
            put16(b, 0);
            put32(b, 200);
            putF(b, 0.5f);
            for (float v : {1.0f, 0.0f, 0.0f})
                putF(b, v);
        }
    }
    return b;
}

void testPath(const std::filesystem::path &) {
    GphFile g;
    CHECK(!g.parse(path()));
    CHECK(g.kinds.size() == 1 && g.kinds[0].name == "TRC" && g.points.size() == 6 && g.edges.size() == 7);
    CHECK(g.edges.size() == 7 && g.edges[1].twoFaces && g.edges[1].second[0] == 1 && !g.edges[0].twoFaces);
    CHECK(g.faces.size() == 2 && g.faces[0].name == "EDG10_01" && g.faces[0].edgePlanes.size() == 4);
    CHECK(g.faces.size() == 2 && g.faces[0].triggers.size() == 1 && g.faces[0].triggers[0].value == 200);
    CHECK(g.faces.size() == 2 && g.faces[0].triggerDirection[0] == 1.0f && g.faces[1].triggers.empty());
    Bytes cut = path();
    cut.pop_back();
    CHECK(bool(g.parse(cut)));
}

// An object's pieces joined, less the 0 every one ends with; its kind by its file's name.
void testObjectData(const std::filesystem::path &) {
    std::vector<SiPiece> pieces(2);
    pieces[0].data = {1, 2};
    pieces[1].data = {3, 0};
    CHECK(objectData(pieces) == Bytes({1, 2, 3}));
    SiObject o;
    o.format = "OBJ";
    o.fileName = "Q:\\Lego\\Media\\Model\\Chptr.MOD";
    CHECK(objectKind(o) == "mod");
    o.fileName = "inline";
    CHECK(objectKind(o).empty());
}

} // namespace

static const reluva::test::Suite kSuite("legoisland.unit.obj",
                                        {
                                            {"animation", [](const std::filesystem::path &t) { testAnimation(t); }},
                                            {"textures", [](const std::filesystem::path &t) { testTextures(t); }},
                                            {"model", [](const std::filesystem::path &t) { testModel(t); }},
                                            {"path", [](const std::filesystem::path &t) { testPath(t); }},
                                            {"object data", [](const std::filesystem::path &t) { testObjectData(t); }},
                                        });
