// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Drive page: a car driven round a city, as a first
// cruise mode. The car is Reluva's simulation (car.cpp) of the game's
// tuning; the cameras follow the game's camera files.

#include "engines/age/viewers.h"

#include "common/font.h"
#include "engines/age/city_collision.h"
#include "engines/age/tune.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kFar = 4000;
constexpr float kPi = 3.14159265358979f;
constexpr float kMph = 2.23694f; // miles an hour per metre a second

enum : uint32_t { kDriveThrottle = 1, kDriveBrake = 2, kDriveLeft = 4, kDriveRight = 8, kDriveHandbrake = 16 };

std::string textOf(const std::vector<uint8_t>& data) { return std::string(data.begin(), data.end()); }

std::string iniValue(const std::string& text, const std::string& key) {
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

gfx::Mat4 matrixOf(const Mat3& r, const Vec3& p) {
    return {r.x[0], r.x[1], r.x[2], 0, r.y[0], r.y[1], r.y[2], 0, r.z[0], r.z[1], r.z[2], 0, p[0], p[1], p[2], 1};
}

} // namespace

DriveViewer::DriveViewer(ViewerContext& ctx) : Page(ctx), cities_(findCities(ctx.files)) {
    // The cars the menus offer (tune/*.info), if their simulation and model are there.
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("tune/", 0) != 0 || key.size() < 5 || key.compare(key.size() - 5, 5, ".info") != 0)
            continue;
        std::vector<uint8_t> data;
        if (ctx.files.read(n, data))
            continue;
        const std::string text = textOf(data);
        CarInfo car{iniValue(text, "BaseName"), iniValue(text, "Description"), {}};
        const std::string colors = iniValue(text, "Colors");
        for (size_t at = 0; at <= colors.size();) {
            const size_t bar = std::min(colors.find('|', at), colors.size());
            car.colors.push_back(colors.substr(at, bar - at));
            at = bar + 1;
        }
        if (!car.base.empty() && ctx.files.has("tune/vehicle/" + car.base + ".vehCarSim") &&
            ctx.files.has("geometry/" + car.base + ".pkg"))
            cars_.push_back(car);
    }
}

DriveViewer::~DriveViewer() { deactivate(); }

void DriveViewer::deactivate() {
    engine_.stop();
    unloadCar();
    scene_.reset();
    renderer_ = nullptr;
}

void DriveViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Drive page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    scene_ = std::make_unique<CityScene>(*renderer_, ctx_.files);
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    keys_ = 0;
    lastMs_ = 0;
    loadCity(city_);
}

void DriveViewer::loadCity(int index) {
    city_ = index;
    unloadCar();
    error_ = scene_->load(cities_[size_t(city_)].map).message;
    scene_->setLighting(lighting_);
    boundsUsed_ = buildCityCollision(ctx_.files, scene_->psdl(), scene_->mesh(), scene_->instances(), world_);
    loadCar(car_);
}

void DriveViewer::unloadCar() {
    if (!renderer_)
        return;
    destroyModelMesh(*renderer_, body_);
    for (ModelMesh& w : wheels_)
        destroyModelMesh(*renderer_, w);
}

void DriveViewer::loadCar(int index) {
    unloadCar();
    car_ = index;
    const CarInfo& car = cars_[size_t(car_)];
    setTitle(car.description + " in " + cities_[size_t(city_)].name);
    if (Error err = loadCarSetup(ctx_.files, car.base, setup_)) {
        error_ = err.message;
        return;
    }
    // The body without its wheels, which are drawn where the simulation
    // has them.
    const std::string file = "geometry/" + car.base + ".pkg";
    std::vector<uint8_t> data;
    Pkg pkg;
    if (!ctx_.files.read(file, data) && !readPkg(data, pkg)) {
        if (pkg.paintJobs)
            paintJob_ %= pkg.paintJobs;
        auto isWheel = [](const std::string& part) { return part.rfind("WHL", 0) == 0; };
        buildModelMesh(*renderer_, scene_->textures(), ctx_.files, file, pkg, "H", paintJob_, false, body_,
                       [&](const std::string& part) { return !isWheel(part); });
        for (int i = 0; i < 4; ++i) {
            const std::string name = "WHL" + std::to_string(i);
            buildModelMesh(*renderer_, scene_->textures(), ctx_.files, file, pkg, "H", paintJob_, false,
                           wheels_[size_t(i)], [&](const std::string& part) { return part == name; }, false);
        }
    }
    engine_.stop();
    if (ctx_.mixer)
        if (Error err = engine_.start(*ctx_.mixer, ctx_.files, car.base))
            error_ = err.message;
    loadCameras();
    resetCar();
}

