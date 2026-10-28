// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Race page: the game's Blitz, Circuit and Checkpoint races, from its
// race files (race.h), against opponents driving their paths along the
// roads (opponent.h). The rules as Reluva plays them are in
// docs/engines/age-racing.md; the game's menus aren't redrawn yet, this
// page has its own.

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
constexpr float kCountdown = 3;         // seconds before the start
constexpr float kCircuitRadius = 15;    // circuits' waypoints give no radius
constexpr float kOpponentReach = 10;    // opponents count a checkpoint this much wider
constexpr float kSkill[2] = {0.85f, 0.95f}; // amateur, professional: opponents' share of top speed
const RaceKind kKinds[] = {RaceKind::Blitz, RaceKind::Circuit, RaceKind::Checkpoint};

std::string clockText(float seconds) {
    if (seconds < 0)
        seconds = 0;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(seconds, 60.0f));
    return buf;
}

float flatDistance(const Vec3& a, const Vec3& b) {
    const float dx = a[0] - b[0], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

RaceViewer::RaceViewer(ViewerContext& ctx) : Page(ctx), cities_(findCities(ctx.files)), cars_(findCars(ctx.files)) {}

RaceViewer::~RaceViewer() { deactivate(); }

void RaceViewer::deactivate() {
    life_.stop();
    engine_.stop();
    racers_.clear();
    flares_.reset();
    scene_.reset();
    loadedCity_ = -1;
    renderer_ = nullptr;
    state_ = State::Menu;
}

void RaceViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Race page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    scene_ = std::make_unique<CityScene>(*renderer_, ctx_.files);
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    keys_ = 0;
    lastMs_ = 0;
    state_ = State::Menu;
    loadCity();
}

void RaceViewer::loadCity() {
    if (loadedCity_ == city_)
        return;
    racers_.clear();
    flares_.reset();
    const std::string& map = cities_[size_t(city_)].map;
    error_ = scene_->load(map).message;
    flares_ = std::make_unique<FlareDrawer>(*renderer_, scene_->textures());
    buildCityCollision(ctx_.files, scene_->psdl(), scene_->mesh(), scene_->instances(), world_);
    roads_.build(scene_->psdl());
    if (Error err = life_.load(ctx_.files, map); err && error_.empty())
        error_ = err.message;
    loadedCity_ = city_;
    raceNames_ = raceNames(ctx_.files, map, kKinds[kind_]);
    raceIndex_ = std::min(raceIndex_, std::max(0, int(raceNames_.size()) - 1));
    // Show where the chosen race starts.
    if (!raceNames_.empty())
        loadRace(ctx_.files, map, kKinds[kind_], raceIndex_, race_);
}

bool RaceViewer::takesKey(const Event& ev) const {
    return state_ != State::Menu && ev.type == EventType::KeyDown && ev.key == key::Escape;
}

