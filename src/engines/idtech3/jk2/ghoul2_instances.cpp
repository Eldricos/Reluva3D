// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/ghoul2_instances.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3::jk2 {

namespace {

constexpr Bone34 kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
// What the original places a model's root bone on (its renderer's
// "identity"): a quarter turn about the vertical, x to y, so a skeleton
// faces the way its entity's axes say.
constexpr Bone34 kRootPlacement{0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
constexpr float kPi = 3.14159265358979323846f;

Bone34 mul(const Bone34 &a, const Bone34 &b) {
    Bone34 o{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            float v = c == 3 ? a[size_t(r * 4 + 3)] : 0;
            for (int k = 0; k < 3; ++k)
                v += a[size_t(r * 4 + k)] * b[size_t(k * 4 + c)];
            o[size_t(r * 4 + c)] = v;
        }
    }
    return o;
}

// Angles (pitch, yaw, roll in degrees) as a matrix whose columns are the
// forward, left and up axes they turn to, as Quake III's AnglesToAxis.
Bone34 anglesMatrix(const Vec3 &angles) {
    const float p = angles[0] * kPi / 180;
    const float y = angles[1] * kPi / 180;
    const float r = angles[2] * kPi / 180;
    const float sp = std::sin(p);
    const float cp = std::cos(p);
    const float sy = std::sin(y);
    const float cy = std::cos(y);
    const float sr = std::sin(r);
    const float cr = std::cos(r);
    const Vec3 forward{cp * cy, cp * sy, -sp};
    const Vec3 left{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp};
    const Vec3 up{cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
    Bone34 m{};
    for (int i = 0; i < 3; ++i) {
        m[size_t(i * 4 + 0)] = forward[size_t(i)];
        m[size_t(i * 4 + 1)] = left[size_t(i)];
        m[size_t(i * 4 + 2)] = up[size_t(i)];
    }
    return m;
}

Vec3 normalized(Vec3 v) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0)
        for (float &c : v)
            c /= len;
    return v;
}

Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// Where an animation is at `time`: the frame it's on, the next, and how far
// towards it, as the original times them (frames of 50 ms at speed 1).
void animationFrames(int32_t startFrame, int32_t endFrame, int32_t flags, float speed, int32_t startTime, int32_t time,
                     int frames, int &current, int &next, float &towards) {
    const float elapsed = std::max(0.0f, float(time - startTime) / 50.0f);
    float at = float(startFrame) + elapsed * speed;
    const int size = endFrame - startFrame;
    towards = 0;
    if (size == 0) {
        current = next = startFrame;
    } else if (speed >= 0) {
        if (at > float(endFrame - 1)) {
            if (!(flags & Ghoul2Instances::kAnimLoop)) {
                // Held on its last frame.
                current = next = std::clamp(endFrame - 1, 0, frames - 1);
                return;
            }
            at = float(startFrame) + std::fmod(at - float(startFrame), float(size));
        }
        current = int(std::floor(at));
        towards = at - float(current);
        next = current + 1;
        if (next >= endFrame)
            next = (flags & Ghoul2Instances::kAnimLoop) ? startFrame : endFrame - 1;
    } else {
        // Played backwards: from startFrame down towards endFrame.
        if (at < float(endFrame + 1)) {
            if (flags & Ghoul2Instances::kAnimLoop)
                at = float(startFrame) - std::fmod(float(startFrame) - at, float(-size));
            else
                at = float(endFrame + 1);
        }
        current = int(std::ceil(at));
        towards = float(current) - at;
        next = current - 1;
        if (next <= endFrame)
            next = (flags & Ghoul2Instances::kAnimLoop) ? startFrame : endFrame + 1;
    }
    current = std::clamp(current, 0, frames - 1);
    next = std::clamp(next, 0, frames - 1);
}

} // namespace

