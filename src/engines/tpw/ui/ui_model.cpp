// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/ui_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace reluva::tpw {

namespace {

struct Corner {
    float x;
    float y;
    float u;
    float v;
};

// A mesh's vertices on the screen, in pixels, as the model places them.
std::vector<std::array<float, 2>> placed(const Model::Mesh &mesh) {
    std::vector<std::array<float, 2>> out;
    for (const gfx::Vec3 &p : mesh.positions) {
        const gfx::Vec3 at = gfx::transformPoint(mesh.transform, p);
        out.push_back({at[0] / kUiUnitsPerPixel, -at[1] / kUiUnitsPerPixel});
    }
    return out;
}

int texel(float t, int size) {
    if (!std::isfinite(t))
        return 0;
    const float f = t - std::floor(t);
    return std::min(size - 1, int(f * float(size)));
}

// Whether a pixel's middle exactly on the edge from a to b belongs to the
// triangle: its top and left edges do, so one shared by two triangles is
// drawn once (the window's glass is see-through and would show it twice).
bool ownsEdge(const Corner &a, const Corner &b) {
    return (a.y == b.y && b.x < a.x) || b.y > a.y;
}

// A triangle on the screen, its texture's coordinates straight across it,
// as a flat model facing the screen has them.
void fillTriangle(Surface &screen, std::array<Corner, 3> c, const image::Image &texture) {
    float area = (c[1].x - c[0].x) * (c[2].y - c[0].y) - (c[2].x - c[0].x) * (c[1].y - c[0].y);
    if (std::fabs(area) < 1e-6f)
        return;
    if (area < 0) {
        std::swap(c[1], c[2]);
        area = -area;
    }
    const int x0 = std::max(0, int(std::floor(std::min({c[0].x, c[1].x, c[2].x}))));
    const int x1 = std::min(screen.w, int(std::ceil(std::max({c[0].x, c[1].x, c[2].x}))));
    const int y0 = std::max(0, int(std::floor(std::min({c[0].y, c[1].y, c[2].y}))));
    const int y1 = std::min(screen.h, int(std::ceil(std::max({c[0].y, c[1].y, c[2].y}))));
    // Each corner's weight: how far the pixel is over the edge opposite it.
    auto edge = [](const Corner &a, const Corner &b, float px, float py) {
        return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
    };
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const float px = float(x) + 0.5f;
            const float py = float(y) + 0.5f;
            const float w0 = edge(c[1], c[2], px, py);
            const float w1 = edge(c[2], c[0], px, py);
            const float w2 = edge(c[0], c[1], px, py);
            if (w0 < 0 || w1 < 0 || w2 < 0 || (w0 == 0 && !ownsEdge(c[1], c[2])) ||
                (w1 == 0 && !ownsEdge(c[2], c[0])) || (w2 == 0 && !ownsEdge(c[0], c[1])))
                continue;
            const float u = (w0 * c[0].u + w1 * c[1].u + w2 * c[2].u) / area;
            const float v = (w0 * c[0].v + w1 * c[1].v + w2 * c[2].v) / area;
            // v runs up the texture, as on the game's other models.
            const uint8_t *t = texture.pixel(texel(u, texture.w), texel(1 - v, texture.h));
            uint8_t *dst = screen.row(y) + x * 4;
            const int a = t[3];
            for (int i = 0; i < 3; ++i)
                dst[i] = uint8_t((t[i] * a + dst[i] * (255 - a)) / 255);
        }
}

// Draws the mesh's faces with their vertices where `at` puts them.
void drawFaces(Surface &screen, const LoadedModel &model, const Model::Mesh &mesh,
               const std::vector<std::array<float, 2>> &at) {
    for (const Model::Group &g : mesh.groups) {
        if (g.texture >= model.textures.size() || model.textures[g.texture].rgba.empty())
            continue;
        for (size_t f = g.firstFace; f < size_t(g.firstFace) + g.faceCount; ++f) {
            std::array<Corner, 3> corners;
            for (size_t k = 0; k < 3; ++k) {
                const size_t vertex = mesh.faces[f].vertex[k];
                const auto &p = at[mesh.positionOf[vertex]];
                corners[k] = {p[0], p[1], mesh.uv[vertex][0], mesh.uv[vertex][1]};
            }
            fillTriangle(screen, corners, model.textures[g.texture]);
        }
    }
}

// Where a coordinate of a mesh going from `low` to `high` goes, stretched
// to `from` to `to`: kept its distance from the nearer edge.
float stretch(float c, float low, float high, float from, float to) {
    return c - low <= high - c ? from + (c - low) : to - (high - c);
}

} // namespace

void drawUiMesh(Surface &screen, const LoadedModel &model, size_t mesh) {
    if (mesh < model.model.meshes().size()) {
        const Model::Mesh &m = model.model.meshes()[mesh];
        drawFaces(screen, model, m, placed(m));
    }
}

void drawUiMesh(Surface &screen, const LoadedModel &model, size_t mesh, const ScreenRect &to) {
    if (mesh >= model.model.meshes().size())
        return;
    const Model::Mesh &m = model.model.meshes()[mesh];
    std::vector<std::array<float, 2>> at = placed(m);
    std::array<float, 2> low{1e9f, 1e9f};
    std::array<float, 2> high{-1e9f, -1e9f};
    for (const auto &p : at)
        for (size_t i = 0; i < 2; ++i) {
            low[i] = std::min(low[i], p[i]);
            high[i] = std::max(high[i], p[i]);
        }
    for (auto &p : at) {
        p[0] = stretch(p[0], low[0], high[0], float(to.x), float(to.x + to.w));
        p[1] = stretch(p[1], low[1], high[1], float(to.y), float(to.y + to.h));
    }
    drawFaces(screen, model, m, at);
}

ScreenRect uiMeshRect(const LoadedModel &model, size_t mesh) {
    if (mesh >= model.model.meshes().size() || model.model.meshes()[mesh].positions.empty())
        return {};
    float x0 = 1e9f;
    float y0 = 1e9f;
    float x1 = -1e9f;
    float y1 = -1e9f;
    for (const auto &p : placed(model.model.meshes()[mesh])) {
        x0 = std::min(x0, p[0]);
        y0 = std::min(y0, p[1]);
        x1 = std::max(x1, p[0]);
        y1 = std::max(y1, p[1]);
    }
    return {int(std::lround(x0)), int(std::lround(y0)), int(std::lround(x1 - x0)), int(std::lround(y1 - y0))};
}

} // namespace reluva::tpw
