// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/odf.h"
#include "graphics/gfx3d.h"

#include <optional>
#include <vector>

namespace reluva::storm3d {

/**
 * How a ship flies to a point it's sent to, on the plane: it turns
 * towards it, gathers speed, slows for a sharp turn, and brakes to a stop
 * there. The rates are its physics file's and those fitted to the
 * original's tracks. See
 * docs/engines/storm3d/games/armada/systems/movement.md.
 */
class Movement {
public:
    /** A ship's way of flying, from its physics file (`physicsFile`, "cnstphys.odf"). */
    struct Physics {
        float topSpeed = 50;     // warpSpeed, units a second
        float cornerSpeed = 20;  // impulseSpeed: its most while turning sharply
        float turnRate = 0.72f;  // yawRotationRate, radians a second
        float wideTurn = 0.52f;  // headingCloseEnoughForWarp, radians: past it, a sharp turn
        float warpFrom = 10;     // tooCloseForWarp: nearer its goal than this, no faster than impulse
        float turnFrom = 40;     // tooCloseToTurn: a point behind it and nearer than this, it backs to
        float reverse = 0.6667f; // maxReverseSpeed: backing, of its impulse speed
    };
    /** A ship's physics from its definition, through its `physicsFile`; the defaults if it has none. */
    static Physics physicsOf(const Odf &unit, const Odf::Loader &loader);

    /** Where a ship is and what it's doing. */
    struct Ship {
        gfx::Vec3 at{};
        float heading = 0; // radians from +z towards +x; 0 as the game makes its ships
        float speed = 0;   // units a second, along its heading
        std::optional<gfx::Vec3> goal;
        std::vector<gfx::Vec3> route; // the points it passes on its way, first first
        float waiting = 0;            // seconds before it sets off
        gfx::Vec3 avoiding{};         // its push away from others last frame, as avoid() smooths it
    };
    /** How long a ship sent somewhere waits before it sets off, in seconds. */
    static constexpr float kSetOff = 0.35f;
    /** Something to fly round: its middle, and how far from it a ship's middle must keep with its own radius added. */
    struct Obstacle {
        gfx::Vec3 middle{};
        float radius = 0;
    };
    /**
     * The way from `from` to `to` for a ship of `radius`: past each
     * obstacle the way comes too near, nearest first, on the side away
     * from it, and on from there; empty for the straight line.
     */
    static std::vector<gfx::Vec3> routeAround(const gfx::Vec3 &from, const gfx::Vec3 &to,
                                              const std::vector<Obstacle> &obstacles, float radius);
    /**
     * What it costs to fly over each part of the map, as the program's path
     * planner keeps it (0x4097c0): cells of HIGHEST_QUADTREE_GRID_RESOLUTION
     * (50) over the map and 500 past each edge, each DEFAULT_TERRAIN_VALUE
     * (100) but where something marks a square dearer, or impassable (-1).
     */
    struct Terrain {
        static constexpr float kCell = 50;
        static constexpr int kDefault = 100;
        static constexpr int kImpassable = -1;
        static constexpr int kCannotEnter = 200; // CANNOT_PATH_INTO_COST: dearer, no path goes in
        static constexpr int kOffMap = 90000;    // OFF_MAP_PATHING_COST
        float x0 = 0;
        float z0 = 0;
        int across = 0;
        int down = 0;
        std::vector<int> cost; // by cell, row by row
        /** One over the map's area (x from `x0` to `x1`, z from `z0` to `z1`), 500 more each side off the map. */
        static Terrain over(float x0, float z0, float x1, float z1);
        /** A square `half` each way from `middle` marked at `price`, the dearer kept where squares meet (impassable the dearest). */
        void mark(const gfx::Vec3 &middle, float half, int price);
        /** The cost of the cell under a point; off the grid, off the map. */
        int at(float x, float z) const;
    };
    /**
     * The way across `terrain` from `from` to `to`: an A* search over its
     * cells, a step its length times the cell's cost over the default, into
     * no cell dearer than kCannotEnter; then the corners cut where the
     * straight line costs no more (PERFORM_RUBBER_BANDING).
     * @return the points it passes, `from` left out, its end last: `to`, or
     *         the middle of the reachable cell nearest it if it can't be
     *         reached
     */
    static std::vector<gfx::Vec3> plan(const Terrain &terrain, const gfx::Vec3 &from, const gfx::Vec3 &to);
    /** Another unit a ship keeps clear of: where its middle is, how it moves, and its size. */
    struct Neighbour {
        gfx::Vec3 middle{};
        gfx::Vec3 velocity{}; // units a second
        float radius = 0;
    };
    /**
     * A ship's push away from the others it would come too near, as
     * RTS_CFG.h's collision avoidance has it: each where it'll be within
     * COLLISION_LOOK_AHEAD_TIME (1.4 seconds), too near within
     * COLLISION_SAFETY_MARGIN (1.2) times their sizes together, pushed from
     * by how far inside that it comes, up and down by Y_REPULSION (0.5) of
     * it; kept as OLD_AVOIDANCE_FORCE_WEIGHT (0.7) of the last frame's and
     * the rest of this one's, into `ship.avoiding`, which is returned.
     */
    static gfx::Vec3 avoid(Ship &ship, const gfx::Vec3 &middle, float radius, const std::vector<Neighbour> &others);
    /** Moves a ship on by `seconds` (a frame's, a tenth of a second or less). */
    static void step(Ship &ship, const Physics &physics, double seconds);

    /** RTS_CFG.h's SHIP_COMBAT_FLOOR and SHIP_COMBAT_CEILING: the heights ships keep between. */
    static constexpr float kFloor = 10;
    static constexpr float kCeiling = 67;
    /** The height ships fly at, their goals' (the floor's and ceiling's middle, 38.5). */
    static constexpr float kPlane = (kFloor + kCeiling) / 2;
    /**
     * A ship's height after being pushed up or down by `rise` (units, its
     * avoidance's part, this frame's), going no higher once above the
     * ceiling nor lower once below the floor (0x42c457); and back towards
     * the plane by up to `settle`.
     */
    static float climb(float height, float rise, float settle);
};

} // namespace reluva::storm3d
