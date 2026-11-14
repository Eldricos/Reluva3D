// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/formats/md3.h"

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::idtech3 {

// Raven's Ghoul2 models (Jedi Outcast on): a mesh (.glm, "2LGM" version 6)
// whose vertices hang off the bones of a separate skeleton (.gla, "2LGA"
// version 6) with its animation frames. See
// docs/engines/idtech3/formats/ghoul2.md.

// A bone's 3x4 matrix, row by row: each row the rotation's three values,
// then the translation's.
using Bone34 = std::array<float, 12>;

struct GlmVertex {
    Vec3 pos{};
    Vec3 normal{};
    int weightCount = 1;
    std::array<uint8_t, 4> bone{}; // indexes into its surface's boneRefs
    std::array<float, 4> weight{};
};

// One surface at one level of detail.
struct GlmSurface {
    std::vector<int32_t> boneRefs; // the skeleton's bones its vertices use
    std::vector<GlmVertex> vertices;
    std::vector<std::array<float, 2>> st;
    std::vector<uint32_t> indices; // three per triangle
};

constexpr uint32_t kGlmSurfaceTag = 0x1; // a bolt point, not drawn
constexpr uint32_t kGlmSurfaceOff = 0x2; // off unless the game turns it on

// A surface as the model names it: the same at every level of detail.
struct GlmSurfaceInfo {
    std::string name;
    uint32_t flags = 0; // kGlmSurface*: drawn only with none
    std::string shader;
    int parent = -1;
    std::vector<int> children;
};

struct GlmModel {
    std::string name;
    std::string animName; // the skeleton, without ".gla"; "*default" for none
    int boneCount = 0;
    std::vector<GlmSurfaceInfo> surfaces;
    std::vector<std::vector<GlmSurface>> lods; // [level][surface], level 0 the most detailed
};

struct GlaBone {
    std::string name;
    uint32_t flags = 0;
    int parent = -1;
    Bone34 basePose{};
    Bone34 basePoseInverse{};
    std::vector<int> children;
};

// A skeleton and its frames. Each frame's bones are kept compressed, as
// the file has them, and unpacked as they're needed (bone()).
struct GlaSkeleton {
    std::string name;
    float scale = 1; // as built; the renderer doesn't use it
    int frameCount = 0;
    std::vector<GlaBone> bones;       // not always a parent before its children
    std::vector<uint32_t> frameBones; // [frame * bones + bone]: an index into pool
    std::vector<std::array<uint8_t, 14>> pool;

    // Bone `bone` of `frame`, relative to its parent.
    Bone34 bone(int frame, int bone) const;
};

Error parseGlm(const std::vector<uint8_t> &data, GlmModel &out);
Error parseGla(const std::vector<uint8_t> &data, GlaSkeleton &out);

// Each bone's matrix for a frame, `backLerp` of the way back towards
// `oldFrame` (each of the 12 values blended), placing the model's
// vertices: the root's frame bone, then each child's on its parent's.
// Without a skeleton (nullptr: "*default"), one bone, the identity.
void poseBones(const GlaSkeleton *skeleton, int frame, int oldFrame, float backLerp, std::vector<Bone34> &out);

// A surface's vertices placed by `bones` (poseBones'): positions and
// normals, each its bones' weighted sum.
void skinSurface(const GlmSurface &surface, const std::vector<Bone34> &bones, std::vector<Md3Vertex> &out);

} // namespace reluva::idtech3
