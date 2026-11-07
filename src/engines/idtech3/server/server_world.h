// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

// What the server's world needs of an entity, as its game has it now (read
// from the game's own structure, laid out as that game lays it out).
struct EntityShape {
    Vec3f origin{}, angles{}, mins{}, maxs{};
    int32_t contents = 0;
    int32_t owner = -1; // the entity number of its owner, or -1
    bool bmodel = false;
    int32_t modelIndex = 0; // a brush model's, in the map
};

// The server's side of the world, as Quake III's server keeps it for its
// game: the entities the game links, each where its box last was (with
// the areas and clusters it touches), found again by a tree splitting the
// level in two four times over, as the original's; and moves, points and
// boxes tested against the level and those entities.
//
// Entities are looked at in order of their numbers, whatever the tree
// gives, so which one a move hits first doesn't depend on where it is.
class ServerWorld {
public:
    // Reads entity `n`'s shape now; false if it has none.
    using ShapeReader = std::function<bool(int32_t n, EntityShape& out)>;
    explicit ServerWorld(ShapeReader shape) : shape_(std::move(shape)) {}

    // A level (none: only entities, and no areas); forgets the entities.
    // The portals are the game's doors between areas; `noAreas` says
    // whether every area counts as joined (the original's cm_noAreas).
    void setLevel(const Bsp* map, const CollisionWorld* world, const AreaPortals* portals,
                  std::function<bool()> noAreas = {});
    void setPortals(const AreaPortals* portals) { portals_ = portals; }
    void clear();

    // Links entity `n` where `shape` puts it: its box (`absMin`, `absMax`
    // out: its extent a unit larger all round, a turned brush model's by
    // its farthest corner), areas and clusters kept.
    void link(int32_t n, const EntityShape& shape, Vec3f& absMin, Vec3f& absMax);
    // Linked without a place (a game whose entities can't be read): moves
    // and boxes look at it everywhere.
    void linkUnplaced(int32_t n);
    void unlink(int32_t n);
    const std::set<int32_t>& linked() const { return linked_; }
    bool isLinked(int32_t n) const { return linked_.count(n) != 0; }

    // Where a linked entity is: its box, the one or two areas it touches
    // (-1 if none; two for a door between them), the clusters it touches
    // (those inside the map), and how many leafs it's in.
    struct Placement {
        Vec3f absMin{}, absMax{};
        std::pair<int, int> areas{-1, -1};
        std::vector<int> clusters;
        size_t leafCount = 0;
        bool everywhere = false; // linked unplaced
    };
    const Placement* placement(int32_t n) const;
    // The same worked out for a box (for an entity linked another way).
    Placement place(const Vec3f& absMin, const Vec3f& absMax) const;

    bool areasConnected(int area1, int area2) const;

    // A box (mins, maxs about the point) moved from start to end against
    // the level and the linked entities whose contents are in `mask`, but
    // `pass` and what it owns or owns it (-1: none). `hit` gets the entity
    // stopped at (kWorld, or kNone).
    static constexpr int32_t kWorld = 1022, kNone = 1023;
    TraceResult trace(const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end, int32_t pass,
                      int32_t mask, int32_t& hit, bool entities = true) const;
    // The contents at a point: the level's and the entities' it's in, but
    // `pass`'s.
    int32_t pointContents(const Vec3f& point, int32_t pass) const;
    // The linked entities whose boxes touch [mins, maxs], in order.
    void entitiesInBox(const Vec3f& mins, const Vec3f& maxs, std::vector<int32_t>& out) const;

    // An entity's size as the original's server tells the client game
    // (entityState_t's solid): `brushModel` for a brush model, else for a
    // solid box (contents solid or body) its half width, depth below and
    // height above (+32), a byte each; 0 for what isn't solid.
    static int32_t packedSolid(const EntityShape& shape, int32_t brushModel);

private:
    struct Node {
        int axis = -1; // -1: a leaf of the tree
        float dist = 0;
        std::array<int, 2> children{-1, -1}; // above, below
        std::vector<int32_t> entities;
    };
    void build(int depth, const Vec3f& mins, const Vec3f& maxs);
    void collect(const Vec3f& mins, const Vec3f& maxs, std::vector<int32_t>& out) const;
    void remove(int32_t n);

    ShapeReader shape_;
    const Bsp* map_ = nullptr;
    const CollisionWorld* world_ = nullptr;
    const AreaPortals* portals_ = nullptr;
    std::function<bool()> noAreas_;
    std::vector<Node> nodes_;
    std::set<int32_t> linked_;
    std::map<int32_t, Placement> placements_;
    std::map<int32_t, int> nodeOf_;   // where each placed entity is in the tree
    std::vector<int32_t> unplaced_;   // linked without a place
};

} // namespace reluva::idtech3
