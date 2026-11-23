// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/model_view.h"

#include "common/fs.h"
#include "engines/tpw/textures.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr uint8_t kUntextured = 190;
constexpr int kDot = 3; // a height sample's size on the screen, in pixels
// A group drawn at full brightness, unlit: the beams, flames, lightning and
// signs have it.
constexpr uint32_t kUnlitGroup = 0x1;
// A texture blended by its alpha rather than cut at half.
constexpr uint32_t kAlphaTexture = 0x2;
// A blended texel at least this opaque hides what's behind it.
constexpr int kOpaqueAlpha = 240;

// A texture coordinate as a texel, repeating; one that isn't a number is 0.
int texel(float t, int size) {
    if (!std::isfinite(t))
        return 0;
    const float f = t - std::floor(t);
    return std::min(size - 1, int(f * float(size)));
}

// The height map's samples, each at the middle of its square of the
// model's ground, from the model's origin, as dots over the model:
// yellow, red where the sample is the highest of its tile's.
void drawHeights(Surface &screen, const LoadedModel &loaded, const soft3d::OrbitCamera &camera) {
    const HeightMap &map = loaded.heights;
    constexpr int kSide = HeightMap::kSamplesPerTile;
    constexpr float kStep = HeightMap::kTileSize / float(kSide);
    for (int z = 0; z < map.depth() * kSide; ++z)
        for (int x = 0; x < map.width() * kSide; ++x) {
            const uint8_t h = map.sample(x, z);
            const gfx::Vec3 at = {(float(x) + 0.5f) * kStep, float(h) * HeightMap::kHeightStep,
                                  (float(z) + 0.5f) * kStep};
            const soft3d::ViewVertex v = camera.toView(toViewerWorld(at));
            if (v.z < camera.nearZ)
                continue;
            const soft3d::ScreenVertex p = camera.project(v);
            const bool top = h == map.tileHeights()[size_t((z / kSide) * map.width() + x / kSide)];
            for (int dy = 0; dy < kDot; ++dy)
                for (int dx = 0; dx < kDot; ++dx) {
                    const int sx = int(p.x) + dx - kDot / 2;
                    const int sy = int(p.y) + dy - kDot / 2;
                    if (sx < 0 || sy < 0 || sx >= screen.w || sy >= screen.h)
                        continue;
                    uint8_t *px = screen.row(sy) + sx * 4;
                    px[0] = 255;
                    px[1] = top ? 60 : 230;
                    px[2] = 40;
                }
        }
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
    // Its animations: the folder's animations named as it, and more.
    const size_t slash = name.rfind('/');
    const std::string folder = name.substr(0, slash + 1);
    const std::string stem = fs::toLower(name.substr(slash + 1, name.size() - slash - 5));
    for (const std::string &other : files.list()) {
        if (other.size() <= folder.size() + stem.size() + 4 || other.compare(0, folder.size(), folder) != 0 ||
            other.find('/', folder.size()) != std::string::npos ||
            fs::toLower(other.substr(other.size() - 4)) != ".md2" ||
            fs::toLower(other.substr(folder.size(), stem.size())) != stem)
            continue;
        Animation animation;
        if (!files.read(other, data) && !animation.parse(data)) {
            animationNames.push_back(other.substr(folder.size()));
            animations.push_back(std::move(animation));
        }
    }
    std::string heightsName = name.substr(0, name.size() - 4) + ".hmp";
    if (files.has(heightsName) && !files.read(heightsName, data) && !heights.parse(data))
        hasHeights = true;
    soft3d::Vec low{INFINITY, INFINITY, INFINITY};
    soft3d::Vec high{-INFINITY, -INFINITY, -INFINITY};
    for (const Model::Mesh &mesh : model.meshes())
        for (const gfx::Vec3 &p : mesh.positions) {
            const soft3d::Vec w = toViewerWorld(gfx::transformPoint(mesh.transform, p));
            low = {std::min(low.x, w.x), std::min(low.y, w.y), std::min(low.z, w.z)};
            high = {std::max(high.x, w.x), std::max(high.y, w.y), std::max(high.z, w.z)};
        }
    if (low.x <= high.x) {
        centre = (low + high) * 0.5f;
        radius = std::max(1e-3f, std::sqrt(soft3d::dot(high - low, high - low)) / 2);
    }
    return Error::success();
}