void RaceViewer::startRace() {
    const std::string& map = cities_[size_t(city_)].map;
    if (raceNames_.empty())
        return;
    if (Error err = loadRace(ctx_.files, map, kKinds[kind_], raceIndex_, race_)) {
        error_ = err.message;
        return;
    }
    error_.clear();
    const RaceSettings& set = race_.settings[size_t(level_)];
    scene_->setLighting(std::clamp(set.timeOfDay, 0, 3) * 4 + std::clamp(set.weather, 0, 3));

    // The checkpoints in order: the waypoints after the start; a circuit's
    // lap ends back at the first, as many laps as it has.
    targets_.clear();
    const size_t n = race_.waypoints.size();
    const int laps = race_.kind == RaceKind::Circuit ? std::max(1, set.laps) : 1;
    for (int lap = 0; lap < laps; ++lap) {
        for (size_t i = 1; i < n; ++i)
            targets_.push_back(i);
        if (race_.kind == RaceKind::Circuit)
            targets_.push_back(0);
    }

    racers_.clear();
    Racer player;
    player.car = std::make_unique<CarActor>();
    player.player = true;
    player.name = cars_[size_t(car_)].description + " (you)";
    if (Error err = player.car->load(*renderer_, scene_->textures(), ctx_.files, cars_[size_t(car_)].base, 0))
        error_ = err.message;
    const Waypoint& start = race_.waypoints[0];
    player.car->sim.reset(player.car->setup, start.position + Vec3{0, 0.5f, 0}, headingToYaw(start.heading));
    racers_.push_back(std::move(player));

    // The race's aimap, read over the city's: the opponents' cars and
    // skill, the roads left without traffic, the police.
    AiMapSettings ai = life_.citySettings();
    std::vector<uint8_t> data;
    if (!ctx_.files.read(raceAiMapName(map, race_.kind, raceIndex_, level_ == 1), data) ||
        !ctx_.files.read(raceAiMapName(map, race_.kind, raceIndex_, false), data))
        parseAiMap(std::string(data.begin(), data.end()), ai);

    // Opponents: as many as the race has paths for, in the cars its aimap
    // gives them (else others of the player's list).
    const auto& paths = race_.opponents[size_t(level_)];
    const size_t count = std::min(paths.size(), size_t(std::max(0, set.opponents)));
    for (size_t i = 0; i < count; ++i) {
        std::string base = cars_[(size_t(car_) + 1 + i * 3) % cars_.size()].base;
        float skill = kSkill[level_];
        const std::string opp = "-" + std::to_string(i) + ".opp";
        for (const auto& o : ai.opponents)
            if (o.path.size() > opp.size() && o.path.compare(o.path.size() - opp.size(), opp.size(), opp) == 0) {
                base = o.model;
                skill = std::clamp(o.skill, 0.6f, 1.0f);
            }
        Racer r;
        r.car = std::make_unique<CarActor>();
        r.name = base;
        for (const CarInfo& info : cars_)
            if (info.base == base)
                r.name = info.description;
        if (r.car->load(*renderer_, scene_->textures(), ctx_.files, base, uint32_t(i)))
            continue;
        const auto& o = paths[i];
        r.car->sim.reset(r.car->setup, o.start + Vec3{0, 0.5f, 0}, o.startYaw);
        size_t loopFrom = 0;
        const bool circuit = race_.kind == RaceKind::Circuit;
        r.driver.start(routePath(roads_, o.start, o.path, circuit, loopFrom), circuit ? laps : 0, skill, loopFrom);
        racers_.push_back(std::move(r));
    }

    // Traffic, pedestrians and police as the race's settings say.
    CityLife::Options life;
    life.traffic = set.ambient;
    life.peds = set.peds;
    life.cops = std::max(0, set.cops);
    life.rain = set.weather == 3;
    life.seed = uint32_t(raceIndex_ * 7 + level_ + 1);
    if (life_.loaded())
        if (Error err = life_.start(*renderer_, scene_->textures(), ctx_.files, ai, life, ctx_.mixer))
            error_ = err.message;
    fill_ = true;

    engine_.stop();
    if (ctx_.mixer)
        engine_.start(*ctx_.mixer, ctx_.files, cars_[size_t(car_)].base);
    cameras_ = loadCarCameras(ctx_.files, cars_[size_t(car_)].base);
    camera_ = std::min(camera_, int(cameras_.size()) - 1);
    cameraReset_ = true;
    clock_ = -kCountdown;
    outcome_.clear();
    keys_ = 0;
    state_ = State::Countdown;
    setTitle(race_.name + " (" + raceKindName(race_.kind) + ", " + cities_[size_t(city_)].name + ")");
}

void RaceViewer::endRace() {
    life_.stop();
    engine_.stop();
    racers_.clear();
    state_ = State::Menu;
    scene_->setLighting(4);
}

float RaceViewer::progressOf(const Racer& r) const {
    // Checkpoints reached, and how near the next.
    if (r.next >= targets_.size())
        return float(targets_.size()) + 1 - r.finishTime / 1e5f;
    const float d = flatDistance(r.car->sim.position, targetPoint(r.next).position);
    return float(r.next) + 1 / (1 + d / 50);
}

