// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Jedi Outcast's single-player Ghoul2 functions, on the instances as the
// game sees them in its memory (docs/engines/idtech3/games/jk2/systems/sp-ghoul2.md).

#include "engines/idtech3/eliteforce/sp_game_host.h"

#include "common/fs.h"
#include "engines/idtech3/formats/ghoul2.h"
#include "engines/idtech3/jk2/ghoul2_instances.h"

#include <algorithm>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

// A Visual C++ 6 std::vector: its allocator's word, then its first, last
// and end pointers.
constexpr uint32_t kVectorSize = 16;
constexpr uint32_t kFirst = 4;
constexpr uint32_t kLast = 8;
constexpr uint32_t kEnd = 12;

// boltInfo_t (16 bytes): bone, surface, surface type, uses.
constexpr uint32_t kBoltSize = 16;

int surfaceNamed(const GlmModel &mesh, const std::string &name) {
    for (size_t i = 0; i < mesh.surfaces.size(); ++i)
        if (fs::toLower(mesh.surfaces[i].name) == fs::toLower(name))
            return int(i);
    return -1;
}

int boneNamed(const GlaSkeleton &skeleton, const std::string &name) {
    for (size_t i = 0; i < skeleton.bones.size(); ++i)
        if (fs::toLower(skeleton.bones[i].name) == fs::toLower(name))
            return int(i);
    return -1;
}

// boneInfo_t (148 bytes, 0x456e05): its bone, a turn's matrix, flags, the
// animation's frames, times and speed, the blend's, the turn's blend, and
// the matrix made from the turn.
constexpr uint32_t kBoneSize = 148;

constexpr uint32_t kBoneFlags = 52;
constexpr uint32_t kBoneMatrix = 4;
constexpr uint32_t kBoneStartFrame = 56;
constexpr uint32_t kBoneEndFrame = 60;
constexpr uint32_t kBoneStartTime = 64;
constexpr uint32_t kBonePauseTime = 68;
constexpr uint32_t kBoneSpeed = 72;
constexpr uint32_t kBoneBlendFrame = 76;
constexpr uint32_t kBoneBlendLerpFrame = 80;
constexpr uint32_t kBoneBlendTime = 84;
constexpr uint32_t kBoneBlendStart = 88;
constexpr uint32_t kBoneAngleBlendTime = 92;
constexpr uint32_t kBoneAngleBlendStart = 96;
constexpr int32_t kAngleFlags = 0x7;                     // pre-multiplied, post-multiplied, replacing
constexpr int32_t kAnimFlags = 0x8 | 0x10 | 0x40 | 0x80; // override, loop, freeze, blend

float asFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

uint32_t floatWord(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// surfaceInfo_t (24 bytes, 0x45a877): flags, surface, and a generated
// surface's place (two barycentric coordinates, its polygon, its level of
// detail).
constexpr uint32_t kSurfaceRecordSize = 24;
constexpr uint32_t kSurfaceFlagsSet = 0x102; // off, and with nothing under it
constexpr int32_t kGeneratedSurfaces = 10000;

// CGhoul2Info (196 bytes, the game's own stride, 0x2001135a).
constexpr uint32_t kInfoSize = 0xc4;
constexpr uint32_t kSurfaces = 0;
constexpr uint32_t kBolts = 16;
constexpr uint32_t kBones = 32;
constexpr uint32_t kModelIndex = 48;
constexpr uint32_t kCustomShader = 52;
constexpr uint32_t kCustomSkin = 56;
constexpr uint32_t kModelBoltLink = 60;
constexpr uint32_t kSurfaceRoot = 64;
constexpr uint32_t kLodBias = 68;
constexpr uint32_t kNewOrigin = 72;
constexpr uint32_t kModel = 76;
constexpr uint32_t kFileName = 80; // 64 letters
constexpr uint32_t kAnimFrameDefault = 144;
constexpr uint32_t kSkelFrameNum = 148;
constexpr uint32_t kMeshFrameNum = 152;
constexpr uint32_t kFlags = 156;
constexpr uint32_t kSkin = 168;
constexpr uint32_t kValid = 172;

} // namespace

// --- The vectors ------------------------------------------------------------------

uint32_t SpGameHost::vectorCount(uint32_t vector, uint32_t elementSize) const {
    const uint32_t first = _cpu->read32(vector + kFirst);
    const uint32_t last = _cpu->read32(vector + kLast);
    return first && last > first ? (last - first) / elementSize : 0;
}

uint32_t SpGameHost::vectorAt(uint32_t vector, uint32_t elementSize, uint32_t index) const {
    return _cpu->read32(vector + kFirst) + index * elementSize;
}

uint32_t SpGameHost::vectorGrow(uint32_t vector, uint32_t elementSize, uint32_t count) {
    // Room for `count`, the elements kept, the new ones zeroed: what the
    // caller then fills in. Buffers come from the heap the game's own code
    // allocates from, so either side can free them.
    const uint32_t have = vectorCount(vector, elementSize);
    const uint32_t first = _cpu->read32(vector + kFirst);
    const uint32_t capacity = first ? (_cpu->read32(vector + kEnd) - first) / elementSize : 0;
    if (count > capacity) {
        const uint32_t room = std::max(count, capacity * 2);
        const uint32_t buffer = _process->memory().alloc(room * elementSize);
        if (!buffer)
            return 0;
        std::memset(_cpu->memory().data() + buffer, 0, room * elementSize);
        if (first) {
            std::memcpy(_cpu->memory().data() + buffer, _cpu->memory().data() + first, have * elementSize);
            _process->memory().free(first);
        }
        _cpu->write32(vector + kFirst, buffer);
        _cpu->write32(vector + kEnd, buffer + room * elementSize);
    }
    const uint32_t start = _cpu->read32(vector + kFirst);
    if (count > have)
        std::memset(_cpu->memory().data() + start + have * elementSize, 0, (count - have) * elementSize);
    _cpu->write32(vector + kLast, start + count * elementSize);
    return start;
}

void SpGameHost::vectorFree(uint32_t vector) {
    if (const uint32_t first = _cpu->read32(vector + kFirst))
        _process->memory().free(first);
    for (uint32_t o = kFirst; o < kVectorSize; o += 4)
        _cpu->write32(vector + o, 0);
}

// --- The engine's array of instances ----------------------------------------------

