// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/awe/gr2.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Characters and objects as the game's .gr2 files hold them: a skeleton,
// skinned meshes and their textures (models/<who>/<who>_model.gr2), and
// animations of the same skeleton (one per file, <who>_walk.gr2...).
// See docs/formats/awe/gr2.md.

namespace reluva::awe {

class GrannyDll;

using Mat4 = std::array<float, 16>; // column-major, as gfx::Mat4

// A bone's transform relative to its parent.
struct BoneTransform {
    float position[3] = {0, 0, 0};
    float orientation[4] = {0, 0, 0, 1}; // quaternion x, y, z, w
    float scaleShear[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // rows
};
Mat4 toMatrix(const BoneTransform& t);

struct Bone {
    std::string name;
    int parent = -1;
    BoneTransform rest;
    Mat4 inverseWorld{}; // from the model to the bone, at rest
};

struct ModelTexture {
    std::string name; // the artist's file name, for display
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // empty if it couldn't be decoded
};

struct ModelVertex {
    float position[3];
    float normal[3];
    float uv[2];
    uint8_t bones[4]; // skeleton bone indices
    float weights[4];
};

struct ModelMesh {
    std::string name;
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    struct Group {
        int texture = -1; // index into Model::textures, -1 none
        uint32_t firstIndex = 0, indexCount = 0;
    };
    std::vector<Group> groups;
};

struct Model {
    std::string name;
    std::vector<Bone> bones;
    std::vector<ModelMesh> meshes;
    std::vector<ModelTexture> textures;
    float unitsPerMeter = 100;
    // Model space: Z up (3D Studio MAX's), as the files say.
};

// Reads the first model in a file. Textures are decoded with the DLL
// when one is given (they're Bink-compressed), else left empty.
Error loadModel(const Gr2File& file, GrannyDll* dll, Model& out);

// A curve: a B-spline of `degree`, one knot (time) per control point.
struct Curve {
    int degree = 0;
    std::vector<float> knots;
    std::vector<float> controls; // knots.size() * dimension values
    bool empty() const { return knots.empty(); }
    // The value at time `t` (clamped to the curve's range) into `out`
    // (`dimension` floats).
    void evaluate(float t, int dimension, float* out) const;
};

struct AnimationTrack {
    std::string bone;
    Curve position, orientation, scaleShear;
};

struct Animation {
    std::string name;
    float duration = 0, timeStep = 0;
    std::vector<AnimationTrack> tracks;
};

// Reads the file's first animation (its first track group). Curves in
// Granny's compressed formats (CurveData: D4nK8uC7u and the like) are
// expanded to plain ones by the DLL when one is given; without it, their
// tracks keep the bones at rest.
Error loadAnimation(const Gr2File& file, Animation& out, GrannyDll* dll = nullptr);

// A skeleton's pose: each bone's world matrix and its skinning matrix
// (world times inverse rest world).
struct Pose {
    std::vector<Mat4> world, skinning;
};
// The rest pose, or the animation at `time` (bones it has no track for
// stay at rest).
void computePose(const Model& model, const Animation* animation, float time, Pose& out);
// Skins a mesh's vertices for the pose: positions and normals.
void skin(const ModelMesh& mesh, const Pose& pose, std::vector<std::array<float, 3>>& positions,
          std::vector<std::array<float, 3>>& normals);

} // namespace reluva::awe
