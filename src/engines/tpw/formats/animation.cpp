// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's 1,209 animations', worked out from them:
// docs/engines/tpw/games/simcoaster/formats/md2-animation.md.

#include "engines/tpw/formats/animation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::tpw {

namespace {

constexpr uint32_t kMagic = 0x1cd15d46;
constexpr uint32_t kAnimationFlag = 0x20;
constexpr size_t kHeaderSize = 0xb8;
// The root, the file's last bytes; the tracks' records before it.
constexpr size_t kRootSize = 0x48;
constexpr size_t kRootTracks = 0x2c;
constexpr size_t kRootFrames = 0x08;
constexpr size_t kTrackSize = 0x40;
// A track's flags, and where its record points for each kind.
constexpr uint32_t kHasPosition = 0x1;
constexpr uint32_t kHasRotation = 0x18;
constexpr uint32_t kHasScale = 0x180;
constexpr uint32_t kHasOther = 0x1000 | 0x10000 | 0x20000;
constexpr size_t kPositionAt = 0x18;
constexpr size_t kRotationAt = 0x1c;
constexpr size_t kScaleAt = 0x20;
constexpr uint32_t kBezierTrack = 0x12;
constexpr uint32_t kLinearTrack = 0x18;
constexpr uint16_t kLastKey = 0xffff;
constexpr size_t kRotationKeySize = 20;
constexpr size_t kScaleKeySize = 16;
// A track no longer than this is taken for a broken one.
constexpr size_t kMostKeys = 10000;

class Reader {
public:
    explicit Reader(const std::vector<uint8_t> &data) : _d(data) {}
    bool has(size_t at, size_t size) const { return at <= _d.size() && size <= _d.size() - at; }
    uint16_t u16(size_t at) const { return uint16_t(_d[at] | _d[at + 1] << 8); }
    uint32_t u32(size_t at) const { return uint32_t(u16(at)) | uint32_t(u16(at + 2)) << 16; }
    float f32(size_t at) const {
        const uint32_t bits = u32(at);
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }
    gfx::Vec3 vec3(size_t at) const { return {f32(at), f32(at + 4), f32(at + 8)}; }

private:
    const std::vector<uint8_t> &_d;
};

Error readPosition(const Reader &r, size_t at, Animation::Track &t) {
    // The kind, how many values and keys, where the values and the keys'
    // frames are.
    if (!r.has(at, 16))
        return Error::fail("a position track runs past the end");
    const uint32_t kind = r.u32(at);
    const size_t values = r.u16(at + 4);
    const size_t keys = r.u16(at + 6);
    const size_t valuesAt = r.u32(at + 8);
    const size_t framesAt = r.u32(at + 12);
    if (kind == kBezierTrack && values == 3 * keys - 2 && keys > 0)
        t.curve = Animation::Curve::kBezier;
    else if (kind == kLinearTrack && values == keys && keys > 0)
        t.curve = Animation::Curve::kLinear;
    else
        return Error::fail("a position track of a kind not known");
    if (!r.has(valuesAt, 12 * values) || !r.has(framesAt, 4 * keys))
        return Error::fail("a position track runs past the end");
    for (size_t i = 0; i < values; ++i)
        t.positions.push_back(r.vec3(valuesAt + 12 * i));
    for (size_t i = 0; i < keys; ++i)
        t.positionFrames.push_back(r.u32(framesAt + 4 * i));
    return Error::success();
}

// Keys of a frame, a mark (0xFFFF on the last) and a quaternion, x, y,
// z, w.
Error readRotation(const Reader &r, size_t at, Animation::Track &t) {
    for (size_t k = 0;; ++k) {
        const size_t key = at + kRotationKeySize * k;
        if (k >= kMostKeys || !r.has(key, kRotationKeySize))
            return Error::fail("a rotation track runs past the end");
        // Stored x, y, z, w: so read, 1,129 of the tracks start at their
        // node's rest pose, against 100 with w first.
        t.rotations.push_back({r.u16(key), {r.f32(key + 16), r.f32(key + 4), r.f32(key + 8), r.f32(key + 12)}});
        if (r.u16(key + 2) == kLastKey)
            return Error::success();
    }
}

std::array<float, 4> slerp(const std::array<float, 4> &a, std::array<float, 4> b, float u) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0) {
        dot = -dot;
        for (float &c : b)
            c = -c;
    }
    float wa = 1 - u;
    float wb = u;
    if (dot < 0.9995f) {
        const float angle = std::acos(dot);
        wa = std::sin((1 - u) * angle) / std::sin(angle);
        wb = std::sin(u * angle) / std::sin(angle);
    }
    std::array<float, 4> q;
    float length = 0;
    for (size_t i = 0; i < 4; ++i) {
        q[i] = wa * a[i] + wb * b[i];
        length += q[i] * q[i];
    }
    length = std::sqrt(length);
    for (float &c : q)
        c /= length > 0 ? length : 1;
    return q;
}

// Where `frame` falls among rising key frames: the key before it and how
// far on to the next, 0 to 1; held at the ends.
template<typename FrameOf>
std::pair<size_t, float> place(size_t keys, FrameOf frameOf, float frame) {
    if (keys < 2 || frame <= float(frameOf(0)))
        return {0, 0};
    for (size_t i = 0; i + 1 < keys; ++i) {
        const float a = float(frameOf(i));
        const float b = float(frameOf(i + 1));
        if (frame < b)
            return {i, b > a ? (frame - a) / (b - a) : 0};
    }
    return {keys - 1, 0};
}

} // namespace

