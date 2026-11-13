// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/idtech3/collision.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace reluva::idtech3::botlib {

// The bots' map of a level (maps/<name>.aas): the space a player can be
// in, cut into convex areas, and how to get from each area to its
// neighbours (walking, jumping, teleporting...), with how long that takes.
// The format and what routing answers were read from the GPL ioquake3 bot
// library (aasfile.h, be_aas_file.c, be_aas_sample.c, be_aas_route.c);
// docs/engines/idtech3/formats/aas.md describes them. This is new code from that
// description.

// How a bot gets from one area to the next.
namespace travel {
constexpr int kInvalid = 1;
constexpr int kWalk = 2;
constexpr int kCrouch = 3;
constexpr int kBarrierJump = 4;
constexpr int kJump = 5;
constexpr int kLadder = 6;
constexpr int kWalkOffLedge = 7;
constexpr int kSwim = 8;
constexpr int kWaterJump = 9;
constexpr int kTeleport = 10;
constexpr int kElevator = 11;
constexpr int kRocketJump = 12;
constexpr int kBfgJump = 13;
constexpr int kGrappleHook = 14;
constexpr int kDoubleJump = 15;
constexpr int kRampJump = 16;
constexpr int kStrafeJump = 17;
constexpr int kJumpPad = 18;
constexpr int kFuncBob = 19;
constexpr int kTypeMask = 0xffffff;
} // namespace travel

// Travel flags: which kinds of travel and which areas a route may use.
namespace tfl {
constexpr int kInvalid = 0x1;
constexpr int kWalk = 0x2;
constexpr int kCrouch = 0x4;
constexpr int kBarrierJump = 0x8;
constexpr int kJump = 0x10;
constexpr int kLadder = 0x20;
constexpr int kWalkOffLedge = 0x80;
constexpr int kSwim = 0x100;
constexpr int kWaterJump = 0x200;
constexpr int kTeleport = 0x400;
constexpr int kElevator = 0x800;
constexpr int kRocketJump = 0x1000;
constexpr int kBfgJump = 0x2000;
constexpr int kGrappleHook = 0x4000;
constexpr int kDoubleJump = 0x8000;
constexpr int kRampJump = 0x10000;
constexpr int kStrafeJump = 0x20000;
constexpr int kJumpPad = 0x40000;
constexpr int kAir = 0x80000;
constexpr int kWater = 0x100000;
constexpr int kSlime = 0x200000;
constexpr int kLava = 0x400000;
constexpr int kDoNotEnter = 0x800000;
constexpr int kFuncBob = 0x1000000;
constexpr int kFlight = 0x2000000;
constexpr int kBridge = 0x4000000;
constexpr int kNotTeam1 = 0x8000000;
constexpr int kNotTeam2 = 0x10000000;
constexpr int kDefault = kWalk | kCrouch | kBarrierJump | kJump | kLadder | kWalkOffLedge | kSwim | kWaterJump |
                         kTeleport | kElevator | kAir | kWater | kJumpPad | kFuncBob;
} // namespace tfl

// What an area holds (its settings' contents) and its flags.
namespace areacontents {
constexpr int kWater = 1;
constexpr int kLava = 2;
constexpr int kSlime = 4;
constexpr int kClusterPortal = 8;
constexpr int kTelePortal = 16;
constexpr int kRoutePortal = 32;
constexpr int kTeleporter = 64;
constexpr int kJumpPad = 128;
constexpr int kDoNotEnter = 256;
constexpr int kViewPortal = 512;
constexpr int kMover = 1024;
constexpr int kNotTeam1 = 2048;
constexpr int kNotTeam2 = 4096;
constexpr int kModelNumShift = 24;
} // namespace areacontents
namespace areaflag {
constexpr int kGrounded = 1;
constexpr int kLadder = 2;
constexpr int kLiquid = 4;
constexpr int kDisabled = 8;
constexpr int kBridge = 16;
} // namespace areaflag
namespace presence {
constexpr int kNone = 1;
constexpr int kNormal = 2;
constexpr int kCrouch = 4;
} // namespace presence
namespace faceflag {
constexpr int kSolid = 1;
constexpr int kLadder = 2;
constexpr int kGround = 4;
constexpr int kGap = 8;
constexpr int kLiquid = 16;
constexpr int kLiquidSurface = 32;
constexpr int kBridge = 64;
} // namespace faceflag

