// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Worked out from Midtown Madness 2's pedestrian files (text, but for the
// animations); see docs/formats/age/ped.md.

#include "engines/age/ped_model.h"

#include "engines/age/pkg.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>

namespace reluva::age {

namespace {

std::vector<std::string> wordsOf(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string w;
    while (in >> w)
        out.push_back(w);
    return out;
}

float num(const std::vector<std::string>& w, size_t i) { return i < w.size() ? std::strtof(w[i].c_str(), nullptr) : 0; }

std::string textOf(const std::vector<uint8_t>& data) { return std::string(data.begin(), data.end()); }

} // namespace

Error parseSkeleton(const std::string& text, Skeleton& out) {
    out = Skeleton{};
    std::vector<int> open; // the bones whose braces are open
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto w = wordsOf(line);
        if (w.empty())
            continue;
        if (w[0] == "bone" && w.size() >= 2) {
            Skeleton::Bone b;
            b.name = w[1];
            b.parent = open.empty() ? -1 : open.back();
            open.push_back(int(out.bones.size()));
            out.bones.push_back(b);
        } else if (w[0] == "offset" && !open.empty()) {
            out.bones[size_t(open.back())].offset = {num(w, 1), num(w, 2), num(w, 3)};
        } else if (w[0] == "}") {
            if (open.empty())
                return Error::fail("unbalanced braces");
            open.pop_back();
        }
    }
    if (out.bones.empty())
        return Error::fail("no bones");
    return Error::success();
}

Error readPedAnimation(const std::vector<uint8_t>& data, PedAnimation& out) {
    // A word 0, the frames and the channels, five bytes not known (a
    // float and a byte), then the frames' values.
    out = PedAnimation{};
    constexpr size_t kHeader = 17;
    if (data.size() < kHeader)
        return Error::fail("too short for an animation");
    uint32_t h[3];
    std::memcpy(h, data.data(), sizeof h);
    if (h[0] != 0 || h[1] == 0 || h[2] < 3 || h[1] > 10000 || h[2] > 1000 ||
        data.size() < kHeader + uint64_t(h[1]) * h[2] * 4)
        return Error::fail("not an animation this reads");
    out.frames = int(h[1]);
    out.channels = int(h[2]);
    out.values.resize(size_t(h[1]) * h[2]);
    std::memcpy(out.values.data(), data.data() + kHeader, out.values.size() * 4);
    return Error::success();
}

Error parsePedMoves(const std::string& csv, std::vector<PedMove>& out) {
    // name, animation, first frame, last frame, four distances, next.
    out.clear();
    std::istringstream in(csv);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string> f;
        size_t at = 0;
        while (true) {
            const size_t c = line.find(',', at);
            f.push_back(line.substr(at, c == std::string::npos ? std::string::npos : c - at));
            if (c == std::string::npos)
                break;
            at = c + 1;
        }
        if (f.size() >= 9 && !f[0].empty() && !f[1].empty())
            out.push_back({f[0], f[1], f[8]});
    }
    if (out.empty())
        return Error::fail("no moves");
    return Error::success();
}