std::shared_ptr<const GlmModel> Ghoul2Instances::loadMesh(const std::string &file) {
    const std::string key = fs::toLower(file);
    if (const auto it = _meshes.find(key); it != _meshes.end())
        return it->second;
    std::vector<uint8_t> data;
    auto glm = std::make_shared<GlmModel>();
    std::shared_ptr<const GlmModel> loaded;
    if (!_files.read(file, data) && !parseGlm(data, *glm))
        loaded = std::move(glm);
    return _meshes[key] = loaded;
}

const GlaSkeleton *Ghoul2Instances::loadSkeleton(const std::string &name) {
    if (name.empty() || name[0] == '*')
        return nullptr;
    const std::string key = fs::toLower(name);
    auto [it, added] = _skeletons.try_emplace(key);
    if (added) {
        std::vector<uint8_t> data;
        auto gla = std::make_unique<GlaSkeleton>();
        if (!_files.read(name + ".gla", data) && !parseGla(data, *gla))
            it->second = std::move(gla);
    }
    return it->second.get();
}

Ghoul2Instances::Model *Ghoul2Instances::model(int32_t handle, int32_t modelIndex) {
    const auto it = _instances.find(handle);
    if (it == _instances.end() || modelIndex < 0 || size_t(modelIndex) >= it->second.models.size())
        return nullptr;
    Model &m = it->second.models[size_t(modelIndex)];
    return m.used ? &m : nullptr;
}

int32_t Ghoul2Instances::addModel(int32_t &handle, const std::string &file, int32_t modelIndex, int32_t customSkin) {
    std::shared_ptr<const GlmModel> glm = loadMesh(file);
    if (!glm)
        return -1;
    if (!handle || !_instances.count(handle))
        handle = _next++;
    Instance &inst = _instances[handle];
    Model m;
    m.file = file;
    m.glm = glm;
    m.skeleton = loadSkeleton(glm->animName);
    m.customSkin = customSkin;
    // A free slot, else the end.
    size_t at = inst.models.size();
    for (size_t i = 0; i < inst.models.size(); ++i)
        if (!inst.models[i].used) {
            at = i;
            break;
        }
    if (modelIndex >= 0 && size_t(modelIndex) >= inst.models.size())
        at = inst.models.size();
    if (at == inst.models.size())
        inst.models.push_back(std::move(m));
    else
        inst.models[at] = std::move(m);
    return int32_t(at);
}

bool Ghoul2Instances::hasModel(int32_t handle, int32_t modelIndex) const {
    const auto it = _instances.find(handle);
    return it != _instances.end() && modelIndex >= 0 && size_t(modelIndex) < it->second.models.size() &&
           it->second.models[size_t(modelIndex)].used;
}

bool Ghoul2Instances::removeModel(int32_t handle, int32_t modelIndex) {
    Model *m = model(handle, modelIndex);
    if (!m)
        return false;
    m->used = false;
    return true;
}

int32_t Ghoul2Instances::duplicate(int32_t from) {
    const auto it = _instances.find(from);
    if (it == _instances.end())
        return 0;
    const int32_t handle = _next++;
    _instances[handle] = it->second;
    return handle;
}

void Ghoul2Instances::copyModel(int32_t from, int32_t fromModel, int32_t to, int32_t toModel) {
    const Model *src = model(from, fromModel);
    const auto it = _instances.find(to);
    if (!src || it == _instances.end() || toModel < 0)
        return;
    std::vector<Model> &models = it->second.models;
    if (size_t(toModel) >= models.size()) {
        Model unused;
        unused.used = false;
        models.resize(size_t(toModel) + 1, unused);
    }
    models[size_t(toModel)] = *src;
}

std::string Ghoul2Instances::skeletonName(int32_t handle, int32_t modelIndex) const {
    const auto it = _instances.find(handle);
    if (it == _instances.end() || modelIndex < 0 || size_t(modelIndex) >= it->second.models.size())
        return {};
    const Model &m = it->second.models[size_t(modelIndex)];
    return m.used && m.glm ? m.glm->animName : std::string();
}