void SpGameHost::addGhoul2Array() {
    // An object with a table of five virtual functions (this in ECX, the
    // callee popping its arguments): New, Delete, IsValid, and Get twice
    // (the game calls the second, 0x10, for its own use).
    auto method = [this](const char *name, uint32_t argBytes, const std::function<uint32_t(x86::Cpu &)> &fn) {
        return _cpu->addNative(std::string("Ghoul2InfoArray::") + name, [fn, argBytes](x86::Cpu &c) {
            c.reg[x86::Cpu::EAX] = fn(c);
            return argBytes;
        });
    };
    const uint32_t table = _process->memory().alloc(5 * 4);
    _cpu->write32(table, method("New", 0, [this](x86::Cpu &) {
                      const uint32_t vector = _process->memory().alloc(kVectorSize);
                      std::memset(_cpu->memory().data() + vector, 0, kVectorSize);
                      const int32_t handle = _g2Next++;
                      _g2Instances[handle] = vector;
                      return uint32_t(handle);
                  }));
    _cpu->write32(table + 4, method("Delete", 4, [this](x86::Cpu &c) {
                      const auto it = _g2Instances.find(int32_t(c.arg(0)));
                      if (it != _g2Instances.end()) {
                          freeGhoul2Models(it->second);
                          _process->memory().free(it->second);
                          _g2Instances.erase(it);
                      }
                      return 0u;
                  }));
    _cpu->write32(table + 8, method("IsValid", 4,
                                    [this](x86::Cpu &c) { return _g2Instances.count(int32_t(c.arg(0))) ? 1u : 0u; }));
    auto get = [this](x86::Cpu &c) -> uint32_t {
        const auto it = _g2Instances.find(int32_t(c.arg(0)));
        if (it == _g2Instances.end()) {
            c.abort("Ghoul2InfoArray::Get: no instance " + std::to_string(int32_t(c.arg(0))));
            return 0;
        }
        return it->second;
    };
    _cpu->write32(table + 12, method("Get", 4, get));
    _cpu->write32(table + 16, method("Get", 4, get));
    _g2Array = _process->memory().alloc(4);
    _cpu->write32(_g2Array, table);
}

void SpGameHost::freeGhoul2Models(uint32_t vector) {
    // Each model's own lists, then the models.
    for (uint32_t i = 0, n = vectorCount(vector, kInfoSize); i < n; ++i) {
        const uint32_t info = vectorAt(vector, kInfoSize, i);
        vectorFree(info + kSurfaces);
        vectorFree(info + kBolts);
        vectorFree(info + kBones);
    }
    vectorFree(vector);
}

uint32_t SpGameHost::ghoul2Vector(uint32_t handleAddress) const {
    // A CGhoul2Info_v: the handle's word, 0 for none yet.
    if (!handleAddress || !_cpu->valid(handleAddress, 4))
        return 0;
    const auto it = _g2Instances.find(int32_t(_cpu->read32(handleAddress)));
    return it == _g2Instances.end() ? 0 : it->second;
}

bool SpGameHost::ghoul2Drawn(uint32_t handleAddress, int32_t time, std::vector<Ghoul2Pose> &out) {
    const uint32_t vector = ghoul2Vector(handleAddress);
    if (!vector)
        return false;
    const int32_t handle = int32_t(_cpu->read32(handleAddress));
    syncGhoul2Pose(handle, vector);
    return _g2Pose->drawn(handle, time, out);
}

bool SpGameHost::ghoul2Collide(int32_t n, const Vec3f &start, const Vec3f &end, const Vec3f &mins, const Vec3f &maxs,
                               jk2::Ghoul2Trace type, int lod, bool replaced, std::vector<jk2::Ghoul2Hit> &hits,
                               TraceResult &t) {
    // The entity's instance (0x160), its model scale (0xe8); not a saber's
    // blade (CONTENTS_LIGHTSABER), which keeps its box.
    constexpr uint32_t kGhoul2 = 0x160;
    constexpr uint32_t kModelScale = 0xe8;
    constexpr uint32_t kLightsaber = 0x40000;
    const uint32_t ent = entityAddress(n);
    if (!ent || (_cpu->read32(ent + uint32_t(_entity.contents)) & kLightsaber))
        return true;
    std::vector<Ghoul2Pose> models;
    if (!ghoul2Drawn(ent + kGhoul2, _levelTime, models) || models.empty())
        return true;
    // A client's model stands on its player's origin, turned only by its
    // yaw (0xac); anything else's by its own angles.
    Vec3 angles{};
    Vec3 origin{};
    if (const uint32_t ps = _cpu->read32(ent + uint32_t(_entity.client))) {
        angles[1] = readFloat(ps + 0xac);
        const Vec3f o = readVec(ps + 0x14);
        origin = {o[0], o[1], o[2]};
    } else {
        const Vec3f a = readVec(ent + uint32_t(_entity.currentAngles));
        const Vec3f o = readVec(ent + uint32_t(_entity.currentOrigin));
        angles = {a[0], a[1], a[2]};
        origin = {o[0], o[1], o[2]};
    }
    const Vec3f scale = readVec(ent + kModelScale);
    // A box's half width counts as the ray's (0x43ae47).
    const float radius = (maxs[0] - mins[0]) * 0.5f;
    // A nearer box brings fresh records; one only started in adds to them.
    const std::vector<jk2::Ghoul2Hit> before = hits;
    if (replaced)
        hits.clear();
    const size_t count = hits.size();
    jk2::traceGhoul2(models, jk2::Ghoul2Instances::worldMatrix(angles, origin), {scale[0], scale[1], scale[2]},
                     {start[0], start[1], start[2]}, {end[0], end[1], end[2]}, n, type, lod, radius, hits);
    if (hits.size() == count) {
        hits = before;
        return false;
    }
    for (const jk2::Ghoul2Hit &h : hits)
        if (h.entity == n) {
            t.normal = {h.normal[0], h.normal[1], h.normal[2]};
            break;
        }
    return true;
}

const SpGameHost::Ghoul2Files *SpGameHost::ghoul2Files(const std::string &name) {
    std::string key = fs::toLower(name);
    if (key.size() < 4 || key.compare(key.size() - 4, 4, ".glm") != 0)
        key += ".glm";
    if (const auto it = _g2Files.find(key); it != _g2Files.end())
        return it->second.get();
    auto files = std::make_unique<Ghoul2Files>();
    std::vector<uint8_t> data;
    if (_files.read(key, data) || parseGlm(data, files->mesh)) {
        _g2Files[key] = nullptr;
        return nullptr;
    }
    // Its skeleton by the name the mesh gives.
    if (files->mesh.animName != "*default" && !_files.read(files->mesh.animName + ".gla", data))
        files->hasSkeleton = !parseGla(data, files->skeleton);
    return (_g2Files[key] = std::move(files)).get();
}

const SpGameHost::Ghoul2Files *SpGameHost::ghoul2FilesOf(uint32_t info) {
    return info && _cpu->valid(info, kInfoSize) ? ghoul2Files(_cpu->string(info + kFileName, 64)) : nullptr;
}

int32_t SpGameHost::ghoul2BoneRecord(uint32_t info, const std::string &name, bool add) {
    // As jk2sp.exe's (0x456db0): the bone's record if there's one; else,
    // to add, the first unused (bone -1) or a new one, otherwise zeroed.
    const Ghoul2Files *files = ghoul2FilesOf(info);
    const int bone = files && files->hasSkeleton ? boneNamed(files->skeleton, name) : -1;
    if (bone < 0)
        return -1;
    const uint32_t list = info + kBones;
    const uint32_t count = vectorCount(list, kBoneSize);
    for (uint32_t i = 0; i < count; ++i)
        if (int32_t(_cpu->read32(vectorAt(list, kBoneSize, i))) == bone)
            return int32_t(i);
    if (!add)
        return -1;
    uint32_t place = 0;
    while (place < count && int32_t(_cpu->read32(vectorAt(list, kBoneSize, place))) != -1)
        ++place;
    if (place == count && !vectorGrow(list, kBoneSize, count + 1))
        return -1;
    const uint32_t record = vectorAt(list, kBoneSize, place);
    std::memset(_cpu->memory().data() + record, 0, kBoneSize);
    _cpu->write32(record, uint32_t(bone));
    return int32_t(place);
}