Error parsePedMesh(const std::string& text, PedMesh& out) {
    // Two layouts: corners in packets, a packet a material (the men), or
    // in one list with each material taking the next of them and of the
    // triangles (the women). Either way the vertices come in bone order,
    // as many for each bone as "mtxv" says, in the bone's own space.
    out = PedMesh{};
    std::vector<Vec3> positions, normals;
    std::vector<std::array<float, 2>> uvs;
    std::vector<int> perBone;
    struct Adjunct {
        int v = 0, n = 0, t = 0;
    };
    struct Range {
        size_t adjuncts = 0, triangles = 0, packets = 1;
    };
    std::vector<Range> ranges;      // each material's share of the one list
    std::vector<Adjunct> adj;       // the one list, or the packet's
    std::vector<uint32_t> tris;
    std::vector<std::pair<std::vector<Adjunct>, std::vector<uint32_t>>> packets;
    std::istringstream in(text);
    std::string line;
    enum class In { None, Material, Packet } block = In::None;
    while (std::getline(in, line)) {
        const auto w = wordsOf(line);
        if (w.empty())
            continue;
        const std::string& k = w[0];
        if (block == In::Material) {
            if (k == "}")
                block = In::None;
            else if (k == "adjuncts:")
                ranges.back().adjuncts = size_t(num(w, 1));
            else if (k == "primitives:")
                ranges.back().triangles = size_t(num(w, 1));
            else if (k == "packets:")
                ranges.back().packets = size_t(num(w, 1));
            else if (k == "diffuse:")
                out.materials.back().diffuse = {num(w, 1), num(w, 2), num(w, 3), 1};
            continue;
        }
        if (k == "}" && block == In::Packet) {
            packets.push_back({adj, tris});
            adj.clear();
            tris.clear();
            block = In::None;
        } else if (k == "adj" && w.size() >= 5) {
            adj.push_back({std::atoi(w[1].c_str()), std::atoi(w[2].c_str()), std::atoi(w[4].c_str())});
        } else if (k == "tri" && w.size() >= 4) {
            for (size_t i = 1; i <= 3; ++i)
                tris.push_back(uint32_t(std::atoi(w[i].c_str())));
        } else if (k == "v") {
            positions.push_back({num(w, 1), num(w, 2), num(w, 3)});
        } else if (k == "n") {
            normals.push_back({num(w, 1), num(w, 2), num(w, 3)});
        } else if (k == "t1") {
            uvs.push_back({num(w, 1), num(w, 2)});
        } else if (k == "mtxv") {
            for (size_t i = 1; i < w.size(); ++i)
                perBone.push_back(std::atoi(w[i].c_str()));
        } else if (k == "mtl" && w.size() >= 2) {
            out.materials.push_back({});
            out.materials.back().name = w[1];
            ranges.push_back({});
            block = In::Material;
        } else if (k == "packet") {
            block = In::Packet;
        }
    }
    if (out.materials.empty())
        return Error::fail("no materials");
    std::vector<int> boneOf;
    for (size_t b = 0; b < perBone.size(); ++b)
        boneOf.insert(boneOf.end(), size_t(std::max(0, perBone[b])), int(b));
    if (boneOf.size() != positions.size())
        return Error::fail("mtxv doesn't account for the vertices");
    if (packets.empty()) { // the one list, shared out in order
        size_t a = 0, t = 0;
        for (const Range& r : ranges) {
            if (a + r.adjuncts > adj.size() || (t + r.triangles) * 3 > tris.size())
                return Error::fail("the materials want more corners or triangles than there are");
            std::vector<uint32_t> local;
            for (size_t i = t * 3; i < (t + r.triangles) * 3; ++i) {
                if (tris[i] < a || tris[i] >= a + r.adjuncts)
                    return Error::fail("a triangle uses another material's corners");
                local.push_back(tris[i] - uint32_t(a));
            }
            packets.push_back({std::vector<Adjunct>(adj.begin() + long(a), adj.begin() + long(a + r.adjuncts)), local});
            a += r.adjuncts;
            t += r.triangles;
        }
    }
    // Each material's packets, in order.
    std::vector<size_t> owner;
    for (size_t m = 0; m < ranges.size(); ++m)
        owner.insert(owner.end(), ranges[m].packets, m);
    if (owner.size() != packets.size())
        return Error::fail("the materials don't account for the packets");
    for (size_t p = 0; p < packets.size(); ++p) {
        PedMesh::Material& mat = out.materials[owner[p]];
        const uint32_t first = uint32_t(mat.corners.size());
        for (const Adjunct& a : packets[p].first) {
            if (a.v < 0 || size_t(a.v) >= positions.size())
                return Error::fail("a corner out of range");
            PedMesh::Corner c;
            c.position = positions[size_t(a.v)];
            c.bone = boneOf[size_t(a.v)];
            if (a.n >= 0 && size_t(a.n) < normals.size())
                c.normal = normals[size_t(a.n)];
            if (a.t >= 0 && size_t(a.t) < uvs.size()) {
                c.uv[0] = uvs[size_t(a.t)][0];
                c.uv[1] = uvs[size_t(a.t)][1];
            }
            mat.corners.push_back(c);
        }
        for (uint32_t i : packets[p].second) {
            if (first + i >= mat.corners.size())
                return Error::fail("a triangle out of range");
            mat.indices.push_back(first + i);
        }
    }
    return Error::success();
}