void DriveViewer::loadCameras() {
    cameras_.clear();
    const std::string& car = cars_[size_t(car_)].base;
    for (const char* which : {"_near.camTrackCS", "_far.camTrackCS", ".camPovCS"}) {
        std::vector<uint8_t> data;
        TuneNode root;
        if (ctx_.files.read("tune/camera/" + car + which, data) || parseTune(textOf(data), root) ||
            root.children.empty())
            continue;
        const TuneNode& n = root.children.front();
        Camera c;
        c.inside = std::string(which) == ".camPovCS";
        for (size_t k = 0; k < 3; ++k) {
            c.offset[k] = n.number("Offset", k, c.offset[k]);
            c.lookAt[k] = n.number("TrackTo", k, c.lookAt[k]);
        }
        c.minDistance = n.number("MinDist", 0, c.minDistance);
        c.maxDistance = std::max(c.minDistance, n.number("MaxDist", 0, c.maxDistance));
        c.fov = n.number("CameraFOV", 0, c.fov);
        cameras_.push_back(c);
    }
    if (cameras_.empty())
        cameras_.push_back({});
    camera_ = std::min(camera_, int(cameras_.size()) - 1);
    cameraReset_ = true;
}

void DriveViewer::resetCar() {
    // On the road nearest the middle, a little above it.
    sim_.reset(setup_, scene_->start + Vec3{0, 0.5f, 0}, scene_->startYaw);
    cameraReset_ = true;
}

void DriveViewer::handleEvent(const Event& ev) {
    if (!renderer_ || cars_.empty())
        return;
    auto driveKey = [](uint32_t k) -> uint32_t {
        switch (k) {
        case key::Up:
        case 'w': return kDriveThrottle;
        case key::Down:
        case 's': return kDriveBrake;
        case key::Left:
        case 'a': return kDriveLeft;
        case key::Right:
        case 'd': return kDriveRight;
        case ' ': return kDriveHandbrake;
        default: return 0;
        }
    };
    if (ev.type == EventType::KeyDown) {
        if (const uint32_t d = driveKey(ev.key)) {
            keys_ |= d;
            return;
        }
        const int cars = int(cars_.size()), cities = int(cities_.size());
        if (ev.key == key::PageUp || ev.key == key::PageDown) {
            paintJob_ = 0;
            loadCar((car_ + (ev.key == key::PageDown ? 1 : cars - 1)) % cars);
        } else if (ev.key == key::Home || ev.key == key::End) {
            loadCity((city_ + (ev.key == key::End ? 1 : cities - 1)) % cities);
        } else if (ev.key == 'p') {
            ++paintJob_;
            loadCar(car_);
        } else if (ev.key == 'c') {
            camera_ = (camera_ + 1) % int(cameras_.size());
            cameraReset_ = true;
        } else if (ev.key == 't') {
            lighting_ = (lighting_ + 1) % 16;
            scene_->setLighting(lighting_);
        } else if (ev.key == 'r') {
            resetCar();
        } else if (ev.key == 'i') {
            showInfo_ = !showInfo_;
        }
    } else if (ev.type == EventType::KeyUp) {
        keys_ &= ~driveKey(ev.key);
    }
}

void DriveViewer::drawCar() {
    const gfx::Mat4 carMatrix = matrixOf(sim_.orientation, sim_.position);
    const SceneLight& light = scene_->light();
    for (bool translucent : {false, true}) {
        drawModelMesh(*renderer_, body_, carMatrix, light, translucent);
        for (int i = 0; i < 4; ++i) {
            const CarSim::Wheel& w = sim_.wheels[size_t(i)];
            const AxleSetup& axle = i < 2 ? setup_.front : setup_.back;
            const Vec3 at = setup_.wheels[size_t(i)] + Vec3{0, w.compression - axle.suspensionExtent, 0};
            // Steered about the vertical, rolling about the axle.
            const Mat3 turn = Mat3::rotationY(w.steer) * Mat3::rotation({1, 0, 0}, -w.spin);
            drawModelMesh(*renderer_, wheels_[size_t(i)], gfx::multiply(carMatrix, matrixOf(turn, at)), light,
                          translucent);
        }
    }
}

