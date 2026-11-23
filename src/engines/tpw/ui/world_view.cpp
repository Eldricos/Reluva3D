// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/world_view.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr int kGroundPixels = 2; // the painted ground's pixels to a cell's side
constexpr int kRingDots = 48;    // dots around a placed object's radius
constexpr int kQuadCells = 8;    // the ground drawn in squares of this many cells a side
// The layers' textures, two each: base, middle, top, as the Polar Zone's
// source pictures name them (base1, base2, mid1...).
constexpr size_t kLayers = 3;
// The masks that mix each layer's two textures: the base's own; the
// middle's and the top's, the same picture in the sources.
constexpr size_t kBlendMasks[kLayers] = {0, 2, 2};

// A picture's pixel, repeating: the land's pictures carry a border of
// half the level's scale on each side for wrapping, left out here.
const uint8_t *wrapped(const image::Image &img, int border, int x, int y) {
    return img.pixel(border + x % (img.w - 2 * border), border + y % (img.h - 2 * border));
}
uint8_t wrapped(const Land::Mask &mask, int border, int x, int y) {
    const int mx = border + x % (mask.width - 2 * border);
    const int my = border + y % (mask.height - 2 * border);
    return mask.pixels[size_t(my) * size_t(mask.width) + size_t(mx)];
}

// The guess, from the Polar Zone's source pictures: each layer its two
// textures mixed by its blend mask; the middle laid over the base as much
// as a cell's byte 2 says, the top over both as much as its byte 3.
// Byte 1, the base's weight, and the cover masks aren't used. It looks
// like the ground of the recordings, but isn't the game's own recipe.
image::Image paintGround(const Land &land) {
    image::Image out;
    if (land.levels().empty())
        return out;
    const Land::Level &level = land.levels()[0];
    const int border = int(level.scale / 2);
    if (level.textures.size() < 2 * kLayers || level.masks.size() <= kBlendMasks[kLayers - 1])
        return out;
    for (const image::Image &t : level.textures)
        if (t.w <= 2 * border || t.h <= 2 * border)
            return out;
    for (const Land::Mask &m : level.masks)
        if (m.width <= 2 * border || m.height <= 2 * border)
            return out;
    out.w = land.width() * kGroundPixels;
    out.h = land.depth() * kGroundPixels;
    out.rgba.assign(size_t(out.w) * size_t(out.h) * 4, 0);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            const uint8_t *cell = land.cell(x / kGroundPixels, y / kGroundPixels);
            if (cell[0] < 128)
                continue;
            float layer[kLayers][3];
            for (size_t l = 0; l < kLayers; ++l) {
                const uint8_t *a = wrapped(level.textures[2 * l], border, x, y);
                const uint8_t *b = wrapped(level.textures[2 * l + 1], border, x, y);
                const float t = float(wrapped(level.masks[kBlendMasks[l]], border, x, y)) / 255;
                for (int i = 0; i < 3; ++i)
                    layer[l][i] = float(a[i]) + (float(b[i]) - float(a[i])) * t;
            }
            uint8_t *p = out.pixel(x, y);
            for (int i = 0; i < 3; ++i) {
                float v = layer[0][i] + (layer[1][i] - layer[0][i]) * float(cell[2]) / 255;
                v += (layer[2][i] - v) * float(cell[3]) / 255;
                p[i] = uint8_t(std::min(255.0f, v));
            }
            p[3] = 255;
        }
    return out;
}

