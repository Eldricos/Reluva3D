// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Ghoul2 meshes (.glm) and skeletons (.gla). The layout, the packed bone
// weights, the compressed bones and how bones and vertices are posed were
// read from the GPL Jedi Outcast source (mdx_format.h, matcomp.c,
// tr_ghoul2.cpp) and checked against every one of Jedi Outcast's files;
// docs/engines/idtech3/formats/ghoul2.md describes them. This is new code
// written from that description.

#include "engines/idtech3/formats/ghoul2.h"

#include <cmath>
#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr int32_t kVersion = 6;
constexpr size_t kNameSize = 64;
constexpr size_t kGlmHeaderSize = 164;
constexpr size_t kGlaHeaderSize = 100;
constexpr size_t kSurfaceInfoSize = 144; // before its children's indexes
constexpr size_t kSurfaceSize = 40;
constexpr size_t kVertexSize = 32;
constexpr size_t kBoneHeaderSize = 172; // before its children's indexes
constexpr size_t kPackedBoneSize = 14;
constexpr int kMaxBoneRefs = 32; // a vertex's bone is 5 bits
constexpr int kMaxBones = 1024;  // far past any file's (72)
constexpr int kMaxLods = 8;

class Reader {
public:
    explicit Reader(const std::vector<uint8_t> &d) : _d(d) {}
    bool inside(int64_t at, int64_t size) const { return at >= 0 && size >= 0 && at + size <= int64_t(_d.size()); }
    int32_t i32(size_t at) const {
        return int32_t(uint32_t(_d[at]) | uint32_t(_d[at + 1]) << 8 | uint32_t(_d[at + 2]) << 16 |
                       uint32_t(_d[at + 3]) << 24);
    }
    uint32_t u32(size_t at) const { return uint32_t(i32(at)); }
    uint16_t u16(size_t at) const { return uint16_t(_d[at] | _d[at + 1] << 8); }
    float f32(size_t at) const {
        const uint32_t bits = u32(at);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    Vec3 vec3(size_t at) const { return {f32(at), f32(at + 4), f32(at + 8)}; }
    std::string name(size_t at) const {
        size_t len = 0;
        while (len < kNameSize && _d[at + len])
            ++len;
        return std::string(reinterpret_cast<const char *>(&_d[at]), len);
    }
    Bone34 bone(size_t at) const {
        Bone34 b{};
        for (size_t i = 0; i < 12; ++i)
            b[i] = f32(at + i * 4);
        return b;
    }
    uint8_t u8(size_t at) const { return _d[at]; }

private:
    const std::vector<uint8_t> &_d;
};

Bone34 multiply(const Bone34 &a, const Bone34 &b) {
    Bone34 out{};
    for (size_t r = 0; r < 3; ++r) {
        for (size_t c = 0; c < 4; ++c)
            out[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c];
        out[r * 4 + 3] += a[r * 4 + 3];
    }
    return out;
}

// The children's indexes after a record, each checked against `count`.
bool readChildren(const Reader &r, size_t at, int n, int count, std::vector<int> &out) {
    if (n < 0 || n > count || !r.inside(int64_t(at), int64_t(n) * 4))
        return false;
    for (int i = 0; i < n; ++i) {
        const int32_t c = r.i32(at + size_t(i) * 4);
        if (c < 0 || c >= count)
            return false;
        out.push_back(c);
    }
    return true;
}

Error readSurface(const Reader &r, size_t at, int index, int boneCount, GlmSurface &s) {
    const std::string where = "GLM surface " + std::to_string(index);
    if (!r.inside(int64_t(at), kSurfaceSize))
        return Error::fail(where + " lies outside the file");
    if (r.i32(at + 4) != index)
        return Error::fail(where + " says it's surface " + std::to_string(r.i32(at + 4)));
    const int32_t numVerts = r.i32(at + 12);
    const int32_t ofsVerts = r.i32(at + 16);
    const int32_t numTris = r.i32(at + 20);
    const int32_t ofsTris = r.i32(at + 24);
    const int32_t numRefs = r.i32(at + 28);
    const int32_t ofsRefs = r.i32(at + 32);
    if (numVerts < 0 || numTris < 0 || numRefs < 0 || numRefs > kMaxBoneRefs)
        return Error::fail(where + " has bad counts");
    // The texture coordinates follow the vertices.
    if (!r.inside(int64_t(at) + ofsVerts, int64_t(numVerts) * (kVertexSize + 8)) ||
        !r.inside(int64_t(at) + ofsTris, int64_t(numTris) * 12) ||
        !r.inside(int64_t(at) + ofsRefs, int64_t(numRefs) * 4))
        return Error::fail(where + "'s data lies outside the file");
    for (int i = 0; i < numRefs; ++i) {
        const int32_t b = r.i32(at + size_t(ofsRefs) + size_t(i) * 4);
        if (b < 0 || b >= boneCount)
            return Error::fail(where + ": bone " + std::to_string(b) + " (of " + std::to_string(boneCount) + ")");
        s.boneRefs.push_back(b);
    }
    for (int i = 0; i < numVerts; ++i) {
        const size_t v = at + size_t(ofsVerts) + size_t(i) * kVertexSize;
        GlmVertex out;
        out.normal = r.vec3(v);
        out.pos = r.vec3(v + 12);
        // Packed: the weight count less one in the top two bits; each
        // weight's bone in 5 bits from bit 0; the top two bits of each but
        // the last's 10-bit weight from bit 20, its low eight in the bytes
        // after. The last weight is what the others leave of 1.
        const uint32_t packed = r.u32(v + 24);
        out.weightCount = int(packed >> 30) + 1;
        float total = 0;
        for (int w = 0; w < out.weightCount; ++w) {
            const auto bone = uint8_t((packed >> (5 * w)) & 31);
            if (bone >= numRefs)
                return Error::fail(where + ": vertex " + std::to_string(i) + " uses bone reference " +
                                   std::to_string(bone) + " (of " + std::to_string(numRefs) + ")");
            out.bone[size_t(w)] = bone;
            if (w == out.weightCount - 1) {
                out.weight[size_t(w)] = 1 - total;
            } else {
                const uint32_t ten = r.u8(v + 28 + size_t(w)) | ((packed >> (12 + 2 * w)) & 0x300);
                out.weight[size_t(w)] = float(ten) / 1023.0f;
                total += out.weight[size_t(w)];
            }
        }
        s.vertices.push_back(out);
    }
    const size_t st = at + size_t(ofsVerts) + size_t(numVerts) * kVertexSize;
    for (int i = 0; i < numVerts; ++i)
        s.st.push_back({r.f32(st + size_t(i) * 8), r.f32(st + size_t(i) * 8 + 4)});
    for (int i = 0; i < numTris * 3; ++i) {
        const int32_t v = r.i32(at + size_t(ofsTris) + size_t(i) * 4);
        if (v < 0 || v >= numVerts)
            return Error::fail(where + ": triangle " + std::to_string(i / 3) + " uses vertex " + std::to_string(v) +
                               " (of " + std::to_string(numVerts) + ")");
        s.indices.push_back(uint32_t(v));
    }
    return Error::success();
}

} // namespace

