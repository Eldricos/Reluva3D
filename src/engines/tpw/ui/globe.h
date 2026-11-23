// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/tpw/ui/front_end.h"
#include "graphics/soft3d/rasterizer.h"
#include "graphics/soft3d/view3d.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * The front end's globe: the part of the camera path's model under its
 * `baseobject`, risen out of the table at the path's end, the three
 * worlds' rides on it, each world a wedge about its upright axis with its
 * gate in the middle. It turns about that axis. See
 * docs/engines/tpw/games/simcoaster/systems/globe.md.
 */
class Globe {
public:
    static constexpr int kWorlds = 3;

    /** A world on the globe: its name's number in THEMENAMES.str and its folder. */
    struct World {
        int theme;
        const char *folder;
        const char *prefix; // its pieces' names start so: "ns02(gate)"
    };
    static const std::array<World, kWorlds> &worlds();

    /** Finds the globe and its worlds' gates in the scene, which must outlive it. */
    Error load(const FrontEndScene &scene);

    /** How far the globe is turned (radians) when a world's gate faces the camera. */
    float facing(int world) const;

    /**
     * The camera over the globe: `approach` 0 is where the path leaves
     * it, 1 the close view the globe turns under.
     */
    soft3d::OrbitCamera camera(float approach, int w, int h) const;

    /** The camera flown from the close view (0) down to a world's gate (1), the globe turned by `turn`. */
    soft3d::OrbitCamera cameraToGate(int world, float turn, float t, int w, int h) const;

    /** Draws the scene over the whole of an RGBA32 surface, the globe turned by `turn` radians. */
    void draw(Surface &screen, soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &camera, float turn) const;

    /** The world drawn at a point of a screen of w × h, the globe turned by `turn`; −1 for none. */
    int worldAt(int x, int y, const soft3d::OrbitCamera &camera, float turn, int w, int h) const;

private:
    std::vector<gfx::Mat4> turned(float turn) const;

    const FrontEndScene *_scene = nullptr;
    std::vector<gfx::Mat4> _rest;                       // each mesh's place at the path's end
    std::vector<std::vector<gfx::Vec3>> _restPositions; // and its positions
    std::vector<int> _worldOf;                          // each mesh's world, or −1 off the globe
    gfx::Vec3 _centre{};
    float _radius = 1;
    std::array<gfx::Vec3, kWorlds> _gates{};
};

/**
 * The globe as the front end shows it once the camera's path has ended:
 * the camera coming down to the close view, the globe turning, by a clock
 * from the path's end.
 */
class GlobeTurning {
public:
    /** `globe` must outlive it; `start` is when the path ended, in System::getMillis()'s. */
    GlobeTurning(const Globe &globe, uint64_t start) : _globe(&globe), _start(start) {}

    const Globe &globe() const { return *_globe; }
    /** How far the globe is turned at a time, in radians. */
    float turn(uint64_t now) const;
    /** The camera at a time. */
    soft3d::OrbitCamera camera(uint64_t now, int w, int h) const;
    /** Draws the scene at a time over the whole of an RGBA32 surface. */
    void draw(Surface &screen, soft3d::Rasterizer &rasterizer, uint64_t now) const;

private:
    const Globe *_globe;
    uint64_t _start;
};

/**
 * Choosing a world on the globe, after the Personnel File: the globe
 * turns on until an open world is clicked (`open`, by Globe::worlds()'
 * order), then turns that world to the camera, which flies down to its
 * gate, "- PLEASE WAIT -" over it.
 * @return The world chosen, by Globe::worlds()' order; −1 if the window
 *     was closed (or the launcher asked for) meanwhile.
 */
int chooseWorld(System &sys, const SearchSet &files, const GlobeTurning &turning,
                const std::array<bool, Globe::kWorlds> &open);

} // namespace reluva::tpw