void RaceViewer::stepRace(float dt) {
    clock_ += dt;
    const bool started = clock_ >= 0;
    for (Racer& r : racers_) {
        CarSim& sim = r.car->sim;
        CarControls c;
        if (started) {
            if (r.player)
                c = r.next < targets_.size() ? controlsOf(keys_) : CarControls{0, 1, 0, false};
            else
                c = r.driver.drive(sim, dt);
        } else {
            c.brake = 0; // waiting on the line
        }
        sim.step(c, world_, dt);
        r.car->lamps = {scene_->lampsLit(), c.brake > 0 && sim.forwardSpeed() > 0.5f, sim.gear < 0};
        if (!r.player && (r.driver.wantsReset() || sim.position[1] < scene_->psdl().min[1] - 50)) {
            sim.reset(r.car->setup, r.driver.resetPoint() + Vec3{0, 0.5f, 0}, r.driver.resetYaw());
            r.driver.resetDone();
        }
        if (r.player) {
            engine_.update(sim.rpm, sim.gear > 0 ? c.throttle : c.brake);
            if (sim.position[1] < scene_->psdl().min[1] - 50) // fallen out: back to the last checkpoint
                sim.reset(r.car->setup,
                          (r.next > 0 ? targetPoint(r.next - 1) : race_.waypoints[0]).position + Vec3{0, 0.5f, 0},
                          headingToYaw((r.next > 0 ? targetPoint(r.next - 1) : race_.waypoints[0]).heading));
        }
        // Checkpoints.
        if (started && r.next < targets_.size()) {
            const Waypoint& w = targetPoint(r.next);
            const float radius = (w.radius > 0 ? w.radius : kCircuitRadius) + (r.player ? 2 : kOpponentReach);
            if (flatDistance(sim.position, w.position) < radius && std::fabs(sim.position[1] - w.position[1]) < 10) {
                if (++r.next == targets_.size())
                    r.finishTime = clock_;
            }
        }
    }
    for (size_t i = 0; i < racers_.size(); ++i)
        for (size_t j = i + 1; j < racers_.size(); ++j)
            collideCars(racers_[i].car->sim, racers_[j].car->sim);
    std::vector<CarSim*> others;
    for (size_t i = 1; i < racers_.size(); ++i)
        others.push_back(&racers_[i].car->sim);
    life_.setLamps(scene_->lampsLit(), scene_->night());
    life_.update(dt, racers_.front().car->sim, others, world_, roads_, fill_);
    fill_ = false;

    if (state_ == State::Countdown && started)
        state_ = State::Racing;
    if (state_ == State::Finished)
        return;
    // The end: the player finished, or ran out of time in a Blitz.
    Racer& player = racers_.front();
    const RaceSettings& set = race_.settings[size_t(level_)];
    if (player.finishTime >= 0) {
        int place = 1;
        for (const Racer& r : racers_)
            if (!r.player && r.finishTime >= 0 && r.finishTime < player.finishTime)
                ++place;
        outcome_ = race_.kind == RaceKind::Blitz ? "Finished in " + clockText(player.finishTime)
                                                 : "Finished " + std::to_string(place) + " of " +
                                                       std::to_string(racers_.size()) + " in " +
                                                       clockText(player.finishTime);
        state_ = State::Finished;
    } else if (race_.kind == RaceKind::Blitz && set.timeLimit > 0 && clock_ > set.timeLimit) {
        outcome_ = "Time's up";
        state_ = State::Finished;
    } else if (life_.police().busted()) {
        outcome_ = "Busted";
        state_ = State::Finished;
    }
}

