// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: models (.sod) in each layout, on models written
// here, and how the Models page places their nodes and picks a level of
// detail.

#include "engines/storm3d/formats/sod.h"
#include "engines/storm3d/pages/model_page.h"
#include "tests/engines/storm3d/sod_writer.h"

#include "tests/support/test.h"

#include <cmath>

using namespace reluva;
using reluva::storm3d::ModelPage;
using reluva::storm3d::SodModel;
namespace sodtest = reluva::storm3d::testing;

namespace {

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-5f;
}

// 1.8: nodes of each type, a mesh with its drawing mode, keys, and a
// texture animation's value.
void testV18() {
    SodModel m;
    CHECK(!m.parse(sodtest::modelV18("Bbase")));
    CHECK(m.magic == "Storm3D_SW" && near(m.version, 1.8f));
    CHECK(m.textureRefs.empty() && m.materials.size() == 1 && m.materials[0].diffuse[1] == 0.8f);
    CHECK(m.nodes.size() == 4 && m.meshes.size() == 1);
    const SodModel::Node *square = m.node("square");
    CHECK(square && square->mesh == 0 && square->parent == "root" && square->transform[9] == 2);
    const SodModel::Mesh &mesh = m.meshes[0];
    CHECK(mesh.drawMode == "default" && mesh.texture == "Bbase" && mesh.vertices.size() == 4 && mesh.uvs.size() == 4);
    CHECK(mesh.groups.size() == 1 && mesh.groups[0].material == "hull" && mesh.groups[0].faces.size() == 2);
    CHECK(mesh.groups[0].faces[1].vertex[2] == 3 && mesh.flag == 1);
    CHECK(m.node("redstrobe1") && m.node("redstrobe1")->type == SodModel::kSprite);
    CHECK(m.node("steam") && m.node("steam")->emitter == "steammed");
    CHECK(m.animations.size() == 1 && m.animations[0].keys.size() == 2 && m.animations[0].keys[1][10] == 1);
    CHECK(m.textureAnimations.size() == 1 && m.textureAnimations[0].animation == "tex1x4" &&
          m.textureAnimations[0].value == 0.5f);
}

// 1.6: the texture table, no drawing mode, a mesh's list of vertices.
void testV16() {
    SodModel m;
    CHECK(!m.parse(sodtest::modelV16()));
    CHECK(m.textureRefs.size() == 1 && m.textureRefs[0].texture == "Bbase" && m.textureRefs[0].bytes[0] == 2);
    CHECK(m.meshes.size() == 1 && m.meshes[0].drawMode.empty() && m.meshes[0].vertexList.size() == 4);
    CHECK(m.animations.size() == 1 && m.animations[0].length == 4.0f && m.animations[0].keys.empty());
    CHECK(m.textureAnimations.size() == 1 && m.textureAnimations[0].value == 0);
}

// 1.2: a quad cut into two triangles, as a fan from its first corner.
void testV12() {
    SodModel m;
    CHECK(!m.parse(sodtest::modelV12()));
    CHECK(m.meshes.size() == 1 && m.meshes[0].groups.size() == 1 && m.meshes[0].groups[0].material == "glow");
    const auto &faces = m.meshes[0].groups[0].faces;
    CHECK(faces.size() == 2);
    CHECK(faces.size() == 2 && faces[1].vertex == (std::array<uint16_t, 3>{0, 2, 3}));
    CHECK(m.animations.empty());
}

// 1.0: the meshes named, then the tree; the 4 x 4 matrix's translation
// in its last row.
void testV10() {
    SodModel m;
    CHECK(!m.parse(sodtest::modelV10()));
    CHECK(m.meshes.size() == 2 && m.nodes.size() == 2);
    const SodModel::Node *b = m.node("b");
    CHECK(b && b->mesh == 1 && b->parent == "a" && b->transform[9] == 5 && b->transform[11] == 7);
}

