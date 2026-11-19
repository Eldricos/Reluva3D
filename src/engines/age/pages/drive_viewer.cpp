// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Drive page: a car driven round a city, as a cruise
// mode. The car is Reluva's simulation (car.cpp) of the game's tuning;
// the cameras follow the game's camera files. Also the driving helpers
// the Race page shares.

#include "engines/age/viewers.h"

#include "graphics/font.h"
#include "engines/age/game/city_collision.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kPi = 3.14159265358979f;
constexpr float kBigAir = 1;     // seconds off the ground that count as big air (Reluva's)
constexpr float kMph = 2.23694f; // miles an hour per metre a second
// None, light, normal (London's cruising [Density], 0.1), heavy: shares of
// the lanes' capacity (traffic.h).
constexpr float kTrafficLevels[] = {0, 0.05f, 0.1f, 0.25f};
const char *const kTrafficNames[] = {"no traffic", "light traffic", "normal traffic", "heavy traffic"};
constexpr float kPedLevels[] = {0, 0.05f, 0.1f, 0.2f}; // the pedestrians' density with each

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
    const int barX = speedX, barY = h - 22, barW = right - speedX, barH = 8;
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
    constexpr int kX = 447, kY = 0, kW = 193, kH = 77;
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

DriveViewer::DriveViewer(ViewerContext &ctx) : Page(ctx), _cities(findCities(ctx.files)), _cars(findCars(ctx.files)) {
}

DriveViewer::~DriveViewer() {
    deactivate();
}

void DriveViewer::deactivate() {
    _siren.stop();
    _life.stop();
    _engine.stop();
    _music.stop();
    _car.unload();
    _flares.reset();
    _smoke.reset();
    _hudMap.unload();
    _scene.reset();
    _renderer = nullptr;
}

void DriveViewer::activate() {
    if (Error err = _ctx.sys.init3D(kFrameW, kFrameH, _renderer)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRgbA32);
        clear(16, 18, 24);
        text(4, 4, "The Drive page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    _scene = std::make_unique<CityScene>(*_renderer, _ctx.files);
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRgbA32);
    applySoundOptions(loadOptions(_ctx.savePath), _ctx.mixer);
    _keys = 0;
    _lastMs = 0;
    loadCity(_city);
}

void DriveViewer::loadCity(int index) {
    _city = index;
    _car.unload();
    _flares.reset();
    _smoke.reset();
    _scene->textures().setHalvings(textureHalvings());
    _error = _scene->load(_cities[size_t(_city)].map).message;
    _flares = std::make_unique<FlareDrawer>(*_renderer, _scene->textures());
    _smoke = std::make_unique<SmokePuffs>(*_renderer, _scene->textures());
    _smoke->loadWheelEffects(_ctx.files);
    _hudMap.load(*_renderer, _ctx.files, _scene->models(), _cities[size_t(_city)].map);
    _pictures.load(_ctx.files);
    _scene->setLighting(_lighting);
    _boundsUsed = buildCityCollision(_ctx.files, _scene->psdl(), _scene->mesh(), _scene->instances(), _world);
    _roads.build(_scene->psdl());
    if (Error err = _life.load(_ctx.files, _cities[size_t(_city)].map); err && _error.empty())
        _error = err.message;
    loadCar(_carIndex);
    startLife();
}

void DriveViewer::cutBridgeGaps() {
    // What the city has across the drawbridges' gaps goes: the leaves are
    // the road there.
    auto inGap = [&](const Vec3 &a, const Vec3 &b, const Vec3 &c) { return _life.gizmos().inGap(a, b, c); };
    _scene->hide(inGap);
    _world.removeIf(inGap);
}

