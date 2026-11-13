// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/combatrender.h"

#include <algorithm>

namespace reluva::stgenerations {

namespace {

// Not M_PI: MSVC only has it with _USE_MATH_DEFINES.
constexpr double kPi = 3.14159265358979323846;

const double kNear = 20;
const double kAmbient = 0.55;

uint32_t shadeColour(uint32_t c, double shade) {
    uint32_t r = std::min(255u, (uint32_t)(((c >> 16) & 0xFF) * shade));
    uint32_t g = std::min(255u, (uint32_t)(((c >> 8) & 0xFF) * shade));
    uint32_t b = std::min(255u, (uint32_t)((c & 0xFF) * shade));
    return 0xFF000000u | r << 16 | g << 8 | b;
}

uint32_t addColour(uint32_t a, uint32_t b) {
    uint32_t r = std::min(255u, ((a >> 16) & 0xFF) + ((b >> 16) & 0xFF));
    uint32_t g = std::min(255u, ((a >> 8) & 0xFF) + ((b >> 8) & 0xFF));
    uint32_t bl = std::min(255u, (a & 0xFF) + (b & 0xFF));
    return 0xFF000000u | r << 16 | g << 8 | bl;
}

} // namespace

void Mat3::rotate(double yaw, double pitch, double roll) {
    auto turn = [](Vec3 &a, Vec3 &b, double angle) {
        double c = std::cos(angle), s = std::sin(angle);
        Vec3 na = a * c + b * s, nb = b * c - a * s;
        a = na;
        b = nb;
    };
    // Yaw turns forward towards right; pitch forward towards up; roll up
    // towards right.
    turn(forward, right, yaw);
    turn(forward, up, pitch);
    turn(up, right, roll);
    orthonormalize();
}

void Mat3::orthonormalize() {
    forward = forward.normalized();
    right = up.cross(forward).normalized();
    up = forward.cross(right).normalized();
}

Mat3 Mat3::fromYawPitch(double yaw, double pitch) {
    Vec3 forward(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
    return lookAlong(forward);
}

Mat3 Mat3::lookAlong(const Vec3 &forward, const Vec3 &upHint) {
    Mat3 m;
    m.forward = forward.normalized();
    Vec3 hint = std::fabs(m.forward.dot(upHint.normalized())) > 0.99 ? Vec3(0, 0, 1) : upHint;
    m.right = hint.cross(m.forward).normalized();
    m.up = m.forward.cross(m.right).normalized();
    return m;
}

void CombatRenderer::setViewport(int w, int h, double fovDegrees) {
    _w = w;
    _h = h;
    _focal = (w / 2.0) / std::tan(fovDegrees * kPi / 360);
    _depth.assign((size_t)w * h, 1e30f);
}

void CombatRenderer::setCamera(const Vec3 &position, const Mat3 &orientation) {
    _cameraPos = position;
    _camera = orientation;
}

void CombatRenderer::begin(Canvas &target, int x, int y) {
    _target = &target;
    _ox = x;
    _oy = y;
    std::fill(_depth.begin(), _depth.end(), 1e30f);
}

bool CombatRenderer::project(const Vec3 &world, double &sx, double &sy, double &depth) const {
    Vec3 v = _camera.inverse(world - _cameraPos);
    depth = v.z;
    if (v.z < kNear)
        return false;
    sx = _w / 2.0 + _focal * v.x / v.z;
    sy = _h / 2.0 - _focal * v.y / v.z;
    return true;
}

void CombatRenderer::plot(int x, int y, uint32_t colour) {
    if (!_target || x < 0 || y < 0 || x >= _w || y >= _h)
        return;
    *(uint32_t *)_target->getBasePtr(_ox + x, _oy + y) = colour;
}

void CombatRenderer::drawModel(const Model3D &model, const std::vector<CombatMaterial> &materials,
                               const TextureLookup &textures, const Vec3 &position, const Mat3 &orientation,
                               double scale) {
    if (!_target)
        return;
    Vec3 light = Vec3(0, 1, 1).normalized(); // towards the light at (0, -1, -1)
    for (const Model3D::Part &part : model.parts) {
        std::vector<Vec3> view(part.vertices.size());
        for (size_t i = 0; i < part.vertices.size(); i++) {
            const Model3D::Vertex &v = part.vertices[i];
            Vec3 world = position + orientation.apply(Vec3(v.x, v.y, v.z) * scale);
            view[i] = _camera.inverse(world - _cameraPos);
        }
        for (const Model3D::Triangle &t : part.triangles) {
            // The top bit of a face's material is a flag (shields, debris,
            // the phaser beam); the rest is the index.
            uint16_t index = t.material & 0x7FFF;
            const CombatMaterial *m = index < materials.size() ? &materials[index] : nullptr;
            const CombatImage *texture =
                m && part.textured && !m->texture.empty() && (m->flags & 3) != 3 ? textures(m->texture) : nullptr;
            Vec3 normal = orientation.apply(Vec3(t.nx, t.ny, t.nz));
            double shade = kAmbient + (1 - kAmbient) * std::max(0.0, normal.dot(light));
            // Only the shields' materials have a parameter: they are see-through
            // and shine with their own light.
            bool blend = m && m->param != 0;
            if (blend) {
                shade = 1;
                // Without the top bit a see-through face shows one side only,
                // so a sphere's far half doesn't double its colour.
                if (!(t.material & 0x8000) && normal.dot(_camera.apply(view[t.a])) > 0)
                    continue;
            }
            ClipVertex v[3];
            const uint16_t idx[3] = {t.a, t.b, t.c};
            for (int k = 0; k < 3; k++) {
                v[k].view = view[idx[k]];
                v[k].u = part.vertices[idx[k]].u;
                v[k].v = part.vertices[idx[k]].v;
            }
            drawTriangle(v, 3, texture, m ? m->rgb() : 0xFF808080u, shade, m && (m->flags & 3) == 2, blend);
        }
    }
}

void CombatRenderer::drawTriangle(const ClipVertex *in, int count, const CombatImage *texture, uint32_t colour,
                                  double shade, bool keyBlack, bool blend) {
    // Clip against the near plane.
    ClipVertex poly[8];
    int n = 0;
    for (int i = 0; i < count; i++) {
        const ClipVertex &a = in[i], &b = in[(i + 1) % count];
        bool aIn = a.view.z >= kNear, bIn = b.view.z >= kNear;
        if (aIn)
            poly[n++] = a;
        if (aIn != bIn && n < 8) {
            double t = (kNear - a.view.z) / (b.view.z - a.view.z);
            ClipVertex c;
            c.view = a.view + (b.view - a.view) * t;
            c.u = a.u + (b.u - a.u) * t;
            c.v = a.v + (b.v - a.v) * t;
            poly[n++] = c;
        }
    }
    if (n < 3)
        return;

    struct Screen {
        double x, y, iz, uz, vz;
    } s[8];
    for (int i = 0; i < n; i++) {
        double iz = 1 / poly[i].view.z;
        s[i].x = _w / 2.0 + _focal * poly[i].view.x * iz;
        s[i].y = _h / 2.0 - _focal * poly[i].view.y * iz;
        s[i].iz = iz;
        s[i].uz = poly[i].u * iz;
        s[i].vz = poly[i].v * iz;
    }
    uint32_t flat = shadeColour(colour, shade);
    for (int f = 1; f + 1 < n; f++) {
        const Screen &a = s[0], &b = s[f], &c = s[f + 1];
        double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-6)
            continue;
        int x0 = std::max(0, (int)std::floor(std::min(a.x, std::min(b.x, c.x))));
        int x1 = std::min(_w - 1, (int)std::ceil(std::max(a.x, std::max(b.x, c.x))));
        int y0 = std::max(0, (int)std::floor(std::min(a.y, std::min(b.y, c.y))));
        int y1 = std::min(_h - 1, (int)std::ceil(std::max(a.y, std::max(b.y, c.y))));
        if (x0 > x1 || y0 > y1)
            continue;
        double inv = 1 / area;
        for (int y = y0; y <= y1; y++) {
            double py = y + 0.5;
            uint32_t *row = (uint32_t *)_target->getBasePtr(_ox, _oy + y);
            float *depth = &_depth[(size_t)y * _w];
            for (int x = x0; x <= x1; x++) {
                double px = x + 0.5;
                double w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) * inv;
                double w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) * inv;
                double w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0)
                    continue;
                double iz = w0 * a.iz + w1 * b.iz + w2 * c.iz;
                float z = (float)(1 / iz);
                if (z >= depth[x])
                    continue;
                uint32_t pixel = flat;
                if (texture) {
                    double u = (w0 * a.uz + w1 * b.uz + w2 * c.uz) / iz, v = (w0 * a.vz + w1 * b.vz + w2 * c.vz) / iz;
                    int tx = std::max(0, std::min(texture->w - 1, (int)(u * texture->w)));
                    int ty = std::max(0, std::min(texture->h - 1, (int)(v * texture->h)));
                    uint32_t t = texture->get(tx, ty);
                    if (keyBlack && (t & 0xFFFFFF) == 0)
                        continue;
                    pixel = shadeColour(t, shade);
                }
                if (blend) {
                    // Half and half, behind what is nearer, leaving the depth.
                    row[x] = 0xFF000000u | (((row[x] & 0xFEFEFE) >> 1) + ((pixel & 0xFEFEFE) >> 1));
                    continue;
                }
                depth[x] = z;
                row[x] = pixel;
            }
        }
    }
}

