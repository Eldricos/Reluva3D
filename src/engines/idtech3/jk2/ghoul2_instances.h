// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/formats/ghoul2.h"
#include "engines/idtech3/vm/qvm.h"

#include "common/archive.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::jk2 {

/**
 * The Ghoul2 instances a Jedi Outcast module keeps (a player's body, its
 * weapon, its saber), as the engine keeps them for it: each a list of
 * models, each with the animations and angles the module has set on its
 * bones, the bolts it asked for, the surfaces it turned on or off, and
 * the bolt of another model it hangs from. The module holds an instance
 * by a number the engine gives it.
 *
 * Posing follows the original's rules (see
 * docs/engines/idtech3/formats/ghoul2.md, Animation): a bone with an
 * animation of its own takes its frame from it, the others their
 * parent's; an angle override turns a bone's final placement.
 */
class Ghoul2Instances {
public:
    // A bone's animation (SetBoneAnim's flags).
    static constexpr int32_t kAnglesPreMult = 0x1;
    static constexpr int32_t kAnglesPostMult = 0x2;
    static constexpr int32_t kAnglesReplace = 0x4;
    static constexpr int32_t kAnimOverride = 0x8;
    static constexpr int32_t kAnimLoop = 0x10;
    static constexpr int32_t kAnimFreeze = 0x40 | kAnimOverride;
    static constexpr int32_t kAnimBlend = 0x80;
    // Axes an override's angles turn about (Eorientations).
    enum Axis { kOrigin, kPositiveX, kPositiveZ, kPositiveY, kNegativeX, kNegativeZ, kNegativeY };

    explicit Ghoul2Instances(const SearchSet &files) : _files(files) {}

    // Ghoul2's calls, which the game module and the client game number
    // differently but pass alike (an instance by its number, or by the
    // address of the word holding it where the call may change it).
    enum Call {
        kListBones,
        kListSurfaces,
        kHaveWeGhoulModels,      // (ghoul2)
        kSetModels,              // (ghoul2, models, skins)
        kGetBolt,                // (ghoul2, model, bolt, matrix out, angles, origin, time, models, scale)
        kGetBoltNoRec,           // the same
        kInitGhoul2Model,        // (&ghoul2, file, model, skin, shader, flags, lod bias) -> its index
        kAddBolt,                // (ghoul2, model, name) -> bolt
        kSetBoltInfo,            // (ghoul2, model, (model << 10) | bolt)
        kAngleOverride,          // (ghoul2, model, bone, angles, flags, up, right, forward, models, blend, time)
        kPlayAnim,               // (ghoul2, model, bone, start, end, flags, speed, time, set frame, blend)
        kGetGlaName,             // (ghoul2, model, buffer)
        kCopyGhoul2Instance,     // (from, to, model)
        kCopySpecificModel,      // (from, from model, to, to model)
        kDuplicateInstance,      // (from, &to)
        kHasGhoul2ModelOnIndex,  // (&ghoul2, model)
        kRemoveGhoul2Model,      // (&ghoul2, model)
        kCleanModels,            // (&ghoul2)
        kGiveMeVectorFromMatrix, // (matrix, axis, vector out)
        kSetRootSurface,         // (ghoul2, model, surface)
        kSetSurfaceOnOff,        // (ghoul2, surface, flags)
        kSetNewOrigin,           // (ghoul2, bolt)
    };
    int32_t call(Qvm &vm, Call which, const int32_t *args);

    // A model added to instance `*handle` (0: a new instance, its number
    // written back), at `modelIndex` if that's past the end or free, else
    // at the end; its index, or -1 if the file can't be read.
    int32_t addModel(int32_t &handle, const std::string &file, int32_t modelIndex, int32_t customSkin);
    bool has(int32_t handle) const { return _instances.count(handle) != 0; }
    bool hasModel(int32_t handle, int32_t modelIndex) const;
    void remove(int32_t handle) { _instances.erase(handle); }
    bool removeModel(int32_t handle, int32_t modelIndex);
    // A new instance with `from`'s models and their state; its number.
    int32_t duplicate(int32_t from);
    // Model `fromModel` of `from` copied over model `toModel` of `to`.
    void copyModel(int32_t from, int32_t fromModel, int32_t to, int32_t toModel);
    // The skeleton's name (without .gla); empty if there's no such model.
    std::string skeletonName(int32_t handle, int32_t modelIndex) const;

