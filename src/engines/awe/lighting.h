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
    Rgb diffuse{}, ambient{};
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
};

// A character's material (D3DMATERIAL8) from its common fields. Its
// attached models use it too.
struct Material {
    Rgb diffuse{1, 1, 1}, ambient{1, 1, 1}, emissive{};
    bool lit = true; // b0c (D3DRS_LIGHTING); unlit draws are white
    // The diffuse colour's alpha (+0x98), and whether the model is blended
    // by it times the texture's (footprints fading: 0x428b1f).
    float alpha = 1;
    bool blend = false;
};

// Whether the scene is lit for the night (0x4027b0): unless it's marked
// NVNT, at time of day 2, or 1 in a scene marked OUTD (outdoors).
bool nightLighting(const Ac2Scene& scene, int timeOfDay);

// The scene's lighting with the story as it stands (the time of day, the
// lights' conditions).
SceneLighting sceneLighting(const Ac2Scene& scene, const GameState& state);

Material materialOf(const Ac2Record& object);

// The colour Direct3D's lighting gives a vertex at `position` with the
// unit `normal`, both in the world: emissive, plus ambient times the
// material's, plus each light's (clamped to 0..1). Specular isn't
// included.
Rgb lightVertex(const SceneLighting& lighting, const Material& material, const float position[3],
                const float normal[3]);

} // namespace reluva::awe