void DriveViewer::startLife() {
    // The city's settings and cruising's (race/<city>/roam.aimap: the
    // police).
    if (!_life.loaded())
        return;
    AiMapSettings settings = _life.citySettings();
    std::vector<uint8_t> data;
    if (!_ctx.files.read("race/" + _cities[size_t(_city)].map + "/roam.aimap", data))
        parseAiMap(std::string(data.begin(), data.end()), settings);
    CityLife::Options o;
    o.traffic = kTrafficLevels[_trafficLevel];
    o.peds = kPedLevels[_trafficLevel] * pedestrianShare();
    o.cops = _cops ? -1 : 0;
    o.rain = _lighting % 4 == 3;
    o.laneChanges = true; // as San Francisco's cruising says
    o.patrols = true;
    if (Error err = _life.start(*_renderer, _scene->textures(), _ctx.files, settings, o, _ctx.mixer);
        err && _error.empty())
        _error = err.message;
    cutBridgeGaps();
    _fill = true;
}

void DriveViewer::loadCar(int index) {
    _carIndex = index;
    const CarInfo &car = _cars[size_t(_carIndex)];
    setTitle(car.description + " in " + _cities[size_t(_city)].name);
    if (Error err = _car.load(*_renderer, _scene->textures(), _ctx.files, car.base, _paintJob))
        _error = err.message;
    _paintJob %= _car.paintJobs;
    _engine.stop();
    if (_ctx.mixer)
        if (Error err = _engine.start(*_ctx.mixer, _ctx.files, car.base))
            _error = err.message;
    startSounds();
    // The cruising music (aud/dmusic), once for the drive.
    if (_ctx.mixer && _music.segment().empty())
        if (Error err = _music.start(_ctx.files, *_ctx.mixer, false, uint32_t(_city * 5 + _carIndex)))
            _error = "music: " + err.message;
    // A police car's siren: the player's table for the city.
    _siren.stop();
    _sirenOn = false;
    if (_ctx.mixer && _car.hasSiren())
        _siren.load(*_ctx.mixer, _ctx.files,
                    "aud/cardata/player/" + std::string(_cities[size_t(_city)].map == "london" ? "london" : "sf") +
                        "policesiren.csv");
    _cameras = loadCarCameras(_ctx.files, car.base);
    _camera = std::min(_camera, int(_cameras.size()) - 1);
    _dash.load(_ctx.files, _scene->models(), _scene->textures(), car.base);
    _wear.load(_ctx.files, car.base, _car);
    resetCar();
}

void DriveViewer::startSounds() {
    _sounds.stop();
    if (_ctx.mixer)
        _sounds.start(*_ctx.mixer, _ctx.files, _cars[size_t(_carIndex)].base, _lighting % 4 == 3);
}

void DriveViewer::resetCar() {
    // On the road nearest the middle, a little above it.
    _car.sim.reset(_car.setup, _scene->start + Vec3{0, 0.5f, 0}, _scene->startYaw);
    _wear.mend(_car);
    _wrecked = 0;
    if (_smoke)
        _smoke->clear();
    _cameraReset = true;
    _fill = true;
}