struct AasPlane {
    Vec3f normal{};
    float dist = 0;
    int type = 0;
};
struct AasFace {
    int plane = 0;
    int flags = 0;
    int numEdges = 0;
    int firstEdge = 0;
    int frontArea = 0;
    int backArea = 0;
};
struct AasArea {
    int numFaces = 0;
    int firstFace = 0;
    Vec3f mins{};
    Vec3f maxs{};
    Vec3f center{};
};
struct AasAreaSettings {
    int contents = 0;
    int flags = 0;
    int presence = 0;
    int cluster = 0;
    int clusterArea = 0;
    // its reachabilities
    int numReach = 0;
    int firstReach = 0;
};
// A way from one area into another: from `start` (in this area) to
// `end` (in `area`), of a kind (travel::), taking `time` hundredths of a
// second.
struct AasReach {
    int area = 0;
    int face = 0;
    int edge = 0;
    Vec3f start{};
    Vec3f end{};
    int travelType = 0;
    int time = 0;
};
struct AasNode {
    int plane = 0;
    int children[2] = {}; // > 0 a node, < 0 minus an area, 0 solid
};

class AasWorld {
public:
    // Reads a .aas file (version 4, or 5 with its header scrambled).
    Error load(const std::vector<uint8_t> &data);
    bool loaded() const { return _loaded; }
    int32_t bspChecksum() const { return _bspChecksum; }

    int areaCount() const { return int(_areas.size()); }
    const AasArea &area(int n) const { return _areas[size_t(n)]; }
    const AasAreaSettings &settings(int n) const { return _settings[size_t(n)]; }
    const std::vector<AasReach> &reachabilities() const { return _reach; }
    const std::vector<AasFace> &faces() const { return _faces; }
    const std::vector<int> &faceIndex() const { return _faceIndex; }
    const std::vector<AasPlane> &planes() const { return _planes; }
    bool validArea(int n) const { return n > 0 && n < areaCount(); }

    // The area a point is in, 0 if in solid.
    int pointArea(const Vec3f &point) const;
    // The areas a line passes through, in order, and where it enters
    // each (if `points`), at most `max`.
    int traceAreas(const Vec3f &start, const Vec3f &end, std::vector<int> &areas, std::vector<Vec3f> *points,
                   int max) const;
    // The box of a player standing (presence::Normal) or crouching.
    static void presenceBox(int presenceType, Vec3f &mins, Vec3f &maxs);

    bool grounded(int a) const { return validArea(a) && (settings(a).flags & areaflag::kGrounded); }
    bool swim(int a) const { return validArea(a) && (settings(a).flags & areaflag::kLiquid); }
    bool crouchOnly(int a) const { return validArea(a) && !(settings(a).presence & presence::kNormal); }
    bool ladder(int a) const { return validArea(a) && (settings(a).flags & areaflag::kLadder); }

    static int travelFlagForType(int travelType);
    // The travel flags entering an area needs (its liquid or air, do not
    // enter, a bridge...).
    int contentsTravelFlags(int area) const;
    // Hundredths of a second to cross an area from one point to another
    // (walking, crouching or swimming as the area asks), at least 1.
    int areaTravelTime(int area, const Vec3f &from, const Vec3f &to) const;

    // The quickest way from an area (from `origin` in it, if given) to a
    // goal area using only `travelFlags`: its time and the first
    // reachability to take (an index into reachabilities()). False if
    // there is none. In the goal area already: time 1, reachability 0.
    bool route(int area, const Vec3f *origin, int goalArea, int travelFlags, int &time, int &reach);
    int travelTime(int area, const Vec3f &origin, int goalArea, int travelFlags);
    // Turns an area on or off for routing (doors closing...); returns
    // whether it was on.
    bool enableArea(int area, bool enable);
    // The area's reachabilities after `reach` (an index; 0 for its
    // first), 0 when there are no more.
    int nextReachability(int area, int reach) const;

private:
    // The quickest time from every area to one goal, and the reachability
    // each takes first.
    struct RouteTable {
        std::vector<int> time; // 0: no way
        std::vector<int> reach;
    };
    const RouteTable &table(int goalArea, int travelFlags);
    bool usable(const AasReach &r, int travelFlags) const;

    bool _loaded = false;
    int32_t _bspChecksum = 0;
    std::vector<Vec3f> _vertexes;
    std::vector<AasPlane> _planes;
    std::vector<std::pair<int, int>> _edges;
    std::vector<int> _edgeIndex;
    std::vector<int> _faceIndex;
    std::vector<AasFace> _faces;
    std::vector<AasArea> _areas;
    std::vector<AasAreaSettings> _settings;
    std::vector<AasReach> _reach;
    std::vector<AasNode> _nodes;
    std::vector<int> _reachArea;                       // the area each reachability leaves from
    std::vector<std::vector<int>> _into;               // per area, the reachabilities leading into it
    std::map<std::pair<int, int>, RouteTable> _tables; // by goal area and travel flags
};

} // namespace reluva::idtech3::botlib
