// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/car_actor.h"

#include "engines/age/tune.h"

#include <algorithm>

namespace reluva::age {

gfx::Mat4 matrixOf(const Mat3& r, const Vec3& p) {
    return {r.x[0], r.x[1], r.x[2], 0, r.y[0], r.y[1], r.y[2], 0, r.z[0], r.z[1], r.z[2], 0, p[0], p[1], p[2], 1};
}

Error CarActor::load(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files,
                     const std::string& car, uint32_t paintJob) {
    unload();
    renderer_ = &renderer;
    if (Error err = loadCarSetup(files, car, setup))
        return err;
    // The body without its wheels, which are drawn where the simulation
    // has them.
    const std::string file = "geometry/" + car + ".pkg";
    std::vector<uint8_t> data;
    Pkg pkg;
    if (Error err = files.read(file, data))
        return err;
    if (Error err = readPkg(data, pkg))
        return Error::fail(file + ": " + err.message);
    paintJobs = std::max(1u, pkg.paintJobs);
    paintJob %= paintJobs;
    auto isWheel = [](const std::string& part) { return part.rfind("WHL", 0) == 0; };
    buildModelMesh(renderer, textures, files, file, pkg, "H", paintJob, false, body_,
                   [&](const std::string& part) { return !isWheel(part); });
    for (int i = 0; i < 4; ++i) {
        const std::string name = "WHL" + std::to_string(i);
        buildModelMesh(renderer, textures, files, file, pkg, "H", paintJob, false, wheels_[size_t(i)],
                       [&](const std::string& part) { return part == name; }, false);
    }
    return Error::success();
}

void CarActor::unload() {
    if (!renderer_)
        return;
    destroyModelMesh(*renderer_, body_);
    for (ModelMesh& w : wheels_)
        destroyModelMesh(*renderer_, w);
    renderer_ = nullptr;
}

void CarActor::draw(const SceneLight& light) const {
    if (!renderer_)
        return;
    const gfx::Mat4 carMatrix = matrixOf(sim.orientation, sim.position);
    for (bool translucent : {false, true}) {
        drawModelMesh(*renderer_, body_, carMatrix, light, translucent);
        for (int i = 0; i < 4; ++i) {
            const CarSim::Wheel& w = sim.wheels[size_t(i)];
            const AxleSetup& axle = i < 2 ? setup.front : setup.back;
            const Vec3 at = setup.wheels[size_t(i)] + Vec3{0, w.compression - axle.suspensionExtent, 0};
            // Steered about the vertical, rolling about the axle.
            const Mat3 turn = Mat3::rotationY(w.steer) * Mat3::rotation({1, 0, 0}, -w.spin);
            drawModelMesh(*renderer_, wheels_[size_t(i)], gfx::multiply(carMatrix, matrixOf(turn, at)), light,
                          translucent);
        }
    }
}

std::vector<CarCamera> loadCarCameras(const SearchSet& files, const std::string& car) {
    std::vector<CarCamera> cameras;
    for (const char* which : {"_near.camTrackCS", "_far.camTrackCS", ".camPovCS"}) {
        std::vector<uint8_t> data;
        TuneNode root;
        if (files.read("tune/camera/" + car + which, data) ||
            parseTune(std::string(data.begin(), data.end()), root) || root.children.empty())
            continue;
        const TuneNode& n = root.children.front();
        CarCamera c;
        c.inside = std::string(which) == ".camPovCS";
        for (size_t k = 0; k < 3; ++k) {
            c.offset[k] = n.number("Offset", k, c.offset[k]);
            c.lookAt[k] = n.number("TrackTo", k, c.lookAt[k]);
        }
        c.minDistance = n.number("MinDist", 0, c.minDistance);
        c.maxDistance = std::max(c.minDistance, n.number("MaxDist", 0, c.maxDistance));
        c.fov = n.number("CameraFOV", 0, c.fov);
        cameras.push_back(c);
    }
    if (cameras.empty())
        cameras.push_back({});
    return cameras;
}

CameraView updateCarCamera(const CarCamera& cam, const CarSim& sim, float dt, Vec3& follow, bool reset) {
    CameraView v;
    if (cam.inside) {
        v.eye = sim.position + sim.orientation * cam.offset;
        v.target = v.eye - sim.orientation.z;
        v.up = sim.orientation.y;
        return v;
    }
    // Following, turned only with the car's heading.
    Vec3 ahead = -sim.orientation.z;
    ahead[1] = 0;
    ahead = length(ahead) > 0.1f ? normalized(ahead) : Vec3{0, 0, -1};
    const Mat3 heading{cross(ahead, {0, 1, 0}), {0, 1, 0}, -ahead};
    v.target = sim.position + heading * cam.lookAt;
    const Vec3 wanted = sim.position + heading * cam.offset;
    if (reset)
        follow = wanted;
    follow += (wanted - follow) * std::min(1.0f, dt * 6);
    // Within the camera's distances of the car, horizontally.
    Vec3 flat = follow - v.target;
    const float height = flat[1];
    flat[1] = 0;
    const float d = length(flat);
    if (d > 1e-3f) {
        flat = flat * (std::clamp(d, cam.minDistance, cam.maxDistance) / d);
        follow = v.target + flat + Vec3{0, height, 0};
    }
    v.eye = follow;
    return v;
}

} // namespace reluva::age