void RaceViewer::handleEvent(const Event& ev) {
    if (!renderer_)
        return;
    if (ev.type == EventType::KeyUp) {
        keys_ &= ~driveKeyOf(ev.key);
        return;
    }
    if (ev.type != EventType::KeyDown)
        return;
    if (state_ == State::Menu) {
        const int fields = 5;
        auto wrap = [](int v, int n) { return n > 0 ? (v % n + n) % n : 0; };
        if (ev.key == key::Up)
            field_ = wrap(field_ - 1, fields);
        else if (ev.key == key::Down)
            field_ = wrap(field_ + 1, fields);
        else if (ev.key == key::Left || ev.key == key::Right) {
            const int step = ev.key == key::Right ? 1 : -1;
            switch (field_) {
            case 0:
                city_ = wrap(city_ + step, int(cities_.size()));
                loadCity();
                break;
            case 1: kind_ = wrap(kind_ + step, 3); break;
            case 2: raceIndex_ = wrap(raceIndex_ + step, int(raceNames_.size())); break;
            case 3: level_ = wrap(level_ + step, 2); break;
            case 4: car_ = wrap(car_ + step, int(cars_.size())); break;
            }
            if (field_ == 1 || field_ == 2) {
                const std::string& map = cities_[size_t(city_)].map;
                raceNames_ = raceNames(ctx_.files, map, kKinds[kind_]);
                raceIndex_ = std::min(raceIndex_, std::max(0, int(raceNames_.size()) - 1));
                if (!raceNames_.empty())
                    loadRace(ctx_.files, map, kKinds[kind_], raceIndex_, race_);
            }
        } else if (ev.key == key::Return || ev.key == key::KpEnter) {
            startRace();
        }
        return;
    }
    if (ev.key == key::Escape || (state_ == State::Finished && (ev.key == key::Return || ev.key == key::KpEnter))) {
        endRace();
        return;
    }
    if (const uint32_t d = driveKeyOf(ev.key)) {
        keys_ |= d;
    } else if (ev.key == 'c') {
        camera_ = (camera_ + 1) % int(cameras_.size());
        cameraReset_ = true;
    }
}

void RaceViewer::drawCheckpoints() {
    // The game's checkpoint stand (pt_check) at both ends of each gate still
    // to reach, across the way its heading points.
    const ModelCache::Model& stand = scene_->models().get("pt_check");
    if (!stand.ok || racers_.empty())
        return;
    const Racer& player = racers_.front();
    for (size_t k = player.next; k < targets_.size() && k < player.next + 2; ++k) {
        const Waypoint& w = targetPoint(k);
        const float yaw = headingToYaw(w.heading);
        const Mat3 turn = Mat3::rotationY(yaw);
        const float radius = w.radius > 0 ? w.radius : kCircuitRadius;
        for (float side : {-1.0f, 1.0f}) {
            const Vec3 at = w.position + turn * Vec3{side * radius, 0.5f, 0};
            drawModelMesh(*renderer_, stand.levels.front(), matrixOf(turn, at), scene_->light(), false);
        }
    }
}

void RaceViewer::drawArrow() {
    // The game's HUD arrow (hudarrow01, hudarrow_blitz01 in a Blitz) above
    // the car, pointing to the next checkpoint.
    const Racer& player = racers_.front();
    if (player.next >= targets_.size())
        return;
    const ModelCache::Model& arrow =
        scene_->models().get(race_.kind == RaceKind::Blitz ? "hudarrow_blitz01" : "hudarrow01");
    if (!arrow.ok)
        return;
    const Vec3 at = player.car->sim.position + Vec3{0, 2.8f, 0};
    Vec3 to = targetPoint(player.next).position - at;
    to[1] = 0;
    const float yaw = length(to) > 0.1f ? std::atan2(-to[0], -to[2]) : 0;
    SceneLight bright;
    bright.ambient = {1, 1, 1};
    bright.directed = {0, 0, 0};
    for (const ModelDraw& d : arrow.levels.front().draws) {
        gfx::DrawCall call;
        call.mesh = arrow.levels.front().mesh;
        call.firstIndex = d.firstIndex;
        call.indexCount = d.indexCount;
        call.model = matrixOf(Mat3::rotationY(yaw), at);
        call.texture = d.texture;
        call.state.depthTest = false;
        call.state.cull = gfx::Cull::None;
        renderer_->draw(call);
    }
}

