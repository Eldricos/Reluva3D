// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island's unit tests: the data viewer placing an animation's nodes
// in time (docs/engines/legoisland/formats/obj.md), on keys made here.

#include "engines/legoisland/pages/scene.h"

#include "tests/support/test.h"

#include <cmath>

using namespace reluva;
using namespace reluva::legoisland;

namespace {

WdbKey key(uint32_t time, float a, float b, float c, float d = 0) {
    WdbKey k;
    k.time = time;
    k.flags = 1;
    k.value = {a, b, c, d};
    return k;
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

// Before the first key its value, after the last the last's, between two
// the way from one to the other; a turn the shorter way round.
void testKeys(const std::filesystem::path &) {
    const std::vector<WdbKey> moves = {key(1000, 0, 0, 0), key(3000, 4, 2, -2)};
    CHECK(near(keyValue(moves, 0)[0], 0) && near(keyValue(moves, 5000)[0], 4));
    const std::array<float, 4> half = keyValue(moves, 2000);
    CHECK(near(half[0], 2) && near(half[1], 1) && near(half[2], -1));

    // A quarter turn about y, then halfway: an eighth.
    const float s = std::sqrt(0.5f);
    const std::vector<WdbKey> turns = {key(0, 1, 0, 0, 0), key(1000, s, 0, s, 0)};
    const std::array<float, 4> q = keyValue(turns, 500, true);
    CHECK(near(q[0], std::cos(0.3926991f)) && near(q[2], std::sin(0.3926991f)));
    // The same quarter turn written negated goes the same short way.
    const std::vector<WdbKey> flipped = {key(0, 1, 0, 0, 0), key(1000, -s, 0, -s, 0)};
    CHECK(near(std::fabs(keyValue(flipped, 500, true)[2]), std::sin(0.3926991f)));
    CHECK(keyValue({}, 10)[0] == 0);
}

// A child moves with its parent, turned by it; a showing key hides it from its time on.
void testPlaces(const std::filesystem::path &) {
    WdbAnimNode root;
    root.name = "-ROOT";
    WdbAnimNode parent;
    parent.name = "CAR";
    parent.translation = {key(0, 10, 0, 0), key(1000, 20, 0, 0)};
    const float s = std::sqrt(0.5f);
    parent.rotation = {key(0, s, 0, s, 0)}; // a quarter turn about y
    WdbAnimNode child;
    child.name = "WHEEL";
    child.translation = {key(0, 1, 0, 0)};
    child.morph = {key(600, 0, 0, 0)};
    parent.children.push_back(child);
    root.children.push_back(parent);

    NodePlaces places;
    placeNodes(root, 500, places);
    const gfx::Mat4 *car = places.find("car");
    CHECK(car && near((*car)[12], 15));
    const gfx::Mat4 *wheel = places.find("Wheel");
    // One along x, turned a quarter about y, is one along -z.
    CHECK(wheel && near((*wheel)[12], 15) && near((*wheel)[14], -1));
    CHECK(places.isShown("WHEEL") && !places.find("TIRE"));
    placeNodes(root, 700, places);
    CHECK(!places.isShown("wheel") && places.isShown("car"));
    CHECK(findNode(root, "wheel") && findNode(root, "wheel")->name == "WHEEL" && !findNode(root, "tire"));
}

} // namespace

static const reluva::test::Suite kSuite("legoisland.unit.scene",
                                        {
                                            {"keys", [](const std::filesystem::path &t) { testKeys(t); }},
                                            {"places", [](const std::filesystem::path &t) { testPlaces(t); }},
                                        });