bool SpGameHost::ghoul2SetAngles(x86::Cpu &c, uint32_t info, int32_t record, int first) {
    // (…, angles, flags, up, right, forward, model list, blend time, time),
    // from argument `first`: as jk2sp.exe's (0x456280), the record's angle
    // flags replaced, its blend's time and start kept, and its matrix made
    // from the angles in the bone's own frame. The model's cached pose is
    // made stale (0x452a5d).
    const Ghoul2Files *files = ghoul2FilesOf(info);
    if (record < 0 || !files || !files->hasSkeleton)
        return false;
    const uint32_t r = vectorAt(info + kBones, kBoneSize, uint32_t(record));
    const int32_t bone = int32_t(_cpu->read32(r));
    if (bone < 0 || size_t(bone) >= files->skeleton.bones.size())
        return false;
    const uint32_t angles = c.arg(first);
    const int32_t flags = int32_t(c.arg(first + 1));
    const Vec3 a{asFloat(_cpu->read32(angles)), asFloat(_cpu->read32(angles + 4)), asFloat(_cpu->read32(angles + 8))};
    const Bone34 m = jk2::Ghoul2Instances::angleOverride(
        files->skeleton.bones[size_t(bone)], a, jk2::Ghoul2Instances::Axis(c.arg(first + 2)),
        jk2::Ghoul2Instances::Axis(c.arg(first + 3)), jk2::Ghoul2Instances::Axis(c.arg(first + 4)));
    _cpu->write32(r + kBoneFlags, uint32_t((int32_t(_cpu->read32(r + kBoneFlags)) & ~kAngleFlags) | flags));
    _cpu->write32(r + kBoneAngleBlendTime, c.arg(first + 6));
    _cpu->write32(r + kBoneAngleBlendStart, c.arg(first + 7));
    for (uint32_t i = 0; i < 12; ++i)
        _cpu->write32(r + kBoneMatrix + 4 * i, floatWord(m[i]));
    _cpu->write32(info + kSkelFrameNum, 0);
    return true;
}

bool SpGameHost::ghoul2SetAnimation(x86::Cpu &c, uint32_t info, int32_t record, int first) {
    // (…, start frame, end frame, flags, speed, time, frame to start at or
    // -1, blend time or -1), from argument `first`: the animation as
    // multiplayer's makes it (Ghoul2Instances::makeAnimation), written into
    // the record with its other flags kept.
    if (record < 0)
        return false;
    const uint32_t r = vectorAt(info + kBones, kBoneSize, uint32_t(record));
    using G2 = jk2::Ghoul2Instances;
    G2::Animation old;
    old.startFrame = int32_t(_cpu->read32(r + kBoneStartFrame));
    old.endFrame = int32_t(_cpu->read32(r + kBoneEndFrame));
    old.flags = int32_t(_cpu->read32(r + kBoneFlags)) & kAnimFlags;
    old.speed = asFloat(_cpu->read32(r + kBoneSpeed));
    old.startTime = int32_t(_cpu->read32(r + kBoneStartTime));
    old.blendStart = int32_t(_cpu->read32(r + kBoneBlendStart));
    old.blendTime = int32_t(_cpu->read32(r + kBoneBlendTime));
    old.blendFrame = asFloat(_cpu->read32(r + kBoneBlendFrame));
    old.blendLerpFrame = int32_t(_cpu->read32(r + kBoneBlendLerpFrame));
    const G2::Animation a = G2::makeAnimation(
        &old, int32_t(c.arg(first)), int32_t(c.arg(first + 1)), int32_t(c.arg(first + 2)), asFloat(c.arg(first + 3)),
        int32_t(c.arg(first + 4)), asFloat(c.arg(first + 5)), int32_t(c.arg(first + 6)));
    _cpu->write32(r + kBoneFlags, uint32_t((int32_t(_cpu->read32(r + kBoneFlags)) & ~kAnimFlags) | a.flags));
    _cpu->write32(r + kBoneStartFrame, uint32_t(a.startFrame));
    _cpu->write32(r + kBoneEndFrame, uint32_t(a.endFrame));
    _cpu->write32(r + kBoneStartTime, uint32_t(a.startTime));
    _cpu->write32(r + kBonePauseTime, 0);
    _cpu->write32(r + kBoneSpeed, floatWord(a.speed));
    _cpu->write32(r + kBoneBlendFrame, floatWord(a.blendFrame));
    _cpu->write32(r + kBoneBlendLerpFrame, uint32_t(a.blendLerpFrame));
    _cpu->write32(r + kBoneBlendTime, uint32_t(a.blendTime));
    _cpu->write32(r + kBoneBlendStart, uint32_t(a.blendStart));
    _cpu->write32(info + kSkelFrameNum, 0);
    return true;
}

bool SpGameHost::ghoul2HasModels(uint32_t handleAddress) const {
    const uint32_t vector = ghoul2Vector(handleAddress);
    for (uint32_t i = 0, n = vector ? vectorCount(vector, kInfoSize) : 0; i < n; ++i)
        if (int32_t(_cpu->read32(vectorAt(vector, kInfoSize, i) + kModelIndex)) != -1)
            return true;
    return false;
}

bool SpGameHost::ghoul2GetAnimation(x86::Cpu &c, uint32_t info, int32_t record, int first) {
    // (…, time, &frame, &start, &end, &flags, &speed, models), from argument
    // `first`: where an animation playing (overriding or looping) is; a
    // paused one where it was paused. None: all 0, and false.
    const uint32_t out[5] = {c.arg(first + 1), c.arg(first + 2), c.arg(first + 3), c.arg(first + 4), c.arg(first + 5)};
    auto clear = [&] {
        for (const uint32_t o : out)
            if (o && c.valid(o, 4))
                c.write32(o, 0);
        return false;
    };
    const Ghoul2Files *files = ghoul2FilesOf(info);
    if (!files || !files->hasSkeleton || record < 0 || uint32_t(record) >= vectorCount(info + kBones, kBoneSize))
        return clear();
    const uint32_t r = vectorAt(info + kBones, kBoneSize, uint32_t(record));
    const int32_t flags = int32_t(_cpu->read32(r + kBoneFlags));
    if (!(flags & (0x8 | 0x10)))
        return clear();
    const int32_t start = int32_t(_cpu->read32(r + kBoneStartFrame));
    const int32_t end = int32_t(_cpu->read32(r + kBoneEndFrame));
    const float speed = asFloat(_cpu->read32(r + kBoneSpeed));
    const int32_t paused = int32_t(_cpu->read32(r + kBonePauseTime));
    int current = 0;
    int next = 0;
    float towards = 0;
    jk2::Ghoul2Instances::animationFrames(start, end, flags, speed, int32_t(_cpu->read32(r + kBoneStartTime)),
                                          paused ? paused : int32_t(c.arg(first)), files->skeleton.frameCount, current,
                                          next, towards);
    const uint32_t values[5] = {floatWord(float(current) + towards), uint32_t(start), uint32_t(end), uint32_t(flags),
                                floatWord(speed)};
    for (int i = 0; i < 5; ++i)
        if (out[i] && c.valid(out[i], 4))
            c.write32(out[i], values[i]);
    return true;
}