Error Animation::parse(const std::vector<uint8_t> &data) {
    *this = {};
    const Reader r(data);
    if (!r.has(0, kHeaderSize + kRootSize) || r.u32(0) != kMagic || !(r.u32(0x30) & kAnimationFlag) || r.u32(0x50) != 0)
        return Error::fail("not a model's animation (.MD2, flag 0x20)");
    const size_t root = data.size() - kRootSize;
    _frames = r.u32(root + kRootFrames);
    const size_t first = r.u32(root + kRootTracks);
    if (first < kHeaderSize || first > root || (root - first) % kTrackSize)
        return Error::fail("the animation's tracks aren't where its root says");
    for (size_t at = first; at < root; at += kTrackSize) {
        Track t;
        t.flags = r.u32(at + 4);
        t.frames = r.u32(at + 0x0c);
        t.target = r.u16(at + 0x14);
        t.hasOther = t.flags & kHasOther;
        if (t.flags & kHasPosition)
            if (Error err = readPosition(r, r.u32(at + kPositionAt), t))
                return err;
        if (t.flags & kHasRotation)
            if (Error err = readRotation(r, r.u32(at + kRotationAt), t))
                return err;
        if (t.flags & kHasScale) {
            // As many as the high half of the word at 0x10.
            const size_t keys = r.u16(at + 0x12);
            const size_t keysAt = r.u32(at + kScaleAt);
            if (!r.has(keysAt, kScaleKeySize * keys))
                return Error::fail("a scale track runs past the end");
            for (size_t k = 0; k < keys; ++k)
                t.scales.push_back({r.u32(keysAt + kScaleKeySize * k), r.vec3(keysAt + kScaleKeySize * k + 4)});
        }
        _tracks.push_back(std::move(t));
    }
    return Error::success();
}

gfx::Mat4 Animation::pose(const Track &t, float frame, const gfx::Mat4 &rest) {
    // The rest pose's columns: its axes, scaled, and its translation.
    gfx::Vec3 axes[3];
    gfx::Vec3 scale;
    for (size_t c = 0; c < 3; ++c) {
        axes[c] = {rest[c * 4], rest[c * 4 + 1], rest[c * 4 + 2]};
        scale[c] = std::sqrt(axes[c][0] * axes[c][0] + axes[c][1] * axes[c][1] + axes[c][2] * axes[c][2]);
    }
    gfx::Vec3 translation = {rest[12], rest[13], rest[14]};

    if (t.curve != Curve::kNone && !t.positionFrames.empty()) {
        const auto [i, u] = place(t.positionFrames.size(), [&](size_t k) { return t.positionFrames[k]; }, frame);
        if (t.curve == Curve::kLinear) {
            const gfx::Vec3 &a = t.positions[i];
            const gfx::Vec3 &b = t.positions[std::min(i + 1, t.positions.size() - 1)];
            for (size_t c = 0; c < 3; ++c)
                translation[c] = a[c] + (b[c] - a[c]) * u;
        } else {
            // A cubic Bezier from key i through its two control points.
            const size_t at = 3 * i;
            const gfx::Vec3 &p0 = t.positions[at];
            const gfx::Vec3 &p1 = t.positions[std::min(at + 1, t.positions.size() - 1)];
            const gfx::Vec3 &p2 = t.positions[std::min(at + 2, t.positions.size() - 1)];
            const gfx::Vec3 &p3 = t.positions[std::min(at + 3, t.positions.size() - 1)];
            const float v = 1 - u;
            for (size_t c = 0; c < 3; ++c)
                translation[c] = v * v * v * p0[c] + 3 * v * v * u * p1[c] + 3 * v * u * u * p2[c] + u * u * u * p3[c];
        }
    }
    if (!t.scales.empty()) {
        const auto [i, u] = place(t.scales.size(), [&](size_t k) { return t.scales[k].frame; }, frame);
        const gfx::Vec3 &a = t.scales[i].scale;
        const gfx::Vec3 &b = t.scales[std::min(i + 1, t.scales.size() - 1)].scale;
        for (size_t c = 0; c < 3; ++c)
            scale[c] = a[c] + (b[c] - a[c]) * u;
    }
    gfx::Mat4 m = rest;
    if (!t.rotations.empty()) {
        const auto [i, u] = place(t.rotations.size(), [&](size_t k) { return t.rotations[k].frame; }, frame);
        const auto q = slerp(t.rotations[i].q, t.rotations[std::min(i + 1, t.rotations.size() - 1)].q, u);
        const float w = q[0];
        const float x = q[1];
        const float y = q[2];
        const float z = q[3];
        // The rotation's columns, as the rest matrices' are.
        const float rot[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + w * z),     2 * (x * z - w * y),
                              2 * (x * y - w * z),     1 - 2 * (x * x + z * z), 2 * (y * z + w * x),
                              2 * (x * z + w * y),     2 * (y * z - w * x),     1 - 2 * (x * x + y * y)};
        for (size_t c = 0; c < 3; ++c)
            for (size_t r = 0; r < 3; ++r)
                m[c * 4 + r] = rot[c * 3 + r] * scale[c];
    } else {
        for (size_t c = 0; c < 3; ++c)
            for (size_t r = 0; r < 3; ++r)
                m[c * 4 + r] =
                    scale[c] > 0
                        ? axes[c][r] /
                              std::sqrt(axes[c][0] * axes[c][0] + axes[c][1] * axes[c][1] + axes[c][2] * axes[c][2]) *
                              scale[c]
                        : 0;
    }
    m[12] = translation[0];
    m[13] = translation[1];
    m[14] = translation[2];
    return m;
}

} // namespace reluva::tpw
