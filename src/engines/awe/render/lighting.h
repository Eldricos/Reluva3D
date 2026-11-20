// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/formats/ac2.h"
#include "engines/awe/game/conditions.h"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

// The light on the characters as motoe.exe sets it up: Direct3D 8's
// fixed-function lighting, per vertex, with the scene's LITEs as the
// lights (0x420ee0), the camera's ambient colour (or a night one) as the
// ambient light, and each character's own material (0x427160). See
// docs/engines/awe/games/motoe/systems/lighting.md.

namespace reluva::awe {

/** A colour's red, green and blue, 0..1 where Direct3D clamps them. */
using Rgb = std::array<float, 3>;

/** A LITE as handed to Direct3D (D3DLIGHT8), in the world (y up). */
struct SceneLight {
    /** D3DLIGHTTYPE's values. */
    enum class Type { kPoint = 1, kSpot = 2, kDirectional = 3 } type = Type::kPoint;
    Rgb diffuse{};
    Rgb ambient{};
    Rgb specular{};
    // direction: the way the light goes
    std::array<float, 3> position{};
    std::array<float, 3> direction{};
    float range = 0;
    float attenuation[3] = {}; // 1 / (a0 + a1 d + a2 d²)
    // a spot's inner and outer cone, radians
    float theta = 0;
    float phi = 0;
};

/** What lights a scene's characters: the ambient light (D3DRS_AMBIENT, 0..1) and the lights that are on. */
struct SceneLighting {
    Rgb ambient{};
    std::vector<SceneLight> lights;
    std::array<float, 3> eye{}; // the camera, for the specular (D3DRS_LOCALVIEWER's default)
};

/** A character's material (D3DMATERIAL8) from its common fields. Its attached models use it too. */
struct Material {
    Rgb diffuse{1, 1, 1};
    Rgb ambient{1, 1, 1};
    Rgb emissive{};
    Rgb specular{};
    float power = 0;
    bool lit = true; // b0c (D3DRS_LIGHTING); unlit draws are white
    // The diffuse colour's alpha (+0x98), and whether the model is blended
    // by it times the texture's (footprints fading: 0x428b1f).
    float alpha = 1;
    bool blend = false;
};

/**
 * Whether the scene is lit for the night (0x4027b0): unless it's marked
 * NVNT, at time of day 2, or 1 in a scene marked OUTD (outdoors).
 */
bool nightLighting(const Ac2Scene &scene, int timeOfDay);

/**
 * The colour the scene's pictures are drawn in now, for a game that tints
 * them at night (LightingRules::nightPictures: ac.exe 0x42e89c), with the
 * scene's pictures' own colours by day; false when they're drawn as they
 * are.
 */
bool nightPictureTint(const Ac2Scene &scene, const GameState &state, Rgb &tint);

/**
 * The night colour particles and OLDS pictures are drawn in now: time 1
 * or more outdoors alone, without nightPictureTint's FAKE case (ac.exe
 * 0x425a28, 0x43017e); false by day, indoors or for a game without one.
 */
bool nightOutdoorTint(const Ac2Scene &scene, const GameState &state, Rgb &tint);

/**
 * And Then There Were None's lights with their 25th field (b6c) set, each
 * copied over the scene's first light of its index and itself taken out
 * (LightingRules::lightsReplace; ac.exe 0x42c9a4).
 */
void replaceLights(Ac2Scene &scene);

/**
 * The file a scene's picture `file` is drawn from now: late at night, for
 * a game with night pictures (LightingRules::nightFiles), "x_night.jpg"
 * for "x.jpg" when `has` says there is one; else `file`.
 */
std::string nightPictureFile(const std::string &file, const GameState &state,
                             const std::function<bool(const std::string &)> &has);

/**
 * Each of the scene's objects' conditions as they stand (scene.objects()'s
 * order). The game decides whether a light is on when the scene is
 * entered (0x420ee0); "update" retests objects' conditions but not that.
 */
std::vector<bool> lightConditions(const Ac2Scene &scene, const GameState &state);

/**
 * The scene's lighting with the story as it stands: the time of day, and
 * the lights' conditions as `conditions` has them (from lightConditions
 * on entering the scene), or as they stand when it's empty.
 * @param switched Lights "light `<name>` on|off" switched, over their own
 *        "on" field.
 */
SceneLighting sceneLighting(const Ac2Scene &scene, const GameState &state, const std::vector<bool> &conditions = {},
                            const std::map<const Ac2Record *, bool> &switched = {});

/** A character's material from its fields; `alpha` and `blend` are left as they start. */
Material materialOf(const Ac2Record &object);

/**
 * The colour Direct3D's lighting gives a vertex at `position` with the
 * unit `normal`, both in the world: emissive, plus ambient times the
 * material's, plus each light's (clamped to 0..1). With `specular`, also
 * the specular colour Direct3D adds after the texture (clamped to 0..1).
 */
Rgb lightVertex(const SceneLighting &lighting, const Material &material, const float position[3], const float normal[3],
                Rgb *specular = nullptr);

} // namespace reluva::awe
