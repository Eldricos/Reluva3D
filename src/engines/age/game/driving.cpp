// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The driving shared by the race screen and the data viewer's Drive page:
// the cars' list, the keys and controls, the options put in force, the
// speedometer and the rear-view mirror.

#include "engines/age/ui/screen.h"

#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kMph = 2.23694f; // miles an hour per metre a second

std::string textOf(const std::vector<uint8_t> &data) {
    return std::string(data.begin(), data.end());
}

std::string iniValue(const std::string &text, const std::string &key) {
    size_t at = 0;
    while (at < text.size()) {
        size_t eol = text.find('\n', at);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = text.substr(at, eol - at);
        at = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == '=')
            return line.substr(key.size() + 1);
    }
    return {};
}

} // namespace

std::vector<CarInfo> findCars(const SearchSet &files) {
    std::vector<CarInfo> cars;
    for (const std::string &n : files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("tune/", 0) != 0 || key.size() < 5 || key.compare(key.size() - 5, 5, ".info") != 0)
            continue;
        std::vector<uint8_t> data;
        if (files.read(n, data))
            continue;
        const std::string text = textOf(data);
        CarInfo car{iniValue(text, "BaseName"), iniValue(text, "Description"), {}};
        const std::string colors = iniValue(text, "Colors");
        for (size_t at = 0; at <= colors.size();) {
            const size_t bar = std::min(colors.find('|', at), colors.size());
            car.colors.push_back(colors.substr(at, bar - at));
            at = bar + 1;
        }
        if (!car.base.empty() && files.has("tune/vehicle/" + car.base + ".vehCarSim") &&
            files.has("geometry/" + car.base + ".pkg"))
            cars.push_back(car);
    }
    return cars;
}

Control controlOf(uint32_t k) {
    return currentOptions().controlOf(k);
}

void applyGraphicsOptions(CityScene &scene, CityLife &life) {
    const GameOptions &o = currentOptions();
    static const float kDetail[4] = {0.4f, 0.6f, 0.8f, 1.0f};
    scene.setSkyShown(o.texturedSky);
    scene.setCloudShadows(o.cloudShadows);
    CarActor::g_reflections = o.vehicleReflections;
    scene.setVisibility(o.smartRendering);
    scene.setModelDistance(300 + 600 * o.visibility);
    scene.setModelDetail(kDetail[std::clamp(o.objectDetail, 0, 3)]);
    life.setAmbientGain(o.ambient ? 1.0f : 0.0f);
}

int textureHalvings() {
    return 3 - std::clamp(currentOptions().textureQuality, 0, 3);
}

float pedestrianShare() {
    return currentOptions().pedestrians ? 1.0f : 0.0f;
}

bool headlightPoolsOn() {
    return currentOptions().lightingQuality >= 0.5f;
}

void applyControlOptions(CarSim &sim) {
    sim.autoReverse = currentOptions().autoReverse;
    sim.steerRate = currentOptions().steerRate();
}

uint32_t driveKeyOf(uint32_t k) {
    switch (controlOf(k)) {
    case Control::kThrottle:
        return kDriveThrottle;
    case Control::kBrakes:
        return kDriveBrake;
    case Control::kSteerLeft:
        return kDriveLeft;
    case Control::kSteerRight:
        return kDriveRight;
    case Control::kHandbrake:
        return kDriveHandbrake;
    case Control::kHorn:
        return kDriveHorn;
    case Control::kReverse:
        return kDriveReverse;
    default:
        return 0;
    }
}

CarControls controlsOf(uint32_t keys) {
    CarControls c;
    c.throttle = keys & kDriveThrottle ? 1.0f : 0.0f;
    c.brake = keys & kDriveBrake ? 1.0f : 0.0f;
    c.steer = (keys & kDriveRight ? 1.0f : 0.0f) - (keys & kDriveLeft ? 1.0f : 0.0f);
    c.handbrake = keys & kDriveHandbrake;
    c.reverse = keys & kDriveReverse;
    return c;
}

void drawSpeedometer(Surface &overlay, const CarSim &sim, int right, float damage, const HudPictures &pictures) {
    const int h = overlay.h;
    if (pictures.loaded()) {
        pictures.gauges(overlay, sim, damage);
        return;
    }
    char speed[16];
    std::snprintf(speed, sizeof speed, "%3.0f", std::fabs(sim.forwardSpeed()) * kMph);
    const int scale = 4;
    const int speedX = right - 3 * font::kAdvance * scale - 40;
    font::drawText(overlay, speedX, h - 70, speed, {255, 255, 255}, scale);
    font::drawText(overlay, right - 30, h - 40, "MPH", {200, 200, 200});
    const std::string gear = sim.gear < 0 ? "R" : std::to_string(sim.gear);
    font::drawText(overlay, right - 24, h - 70, gear, {255, 210, 80}, 2);
    // The revs as a bar, red past the optimum.
    const CarSetup &setup = sim.setup();
    const float revs = std::clamp(sim.rpm / setup.maxRpm, 0.0f, 1.0f);
    const int barX = speedX;
    const int barY = h - 22;
    const int barW = right - speedX;
    const int barH = 8;
    for (int yy = barY; yy < barY + barH; ++yy) {
        uint8_t *p = overlay.row(yy) + barX * 4;
        for (int x = 0; x < barW; ++x, p += 4) {
            const bool lit = x < int(revs * float(barW));
            const bool red = float(x) / float(barW) > setup.optRpm / setup.maxRpm;
            p[0] = lit ? 255 : 40;
            p[1] = lit ? (red ? 60 : 220) : 40;
            p[2] = lit ? (red ? 40 : 80) : 40;
            p[3] = 200;
        }
    }
    // The damage, thinner, under it.
    for (int yy = barY + barH + 3; yy < barY + barH + 7 && yy < h; ++yy) {
        uint8_t *p = overlay.row(yy) + barX * 4;
        for (int x = 0; x < barW; ++x, p += 4) {
            const bool lit = x < int(std::clamp(damage, 0.0f, 1.0f) * float(barW));
            p[0] = lit ? 255 : 40;
            p[1] = lit ? 130 : 40;
            p[2] = lit ? 30 : 40;
            p[3] = 200;
        }
    }
}

void drawRearMirror(gfx::Renderer3D &renderer, const CarSim &sim, float farClip,
                    const std::function<void(const gfx::Mat4 &viewProj, const Vec3 &eye)> &world) {
    constexpr int kX = 447;
    constexpr int kY = 0;
    constexpr int kW = 193;
    constexpr int kH = 77;
    constexpr float kFov = 25; // degrees, up and down
    // From the driver's head, looking back level.
    const Vec3 eye = sim.position + sim.orientation * Vec3{0, 1.2f, 0};
    Vec3 back = sim.orientation.z;
    back[1] = 0;
    back = length(back) > 0.1f ? normalized(back) : Vec3{0, 0, 1};
    gfx::Mat4 proj = gfx::perspective(kFov * kPi / 180, float(kW) / float(kH), 0.1f, farClip);
    for (int c = 0; c < 4; ++c)
        proj[size_t(c * 4)] = -proj[size_t(c * 4)]; // left for right
    const gfx::Mat4 viewProj = gfx::multiply(proj, gfx::lookAlong(eye, back, {0, 1, 0}));
    renderer.clearDepth();
    renderer.setViewport(kX, kY, kW, kH);
    renderer.setView(viewProj, eye);
    renderer.setMirrored(true);
    world(viewProj, eye);
    renderer.setMirrored(false);
    renderer.setViewport(0, 0, 0, 0);
    renderer.clearDepth();
}

} // namespace reluva::age
