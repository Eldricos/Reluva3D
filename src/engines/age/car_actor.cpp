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
        if (!c.inside && n.number("ApproachOn") != 0) {
            c.approachSlow = n.number("MinAppXZPos", 0, c.approachSlow);
            c.approachFast = std::max(c.approachSlow, n.number("MaxAppXZPos", 0, c.approachFast));
            c.slowSpeed = n.number("MinSpeed", 0, c.slowSpeed);
            c.fastSpeed = std::max(c.slowSpeed + 1, n.number("MaxSpeed", 0, c.fastSpeed));
            c.approachRise = n.number("AppInc", 0, c.approachRise);
            c.approachFall = n.number("AppDec", 0, c.approachFall);
            c.approachUp = n.number("AppYPos", 0, c.approachUp);
        }
        c.hardSteer = n.number("MinHardSteer", 0, c.hardSteer);
        c.driftDelay = n.number("DriftDelay", 0, c.driftDelay);
        c.driftRate = n.number("FrontRate", 0, c.driftRate);
        c.hillMin = n.number("HillMin");
        c.hillMax = std::max(c.hillMin, n.number("HillMax"));
        c.hillLerp = std::clamp(n.number("HillLerp"), 0.0f, 1.0f);
        c.reverse = n.number("ReverseOn") != 0;
        c.reverseDelay = n.number("RevDelay", 0, c.reverseDelay);
        c.reverseOn = n.number("RevOnApp", 0, c.reverseOn);
        c.reverseOff = n.number("RevOffApp", 0, c.reverseOff);
        cameras.push_back(c);
    }
    if (cameras.empty())
        cameras.push_back({});
    return cameras;
}

CameraView updateCarCamera(const CarCamera& cam, const CarSim& sim, float dt, CameraFollow& follow, bool reset) {
    constexpr float kFrameRate = 30; // HillLerp is a frame's
    CameraView v;
    if (cam.inside) {
        v.eye = sim.position + sim.orientation * cam.offset;
        v.target = v.eye - sim.orientation.z;
        v.up = sim.orientation.y;
        return v;
    }
    // Following, turned only with the car's heading; backing long enough,
    // the other way round.
    const float speed = sim.forwardSpeed();
    follow.backingFor = speed < -1 ? follow.backingFor + dt : 0;
    const bool backwards = cam.reverse && follow.backingFor > cam.reverseDelay;
    if (backwards != follow.backwards && !reset)
        follow.swinging = true;
    follow.backwards = backwards;
    Vec3 ahead = -sim.orientation.z;
    const float rise = ahead[1];
    ahead[1] = 0;
    ahead = length(ahead) > 0.1f ? normalized(ahead) : Vec3{0, 0, -1};
    if (backwards)
        ahead = -ahead;
    const Mat3 heading{cross(ahead, {0, 1, 0}), {0, 1, 0}, -ahead};
    v.target = sim.position + heading * cam.lookAt;
    // Tilted with the hill: up behind the car going down, down going up.
    const float slope = std::clamp(backwards ? -rise : rise, cam.hillMin, cam.hillMax);
    follow.slope =
        reset ? slope : follow.slope + (slope - follow.slope) * std::min(1.0f, cam.hillLerp * kFrameRate * dt);
    Vec3 offset = cam.offset;
    if (follow.slope != 0) {
        const float s = follow.slope, c = std::sqrt(std::max(0.0f, 1 - s * s));
        offset = {offset[0], offset[1] * c - offset[2] * s, offset[2] * c + offset[1] * s};
    }
    const Vec3 wanted = sim.position + heading * offset;
    // How fast it catches up: by the speed, rising and falling at most so
    // fast; drifting, slower; swinging round, its own.
    const float share = std::clamp((std::fabs(speed) - cam.slowSpeed) / (cam.fastSpeed - cam.slowSpeed), 0.0f, 1.0f);
    const float rate = cam.approachSlow + (cam.approachFast - cam.approachSlow) * share;
    const float steer = std::fabs(sim.wheels[0].steer) / std::max(0.01f, sim.setup().front.steeringLimit);
    follow.hardFor = steer >= cam.hardSteer ? follow.hardFor + dt : 0;
    if (reset) {
        follow.position = wanted;
        follow.rate = rate;
        follow.swinging = false;
    }
    follow.rate = std::clamp(rate, follow.rate - cam.approachFall * dt, follow.rate + cam.approachRise * dt);
    float across = follow.rate;
    if (follow.hardFor > cam.driftDelay)
        across *= cam.driftRate;
    if (follow.swinging) {
        across = backwards ? cam.reverseOn : cam.reverseOff;
        follow.swinging = std::hypot(wanted[0] - follow.position[0], wanted[2] - follow.position[2]) > 1;
    }
    // Across, round the car: its bearing and distance from the point it
    // looks at each eased, the distance kept within the camera's.
    const float pull = std::min(1.0f, dt * across);
    const float nowX = follow.position[0] - v.target[0], nowZ = follow.position[2] - v.target[2];
    const float wantX = wanted[0] - v.target[0], wantZ = wanted[2] - v.target[2];
    float bearing = std::atan2(nowX, nowZ);
    float turn = std::atan2(wantX, wantZ) - bearing;
    turn = std::remainder(turn, 6.2831853f);
    bearing += turn * pull;
    float distance = std::hypot(nowX, nowZ);
    distance += (std::hypot(wantX, wantZ) - distance) * pull;
    distance = std::clamp(distance, cam.minDistance, cam.maxDistance);
    follow.position[0] = v.target[0] + std::sin(bearing) * distance;
    follow.position[2] = v.target[2] + std::cos(bearing) * distance;
    follow.position[1] += (wanted[1] - follow.position[1]) * std::min(1.0f, dt * cam.approachUp);
    v.eye = follow.position;
    return v;
}

} // namespace reluva::age
