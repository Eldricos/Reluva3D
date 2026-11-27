// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: .3do models read and their nodes placed, .key
// animations and .pup puppets.
//
//   reluva-tests sith.unit.models

#include "engines/sith/formats/keyframe.h"
#include "engines/sith/formats/model.h"
#include "engines/sith/formats/puppet.h"
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

void testKeyframes() {
    Keyframes key;
    CHECK(!parseKeyframes(testing::makeKeyframeFile(), key));
    CHECK(key.frames == 10 && near(key.fps, 15) && key.joints == 2);
    CHECK(key.markers.size() == 1 && near(key.markers[0].frame, 4) && key.markers[0].type == 1);
    CHECK(key.nodes.size() == 2 && key.nodes[1].meshName == "arm node" && key.nodes[0].entries.size() == 2);
    if (key.nodes.size() != 2)
        return;
    // Between keyframes, each value moves by its change a frame; from the
    // next keyframe on, by that one's.
    Vec3 p;
    Vec3 r;
    CHECK(key.at(key.nodes[0], 2.5f, p, r) && near(p.z, 2.25f));
    CHECK(key.at(key.nodes[0], 7, p, r) && near(p.z, 2.5f));
    CHECK(key.at(key.nodes[1], 5, p, r) && near(r.y, 370) && near(p.x, 1));

    std::vector<uint8_t> cut = testing::makeKeyframeFile();
    cut.resize(cut.size() - 20);
    CHECK(parseKeyframes(cut, key));

    // What follows the nodes NODES gives isn't read, as in the game.
    std::vector<uint8_t> extra = testing::makeKeyframeFile();
    for (char c : std::string("0\r\nanything\r\n"))
        extra.push_back(uint8_t(c));
    CHECK(!parseKeyframes(extra, key) && key.nodes.size() == 2);
}

void testPuppet() {
    const std::string text = "# Puppet control file\r\n"
                             "Mode=0\t\t# Default mode\r\n"
                             "stand\t\tkystand0.key\t0x00\t0\t0\r\n"
                             "walk\t\tkywalkF.key\t0x01\t2\t2\t# submode 1\r\n"
                             "Mode=1, BasedOn=0\r\n"
                             "stand\t\tkystand2.key\t0x00\t0\t0\r\n"
                             "mode=2, basedon=1\t\t\r\n"
                             "fire\t\tkyfire.key\t0x28\t1\t3\r\n"
                             "joints\r\n0=15\t# head\r\n2=17\r\nend\r\n"
                             "after\tignored.key\t0\t0\t0\r\n";
    Puppet pup;
    CHECK(!parsePuppet(std::vector<uint8_t>(text.begin(), text.end()), pup));
    CHECK(pup.modes.size() == 3 && pup.modes[2].basedOn == 1);
    const Puppet::Action *walk = pup.find(2, "WALK");
    CHECK(walk && walk->keyframe == "kywalkF.key" && walk->flags == 1 && walk->highPriority == 2);
    const Puppet::Action *stand = pup.find(2, "stand");
    CHECK(stand && stand->keyframe == "kystand2.key");
    CHECK(pup.find(0, "fire") == nullptr);
    CHECK(pup.joints.size() == 2 && pup.joints[0] == 15 && pup.joints[2] == 17);

    const std::string bad = "Mode=0\r\nstand kystand0.key 0x00 zero 0\r\n";
    CHECK(parsePuppet(std::vector<uint8_t>(bad.begin(), bad.end()), pup));
}

} // namespace

static const reluva::test::Suite kSuite("sith.unit.models",
                                        {
                                            {"read", [](const std::filesystem::path &) { testRead(); }},
                                            {"pose", [](const std::filesystem::path &) { testPose(); }},
                                            {"keyframes", [](const std::filesystem::path &) { testKeyframes(); }},
                                            {"puppet", [](const std::filesystem::path &) { testPuppet(); }},
                                        });