void DriveViewer::drawHud() {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    const int lh = font::kLineHeight;
    const CarInfo& car = cars_[size_t(car_)];
    int y = 4;
    auto line = [&](const std::string& s, Color c) {
        font::drawText(overlay_, 4, y, s, c);
        y += lh;
    };
    std::string color = paintJob_ < car.colors.size() ? car.colors[paintJob_] : std::to_string(paintJob_ + 1);
    line(car.description + " (" + color + ", P)  in " + cities_[size_t(city_)].name + ", " +
             scene_->lightingName(),
         {150, 200, 255});
    if (!error_.empty())
        line(error_, {255, 120, 120});
    if (showInfo_) {
        const char* const cameraNames[] = {"near", "far", "in the car"};
        line(std::string("camera: ") + cameraNames[std::min(camera_, 2)] +
                 " (C); Up/Down, Left/Right, Space; PageUp/Down car, Home/End city, T time, R reset",
             {230, 230, 230});
        char buf[160];
        std::snprintf(buf, sizeof buf, "%zu collision triangles (%zu placed models' bounds); at %.0f %.0f %.0f",
                      world_.size(), boundsUsed_, sim_.position[0], sim_.position[1], sim_.position[2]);
        line(buf, {230, 230, 230});
    }

    // Speed, gear and revs, bottom right.
    char speed[16];
    std::snprintf(speed, sizeof speed, "%3.0f", std::fabs(sim_.forwardSpeed()) * kMph);
    const int scale = 4;
    const int right = kFrameW - 8;
    const int speedX = right - 3 * font::kAdvance * scale - 40;
    font::drawText(overlay_, speedX, kFrameH - 70, speed, {255, 255, 255}, scale);
    font::drawText(overlay_, right - 30, kFrameH - 40, "MPH", {200, 200, 200});
    const std::string gear = sim_.gear < 0 ? "R" : std::to_string(sim_.gear);
    font::drawText(overlay_, right - 24, kFrameH - 70, gear, {255, 210, 80}, 2);
    // The revs as a bar, red past the optimum.
    const float revs = std::clamp(sim_.rpm / setup_.maxRpm, 0.0f, 1.0f);
    const int barX = speedX, barY = kFrameH - 22, barW = right - speedX, barH = 8;
    for (int yy = barY; yy < barY + barH; ++yy) {
        uint8_t* p = overlay_.row(yy) + barX * 4;
        for (int x = 0; x < barW; ++x, p += 4) {
            const bool lit = x < int(revs * float(barW));
            const bool red = float(x) / float(barW) > setup_.optRpm / setup_.maxRpm;
            p[0] = lit ? 255 : 40;
            p[1] = lit ? (red ? 60 : 220) : 40;
            p[2] = lit ? (red ? 40 : 80) : 40;
            p[3] = 200;
        }
    }
}

void DriveViewer::update(uint64_t nowMs) {
    if (!renderer_ || cars_.empty())
        return;
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000) : 0;
    lastMs_ = nowMs;

    CarControls controls;
    controls.throttle = keys_ & kDriveThrottle ? 1.0f : 0.0f;
    controls.brake = keys_ & kDriveBrake ? 1.0f : 0.0f;
    controls.steer = (keys_ & kDriveRight ? 1.0f : 0.0f) - (keys_ & kDriveLeft ? 1.0f : 0.0f);
    controls.handbrake = keys_ & kDriveHandbrake;
    sim_.step(controls, world_, dt);
    // Fallen out of the city: back to the start.
    if (sim_.position[1] < scene_->psdl().min[1] - 50)
        resetCar();
    engine_.update(sim_.rpm, sim_.gear > 0 ? controls.throttle : controls.brake);

    // The camera: in the car, or following it, turned only with its heading.
    const Camera& cam = cameras_[size_t(camera_)];
    Vec3 eye, target, up{0, 1, 0};
    if (cam.inside) {
        eye = sim_.position + sim_.orientation * cam.offset;
        target = eye - sim_.orientation.z;
        up = sim_.orientation.y;
    } else {
        Vec3 ahead = -sim_.orientation.z;
        ahead[1] = 0;
        ahead = length(ahead) > 0.1f ? normalized(ahead) : Vec3{0, 0, -1};
        const Mat3 heading{cross(ahead, {0, 1, 0}), {0, 1, 0}, -ahead};
        target = sim_.position + heading * cam.lookAt;
        const Vec3 wanted = sim_.position + heading * cam.offset;
        if (cameraReset_)
            cameraPos_ = wanted;
        cameraPos_ += (wanted - cameraPos_) * std::min(1.0f, dt * 6);
        // Within the camera's distances of the car, horizontally.
        Vec3 flat = cameraPos_ - target;
        const float height = flat[1];
        flat[1] = 0;
        const float d = length(flat);
        if (d > 1e-3f) {
            flat = flat * (std::clamp(d, cam.minDistance, cam.maxDistance) / d);
            cameraPos_ = target + flat + Vec3{0, height, 0};
        }
        eye = cameraPos_;
    }
    cameraReset_ = false;
    const Vec3 forward = normalized(target - eye);
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(cam.fov * kPi / 180, float(kFrameW) / float(kFrameH), 0.1f, kFar),
                      gfx::lookAlong(eye, forward, up));
    renderer_->beginFrame(viewProj, eye, {150, 170, 200});
    scene_->draw(viewProj, eye, true);
    if (!cam.inside)
        drawCar();
    drawHud();
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::age
