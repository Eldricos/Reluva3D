// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Drive page: a car driven round a city, as a cruise
// mode. The car is Reluva's simulation (car.cpp) of the game's tuning;
// the cameras follow the game's camera files. Also the driving helpers
// the Race page shares.

#include "engines/age/viewers.h"

#include "common/font.h"
#include "engines/age/city_collision.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kFar = 4000;
constexpr float kPi = 3.14159265358979f;
constexpr float kMph = 2.23694f; // miles an hour per metre a second
constexpr float kTrafficLevels[] = {0, 0.25f, 0.5f, 1}; // none, light, normal, heavy
const char* const kTrafficNames[] = {"no traffic", "light traffic", "normal traffic", "heavy traffic"};
constexpr float kPedsPerTraffic = 0.2f; // pedestrian density for each of traffic

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

} // namespace

std::vector<CarInfo> findCars(const SearchSet& files) {
    std::vector<CarInfo> cars;
    for (const std::string& n : files.list()) {
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

uint32_t driveKeyOf(uint32_t k) {
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
}

CarControls controlsOf(uint32_t keys) {
    CarControls c;
    c.throttle = keys & kDriveThrottle ? 1.0f : 0.0f;
    c.brake = keys & kDriveBrake ? 1.0f : 0.0f;
    c.steer = (keys & kDriveRight ? 1.0f : 0.0f) - (keys & kDriveLeft ? 1.0f : 0.0f);
    c.handbrake = keys & kDriveHandbrake;
    return c;
}

void drawSpeedometer(Surface& overlay, const CarSim& sim) {
    const int w = overlay.w, h = overlay.h;
    char speed[16];
    std::snprintf(speed, sizeof speed, "%3.0f", std::fabs(sim.forwardSpeed()) * kMph);
    const int scale = 4;
    const int right = w - 8;
    const int speedX = right - 3 * font::kAdvance * scale - 40;
    font::drawText(overlay, speedX, h - 70, speed, {255, 255, 255}, scale);
    font::drawText(overlay, right - 30, h - 40, "MPH", {200, 200, 200});
    const std::string gear = sim.gear < 0 ? "R" : std::to_string(sim.gear);
    font::drawText(overlay, right - 24, h - 70, gear, {255, 210, 80}, 2);
    // The revs as a bar, red past the optimum.
    const CarSetup& setup = sim.setup();
    const float revs = std::clamp(sim.rpm / setup.maxRpm, 0.0f, 1.0f);
    const int barX = speedX, barY = h - 22, barW = right - speedX, barH = 8;
    for (int yy = barY; yy < barY + barH; ++yy) {
        uint8_t* p = overlay.row(yy) + barX * 4;
        for (int x = 0; x < barW; ++x, p += 4) {
            const bool lit = x < int(revs * float(barW));
            const bool red = float(x) / float(barW) > setup.optRpm / setup.maxRpm;
            p[0] = lit ? 255 : 40;
            p[1] = lit ? (red ? 60 : 220) : 40;
            p[2] = lit ? (red ? 40 : 80) : 40;
            p[3] = 200;
        }
    }
}

DriveViewer::DriveViewer(ViewerContext& ctx) : Page(ctx), cities_(findCities(ctx.files)), cars_(findCars(ctx.files)) {}

DriveViewer::~DriveViewer() { deactivate(); }

void DriveViewer::deactivate() {
    siren_.stop();
    life_.stop();
    engine_.stop();
    car_.unload();
    flares_.reset();
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
    car_.unload();
    flares_.reset();
    error_ = scene_->load(cities_[size_t(city_)].map).message;
    flares_ = std::make_unique<FlareDrawer>(*renderer_, scene_->textures());
    scene_->setLighting(lighting_);
    boundsUsed_ = buildCityCollision(ctx_.files, scene_->psdl(), scene_->mesh(), scene_->instances(), world_);
    roads_.build(scene_->psdl());
    if (Error err = life_.load(ctx_.files, cities_[size_t(city_)].map); err && error_.empty())
        error_ = err.message;
    loadCar(carIndex_);
    startLife();
}

void DriveViewer::cutBridgeGaps() {
    // What the city has across the drawbridges' gaps goes: the leaves are
    // the road there.
    auto inGap = [&](const Vec3& a, const Vec3& b, const Vec3& c) { return life_.gizmos().inGap(a, b, c); };
    scene_->hide(inGap);
    world_.removeIf(inGap);
}

void DriveViewer::startLife() {
    // The city's settings and cruising's (race/<city>/roam.aimap: the
    // police).
    if (!life_.loaded())
        return;
    AiMapSettings settings = life_.citySettings();
    std::vector<uint8_t> data;
    if (!ctx_.files.read("race/" + cities_[size_t(city_)].map + "/roam.aimap", data))
        parseAiMap(std::string(data.begin(), data.end()), settings);
    CityLife::Options o;
    o.traffic = kTrafficLevels[trafficLevel_];
    o.peds = o.traffic * kPedsPerTraffic;
    o.cops = cops_ ? -1 : 0;
    o.rain = lighting_ % 4 == 3;
    o.laneChanges = true; // as San Francisco's cruising says
    o.patrols = true;
    if (Error err = life_.start(*renderer_, scene_->textures(), ctx_.files, settings, o, ctx_.mixer);
        err && error_.empty())
        error_ = err.message;
    cutBridgeGaps();
    fill_ = true;
}

void DriveViewer::loadCar(int index) {
    carIndex_ = index;
    const CarInfo& car = cars_[size_t(carIndex_)];
    setTitle(car.description + " in " + cities_[size_t(city_)].name);
    if (Error err = car_.load(*renderer_, scene_->textures(), ctx_.files, car.base, paintJob_))
        error_ = err.message;
    paintJob_ %= car_.paintJobs;
    engine_.stop();
    if (ctx_.mixer)
        if (Error err = engine_.start(*ctx_.mixer, ctx_.files, car.base))
            error_ = err.message;
    // A police car's siren: the player's table for the city.
    siren_.stop();
    sirenOn_ = false;
    if (ctx_.mixer && car_.hasSiren())
        siren_.load(*ctx_.mixer, ctx_.files,
                    "aud/cardata/player/" + std::string(cities_[size_t(city_)].map == "london" ? "london" : "sf") +
                        "policesiren.csv");
    cameras_ = loadCarCameras(ctx_.files, car.base);
    camera_ = std::min(camera_, int(cameras_.size()) - 1);
    resetCar();
}

void DriveViewer::resetCar() {
    // On the road nearest the middle, a little above it.
    car_.sim.reset(car_.setup, scene_->start + Vec3{0, 0.5f, 0}, scene_->startYaw);
    cameraReset_ = true;
    fill_ = true;
}

void DriveViewer::handleEvent(const Event& ev) {
    if (!renderer_ || cars_.empty())
        return;
    if (ev.type == EventType::KeyDown) {
        if (const uint32_t d = driveKeyOf(ev.key)) {
            keys_ |= d;
            return;
        }
        const int cars = int(cars_.size()), cities = int(cities_.size());
        if (ev.key == key::PageUp || ev.key == key::PageDown) {
            paintJob_ = 0;
            loadCar((carIndex_ + (ev.key == key::PageDown ? 1 : cars - 1)) % cars);
        } else if (ev.key == key::Home || ev.key == key::End) {
            loadCity((city_ + (ev.key == key::End ? 1 : cities - 1)) % cities);
        } else if (ev.key == 'p') {
            ++paintJob_;
            loadCar(carIndex_);
        } else if (ev.key == 'c') {
            camera_ = (camera_ + 1) % int(cameras_.size());
            cameraReset_ = true;
        } else if (ev.key == 't') {
            const bool rain = lighting_ % 4 == 3;
            lighting_ = (lighting_ + 1) % 16;
            scene_->setLighting(lighting_);
            if (rain != (lighting_ % 4 == 3))
                startLife(); // the pedestrians dressed for the weather
        } else if (ev.key == 'y') {
            trafficLevel_ = (trafficLevel_ + 1) % 4;
            life_.setTrafficDensity(kTrafficLevels[trafficLevel_]);
            life_.setPedDensity(kTrafficLevels[trafficLevel_] * kPedsPerTraffic);
        } else if (ev.key == 'h' && car_.hasSiren()) {
            sirenOn_ = !sirenOn_;
        } else if (ev.key == 'o') {
            cops_ = !cops_;
            startLife();
        } else if (ev.key == 'r') {
            resetCar();
            life_.police().forgive();
        } else if (ev.key == 'i') {
            showInfo_ = !showInfo_;
        }
    } else if (ev.type == EventType::KeyUp) {
        keys_ &= ~driveKeyOf(ev.key);
    }
}

void DriveViewer::drawHud() {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    const int lh = font::kLineHeight;
    const CarInfo& car = cars_[size_t(carIndex_)];
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
        char life[160];
        std::snprintf(life, sizeof life, "%s (Y): %zu cars, %zu people; %zu police (O), %d on patrol%s",
                      kTrafficNames[trafficLevel_], life_.traffic().count(), life_.pedestrians().count(),
                      life_.police().count(), life_.police().patrolling(), car_.hasSiren() ? "; H siren" : "");
        line(life, {230, 230, 230});
        char buf[160];
        const Vec3& p = car_.sim.position;
        std::snprintf(buf, sizeof buf, "%zu collision triangles (%zu placed models' bounds); at %.0f %.0f %.0f",
                      world_.size(), boundsUsed_, p[0], p[1], p[2]);
        line(buf, {230, 230, 230});
    }
    if (const int chasing = life_.police().chasing()) {
        const std::string how = chasing > 1 ? " (" + std::to_string(chasing) + ")" : "";
        font::drawText(overlay_, 4, kFrameH - 24, "POLICE CHASING" + how, {255, 90, 90}, 2);
    }
    if (busted_ > 0)
        font::drawText(overlay_, kFrameW / 2 - 120, kFrameH / 2 - 40, "BUSTED!", {255, 80, 80}, 6);
    drawSpeedometer(overlay_, car_.sim);
}

void DriveViewer::update(uint64_t nowMs) {
    if (!renderer_ || cars_.empty())
        return;
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000) : 0;
    lastMs_ = nowMs;

    const CarControls controls = controlsOf(keys_);
    car_.sim.step(controls, world_, dt);
    // Fallen out of the city: back to the start.
    if (car_.sim.position[1] < scene_->psdl().min[1] - 50)
        resetCar();
    engine_.update(car_.sim.rpm, car_.sim.gear > 0 ? controls.throttle : controls.brake);
    car_.lamps = {scene_->lampsLit(), controls.brake > 0 && car_.sim.forwardSpeed() > 0.5f, car_.sim.gear < 0};
    clock_ += dt;
    car_.siren = sirenOn_ ? 1 + int(clock_ * 4) % 2 : 0;
    siren_.update(dt, sirenOn_ ? 0.9f : 0.0f);
    life_.setLamps(scene_->lampsLit(), scene_->night());
    life_.update(dt, car_.sim, {}, world_, roads_, fill_);
    fill_ = false;
    // Busted: a moment to see it, then back to the start.
    if (busted_ > 0) {
        busted_ -= dt;
        if (busted_ <= 0) {
            resetCar();
            life_.police().forgive();
        }
    } else if (life_.police().busted()) {
        busted_ = 3;
    }

    const CarCamera& cam = cameras_[size_t(camera_)];
    const CameraView view = updateCarCamera(cam, car_.sim, dt, cameraPos_, cameraReset_);
    cameraReset_ = false;
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(cam.fov * kPi / 180, float(kFrameW) / float(kFrameH), 0.1f, kFar),
                      gfx::lookAlong(view.eye, normalized(view.target - view.eye), view.up));
    renderer_->beginFrame(viewProj, view.eye, {150, 170, 200});
    scene_->draw(viewProj, view.eye, true);
    if (scene_->lampsLit()) {
        std::vector<gfx::DynamicLight> pools;
        gfx::DynamicLight mine;
        if (car_.headlightPool(mine))
            pools.push_back(mine);
        life_.headlightPools(pools, view.eye);
        scene_->drawDynamicLights(pools, view.eye);
    }
    life_.draw(*renderer_, scene_->models(), scene_->light(), viewProj, view.eye, flares_.get());
    if (!cam.inside) {
        car_.draw(scene_->light());
        car_.drawFlares(*flares_, view.eye);
    }
    drawHud();
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::age