Error parseGlm(const std::vector<uint8_t> &data, GlmModel &out) {
    out = GlmModel{};
    const Reader r(data);
    if (data.size() < kGlmHeaderSize || std::memcmp(data.data(), "2LGM", 4) != 0)
        return Error::fail("Not a GLM model");
    if (r.i32(4) != kVersion)
        return Error::fail("Unsupported GLM version " + std::to_string(r.i32(4)) + " (expected 6)");
    out.name = r.name(8);
    out.animName = r.name(72);
    out.boneCount = r.i32(140);
    const int32_t numLods = r.i32(144);
    const int32_t ofsLods = r.i32(148);
    const int32_t numSurfaces = r.i32(152);
    if (out.boneCount < 1 || out.boneCount > kMaxBones || numLods < 1 || numLods > kMaxLods || numSurfaces < 0 ||
        !r.inside(kGlmHeaderSize, int64_t(numSurfaces) * 4))
        return Error::fail("GLM header has bad counts");
    // Each surface's name, flags, shader and place in the hierarchy, through
    // offsets after the header (from where they start).
    for (int i = 0; i < numSurfaces; ++i) {
        const int64_t at = int64_t(kGlmHeaderSize) + r.i32(kGlmHeaderSize + size_t(i) * 4);
        if (!r.inside(at, kSurfaceInfoSize))
            return Error::fail("GLM surface " + std::to_string(i) + "'s name lies outside the file");
        GlmSurfaceInfo info;
        info.name = r.name(size_t(at));
        info.flags = r.u32(size_t(at) + 64);
        info.shader = r.name(size_t(at) + 68);
        info.parent = r.i32(size_t(at) + 136);
        if (info.parent < -1 || info.parent >= numSurfaces ||
            !readChildren(r, size_t(at) + kSurfaceInfoSize, r.i32(size_t(at) + 140), numSurfaces, info.children))
            return Error::fail("GLM surface " + std::to_string(i) + " (" + info.name + ") has a bad parent or child");
        out.surfaces.push_back(std::move(info));
    }
    // The levels of detail one after another; in each, offsets to its
    // surfaces from just after its own size.
    int64_t lod = ofsLods;
    for (int level = 0; level < numLods; ++level) {
        if (!r.inside(lod, 4 + int64_t(numSurfaces) * 4))
            return Error::fail("GLM level of detail " + std::to_string(level) + " lies outside the file");
        std::vector<GlmSurface> surfaces(static_cast<size_t>(numSurfaces));
        for (int i = 0; i < numSurfaces; ++i) {
            const int64_t at = lod + 4 + r.i32(size_t(lod) + 4 + size_t(i) * 4);
            if (!r.inside(at, kSurfaceSize))
                return Error::fail("GLM level " + std::to_string(level) + "'s surface " + std::to_string(i) +
                                   " lies outside the file");
            if (Error err = readSurface(r, size_t(at), i, out.boneCount, surfaces[size_t(i)]))
                return Error::fail(err.message + " (level " + std::to_string(level) + ")");
        }
        out.lods.push_back(std::move(surfaces));
        const int32_t size = r.i32(size_t(lod));
        if (size <= 0 && level + 1 < numLods)
            return Error::fail("GLM level of detail " + std::to_string(level) + " has a bad size");
        lod += size;
    }
    return Error::success();
}