void CombatRenderer::drawBillboard(const Vec3 &world, double size, int w, int h,
                                   const std::function<uint32_t(int, int)> &pixel, bool additive) {
    double sx, sy, depth;
    if (!_target || !project(world, sx, sy, depth))
        return;
    double pixels = size * _focal / depth;
    int dw = std::max(1, (int)pixels), dh = std::max(1, (int)(pixels * h / std::max(1, w)));
    int x0 = (int)(sx - dw / 2.0), y0 = (int)(sy - dh / 2.0);
    for (int y = std::max(0, y0); y < std::min(_h, y0 + dh); y++) {
        uint32_t *row = (uint32_t *)_target->getBasePtr(_ox, _oy + y);
        for (int x = std::max(0, x0); x < std::min(_w, x0 + dw); x++) {
            if (depth > _depth[(size_t)y * _w + x])
                continue;
            uint32_t c = pixel((x - x0) * w / dw, (y - y0) * h / dh);
            if (!(c >> 24))
                continue;
            row[x] = additive ? addColour(row[x], c) : c;
        }
    }
}

void CombatRenderer::drawLine(const Vec3 &a, const Vec3 &b, uint32_t colour, int thickness) {
    // Clip the line to the near plane, then step along it.
    Vec3 va = _camera.inverse(a - _cameraPos), vb = _camera.inverse(b - _cameraPos);
    if (va.z < kNear && vb.z < kNear)
        return;
    if (va.z < kNear)
        va = va + (vb - va) * ((kNear - va.z) / (vb.z - va.z));
    if (vb.z < kNear)
        vb = vb + (va - vb) * ((kNear - vb.z) / (va.z - vb.z));
    double ax = _w / 2.0 + _focal * va.x / va.z, ay = _h / 2.0 - _focal * va.y / va.z;
    double bx = _w / 2.0 + _focal * vb.x / vb.z, by = _h / 2.0 - _focal * vb.y / vb.z;
    int steps = (int)std::min(4000.0, std::max(std::fabs(bx - ax), std::fabs(by - ay))) + 1;
    for (int i = 0; i <= steps; i++) {
        double t = (double)i / steps;
        int x = (int)(ax + (bx - ax) * t), y = (int)(ay + (by - ay) * t);
        for (int k = 0; k < thickness; k++)
            plot(x, y + k, colour);
    }
}

} // namespace reluva::stgenerations