void DriveViewer::handleEvent(const Event &ev) {
    if (!_renderer || _cars.empty())
        return;
    // A game controller: its sticks and triggers drive, its buttons press
    // keys.
    if (ev.type == EventType::kPadAxis) {
        _pad.axis(ev.pad, ev.value);
        return;
    }
    if (ev.type == EventType::kPadDown || ev.type == EventType::kPadUp) {
        if (Event key; padKeyEvent(ev, true, currentOptions(), key))
            handleEvent(key);
        return;
    }
    if (ev.type == EventType::kKeyDown) {
        const Control control = controlOf(ev.key);
        if (control == Control::kHorn && _car.hasSiren()) { // a police car's horn is its siren
            _sirenOn = !_sirenOn;
            return;
        }
        if (const uint32_t d = driveKeyOf(ev.key)) {
            _keys |= d;
            return;
        }
        if (ev.key == key::kF1) {
            _view.controlsList = !_view.controlsList; // stops the drive while it's up
            _keys = 0;
            return;
        }
        if (control == Control::kRotatingMap) {
            _hudMap.toggleRotating();
            return;
        }
        if (_cd.press(control, _music, _ctx.files, _ctx.mixer))
            return;
        if (_view.press(control, true)) {
            _cameraReset = _cameraReset || control == Control::kThrillCam;
            return;
        }
        const int cars = int(_cars.size()), cities = int(_cities.size());
        if (ev.key == key::kPageUp || ev.key == key::kPageDown) {
            _paintJob = 0;
            loadCar((_carIndex + (ev.key == key::kPageDown ? 1 : cars - 1)) % cars);
        } else if (ev.key == key::kHome || ev.key == key::kEnd) {
            loadCity((_city + (ev.key == key::kEnd ? 1 : cities - 1)) % cities);
        } else if (control == Control::kChangeCamera) {
            _camera = (_camera + 1) % int(_cameras.size());
            _cameraReset = true;
            _blend.start(_lastView, _cameras[size_t(_camera)].blendTime);
        } else if (control == Control::kTransmission) {
            _manual = !_manual;
        } else if (control == Control::kShiftUp || control == Control::kShiftDown) {
            _shift = control == Control::kShiftUp ? 1 : -1;
        } else if (control == Control::kMapZoom) {
            _hudMap.toggleZoom();
        } else if (control == Control::kFullScreenMap) {
            _hudMap.toggleFullScreen();
        } else if (control == Control::kMapToggle) {
            _hudMap.toggleShown();
        } else if (control == Control::kHudToggle) {
            _hudShown = !_hudShown;
        } else if (control == Control::kRearViewMirror) {
            _mirror = !_mirror;
            // The page's own keys, on function keys clear of the game's controls
            // (F4 is the game's restart).
        } else if (ev.key == key::kF1 + 7) {
            ++_paintJob;
            loadCar(_carIndex);
        } else if (ev.key == key::kF1 + 4) {
            const bool rain = _lighting % 4 == 3;
            _lighting = (_lighting + 1) % 16;
            _scene->setLighting(_lighting);
            if (rain != (_lighting % 4 == 3)) {
                startLife();   // the pedestrians dressed for the weather
                startSounds(); // the tyres on wet roads
            }
        } else if (ev.key == key::kF1 + 5) {
            _trafficLevel = (_trafficLevel + 1) % 4;
            _life.setTrafficDensity(kTrafficLevels[_trafficLevel]);
            _life.setPedDensity(kPedLevels[_trafficLevel] * pedestrianShare());
        } else if (ev.key == key::kF1 + 6) {
            _cops = !_cops;
            startLife();
        } else if (ev.key == key::kF1 + 3) {
            resetCar();
            _life.police().forgive();
        } else if (ev.key == key::kF1 + 8) {
            _showInfo = !_showInfo;
        }
    } else if (ev.type == EventType::kKeyUp) {
        _keys &= ~driveKeyOf(ev.key);
        _view.press(controlOf(ev.key), false);
    }
}