Error parseGla(const std::vector<uint8_t> &data, GlaSkeleton &out) {
    out = GlaSkeleton{};
    const Reader r(data);
    if (data.size() < kGlaHeaderSize || std::memcmp(data.data(), "2LGA", 4) != 0)
        return Error::fail("Not a GLA skeleton");
    if (r.i32(4) != kVersion)
        return Error::fail("Unsupported GLA version " + std::to_string(r.i32(4)) + " (expected 6)");
    out.name = r.name(8);
    out.scale = r.f32(72);
    out.frameCount = r.i32(76);
    const int32_t ofsFrames = r.i32(80);
    const int32_t numBones = r.i32(84);
    const int32_t ofsPool = r.i32(88);
    const int32_t ofsEnd = r.i32(96);
    if (out.frameCount < 1 || numBones < 1 || numBones > kMaxBones || !r.inside(kGlaHeaderSize, int64_t(numBones) * 4))
        return Error::fail("GLA header has bad counts");
    for (int i = 0; i < numBones; ++i) {
        const int64_t at = int64_t(kGlaHeaderSize) + r.i32(kGlaHeaderSize + size_t(i) * 4);
        if (!r.inside(at, kBoneHeaderSize))
            return Error::fail("GLA bone " + std::to_string(i) + " lies outside the file");
        GlaBone b;
        b.name = r.name(size_t(at));
        b.flags = r.u32(size_t(at) + 64);
        b.parent = r.i32(size_t(at) + 68);
        b.basePose = r.bone(size_t(at) + 72);
        b.basePoseInverse = r.bone(size_t(at) + 120);
        if (b.parent < -1 || b.parent >= numBones || b.parent == i ||
            !readChildren(r, size_t(at) + kBoneHeaderSize, r.i32(size_t(at) + 168), numBones, b.children))
            return Error::fail("GLA bone " + std::to_string(i) + " (" + b.name + ") has a bad parent or child");
        out.bones.push_back(std::move(b));
    }
    // A bone may come before its parent (the humanoid skeleton's eyebrows
    // do), so posing follows the parents; each bone's must reach a root.
    for (size_t i = 0; i < out.bones.size(); ++i) {
        int at = int(i);
        for (size_t steps = 0; at >= 0; ++steps) {
            if (steps > out.bones.size())
                return Error::fail("GLA bone " + std::to_string(i) + "'s parents go round in a loop");
            at = out.bones[size_t(at)].parent;
        }
    }
    // Each frame's bones as 3-byte indexes into the pool of compressed ones.
    const int64_t indexBytes = int64_t(out.frameCount) * numBones * 3;
    if (!r.inside(ofsFrames, indexBytes) || ofsPool < ofsFrames + indexBytes || ofsEnd < ofsPool ||
        !r.inside(ofsPool, ofsEnd - ofsPool))
        return Error::fail("GLA frames lie outside the file");
    const size_t poolSize = size_t(ofsEnd - ofsPool) / kPackedBoneSize;
    out.frameBones.resize(size_t(out.frameCount) * size_t(numBones));
    for (size_t i = 0; i < out.frameBones.size(); ++i) {
        const size_t at = size_t(ofsFrames) + i * 3;
        const uint32_t index = r.u8(at) | uint32_t(r.u8(at + 1)) << 8 | uint32_t(r.u8(at + 2)) << 16;
        if (index >= poolSize)
            return Error::fail("GLA frame " + std::to_string(i / size_t(numBones)) + "'s bone " +
                               std::to_string(i % size_t(numBones)) + " is " + std::to_string(index) + " (of " +
                               std::to_string(poolSize) + ")");
        out.frameBones[i] = index;
    }
    out.pool.resize(poolSize);
    for (size_t i = 0; i < poolSize; ++i)
        std::memcpy(out.pool[i].data(), &data[size_t(ofsPool) + i * kPackedBoneSize], kPackedBoneSize);
    return Error::success();
}

