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
    clean_.clear();
    damaged_.clear();
    for (const ModelDraw& d : body_.draws) {
        bool translucent;
        clean_.push_back(d.texture);
        damaged_.push_back(d.textureName.empty() ? 0 : textures.get(d.textureName + "_dmg", translucent, true));
    }
    for (int i = 0; i < 4; ++i) {
        const std::string name = "WHL" + std::to_string(i);
        buildModelMesh(renderer, textures, files, file, pkg, "H", paintJob, false, wheels_[size_t(i)],
                       [&](const std::string& part) { return part == name; }, false);
    }
    auto lamp = [&](const char* part, ModelMesh& out) {
        buildModelMesh(renderer, textures, files, file, pkg, "", paintJob, true, out,
                       [&](const std::string& p) { return p == part; });
    };
    lamp("HLIGHT", head_);
    lamp("TLIGHT", tail_);
    lamp("BLIGHT", brake_);
    lamp("RLIGHT", reverse_);
    headlights_.clear();
    sirenLamps_.clear();
    for (const PkgMesh& m : pkg.meshes) {
        const std::string part = m.part();
        std::vector<uint8_t> placement;
        Mtx mtx;
        if ((part.rfind("HEADLIGHT", 0) != 0 && part.rfind("SRN", 0) != 0) ||
            files.read("geometry/" + car + "_" + part + ".mtx", placement) || readMtx(placement, mtx))
            continue;
        if (part[0] == 'H') {
            headlights_.push_back(mtx.origin);
        } else if (!m.groups.empty()) {
            const PkgShader* sh = pkg.shader(paintJob, m.groups.front().shader);
            sirenLamps_.push_back({mtx.origin, sh ? std::array<float, 3>{sh->diffuse[0], sh->diffuse[1], sh->diffuse[2]}
                                                  : std::array<float, 3>{1, 1, 1}});
        }
    }
    for (int i = 0; i < 2; ++i)
        lamp(i == 0 ? "SIREN0" : "SIREN1", sirens_[size_t(i)]);
    return Error::success();
}

void CarActor::unload() {
    if (!renderer_)
        return;
    destroyModelMesh(*renderer_, body_);
    for (ModelMesh& w : wheels_)
        destroyModelMesh(*renderer_, w);
    for (ModelMesh& m : sirens_)
        destroyModelMesh(*renderer_, m);
    for (ModelMesh* m : {&head_, &tail_, &brake_, &reverse_})
        destroyModelMesh(*renderer_, *m);
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
        if (siren > 0)
            drawModelMesh(*renderer_, sirens_[size_t(siren - 1) % 2], carMatrix, light, translucent);
        if (lamps.head) {
            drawModelMesh(*renderer_, head_, carMatrix, light, translucent);
            drawModelMesh(*renderer_, tail_, carMatrix, light, translucent);
        }
        if (lamps.brake) {
            drawModelMesh(*renderer_, brake_, carMatrix, light, translucent);
            drawModelMesh(*renderer_, tail_, carMatrix, light, translucent);
        }
        if (lamps.reverse)
            drawModelMesh(*renderer_, reverse_, carMatrix, light, translucent);
    }
}

void CarActor::dent(const Vec3& at, float radius) {
    for (size_t i = 0; i < body_.draws.size() && i < damaged_.size(); ++i)
        if (damaged_[i] && length(body_.draws[i].center - at) <= radius)
            body_.draws[i].texture = damaged_[i];
}

void CarActor::mend() {
    for (size_t i = 0; i < body_.draws.size() && i < clean_.size(); ++i)
        body_.draws[i].texture = clean_[i];
}

void CarActor::drawFlares(const FlareDrawer& flares, const Vec3& eye) const {
    if (!renderer_)
        return;
    const Vec3 toEye = normalized(eye - sim.position);
    // Headlights seen from in front of the car.
    const float facing = dot(-sim.orientation.z, toEye);
    if (lamps.head && facing > 0)
        for (const Vec3& h : headlights_)
            flares.draw(sim.position + sim.orientation * h, 1.6f, {facing, facing * 0.95f, facing * 0.8f}, eye);
    // The sirens lit (the first half, then the second: blue, then red),
    // from all round.
    if (siren > 0)
        for (size_t i = 0; i < sirenLamps_.size(); ++i)
            if ((i < sirenLamps_.size() / 2) == (siren == 1))
                flares.draw(sim.position + sim.orientation * sirenLamps_[i].first, 2.0f, sirenLamps_[i].second, eye);
}

bool CarActor::headlightPool(gfx::DynamicLight& out) const {
    if (!renderer_ || !lamps.head)
        return false;
    out = {sim.position - sim.orientation.z * 10.0f + sim.orientation.y * 1.0f, 13, {0.6f, 0.55f, 0.45f}};
    return true;
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
