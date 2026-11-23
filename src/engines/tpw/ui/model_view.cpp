// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/model_view.h"

#include "engines/tpw/textures.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr uint8_t kUntextured = 190;

// The game's models stand with y up; the viewer's world has z up. Swapping
// the two also turns a left-handed space into a right-handed one, as
// Direct3D's would be (inferred).
soft3d::Vec toWorld(const gfx::Vec3 &p) {
    return {p[0], p[2], p[1]};
}

// A texture coordinate as a texel, repeating; one that isn't a number is 0.
int texel(float t, int size) {
    if (!std::isfinite(t))
        return 0;
    const float f = t - std::floor(t);
    return std::min(size - 1, int(f * float(size)));
}

} // namespace

Error LoadedModel::load(const SearchSet &files, const std::string &name) {
    *this = {};
    std::vector<uint8_t> data;
    if (Error err = files.read(name, data))
        return err;
    if (Error err = model.parse(data))
        return err;
    textures.resize(model.textures().size());
    for (size_t i = 0; i < textures.size(); ++i) {
        const std::string found = findTexture(files, name, model.textures()[i].name);
        if (!found.empty() && !loadPicture(files, found, textures[i]) && textures[i].w > 0)
            ++texturesFound;
    }
    soft3d::Vec low{INFINITY, INFINITY, INFINITY};
    soft3d::Vec high{-INFINITY, -INFINITY, -INFINITY};
    for (const Model::Mesh &mesh : model.meshes())
        for (const gfx::Vec3 &p : mesh.positions) {
            const soft3d::Vec w = toWorld(gfx::transformPoint(mesh.transform, p));
            low = {std::min(low.x, w.x), std::min(low.y, w.y), std::min(low.z, w.z)};
            high = {std::max(high.x, w.x), std::max(high.y, w.y), std::max(high.z, w.z)};
        }
    if (low.x <= high.x) {
        centre = (low + high) * 0.5f;
        radius = std::max(1e-3f, std::sqrt(soft3d::dot(high - low, high - low)) / 2);
    }
    return Error::success();
}

void drawModel(Surface &screen, soft3d::Rasterizer &rasterizer, const LoadedModel &loaded, const ModelView &view) {
    const soft3d::OrbitCamera camera(loaded.centre, view.yaw, view.pitch, loaded.radius * view.distance, screen.w,
                                     screen.h);
    const soft3d::Vec light = soft3d::normalize({0.4f, -0.6f, 0.7f});
    rasterizer.begin(screen);
    for (const Model::Mesh &mesh : loaded.model.meshes()) {
        // Each face's texture, by the group it is in; none outside them.
        std::vector<const image::Image *> faceTexture(mesh.faces.size(), nullptr);
        for (const Model::Group &g : mesh.groups)
            if (view.textured && g.texture < loaded.textures.size() && loaded.textures[g.texture].w > 0)
                std::fill_n(faceTexture.begin() + long(g.firstFace), g.faceCount, &loaded.textures[g.texture]);
        for (size_t f = 0; f < mesh.faces.size(); ++f) {
            const Model::Face &face = mesh.faces[f];
            soft3d::Vec corner[3];
            for (size_t k = 0; k < 3; ++k)
                corner[k] =
                    toWorld(gfx::transformPoint(mesh.transform, mesh.positions[mesh.positionOf[face.vertex[k]]]));
            // Lit by the face's own shape, from either side.
            const soft3d::Vec normal = soft3d::normalize(soft3d::cross(corner[1] - corner[0], corner[2] - corner[0]));
            const float shade = 0.4f + 0.6f * std::fabs(soft3d::dot(normal, light));
            std::vector<soft3d::ViewVertex> polygon;
            for (size_t k = 0; k < 3; ++k) {
                const auto &uv = mesh.uv[face.vertex[k]];
                soft3d::ViewVertex v = camera.toView(corner[k], uv[0], uv[1]);
                v.s = shade;
                polygon.push_back(v);
            }
            polygon = soft3d::clipNear(polygon, camera.nearZ);
            const image::Image *texture = faceTexture[f];
            auto paint = [texture](float u, float v, float s, uint8_t *rgba) {
                static constexpr uint8_t kGrey[4] = {kUntextured, kUntextured, kUntextured, 255};
                const uint8_t *c = kGrey;
                if (texture) {
                    c = texture->pixel(texel(u, texture->w), texel(v, texture->h));
                    if (c[3] < 128)
                        return false;
                }
                for (int i = 0; i < 3; ++i)
                    rgba[i] = uint8_t(std::min(255.0f, float(c[i]) * s));
                rgba[3] = 255;
                return true;
            };
            for (size_t k = 2; k < polygon.size(); ++k)
                rasterizer.drawTriangle(camera.project(polygon[0]), camera.project(polygon[k - 1]),
                                        camera.project(polygon[k]), paint);
        }
    }
}

} // namespace reluva::tpw
