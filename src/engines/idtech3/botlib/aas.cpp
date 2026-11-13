// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bots' map of a level (.aas). See aas.h and docs/engines/idtech3/formats/aas.md.

#include "engines/idtech3/botlib/aas.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <queue>

namespace reluva::idtech3::botlib {

namespace {

constexpr int kLumps = 14;
constexpr size_t kHeaderSize = 12 + kLumps * 8;
enum Lump {
    kBBoxes,
    kVertexes,
    kPlanes,
    kEdges,
    kEdgeIndex,
    kFaces,
    kFaceIndex,
    kAreas,
    kAreaSettings,
    kReachability,
    kNodes,
    kPortals,
    kPortalIndex,
    kClusters
};

int32_t i32(const uint8_t *p) {
    return int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
}
float f32(const uint8_t *p) {
    const int32_t bits = i32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
Vec3f vec(const uint8_t *p) {
    return {f32(p), f32(p + 4), f32(p + 8)};
}

float length(const Vec3f &v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

} // namespace

Error AasWorld::load(const std::vector<uint8_t> &data) {
    *this = AasWorld{};
    if (data.size() < kHeaderSize || std::memcmp(data.data(), "EAAS", 4) != 0)
        return Error::fail("Not an AAS file");
    const int32_t version = i32(&data[4]);
    if (version != 4 && version != 5)
        return Error::fail("Unsupported AAS version " + std::to_string(version));
    // Version 5 scrambles the rest of the header: each byte XORed with its
    // position (from the checksum on) times 119, kept to a byte.
    std::vector<uint8_t> header(data.begin(), data.begin() + long(kHeaderSize));
    if (version == 5)
        for (size_t i = 8; i < kHeaderSize; ++i)
            header[i] ^= uint8_t((i - 8) * 119);
    _bspChecksum = i32(&header[8]);
    // Each lump: where it is, its length, and how many records of a size.
    auto lump = [&](int n, size_t recordSize, size_t &count) -> const uint8_t * {
        const int32_t at = i32(&header[12 + size_t(n) * 8]);
        const int32_t len = i32(&header[16 + size_t(n) * 8]);
        count = 0;
        if (at < 0 || len < 0 || size_t(at) + size_t(len) > data.size())
            return nullptr;
        count = size_t(len) / recordSize;
        return data.data() + at;
    };
    size_t n = 0;
    const uint8_t *p = lump(kVertexes, 12, n);
    if (!p && n)
        return Error::fail("AAS lump outside the file");
    for (size_t i = 0; i < n; ++i)
        _vertexes.push_back(vec(p + i * 12));
    if ((p = lump(kPlanes, 20, n)))
        for (size_t i = 0; i < n; ++i)
            _planes.push_back({vec(p + i * 20), f32(p + i * 20 + 12), i32(p + i * 20 + 16)});
    if ((p = lump(kEdges, 8, n)))
        for (size_t i = 0; i < n; ++i)
            _edges.emplace_back(i32(p + i * 8), i32(p + i * 8 + 4));
    if ((p = lump(kEdgeIndex, 4, n)))
        for (size_t i = 0; i < n; ++i)
            _edgeIndex.push_back(i32(p + i * 4));
    if ((p = lump(kFaces, 24, n)))
        for (size_t i = 0; i < n; ++i) {
            const uint8_t *f = p + i * 24;
            _faces.push_back({i32(f), i32(f + 4), i32(f + 8), i32(f + 12), i32(f + 16), i32(f + 20)});
        }
    if ((p = lump(kFaceIndex, 4, n)))
        for (size_t i = 0; i < n; ++i)
            _faceIndex.push_back(i32(p + i * 4));
    if ((p = lump(kAreas, 48, n)))
        for (size_t i = 0; i < n; ++i) {
            const uint8_t *a = p + i * 48; // its own number first
            _areas.push_back({i32(a + 4), i32(a + 8), vec(a + 12), vec(a + 24), vec(a + 36)});
        }
    if ((p = lump(kAreaSettings, 28, n)))
        for (size_t i = 0; i < n; ++i) {
            const uint8_t *s = p + i * 28;
            _settings.push_back({i32(s), i32(s + 4), i32(s + 8), i32(s + 12), i32(s + 16), i32(s + 20), i32(s + 24)});
        }
    if ((p = lump(kReachability, 44, n)))
        for (size_t i = 0; i < n; ++i) {
            const uint8_t *r = p + i * 44;
            _reach.push_back({i32(r), i32(r + 4), i32(r + 8), vec(r + 12), vec(r + 24), i32(r + 36),
                              int(uint16_t(r[40] | r[41] << 8))});
        }
    if ((p = lump(kNodes, 12, n)))
        for (size_t i = 0; i < n; ++i)
            _nodes.push_back({i32(p + i * 12), {i32(p + i * 12 + 4), i32(p + i * 12 + 8)}});
    if (_areas.empty() || _settings.size() < _areas.size() || _nodes.size() < 2)
        return Error::fail("AAS file without areas");

    // Check the numbers the rest relies on.
    for (const AasNode &node : _nodes)
        if (node.plane < 0 || size_t(node.plane) >= _planes.size() || node.children[0] >= int(_nodes.size()) ||
            node.children[1] >= int(_nodes.size()) || -node.children[0] >= areaCount() ||
            -node.children[1] >= areaCount())
            return Error::fail("AAS node out of range");
    _reachArea.assign(_reach.size(), 0);
    _into.assign(_areas.size(), {});
    for (int a = 0; a < areaCount(); ++a) {
        const AasAreaSettings &s = _settings[size_t(a)];
        if (s.numReach < 0 || s.firstReach < 0 || size_t(s.firstReach) + size_t(s.numReach) > _reach.size())
            return Error::fail("AAS area reachabilities out of range");
        for (int r = s.firstReach; r < s.firstReach + s.numReach; ++r) {
            if (!validArea(_reach[size_t(r)].area))
                return Error::fail("AAS reachability into an area out of range");
            _reachArea[size_t(r)] = a;
            _into[size_t(_reach[size_t(r)].area)].push_back(r);
        }
    }
    _loaded = true;
    return Error::success();
}

int AasWorld::pointArea(const Vec3f &point) const {
    if (!_loaded)
        return 0;
    // Down the tree from node 1 (0 stands for solid): the front child when
    // in front of the node's plane, else the back one, to an area.
    int node = 1;
    while (node > 0) {
        const AasNode &n = _nodes[size_t(node)];
        const AasPlane &pl = _planes[size_t(n.plane)];
        const float d = point[0] * pl.normal[0] + point[1] * pl.normal[1] + point[2] * pl.normal[2] - pl.dist;
        node = n.children[d > 0 ? 0 : 1];
    }
    return -node;
}

int AasWorld::traceAreas(const Vec3f &start, const Vec3f &end, std::vector<int> &areas, std::vector<Vec3f> *points,
                         int max) const {
    areas.clear();
    if (points)
        points->clear();
    if (!_loaded || max <= 0)
        return 0;
    // The line split by each node's plane, the part nearer the start
    // followed first, so the areas come in order.
    std::function<bool(int, const Vec3f &, const Vec3f &)> walk = [&](int node, const Vec3f &s, const Vec3f &e) {
        if (node < 0) {
            areas.push_back(-node);
            if (points)
                points->push_back(s);
            return int(areas.size()) < max;
        }
        if (node == 0)
            return true;
        const AasNode &n = _nodes[size_t(node)];
        const AasPlane &pl = _planes[size_t(n.plane)];
        const float front = s[0] * pl.normal[0] + s[1] * pl.normal[1] + s[2] * pl.normal[2] - pl.dist;
        const float back = e[0] * pl.normal[0] + e[1] * pl.normal[1] + e[2] * pl.normal[2] - pl.dist;
        if (front > 0 && back > 0)
            return walk(n.children[0], s, e);
        if (front <= 0 && back <= 0)
            return walk(n.children[1], s, e);
        const float f = std::clamp(front / (front - back), 0.0f, 1.0f);
        const Vec3f mid{s[0] + (e[0] - s[0]) * f, s[1] + (e[1] - s[1]) * f, s[2] + (e[2] - s[2]) * f};
        const int near = front > 0 ? 0 : 1;
        return walk(n.children[near], s, mid) && walk(n.children[1 - near], mid, e);
    };
    walk(1, start, end);
    return int(areas.size());
}

void AasWorld::presenceBox(int presenceType, Vec3f &mins, Vec3f &maxs) {
    mins = {-15, -15, -24};
    maxs = {15, 15, presenceType == presence::kNormal ? 32.0f : 8.0f};
}

int AasWorld::travelFlagForType(int travelType) {
    static const int kFlags[] = {tfl::kInvalid,    tfl::kInvalid,  tfl::kWalk,         tfl::kCrouch,  tfl::kBarrierJump,
                                 tfl::kJump,       tfl::kLadder,   tfl::kWalkOffLedge, tfl::kSwim,    tfl::kWaterJump,
                                 tfl::kTeleport,   tfl::kElevator, tfl::kRocketJump,   tfl::kBfgJump, tfl::kGrappleHook,
                                 tfl::kDoubleJump, tfl::kRampJump, tfl::kStrafeJump,   tfl::kJumpPad, tfl::kFuncBob};
    int result = 0;
    // The team flags above the type.
    if (travelType & (1 << 24))
        result |= tfl::kNotTeam1;
    if (travelType & (2 << 24))
        result |= tfl::kNotTeam2;
    const int type = travelType & travel::kTypeMask;
    return result | (type >= 0 && type < int(std::size(kFlags)) ? kFlags[type] : tfl::kInvalid);
}

int AasWorld::contentsTravelFlags(int area) const {
    if (!validArea(area))
        return 0;
    const AasAreaSettings &s = settings(area);
    int flags = s.contents & areacontents::kWater   ? tfl::kWater
                : s.contents & areacontents::kSlime ? tfl::kSlime
                : s.contents & areacontents::kLava  ? tfl::kLava
                                                    : tfl::kAir;
    if (s.contents & areacontents::kDoNotEnter)
        flags |= tfl::kDoNotEnter;
    if (s.contents & areacontents::kNotTeam1)
        flags |= tfl::kNotTeam1;
    if (s.contents & areacontents::kNotTeam2)
        flags |= tfl::kNotTeam2;
    if (s.flags & areaflag::kBridge)
        flags |= tfl::kBridge;
    return flags;
}

int AasWorld::areaTravelTime(int area, const Vec3f &from, const Vec3f &to) const {
    // Speeds: walking 300 units a second, crouching 100, swimming
    // (counted as) 100; times in hundredths of a second.
    const Vec3f d{to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    const float factor = crouchOnly(area) ? 1.3f : swim(area) ? 1.0f : 0.33f;
    return std::max(1, int(length(d) * factor));
}

bool AasWorld::usable(const AasReach &r, int travelFlags) const {
    if (settings(r.area).flags & areaflag::kDisabled)
        return false;
    if (travelFlagForType(r.travelType) & ~travelFlags)
        return false;
    return !(contentsTravelFlags(r.area) & ~travelFlags);
}

const AasWorld::RouteTable &AasWorld::table(int goalArea, int travelFlags) {
    const auto key = std::make_pair(goalArea, travelFlags);
    if (const auto it = _tables.find(key); it != _tables.end())
        return it->second;
    RouteTable &t = _tables[key];
    t.time.assign(_areas.size(), 0);
    t.reach.assign(_areas.size(), -1);
    // Out from the goal along the reachabilities backwards, quickest
    // first. An area's time: the next area's, plus the reachability's,
    // plus crossing the next area from where it arrives to where that
    // area leaves (its own first reachability's start).
    std::vector<Vec3f> leaveAt(_areas.size());
    std::vector<bool> done(_areas.size(), false);
    using Item = std::pair<int, int>; // time, area
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    t.time[size_t(goalArea)] = 1;
    leaveAt[size_t(goalArea)] = area(goalArea).center;
    open.push({1, goalArea});
    while (!open.empty()) {
        const auto [time, a] = open.top();
        open.pop();
        if (done[size_t(a)] || time != t.time[size_t(a)])
            continue;
        done[size_t(a)] = true;
        for (const int r : _into[size_t(a)]) {
            const AasReach &reach = _reach[size_t(r)];
            const int from = _reachArea[size_t(r)];
            if (done[size_t(from)] || !usable(reach, travelFlags) || (settings(from).flags & areaflag::kDisabled))
                continue;
            const int crossing = a == goalArea ? 0 : areaTravelTime(a, reach.end, leaveAt[size_t(a)]);
            const int total = time + reach.time + crossing;
            if (t.time[size_t(from)] == 0 || total < t.time[size_t(from)]) {
                t.time[size_t(from)] = total;
                t.reach[size_t(from)] = r;
                leaveAt[size_t(from)] = reach.start;
                open.push({total, from});
            }
        }
    }
    return t;
}

bool AasWorld::route(int a, const Vec3f *origin, int goalArea, int travelFlags, int &time, int &reach) {
    time = 0;
    reach = 0;
    if (!_loaded)
        return false;
    if (a == goalArea && validArea(a)) {
        time = 1;
        return true;
    }
    if (!validArea(a) || !validArea(goalArea) || !settings(a).numReach || !settings(goalArea).numReach)
        return false;
    if ((settings(a).contents | settings(goalArea).contents) & areacontents::kDoNotEnter)
        travelFlags |= tfl::kDoNotEnter;
    const RouteTable &t = table(goalArea, travelFlags);
    if (!t.time[size_t(a)] || t.reach[size_t(a)] < 0)
        return false;
    reach = t.reach[size_t(a)];
    time = t.time[size_t(a)];
    if (origin)
        time += areaTravelTime(a, *origin, _reach[size_t(reach)].start);
    return true;
}

int AasWorld::travelTime(int a, const Vec3f &origin, int goalArea, int travelFlags) {
    int time = 0;
    int reach = 0;
    return route(a, &origin, goalArea, travelFlags, time, reach) ? time : 0;
}

bool AasWorld::enableArea(int a, bool enable) {
    if (!validArea(a))
        return false;
    int &flags = _settings[size_t(a)].flags;
    const bool was = !(flags & areaflag::kDisabled);
    if (was != enable) {
        flags = enable ? flags & ~areaflag::kDisabled : flags | areaflag::kDisabled;
        _tables.clear();
    }
    return was;
}

int AasWorld::nextReachability(int a, int reach) const {
    if (!validArea(a))
        return 0;
    const AasAreaSettings &s = settings(a);
    if (s.numReach == 0)
        return 0;
    if (reach == 0)
        return s.firstReach;
    if (reach < s.firstReach || reach + 1 >= s.firstReach + s.numReach)
        return 0;
    return reach + 1;
}

} // namespace reluva::idtech3::botlib