soft3d::Vec defaultLight() {
    return soft3d::normalize({0.4f, -0.6f, 0.7f});
}

soft3d::Vec toViewerWorld(const gfx::Vec3 &p) {
    // Swapping y and z also turns a left-handed space into a right-handed
    // one, as Direct3D's would be (inferred).
    return {p[0], p[2], p[1]};
}

void drawTexturedTriangle(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &camera,
                          const std::array<soft3d::Vec, 3> &corners, const std::array<std::array<float, 2>, 3> &uv,
                          const image::Image *texture, const soft3d::Vec &light, TriangleStyle style) {
    // Lit by the face's own slope, from either side.
    const soft3d::Vec normal = soft3d::normalize(soft3d::cross(corners[1] - corners[0], corners[2] - corners[0]));
    const float shade = style.unlit ? 1.0f : 0.4f + 0.6f * std::fabs(soft3d::dot(normal, light));
    std::vector<soft3d::ViewVertex> polygon;
    for (size_t k = 0; k < 3; ++k) {
        soft3d::ViewVertex v = camera.toView(corners[k], uv[k][0], uv[k][1]);
        v.s = shade;
        polygon.push_back(v);
    }
    polygon = soft3d::clipNear(polygon, camera.nearZ);
    auto paint = [texture, style](float u, float v, float s, uint8_t *rgba) {
        static constexpr uint8_t kGrey[4] = {kUntextured, kUntextured, kUntextured, 255};
        const uint8_t *c = kGrey;
        if (texture) {
            // v runs up the picture: the hall's paintings stand upright so.
            c = texture->pixel(texel(u, texture->w), texel(1 - v, texture->h));
            if (!style.blended && c[3] < 128)
                return false;
        }
        const int a = style.blended && texture ? c[3] : 255;
        for (int i = 0; i < 3; ++i)
            rgba[i] = uint8_t((std::min(255.0f, float(c[i]) * s) * float(a) + float(rgba[i]) * float(255 - a)) / 255);
        rgba[3] = 255;
        return a >= kOpaqueAlpha;
    };
    for (size_t k = 2; k < polygon.size(); ++k)
        rasterizer.drawTriangle(camera.project(polygon[0]), camera.project(polygon[k - 1]), camera.project(polygon[k]),
                                paint);
}

void drawMeshes(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &camera, const LoadedModel &loaded,
                bool textured, const soft3d::Vec &light, const std::vector<gfx::Mat4> *transforms,
                const std::vector<std::vector<gfx::Vec3>> *positions) {
    struct MeshFaces {
        const Model::Mesh *mesh;
        const gfx::Mat4 *transform;
        const std::vector<gfx::Vec3> *places;
        std::vector<const image::Image *> texture;
        std::vector<TriangleStyle> style;
    };
    std::vector<MeshFaces> meshFaces;
    for (size_t m = 0; m < loaded.model.meshes().size(); ++m) {
        const Model::Mesh &mesh = loaded.model.meshes()[m];
        const gfx::Mat4 &transform = transforms && m < transforms->size() ? (*transforms)[m] : mesh.transform;
        const std::vector<gfx::Vec3> &places =
            positions && m < positions->size() && (*positions)[m].size() == mesh.positions.size() ? (*positions)[m]
                                                                                                  : mesh.positions;
        // Each face's texture and style, by the group it is in; none outside them.
        std::vector<const image::Image *> faceTexture(mesh.faces.size(), nullptr);
        std::vector<TriangleStyle> faceStyle(mesh.faces.size());
        for (const Model::Group &g : mesh.groups) {
            if (textured && g.texture < loaded.textures.size() && loaded.textures[g.texture].w > 0)
                std::fill_n(faceTexture.begin() + long(g.firstFace), g.faceCount, &loaded.textures[g.texture]);
            const bool blended =
                g.texture < loaded.model.textures().size() && loaded.model.textures()[g.texture].flags & kAlphaTexture;
            std::fill_n(faceStyle.begin() + long(g.firstFace), g.faceCount,
                        TriangleStyle{(g.flags & kUnlitGroup) != 0, blended});
        }
        meshFaces.push_back({&mesh, &transform, &places, std::move(faceTexture), std::move(faceStyle)});
    }
    // The blended faces over the rest.
    for (int pass = 0; pass < 2; ++pass)
        for (const MeshFaces &m : meshFaces) {
            const Model::Mesh &mesh = *m.mesh;
            for (size_t f = 0; f < mesh.faces.size(); ++f) {
                if (m.style[f].blended != (pass == 1))
                    continue;
                const Model::Face &face = mesh.faces[f];
                std::array<soft3d::Vec, 3> corners;
                std::array<std::array<float, 2>, 3> uv;
                for (size_t k = 0; k < 3; ++k) {
                    corners[k] =
                        toViewerWorld(gfx::transformPoint(*m.transform, (*m.places)[mesh.positionOf[face.vertex[k]]]));
                    uv[k] = mesh.uv[face.vertex[k]];
                }
                drawTexturedTriangle(rasterizer, camera, corners, uv, m.texture[f], light, m.style[f]);
            }
        }
}

