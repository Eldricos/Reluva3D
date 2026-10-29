// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What each PSDL attribute draws, worked out by drawing them and comparing
// with the game (docs/formats/age/psdl.md). Texture coordinates are
// Reluva's own choice where the file doesn't give them.

#include "engines/age/city_mesh.h"

#include "engines/age/pathset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>

namespace reluva::age {

namespace {

constexpr float kGroundTile = 10;  // metres a ground texture covers, for fans and roofs
constexpr float kRoadRepeat = 1;   // a texture's length along a strip, per strip width
constexpr int kArchSteps = 8;      // pieces across a curved tunnel ceiling

// A tunnel attribute's flags (psdl.md): which sides have walls, the
// ceiling, and which ends of a curved ceiling are closed.
namespace tunnelflag {
constexpr uint16_t Left = 0x1, Right = 0x2, FlatCeiling = 0x8, ClosedStartLeft = 0x10, ClosedEndLeft = 0x20,
                   ClosedStartRight = 0x40, ClosedEndRight = 0x80, CurvedCeiling = 0x100;
}

float length(const Vec3& a, const Vec3& b) {
    const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

class Builder {
public:
    Builder(const Psdl& psdl, CityMesh& out) : psdl_(psdl), out_(out) {
        for (size_t i = 0; i < out_.batches.size(); ++i)
            batchOf_.emplace(keyOf(out_.batches[i].texture, out_.batches[i].water, out_.batches[i].detail), i);
    }

    void block(const PsdlBlock& b, uint16_t index) {
        block_ = index;
        uint32_t texture = 0;
        tunnel_.reset();
        for (const PsdlAttribute& a : b.attributes) {
            if (a.type == psdlattr::Texture) {
                texture = uint32_t(a.subtype) * 256 + a.words[0];
                continue;
            }
            if (a.type == psdlattr::Tunnel && a.words.size() >= 3) {
                const Tunnel t{a.words[0], float(a.words[1]) / 256, float(a.words[2]) / 256, texture};
                if (a.words.size() >= 6)
                    junction(b, t, uint32_t(a.words[4]) | uint32_t(a.words[5]) << 16);
                else
                    tunnel_ = t; // for the strip that follows
                continue;
            }
            attribute(a, texture);
        }
    }

    // A flat fan over a block's outline at `height` (water).
    void plane(const PsdlBlock& b, uint16_t index, float height, uint32_t texture) {
        block_ = index;
        water_ = true;
        std::vector<Vec3> p;
        for (const auto& [v, beyond] : b.perimeter) {
            Vec3 q = vertex(v);
            q[1] = height;
            p.push_back(q);
        }
        if (p.size() >= 3)
            fan(texture, p);
    }

private:
    Vec3 vertex(uint16_t i) const { return i < psdl_.vertices.size() ? psdl_.vertices[i] : Vec3{}; }
    float height(uint16_t i) const { return i < psdl_.heights.size() ? psdl_.heights[i] : 0; }

    static std::string keyOf(const std::string& texture, bool water, uint8_t detail) {
        return std::string(water ? "~" : "") + char('0' + detail) + texture;
    }
    size_t batchIndex(uint32_t texture) {
        const std::string name = psdl_.texture(texture);
        const std::string key = keyOf(name, water_, detail_);
        auto it = batchOf_.find(key);
        if (it == batchOf_.end()) {
            it = batchOf_.emplace(key, out_.batches.size()).first;
            out_.batches.push_back({name, {}, {}, water_, false, detail_});
        }
        return it->second;
    }
    std::vector<uint32_t>& batch(uint32_t texture) { return out_.batches[batchIndex(texture)].indices; }

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
        out_.batches[batchIndex(texture)].blocks.push_back(block_);
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
    // Where the third texture of the road's is a low-detail version of it
    // (r2_f, swalk_f, r2_lo_f), the road is made twice: with its own for
    // near, with that one for far off.
    void road(uint32_t texture, const std::vector<Vec3>& left, const std::vector<Vec3>& right) {
        std::vector<Vec3> middle(left.size());
        for (size_t i = 0; i < left.size(); ++i)
            for (size_t k = 0; k < 3; ++k)
                middle[i][k] = (left[i][k] + right[i][k]) / 2;
        const bool far = psdl_.texture(texture + 2).find("_lo") != std::string::npos;
        detail_ = far ? 1 : 0;
        strip(texture, left, middle, 1, 0);
        strip(texture, middle, right, 0, 1);
        if (far) {
            detail_ = 2;
            strip(texture + 2, left, middle, 1, 0);
            strip(texture + 2, middle, right, 0, 1);
        }
        detail_ = 0;
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

    // A divider between a divided road's carriageways: flat, or raised
    // `height` with sides and ends (a kerbed island, a barrier).
    void divider(uint32_t texture, const std::vector<Vec3>& left, const std::vector<Vec3>& right, float height) {
        if (height <= 0) {
            strip(texture, left, right);
            return;
        }
        std::vector<Vec3> topLeft = left, topRight = right;
        for (Vec3& v : topLeft)
            v[1] += height;
        for (Vec3& v : topRight)
            v[1] += height;
        strip(texture, topLeft, topRight);
        const float v = std::min(1.0f, height / 4);
        for (size_t i = 0; i + 1 < left.size() && i + 1 < right.size(); ++i) {
            const float u = std::hypot(left[i + 1][0] - left[i][0], left[i + 1][2] - left[i][2]) / 4;
            quad(texture, left[i], left[i + 1], topLeft[i + 1], topLeft[i], {{0, v}, {u, v}, {u, 0}, {0, 0}}, false);
            quad(texture, right[i + 1], right[i], topRight[i], topRight[i + 1], {{0, v}, {u, v}, {u, 0}, {0, 0}},
                 false);
        }
        if (left.empty() || right.empty())
            return;
        quad(texture, right.front(), left.front(), topLeft.front(), topRight.front(), {{0, v}, {1, v}, {1, 0}, {0, 0}},
             false);
        quad(texture, left.back(), right.back(), topRight.back(), topLeft.back(), {{0, v}, {1, v}, {1, 0}, {0, 0}},
             false);
    }

    // A wall from left to right, between two heights at each end.
    void wall(uint32_t texture, const Vec3& left, const Vec3& right, float leftBottom, float rightBottom, float top,
              float u, float v) {
        const Vec3 a{left[0], leftBottom, left[2]}, b{right[0], rightBottom, right[2]};
        const Vec3 c{right[0], top, right[2]}, d{left[0], top, left[2]};
        quad(texture, a, b, c, d, {{0, v}, {u, v}, {u, 0}, {0, 0}}, false);
    }

    // A tunnel's (or a railing's) two heights and flags. Walls stand
    // along a strip's two sides from the ground up; a flat ceiling is at
    // the first height; a curved one springs from the lower of the two and
    // reaches the higher at its crown (London's Underground: walls 5.5 m, a
    // crown at 7; San Francisco's road tunnels: 7 and 9 or 10), its ends
    // closed above the first height where the flags say (where it meets a
    // junction's flat ceiling).
    struct Tunnel {
        uint16_t flags = 0;
        float first = 0, second = 0;
        uint32_t texture = 0;
        bool curved() const { return flags & tunnelflag::CurvedCeiling; }
        float wall() const { return curved() ? std::min(first, second) : first; }
        float crown() const { return curved() ? std::max(first, second) : first; }
    };

    // A wall `height` high along `edge`, standing on the ground there, its
    // texture once up it and repeated along it by length over height.
    void sideWall(uint32_t texture, const std::vector<Vec3>& edge, float height) {
        float u = 0;
        for (size_t i = 0; i + 1 < edge.size(); ++i) {
            const float u2 = u + length(edge[i], edge[i + 1]) / std::max(0.5f, height);
            const Vec3 top0{edge[i][0], edge[i][1] + height, edge[i][2]};
            const Vec3 top1{edge[i + 1][0], edge[i + 1][1] + height, edge[i + 1][2]};
            quad(texture, edge[i], edge[i + 1], top1, top0, {{u, 1}, {u2, 1}, {u2, 0}, {u, 0}}, false);
            u = u2;
        }
    }

    void tunnel(const Tunnel& t, const std::vector<Vec3>& left, const std::vector<Vec3>& right) {
        if (left.size() < 2 || left.size() != right.size())
            return;
        if (t.flags & tunnelflag::Left)
            sideWall(t.texture, left, t.wall());
        if (t.flags & tunnelflag::Right)
            sideWall(t.texture, right, t.wall());
        const bool curved = t.curved();
        if (!curved && !(t.flags & tunnelflag::FlatCeiling))
            return;
        // The ceiling's cross-sections, left to right, at each section.
        const int steps = curved ? kArchSteps : 1;
        auto across = [&](size_t i, int k) {
            const float f = float(k) / float(steps);
            Vec3 p;
            for (size_t c = 0; c < 3; ++c)
                p[c] = left[i][c] + (right[i][c] - left[i][c]) * f;
            p[1] += t.wall() + (t.crown() - t.wall()) * std::sin(f * 3.14159265f);
            return p;
        };
        for (size_t i = 0; i + 1 < left.size(); ++i)
            for (int k = 0; k < steps; ++k) {
                const float u0 = float(k) / float(steps), u1 = float(k + 1) / float(steps);
                quad(t.texture, across(i, k), across(i, k + 1), across(i + 1, k + 1), across(i + 1, k),
                     {{u0, 0}, {u1, 0}, {u1, 1}, {u0, 1}}, false);
            }
        if (!curved || t.first > t.wall())
            return;
        // Closed ends: the arch's end above the walls' height, each half.
        auto close = [&](size_t i, bool leftHalf) {
            const Vec3 l = across(i, 0), r = across(i, steps);
            const Vec3 middle{(l[0] + r[0]) / 2, (l[1] + r[1]) / 2, (l[2] + r[2]) / 2};
            const int from = leftHalf ? 0 : steps / 2, to = leftHalf ? steps / 2 : steps;
            for (int k = from; k < to; ++k)
                triangle(t.texture, {middle, across(i, k), across(i, k + 1)}, {{0.5f, 1}, {0, 0}, {1, 0}}, false);
        };
        const size_t last = left.size() - 1;
        if (t.flags & tunnelflag::ClosedStartLeft)
            close(0, true);
        if (t.flags & tunnelflag::ClosedStartRight)
            close(0, false);
        if (t.flags & tunnelflag::ClosedEndLeft)
            close(last, true);
        if (t.flags & tunnelflag::ClosedEndRight)
            close(last, false);
    }

    // A junction's tunnel: walls along the block's outline where `edges`
    // has a bit (bit i: the edge ending at outline vertex i; those with no
    // block beyond them), and its flat ceiling over the whole block.
    void junction(const PsdlBlock& b, const Tunnel& t, uint32_t edges) {
        const size_t n = b.perimeter.size();
        if (n < 3)
            return;
        for (size_t i = 0; i < n && i < 32; ++i)
            if (edges >> i & 1)
                sideWall(t.texture, {vertex(b.perimeter[(i + n - 1) % n].first), vertex(b.perimeter[i].first)},
                         t.first);
        if (t.flags & (tunnelflag::FlatCeiling | tunnelflag::CurvedCeiling)) {
            std::vector<Vec3> p;
            for (const auto& [v, beyond] : b.perimeter) {
                Vec3 q = vertex(v);
                q[1] += t.first;
                p.push_back(q);
            }
            fan(t.texture, p);
        }
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
            if (tunnel_)
                tunnel(*tunnel_, ol, orr);
            tunnel_.reset();
            break;
        }
        case psdlattr::DividedRoad: {
            // The divider: its texture (high byte, numbered as the texture
            // attribute's) and kind (low two bits: 1 flat, 2 a kerbed
            // island, 3 a barrier), and its height in 256ths of a metre;
            // then sections of: outer left, inner left, divider left,
            // divider right, inner right, outer right (psdl.md).
            std::vector<Vec3> s[6];
            for (size_t i = 2; i + 5 < w.size(); i += 6)
                for (size_t k = 0; k < 6; ++k)
                    s[k].push_back(vertex(w[i + k]));
            // Each carriageway from its curb (v = 1) to the divider (the
            // centre line, v = 0).
            strip(texture, s[1], s[2], 1, 0);
            strip(texture, s[3], s[4], 0, 1);
            const uint32_t dividerTexture = w.size() > 1 && (w[0] >> 8) ? uint32_t(w[0] >> 8) : texture + 1;
            const float height = w.size() > 1 && (w[0] & 3) >= 2 ? float(w[1]) / 256 : 0;
            divider(dividerTexture, s[2], s[3], height);
            sidewalk(texture + 1, s[1], s[0]);
            sidewalk(texture + 1, s[4], s[5]);
            if (tunnel_)
                tunnel(*tunnel_, s[0], s[5]);
            tunnel_.reset();
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
            if (tunnel_)
                tunnel(*tunnel_, left, right);
            tunnel_.reset();
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
        default: break; // facade bounds aren't drawn
        }
    }

    const Psdl& psdl_;
    CityMesh& out_;
    std::map<std::string, size_t> batchOf_;
    std::optional<Tunnel> tunnel_; // waiting for the strip it walls
    uint16_t block_ = 0;
    bool water_ = false; // making the water's planes
    uint8_t detail_ = 0; // making a road's near or far version
};

} // namespace

void buildCityMesh(const Psdl& psdl, CityMesh& out) {
    out = CityMesh{};
    Builder b(psdl, out);
    for (size_t i = 0; i < psdl.blocks.size(); ++i)
        b.block(psdl.blocks[i], uint16_t(i));
}

Error parseCityWater(const std::string& text, CityWater& out) {
    out = CityWater{};
    size_t at = 0;
    bool first = true;
    while (at < text.size()) {
        const size_t start = text.find_first_not_of(" \t\r\n", at);
        if (start == std::string::npos)
            break;
        const size_t end = text.find_first_of(" \t\r\n", start);
        const std::string word = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        at = end == std::string::npos ? text.size() : end;
        char* rest = nullptr;
        if (first) {
            out.height = std::strtof(word.c_str(), &rest);
            first = false;
        } else {
            const unsigned long block = std::strtoul(word.c_str(), &rest, 10);
            if (block > 0xffff)
                return Error::fail("water: block " + word + " out of range");
            out.blocks.push_back(uint16_t(block));
        }
        if (!rest || *rest != '\0')
            return Error::fail("water: \"" + word + "\" isn't a number");
    }
    if (first)
        return Error::fail("water: empty");
    return Error::success();
}

void addCityDecals(const std::vector<PathsetPath>& paths, CityMesh& out) {
    constexpr float kLift = 0.03f; // over the road, so as not to flicker into it
    // The highest of the city's surfaces facing up under a point, and its
    // block. (The paths' point words aren't reliably blocks.)
    struct Triangle {
        uint32_t v[3];
        uint16_t block;
    };
    std::vector<Triangle> triangles;
    for (const CityMesh::Batch& b : out.batches) {
        if (b.water || b.decal || b.detail == 2)
            continue;
        for (size_t t = 0; t < b.blocks.size() && 3 * t + 2 < b.indices.size(); ++t)
            triangles.push_back({{b.indices[3 * t], b.indices[3 * t + 1], b.indices[3 * t + 2]}, b.blocks[t]});
    }
    auto ground = [&](const Vec3& at, uint16_t* block) {
        float best = -1e30f;
        for (const Triangle& tri : triangles) {
            const auto& A = out.vertices[tri.v[0]].pos;
            const auto& B = out.vertices[tri.v[1]].pos;
            const auto& C = out.vertices[tri.v[2]].pos;
            if (at[0] < std::min({A[0], B[0], C[0]}) - 0.1f || at[0] > std::max({A[0], B[0], C[0]}) + 0.1f ||
                at[2] < std::min({A[2], B[2], C[2]}) - 0.1f || at[2] > std::max({A[2], B[2], C[2]}) + 0.1f)
                continue;
            // Barycentric in X and Z.
            const float d = (B[2] - C[2]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[2] - C[2]);
            if (std::fabs(d) < 1e-6f)
                continue;
            const float u = ((B[2] - C[2]) * (at[0] - C[0]) + (C[0] - B[0]) * (at[2] - C[2])) / d;
            const float v = ((C[2] - A[2]) * (at[0] - C[0]) + (A[0] - C[0]) * (at[2] - C[2])) / d;
            if (u < -0.01f || v < -0.01f || u + v > 1.01f)
                continue;
            const float y = u * A[1] + v * B[1] + (1 - u - v) * C[1];
            if (y < at[1] + 3 && y > best) { // not a roof over it
                best = y;
                if (block)
                    *block = tri.block;
            }
        }
        return best > -1e29f ? best : at[1];
    };
    for (const PathsetPath& p : paths) {
        if (p.model.empty() || p.model.rfind("PATH", 0) == 0 || p.points.size() < 4)
            continue;
        CityMesh::Batch batch;
        batch.texture = p.model;
        batch.decal = true;
        // The texture once across, and along as often as it's as long as
        // it's wide.
        float along = 0;
        const uint32_t first = uint32_t(out.vertices.size());
        for (size_t k = 0; k + 1 < p.points.size(); k += 2) {
            const Vec3 a = p.points[k], b = p.points[k + 1];
            if (k >= 2) {
                const Vec3 mid = (a + b) * 0.5f, prev = (p.points[k - 2] + p.points[k - 1]) * 0.5f;
                const float width = std::max(0.1f, length(b - a));
                along += length(mid - prev) / width;
            }
            for (int side = 0; side < 2; ++side) {
                gfx::Vertex v{};
                const Vec3& at = side ? b : a;
                v.pos[0] = at[0];
                v.pos[1] = ground(at, nullptr) + kLift;
                v.pos[2] = at[2];
                v.normal[1] = 1;
                v.uv[0] = float(side);
                v.uv[1] = along;
                v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
                out.vertices.push_back(v);
            }
        }
        const uint32_t pairs = (uint32_t(out.vertices.size()) - first) / 2;
        for (uint32_t k = 0; k + 1 < pairs; ++k) {
            const uint32_t i = first + 2 * k;
            // Its block, for what's seen from where: the one under its
            // middle (0, always drawn, if none).
            Vec3 middle{};
            for (uint32_t c = 0; c < 4; ++c)
                for (size_t q = 0; q < 3; ++q)
                    middle[q] += out.vertices[i + c].pos[q] / 4;
            uint16_t block = 0;
            ground(middle, &block);
            // Both ways round: the pairs aren't all in the same order.
            for (uint32_t t : {i, i + 2, i + 1, i + 1, i + 2, i + 3, i, i + 1, i + 2, i + 1, i + 3, i + 2})
                batch.indices.push_back(t);
            for (int t = 0; t < 4; ++t)
                batch.blocks.push_back(block);
        }
        if (!batch.indices.empty())
            out.batches.push_back(std::move(batch));
    }
}

void addCityWater(const Psdl& psdl, const CityWater& water, uint32_t texture, CityMesh& out) {
    Builder b(psdl, out);
    for (uint16_t i : water.blocks)
        if (i < psdl.blocks.size())
            b.plane(psdl.blocks[i], i, water.height, texture);
}

} // namespace reluva::age
