// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The grid's layout and how the original samples it were read from the
// GPL Quake III Arena renderer (tr_bsp.c R_LoadLightGrid, tr_light.c);
// docs/engines/idtech3/formats/bsp.md describes it.

#include "engines/idtech3/render/light_grid.h"
#include "engines/idtech3/render/world_renderer.h" // brightenLight

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

constexpr float kTau = 6.28318530718f;
constexpr float kMinimumAmbient = 32;

} // namespace

void LightGrid::build(const Bsp &bsp, std::array<float, 3> cellSize) {
    _cells.clear();
    if (bsp.models.empty() || bsp.gridIndex.empty())
        return;
    _size = cellSize;
    // Grid points on multiples of the cell size inside the world's bounds.
    for (int i = 0; i < 3; ++i) {
        const size_t k = size_t(i);
        if (_size[k] <= 0)
            return;
        _origin[k] = _size[k] * std::ceil(bsp.models[0].mins[k] / _size[k]);
        const float top = _size[k] * std::floor(bsp.models[0].maxs[k] / _size[k]);
        _count[k] = int((top - _origin[k]) / _size[k]) + 1;
        if (_count[k] < 1)
            return;
    }
    if (size_t(_count[0]) * size_t(_count[1]) * size_t(_count[2]) != bsp.gridIndex.size())
        return;
    // Raven's maps skip a point with no style, Quake III's one whose
    // ambient light is black; parseBsp gives the latter no style too.
    _points.clear();
    for (const Bsp::GridPoint &g : bsp.gridPoints) {
        Point p;
        p.latLong = g.latLong;
        p.inWall = g.styles[0] == kNoStyle;
        int ambient[3] = {};
        int directed[3] = {};
        // Its styles in order, up to the first unused.
        for (size_t k = 0; k < kLightStyles && !p.inWall; ++k) {
            if (g.styles[k] == kNoStyle)
                break;
            std::array<uint8_t, 3> a = g.ambient[k];
            std::array<uint8_t, 3> d = g.directed[k];
            brightenLight(a.data());
            brightenLight(d.data());
            for (size_t c = 0; c < 3; ++c) {
                ambient[c] += a[c];
                directed[c] += d[c];
            }
        }
        for (size_t c = 0; c < 3; ++c) {
            p.ambient[c] = static_cast<uint8_t>(std::min(ambient[c], 255));
            p.directed[c] = static_cast<uint8_t>(std::min(directed[c], 255));
        }
        _points.push_back(p);
    }
    _cells = bsp.gridIndex;
}

PointLight LightGrid::sample(const std::array<float, 3> &pos, bool forModel) const {
    PointLight light;
    if (_cells.empty()) {
        light.ambient = {150 + kMinimumAmbient, 150 + kMinimumAmbient, 150 + kMinimumAmbient};
        light.directed = {150, 150, 150};
        const float n = std::sqrt(0.45f * 0.45f + 0.3f * 0.3f + 0.9f * 0.9f);
        light.direction = {0.45f / n, 0.3f / n, 0.9f / n};
        return light;
    }
    int cell[3];
    float frac[3];
    for (int i = 0; i < 3; ++i) {
        const float v = (pos[size_t(i)] - _origin[size_t(i)]) / _size[size_t(i)];
        const float f = std::floor(v);
        frac[i] = v - f;
        cell[i] = std::clamp(int(f), 0, _count[size_t(i)] - 1);
    }
    const size_t step[3] = {1, size_t(_count[0]), size_t(_count[0]) * size_t(_count[1])};
    float total = 0;
    std::array<float, 3> dir{};
    for (int corner = 0; corner < 8; ++corner) {
        float weight = 1;
        size_t at = size_t(cell[0]) * step[0] + size_t(cell[1]) * step[1] + size_t(cell[2]) * step[2];
        bool inside = true;
        for (int j = 0; j < 3; ++j) {
            if (corner & (1 << j)) {
                if (cell[j] + 1 > _count[size_t(j)] - 1) {
                    inside = false; // past the grid's edge
                    break;
                }
                weight *= frac[j];
                at += step[j];
            } else {
                weight *= 1 - frac[j];
            }
        }
        if (!inside)
            continue;
        const Point &p = _points[_cells[at]];
        if (p.inWall)
            continue;
        total += weight;
        for (size_t c = 0; c < 3; ++c) {
            light.ambient[c] += weight * p.ambient[c];
            light.directed[c] += weight * p.directed[c];
        }
        // Direction as two angles: longitude (its first byte) from +Z,
        // latitude (its second) around it.
        const float lat = float(p.latLong[1]) * kTau / 256;
        const float lng = float(p.latLong[0]) * kTau / 256;
        const std::array<float, 3> n{std::cos(lat) * std::sin(lng), std::sin(lat) * std::sin(lng), std::cos(lng)};
        for (int c = 0; c < 3; ++c)
            dir[size_t(c)] += weight * n[size_t(c)];
    }
    if (total > 0 && total < 0.99f)
        for (int c = 0; c < 3; ++c) {
            light.ambient[size_t(c)] /= total;
            light.directed[size_t(c)] /= total;
        }
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len > 0)
        light.direction = {dir[0] / len, dir[1] / len, dir[2] / len};
    if (forModel)
        for (float &a : light.ambient)
            a = std::min(255.0f, a + kMinimumAmbient);
    return light;
}

} // namespace reluva::idtech3
