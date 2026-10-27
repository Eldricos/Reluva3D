// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/patch.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

// A vertex's attributes as floats, so they can be blended.
struct Point {
    float v[15]; // pos 3, st 2, lightmap 2, normal 3, color 4 (0-255), unused 1
};

Point toPoint(const BspVertex& b) {
    Point p{};
    for (int i = 0; i < 3; ++i) {
        p.v[i] = b.pos[size_t(i)];
        p.v[7 + i] = b.normal[size_t(i)];
    }
    for (int i = 0; i < 2; ++i) {
        p.v[3 + i] = b.st[size_t(i)];
        p.v[5 + i] = b.lightmap[size_t(i)];
    }
    for (int i = 0; i < 4; ++i)
        p.v[10 + i] = b.color[size_t(i)];
    return p;
}

BspVertex toVertex(const Point& p) {
    BspVertex b;
    for (int i = 0; i < 3; ++i) {
        b.pos[size_t(i)] = p.v[i];
        b.normal[size_t(i)] = p.v[7 + i];
    }
    for (int i = 0; i < 2; ++i) {
        b.st[size_t(i)] = p.v[3 + i];
        b.lightmap[size_t(i)] = p.v[5 + i];
    }
    for (int i = 0; i < 4; ++i)
        b.color[size_t(i)] = static_cast<uint8_t>(std::clamp(std::lround(p.v[10 + i]), 0L, 255L));
    // Blending normals shortens them.
    const float len = std::sqrt(b.normal[0] * b.normal[0] + b.normal[1] * b.normal[1] + b.normal[2] * b.normal[2]);
    if (len > 0)
        for (float& n : b.normal)
            n /= len;
    return b;
}

// Quadratic Bezier: (1-t)^2 a + 2t(1-t) b + t^2 c.
Point bezier(const Point& a, const Point& b, const Point& c, float t) {
    const float wa = (1 - t) * (1 - t), wb = 2 * t * (1 - t), wc = t * t;
    Point r;
    for (int i = 0; i < 15; ++i)
        r.v[i] = wa * a.v[i] + wb * b.v[i] + wc * c.v[i];
    return r;
}

// The triangles of a grid of points (cols x rows of them, row by row,
// from `base` in `vertices`), wound clockwise seen from where the normals
// point: the geometric normal (right-hand rule) should face away from
// them. Decided once for the grid, from its first triangle that isn't
// degenerate.
void appendTriangles(uint32_t base, int cols, int rows, const std::vector<BspVertex>& vertices,
                     std::vector<uint32_t>& indices) {
    auto at = [&](int r, int c) { return base + uint32_t(r * cols + c); };
    bool flip = false;
    for (int r = 0; r + 1 < rows; ++r) {
        bool decided = false;
        for (int c = 0; c + 1 < cols && !decided; ++c) {
            const BspVertex& a = vertices[at(r, c)];
            const BspVertex& b = vertices[at(r + 1, c)];
            const BspVertex& d = vertices[at(r, c + 1)];
            const float e1[3] = {b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2]};
            const float e2[3] = {d.pos[0] - a.pos[0], d.pos[1] - a.pos[1], d.pos[2] - a.pos[2]};
            const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                e1[0] * e2[1] - e1[1] * e2[0]};
            const float s = n[0] * a.normal[0] + n[1] * a.normal[1] + n[2] * a.normal[2];
            if (std::fabs(s) > 1e-6f) {
                flip = s > 0;
                decided = true;
            }
        }
        if (decided)
            break;
    }
    for (int r = 0; r + 1 < rows; ++r)
        for (int c = 0; c + 1 < cols; ++c) {
            const uint32_t a = at(r, c), b = at(r + 1, c), d = at(r, c + 1), e = at(r + 1, c + 1);
            if (flip)
                indices.insert(indices.end(), {a, d, b, b, d, e});
            else
                indices.insert(indices.end(), {a, b, d, b, e, d});
        }
}

} // namespace