int Ghoul2Instances::boneIndex(const Model &m, const std::string &name) const {
    if (!m.skeleton)
        return -1;
    for (size_t i = 0; i < m.skeleton->bones.size(); ++i)
        if (fs::toLower(m.skeleton->bones[i].name) == fs::toLower(name))
            return int(i);
    return -1;
}

int32_t Ghoul2Instances::addBolt(int32_t handle, int32_t modelIndex, const std::string &name) {
    Model *m = model(handle, modelIndex);
    if (!m)
        return -1;
    for (size_t i = 0; i < m->bolts.size(); ++i)
        if (fs::toLower(m->bolts[i].name) == fs::toLower(name))
            return int32_t(i);
    // A surface by that name (tag surfaces' names start with '*'), else a
    // bone, as the original looks for them.
    Bolt b;
    b.name = name;
    const std::string wanted = fs::toLower(name);
    for (size_t i = 0; i < m->glm->surfaces.size() && b.surface < 0; ++i)
        if (fs::toLower(m->glm->surfaces[i].name) == wanted)
            b.surface = int(i);
    if (b.surface < 0)
        b.bone = boneIndex(*m, name);
    if (b.surface < 0 && b.bone < 0)
        return -1;
    m->bolts.push_back(b);
    return int32_t(m->bolts.size()) - 1;
}

void Ghoul2Instances::setBoltLink(int32_t handle, int32_t modelIndex, int32_t link) {
    if (Model *m = model(handle, modelIndex))
        m->boltLink = link;
}

bool Ghoul2Instances::setAnimation(int32_t handle, int32_t modelIndex, const std::string &bone, int32_t startFrame,
                                   int32_t endFrame, int32_t flags, float speed, int32_t time, float setFrame,
                                   int32_t blendTime) {
    Model *m = model(handle, modelIndex);
    const int b = m ? boneIndex(*m, bone) : -1;
    if (b < 0)
        return false;
    Animation a;
    a.startFrame = startFrame;
    a.endFrame = endFrame;
    a.flags = flags;
    a.speed = speed;
    a.startTime = time;
    // A frame to start from: as if begun that many frames ago.
    if (setFrame >= 0 && speed != 0)
        a.startTime = time - int32_t((setFrame - float(startFrame)) / speed * 50);
    const auto old = m->animations.find(b);
    if ((flags & kAnimBlend) && old != m->animations.end() && (old->second.flags & (kAnimOverride | kAnimLoop))) {
        const Animation &o = old->second;
        if (o.blendStart == time) {
            // Set again before its blend began: only the blend's length changes.
            a.blendStart = o.blendStart;
            a.blendFrame = o.blendFrame;
            a.blendLerpFrame = o.blendLerpFrame;
        } else {
            blendFrom(o, time, a);
        }
        a.blendTime = blendTime;
    } else {
        // Nothing playing to blend from.
        a.flags &= ~kAnimBlend;
    }
    m->animations[b] = a;
    m->posedTime = -1;
    return true;
}

