// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/gfx3d.h"

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace reluva::idtech3 {

/**
 * Jedi Outcast single player's weather, as its renderer keeps it
 * (docs/engines/idtech3/games/jk2/systems/sp-weather.md): the client game's
 * R_WorldEffectCommand lines, and the rain they start falling round the
 * eye while it's in the level's "outside" brushes.
 */
class WorldEffects {
public:
    // The contents the rain falls in, and what stops it (Jedi Outcast's).
    static constexpr int32_t kContentsOutside = 0x10000;
    static constexpr int32_t kContentsWater = 4;
    // The rain's picture, its red made its alpha (0x48e346).
    static constexpr const char *kRainImage = "gfx/world/rain";

    /** A command line ("rain init 500", "rain alpha 0.3"...); false if not understood. */
    bool command(const std::string &line);
    /** No weather (a level's start). */
    void clear();
    /** Whether rain falls (it was started). */
    bool raining() const { return !_drops.empty(); }
    /**
     * A frame `seconds` long: the wind, the fade in or out by `eyeContents`,
     * the drops falling.
     */
    void update(float seconds, int32_t eyeContents);
    /**
     * The drops as triangles in the world (three vertices each) round the
     * eye `eye`, a drop as wide as `left` (the view's left axis) shows;
     * none while it's faded out.
     */
    void triangles(const std::array<float, 3> &eye, const std::array<float, 3> &left,
                   std::vector<gfx::Vertex> &out) const;
    /** How faded in it is (0 to 1). */
    float fade() const { return _fade; }
    /** Whether it's falling where the eye is (in the open, out of water), as of the last update. */
    bool fallingHere() const { return _fallingHere; }
    /** The wind blowing it now (across, of length up to 1). */
    const std::array<float, 3> &wind() const { return _wind; }
    /** Its wind's lean ("rain angle"). */
    float windAngle() const { return _windAngle; }

private:
    float random(float from, float to);
    void respawn(std::array<float, 6> &drop, bool start);

    // Each drop: the angle round the eye, its distance, its height; then
    // how fast each changes a second.
    std::vector<std::array<float, 6>> _drops;
    float _height = 5;
    std::array<float, 3> _spread{6.28318531f, 20, 20}; // angle, distance, height below the eye
    float _alpha = 0.15f;
    float _windAngle = 1;
    std::array<float, 3> _minVelocity{0.1f, 0.1f, -60};
    std::array<float, 3> _maxVelocity{-0.1f, -0.1f, -50};
    std::array<float, 3> _wind{};       // blowing now
    std::array<float, 3> _windTarget{}; // blowing towards
    int _gustFrames = 0;                // frames until the next gust's direction
    float _fade = 0;
    bool _fallingHere = false;
    std::mt19937 _random{1};
};

} // namespace reluva::idtech3