void tessellatePatch(const BspVertex* control, int width, int height, int level, std::vector<BspVertex>& vertices,
                     std::vector<uint32_t>& indices) {
    if (width < 3 || height < 3 || level < 1)
        return;
    const int blocksX = (width - 1) / 2, blocksY = (height - 1) / 2;
    const int cols = blocksX * level + 1, rows = blocksY * level + 1;
    const uint32_t base = static_cast<uint32_t>(vertices.size());

    for (int row = 0; row < rows; ++row) {
        // Which block this row is in, and where in it; the last row belongs
        // to the last block at t = 1.
        const int by = std::min(row / level, blocksY - 1);
        const float tv = float(row - by * level) / float(level);
        for (int col = 0; col < cols; ++col) {
            const int bx = std::min(col / level, blocksX - 1);
            const float tu = float(col - bx * level) / float(level);
            Point across[3];
            for (int j = 0; j < 3; ++j) {
                const BspVertex* r = control + (by * 2 + j) * width + bx * 2;
                across[j] = bezier(toPoint(r[0]), toPoint(r[1]), toPoint(r[2]), tu);
            }
            vertices.push_back(toVertex(bezier(across[0], across[1], across[2], tv)));
        }
    }

    appendTriangles(base, cols, rows, vertices, indices);
}

float patchBend(const BspVertex* control, int width, int height) {
    auto off = [](const std::array<float, 3>& a, const std::array<float, 3>& m, const std::array<float, 3>& b) {
        float d2 = 0;
        for (size_t k = 0; k < 3; ++k) {
            const float d = m[k] - (a[k] + b[k]) / 2;
            d2 += d * d;
        }
        return std::sqrt(d2);
    };
    float bend = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x + 2 < width; x += 2)
            bend = std::max(bend, off(control[y * width + x].pos, control[y * width + x + 1].pos,
                                      control[y * width + x + 2].pos));
    for (int x = 0; x < width; ++x)
        for (int y = 0; y + 2 < height; y += 2)
            bend = std::max(bend, off(control[y * width + x].pos, control[(y + 1) * width + x].pos,
                                      control[(y + 2) * width + x].pos));
    return bend;
}

int patchLevel(const BspVertex* control, int width, int height, float error) {
    const float bend = patchBend(control, width, height);
    if (bend < 0.5f)
        return 1;
    return std::clamp(int(std::ceil(std::sqrt(bend / (2 * std::max(error, 0.01f))))), 2, 16);
}

} // namespace reluva::idtech3