void Ghoul2Instances::blendFrom(const Animation &from, int32_t time, Animation &to) {
    // The last animation's frame now, as the original finds it for this:
    // past the end, a looping one comes round, a frozen one holds its last
    // frame, any other is taken as at frame 0.
    float frame = float(from.startFrame) + std::max(0.0f, float(time - from.startTime) / 50.0f) * from.speed;
    const int size = from.endFrame - from.startFrame;
    const float end = float(from.endFrame);
    const bool loop = (from.flags & kAnimLoop) != 0;
    if (size != 0 && ((from.speed > 0 && frame > end - 1) || (from.speed < 0 && frame < end + 1))) {
        if (loop) {
            if ((from.speed < 0 && frame <= end + 1) || (from.speed > 0 && frame >= end))
                frame = end + std::fmod(frame - end, float(size)) - float(size);
        } else if ((from.flags & kAnimFreeze) == kAnimFreeze) {
            frame = from.speed > 0 ? end - 1 : end + 1;
        } else {
            frame = 0;
        }
    }
    to.blendStart = time;
    if (from.speed < 0) {
        to.blendFrame = std::floor(frame);
        to.blendLerpFrame = int(std::floor(frame));
        return;
    }
    to.blendFrame = frame;
    to.blendLerpFrame = int(frame + 1);
    // Off the end: the first frame if it loops, else the last (0 for an
    // animation ending at 0, where the original sets the other frame).
    if (to.blendFrame >= end) {
        if (loop)
            to.blendFrame = float(from.startFrame);
        else if (from.endFrame <= 0)
            to.blendLerpFrame = 0;
        else
            to.blendFrame = end - 1;
    }
    if (to.blendLerpFrame >= from.endFrame)
        to.blendLerpFrame = loop ? from.startFrame : std::max(0, from.endFrame - 1);
}

bool Ghoul2Instances::setAngles(int32_t handle, int32_t modelIndex, const std::string &bone, const Vec3 &angles,
                                int32_t flags, Axis up, Axis right, Axis forward) {
    Model *m = model(handle, modelIndex);
    const int b = m ? boneIndex(*m, bone) : -1;
    if (b < 0)
        return false;
    if (!(flags & (kAnglesPreMult | kAnglesPostMult | kAnglesReplace))) {
        m->angles.erase(b);
        m->angleFlags.erase(b);
        m->posedTime = -1;
        return true;
    }
    // The angles moved onto the axes the bone turns about, as the
    // original's: yaw about `up`, pitch about `right`, roll about `forward`.
    Vec3 a{};
    switch (up) {
    case kNegativeX:
        a[1] = angles[2] + 180;
        break;
    case kPositiveX:
        a[1] = angles[2];
        break;
    case kNegativeY:
    case kPositiveY:
        a[1] = angles[0];
        break;
    case kNegativeZ:
        a[1] = angles[1] + 180;
        break;
    case kPositiveZ:
        a[1] = angles[1];
        break;
    default:
        break;
    }
    switch (right) {
    case kNegativeX:
        a[0] = angles[2];
        break;
    case kPositiveX:
        a[0] = angles[2] + 180;
        break;
    case kNegativeY:
        a[0] = angles[0];
        break;
    case kPositiveY:
        a[0] = angles[0] + 180;
        break;
    case kNegativeZ:
    case kPositiveZ:
        a[0] = angles[1];
        break;
    default:
        break;
    }
    switch (forward) {
    case kNegativeX:
    case kPositiveX:
        a[2] = angles[2];
        break;
    case kNegativeY:
        a[2] = angles[0];
        break;
    case kPositiveY:
        a[2] = angles[0] + 180;
        break;
    case kNegativeZ:
        a[2] = angles[1];
        break;
    case kPositiveZ:
        a[2] = angles[1] + 180;
        break;
    default:
        break;
    }
    // Turned in the bone's own frame: its base pose, the turn, back.
    const GlaBone &gb = m->skeleton->bones[size_t(b)];
    m->angles[b] = mul(gb.basePose, mul(anglesMatrix(a), gb.basePoseInverse));
    m->angleFlags[b] = flags;
    m->posedTime = -1;
    return true;
}

bool Ghoul2Instances::setSurfaceOnOff(int32_t handle, const std::string &surface, int32_t flags) {
    const auto it = _instances.find(handle);
    if (it == _instances.end())
        return false;
    bool found = false;
    for (Model &m : it->second.models) {
        if (!m.used || !m.glm)
            continue;
        for (size_t i = 0; i < m.glm->surfaces.size(); ++i)
            if (fs::toLower(m.glm->surfaces[i].name) == fs::toLower(surface)) {
                m.surfaceFlags[int(i)] = flags;
                found = true;
            }
    }
    return found;
}

