// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: .3do models read, and their nodes placed.
//
//   reluva-tests sith.unit.models

#include "engines/sith/formats/model.h"
#include "engines/sith/render/draw3d.h"

#include "tests/engines/sith/fixtures.h"
#include "tests/support/test.h"

#include <cmath>
#include <string>

using namespace reluva;
using namespace reluva::sith;

namespace {

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

void testRead() {
    const std::vector<uint8_t> data = testing::makeModelFile();
    Model model;
    CHECK(!parseModel(data, model));
    CHECK(model.materials.size() == 2 && model.materials[1] == "SIDE.MAT");
    CHECK(near(model.radius, 0.5f) && near(model.insertOffset.z, 0.25f));
    CHECK(model.geosets.size() == 2 && model.geosets[0].size() == 2);
    if (model.geosets.size() != 2 || model.geosets[0].size() != 2)
        return;
    const Model::Mesh &box = model.geosets[0][0];
    CHECK(box.name == "salad bar" && box.vertices.size() == 4 && box.texVertices.size() == 4);
    CHECK(box.faces.size() == 2 && box.faces[0].vertices.size() == 4 && box.faces[0].texVertices[3] == 3);
    CHECK(box.faces[1].material == 1 && box.faces[1].texVertices[1] == 2 && near(box.faces[1].normal.z, 1));
    const Model::Mesh &solid = model.geosets[0][1];
    CHECK(solid.texVertices.empty() && solid.faces.size() == 1 && solid.faces[0].texVertices[0] == -1);
    CHECK(model.nodes.size() == 2 && model.nodes[1].name == "arm node" && model.nodes[1].parent == 0);
    CHECK(near(model.nodes[1].rotation.y, 90) && near(model.nodes[1].pivot.x, 0.1f));

    // A face naming a vertex the mesh hasn't.
    std::string text(data.begin(), data.end());
    const size_t at = text.find("0,0 1,1");
    text.replace(at, 3, "9,0");
    CHECK(parseModel(std::vector<uint8_t>(text.begin(), text.end()), model));
}

void testPose() {
    Model model;
    CHECK(!parseModel(testing::makeModelFile(), model));
    const std::vector<Transform> nodes = poseNodes(model);
    CHECK(nodes.size() == 2);
    if (nodes.size() != 2)
        return;
    // The child, 1 along x from its parent at z 2, turned 90 degrees of yaw:
    // its own x points along y.
    const Vec3 origin = nodes[1].apply({0, 0, 0});
    CHECK(near(origin.x, 1) && near(origin.y, 0) && near(origin.z, 2));
    const Vec3 x = nodes[1].turn({1, 0, 0});
    CHECK(near(x.x, 0) && near(x.y, 1));

    // Pitch turns y up into z; roll, about y, turns x down into -z.
    const Transform pitch = Transform::place({}, {90, 0, 0});
    CHECK(near(pitch.turn({0, 1, 0}).z, 1));
    const Transform roll = Transform::place({}, {0, 0, 90});
    CHECK(near(roll.turn({1, 0, 0}).z, -1));

    // A pose in place of the model's own.
    const std::vector<Transform> posed = poseNodes(model, {{Vec3{0, 0, 5}, Vec3{}}});
    CHECK(near(posed[1].apply({0, 0, 0}).z, 5));
}

} // namespace

static const reluva::test::Suite kSuite("sith.unit.models",
                                        {
                                            {"read", [](const std::filesystem::path &) { testRead(); }},
                                            {"pose", [](const std::filesystem::path &) { testPose(); }},
                                        });
