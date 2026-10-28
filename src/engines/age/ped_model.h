// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/age/render_cache.h"
#include "engines/age/vecmath.h"

#include <map>
#include <string>
#include <vector>

namespace reluva::age {

// The game's pedestrians: a skinned model (anim/<model>.mod, text), its
// skeleton (anim/<model>.skel, text), its moves (anim/<model>.csv) and
// their animations (anim/pedanim_*.anim). See docs/formats/age/ped.md.

struct Skeleton {
    struct Bone {
        std::string name;
        int parent = -1;
        Vec3 offset{}; // from the parent's joint
    };
    std::vector<Bone> bones; // parents before children, as in the file
};
Error parseSkeleton(const std::string& text, Skeleton& out);

// Frames of 3 + 3 * bones values: the root's position, then each bone's
// turn about X, Y and Z (radians).
struct PedAnimation {
    int frames = 0, channels = 0;
    std::vector<float> values;
    const float* frame(int f) const { return values.data() + size_t(f) * size_t(channels); }
};
Error readPedAnimation(const std::vector<uint8_t>& data, PedAnimation& out);

// A move: an animation and the move that follows it.
struct PedMove {
    std::string name, animation, next;
};
Error parsePedMoves(const std::string& csv, std::vector<PedMove>& out);

// The model: each corner (a position in its bone's space, a normal and
// texture coordinates) and the triangles of each material.
struct PedMesh {
    struct Corner {
        Vec3 position{}, normal{};
        float uv[2]{};
        int bone = 0;
    };
    struct Material {
        std::string name;
        std::array<float, 4> diffuse{1, 1, 1, 1};
        std::vector<Corner> corners;
        std::vector<uint32_t> indices; // into corners
    };
    std::vector<Material> materials;
};
Error parsePedMesh(const std::string& text, PedMesh& out);

// Each bone's place in the model for a frame: rotation and position.
struct BonePose {
    Mat3 turn;
    Vec3 at{};
};
// The root's position across the ground is left out (the walker moves
// instead), its height kept.
std::vector<BonePose> poseOf(const Skeleton& skeleton, const PedAnimation& anim, int frame);

// A pedestrian model ready to draw: every frame of the moves it uses
// posed and uploaded, and each paint job's colours (from
// geometry/<model>.pkg).
class PedModel {
public:
    static constexpr float kFrameRate = 20; // frames a second (assumed)

    struct Move {
        int firstFrame = 0, frames = 0; // in the mesh, in frames of corners()
        Vec3 travel{};                  // metres a second it carries the walker (in its own space)
        int next = -1;                  // the move after it
    };

    PedModel() = default;
    PedModel(const PedModel&) = delete;
    PedModel& operator=(const PedModel&) = delete;
    ~PedModel() { unload(); }

    // `moves`: the names of the moves wanted (and those they lead to).
    Error load(gfx::Renderer3D& renderer, const SearchSet& files, const std::string& model,
               const std::vector<std::string>& moves);
    void unload();
    int move(const std::string& name) const; // -1 if it hasn't one
    const Move& moveAt(int index) const { return moves_[size_t(index)]; }
    uint32_t paintJobs() const { return uint32_t(paints_.size()); }
    // `time` seconds into a move (looped by its frames).
    void draw(const gfx::Mat4& model, int move, float time, uint32_t paintJob, const SceneLight& light) const;

private:
    gfx::Renderer3D* renderer_ = nullptr;
    gfx::MeshId mesh_ = 0;
    size_t corners_ = 0; // a frame's
    std::vector<std::pair<uint32_t, uint32_t>> ranges_;    // each material's indices: first, count
    std::vector<std::vector<std::array<float, 4>>> paints_; // each paint job's colour for each material
    std::vector<Move> moves_;
    std::map<std::string, int> moveIndex_;
};

} // namespace reluva::age