bool Ghoul2Instances::setRootSurface(int32_t handle, int32_t modelIndex, const std::string &surface) {
    // Only the surface and those under it are drawn: the rest are turned off.
    Model *m = model(handle, modelIndex);
    if (!m)
        return false;
    int root = -1;
    for (size_t i = 0; i < m->glm->surfaces.size(); ++i)
        if (fs::toLower(m->glm->surfaces[i].name) == fs::toLower(surface))
            root = int(i);
    if (root < 0)
        return false;
    std::vector<bool> keep(m->glm->surfaces.size());
    auto mark = [&](auto &self, int s) -> void {
        keep[size_t(s)] = true;
        for (int c : m->glm->surfaces[size_t(s)].children)
            self(self, c);
    };
    mark(mark, root);
    for (size_t i = 0; i < keep.size(); ++i)
        if (!keep[i])
            m->surfaceFlags[int(i)] = kGlmSurfaceOff;
    return true;
}

void Ghoul2Instances::poseModel(const Model &m, int32_t time, std::vector<Bone34> &bones) const {
    // A model hung from another's bolt is placed on the bolt instead
    // (modelPlacement).
    const Bone34 &root = m.boltLink >= 0 ? kIdentity : kRootPlacement;
    const GlaSkeleton *sk = m.skeleton;
    if (!sk || sk->frameCount <= 0) {
        bones.assign(1, root);
        return;
    }
    const size_t n = sk->bones.size();
    bones.assign(n, kIdentity);
    struct Timing {
        int current = 0;
        int next = 0;
        float towards = 0;
        // Fading in from a pose of the last animation's, `blendIn` of the way.
        bool blend = false;
        float blendFrame = 0;
        int blendLerpFrame = 0;
        float blendIn = 0;
    };
    std::vector<Timing> timing(n);
    std::vector<bool> done(n);
    auto pose = [&](auto &self, size_t i) -> void {
        if (done[i])
            return;
        const int parent = sk->bones[i].parent;
        if (parent >= 0)
            self(self, size_t(parent));
        // Its own animation, else its parent's frames (the root's: the first).
        if (const auto a = m.animations.find(int(i)); a != m.animations.end()) {
            const Animation &an = a->second;
            timing[i] = {};
            animationFrames(an.startFrame, an.endFrame, an.flags, an.speed, an.startTime, time, sk->frameCount,
                            timing[i].current, timing[i].next, timing[i].towards);
            const float since = float(time - an.blendStart);
            if ((an.flags & kAnimBlend) && since >= 0 && since < float(an.blendTime)) {
                timing[i].blend = true;
                timing[i].blendFrame = an.blendFrame;
                timing[i].blendLerpFrame = an.blendLerpFrame;
                timing[i].blendIn = since / float(an.blendTime);
            }
        } else if (parent >= 0) {
            timing[i] = timing[size_t(parent)];
        }
        Bone34 local = sk->bone(timing[i].current, int(i));
        if (timing[i].towards > 0 && timing[i].next != timing[i].current) {
            const Bone34 next = sk->bone(timing[i].next, int(i));
            for (size_t k = 0; k < 12; ++k)
                local[k] = local[k] * (1 - timing[i].towards) + next[k] * timing[i].towards;
        }
        if (timing[i].blend) {
            // The pose blended from: its two frames weighted the other way
            // round from its fraction, as the original's are.
            const Timing &t = timing[i];
            const int first = t.blendFrame >= 0 && t.blendFrame < float(sk->frameCount) ? int(t.blendFrame) : 0;
            const int second = std::clamp(t.blendLerpFrame, 0, sk->frameCount - 1);
            const float back = t.blendFrame - float(int(t.blendFrame));
            const Bone34 a = sk->bone(first, int(i));
            const Bone34 b = sk->bone(second, int(i));
            for (size_t k = 0; k < 12; ++k)
                local[k] = local[k] * t.blendIn + (a[k] * back + b[k] * (1 - back)) * (1 - t.blendIn);
        }
        const Bone34 &above = parent >= 0 ? bones[size_t(parent)] : root;
        const auto ang = m.angles.find(int(i));
        const int32_t flags = ang != m.angles.end() ? m.angleFlags.at(int(i)) : 0;
        if (flags & kAnglesPreMult)
            bones[i] = mul(above, ang->second);
        else
            bones[i] = mul(above, local);
        // The replacing kind is taken as turning after, as Jedi Outcast's
        // players use the latter.
        if (flags & (kAnglesPostMult | kAnglesReplace))
            bones[i] = mul(bones[i], ang->second);
        done[i] = true;
    };
    for (size_t i = 0; i < n; ++i)
        pose(pose, i);
}

