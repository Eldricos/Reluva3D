// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/awe/gr2.h"

#include <array>
#include <cstdint>
#include <map>
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
    // The skeleton's track masks by name ("sitidle1"), each a weight by
    // bone name: a seated character's animations are limited to one.
    std::map<std::string, std::map<std::string, float>> masks;
    // Model space: Z up (3D Studio MAX's), as the files say.
};

// Reads the first model in a file, with its textures decoded.
Error loadModel(const Gr2File& file, Model& out);

// A texture's pixels as stored: its first image's first MIP level.
std::vector<uint8_t> textureBytes(const Gr2File& file, const Gr2Object& texture);
// The flags a Bink texture (Encoding 3) is decoded with: its SubFormat,
// plus 1 if its layout has alpha.
uint32_t binkTextureFlags(const Gr2Object& texture);

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
    // How much each bone follows it, by bone name (its file's skeleton's
    // TrackMask; the game's mask for layering it over others). Empty if
    // the file has none.
    std::map<std::string, float> mask;
};

// Reads the file's first animation (its first track group). Curves in
// Granny's compressed formats (CurveData: D4nK8uC7u and the like) are
// expanded to plain ones (curves.h).
Error loadAnimation(const Gr2File& file, Animation& out);
// Replaces the animation's compressed curves, as loadAnimation() expanded
// them from `file`, with granny2.dll's expansion of them
// (GrannyCurveExtractKnotValues): the reference the native one is checked
// against (reluva-tool awe-check).
Error expandCurvesWithDll(const Gr2File& file, GrannyDll& dll, Animation& out);

// A skeleton's pose: each bone's world matrix and its skinning matrix
// (world times inverse rest world).
struct Pose {
    std::vector<Mat4> world, skinning;
};
// A turn added to a bone on top of its animation (a head turned to look
// at someone): a quaternion (x, y, z, w) in the bone's own axes.
struct BoneTurn {
    int bone = -1;
    float rotation[4] = {0, 0, 0, 1};
};
// The rest pose, or the animation at `time` (bones it has no track for
// stay at rest), with any `turns` added.
void computePose(const Model& model, const Animation* animation, float time, Pose& out,
                 const std::vector<BoneTurn>* turns = nullptr);
// The same in two steps: each bone's transform relative to its parent
// (rest where the animation has no track), and the pose those make.
void sampleLocal(const Model& model, const Animation* animation, float time, std::vector<BoneTransform>& local);
void poseFromLocal(const Model& model, std::vector<BoneTransform> local, Pose& out,
                   const std::vector<BoneTurn>* turns = nullptr);
// Skins a mesh's vertices for the pose: positions and normals.
void skin(const ModelMesh& mesh, const Pose& pose, std::vector<std::array<float, 3>>& positions,
          std::vector<std::array<float, 3>>& normals);

} // namespace reluva::awe
