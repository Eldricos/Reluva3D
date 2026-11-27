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
};

} // namespace reluva::storm3d
