// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/render/draw3d.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::sith {

namespace {

constexpr float kDegrees = 3.14159265f / 180.0f;

soft3d::Vec toSoft(const Vec3 &v) {
    return {v.x, v.y, v.z};
}

float dot(const Vec3 &a, const Vec3 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

} // namespace

Transform Transform::place(const Vec3 &position, const Vec3 &pitchYawRoll) {
    // Yaw turns about z, pitch about x, roll about y, applied roll first,
    // then pitch, then yaw (docs/engines/sith/games/jk/formats/3do.md).
    const float p = pitchYawRoll.x * kDegrees;
    const float y = pitchYawRoll.y * kDegrees;
    const float r = pitchYawRoll.z * kDegrees;
    const float cp = std::cos(p);
    const float sp = std::sin(p);
    const float cy = std::cos(y);
    const float sy = std::sin(y);
    const float cr = std::cos(r);
    const float sr = std::sin(r);
    const std::array<std::array<float, 3>, 3> yaw{{{cy, -sy, 0}, {sy, cy, 0}, {0, 0, 1}}};
    const std::array<std::array<float, 3>, 3> pitch{{{1, 0, 0}, {0, cp, -sp}, {0, sp, cp}}};
    const std::array<std::array<float, 3>, 3> roll{{{cr, 0, sr}, {0, 1, 0}, {-sr, 0, cr}}};
    auto mul = [](const auto &a, const auto &b) {
        std::array<std::array<float, 3>, 3> m{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                m[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        return m;
    };
    Transform out;
    out.r = mul(mul(yaw, pitch), roll);
    out.t = position;
    return out;
}

Transform Transform::operator*(const Transform &inner) const {
    Transform out;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out.r[i][j] = r[i][0] * inner.r[0][j] + r[i][1] * inner.r[1][j] + r[i][2] * inner.r[2][j];
    out.t = apply(inner.t);
    return out;
}

Vec3 Transform::apply(const Vec3 &p) const {
    const Vec3 d = turn(p);
    return {d.x + t.x, d.y + t.y, d.z + t.z};
}

Vec3 Transform::turn(const Vec3 &d) const {
    return {r[0][0] * d.x + r[0][1] * d.y + r[0][2] * d.z, r[1][0] * d.x + r[1][1] * d.y + r[1][2] * d.z,
            r[2][0] * d.x + r[2][1] * d.y + r[2][2] * d.z};
}

const Material *MaterialCache::find(const std::string &name) {
    const std::string key = fs::toLower(name);
    auto it = _cache.find(key);
    if (it != _cache.end())
        return it->second.get();
    std::unique_ptr<Material> mat;
    for (const char *folder : {"mat/", "3do/mat/"}) {
        std::vector<uint8_t> bytes;
        auto m = std::make_unique<Material>();
        if (!_files.read(folder + key, bytes) && !parseMaterial(bytes, *m)) {
            mat = std::move(m);
            break;
        }
    }
    return _cache.emplace(key, std::move(mat)).first->second.get();
}

Draw3D::Draw3D(Surface &target, const soft3d::OrbitCamera &camera, const Colormap &colormap)
    : _camera(camera), _colormap(colormap) {
    _raster.begin(target);
}

void Draw3D::polygon(const std::vector<Corner> &corners, const Paint &paint) {
    const Material::Texture *tex = nullptr;
    int color = paint.color;
    if (paint.material && !paint.material->cels.empty()) {
        const Material::Cel &cel = paint.material->cels[size_t(paint.cel) % paint.material->cels.size()];
        if (cel.texture >= 0)
            tex = &paint.material->textures[size_t(cel.texture)];
        else if (color < 0)
            color = cel.color;
    }
    if (corners.size() < 3 || (!tex && color < 0)) {
        ++_skipped;
        return;
    }
    std::vector<soft3d::ViewVertex> view;
    view.reserve(corners.size());
    for (const Corner &c : corners) {
        soft3d::ViewVertex v = _camera.toView(toSoft(c.at), c.u, c.v);
        v.s = c.light;
        view.push_back(v);
    }
    view = soft3d::clipNear(view, _camera.nearZ);
    if (view.size() < 3) {
        ++_skipped;
        return;
    }
    std::vector<soft3d::ScreenVertex> screen;
    screen.reserve(view.size());
    for (const soft3d::ViewVertex &v : view)
        screen.push_back(_camera.project(v));

    const Colormap &cmap = paint.colormap ? *paint.colormap : _colormap;
    const Palette &pal = cmap.palette;
    const bool keyed = tex && tex->transparent && paint.transparent;
    auto shade = [&](float u, float v, float light, uint8_t *rgba) {
        uint8_t index = uint8_t(color < 0 ? 0 : color);
        if (tex) {
            const int w = tex->w;
            const int h = tex->h;
            int x = int(std::floor(u)) % w;
            int y = int(std::floor(v)) % h;
            x += x < 0 ? w : 0;
            y += y < 0 ? h : 0;
            index = tex->mips[0][size_t(y) * size_t(w) + size_t(x)];
            if (keyed && index == 0)
                return false;
        }
        const int level = std::clamp(int(light * (Colormap::kLightLevels - 1) + 0.5f), 0, Colormap::kLightLevels - 1);
        const Color c = pal[cmap.lit(level, index)];
        rgba[0] = c.r;
        rgba[1] = c.g;
        rgba[2] = c.b;
        rgba[3] = 255;
        return true;
    };
    for (size_t i = 1; i + 1 < screen.size(); ++i)
        _raster.drawTriangle(screen[0], screen[i], screen[i + 1], shade);
    ++_drawn;
}

void Draw3D::billboard(const Vec3 &centre, float width, float height, const Paint &paint, float light) {
    float texW = 1;
    float texH = 1;
    if (paint.material && !paint.material->textures.empty()) {
        texW = float(paint.material->textures[0].w);
        texH = float(paint.material->textures[0].h);
    }
    const soft3d::Vec r = _camera.right * (width / 2);
    const soft3d::Vec u = _camera.up * (height / 2);
    auto corner = [&](float sx, float sy, float tu, float tv) {
        Corner c;
        c.at = {centre.x + r.x * sx + u.x * sy, centre.y + r.y * sx + u.y * sy, centre.z + r.z * sx + u.z * sy};
        c.u = tu * texW;
        c.v = tv * texH;
        c.light = light;
        return c;
    };
    // Texel centres: the texture's last row and column, not the next tile's first.
    const float e = 0.999f;
    polygon({corner(-1, 1, 0, 0), corner(1, 1, e, 0), corner(1, -1, e, e), corner(-1, -1, 0, e)}, paint);
}

std::vector<Transform> poseNodes(const Model &model, const std::vector<std::array<Vec3, 2>> &pose) {
    std::vector<Transform> out(model.nodes.size());
    std::vector<bool> done(model.nodes.size(), false);
    // Parents come before their children in every model, but not by rule:
    // each node is placed once its parent is.
    for (size_t pass = 0; pass < model.nodes.size(); ++pass) {
        bool more = false;
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (done[i])
                continue;
            const Model::Node &n = model.nodes[i];
            if (n.parent >= 0 && !done[size_t(n.parent)]) {
                more = true;
                continue;
            }
            const bool posed = i < pose.size();
            const Transform local = Transform::place(posed ? pose[i][0] : n.position, posed ? pose[i][1] : n.rotation);
            out[i] = n.parent >= 0 ? out[size_t(n.parent)] * local : local;
            done[i] = true;
        }
        if (!more)
            break;
    }
    return out;
}

void drawModel(Draw3D &draw, MaterialCache &materials, const Model &model, const std::vector<Transform> &nodes,
               const Transform &where, int cel, const Colormap *colormap) {
    if (model.geosets.empty())
        return;
    const std::vector<Model::Mesh> &meshes = model.geosets[0];
    const Vec3 lightDir{0.3f, -0.5f, 0.81f};
    for (size_t n = 0; n < model.nodes.size() && n < nodes.size(); ++n) {
        const int m = model.nodes[n].mesh;
        if (m < 0 || m >= int(meshes.size()))
            continue;
        const Model::Mesh &mesh = meshes[size_t(m)];
        // The mesh's vertices are about its pivot, the joint it turns on.
        const Vec3 &pivot = model.nodes[n].pivot;
        const Transform place = where * nodes[n];
        for (const Model::Face &face : mesh.faces) {
            if (face.geometryMode == 0)
                continue;
            Paint paint;
            paint.material = face.material >= 0 ? materials.find(model.materials[size_t(face.material)]) : nullptr;
            paint.cel = cel;
            paint.colormap = colormap;
            if (face.geometryMode < 4)
                paint.color = paint.material && !paint.material->cels.empty() ? paint.material->cels[0].color : 0;
            const float lit = 0.45f + 0.55f * std::max(0.0f, dot(place.turn(face.normal), lightDir));
            std::vector<Corner> corners;
            corners.reserve(face.vertices.size());
            for (size_t c = 0; c < face.vertices.size(); ++c) {
                const Vec3 &v = mesh.vertices[size_t(face.vertices[c])];
                Corner corner;
                corner.at = place.apply({v.x + pivot.x, v.y + pivot.y, v.z + pivot.z});
                const int t = face.texVertices[c];
                if (t >= 0 && face.geometryMode >= 4) {
                    corner.u = mesh.texVertices[size_t(t)][0];
                    corner.v = mesh.texVertices[size_t(t)][1];
                }
                corner.light = std::min(1.0f, lit + face.extraLight);
                corners.push_back(corner);
            }
            if (face.geometryMode < 4)
                paint.material = nullptr;
            draw.polygon(corners, paint);
        }
    }
}

} // namespace reluva::sith