Bone34 GlaSkeleton::bone(int frame, int index) const {
    const auto &c = pool[frameBones[size_t(frame) * bones.size() + size_t(index)]];
    auto value = [&](size_t i) { return float(uint16_t(c[i * 2] | c[i * 2 + 1] << 8)); };
    // A rotation as a quaternion (w, x, y, z, each 0..65535 for -2..2),
    // then the translation (1/64 of a unit, from -512).
    const float w = value(0) / 16383.0f - 2;
    const float x = value(1) / 16383.0f - 2;
    const float y = value(2) / 16383.0f - 2;
    const float z = value(3) / 16383.0f - 2;
    Bone34 b{};
    b[0] = 1 - 2 * (y * y + z * z);
    b[1] = 2 * (x * y - w * z);
    b[2] = 2 * (x * z + w * y);
    b[4] = 2 * (x * y + w * z);
    b[5] = 1 - 2 * (x * x + z * z);
    b[6] = 2 * (y * z - w * x);
    b[8] = 2 * (x * z - w * y);
    b[9] = 2 * (y * z + w * x);
    b[10] = 1 - 2 * (x * x + y * y);
    b[3] = value(4) / 64 - 512;
    b[7] = value(5) / 64 - 512;
    b[11] = value(6) / 64 - 512;
    return b;
}

void poseBones(const GlaSkeleton *skeleton, int frame, int oldFrame, float backLerp, std::vector<Bone34> &out) {
    static constexpr Bone34 kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    if (!skeleton) {
        out.assign(1, kIdentity);
        return;
    }
    const int frames = skeleton->frameCount;
    frame = ((frame % frames) + frames) % frames;
    oldFrame = ((oldFrame % frames) + frames) % frames;
    out.resize(skeleton->bones.size());
    std::vector<bool> done(out.size());
    // Each bone after its parent, whatever their order (parseGla has
    // checked every chain reaches a root).
    auto pose = [&](auto &self, size_t i) -> void {
        if (done[i])
            return;
        Bone34 b = skeleton->bone(frame, int(i));
        if (backLerp > 0 && oldFrame != frame) {
            const Bone34 old = skeleton->bone(oldFrame, int(i));
            for (size_t k = 0; k < 12; ++k)
                b[k] = b[k] * (1 - backLerp) + old[k] * backLerp;
        }
        const int parent = skeleton->bones[i].parent;
        if (parent >= 0)
            self(self, size_t(parent));
        out[i] = parent < 0 ? b : multiply(out[size_t(parent)], b);
        done[i] = true;
    };
    for (size_t i = 0; i < out.size(); ++i)
        pose(pose, i);
}

void skinSurface(const GlmSurface &surface, const std::vector<Bone34> &bones, std::vector<Md3Vertex> &out) {
    out.resize(surface.vertices.size());
    for (size_t i = 0; i < surface.vertices.size(); ++i) {
        const GlmVertex &v = surface.vertices[i];
        Md3Vertex o;
        for (int w = 0; w < v.weightCount; ++w) {
            const size_t ref = size_t(surface.boneRefs[v.bone[size_t(w)]]);
            const Bone34 &b = ref < bones.size() ? bones[ref] : bones.front();
            const float weight = v.weight[size_t(w)];
            for (size_t r = 0; r < 3; ++r) {
                o.pos[r] +=
                    weight * (b[r * 4] * v.pos[0] + b[r * 4 + 1] * v.pos[1] + b[r * 4 + 2] * v.pos[2] + b[r * 4 + 3]);
                o.normal[r] +=
                    weight * (b[r * 4] * v.normal[0] + b[r * 4 + 1] * v.normal[1] + b[r * 4 + 2] * v.normal[2]);
            }
        }
        out[i] = o;
    }
}

} // namespace reluva::idtech3