const std::vector<Bone34> &Ghoul2Instances::posed(const Model &m, int32_t time) const {
    if (m.posedTime != time || m.posed.empty()) {
        poseModel(m, time, m.posed);
        m.posedTime = time;
    }
    return m.posed;
}

bool Ghoul2Instances::pose(int32_t handle, int32_t modelIndex, int32_t time, std::vector<Bone34> &bones) {
    const Model *m = model(handle, modelIndex);
    if (!m)
        return false;
    bones = posed(*m, time);
    return true;
}

Bone34 Ghoul2Instances::boltInModel(const Model &m, const Bolt &b, const std::vector<Bone34> &bones) const {
    if (b.bone >= 0 && m.skeleton && size_t(b.bone) < bones.size())
        return mul(bones[size_t(b.bone)], m.skeleton->bones[size_t(b.bone)].basePose);
    if (b.surface < 0 || m.glm->lods.empty() || size_t(b.surface) >= m.glm->lods[0].size())
        return kIdentity;
    // A tag surface: its triangle's three corners as placed, the axes from
    // its longest and shortest sides, the third corner the origin.
    const GlmSurface &s = m.glm->lods[0][size_t(b.surface)];
    if (s.vertices.size() < 3)
        return kIdentity;
    std::vector<Md3Vertex> placed;
    skinSurface(s, bones, placed);
    Vec3 tri[3];
    for (int j = 0; j < 3; ++j)
        tri[j] = {placed[size_t(j)].pos[0], placed[size_t(j)].pos[1], placed[size_t(j)].pos[2]};
    Vec3 sides[3];
    for (int j = 0; j < 3; ++j)
        for (int k = 0; k < 3; ++k)
            sides[j][size_t(k)] = tri[(j + 1) % 3][size_t(k)] - tri[j][size_t(k)];
    Vec3 axes[3];
    axes[0] = normalized(sides[0]); // the longest side
    axes[1] = normalized(sides[2]); // the shortest
    const float d = axes[0][0] * axes[1][0] + axes[0][1] * axes[1][1] + axes[0][2] * axes[1][2];
    for (int k = 0; k < 3; ++k)
        axes[0][size_t(k)] -= d * axes[1][size_t(k)];
    axes[0] = normalized(axes[0]);
    axes[2] = normalized(cross(sides[0], sides[2]));
    Bone34 out{};
    for (int r = 0; r < 3; ++r) {
        out[size_t(r * 4 + 0)] = axes[1][size_t(r)];
        out[size_t(r * 4 + 1)] = axes[0][size_t(r)];
        out[size_t(r * 4 + 2)] = -axes[2][size_t(r)];
        out[size_t(r * 4 + 3)] = tri[2][size_t(r)];
    }
    return out;
}

