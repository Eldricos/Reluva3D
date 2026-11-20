// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/ghoul2_collision.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3::jk2 {

namespace {

using V3 = std::array<float, 3>;

V3 sub(const V3 &a, const V3 &b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
float dot(const V3 &a, const V3 &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
V3 cross(const V3 &a, const V3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float length(const V3 &a) {
    return std::sqrt(dot(a, a));
}
V3 normalized(const V3 &a) {
    const float l = length(a);
    return l > 0 ? V3{a[0] / l, a[1] / l, a[2] / l} : a;
}
V3 transformPoint(const Bone34 &m, const V3 &v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3], m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7],
            m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11]};
}
V3 rotate(const Bone34 &m, const V3 &v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[4] * v[0] + m[5] * v[1] + m[6] * v[2],
            m[8] * v[0] + m[9] * v[1] + m[10] * v[2]};
}
// The inverse of a turn and a move.
Bone34 inverseRigid(const Bone34 &m) {
    Bone34 o{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            o[size_t(r * 4 + c)] = m[size_t(c * 4 + r)];
    for (int r = 0; r < 3; ++r)
        o[size_t(r * 4 + 3)] = -(o[size_t(r * 4)] * m[3] + o[size_t(r * 4 + 1)] * m[7] + o[size_t(r * 4 + 2)] * m[11]);
    return o;
}

// What one trace keeps while it walks the models: the ray in the models'
// own space, and whether it's done (the first hit for kReturnOnHit, or
// the records full).
struct Walk {
    const Bone34 *world;
    V3 start;
    V3 end;
    int32_t entity;
    Ghoul2Trace type;
    float radius;
    std::vector<Ghoul2Hit> *hits;
    bool done = false;
};

// One more record, if there's room; done once full or the first is all
// that's wanted.
Ghoul2Hit *addHit(Walk &w) {
    if (w.hits->size() >= kMaxGhoul2Hits) {
        w.done = true;
        return nullptr;
    }
    w.hits->emplace_back();
    if (w.type == Ghoul2Trace::kReturnOnHit || w.hits->size() >= kMaxGhoul2Hits)
        w.done = true;
    return &w.hits->back();
}

// The segment against each triangle, both faces (the original's
// G2_TracePolys, as the GPL source describes it).
void tracePolys(Walk &w, const std::vector<Md3Vertex> &verts, const std::vector<uint32_t> &indices, int32_t model,
                int32_t surface) {
    const V3 dir = sub(w.end, w.start);
    for (size_t j = 0; j + 2 < indices.size() && !w.done; j += 3) {
        const V3 &a = verts[indices[j]].pos;
        const V3 &b = verts[indices[j + 1]].pos;
        const V3 &c = verts[indices[j + 2]].pos;
        const V3 n = cross(sub(b, a), sub(c, a));
        const float denom = dot(dir, n);
        if (std::fabs(denom) < 1e-10f)
            continue;
        const float t = dot(sub(a, w.start), n) / denom;
        if (t < 0 || t > 1)
            continue;
        const V3 p{w.start[0] + t * dir[0], w.start[1] + t * dir[1], w.start[2] + t * dir[2]};
        if (dot(cross(sub(b, a), sub(p, a)), n) < 0 || dot(cross(sub(c, b), sub(p, b)), n) < 0 ||
            dot(cross(sub(a, c), sub(p, c)), n) < 0)
            continue;
        Ghoul2Hit *h = addHit(w);
        if (!h)
            return;
        h->entity = w.entity;
        h->model = model;
        h->surface = surface;
        h->polygon = int32_t(j / 3);
        h->flags = denom > 0 ? kGhoul2FrontFace : 0;
        h->distance = length(sub(p, w.start));
        h->position = transformPoint(*w.world, p);
        h->normal = normalized(rotate(*w.world, n));
        const float whole = length(n);
        if (whole > 0) {
            h->baryI = length(cross(sub(b, p), sub(c, p))) / whole;
            h->baryJ = length(cross(sub(p, a), sub(c, a))) / whole;
        }
    }
}

// With a width: a triangle counts unless its three corners lie beyond the
// same side of the box around the ray, across it or before its start or
// past its end (G2_RadiusTracePolys). Its first corner stands for where it
// was met.
void traceRadius(Walk &w, const std::vector<Md3Vertex> &verts, const std::vector<uint32_t> &indices, int32_t model,
                 int32_t surface) {
    const V3 dir = sub(w.end, w.start);
    const float lengthSq = dot(dir, dir);
    if (lengthSq <= 0)
        return;
    V3 across = cross(dir, V3{0, 0, 1});
    if (length(across) < 1e-3f * std::sqrt(lengthSq))
        across = cross(dir, V3{1, 0, 0});
    across = normalized(across);
    const V3 up = normalized(cross(normalized(dir), across));
    const float toWindow = 0.5f / w.radius;
    std::vector<uint8_t> outside(verts.size());
    uint8_t all = 0x3f;
    for (size_t i = 0; i < verts.size(); ++i) {
        const V3 d = sub(verts[i].pos, w.start);
        const float s = dot(d, across) * toWindow + 0.5f;
        const float t = dot(d, up) * toWindow + 0.5f;
        const float u = dot(d, dir) / lengthSq;
        uint8_t bits = 0;
        bits |= s < 0 ? 1 : s > 1 ? 2 : 0;
        bits |= t < 0 ? 4 : t > 1 ? 8 : 0;
        bits |= u < 0 ? 16 : u > 1 ? 32 : 0;
        outside[i] = bits;
        all &= bits;
    }
    if (all)
        return;
    for (size_t j = 0; j + 2 < indices.size() && !w.done; j += 3) {
        if (outside[indices[j]] & outside[indices[j + 1]] & outside[indices[j + 2]])
            continue;
        const V3 &a = verts[indices[j]].pos;
        const V3 &b = verts[indices[j + 1]].pos;
        const V3 &c = verts[indices[j + 2]].pos;
        Ghoul2Hit *h = addHit(w);
        if (!h)
            return;
        h->entity = w.entity;
        h->model = model;
        h->surface = surface;
        h->polygon = int32_t(j / 3);
        h->flags = kGhoul2FrontFace;
        h->normal = normalized(rotate(*w.world, cross(sub(b, a), sub(c, a))));
        // The first hit is all kReturnOnHit wants: the original stops
        // before giving it a distance or a place.
        if (w.type == Ghoul2Trace::kReturnOnHit)
            return;
        h->distance = length(sub(a, w.start));
        h->position = transformPoint(*w.world, a);
    }
}

} // namespace