std::vector<gfx::Mat4> nodeTransforms(const LoadedModel &loaded, int animation, float frame) {
    const auto &nodes = loaded.model.nodes();
    std::vector<gfx::Mat4> local(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
        local[i] = nodes[i].local;
    if (animation >= 0 && size_t(animation) < loaded.animations.size())
        for (const Animation::Track &t : loaded.animations[size_t(animation)].tracks()) {
            const auto &order = loaded.model.animationOrder();
            if (t.target < order.size() && order[t.target] != Model::kNoNode &&
                (t.curve != Animation::Curve::kNone || !t.rotations.empty() || !t.scales.empty())) {
                const size_t node = order[t.target];
                local[node] = Animation::pose(t, frame, nodes[node].local);
            }
        }
    // Each node comes after its parent.
    std::vector<gfx::Mat4> world(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
        world[i] = nodes[i].parent >= 0 ? gfx::multiply(world[size_t(nodes[i].parent)], local[i]) : local[i];
    return world;
}

std::vector<std::vector<gfx::Vec3>> meshPositions(const LoadedModel &loaded, int animation, float frame) {
    const auto &meshes = loaded.model.meshes();
    std::vector<std::vector<gfx::Vec3>> out;
    for (const Model::Mesh &mesh : meshes)
        out.push_back(mesh.positions);
    if (animation < 0 || size_t(animation) >= loaded.animations.size())
        return out;
    const auto &nodes = loaded.model.nodes();
    const auto &order = loaded.model.animationOrder();
    for (const Animation::Track &t : loaded.animations[size_t(animation)].tracks()) {
        if (t.vertexRuns.empty() || t.target >= order.size() || order[t.target] == Model::kNoNode)
            continue;
        const int mesh = nodes[order[t.target]].mesh;
        if (mesh >= 0)
            Animation::poseVertices(t, frame, out[size_t(mesh)]);
    }
    return out;
}

std::vector<gfx::Mat4> meshTransforms(const LoadedModel &loaded, int animation, float frame) {
    const std::vector<gfx::Mat4> world = nodeTransforms(loaded, animation, frame);
    std::vector<gfx::Mat4> out;
    for (const Model::Mesh &mesh : loaded.model.meshes())
        out.push_back(mesh.node < world.size() ? world[mesh.node] : mesh.transform);
    return out;
}

void drawModel(Surface &screen, soft3d::Rasterizer &rasterizer, const LoadedModel &loaded, const ModelView &view) {
    const soft3d::OrbitCamera camera(loaded.centre, view.yaw, view.pitch, loaded.radius * view.distance, screen.w,
                                     screen.h);
    rasterizer.begin(screen);
    const std::vector<gfx::Mat4> transforms = meshTransforms(loaded, view.animation, view.frame);
    const std::vector<std::vector<gfx::Vec3>> positions = meshPositions(loaded, view.animation, view.frame);
    drawMeshes(rasterizer, camera, loaded, view.textured, defaultLight(), &transforms, &positions);
    if (view.heights && loaded.hasHeights)
        drawHeights(screen, loaded, camera);
}

} // namespace reluva::tpw
