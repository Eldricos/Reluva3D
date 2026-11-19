// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cmath>
#include <functional>
#include <vector>

#include "engines/stgenerations/canvas.h"
#include "engines/stgenerations/formats/combatdata.h"

namespace reluva::stgenerations {

struct Vec3 {
    double x = 0;
    double y = 0;
    double z = 0;
    Vec3() {}
    Vec3(double x, double y, double z) : x(x), y(y), z(z) {}
    Vec3 operator+(const Vec3 &o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3 &o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
    double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3 &o) const { return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x); }
    double length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const {
        double l = length();
        return l > 0 ? *this * (1 / l) : Vec3(0, 0, 1);
    }
};

// An orientation: the columns are the object's right (x), up (y) and forward (z) axes.
struct Mat3 {
    Vec3 right = Vec3(1, 0, 0);
    Vec3 up = Vec3(0, 1, 0);
    Vec3 forward = Vec3(0, 0, 1);

    Vec3 apply(const Vec3 &v) const { return right * v.x + up * v.y + forward * v.z; }
    // World to local.
    Vec3 inverse(const Vec3 &v) const { return Vec3(right.dot(v), up.dot(v), forward.dot(v)); }
    // Turn about the object's own axes (radians): yaw about up, pitch about right, roll about forward.
    void rotate(double yaw, double pitch, double roll);
    // Straighten the axes after many small turns.
    void orthonormalize();
    // From yaw (about the world's up, 0 facing +z) and pitch.
    static Mat3 fromYawPitch(double yaw, double pitch);
    // Looking along forward, with up as near the world's as can be.
    static Mat3 lookAlong(const Vec3 &forward, const Vec3 &upHint = Vec3(0, 1, 0));
};

// The combat's 3D view: a z-buffered rasteriser of textured triangles
// with the DRE library's lighting (ambient 0.55 and one light from
// (0, -1, -1)), drawn in true colour.
class CombatRenderer {
public:
    typedef std::function<const CombatImage *(const std::string &name)> TextureLookup;

    void setViewport(int w, int h, double fovDegrees = 60);
    int getWidth() const { return _w; }
    int getHeight() const { return _h; }
    void setCamera(const Vec3 &position, const Mat3 &orientation);
    const Vec3 &getCameraPosition() const { return _cameraPos; }
    const Mat3 &getCameraOrientation() const { return _camera; }

    // Clear the depth buffer; the picture is the caller's.
    void begin(Canvas &target, int x, int y);
    void drawModel(const Model3D &model, const std::vector<CombatMaterial> &materials, const TextureLookup &textures,
                   const Vec3 &position, const Mat3 &orientation, double scale = 1.0);
    // Project a world point to the view; false if it's behind the camera.
    // depth is the distance along the view.
    bool project(const Vec3 &world, double &sx, double &sy, double &depth) const;
    // A picture centred on a world point, sized size units across, behind nearer things.
    void drawBillboard(const Vec3 &world, double size, int w, int h, const std::function<uint32_t(int, int)> &pixel,
                       bool additive);
    // A line between two world points (the phaser beams).
    void drawLine(const Vec3 &a, const Vec3 &b, uint32_t colour, int thickness = 1);
    void plot(int x, int y, uint32_t colour);
    double getFocal() const { return _focal; }

private:
    struct ClipVertex {
        Vec3 view;
        double u;
        double v;
    };
    // blend: see-through, mixed half and half with what is behind.
    void drawTriangle(const ClipVertex *v, int count, const CombatImage *texture, uint32_t colour, double shade,
                      bool keyBlack, bool blend = false);

    int _w = 0;
    int _h = 0;
    double _focal = 1;
    Vec3 _cameraPos;
    Mat3 _camera;
    Canvas *_target = nullptr;
    int _ox = 0;
    int _oy = 0;
    std::vector<float> _depth;
};

} // namespace reluva::stgenerations
