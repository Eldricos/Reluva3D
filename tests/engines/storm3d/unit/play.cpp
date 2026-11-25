// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: a mission in play, its overview camera moved by
// the keys as the camera in the game's memory moved (camera.md).

#include "engines/storm3d/play/mission_view.h"

#include "tests/support/test.h"

#include <cmath>
#include <set>

using reluva::storm3d::BznMap;
using reluva::storm3d::MissionView;
namespace key = reluva::key;

namespace {

void testKeys() {
    CHECK(MissionView::actionOf(key::kKpPlus) == MissionView::kZoomIn);
    CHECK(MissionView::actionOf(key::kKpMultiply) == MissionView::kTurnRight);
    CHECK(MissionView::actionOf(key::kRight) == MissionView::kScrollRight);
    CHECK(MissionView::actionOf('a') == -1);
}

// The game's memory: + held 0.6 s took the height from 450 to 333.4; −
// stops at 700; * turned it about 17 degrees the smaller way; an arrow
// moves the target along the camera's right, (cos a, −sin a).
void testMoves() {
    MissionView::Overview o;
    o.target = {5751, 39.9f, 1042.8f};
    const MissionView::Overview nearer = MissionView::moved(o, {MissionView::kZoomIn}, 0.6);
    CHECK(std::abs(nearer.height - 333.4f) < 0.1f);
    CHECK(MissionView::moved(nearer, {MissionView::kZoomOut}, 2).height == 700);
    CHECK(MissionView::moved(o, {MissionView::kZoomIn}, 100).height == 10);
    const MissionView::Overview turned = MissionView::moved(nearer, {MissionView::kTurnRight}, 0.6);
    CHECK(std::abs(turned.turn * 180 / 3.14159265f + 17.2f) < 0.2f);
    // Six times the distance behind the target a second, about the 908
    // the game moved in 0.6 s at 333.4 high.
    const MissionView::Overview right = MissionView::moved(turned, {MissionView::kScrollRight}, 0.6);
    const float dx = right.target[0] - turned.target[0];
    const float dz = right.target[2] - turned.target[2];
    CHECK(std::abs(std::sqrt(dx * dx + dz * dz) - 929.5f) < 1);
    CHECK(std::abs(dx / std::sqrt(dx * dx + dz * dz) - std::cos(turned.turn)) < 1e-4f);
    CHECK(dz * std::sin(turned.turn) < 0);
    // Up and down along the way it faces; nothing held, nothing moves.
    const MissionView::Overview up = MissionView::moved(o, {MissionView::kScrollUp}, 1);
    CHECK(up.target[0] == o.target[0] && up.target[2] > o.target[2]);
    const MissionView::Overview still = MissionView::moved(o, {}, 5);
    CHECK(still.height == o.height && still.target == o.target && still.turn == o.turn);
}

// Over the player's Camera_<slot>; the area's middle without one.
void testStart() {
    BznMap map;
    map.area = {0, 6400, 0, 0, 0, 6400};
    BznMap::Path path;
    path.name = "Camera_1";
    path.points = {{5751, 39.9f, 1042.8f}};
    map.paths.push_back(path);
    const MissionView::Overview o = MissionView::startOf(map, 1);
    CHECK(o.target[0] == 5751 && o.target[2] == 1042.8f && o.height == 450 && o.turn == 0);
    CHECK(MissionView::startOf(map, 2).target[0] == 3200);
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.play",
                                        {
                                            {"keys", [](const std::filesystem::path &) { testKeys(); }},
                                            {"moves", [](const std::filesystem::path &) { testMoves(); }},
                                            {"start", [](const std::filesystem::path &) { testStart(); }},
                                        });
