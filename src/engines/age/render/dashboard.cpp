// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/render/dashboard.h"

#include "engines/age/render/car_actor.h"
#include "engines/age/formats/tune.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kPi = 3.14159265358979f;

// The matrix placing a part: about `pivot` (in the part's plane) by
// `angle` (anticlockwise as seen), then at `at` in the camera's space,
// then by the camera into the world.
gfx::Mat4 placed(const Mat3& camera, const Vec3& eye, const Vec3& at, const Vec3& pivot, float angle, float widen) {
    const float c = std::cos(angle), s = std::sin(angle);
    // Local: p' = R (p - pivot) + pivot + at, stretched across by `widen`;
    // world: camera * p' + eye.
    const Mat3 turn{{c, s, 0}, {-s, c, 0}, {0, 0, 1}};
    const Mat3 wide{camera.x * widen, camera.y, camera.z};
    return matrixOf(wide * turn, eye + wide * (pivot + at - turn * pivot));
}

} // namespace

Error loadDashSetup(const SearchSet& files, const std::string& car, DashSetup& out) {
    out = DashSetup{};
    std::vector<uint8_t> data;
    // The files are named .asnode and .asNode.
    if (files.read("tune/" + car + "_dash.asnode", data) && files.read("tune/" + car + "_dash.asNode", data))
        return Error::fail("no dashboard settings for " + car);
    TuneNode root;
    if (Error err = parseTune(std::string(data.begin(), data.end()), root))
        return err;
    if (root.children.empty())
        return Error::fail("empty dashboard settings");
    const TuneNode& n = root.children.front();
    auto vec = [&](const char* name) { return Vec3{n.number(name, 0), n.number(name, 1), n.number(name, 2)}; };
    out.dashPos = vec("DashPos");
    out.roofPos = vec("RoofPos");
    out.wheelPos = vec("WheelPos");
    out.speedPivot = vec("SpeedPivotOffset");
    out.tachPivot = vec("TachPivotOffset");
    out.damagePivot = vec("DmgPivotOffset");
    out.wheelPivot = vec("WheelPivotOffset");
    out.gearPivot = vec("GearPivotOffset");
    out.wheelFact = n.number("WheelFact", 0, 0.9f);
    out.rpmRotMin = n.number("RPMRotMin");
    out.rpmRotMax = n.number("RPMRotMax", 0, 3);
    out.speedRotMin = n.number("SpeedRotMin");
    out.speedRotMax = n.number("SpeedRotMax", 0, 3);
    out.damageRotMin = n.number("DamageRotMin");
    out.damageRotMax = n.number("DamageRotMax", 0, 3);
    return Error::success();
}

bool Dashboard::load(const SearchSet& files, ModelCache& models, TextureCache& textures, const std::string& car) {
    _ok = false;
    _model = car + "_dash";
    if (!files.has("geometry/" + _model + ".pkg") || loadDashSetup(files, car, _setup))
        return false;
    auto mtx = [&](const char* part, Mtx& out) {
        std::vector<uint8_t> data;
        out = Mtx{};
        if (!files.read("geometry/" + _model + "_" + part + ".mtx", data))
            readMtx(data, out);
    };
    mtx("speed_needle", _speed);
    mtx("tach_needle", _tach);
    mtx("damage_needle", _damage);
    mtx("wheel", _wheel);
    mtx("gear_indicator", _gear);
    bool translucent;
    _drive = textures.get("d", translucent);
    _neutral = textures.get("n", translucent);
    _reverse = textures.get("r", translucent);
    _ok = models.part(_model, "dash").mesh != 0;
    return _ok;
}

void Dashboard::draw(gfx::Renderer3D& renderer, ModelCache& models, const CarSim& sim, float damage, const Vec3& eye,
                     const Vec3& forward, const Vec3& up, float aspect, const SceneLight& light) const {
    if (!_ok)
        return;
    const Vec3 back = normalized(forward * -1.0f);
    const Vec3 right = normalized(cross(up, back));
    const Mat3 camera{right, cross(back, right), back};
    // Lit evenly: it's inside the car.
    SceneLight inside = light;
    inside.directed = {0, 0, 0};
    inside.fill = {};
    for (size_t c = 0; c < 3; ++c)
        inside.ambient[c] = std::clamp(light.ambient[c] + light.directed[c] * 0.5f, 0.35f, 1.0f);
    inside.fogEnd = 0;
    renderer.clearDepth();
    // Made for 4:3: stretched across to fill a wider view.
    const float widen = std::max(1.0f, aspect / (4.0f / 3));
    auto part = [&](const char* name, const Vec3& at, const Vec3& pivot, float angle, gfx::TextureId texture = 0) {
        ModelMesh m = models.part(_model, name);
        if (texture)
            for (ModelDraw& d : m.draws)
                d.texture = texture;
        const gfx::Mat4 where = placed(camera, eye, at, pivot, angle, widen);
        drawModelMesh(renderer, m, where, inside, false);
        drawModelMesh(renderer, m, where, inside, true);
    };
    const CarSetup& s = sim.setup();
    // The needles: speed over the top gear's speed, revs over the most,
    // damage; each from its least turn to its most (Reluva's reading).
    const float speed = std::clamp(std::fabs(sim.forwardSpeed()) / std::max(1.0f, s.highSpeed), 0.0f, 1.0f);
    const float revs = std::clamp(sim.rpm / std::max(1.0f, s.maxRpm), 0.0f, 1.0f);
    const float hurt = std::clamp(damage, 0.0f, 1.0f);
    // The steering wheel: a quarter turn at full lock, times WheelFact.
    const float lock = std::max(0.01f, s.front.steeringLimit);
    const float steer = std::clamp(sim.wheels[0].steer / lock, -1.0f, 1.0f) * kPi / 2 * _setup.wheelFact;
    part("roof", _setup.roofPos, {}, 0);
    part("dash", _setup.dashPos, {}, 0);
    part("speed_needle", _setup.dashPos, _speed.pivot + _setup.speedPivot,
         _setup.speedRotMin + speed * (_setup.speedRotMax - _setup.speedRotMin));
    part("tach_needle", _setup.dashPos, _tach.pivot + _setup.tachPivot,
         _setup.rpmRotMin + revs * (_setup.rpmRotMax - _setup.rpmRotMin));
    part("damage_needle", _setup.dashPos, _damage.pivot + _setup.damagePivot,
         _setup.damageRotMin + hurt * (_setup.damageRotMax - _setup.damageRotMin));
    // The gear: texture/r backing up, n standing in first, d driving.
    const bool standing = sim.gear == 1 && std::fabs(sim.forwardSpeed()) < 0.5f;
    part("gear_indicator", _setup.dashPos, _gear.pivot + _setup.gearPivot, 0,
         sim.gear < 0 ? _reverse
         : standing   ? _neutral
                      : _drive);
    part("wheel", _setup.dashPos + _setup.wheelPos, _wheel.pivot + _setup.wheelPivot, steer);
}

} // namespace reluva::age