bool SpGameHost::ghoul2ClearBone(uint32_t info, int32_t record, int32_t mask) {
    if (!info || record < 0 || uint32_t(record) >= vectorCount(info + kBones, kBoneSize))
        return false;
    const uint32_t r = vectorAt(info + kBones, kBoneSize, uint32_t(record));
    const int32_t flags = int32_t(_cpu->read32(r + kBoneFlags)) & ~mask;
    _cpu->write32(r + kBoneFlags, uint32_t(flags));
    if (!(flags & (kAngleFlags | kAnimFlags)))
        _cpu->write32(r, 0xffffffffu);
    _cpu->write32(info + kSkelFrameNum, 0);
    return true;
}

void SpGameHost::ghoul2SetSkins(uint32_t handleAddress, uint32_t skinList) {
    const uint32_t vector = ghoul2Vector(handleAddress);
    for (uint32_t i = 0, n = vector ? vectorCount(vector, kInfoSize) : 0; i < n; ++i) {
        const uint32_t info = vectorAt(vector, kInfoSize, i);
        if (int32_t(_cpu->read32(info + kModelIndex)) == -1)
            continue;
        const uint32_t entry = skinList + 4 * _cpu->read32(info + kCustomSkin);
        if (_cpu->valid(entry, 4))
            _cpu->write32(info + kSkin, _cpu->read32(entry));
    }
}

void SpGameHost::syncGhoul2Pose(int32_t handle, uint32_t vector) {
    if (!_g2Pose)
        _g2Pose = std::make_unique<jk2::Ghoul2Instances>(_files);
    // A hash of the models' records and their lists' contents.
    uint64_t hash = 1469598103934665603ull;
    auto add = [&hash, this](uint32_t from, uint32_t bytes) {
        const uint8_t *p = _cpu->memory().data() + from;
        for (uint32_t i = 0; i < bytes; ++i)
            hash = (hash ^ p[i]) * 1099511628211ull;
    };
    const uint32_t count = vectorCount(vector, kInfoSize);
    add(_cpu->read32(vector + kFirst), count * kInfoSize);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t info = vectorAt(vector, kInfoSize, i);
        add(_cpu->read32(info + kSurfaces + kFirst),
            vectorCount(info + kSurfaces, kSurfaceRecordSize) * kSurfaceRecordSize);
        add(_cpu->read32(info + kBolts + kFirst), vectorCount(info + kBolts, kBoltSize) * kBoltSize);
        add(_cpu->read32(info + kBones + kFirst), vectorCount(info + kBones, kBoneSize) * kBoneSize);
    }
    if (const auto it = _g2PoseHashes.find(handle); it != _g2PoseHashes.end() && it->second == hash)
        return;
    _g2PoseHashes[handle] = hash;
    using G2 = jk2::Ghoul2Instances;
    std::vector<G2::ModelState> models(count);
    std::vector<std::map<int, G2::Animation>> animations(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t info = vectorAt(vector, kInfoSize, i);
        G2::ModelState &m = models[i];
        if (int32_t(_cpu->read32(info + kModelIndex)) == -1)
            continue;
        m.file = _cpu->string(info + kFileName, 64);
        // The skin G2_SetModels gave it (its custom skin is the game's
        // number for it, which the client game's list turns into this).
        m.customSkin = int32_t(_cpu->read32(info + kSkin));
        m.boltLink = int32_t(_cpu->read32(info + kModelBoltLink));
        for (uint32_t b = 0, n = vectorCount(info + kBolts, kBoltSize); b < n; ++b) {
            const uint32_t r = vectorAt(info + kBolts, kBoltSize, b);
            m.bolts.emplace_back(int32_t(_cpu->read32(r)), int32_t(_cpu->read32(r + 4)));
        }
        for (uint32_t s = 0, n = vectorCount(info + kSurfaces, kSurfaceRecordSize); s < n; ++s) {
            const uint32_t r = vectorAt(info + kSurfaces, kSurfaceRecordSize, s);
            m.surfaceFlags[int32_t(_cpu->read32(r + 4))] = int32_t(_cpu->read32(r));
        }
        for (uint32_t b = 0, n = vectorCount(info + kBones, kBoneSize); b < n; ++b) {
            const uint32_t r = vectorAt(info + kBones, kBoneSize, b);
            const int32_t bone = int32_t(_cpu->read32(r));
            const int32_t flags = int32_t(_cpu->read32(r + kBoneFlags));
            if (bone < 0)
                continue;
            if (flags & kAngleFlags) {
                Bone34 matrix{};
                for (uint32_t k = 0; k < 12; ++k)
                    matrix[k] = asFloat(_cpu->read32(r + kBoneMatrix + 4 * k));
                m.angles[bone] = matrix;
                m.angleFlags[bone] = flags & kAngleFlags;
            }
            if (flags & (0x8 | 0x10)) {
                G2::Animation a;
                a.startFrame = int32_t(_cpu->read32(r + kBoneStartFrame));
                a.endFrame = int32_t(_cpu->read32(r + kBoneEndFrame));
                a.flags = flags & kAnimFlags;
                a.speed = asFloat(_cpu->read32(r + kBoneSpeed));
                a.startTime = int32_t(_cpu->read32(r + kBoneStartTime));
                a.blendStart = int32_t(_cpu->read32(r + kBoneBlendStart));
                a.blendTime = int32_t(_cpu->read32(r + kBoneBlendTime));
                a.blendFrame = asFloat(_cpu->read32(r + kBoneBlendFrame));
                a.blendLerpFrame = int32_t(_cpu->read32(r + kBoneBlendLerpFrame));
                animations[i][bone] = a;
            }
        }
    }
    _g2Pose->setInstance(handle, models, animations);
}

// --- The engine's functions ------------------------------------------------------