Bone34 Ghoul2Instances::modelPlacement(Instance &inst, const Model &m, int32_t time, int depth) {
    if (m.boltLink < 0 || depth > 4)
        return kIdentity;
    const int32_t parentIndex = (m.boltLink >> 10) & 1023;
    const int32_t bolt = m.boltLink & 1023;
    if (size_t(parentIndex) >= inst.models.size())
        return kIdentity;
    const Model &parent = inst.models[size_t(parentIndex)];
    if (!parent.used || size_t(bolt) >= parent.bolts.size())
        return kIdentity;
    return mul(modelPlacement(inst, parent, time, depth + 1),
               boltInModel(parent, parent.bolts[size_t(bolt)], posed(parent, time)));
}

bool Ghoul2Instances::boltMatrix(int32_t handle, int32_t modelIndex, int32_t bolt, int32_t time, const Vec3 &angles,
                                 const Vec3 &origin, const Vec3 &scale, Bone34 &out) {
    const auto it = _instances.find(handle);
    const Model *m = model(handle, modelIndex);
    if (!m || bolt < 0 || size_t(bolt) >= m->bolts.size())
        return false;
    Bone34 use = mul(modelPlacement(it->second, *m, time, 0), boltInModel(*m, m->bolts[size_t(bolt)], posed(*m, time)));
    for (int r = 0; r < 3; ++r)
        if (scale[size_t(r)] != 0)
            use[size_t(r * 4 + 3)] *= scale[size_t(r)];
    Bone34 world = anglesMatrix(angles);
    for (int r = 0; r < 3; ++r)
        world[size_t(r * 4 + 3)] = origin[size_t(r)];
    Bone34 placed = mul(world, use);
    // The original turns the result's axes a further 270 degrees of yaw.
    const Vec3 at{placed[3], placed[7], placed[11]};
    placed[3] = placed[7] = placed[11] = 0;
    out = mul(placed, anglesMatrix({0, 270, 0}));
    out[3] = at[0];
    out[7] = at[1];
    out[11] = at[2];
    return true;
}

bool Ghoul2Instances::drawn(int32_t handle, int32_t time, std::vector<Drawn> &out) {
    const auto it = _instances.find(handle);
    if (it == _instances.end())
        return false;
    for (const Model &m : it->second.models) {
        if (!m.used || !m.glm)
            continue;
        Drawn d;
        d.glm = m.glm;
        d.skeleton = m.skeleton;
        d.file = m.file;
        d.customSkin = m.customSkin;
        d.bones = posed(m, time);
        d.placement = modelPlacement(it->second, m, time, 0);
        d.surfaceOn.resize(m.glm->surfaces.size());
        for (size_t i = 0; i < d.surfaceOn.size(); ++i) {
            uint32_t flags = m.glm->surfaces[i].flags;
            if (const auto f = m.surfaceFlags.find(int(i)); f != m.surfaceFlags.end())
                flags = (flags & kGlmSurfaceTag) | uint32_t(f->second);
            d.surfaceOn[i] = (flags & (kGlmSurfaceTag | kGlmSurfaceOff)) == 0;
        }
        out.push_back(std::move(d));
    }
    return true;
}