void DriveViewer::drawHud() {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    const int lh = font::kLineHeight;
    const CarInfo &car = _cars[size_t(_carIndex)];
    int y = 4;
    auto line = [&](const std::string &s, Color c) {
        font::drawText(_overlay, 4, y, s, c);
        y += lh;
    };
    std::string color = _paintJob < car.colors.size() ? car.colors[_paintJob] : std::to_string(_paintJob + 1);
    line(car.description + " (" + color + ", F8)  in " + _cities[size_t(_city)].name + ", " + _scene->lightingName(),
         {150, 200, 255});
    if (!_error.empty())
        line(_error, {255, 120, 120});
    if (_showInfo) {
        const char *const cameraNames[] = {"near", "far", "in the car"};
        line(std::string("camera: ") + cameraNames[std::min(_camera, 2)] +
                 " (C); Up/Down, Left/Right, Space; Enter horn; " +
                 (_manual ? "manual (T), A/Z gears" : "automatic (T)") +
                 "; PageUp/Down car, Home/End city, F5 time, F4 reset; map: E zoom, Q full screen, Tab on/off; "
                 "H HUD; Backspace mirror",
             {230, 230, 230});
        char life[160];
        std::snprintf(life, sizeof life, "%s (F6): %zu cars, %zu people; %zu police (F7), %d on patrol%s",
                      kTrafficNames[_trafficLevel], _life.traffic().count(), _life.pedestrians().count(),
                      _life.police().count(), _life.police().patrolling(), _car.hasSiren() ? "; Enter siren" : "");
        line(life, {230, 230, 230});
        char buf[160];
        const Vec3 &p = _car.sim.position;
        std::snprintf(buf, sizeof buf, "%zu collision triangles (%zu placed models' bounds); at %.0f %.0f %.0f",
                      _world.size(), _boundsUsed, p[0], p[1], p[2]);
        line(buf, {230, 230, 230});
    }
    if (const int chasing = _life.police().chasing()) {
        const std::string how = chasing > 1 ? " (" + std::to_string(chasing) + ")" : "";
        font::drawText(_overlay, 4, kFrameH - 24, "POLICE CHASING" + how, {255, 90, 90}, 2);
    }
    if (_busted > 0)
        font::drawText(_overlay, kFrameW / 2 - 120, kFrameH / 2 - 40, "BUSTED!", {255, 80, 80}, 6);
    else if (_wrecked > 0)
        font::drawText(_overlay, kFrameW / 2 - 140, kFrameH / 2 - 40, "WRECKED!", {255, 150, 60}, 6);
    if (_hudShown)
        drawSpeedometer(_overlay, _car.sim,
                        _hudMap.loaded() ? int(_hudMap.setup().left * float(kFrameW)) - 8 : kFrameW - 8,
                        _wear.fraction(), _pictures);
}

