// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What each PSDL attribute draws, worked out by drawing them and comparing
// with the game (docs/formats/age/psdl.md). Texture coordinates are
// Reluva's own choice where the file doesn't give them.

#include "engines/age/city_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace reluva::age {

namespace {

constexpr float kGroundTile = 10;  // metres a ground texture covers, for fans and roofs
constexpr float kRoadRepeat = 1;   // a texture's length along a strip, per strip width

float length(const Vec3& a, const Vec3& b) {
    const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

class Builder {
public:
    Builder(const Psdl& psdl, CityMesh& out) : psdl_(psdl), out_(out) {}

    void block(const PsdlBlock& b) {
        uint32_t texture = 0;
        for (const PsdlAttribute& a : b.attributes) {
            if (a.type == psdlattr::Texture) {
                texture = uint32_t(a.subtype) * 256 + a.words[0];
                continue;
            }
            attribute(a, texture);
        }
    }

private:
    Vec3 vertex(uint16_t i) const { return i < psdl_.vertices.size() ? psdl_.vertices[i] : Vec3{}; }
    float height(uint16_t i) const { return i < psdl_.heights.size() ? psdl_.heights[i] : 0; }

    std::vector<uint32_t>& batch(uint32_t texture) {
        const std::string name = psdl_.texture(texture);
        auto it = batchOf_.find(name);
        if (it == batchOf_.end()) {
            it = batchOf_.emplace(name, out_.batches.size()).first;
            out_.batches.push_back({name, {}});
        }
        return out_.batches[it->second].indices;
    }

    // A triangle with its face's normal; `ground` turns it to face up.
    void triangle(uint32_t texture, const Vec3 (&p)[3], const float (&uv)[3][2], bool ground) {
        const float ax = p[1][0] - p[0][0], ay = p[1][1] - p[0][1], az = p[1][2] - p[0][2];
        const float bx = p[2][0] - p[0][0], by = p[2][1] - p[0][1], bz = p[2][2] - p[0][2];
        float n[3] = {ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx};
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len < 1e-6f)
            return; // degenerate
        for (float& c : n)
            c /= len;
        if (ground && n[1] < 0)
            for (float& c : n)
                c = -c;
        std::vector<uint32_t>& idx = batch(texture);
        for (int k = 0; k < 3; ++k) {
            gfx::Vertex v{};
            for (int c = 0; c < 3; ++c) {
                v.pos[c] = p[k][size_t(c)];
                v.normal[c] = n[c];
            }
            v.uv[0] = uv[k][0];
            v.uv[1] = uv[k][1];
            v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
            idx.push_back(uint32_t(out_.vertices.size()));
            out_.vertices.push_back(v);
        }
    }

    // A quad a, b, c, d (a-b one edge, d-c the opposite, as a strip).
    void quad(uint32_t texture, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const float (&uv)[4][2],
              bool ground) {
        triangle(texture, {a, b, c}, {{uv[0][0], uv[0][1]}, {uv[1][0], uv[1][1]}, {uv[2][0], uv[2][1]}}, ground);
        triangle(texture, {a, c, d}, {{uv[0][0], uv[0][1]}, {uv[2][0], uv[2][1]}, {uv[3][0], uv[3][1]}}, ground);
    }

    // A fan over vertices, textured by position.
    void fan(uint32_t texture, const std::vector<Vec3>& p) {
        auto uv = [](const Vec3& v) { return std::array<float, 2>{v[0] / kGroundTile, v[2] / kGroundTile}; };
        for (size_t i = 1; i + 1 < p.size(); ++i) {
            const auto a = uv(p[0]), b = uv(p[i]), c = uv(p[i + 1]);
            triangle(texture, {p[0], p[i], p[i + 1]}, {{a[0], a[1]}, {b[0], b[1]}, {c[0], c[1]}}, true);
        }
    }

    // A strip of pairs (left[i], right[i]): the texture's v from `vLeft`
    // at the left to `vRight` at the right, its u along the strip, a
    // texture's height of it per width (so it isn't stretched).
    void strip(uint32_t texture, const std::vector<Vec3>& left, const std::vector<Vec3>& right, float vLeft = 0,
               float vRight = 1) {
        float u = 0;
        for (size_t i = 0; i + 1 < left.size(); ++i) {
            const float width = std::max(0.5f, (length(left[i], right[i]) + length(left[i + 1], right[i + 1])) / 2);
            const float along = (length(left[i], left[i + 1]) + length(right[i], right[i + 1])) / 2;
            const float u2 = u + along / width * kRoadRepeat;
            quad(texture, left[i], right[i], right[i + 1], left[i + 1],
                 {{u, vLeft}, {u, vRight}, {u2, vRight}, {u2, vLeft}}, true);
            u = u2;
        }
    }

    // A road between two curbs: its textures are half a road, the centre
    // line at v = 0 and the curb at v = 1, so each half is drawn from its
    // curb to the middle.
    void road(uint32_t texture, const std::vector<Vec3>& left, const std::vector<Vec3>& right) {
        std::vector<Vec3> middle(left.size());
        for (size_t i = 0; i < left.size(); ++i)
            for (size_t k = 0; k < 3; ++k)
                middle[i][k] = (left[i][k] + right[i][k]) / 2;
        strip(texture, left, middle, 1, 0);
        strip(texture, middle, right, 0, 1);
    }

    // A sidewalk along a road: its inner edge (at the road) lifted to the
    // outer edge's height, and the curb's face down to the road.
    void sidewalk(uint32_t texture, const std::vector<Vec3>& inner, const std::vector<Vec3>& outer) {
        std::vector<Vec3> raised = inner;
        for (size_t i = 0; i < raised.size(); ++i)
            raised[i][1] = std::max(inner[i][1], outer[i][1]);
        strip(texture, raised, outer);
        for (size_t i = 0; i + 1 < inner.size(); ++i)
            if (raised[i][1] > inner[i][1] || raised[i + 1][1] > inner[i + 1][1])
                quad(texture, inner[i], inner[i + 1], raised[i + 1], raised[i], {{0, 0}, {1, 0}, {1, 0.05f}, {0, 0.05f}},
                     false);
    }

    // A wall from left to right, between two heights at each end.
    void wall(uint32_t texture, const Vec3& left, const Vec3& right, float leftBottom, float rightBottom, float top,
              float u, float v) {
        const Vec3 a{left[0], leftBottom, left[2]}, b{right[0], rightBottom, right[2]};
        const Vec3 c{right[0], top, right[2]}, d{left[0], top, left[2]};
        quad(texture, a, b, c, d, {{0, v}, {u, v}, {u, 0}, {0, 0}}, false);
    }

    void attribute(const PsdlAttribute& a, uint32_t texture) {
        const auto& w = a.words;
        switch (a.type) {
        case psdlattr::RoadStrip: {
            // Sections of: outer left (sidewalk), inner left, inner right, outer right.
            std::vector<Vec3> ol, il, ir, orr;
            for (size_t i = 0; i + 3 < w.size(); i += 4) {
                ol.push_back(vertex(w[i]));
                il.push_back(vertex(w[i + 1]));
                ir.push_back(vertex(w[i + 2]));
                orr.push_back(vertex(w[i + 3]));
            }
            road(texture, il, ir);
            sidewalk(texture + 1, il, ol);
            sidewalk(texture + 1, ir, orr);
            break;
        }
        case psdlattr::DividedRoad: {
            // Two words not worked out, then sections of: outer left, inner
            // left, divider left, divider right, inner right, outer right.
            std::vector<Vec3> s[6];
            for (size_t i = 2; i + 5 < w.size(); i += 6)
                for (size_t k = 0; k < 6; ++k)
                    s[k].push_back(vertex(w[i + k]));
            // Each carriageway from its curb (v = 1) to the divider (the
            // centre line, v = 0).
            strip(texture, s[1], s[2], 1, 0);
            strip(texture, s[3], s[4], 0, 1);
            strip(texture + 1, s[2], s[3]);
            sidewalk(texture + 1, s[1], s[0]);
            sidewalk(texture + 1, s[4], s[5]);
            break;
        }
        case psdlattr::SidewalkStrip: {
            // A pair of equal values isn't a section: 188 two-pair strips
            // of the two cities start (0, 0) or (1, 1), what for isn't
            // known. Read as vertices, they'd stretch across the city.
            std::vector<Vec3> inner, outer;
            for (size_t i = 0; i + 1 < w.size(); i += 2) {
                if (w[i] == w[i + 1])
                    continue;
                inner.push_back(vertex(w[i]));
                outer.push_back(vertex(w[i + 1]));
            }
            sidewalk(texture + 1, inner, outer);
            break;
        }
        case psdlattr::RectStrip: {
            std::vector<Vec3> left, right;
            for (size_t i = 0; i + 1 < w.size(); i += 2) {
                left.push_back(vertex(w[i]));
                right.push_back(vertex(w[i + 1]));
            }
            strip(texture, left, right);
            break;
        }
        case psdlattr::Crosswalk:
            // Two pairs across the road: (0, 1) at one side, (2, 3) the other.
            quad(texture + 2, vertex(w[0]), vertex(w[1]), vertex(w[3]), vertex(w[2]), {{0, 0}, {1, 0}, {1, 1}, {0, 1}},
                 true);
            break;
        case psdlattr::RoadFan:
        case psdlattr::Fan: {
            std::vector<Vec3> p;
            for (uint16_t i : w)
                p.push_back(vertex(i));
            fan(texture, p);
            break;
        }
        case psdlattr::Roof: {
            std::vector<Vec3> p;
            for (size_t i = 1; i < w.size(); ++i) {
                Vec3 v = vertex(w[i]);
                v[1] = height(w[0]);
                p.push_back(v);
            }
            fan(texture, p);
            break;
        }
        case psdlattr::Facade: {
            const Vec3 l = vertex(w[4]), r = vertex(w[5]);
            wall(texture, l, r, height(w[0]), height(w[0]), height(w[1]), float(w[2]), float(w[3]));
            break;
        }
        case psdlattr::Sliver: {
            // From the ground up to a height, its texture repeated along it
            // by the second height (a scale).
            const Vec3 l = vertex(w[2]), r = vertex(w[3]);
            const float top = height(w[0]);
            const float u = length(l, r) * height(w[1]) / std::max(0.5f, top - (l[1] + r[1]) / 2);
            wall(texture, l, r, l[1], r[1], top, u, 1);
            break;
        }
        case psdlattr::Tunnel: ++out_.skipped; break;
        default: break; // facade bounds aren't drawn
        }
    }

    const Psdl& psdl_;
    CityMesh& out_;
    std::map<std::string, size_t> batchOf_;
};

} // namespace

void buildCityMesh(const Psdl& psdl, CityMesh& out) {
    out = CityMesh{};
    Builder b(psdl, out);
    for (const PsdlBlock& block : psdl.blocks)
        b.block(block);
}

} // namespace reluva::age
