// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/scene_drawer.h"
#include "engines/idtech3/formats/shader.h"
#include "engines/idtech3/render/surface_sprites.h"
#include "engines/idtech3/render/world_effects.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva {
class SearchSet;
}

namespace reluva::idtech3 {

class CvarStore;
class ShaderCache;

/**
 * Jedi Outcast's weather (WorldEffects) as a Play page shows it: its
 * materials, made as needed after a level loads, and each frame's
 * triangles handed to the scene drawer for the view with the level.
 */
class WeatherDrawing {
public:
    explicit WeatherDrawing(WorldEffects::Engine engine) : _engine(engine) {}

    /** The shaders were cleared (a level loading): the materials are made again when next needed. */
    void reset() { _materials.fill(-1); }

    /**
     * Moves `weather` on by `seconds` for the view `refdef` (refdef_t's
     * bytes, in a 640 x 480 screen drawn `frameHeight` pixels high) and
     * hands what it draws to `scene`.
     */
    void frame(WorldEffects &weather, const std::vector<uint8_t> &refdef, float seconds, int frameHeight,
               ShaderCache &shaders, const SearchSet &files, SceneDrawer &scene);

private:
    int material(WorldEffects::Look look, ShaderCache &shaders, const SearchSet &files);

    WorldEffects::Engine _engine;
    std::array<int, 3> _materials{-1, -1, -1}; // by Look
    std::array<ShaderDef, 3> _shaders;         // the materials' (which must outlive them)
};

/**
 * The surface sprites' wind and weather: the renderer's settings, but
 * where rain or snow sets them (jk2sp.exe's 0x4882ef on, jk2mp.exe's
 * 0x495c86 on).
 */
SurfaceSpriteWeather weatherSprites(const WorldEffects &weather, CvarStore &cvars);

/** A picture as R_LoadImage finds it: the name as a .jpg, .png or .tga (jk2mp.exe's 0x47c050). */
bool loadWeatherPicture(const SearchSet &files, const std::string &name, WeatherPicture &out);

} // namespace reluva::idtech3