    // A bolt on the surface by that name (a tag surface, named with a '*'),
    // else on the bone; its number, the same for the same name; -1 if
    // there's neither.
    int32_t addBolt(int32_t handle, int32_t modelIndex, const std::string &name);
    // The model hangs from another's bolt: (model << 10) | bolt.
    void setBoltLink(int32_t handle, int32_t modelIndex, int32_t link);
    bool setAnimation(int32_t handle, int32_t modelIndex, const std::string &bone, int32_t startFrame, int32_t endFrame,
                      int32_t flags, float speed, int32_t time, float setFrame);
    bool setAngles(int32_t handle, int32_t modelIndex, const std::string &bone, const Vec3 &angles, int32_t flags,
                   Axis up, Axis right, Axis forward);
    // A surface on (flags 0) or off (2) on every model that has it.
    bool setSurfaceOnOff(int32_t handle, const std::string &surface, int32_t flags);
    bool setRootSurface(int32_t handle, int32_t modelIndex, const std::string &surface);

    // A model's bones at `time` (ms), placed in the instance's space: what
    // its vertices are skinned by. False if there's no such model.
    bool pose(int32_t handle, int32_t modelIndex, int32_t time, std::vector<Bone34> &bones);
    // A bolt's placement in the world, as GetBoltMatrix gives it: the
    // instance placed at `origin`, turned by `angles`, scaled by `scale`
    // (0 for 1), at `time`. False if there's no such bolt.
    bool boltMatrix(int32_t handle, int32_t modelIndex, int32_t bolt, int32_t time, const Vec3 &angles,
                    const Vec3 &origin, const Vec3 &scale, Bone34 &out);

    using Drawn = Ghoul2Pose;
    // The instance's models to draw at `time`.
    bool drawn(int32_t handle, int32_t time, std::vector<Drawn> &out);

private:
    struct Animation {
        int32_t startFrame = 0;
        int32_t endFrame = 0;
        int32_t flags = 0;
        float speed = 1;
        int32_t startTime = 0;
    };
    struct Bolt {
        std::string name;
        int bone = -1;    // a bone's bolt
        int surface = -1; // a tag surface's
    };
    struct Model {
        std::string file;
        std::shared_ptr<const GlmModel> glm;
        const GlaSkeleton *skeleton = nullptr;
        int32_t customSkin = 0;
        std::map<int, Animation> animations; // by bone
        std::map<int, Bone34> angles;        // by bone: what the final placement is multiplied by
        std::map<int, int32_t> angleFlags;
        std::vector<Bolt> bolts;
        std::map<int, int32_t> surfaceFlags; // by surface: the module's on/off over the mesh's own
        int32_t boltLink = -1;
        bool used = true;
        // The last pose worked out, for the time it was for: the module asks
        // for bolts many times a frame.
        mutable int32_t posedTime = -1;
        mutable std::vector<Bone34> posed;
    };
    struct Instance {
        std::vector<Model> models;
    };

    Model *model(int32_t handle, int32_t modelIndex);
    const GlaSkeleton *loadSkeleton(const std::string &name);
    std::shared_ptr<const GlmModel> loadMesh(const std::string &file);
    int boneIndex(const Model &m, const std::string &name) const;
    void poseModel(const Model &m, int32_t time, std::vector<Bone34> &bones) const;
    // poseModel's, kept until the time or the model's animations change.
    const std::vector<Bone34> &posed(const Model &m, int32_t time) const;
    // A bolt in its model's space, from the bones it's posed with.
    Bone34 boltInModel(const Model &m, const Bolt &b, const std::vector<Bone34> &bones) const;
    // Where a model is placed in the instance (another model's bolt).
    Bone34 modelPlacement(Instance &inst, const Model &m, int32_t time, int depth);

    const SearchSet &_files;
    std::map<int32_t, Instance> _instances;
    int32_t _next = 1;
    std::map<std::string, std::shared_ptr<const GlmModel>> _meshes; // by file; nullptr: unreadable
    std::map<std::string, std::unique_ptr<GlaSkeleton>> _skeletons; // by name; nullptr: unreadable
};

} // namespace reluva::idtech3::jk2