std::vector<BonePose> poseOf(const Skeleton& skeleton, const PedAnimation& anim, int frame) {
    std::vector<BonePose> pose(skeleton.bones.size());
    const float* v = anim.frame(std::clamp(frame, 0, anim.frames - 1));
    for (size_t b = 0; b < skeleton.bones.size(); ++b) {
        const size_t c = 3 + 3 * b;
        Mat3 turn;
        if (int(c + 2) < anim.channels)
            turn = Mat3::rotation({0, 0, 1}, v[c + 2]) * Mat3::rotation({0, 1, 0}, v[c + 1]) *
                   Mat3::rotation({1, 0, 0}, v[c]);
        const Skeleton::Bone& bone = skeleton.bones[b];
        if (bone.parent < 0) {
            pose[b].at = {0, v[1], 0};
            pose[b].turn = turn;
        } else {
            const BonePose& p = pose[size_t(bone.parent)];
            pose[b].at = p.at + p.turn * bone.offset;
            pose[b].turn = p.turn * turn;
        }
    }
    return pose;
}

Error PedModel::load(gfx::Renderer3D& renderer, const SearchSet& files, const std::string& model,
                     const std::vector<std::string>& wanted) {
    unload();
    std::vector<uint8_t> data;
    Skeleton skeleton;
    PedMesh mesh;
    std::vector<PedMove> moves;
    if (Error err = files.read("anim/" + model + ".skel", data))
        return err;
    if (Error err = parseSkeleton(textOf(data), skeleton))
        return Error::fail(model + ".skel: " + err.message);
    if (Error err = files.read("anim/" + model + ".mod", data))
        return err;
    if (Error err = parsePedMesh(textOf(data), mesh))
        return Error::fail(model + ".mod: " + err.message);
    if (Error err = files.read("anim/" + model + ".csv", data))
        return err;
    if (Error err = parsePedMoves(textOf(data), moves))
        return Error::fail(model + ".csv: " + err.message);
    for (const auto& m : mesh.materials)
        for (const auto& c : m.corners)
            if (c.bone < 0 || size_t(c.bone) >= skeleton.bones.size())
                return Error::fail(model + ".mod: a corner on a bone the skeleton hasn't");

    // The moves wanted and those they lead to.
    std::set<std::string> need(wanted.begin(), wanted.end());
    for (bool more = true; more;) {
        more = false;
        for (const PedMove& m : moves)
            if (need.count(m.name) && !m.next.empty() && need.insert(m.next).second)
                more = true;
    }

    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    for (const auto& m : mesh.materials) {
        corners_ += m.corners.size();
        ranges_.push_back({uint32_t(indices.size()), uint32_t(m.indices.size())});
        const uint32_t base = uint32_t(corners_ - m.corners.size());
        for (uint32_t i : m.indices)
            indices.push_back(base + i);
    }
    for (const PedMove& m : moves) {
        if (!need.count(m.name))
            continue;
        PedAnimation anim;
        if (files.read("anim/" + m.animation + ".anim", data) || readPedAnimation(data, anim) ||
            anim.channels < int(3 + 3 * skeleton.bones.size()))
            continue;
        Move out;
        out.firstFrame = int(vertices.size() / std::max<size_t>(1, corners_));
        out.frames = anim.frames;
        // How far the root goes across the ground, as a speed.
        if (anim.frames > 1) {
            const float* a = anim.frame(0);
            const float* b = anim.frame(anim.frames - 1);
            out.travel = Vec3{b[0] - a[0], 0, b[2] - a[2]} * (kFrameRate / float(anim.frames - 1));
        }
        for (int f = 0; f < anim.frames; ++f) {
            const auto pose = poseOf(skeleton, anim, f);
            for (const auto& mat : mesh.materials)
                for (const auto& c : mat.corners) {
                    const BonePose& p = pose[size_t(c.bone)];
                    const Vec3 at = p.at + p.turn * c.position;
                    const Vec3 n = p.turn * c.normal;
                    gfx::Vertex v{};
                    for (size_t k = 0; k < 3; ++k) {
                        v.pos[k] = at[k];
                        v.normal[k] = n[k];
                    }
                    v.uv[0] = c.uv[0];
                    v.uv[1] = c.uv[1];
                    v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
                    vertices.push_back(v);
                }
        }
        moveIndex_[m.name] = int(moves_.size());
        moves_.push_back(out);
    }
    for (const PedMove& m : moves) {
        const auto it = moveIndex_.find(m.name), next = moveIndex_.find(m.next);
        if (it != moveIndex_.end())
            moves_[size_t(it->second)].next = next != moveIndex_.end() ? next->second : it->second;
    }
    if (moves_.empty())
        return Error::fail(model + ": none of its moves could be read");

    // Colours: the package's paint jobs. Its groups are the materials in
    // another order (and with their corners merged), so each material is
    // matched to the group with as many triangles, the nearest colour
    // deciding between those.
    Pkg pkg;
    const PkgMesh* body = nullptr;
    if (!files.read("geometry/" + model + ".pkg", data) && !readPkg(data, pkg) && !pkg.meshes.empty())
        body = &pkg.meshes.front();
    std::vector<int> shaderOf(mesh.materials.size(), -1);
    if (body) {
        std::vector<bool> used(body->groups.size(), false);
        for (size_t i = 0; i < mesh.materials.size(); ++i) {
            const auto& mat = mesh.materials[i];
            float best = 1e9f;
            for (size_t g = 0; g < body->groups.size(); ++g) {
                const PkgGroup& group = body->groups[g];
                if (used[g] || group.indices.size() != mat.indices.size())
                    continue;
                const PkgShader* s = pkg.shader(0, group.shader);
                float d = 0;
                for (size_t k = 0; s && k < 3; ++k)
                    d += std::fabs(s->diffuse[k] - mat.diffuse[k]);
                if (d < best) {
                    best = d;
                    shaderOf[i] = int(g);
                }
            }
            if (shaderOf[i] >= 0) {
                used[size_t(shaderOf[i])] = true;
                shaderOf[i] = int(body->groups[size_t(shaderOf[i])].shader);
            }
        }
    }
    for (uint32_t job = 0; job < std::max(1u, body ? pkg.paintJobs : 1u); ++job) {
        std::vector<std::array<float, 4>> colors;
        for (size_t i = 0; i < mesh.materials.size(); ++i) {
            const PkgShader* s = shaderOf[i] >= 0 ? pkg.shader(job, uint32_t(shaderOf[i])) : nullptr;
            colors.push_back(s ? s->diffuse : mesh.materials[i].diffuse);
        }
        paints_.push_back(colors);
    }
    renderer_ = &renderer;
    return renderer.createMesh(vertices, indices, mesh_);
}