int32_t Ghoul2Instances::call(Qvm &vm, Call which, const int32_t *a) {
    auto str = [&](int i) { return vm.string(a[i], 256); };
    auto vec = [&](int i) {
        Vec3 v{};
        if (a[i] && vm.valid(a[i], 12))
            for (int k = 0; k < 3; ++k)
                v[size_t(k)] = Qvm::asFloat(vm.readInt(a[i] + 4 * k));
        return v;
    };
    // The instance a call names by the address of the word holding it.
    auto held = [&](int i) { return vm.valid(a[i], 4) ? vm.readInt(a[i]) : 0; };
    switch (which) {
    case kListBones:
    case kListSurfaces:
    case kSetModels:
        return 0;
    case kHaveWeGhoulModels: {
        const auto it = _instances.find(a[0]);
        if (it == _instances.end())
            return 0;
        for (const Model &m : it->second.models)
            if (m.used)
                return 1;
        return 0;
    }
    case kGetBolt:
    case kGetBoltNoRec: {
        Bone34 m{};
        if (!vm.checkWrite(a[3], 48) || !boltMatrix(a[0], a[1], a[2], a[6], vec(4), vec(5), vec(8), m))
            return 0;
        for (int i = 0; i < 12; ++i)
            vm.writeInt(a[3] + 4 * i, Qvm::fromFloat(m[size_t(i)]));
        return 1;
    }
    case kInitGhoul2Model: {
        if (!vm.checkWrite(a[0], 4))
            return -1;
        int32_t handle = vm.readInt(a[0]);
        const int32_t index = addModel(handle, str(1), a[2], a[3]);
        if (handle)
            vm.writeInt(a[0], handle);
        return index;
    }
    case kAddBolt:
        return addBolt(a[0], a[1], str(2));
    case kSetBoltInfo:
        setBoltLink(a[0], a[1], a[2]);
        return 0;
    case kAngleOverride:
        return setAngles(a[0], a[1], str(2), vec(3), a[4], Axis(a[5]), Axis(a[6]), Axis(a[7])) ? 1 : 0;
    case kPlayAnim:
        return setAnimation(a[0], a[1], str(2), a[3], a[4], a[5], Qvm::asFloat(a[6]), a[7], Qvm::asFloat(a[8]), a[9])
                   ? 1
                   : 0;
    case kGetGlaName: {
        const std::string name = skeletonName(a[0], a[1]);
        vm.writeString(a[2], name, 64);
        return 0;
    }
    case kCopyGhoul2Instance: {
        // Every model (index -1) or the one given, into the other instance.
        const auto it = _instances.find(a[0]);
        if (it == _instances.end() || !_instances.count(a[1]))
            return 0;
        for (size_t i = 0; i < it->second.models.size(); ++i)
            if (a[2] < 0 || int32_t(i) == a[2])
                copyModel(a[0], int32_t(i), a[1], int32_t(i));
        return 1;
    }
    case kCopySpecificModel:
        copyModel(a[0], a[1], a[2], a[3]);
        return 0;
    case kDuplicateInstance:
        if (vm.checkWrite(a[1], 4))
            vm.writeInt(a[1], duplicate(a[0]));
        return 0;
    case kHasGhoul2ModelOnIndex:
        return hasModel(held(0), a[1]) ? 1 : 0;
    case kRemoveGhoul2Model:
        return removeModel(held(0), a[1]) ? 1 : 0;
    case kCleanModels:
        if (vm.checkWrite(a[0], 4)) {
            remove(vm.readInt(a[0]));
            vm.writeInt(a[0], 0);
        }
        return 0;
    case kGiveMeVectorFromMatrix: {
        // An axis of a bolt's matrix (a column), or its origin.
        if (!vm.valid(a[0], 48) || !vm.checkWrite(a[2], 12))
            return 0;
        auto m = [&](int r, int c) { return Qvm::asFloat(vm.readInt(a[0] + 4 * (4 * r + c))); };
        int column = 3;
        float sign = 1;
        switch (Axis(a[1])) {
        case kPositiveX:
            column = 0;
            break;
        case kPositiveY:
            column = 1;
            break;
        case kPositiveZ:
            column = 2;
            break;
        case kNegativeX:
            column = 0;
            sign = -1;
            break;
        case kNegativeY:
            column = 1;
            sign = -1;
            break;
        case kNegativeZ:
            column = 2;
            sign = -1;
            break;
        default:
            break;
        }
        for (int r = 0; r < 3; ++r)
            vm.writeInt(a[2] + 4 * r, Qvm::fromFloat(sign * m(r, column)));
        return 0;
    }
    case kSetRootSurface:
        return setRootSurface(a[0], a[1], str(2)) ? 1 : 0;
    case kSetSurfaceOnOff:
        return setSurfaceOnOff(a[0], str(1), a[2]) ? 1 : 0;
    case kSetNewOrigin:
        return 1;
    }
    return 0;
}

} // namespace reluva::idtech3::jk2