void DriveViewer::update(uint64_t nowMs) {
    if (!_renderer || _cars.empty())
        return;
    // Still while F1's list of the controls is up.
    const float dt = _lastMs && !_view.controlsList ? std::min(0.1f, float(nowMs - _lastMs) / 1000) : 0;
    _lastMs = nowMs;

    CarControls controls = controlsOf(_keys);
    _pad.apply(controls);
    if (_wear.wrecked())
        controls.throttle = 0; // the engine's dead
    controls.shift = _shift;
    _shift = 0;
    _car.sim.manual = _manual;
    applyControlOptions(_car.sim);
    _car.sim.step(controls, _world, dt);
    // Fallen out of the city (or sunk): back to the start.
    if (_car.sim.position[1] < _scene->psdl().min[1] - 50)
        resetCar();
    _engine.update(_car.sim.rpm, _car.sim.gear > 0 || _manual ? controls.throttle : controls.brake);
    {
        GameMusic::State music;
        music.speed = std::fabs(_car.sim.forwardSpeed());
        music.chased = _life.police().chasing() > 0;
        _music.update(dt, music);
        // A second off the ground: the tune's big-air motif, once a jump.
        {
            const CarSim &air = _car.sim;
            const bool off =
                std::none_of(air.wheels.begin(), air.wheels.end(), [](const CarSim::Wheel &w) { return w.contact; });
            _airFor = off ? _airFor + dt : 0;
            if (_airFor >= kBigAir && !_airPlayed) {
                _music.bigAir();
                _airPlayed = true;
            }
            _airPlayed = _airPlayed && off;
        }
    }
    _car.lamps = {_scene->lampsLit(), controls.brake > 0 && _car.sim.forwardSpeed() > 0.5f, _car.sim.gear < 0};
    _clock += dt;
    _car.siren = _sirenOn ? 1 + int(_clock * 4) % 2 : 0;
    _siren.update(dt, _sirenOn ? 0.9f : 0.0f);
    _life.setLamps(_scene->lampsLit(), _scene->night());
    _life.update(dt, _car.sim, {}, _world, _roads, _fill);
    // What flies out of the props knocked down: coins, papers, leaves.
    for (const Props::Burst &b : _life.takeBursts())
        if (_smoke)
            _smoke->burst(b.rule, b.at, b.velocity, _smoke->look(b.texture));
    _fill = false;
    _sounds.update(dt, _car.sim, (_keys & kDriveHorn) && !_car.hasSiren());
    _wear.update(dt, _car, _smoke.get());
    if (_smoke)
        _smoke->wheels(dt, _car.sim);
    // A knock shakes the game controller.
    if (const float shake = padRumble(_car.sim.impact); shake > 0 && currentOptions().forceFeedback)
        _ctx.sys.rumble(shake, shake * 0.6f, 200);
    _car.sim.impact = 0;
    if (_smoke)
        _smoke->update(dt);
    // Wrecked: a moment to see it, then back to the start, mended.
    if (_wrecked > 0) {
        _wrecked -= dt;
        if (_wrecked <= 0)
            resetCar();
    } else if (_wear.wrecked()) {
        _wrecked = 4;
    }
    // Busted: a moment to see it, then back to the start.
    if (_busted > 0) {
        _busted -= dt;
        if (_busted <= 0) {
            resetCar();
            _life.police().forgive();
        }
    } else if (_life.police().busted()) {
        _busted = 3;
    }

    const CarCamera &cam = _cameras[size_t(_camera)];
    const CameraView view =
        _view.apply(_blend.apply(updateCarCamera(cam, _car.sim, dt, _cameraPos, _cameraReset), dt), cam, _car.sim, dt);
    _lastView = view;
    _cameraReset = false;
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(_view.fov(cam.fov) * kPi / 180, float(kFrameW) / float(kFrameH), cam.nearClip,
                                       currentOptions().farClip()),
                      gfx::lookAlong(view.eye, normalized(view.target - view.eye), view.up));
    _renderer->beginFrame(viewProj, view.eye, {150, 170, 200});
    applyGraphicsOptions(*_scene, _life);
    _scene->draw(viewProj, view.eye, true);
    if (_scene->lampsLit() && headlightPoolsOn()) {
        std::vector<gfx::DynamicLight> pools;
        gfx::DynamicLight mine;
        if (_car.headlightPool(mine))
            pools.push_back(mine);
        _life.headlightPools(pools, view.eye);
        _scene->drawDynamicLights(pools, view.eye);
    }
    _life.draw(*_renderer, _scene->models(), _scene->light(), viewProj, view.eye, _flares.get());
    _scene->drawBeacons(*_flares, view.eye);
    const bool inside = cam.inside && !_view.thrill;
    if (!inside) {
        _car.draw(_scene->light());
        _car.drawFlares(*_flares, view.eye);
    }
    if (_smoke)
        _smoke->draw(view.eye);
    if (inside && _dash.loaded() && _view.showDashboard() && _view.look == 0) {
        _dash.draw(*_renderer, _scene->models(), _car.sim, _wear.fraction(), view.eye,
                   normalized(view.target - view.eye), view.up, float(kFrameW) / float(kFrameH), _scene->light());
    }
    if (_mirror)
        drawRearMirror(*_renderer, _car.sim, currentOptions().farClip(), [&](const gfx::Mat4 &vp, const Vec3 &eye) {
            _scene->draw(vp, eye, true, true);
            _life.draw(*_renderer, _scene->models(), _scene->light(), vp, eye);
        });
    // The map: the player, the police.
    _hudMap.update(dt);
    std::vector<HudMapIcon> icons;
    for (CarSim *c : _life.police().cars())
        icons.push_back({c->position, -c->orientation.z, hudcolor::kPolice, false});
    icons.push_back({_car.sim.position, -_car.sim.orientation.z, hudcolor::kPlayer, false});
    _hudMap.draw(*_renderer, _scene->models(), _car.sim.position, -_car.sim.orientation.z, icons, kFrameW, kFrameH);
    drawHud();
    _cd.update(dt);
    _cd.draw(_overlay, _pictures);
    if (_view.controlsList)
        drawControlsList(_overlay, currentOptions(), [&](Control c) { return controlFallbackName(c); });
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::age