void RaceViewer::drawMenu() {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    const int lh = font::kLineHeight * 2;
    int y = 60;
    font::drawText(overlay_, 40, 20, "RACE", {255, 210, 80}, 3);
    const std::string kind = raceKindName(kKinds[kind_]);
    const std::string raceName = raceNames_.empty() ? "(none)" : raceNames_[size_t(raceIndex_)];
    const std::string lines[5] = {"City:   " + cities_[size_t(city_)].name, "Event:  " + kind,
                                  "Race:   " + raceName, std::string("Level:  ") + (level_ ? "Professional" : "Amateur"),
                                  "Car:    " + cars_[size_t(car_)].description};
    for (int i = 0; i < 5; ++i) {
        const Color c = i == field_ ? Color{255, 255, 120} : Color{230, 230, 230};
        font::drawText(overlay_, 40, y, (i == field_ ? "> " : "  ") + lines[i], c, 2);
        y += lh + 4;
    }
    if (!raceNames_.empty()) {
        const RaceSettings& s = race_.settings[size_t(level_)];
        char buf[200];
        std::snprintf(buf, sizeof buf, "%zu checkpoints, %d opponents, %d laps, time limit %.0f s",
                      race_.waypoints.size() - 1, s.opponents, race_.kind == RaceKind::Circuit ? s.laps : 1,
                      s.timeLimit);
        font::drawText(overlay_, 40, y + 10, buf, {180, 180, 180});
        y += font::kLineHeight + 4;
        std::snprintf(buf, sizeof buf, "traffic %.0f%%, pedestrians %.0f%%, police %d", s.ambient * 100,
                      s.peds * 100, s.cops);
        font::drawText(overlay_, 40, y + 10, buf, {180, 180, 180});
    }
    font::drawText(overlay_, 40, kFrameH - 40, "Up/Down: choose  Left/Right: change  Enter: race", {150, 200, 255});
    if (!error_.empty())
        font::drawText(overlay_, 40, kFrameH - 24, error_, {255, 120, 120});
}

void RaceViewer::drawRaceHud() {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    const Racer& player = racers_.front();
    const RaceSettings& set = race_.settings[size_t(level_)];
    font::drawText(overlay_, 4, 4, race_.name + "  (" + raceKindName(race_.kind) + ")", {150, 200, 255});
    // Time, or time left in a Blitz.
    const bool blitz = race_.kind == RaceKind::Blitz && set.timeLimit > 0;
    const float shown = blitz ? set.timeLimit - std::max(0.0f, clock_) : std::max(0.0f, clock_);
    font::drawText(overlay_, 4, 18, (blitz ? "LEFT " : "TIME ") + clockText(shown),
                   blitz && shown < 10 ? Color{255, 90, 90} : Color{255, 255, 255}, 2);
    // Checkpoints, laps, position.
    const size_t perLap = race_.waypoints.size() - (race_.kind == RaceKind::Circuit ? 0 : 1);
    char buf[80];
    if (race_.kind == RaceKind::Circuit) {
        const int laps = std::max(1, set.laps);
        std::snprintf(buf, sizeof buf, "LAP %d/%d", std::min(int(player.next / perLap) + 1, laps), laps);
        font::drawText(overlay_, 4, 40, buf, {255, 255, 255}, 2);
    } else {
        std::snprintf(buf, sizeof buf, "CHECKPOINT %zu/%zu", std::min(player.next + 1, targets_.size()),
                      targets_.size());
        font::drawText(overlay_, 4, 40, buf, {255, 255, 255}, 2);
    }
    if (racers_.size() > 1) {
        int place = 1;
        const float mine = progressOf(player);
        for (const Racer& r : racers_)
            if (!r.player && progressOf(r) > mine)
                ++place;
        std::snprintf(buf, sizeof buf, "POS %d/%zu", place, racers_.size());
        font::drawText(overlay_, 4, 62, buf, {255, 210, 80}, 2);
    }
    if (clock_ < 0) {
        const std::string count = std::to_string(int(std::ceil(-clock_)));
        font::drawText(overlay_, kFrameW / 2 - 20, kFrameH / 2 - 40, count, {255, 255, 255}, 8);
    } else if (clock_ < 1) {
        font::drawText(overlay_, kFrameW / 2 - 50, kFrameH / 2 - 40, "GO!", {120, 255, 120}, 8);
    }
    if (state_ == State::Finished) {
        font::drawText(overlay_, 60, 120, outcome_, {255, 255, 120}, 3);
        // The order the racers finished in, those still racing after.
        std::vector<const Racer*> order;
        for (const Racer& r : racers_)
            order.push_back(&r);
        std::stable_sort(order.begin(), order.end(),
                         [&](const Racer* a, const Racer* b) { return progressOf(*a) > progressOf(*b); });
        int y = 170;
        for (size_t i = 0; i < order.size(); ++i) {
            const Racer& r = *order[i];
            std::snprintf(buf, sizeof buf, "%zu. %-40s %s", i + 1, r.name.c_str(),
                          r.finishTime >= 0 ? clockText(r.finishTime).c_str() : "racing");
            font::drawText(overlay_, 60, y, buf, r.player ? Color{255, 255, 120} : Color{230, 230, 230});
            y += font::kLineHeight + 2;
        }
        font::drawText(overlay_, 60, y + 10, "Enter: back to the menu", {150, 200, 255});
    }
    if (life_.police().chasing() > 0 && state_ != State::Finished)
        font::drawText(overlay_, 4, kFrameH - 24, "POLICE CHASING", {255, 90, 90}, 2);
    drawSpeedometer(overlay_, player.car->sim);
}