namespace reluva::idtech3 {

namespace {

constexpr int kMaxGridPoints = 65; // each way, as the original's

// A grid of blendable points, row by row.
struct Mesh {
    int w = 0, h = 0;
    std::vector<Point> p;
    Point& at(int x, int y) { return p[size_t(y * w + x)]; }
};

Point halfway(const Point& a, const Point& b) {
    Point r;
    for (int i = 0; i < 15; ++i)
        r.v[i] = 0.5f * (a.v[i] + b.v[i]);
    return r;
}

// How far `m` is from the line through `a` and `c` (from `a` itself if
// they're the same point).
float offLine(const Point& a, const Point& m, const Point& c) {
    float d[3], v[3], len2 = 0;
    for (int k = 0; k < 3; ++k) {
        d[k] = c.v[k] - a.v[k];
        v[k] = m.v[k] - a.v[k];
        len2 += d[k] * d[k];
    }
    if (len2 > 1e-12f) {
        const float along = (v[0] * d[0] + v[1] * d[1] + v[2] * d[2]) / len2;
        for (int k = 0; k < 3; ++k)
            v[k] -= d[k] * along;
    }
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

Mesh transposed(const Mesh& m) {
    Mesh t;
    t.w = m.h;
    t.h = m.w;
    t.p.resize(m.p.size());
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x)
            t.p[size_t(x * t.w + y)] = m.p[size_t(y * m.w + x)];
    return t;
}

// Halves the columns of control points across the mesh as needed (see
// subdividePatch); `straight` gets, for each column, whether it's a middle
// one straight on every row.
void subdivideColumns(Mesh& m, float error, std::vector<bool>& straight) {
    straight.assign(size_t(m.w), false);
    for (int x = 0; x + 2 < m.w;) {
        float worst = 0;
        for (int y = 0; y < m.h; ++y) {
            const Point &a = m.at(x, y), &b = m.at(x + 1, y), &c = m.at(x + 2, y);
            Point onCurve;
            for (int k = 0; k < 3; ++k)
                onCurve.v[k] = (a.v[k] + 2 * b.v[k] + c.v[k]) * 0.25f;
            worst = std::max(worst, offLine(a, onCurve, c));
        }
        if (worst < 0.1f) {
            straight[size_t(x + 1)] = true;
            x += 2;
            continue;
        }
        if (m.w + 2 > kMaxGridPoints || worst <= error) {
            x += 2;
            continue;
        }
        // Split this run in two at the curve's middle (a, b, c become a,
        // ab, middle, bc, c), then look at its first half again.
        Mesh out;
        out.w = m.w + 2;
        out.h = m.h;
        out.p.resize(size_t(out.w * out.h));
        for (int y = 0; y < m.h; ++y) {
            for (int i = 0; i <= x; ++i)
                out.at(i, y) = m.at(i, y);
            const Point ab = halfway(m.at(x, y), m.at(x + 1, y)), bc = halfway(m.at(x + 1, y), m.at(x + 2, y));
            out.at(x + 1, y) = ab;
            out.at(x + 2, y) = halfway(ab, bc);
            out.at(x + 3, y) = bc;
            for (int i = x + 2; i < m.w; ++i)
                out.at(i + 2, y) = m.at(i, y);
        }
        m = std::move(out);
        straight.insert(straight.begin() + x + 1, 2, false);
    }
}

bool near(const std::array<float, 3>& a, const std::array<float, 3>& b, float within) {
    return std::fabs(a[0] - b[0]) <= within && std::fabs(a[1] - b[1]) <= within && std::fabs(a[2] - b[2]) <= within;
}

// Normals from the grid's shape: at each point, the directions to its
// eight neighbours (the next point that isn't on top of it, up to three
// away; across the seam of a grid that closes on itself), each two
// neighbouring directions' cross product averaged. Turned to face the way
// the normal there already does; kept where the shape gives none.
void meshNormals(PatchGrid& g) {
    const int w = g.width, h = g.height;
    auto pos = [&](int x, int y) { return g.points[size_t(y * w + x)].pos; };
    bool wrapX = true, wrapY = true;
    for (int y = 0; y < h && wrapX; ++y)
        wrapX = near(pos(0, y), pos(w - 1, y), 1.0f);
    for (int x = 0; x < w && wrapY; ++x)
        wrapY = near(pos(x, 0), pos(x, h - 1), 1.0f);
    static const int kAround[8][2] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
    std::vector<std::array<float, 3>> normals(g.points.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::array<float, 3> here = pos(x, y);
            std::array<float, 3> dir[8]{};
            bool good[8]{};
            for (int k = 0; k < 8; ++k)
                for (int step = 1; step <= 3; ++step) {
                    int nx = x + kAround[k][0] * step, ny = y + kAround[k][1] * step;
                    if (wrapX)
                        nx = nx < 0 ? nx + w - 1 : (nx >= w ? nx - w + 1 : nx);
                    if (wrapY)
                        ny = ny < 0 ? ny + h - 1 : (ny >= h ? ny - h + 1 : ny);
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                        break;
                    const std::array<float, 3> there = pos(nx, ny);
                    std::array<float, 3> d{there[0] - here[0], there[1] - here[1], there[2] - here[2]};
                    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    if (len < 0.001f)
                        continue;
                    for (float& c : d)
                        c /= len;
                    dir[k] = d;
                    good[k] = true;
                    break;
                }
            std::array<float, 3> sum{};
            for (int k = 0; k < 8; ++k) {
                const int k2 = (k + 1) & 7;
                if (!good[k] || !good[k2])
                    continue;
                const std::array<float, 3>&a = dir[k2], &b = dir[k];
                std::array<float, 3> n{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
                const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len < 0.001f)
                    continue;
                for (int c = 0; c < 3; ++c)
                    sum[size_t(c)] += n[size_t(c)] / len;
            }
            const float len = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
            const std::array<float, 3>& old = g.points[size_t(y * w + x)].normal;
            if (len < 1e-6f) {
                normals[size_t(y * w + x)] = old;
                continue;
            }
            for (float& c : sum)
                c /= len;
            if (sum[0] * old[0] + sum[1] * old[1] + sum[2] * old[2] < 0)
                for (float& c : sum)
                    c = -c;
            normals[size_t(y * w + x)] = sum;
        }
    for (size_t i = 0; i < g.points.size(); ++i)
        g.points[i].normal = normals[i];
}

// One of a grid's four edges: a row (y fixed) or a column (x fixed).
struct Edge {
    bool row;
    int fixed;
};

std::vector<int> edgePoints(const PatchGrid& g, Edge e) {
    std::vector<int> out;
    if (e.row)
        for (int x = 0; x < g.width; ++x)
            out.push_back(e.fixed * g.width + x);
    else
        for (int y = 0; y < g.height; ++y)
            out.push_back(y * g.width + e.fixed);
    return out;
}

std::array<Edge, 4> edgesOf(const PatchGrid& g) {
    return {Edge{true, 0}, Edge{true, g.height - 1}, Edge{false, 0}, Edge{false, g.width - 1}};
}

// Whether two of an edge's middle points are the same (an edge pulled
// to a point, the tip of a cone): such an edge isn't matched.
bool collapsed(const PatchGrid& g, const std::vector<int>& edge) {
    for (size_t i = 1; i + 1 < edge.size(); ++i)
        for (size_t j = i + 1; j + 1 < edge.size(); ++j)
            if (near(g.points[size_t(edge[i])].pos, g.points[size_t(edge[j])].pos, 0.1f))
                return true;
    return false;
}

BspVertex blend(const BspVertex& a, const BspVertex& b) {
    return toVertex(halfway(toPoint(a), toPoint(b)));
}

// A new column at `x` (between the old x - 1 and x), halfway across every
// row, but at `at` on row `y`.
void insertColumn(PatchGrid& g, int x, int y, const std::array<float, 3>& at) {
    std::vector<BspVertex> out;
    out.reserve(size_t((g.width + 1) * g.height));
    for (int r = 0; r < g.height; ++r)
        for (int c = 0; c <= g.width; ++c) {
            if (c < x)
                out.push_back(g.points[size_t(r * g.width + c)]);
            else if (c > x)
                out.push_back(g.points[size_t(r * g.width + c - 1)]);
            else {
                out.push_back(blend(g.points[size_t(r * g.width + x - 1)], g.points[size_t(r * g.width + x)]));
                if (r == y)
                    out.back().pos = at;
            }
        }
    g.points = std::move(out);
    ++g.width;
}

void insertRow(PatchGrid& g, int y, int x, const std::array<float, 3>& at) {
    std::vector<BspVertex> row;
    for (int c = 0; c < g.width; ++c) {
        row.push_back(blend(g.points[size_t((y - 1) * g.width + c)], g.points[size_t(y * g.width + c)]));
        if (c == x)
            row.back().pos = at;
    }
    g.points.insert(g.points.begin() + y * g.width, row.begin(), row.end());
    ++g.height;
}

// Gives `to` one point `from`'s edges have and its own lack, if any.
bool stitchOne(const PatchGrid& from, PatchGrid& to) {
    for (const Edge eTo : edgesOf(to)) {
        if (eTo.row ? to.width >= kMaxGridPoints : to.height >= kMaxGridPoints)
            continue;
        const std::vector<int> mine = edgePoints(to, eTo);
        for (size_t t = 0; t + 1 < mine.size(); ++t) {
            const std::array<float, 3>&p = to.points[size_t(mine[t])].pos, &q = to.points[size_t(mine[t + 1])].pos;
            if (near(p, q, 0.01f))
                continue;
            for (const Edge eFrom : edgesOf(from)) {
                const std::vector<int> theirs = edgePoints(from, eFrom);
                if (collapsed(from, theirs))
                    continue;
                const int n = int(theirs.size());
                for (int i = 0; i < n; ++i) {
                    if (!near(from.points[size_t(theirs[size_t(i)])].pos, p, 0.1f))
                        continue;
                    for (const int step : {1, -1}) {
                        // q further along their edge, with points between.
                        for (int k = i + 2 * step; k >= 0 && k < n; k += step) {
                            if (!near(from.points[size_t(theirs[size_t(k)])].pos, q, 0.1f))
                                continue;
                            const std::array<float, 3> missing = from.points[size_t(theirs[size_t(i + step)])].pos;
                            if (eTo.row)
                                insertColumn(to, int(t) + 1, eTo.fixed, missing);
                            else
                                insertRow(to, int(t) + 1, eTo.fixed, missing);
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

} // namespace

PatchGrid subdividePatch(const BspVertex* control, int width, int height, float error) {
    PatchGrid g;
    if (width < 3 || height < 3)
        return g;
    Mesh m;
    m.w = width;
    m.h = height;
    for (int i = 0; i < width * height; ++i)
        m.p.push_back(toPoint(control[i]));
    error = std::max(error, 0.01f);
    std::vector<bool> straightX, straightY;
    subdivideColumns(m, error, straightX);
    m = transposed(m);
    subdivideColumns(m, error, straightY);
    m = transposed(m);

    // The control points between those on the curve, moved onto it: each
    // a quarter of each neighbour and half itself, down the columns then
    // along the rows.
    for (int x = 0; x < m.w; ++x)
        for (int y = 1; y + 1 < m.h; y += 2)
            m.at(x, y) = halfway(halfway(m.at(x, y), m.at(x, y + 1)), halfway(m.at(x, y), m.at(x, y - 1)));
    for (int y = 0; y < m.h; ++y)
        for (int x = 1; x + 1 < m.w; x += 2)
            m.at(x, y) = halfway(halfway(m.at(x, y), m.at(x + 1, y)), halfway(m.at(x, y), m.at(x - 1, y)));

    std::vector<int> keepX, keepY;
    for (int x = 0; x < m.w; ++x)
        if (x == 0 || x == m.w - 1 || !straightX[size_t(x)])
            keepX.push_back(x);
    for (int y = 0; y < m.h; ++y)
        if (y == 0 || y == m.h - 1 || !straightY[size_t(y)])
            keepY.push_back(y);
    g.width = int(keepX.size());
    g.height = int(keepY.size());
    for (int y : keepY)
        for (int x : keepX)
            g.points.push_back(toVertex(m.at(x, y)));
    meshNormals(g);
    return g;
}

int stitchPatches(std::vector<PatchGrid>& grids) {
    int added = 0;
    std::vector<bool> changed(grids.size(), false);
    // Only grids whose bounds touch can share an edge (points added lie on
    // the edges, so the bounds don't change).
    std::vector<std::array<float, 6>> bounds(grids.size(), {1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f});
    for (size_t i = 0; i < grids.size(); ++i)
        for (const BspVertex& v : grids[i].points)
            for (size_t k = 0; k < 3; ++k) {
                bounds[i][k] = std::min(bounds[i][k], v.pos[k] - 0.2f);
                bounds[i][k + 3] = std::max(bounds[i][k + 3], v.pos[k] + 0.2f);
            }
    auto touch = [&](size_t a, size_t b) {
        for (size_t k = 0; k < 3; ++k)
            if (bounds[a][k] > bounds[b][k + 3] || bounds[b][k] > bounds[a][k + 3])
                return false;
        return true;
    };
    for (bool again = true; again && added < 10000;) {
        again = false;
        for (size_t a = 0; a < grids.size(); ++a) {
            if (grids[a].points.empty())
                continue;
            for (size_t b = 0; b < grids.size(); ++b) {
                if (a == b || grids[b].points.empty() || grids[a].groupOrigin != grids[b].groupOrigin ||
                    grids[a].groupRadius != grids[b].groupRadius || !touch(a, b))
                    continue;
                while (added < 10000 && stitchOne(grids[a], grids[b])) {
                    ++added;
                    changed[b] = true;
                    again = true;
                }
            }
        }
    }
    for (size_t i = 0; i < grids.size(); ++i)
        if (changed[i])
            meshNormals(grids[i]);
    return added;
}

std::vector<PatchGrid> buildPatchGrids(const Bsp& bsp, float error) {
    std::vector<PatchGrid> grids(bsp.surfaces.size());
    for (size_t i = 0; i < bsp.surfaces.size(); ++i) {
        const BspSurface& s = bsp.surfaces[i];
        if (s.type != SurfaceType::Patch || s.firstVertex < 0 ||
            size_t(s.firstVertex) + size_t(std::max(s.patchWidth, 0)) * size_t(std::max(s.patchHeight, 0)) >
                bsp.vertices.size())
            continue;
        PatchGrid& g = grids[i];
        g = subdividePatch(&bsp.vertices[size_t(s.firstVertex)], s.patchWidth, s.patchHeight, error);
        // The group's bounds, which the map keeps in the lightmap vectors.
        const auto& lo = s.lightmapVecs[0];
        const auto& hi = s.lightmapVecs[1];
        float r2 = 0;
        for (size_t k = 0; k < 3; ++k) {
            g.groupOrigin[k] = (lo[k] + hi[k]) * 0.5f;
            r2 += (lo[k] - g.groupOrigin[k]) * (lo[k] - g.groupOrigin[k]);
        }
        g.groupRadius = std::sqrt(r2);
    }
    stitchPatches(grids);
    return grids;
}

void triangulateGrid(const PatchGrid& grid, std::vector<BspVertex>& vertices, std::vector<uint32_t>& indices) {
    if (grid.width < 2 || grid.height < 2)
        return;
    const auto base = uint32_t(vertices.size());
    vertices.insert(vertices.end(), grid.points.begin(), grid.points.end());
    appendTriangles(base, grid.width, grid.height, vertices, indices);
}

} // namespace reluva::idtech3