void traceGhoul2(const std::vector<Ghoul2Pose> &models, const Bone34 &world, const std::array<float, 3> &scale,
                 const std::array<float, 3> &start, const std::array<float, 3> &end, int32_t entity, Ghoul2Trace type,
                 int lod, float radius, std::vector<Ghoul2Hit> &hits) {
    if (type == Ghoul2Trace::kNone)
        return;
    // The ray brought into the models' own space, as the original's is.
    const Bone34 inverse = inverseRigid(world);
    Walk w{&world, transformPoint(inverse, start), transformPoint(inverse, end), entity, type, radius, &hits};
    std::vector<Md3Vertex> verts;
    for (size_t m = 0; m < models.size() && !w.done; ++m) {
        const Ghoul2Pose &pose = models[m];
        if (!pose.glm || pose.glm->lods.empty() || pose.noCollide)
            continue;
        const auto &level = pose.glm->lods[size_t(std::clamp(lod, 0, int(pose.glm->lods.size()) - 1))];
        // The surfaces from the roots down, children after their parent
        // whether it's on or not, as the original walks them.
        std::vector<int> order;
        for (size_t s = 0; s < pose.glm->surfaces.size(); ++s)
            if (pose.glm->surfaces[s].parent < 0)
                order.push_back(int(s));
        std::reverse(order.begin(), order.end());
        while (!order.empty() && !w.done) {
            const int s = order.back();
            order.pop_back();
            const GlmSurfaceInfo &info = pose.glm->surfaces[size_t(s)];
            for (auto it = info.children.rbegin(); it != info.children.rend(); ++it)
                if (*it >= 0 && size_t(*it) < pose.glm->surfaces.size())
                    order.push_back(*it);
            if (size_t(s) >= level.size() || size_t(s) >= pose.surfaceOn.size() || !pose.surfaceOn[size_t(s)])
                continue;
            const GlmSurface &surface = level[size_t(s)];
            skinSurface(surface, pose.bones, verts);
            for (Md3Vertex &v : verts) {
                v.pos = transformPoint(pose.placement, v.pos);
                for (int i = 0; i < 3; ++i)
                    if (scale[size_t(i)] != 0)
                        v.pos[size_t(i)] *= scale[size_t(i)];
            }
            if (std::fabs(radius) < 0.1f)
                tracePolys(w, verts, surface.indices, int32_t(m), s);
            else
                traceRadius(w, verts, surface.indices, int32_t(m), s);
        }
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Ghoul2Hit &a, const Ghoul2Hit &b) { return a.distance < b.distance; });
}

} // namespace reluva::idtech3::jk2
