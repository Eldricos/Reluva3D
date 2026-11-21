// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/raven/random.h"
#include "graphics/gfx3d.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

/** A picture the misty fog reads, as R_LoadImage gives it: RGBA bytes, each side a power of two. */
struct WeatherPicture {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
};

/**
 * Jedi Outcast's weather, as both its renderers keep it
 * (docs/engines/idtech3/games/jk2/systems/weather.md): command lines
 * (single player's client game's R_WorldEffectCommand, multiplayer's
 * `r_we`) start rain or snow round the eye, with gusts of wind and a misty
 * fog, and each view with the level moves them on and draws them.
 */
class WorldEffects {
public:
    /** The two programs differ a little: the rain's alpha, and `exec`. */
    enum class Engine : uint8_t { kSinglePlayer, kMultiplayer };
    // The contents the weather falls in, and what stops it (Jedi Outcast's).
    static constexpr int32_t kContentsOutside = 0x10000;
    static constexpr int32_t kContentsWater = 4;
    static constexpr const char *kRainImage = "gfx/world/rain";

    explicit WorldEffects(Engine engine = Engine::kSinglePlayer);
    ~WorldEffects();
    WorldEffects(WorldEffects &&) noexcept;
    WorldEffects &operator=(WorldEffects &&) noexcept;

    /** What a command line asks besides, which the page does. */
    struct Outcome {
        bool understood = false;
        std::string execute; // `exec`: a console command to run now
        std::string error;   // the level ends with this (a picture that can't be loaded)
    };
    /** A command line ("rain init 500", "snow fog"...), as the original's parser. */
    Outcome command(const std::string &line);

    /** Loads a picture the fog needs (gfx/world/fog2, any of its file types); false if there's none. */
    std::function<bool(const std::string &name, WeatherPicture &out)> loadPicture;
    /** The level's contents at a point (the world's, without entities). */
    std::function<int32_t(const std::array<float, 3> &point)> contents;

    /** No weather (a level's start). */
    void clear();
    /** Whether rain was started. */
    bool raining() const { return _rain != nullptr; }
    /** Whether snow was started. */
    bool snowing() const { return _snow != nullptr; }

    /** The view the weather is moved on and drawn for. */
    struct ViewParms {
        std::array<float, 3> eye{};
        std::array<float, 3> forward{1, 0, 0};
        std::array<float, 3> left{0, 1, 0};
        std::array<float, 3> up{0, 0, 1};
        float pixel = 0.002f; // a pixel's width at a distance of 1, for the snowflakes
    };
    /** A frame `seconds` long: the rain, then the snow, moved on round the view's eye. */
    void update(float seconds, const ViewParms &view);

    /** What a batch of triangles is drawn with. */
    enum class Look : uint8_t {
        kRain,  // the rain's picture, blended by its vertices' alpha
        kPlain, // no picture, blended by its vertices' alpha: snowflakes, the winds' boxes
        kFog,   // no picture, added by its vertices' alpha, in fogView()
    };
    /** Triangles (three vertices each) drawn with one look, in the order the original draws them. */
    struct Batch {
        Look look = Look::kPlain;
        std::vector<gfx::Vertex> vertices;
    };
    /** The weather as the frame's last update left it, for `view`. */
    void draw(const ViewParms &view, std::vector<Batch> &out) const;
    /** The view the fog is drawn in, its own: 80 degrees, square, 4 to 2048 deep (0x499e51). */
    static gfx::Mat4 fogView();

    /** Whether rain falls where the eye is, or snow round it (R_IsRaining, R_IsSnowing). */
    bool fallingHere() const;
    /** The rain's wind strength for the surface sprites (its lean x 75); false without rain. */
    bool windSpeed(float &speed) const;
    /** The wind for the surface sprites: the rain's, else the snow's; false without either. */
    bool windVector(std::array<float, 3> &wind) const;
    /** How faded in the rain is (0 to 1; 0 without rain). */
    float rainFade() const;

    /** The C library's rand() single player's weather draws from; unset, its own. */
    std::function<int32_t()> rand;
    /** Multiplayer's Q_flrand and Q_irand seed, which its surface sprites share; unset, its own. */
    raven::QRandom *engineRandom = nullptr;

    /** Its random draws, as its program makes them. */
    struct Draws {
        WorldEffects &owner;
        int32_t rand();
        float range(float from, float to);
        int irand(int from, int to);
    };

private:
    struct Effect;
    struct Effects;
    struct Wind;
    struct Fog;
    struct FogLayer;
    struct Rain;
    struct Snow;

    Outcome systemCommand(const std::string &system, const std::string &rest);
    // A misty fog into a system's effects; false (and _error set) without its picture.
    bool addFog(Effects &effects);

    Engine _engine;
    std::unique_ptr<Rain> _rain;
    std::unique_ptr<Snow> _snow;
    bool _showWind = false; // "debug wind"
    ViewParms _view;        // the last frame's, where a new fog is placed from
    std::shared_ptr<const WeatherPicture> _fogPicture;
    std::string _error; // a command's, while it runs
    raven::CrtRandom _ownRand;
    raven::QRandom _ownEngineRandom;
};

} // namespace reluva::idtech3