// The ground lies flat at y = 0 over x from 0 and z from 0, the file's
// first row of cells at the far side, z largest (inferred).
void drawGround(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &camera, const LoadedWorld &world) {
    const Land &land = world.land;
    const float size = Land::kCellSize;
    for (int z = 0; z < land.depth(); z += kQuadCells)
        for (int x = 0; x < land.width(); x += kQuadCells) {
            const int x1 = std::min(land.width(), x + kQuadCells);
            const int z1 = std::min(land.depth(), z + kQuadCells);
            const float u0 = float(x) / float(land.width());
            const float u1 = float(x1) / float(land.width());
            const float v0 = float(z) / float(land.depth());
            const float v1 = float(z1) / float(land.depth());
            auto at = [&](int cx, int cz) {
                return toViewerWorld({float(cx) * size, 0, float(land.depth() - cz) * size});
            };
            drawTexturedTriangle(rasterizer, camera, {at(x, z), at(x1, z), at(x1, z1)},
                                 {{{u0, v0}, {u1, v0}, {u1, v1}}}, &world.ground);
            drawTexturedTriangle(rasterizer, camera, {at(x, z), at(x1, z1), at(x, z1)},
                                 {{{u0, v0}, {u1, v1}, {u0, v1}}}, &world.ground);
        }
}

// A square of the colour around (x, y) on the screen, clipped.
void plot(Surface &screen, int x, int y, int size, Color c) {
    for (int dy = 0; dy < size; ++dy)
        for (int dx = 0; dx < size; ++dx) {
            const int sx = x + dx - size / 2;
            const int sy = y + dy - size / 2;
            if (sx < 0 || sy < 0 || sx >= screen.w || sy >= screen.h)
                continue;
            uint8_t *p = screen.row(sy) + sx * 4;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
        }
}

// The placed objects over the drawing: kind 1 cyan, 2 magenta, 3 yellow,
// others white.
void drawObjects(Surface &screen, const soft3d::OrbitCamera &camera, const ObjectMap &objects) {
    for (const ObjectMap::Object &o : objects.objects()) {
        const Color colour = o.kind == 1   ? Color{80, 230, 255}
                             : o.kind == 2 ? Color{255, 80, 230}
                             : o.kind == 3 ? Color{255, 230, 60}
                                           : Color{255, 255, 255};
        auto at = [&](const gfx::Vec3 &p, int size) {
            const soft3d::ViewVertex v = camera.toView(toViewerWorld(p));
            if (v.z < camera.nearZ)
                return;
            const soft3d::ScreenVertex s = camera.project(v);
            plot(screen, int(s.x), int(s.y), size, colour);
        };
        for (int i = 0; i < kRingDots; ++i) {
            const float a = float(i) * 2 * float(M_PI) / kRingDots;
            at({o.position[0] + o.radius * std::cos(a), o.position[1], o.position[2] + o.radius * std::sin(a)}, 2);
        }
        at(o.position, 6);
    }
}

} // namespace

Error LoadedWorld::load(const SearchSet &files, const std::string &folder) {
    *this = {};
    if (Error err = surroundings.load(files, folder + "/terrain/base.MD2"))
        return err;
    std::vector<uint8_t> data;
    if (!files.read(folder + "/terrain/base.lnd", data) && !land.parse(data)) {
        hasLand = true;
        ground = paintGround(land);
    }
    if (!files.read(folder + "/terrain/base.map", data) && !attributes.parse(data))
        hasAttributes = true;
    if (!files.read(folder + "/scape.omp", data) && !objects.parse(data))
        hasObjects = true;
    return Error::success();
}

void drawWorld(Surface &screen, soft3d::Rasterizer &rasterizer, const LoadedWorld &world, const WorldView &view) {
    const Land &land = world.land;
    const soft3d::Vec middle =
        toViewerWorld({float(land.width()) * Land::kCellSize / 2, 0, float(land.depth()) * Land::kCellSize / 2});
    const soft3d::OrbitCamera camera(world.hasLand ? middle : world.surroundings.centre, view.yaw, view.pitch,
                                     view.distance, screen.w, screen.h);
    rasterizer.begin(screen);
    drawMeshes(rasterizer, camera, world.surroundings, view.textured);
    if (view.ground && world.hasLand && world.ground.w > 0)
        drawGround(rasterizer, camera, world);
    if (view.objects && world.hasObjects)
        drawObjects(screen, camera, world.objects);
}

} // namespace reluva::tpw