void RaceViewer::update(uint64_t nowMs) {
    if (!renderer_ || cars_.empty())
        return;
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000) : 0;
    lastMs_ = nowMs;

    Vec3 eye, target, up{0, 1, 0};
    float fov = 70;
    if (state_ == State::Menu || racers_.empty()) {
        // Round the chosen race's start.
        orbit_ += dt * 0.15f;
        const Vec3 centre = race_.waypoints.empty() ? scene_->start : race_.waypoints[0].position;
        eye = centre + Vec3{std::sin(orbit_) * 40, 18, std::cos(orbit_) * 40};
        target = centre;
    } else {
        // After the finish the others race on to theirs, for the results.
        stepRace(dt);
        const CarCamera& cam = cameras_[size_t(camera_)];
        const CameraView view = updateCarCamera(cam, racers_.front().car->sim, dt, cameraPos_, cameraReset_);
        cameraReset_ = false;
        eye = view.eye;
        target = view.target;
        up = view.up;
        fov = cam.fov;
    }
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(fov * kPi / 180, float(kFrameW) / float(kFrameH), 0.1f, kFar),
                      gfx::lookAlong(eye, normalized(target - eye), up));
    renderer_->beginFrame(viewProj, eye, {150, 170, 200});
    scene_->draw(viewProj, eye, true);
    if (state_ == State::Menu || racers_.empty()) {
        drawMenu();
    } else {
        const bool inside = cameras_[size_t(camera_)].inside;
        if (scene_->lampsLit()) {
            std::vector<gfx::DynamicLight> pools;
            gfx::DynamicLight pool;
            for (const Racer& r : racers_)
                if (r.car->headlightPool(pool))
                    pools.push_back(pool);
            life_.headlightPools(pools, eye);
            scene_->drawDynamicLights(pools, eye);
        }
        life_.draw(*renderer_, scene_->models(), scene_->light(), viewProj, eye, flares_.get());
        for (const Racer& r : racers_)
            if (!(r.player && inside)) {
                r.car->draw(scene_->light());
                r.car->drawFlares(*flares_, eye);
            }
        drawCheckpoints();
        drawArrow();
        drawRaceHud();
    }
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::age
