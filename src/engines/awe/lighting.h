// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/ac2.h"
#include "engines/awe/conditions.h"

#include <array>
#include <vector>

// The light on the characters as motoe.exe sets it up: Direct3D 8's
// fixed-function lighting, per vertex, with the scene's LITEs as the
// lights (0x420ee0), the camera's ambient colour (or a night one) as the
// ambient light, and each character's own material (0x427160). See
// docs/formats/awe/lighting.md.

namespace reluva::awe {

using Rgb = std::array<float, 3>;

// A LITE as handed to Direct3D (D3DLIGHT8), in the world (y up).
struct SceneLight {
    enum class Type { Point = 1, Spot = 2, Directional = 3 } type = Type::Point;
    Rgb diffuse{}, ambient{}, specular{};
    std::array<float, 3> position{}, direction{}; // direction: the way the light goes
    float range = 0;
    float attenuation[3] = {}; // 1 / (a0 + a1 d + a2 d²)
    float theta = 0, phi = 0;  // a spot's inner and outer cone, radians
};

// What lights a scene's characters: the ambient light (D3DRS_AMBIENT,
// 0..1) and the lights that are on.
struct SceneLighting {
    Rgb ambient{};
    std::vector<SceneLight> lights;
    std::array<float, 3> eye{}; // the camera, for the specular (D3DRS_LOCALVIEWER's default)
};

// A character's material (D3DMATERIAL8) from its common fields. Its
// attached models use it too.
struct Material {
    Rgb diffuse{1, 1, 1}, ambient{1, 1, 1}, emissive{}, specular{};
    float power = 0;
    bool lit = true; // b0c (D3DRS_LIGHTING); unlit draws are white
    // The diffuse colour's alpha (+0x98), and whether the model is blended
    // by it times the texture's (footprints fading: 0x428b1f).
    float alpha = 1;
    bool blend = false;
};

// Whether the scene is lit for the night (0x4027b0): unless it's marked
// NVNT, at time of day 2, or 1 in a scene marked OUTD (outdoors).
bool nightLighting(const Ac2Scene& scene, int timeOfDay);

// Each of the scene's objects' conditions as they stand (scene.objects()'s
// order). The game decides whether a light is on when the scene is
// entered (0x420ee0); "update" retests objects' conditions but not that.
std::vector<bool> lightConditions(const Ac2Scene& scene, const GameState& state);

// The scene's lighting with the story as it stands: the time of day, and
// the lights' conditions as `conditions` has them (from lightConditions
// on entering the scene), or as they stand when it's empty.
SceneLighting sceneLighting(const Ac2Scene& scene, const GameState& state,
                            const std::vector<bool>& conditions = {});

Material materialOf(const Ac2Record& object);

// The colour Direct3D's lighting gives a vertex at `position` with the
// unit `normal`, both in the world: emissive, plus ambient times the
// material's, plus each light's (clamped to 0..1). With `specular`, also
// the specular colour Direct3D adds after the texture (clamped to 0..1).
Rgb lightVertex(const SceneLighting& lighting, const Material& material, const float position[3],
                const float normal[3], Rgb* specular = nullptr);

} // namespace reluva::awe