void PedModel::unload() {
    if (renderer_ && mesh_)
        renderer_->destroyMesh(mesh_);
    renderer_ = nullptr;
    mesh_ = 0;
    corners_ = 0;
    ranges_.clear();
    paints_.clear();
    moves_.clear();
    moveIndex_.clear();
}

int PedModel::move(const std::string& name) const {
    const auto it = moveIndex_.find(name);
    return it == moveIndex_.end() ? -1 : it->second;
}

void PedModel::draw(const gfx::Mat4& model, int move, float time, uint32_t paintJob, const SceneLight& light) const {
    if (!mesh_ || move < 0 || size_t(move) >= moves_.size())
        return;
    const Move& m = moves_[size_t(move)];
    const float f = std::max(0.0f, time) * kFrameRate;
    const int a = int(f) % m.frames, b = (a + 1) % m.frames;
    const auto& colors = paints_[paintJob % paints_.size()];
    for (size_t i = 0; i < ranges_.size(); ++i) {
        gfx::DrawCall call;
        call.mesh = mesh_;
        call.firstIndex = ranges_[i].first;
        call.indexCount = ranges_[i].second;
        call.vertexOffset = int32_t(size_t(m.firstFrame + a) * corners_);
        call.oldVertexOffset = int32_t(size_t(m.firstFrame + b) * corners_);
        call.backLerp = f - std::floor(f);
        call.model = model;
        call.color = colors[std::min(i, colors.size() - 1)];
        call.rgbSource = gfx::ColorSource::Lighting;
        call.ambientLight = light.ambient;
        call.directedLight = light.directed;
        call.lightDirection = light.direction;
        call.fillLight = light.fill;
        call.fillDirection = light.fillDirection;
        applyFog(call, light);
        renderer_->draw(call);
    }
}

} // namespace reluva::age