// Damage is refused: another magic, a version not known, a file cut
// short or with bytes left over.
void testRefused() {
    SodModel m;
    std::vector<uint8_t> bad = sodtest::modelV18("Bbase");
    bad[0] = 'X';
    CHECK(m.parse(bad));
    bad = sodtest::modelV18("Bbase");
    bad.pop_back();
    CHECK(m.parse(bad));
    bad = sodtest::modelV18("Bbase");
    bad.push_back(0);
    CHECK(m.parse(bad));
    sodtest::SodWriter w;
    w.header("Storm3D_SW", 2.5f).u16(0).u16(0);
    CHECK(m.parse(w.bytes));
}

// A node's matrix by rows applied to a point as a row, then moved; a
// child placed in its parent.
void testPlacing() {
    // 90 degrees about z: the rows are where x and y go.
    const SodModel::Transform turn = {0, 1, 0, -1, 0, 0, 0, 0, 1, 10, 0, 0};
    const gfx::Vec3 p = gfx::transformPoint(ModelPage::toMatrix(turn), {1, 0, 0});
    CHECK(near(p[0], 10) && near(p[1], 1) && near(p[2], 0));

    SodModel m;
    CHECK(!m.parse(sodtest::modelV18("Bbase")));
    const std::vector<gfx::Mat4> world = ModelPage::worldMatrices(m);
    // redstrobe1 is 1 up in square, which is 2 along in root.
    const gfx::Vec3 strobe = gfx::transformPoint(world[2], {0, 0, 0});
    CHECK(near(strobe[0], 2) && near(strobe[1], 1));
}

// Levels of detail by the lod<n> nodes: a node under another level's is
// hidden.
void testLevels() {
    sodtest::SodWriter w;
    w.header("Storm3D_SW", 1.8f).u16(0).u16(4);
    w.u16(0).str("root").str("").transform(0, 0, 0);
    w.u16(0).str("lod2").str("root").transform(0, 0, 0);
    w.u16(0).str("lod0").str("root").transform(0, 0, 0);
    w.u16(0).str("hull").str("lod2").transform(0, 0, 0);
    w.u16(0).u16(0);
    SodModel m;
    CHECK(!m.parse(w.bytes));
    CHECK((ModelPage::detailLevels(m) == std::vector<int>{0, 2}));
    CHECK(!ModelPage::shownAtLevel(m, 3, 0));
    CHECK(ModelPage::shownAtLevel(m, 3, 2));
    CHECK(ModelPage::shownAtLevel(m, 0, 0));
}

// The parts a ship gets once assimilated: under its node borg, however
// deep; the rest of it isn't.
void testAssimilation() {
    sodtest::SodWriter w;
    w.header("Storm3D_SW", 1.8f).u16(0).u16(5);
    w.u16(0).str("root").str("").transform(0, 0, 0);
    w.u16(0).str("damage").str("root").transform(0, 0, 0);
    w.u16(0).str("Borg").str("damage").transform(0, 0, 0);
    w.u16(0).str("Borgbig").str("Borg").transform(0, 0, 0);
    w.u16(0).str("hull").str("root").transform(0, 0, 0);
    w.u16(0).u16(0);
    SodModel m;
    CHECK(!m.parse(w.bytes));
    CHECK(ModelPage::assimilationPart(m, 2) && ModelPage::assimilationPart(m, 3));
    CHECK(!ModelPage::assimilationPart(m, 1) && !ModelPage::assimilationPart(m, 4));
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.sod",
                                        {
                                            {"1.8", [](const std::filesystem::path &) { testV18(); }},
                                            {"assimilation", [](const std::filesystem::path &) { testAssimilation(); }},
                                            {"1.6", [](const std::filesystem::path &) { testV16(); }},
                                            {"1.2", [](const std::filesystem::path &) { testV12(); }},
                                            {"1.0", [](const std::filesystem::path &) { testV10(); }},
                                            {"refused", [](const std::filesystem::path &) { testRefused(); }},
                                            {"placing", [](const std::filesystem::path &) { testPlacing(); }},
                                            {"levels", [](const std::filesystem::path &) { testLevels(); }},
                                        });
