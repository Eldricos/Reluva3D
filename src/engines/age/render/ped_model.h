// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "graphics/gfx3d.h"
#include "engines/age/render/render_cache.h"
#include "engines/age/vecmath.h"

#include <map>
#include <string>
#include <vector>

namespace reluva::age {

// The game's pedestrians: a skinned model (anim/<model>.mod, text), its
// skeleton (anim/<model>.skel, text), its moves (anim/<model>.csv) and
// their animations (anim/pedanim_*.anim). See docs/engines/age/games/mm2/formats/ped.md.

/** A pedestrian's bones (anim/<model>.skel), each placed from its parent's joint. */
struct Skeleton {
    /** A bone and where its joint sits from its parent's. */
    struct Bone {
        std::string name;
        int parent = -1;
        Vec3 offset{}; // from the parent's joint
    };
    std::vector<Bone> bones; // parents before children, as in the file
};
/**
 * Reads a .skel's nested bones and their offsets.
 * @return A failure on unbalanced braces or no bones.
 */
Error parseSkeleton(const std::string &text, Skeleton &out);

/**
 * Frames of 3 + 3 * bones values: the root's position, then each bone's
 * turn about X, Y and Z (radians).
 */
struct PedAnimation {
    int frames = 0;
    int channels = 0;
    std::vector<float> values;
    /** Frame `f`'s values (0 to frames - 1, unchecked). */
    const float *frame(int f) const { return values.data() + size_t(f) * size_t(channels); }
};
/**
 * Reads an anim/pedanim_*.anim.
 * @return A failure if the header or the size doesn't look like one.
 */
Error readPedAnimation(const std::vector<uint8_t> &data, PedAnimation &out);

/** A move: an animation and the move that follows it. */
struct PedMove {
    std::string name;
    std::string animation;
    std::string next;
};
/**
 * Reads anim/<model>.csv's moves, skipping # lines and those of fewer than 9 fields.
 * @return A failure when no move was read.
 */
Error parsePedMoves(const std::string &csv, std::vector<PedMove> &out);

/**
 * The model: each corner (a position in its bone's space, a normal and
 * texture coordinates) and the triangles of each material.
 */
struct PedMesh {
    /** A corner of a triangle, on one bone. */
    struct Corner {
        Vec3 position{};
        Vec3 normal{};
        float uv[2]{};
        int bone = 0;
    };
    /** A material's colour and its own corners and triangles. */
    struct Material {
        std::string name;
        std::array<float, 4> diffuse{1, 1, 1, 1};
        std::vector<Corner> corners;
        std::vector<uint32_t> indices; // into corners
    };
    std::vector<Material> materials;
};
/**
 * Reads an anim/<model>.mod, in either of its two layouts (ped.md).
 * @return A failure naming what didn't read.
 */
Error parsePedMesh(const std::string &text, PedMesh &out);

/** Each bone's place in the model for a frame: rotation and position. */
struct BonePose {
    Mat3 turn;
    Vec3 at{};
};
/**
 * Each bone's place in the model at `frame` (clamped to the animation's).
 * The root's position across the ground is left out (the walker moves
 * instead), its height kept.
 */
std::vector<BonePose> poseOf(const Skeleton &skeleton, const PedAnimation &anim, int frame);

/**
 * A pedestrian model ready to draw: every frame of the moves it uses
 * posed and uploaded, and each paint job's colours (from
 * geometry/<model>.pkg).
 */
class PedModel {
public:
    static constexpr float kFrameRate = 20; // frames a second (assumed)

    /** A move as loaded: its frames in the uploaded mesh and where it leads. */
    struct Move {
        // in the mesh, in frames of corners()
        int firstFrame = 0;
        int frames = 0;
        Vec3 travel{}; // metres a second it carries the walker (in its own space)
        int next = -1; // the move after it
    };

    PedModel() = default;
    PedModel(const PedModel &) = delete;
    PedModel &operator=(const PedModel &) = delete;
    ~PedModel() { unload(); }

    /**
     * Reads the model's .skel, .mod and .csv, poses every frame of the moves
     * wanted and uploads them, in place of what was loaded.
     * @param moves The names of the moves wanted (and those they lead to).
     * @return A failure naming the file that didn't read, or the renderer's.
     */
    Error load(gfx::Renderer3D &renderer, const SearchSet &files, const std::string &model,
               const std::vector<std::string> &moves);
    /** Frees the mesh from the renderer; nothing is drawn after. */
    void unload();
    /** The index of the move named `name`, or -1 if it hasn't one. */
    int move(const std::string &name) const;
    /** The move at an index move() gave. */
    const Move &moveAt(int index) const { return _moves[size_t(index)]; }
    /** The paint jobs, at least 1 once loaded. */
    uint32_t paintJobs() const { return uint32_t(_paints.size()); }
    /**
     * Draws `time` seconds into a move (looped by its frames, blending
     * between two), in paint job `paintJob` (taken modulo paintJobs()).
     * Nothing if the model isn't loaded or `move` is out of range.
     */
    void draw(const gfx::Mat4 &model, int move, float time, uint32_t paintJob, const SceneLight &light) const;

private:
    gfx::Renderer3D *_renderer = nullptr;
    gfx::MeshId _mesh = 0;
    size_t _corners = 0;                                    // a frame's
    std::vector<std::pair<uint32_t, uint32_t>> _ranges;     // each material's indices: first, count
    std::vector<std::vector<std::array<float, 4>>> _paints; // each paint job's colour for each material
    std::vector<Move> _moves;
    std::map<std::string, int> _moveIndex;
};

} // namespace reluva::age