void SpGameHost::addGhoul2Imports() {
    addGhoul2Array();
    auto slot = [this](const char *name, const std::function<uint32_t(x86::Cpu &)> &fn) {
        const int index = jediGhoul2Slot(name);
        if (index < 0)
            return;
        const uint32_t address = _cpu->addNative(std::string("engine[") + name + "]", [this, index, fn](x86::Cpu &c) {
            if (trace)
                trace(index, c.read32(c.reg[x86::Cpu::ESP]));
            c.reg[x86::Cpu::EAX] = fn(c);
            return 0u;
        });
        _cpu->write32(_imports + 4 * uint32_t(index), address);
    };
    slot("TheGhoul2InfoArray", [this](x86::Cpu &) { return _g2Array; });
    slot("G2API_PrecacheGhoul2Model", [this](x86::Cpu &c) -> uint32_t {
        // (name) -> its model handle, 0 if it can't be read.
        const std::string name = c.string(c.arg(0), 64);
        return ghoul2Files(name) ? uint32_t(_models.add(name)) : 0u;
    });
    // A model's skeleton's name (the mesh header's, without ".gla"), text
    // the engine keeps; 0 for none.
    auto animName = [this](int32_t model) -> uint32_t {
        if (const auto it = _g2AnimNames.find(model); it != _g2AnimNames.end())
            return it->second;
        const Ghoul2Files *files = ghoul2Files(_models.name(model));
        if (!files)
            return 0;
        const uint32_t a = _process->memory().alloc(uint32_t(files->mesh.animName.size()) + 1);
        _cpu->writeString(a, files->mesh.animName, uint32_t(files->mesh.animName.size()) + 1);
        return _g2AnimNames[model] = a;
    };
    slot("G2API_GetAnimFileNameIndex", [animName](x86::Cpu &c) { return animName(int32_t(c.arg(0))); });
    slot("G2API_GetGLAName", [this, animName](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *): its mesh's skeleton name (0x453810).
        const uint32_t info = c.arg(0);
        return ghoul2FilesOf(info) ? animName(int32_t(_cpu->read32(info + kModel))) : 0u;
    });
    slot("RE_GetAnimationCFG", [this](x86::Cpu &c) -> uint32_t {
        // (name, buffer or 0, size) -> the file's length, 0 if there's none;
        // its text copied into the buffer (0x46bda0).
        std::vector<uint8_t> data;
        if (_files.read(c.string(c.arg(0), 64), data))
            return 0;
        const std::string text(data.begin(), std::find(data.begin(), data.end(), uint8_t(0)));
        if (c.arg(1))
            c.writeString(c.arg(1), text, c.arg(2));
        return uint32_t(text.size());
    });
    slot("G2API_AddBolt", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, name) -> the bolt's place, or -1. As jk2sp.exe's
        // (0x4554d0): a surface of that name first, then a bone; a bolt
        // already there is used once more; else the first empty place, or
        // a new one.
        const uint32_t info = c.arg(0);
        const std::string name = c.string(c.arg(1), 64);
        const Ghoul2Files *files = ghoul2FilesOf(info);
        if (!files || name.empty())
            return 0xffffffffu;
        int surface = surfaceNamed(files->mesh, name);
        int bone = -1;
        if (surface < 0) {
            bone = files->hasSkeleton ? boneNamed(files->skeleton, name) : -1;
            if (bone < 0)
                return 0xffffffffu;
        }
        const uint32_t list = info + kBolts;
        const uint32_t count = vectorCount(list, kBoltSize);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t b = vectorAt(list, kBoltSize, i);
            if (surface >= 0 ? int32_t(_cpu->read32(b + 4)) == surface : int32_t(_cpu->read32(b)) == bone) {
                _cpu->write32(b + 12, _cpu->read32(b + 12) + 1);
                return i;
            }
        }
        uint32_t place = 0;
        while (place < count && !(int32_t(_cpu->read32(vectorAt(list, kBoltSize, place))) == -1 &&
                                  int32_t(_cpu->read32(vectorAt(list, kBoltSize, place) + 4)) == -1))
            ++place;
        if (place == count && !vectorGrow(list, kBoltSize, count + 1))
            return 0xffffffffu;
        const uint32_t b = vectorAt(list, kBoltSize, place);
        _cpu->write32(b, uint32_t(bone));
        _cpu->write32(b + 4, uint32_t(surface));
        _cpu->write32(b + 8, 0);
        _cpu->write32(b + 12, 1);
        return place;
    });
    slot("G2API_GetBoneIndex", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, bone name, add if it isn't there) -> its record's place, or -1.
        return uint32_t(ghoul2BoneRecord(c.arg(0), c.string(c.arg(1), 64), c.arg(2) != 0));
    });
    slot("G2API_SetBoneAngles", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, bone name, angles, flags, up, right, forward, models, blend time, time)
        const uint32_t info = c.arg(0);
        return ghoul2SetAngles(c, info, ghoul2BoneRecord(info, c.string(c.arg(1), 64), true), 2) ? 1u : 0u;
    });
    slot("G2API_SetBoneAnglesIndex", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, record, angles, flags, up, right, forward, models, blend time, time)
        const uint32_t info = c.arg(0);
        const int32_t record = int32_t(c.arg(1));
        if (!ghoul2FilesOf(info) || record < 0 || uint32_t(record) >= vectorCount(info + kBones, kBoneSize))
            return 0;
        return ghoul2SetAngles(c, info, record, 2) ? 1u : 0u;
    });
    slot("G2API_SetBoneAnim", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, bone name, start, end, flags, speed, time, frame to start at, blend time)
        const uint32_t info = c.arg(0);
        if (!ghoul2FilesOf(info))
            return 0;
        return ghoul2SetAnimation(c, info, ghoul2BoneRecord(info, c.string(c.arg(1), 64), true), 2) ? 1u : 0u;
    });
    slot("G2API_SetBoneAnimIndex", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, record, start, end, flags, speed, time, frame to start at, blend time)
        const uint32_t info = c.arg(0);
        const int32_t record = int32_t(c.arg(1));
        if (!ghoul2FilesOf(info) || record < 0 || uint32_t(record) >= vectorCount(info + kBones, kBoneSize))
            return 0;
        return ghoul2SetAnimation(c, info, record, 2) ? 1u : 0u;
    });
    slot("RE_RegisterSkin", [this](x86::Cpu &c) {
        // (name) -> its handle: a .skin file's surfaces and shaders, as the
        // client game's.
        return uint32_t(registerSkin(c.string(c.arg(0), 256)));
    });
    slot("G2API_HaveWeGhoul2Models", [this](x86::Cpu &c) { return ghoul2HasModels(c.arg(0)) ? 1u : 0u; });
    slot("G2API_GetBoneAnim", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, bone name, time, &frame, &start, &end, &flags, &speed, models)
        const uint32_t info = c.arg(0);
        return ghoul2GetAnimation(c, info, ghoul2BoneRecord(info, c.string(c.arg(1), 64), false), 2) ? 1u : 0u;
    });
    slot("G2API_GetBoneAnimIndex", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, record, time, &frame, &start, &end, &flags, &speed, models)
        return ghoul2GetAnimation(c, c.arg(0), int32_t(c.arg(1)), 2) ? 1u : 0u;
    });
    // Where a bone's animation runs: start and end frames, if it's playing.
    auto range = [this](x86::Cpu &c, uint32_t info, int32_t record) -> uint32_t {
        if (!ghoul2FilesOf(info) || record < 0 || uint32_t(record) >= vectorCount(info + kBones, kBoneSize))
            return 0;
        const uint32_t r = vectorAt(info + kBones, kBoneSize, uint32_t(record));
        if (!(_cpu->read32(r + kBoneFlags) & (0x8 | 0x10)))
            return 0;
        if (c.valid(c.arg(2), 4))
            c.write32(c.arg(2), _cpu->read32(r + kBoneStartFrame));
        if (c.valid(c.arg(3), 4))
            c.write32(c.arg(3), _cpu->read32(r + kBoneEndFrame));
        return 1;
    };
    slot("G2API_GetAnimRange", [this, range](x86::Cpu &c) {
        // (CGhoul2Info *, bone name, &start, &end)
        return range(c, c.arg(0), ghoul2BoneRecord(c.arg(0), c.string(c.arg(1), 64), false));
    });
    slot("G2API_GetAnimRangeIndex", [range](x86::Cpu &c) {
        // (CGhoul2Info *, record, &start, &end)
        return range(c, c.arg(0), int32_t(c.arg(1)));
    });
    // Pausing: a paused animation started later by the pause's length when
    // it goes on; a playing one stopped where it is.
    auto pause = [this](uint32_t info, int32_t record, int32_t time) -> uint32_t {
        if (!info || record < 0 || uint32_t(record) >= vectorCount(info + kBones, kBoneSize))
            return 0;
        const uint32_t r = vectorAt(info + kBones, kBoneSize, uint32_t(record));
        if (const int32_t paused = int32_t(_cpu->read32(r + kBonePauseTime))) {
            _cpu->write32(r + kBoneStartTime, _cpu->read32(r + kBoneStartTime) + uint32_t(time - paused));
            _cpu->write32(r + kBonePauseTime, 0);
        } else {
            _cpu->write32(r + kBonePauseTime, uint32_t(time));
        }
        return 1;
    };
    slot("G2API_PauseBoneAnim", [this, pause](x86::Cpu &c) {
        // (CGhoul2Info *, bone name, time)
        return pause(c.arg(0), ghoul2BoneRecord(c.arg(0), c.string(c.arg(1), 64), false), int32_t(c.arg(2)));
    });
    slot("G2API_PauseBoneAnimIndex",
         [pause](x86::Cpu &c) { return pause(c.arg(0), int32_t(c.arg(1)), int32_t(c.arg(2))); });
    slot("G2API_IsPaused", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, bone name)
        const int32_t record = ghoul2BoneRecord(c.arg(0), c.string(c.arg(1), 64), false);
        return record >= 0 && _cpu->read32(vectorAt(c.arg(0) + kBones, kBoneSize, uint32_t(record)) + kBonePauseTime)
                   ? 1u
                   : 0u;
    });
    slot("G2API_StopBoneAnim", [this](x86::Cpu &c) {
        // (CGhoul2Info *, bone name)
        const uint32_t info = c.arg(0);
        return ghoul2ClearBone(info, ghoul2BoneRecord(info, c.string(c.arg(1), 64), false), kAnimFlags) ? 1u : 0u;
    });
    slot("G2API_StopBoneAnimIndex",
         [this](x86::Cpu &c) { return ghoul2ClearBone(c.arg(0), int32_t(c.arg(1)), kAnimFlags) ? 1u : 0u; });
    slot("G2API_StopBoneAngles", [this](x86::Cpu &c) {
        // (CGhoul2Info *, bone name)
        const uint32_t info = c.arg(0);
        return ghoul2ClearBone(info, ghoul2BoneRecord(info, c.string(c.arg(1), 64), false), kAngleFlags) ? 1u : 0u;
    });
    slot("G2API_StopBoneAnglesIndex",
         [this](x86::Cpu &c) { return ghoul2ClearBone(c.arg(0), int32_t(c.arg(1)), kAngleFlags) ? 1u : 0u; });
    // A model's own settings.
    auto setField = [this](uint32_t field) {
        return [this, field](x86::Cpu &c) -> uint32_t {
            if (!ghoul2FilesOf(c.arg(0)))
                return 0;
            c.write32(c.arg(0) + field, c.arg(1));
            return 1;
        };
    };
    slot("G2API_SetLodBias", setField(kLodBias));
    slot("G2API_SetSkin", setField(kCustomSkin));
    slot("G2API_SetShader", setField(kCustomShader));
    slot("G2API_SetGhoul2ModelFlags", setField(kFlags));
    slot("G2API_GetGhoul2ModelFlags",
         [this](x86::Cpu &c) { return ghoul2FilesOf(c.arg(0)) ? _cpu->read32(c.arg(0) + kFlags) : 0u; });
    slot("G2API_SetRootSurface", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info_v &, model, surface name): drawn from that surface down.
        const uint32_t vector = ghoul2Vector(c.arg(0));
        const uint32_t model = c.arg(1);
        if (!vector || model >= vectorCount(vector, kInfoSize))
            return 0;
        const uint32_t info = vectorAt(vector, kInfoSize, model);
        const Ghoul2Files *files = ghoul2FilesOf(info);
        const int surface = files ? surfaceNamed(files->mesh, c.string(c.arg(2), 64)) : -1;
        if (surface < 0)
            return 0;
        c.write32(info + kSurfaceRoot, uint32_t(surface));
        return 1;
    });
    slot("G2API_GetSurfaceIndex", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, name) -> the mesh's surface of that name, or -1
        const Ghoul2Files *files = ghoul2FilesOf(c.arg(0));
        return uint32_t(files ? surfaceNamed(files->mesh, c.string(c.arg(1), 64)) : -1);
    });
    slot("G2API_GetSurfaceName", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, surface) -> its name, text the engine keeps; ""
        // if there's no such surface (0x453780).
        const uint32_t info = c.arg(0);
        const Ghoul2Files *files = ghoul2FilesOf(info);
        const int surface = int(c.arg(1));
        const bool found = files && surface >= 0 && size_t(surface) < files->mesh.surfaces.size();
        const auto key = std::pair{found ? int32_t(_cpu->read32(info + kModel)) : 0, found ? surface : -1};
        if (const auto it = _g2SurfaceNames.find(key); it != _g2SurfaceNames.end())
            return it->second;
        const std::string name = found ? files->mesh.surfaces[size_t(surface)].name : std::string();
        const uint32_t a = _process->memory().alloc(uint32_t(name.size()) + 1);
        _cpu->writeString(a, name, uint32_t(name.size()) + 1);
        return _g2SurfaceNames[key] = a;
    });
    slot("G2API_GetSurfaceRenderStatus", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, name) -> its flags: the override's, else the
        // mesh's own (0: drawn); -1 if there's no such surface.
        const uint32_t info = c.arg(0);
        const Ghoul2Files *files = ghoul2FilesOf(info);
        const int surface = files ? surfaceNamed(files->mesh, c.string(c.arg(1), 64)) : -1;
        if (surface < 0)
            return 0xffffffffu;
        for (uint32_t i = 0, n = vectorCount(info + kSurfaces, kSurfaceRecordSize); i < n; ++i) {
            const uint32_t r = vectorAt(info + kSurfaces, kSurfaceRecordSize, i);
            if (int32_t(_cpu->read32(r + 4)) == surface)
                return _cpu->read32(r);
        }
        return files->mesh.surfaces[size_t(surface)].flags;
    });
    slot("G2API_RemoveBolt", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, bolt): used once less, given back at none.
        const uint32_t info = c.arg(0);
        const uint32_t bolt = c.arg(1);
        if (!ghoul2FilesOf(info) || bolt >= vectorCount(info + kBolts, kBoltSize))
            return 0;
        const uint32_t b = vectorAt(info + kBolts, kBoltSize, bolt);
        const uint32_t uses = _cpu->read32(b + 12);
        _cpu->write32(b + 12, uses ? uses - 1 : 0);
        if (uses <= 1) {
            _cpu->write32(b, 0xffffffffu);
            _cpu->write32(b + 4, 0xffffffffu);
        }
        return 1;
    });
    slot("G2API_RemoveGhoul2Model", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info_v &, model): its place freed (model index -1), its
        // lists emptied.
        const uint32_t vector = ghoul2Vector(c.arg(0));
        const uint32_t model = c.arg(1);
        if (!vector || model >= vectorCount(vector, kInfoSize))
            return 0;
        const uint32_t info = vectorAt(vector, kInfoSize, model);
        vectorFree(info + kSurfaces);
        vectorFree(info + kBolts);
        vectorFree(info + kBones);
        c.write32(info + kModelIndex, 0xffffffffu);
        c.write32(info + kFileName, 0);
        c.write8(info + kValid, 0);
        return 1;
    });
    // Attaching: a model hung from another's bolt (its link the model's
    // place times 1024 plus the bolt), an entity's bolt word (the entity
    // times 2^20, the model times 1024, the bolt).
    slot("G2API_AttachG2Model", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, CGhoul2Info *to, bolt, model)
        if (!ghoul2FilesOf(c.arg(0)))
            return 0;
        c.write32(c.arg(0) + kModelBoltLink, ((c.arg(3) & 0x3ff) << 10) | (c.arg(2) & 0x3ff));
        return 1;
    });
    slot("G2API_DetachG2Model", [this](x86::Cpu &c) -> uint32_t {
        if (!ghoul2FilesOf(c.arg(0)))
            return 0;
        c.write32(c.arg(0) + kModelBoltLink, 0xffffffffu);
        return 1;
    });
    slot("G2API_AttachEnt", [](x86::Cpu &c) -> uint32_t {
        // (int *boltInfo, CGhoul2Info *to, bolt, entity, model)
        if (!c.valid(c.arg(0), 4))
            return 0;
        c.write32(c.arg(0), ((c.arg(4) & 0x3ff) << 10) | (c.arg(2) & 0x3ff) | ((c.arg(3) & 0x7ff) << 20));
        return 1;
    });
    slot("G2API_DetachEnt", [](x86::Cpu &c) {
        if (c.valid(c.arg(0), 4))
            c.write32(c.arg(0), 0);
        return 0u;
    });
    slot("G2API_GiveMeVectorFromMatrix", [](x86::Cpu &c) {
        // (mdxaBone_t &, orientation, vec3 &out): the origin, or an axis
        // (a column), negated for the negative ones.
        const uint32_t m = c.arg(0);
        const uint32_t which = c.arg(1);
        const uint32_t out = c.arg(2);
        if (!c.valid(m, 48) || !c.valid(out, 12))
            return 0u;
        // Columns: 0 x, 1 y, 2 z, 3 the origin; Eorientations: origin, +x, +z,
        // +y, -x, -z, -y.
        static const int kColumn[7] = {3, 0, 2, 1, 0, 2, 1};
        const int column = which < 7 ? kColumn[which] : 3;
        const bool negate = which >= 4;
        for (uint32_t i = 0; i < 3; ++i) {
            float v = asFloat(c.read32(m + 4 * (i * 4 + uint32_t(column))));
            c.write32(out + 4 * i, floatWord(negate ? -v : v));
        }
        return 0u;
    });
    slot("G2API_GetBoltMatrix", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info_v &, model, bolt, &matrix out, angles, origin, time,
        // models, scale): the bolt in the world, posed as the instance's
        // records say at that time.
        const uint32_t handleAddress = c.arg(0);
        const uint32_t vector = ghoul2Vector(handleAddress);
        const uint32_t out = c.arg(3);
        if (!vector || !c.valid(out, 48))
            return 0;
        const int32_t handle = int32_t(c.read32(handleAddress));
        syncGhoul2Pose(handle, vector);
        auto vec = [this](uint32_t a) -> Vec3 {
            if (!a || !_cpu->valid(a, 12))
                return {};
            return {asFloat(_cpu->read32(a)), asFloat(_cpu->read32(a + 4)), asFloat(_cpu->read32(a + 8))};
        };
        Bone34 m{};
        if (!_g2Pose->boltMatrix(handle, int32_t(c.arg(1)), int32_t(c.arg(2)), int32_t(c.arg(6)), vec(c.arg(4)),
                                 vec(c.arg(5)), vec(c.arg(8)), m))
            return 0;
        for (uint32_t k = 0; k < 12; ++k)
            c.write32(out + 4 * k, floatWord(m[k]));
        return 1;
    });
    slot("G2API_SetSurfaceOnOff", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info *, name, flags) -> whether the model has it. As
        // jk2sp.exe's (0x45a7b0): the surface's record (a generated
        // surface's left alone) takes the off and no-descendants flags
        // given; with none, a new one, the mesh's own flags for it changed
        // the same way.
        const uint32_t info = c.arg(0);
        const Ghoul2Files *files = ghoul2FilesOf(info);
        if (!files)
            return 0;
        const int surface = surfaceNamed(files->mesh, c.string(c.arg(1), 64));
        if (surface < 0)
            return 0;
        const uint32_t flags = c.arg(2) & kSurfaceFlagsSet;
        const uint32_t list = info + kSurfaces;
        const uint32_t count = vectorCount(list, kSurfaceRecordSize);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t r = vectorAt(list, kSurfaceRecordSize, i);
            const int32_t at = int32_t(_cpu->read32(r + 4));
            if (at == surface && at < kGeneratedSurfaces) {
                _cpu->write32(r, (_cpu->read32(r) & ~kSurfaceFlagsSet) | flags);
                return 1;
            }
        }
        if (!vectorGrow(list, kSurfaceRecordSize, count + 1))
            return 0;
        const uint32_t r = vectorAt(list, kSurfaceRecordSize, count);
        _cpu->write32(r, (files->mesh.surfaces[size_t(surface)].flags & ~kSurfaceFlagsSet) | flags);
        _cpu->write32(r + 4, uint32_t(surface));
        return 1;
    });
    slot("G2API_InitGhoul2Model", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info_v &, name, model index, skin, shader, flags, level
        // of detail bias) -> the model's place in the instance, or -1. As
        // jk2sp.exe's (0x451460): the first place whose model index is -1,
        // else a new one, filled with a new model's defaults.
        const uint32_t handle = c.arg(0);
        const std::string name = c.string(c.arg(1), 64);
        if (name.empty() || !c.valid(handle, 4))
            return 0xffffffffu;
        if (!c.read32(handle)) {
            const auto it = _g2Instances.emplace(_g2Next, _process->memory().alloc(kVectorSize)).first;
            std::memset(_cpu->memory().data() + it->second, 0, kVectorSize);
            c.write32(handle, uint32_t(_g2Next++));
        }
        const uint32_t vector = ghoul2Vector(handle);
        if (!vector)
            return 0xffffffffu;
        uint32_t index = 0;
        const uint32_t count = vectorCount(vector, kInfoSize);
        while (index < count && int32_t(_cpu->read32(vectorAt(vector, kInfoSize, index) + kModelIndex)) != -1)
            ++index;
        if (index == count && !vectorGrow(vector, kInfoSize, count + 1))
            return 0xffffffffu;
        const uint32_t info = vectorAt(vector, kInfoSize, index);
        vectorFree(info + kSurfaces);
        vectorFree(info + kBolts);
        vectorFree(info + kBones);
        std::memset(_cpu->memory().data() + info + kModelIndex, 0, kInfoSize - kModelIndex);
        for (const uint32_t o : {kModelIndex, kNewOrigin, kSkelFrameNum, kMeshFrameNum})
            _cpu->write32(info + o, 0xffffffffu);
        if (!ghoul2Files(name))
            return 0xffffffffu;
        _cpu->writeString(info + kFileName, name, 64);
        _cpu->write32(info + kModelIndex, index);
        _cpu->write32(info + kModel, uint32_t(_models.add(name)));
        _cpu->write32(info + kCustomSkin, c.arg(3));
        _cpu->write32(info + kCustomShader, c.arg(4));
        _cpu->write32(info + kFlags, c.arg(5));
        _cpu->write32(info + kLodBias, c.arg(6));
        _cpu->write32(info + kModelBoltLink, 0xffffffffu);
        _cpu->write8(info + kValid, 1);
        return index;
    });
    // Saved games: an instance as one buffer (jk2sp.exe's, 0x458d10), its
    // model count then for each its record from the model index to the
    // flags (112 bytes), and its surface overrides, bone overrides and
    // bolts, each list's count then its records as they are.
    constexpr uint32_t kSavedInfo = kFlags + 4 - kModelIndex;
    const std::array<std::pair<uint32_t, uint32_t>, 3> lists = {
        {{kSurfaces, kSurfaceRecordSize}, {kBones, kBoneSize}, {kBolts, kBoltSize}}};
    slot("G2API_SaveGhoul2Models", [this, lists](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info_v &, &buffer out, &size out) -> 1. The buffer is
        // the engine's; the game appends it to the save and gives it back
        // (G2API_FreeSaveBuffer).
        const uint32_t vector = ghoul2Vector(c.arg(0));
        const uint32_t count = vector ? vectorCount(vector, kInfoSize) : 0;
        std::vector<uint8_t> out;
        auto put32 = [&out](uint32_t v) {
            for (int i = 0; i < 4; ++i)
                out.push_back(uint8_t(v >> (8 * i)));
        };
        auto copy = [&out, this](uint32_t from, uint32_t bytes) {
            const uint8_t *p = _cpu->memory().data() + from;
            out.insert(out.end(), p, p + bytes);
        };
        put32(count);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t info = vectorAt(vector, kInfoSize, i);
            copy(info + kModelIndex, kSavedInfo);
            for (const auto &[list, size] : lists) {
                const uint32_t n = vectorCount(info + list, size);
                put32(n);
                if (n)
                    copy(vectorAt(info + list, size, 0), n * size);
            }
        }
        const uint32_t buffer = _process->memory().alloc(uint32_t(out.size()));
        if (!buffer || !c.valid(c.arg(1), 4) || !c.valid(c.arg(2), 4)) {
            c.abort("G2API_SaveGhoul2Models: no memory for the instance");
            return 0;
        }
        std::memcpy(_cpu->memory().data() + buffer, out.data(), out.size());
        c.write32(c.arg(1), buffer);
        c.write32(c.arg(2), uint32_t(out.size()));
        return 1;
    });
    slot("G2API_LoadGhoul2Models", [this, lists](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info_v &, buffer): the instance made as saved (0x4592e0);
        // each model with a file set up again, its index its place.
        const uint32_t handle = c.arg(0);
        uint32_t at = c.arg(1);
        if (!c.valid(handle, 4) || !c.valid(at, 4))
            return 0;
        const uint32_t count = c.read32(at);
        at += 4;
        if (!count)
            return 0;
        if (!ghoul2Vector(handle)) {
            const auto it = _g2Instances.emplace(_g2Next, _process->memory().alloc(kVectorSize)).first;
            std::memset(_cpu->memory().data() + it->second, 0, kVectorSize);
            c.write32(handle, uint32_t(_g2Next++));
        }
        const uint32_t vector = ghoul2Vector(handle);
        for (uint32_t i = count, n = vectorCount(vector, kInfoSize); i < n; ++i) {
            const uint32_t info = vectorAt(vector, kInfoSize, i);
            for (const auto &[list, size] : lists)
                vectorFree(info + list);
        }
        if (!vectorGrow(vector, kInfoSize, count)) {
            c.abort("G2API_LoadGhoul2Models: no memory for the instance");
            return 0;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t info = vectorAt(vector, kInfoSize, i);
            if (!c.valid(at, kSavedInfo))
                break;
            _cpu->write8(info + kValid, 0);
            std::memcpy(_cpu->memory().data() + info + kModelIndex, _cpu->memory().data() + at, kSavedInfo);
            at += kSavedInfo;
            const std::string file = _cpu->string(info + kFileName, 64);
            if (int32_t(_cpu->read32(info + kModelIndex)) != -1 && !file.empty()) {
                _cpu->write32(info + kModelIndex, i);
                ghoul2Files(file);
                _cpu->write32(info + kModel, uint32_t(_models.add(file)));
                _cpu->write8(info + kValid, 1);
            }
            for (const auto &[list, size] : lists) {
                if (!c.valid(at, 4))
                    break;
                const uint32_t n = c.read32(at);
                at += 4;
                vectorFree(info + list);
                if (!n)
                    continue;
                const uint32_t first = c.valid(at, n * size) ? vectorGrow(info + list, size, n) : 0;
                if (!first) {
                    c.abort("G2API_LoadGhoul2Models: a damaged instance");
                    return 0;
                }
                std::memcpy(_cpu->memory().data() + first, _cpu->memory().data() + at, n * size);
                at += n * size;
            }
        }
        _g2PoseHashes.erase(int32_t(c.read32(handle)));
        return 0;
    });
    slot("G2API_LoadSaveCodeDestructGhoul2Info", [this](x86::Cpu &c) -> uint32_t {
        // (CGhoul2Info_v &): the instance given back, its handle 0 (0x453900).
        const uint32_t handle = c.arg(0);
        if (!c.valid(handle, 4) || !c.read32(handle))
            return 0;
        if (const auto it = _g2Instances.find(int32_t(c.read32(handle))); it != _g2Instances.end()) {
            freeGhoul2Models(it->second);
            _process->memory().free(it->second);
            _g2Instances.erase(it);
        }
        c.write32(handle, 0);
        return 0;
    });
    slot("G2API_FreeSaveBuffer", [this](x86::Cpu &c) -> uint32_t {
        // (buffer): one G2API_SaveGhoul2Models made (0x4538f0).
        if (const uint32_t buffer = c.arg(0))
            _process->memory().free(buffer);
        return 0;
    });
    slot("G2API_CleanGhoul2Models", [this](x86::Cpu &c) {
        // (CGhoul2Info_v &): the instance given back, its handle 0.
        const uint32_t handle = c.arg(0);
        if (handle && c.valid(handle, 4) && c.read32(handle)) {
            const auto it = _g2Instances.find(int32_t(c.read32(handle)));
            if (it != _g2Instances.end()) {
                freeGhoul2Models(it->second);
                _process->memory().free(it->second);
                _g2Instances.erase(it);
            }
            c.write32(handle, 0);
        }
        return 0u;
    });
}

} // namespace reluva::idtech3::eliteforce
