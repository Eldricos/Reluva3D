// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Race page: the game's Blitz, Circuit and Checkpoint races, from its
// race files (race.h), against opponents driving their paths along the
// roads (opponent.h), and its Crash Course lessons (lesson.h), chosen in
// the game's menus (front_end.h; a menu of its own without them). The
// rules as Reluva plays them are in docs/engines/age-racing.md.

#include "engines/age/viewers.h"

#include "common/font.h"
#include "engines/age/city_collision.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kPi = 3.14159265358979f;
constexpr float kBigAir = 1;                // seconds off the ground that count as big air (Reluva's)
constexpr float kCountdown = 3;             // seconds before the start
constexpr float kCircuitRadius = 15;        // circuits' waypoints give no radius
constexpr float kOpponentReach = 10;        // opponents count a checkpoint this much wider
constexpr float kSkill[2] = {0.85f, 0.95f}; // amateur, professional: opponents' share of top speed
const RaceKind kKinds[] = {RaceKind::Blitz, RaceKind::Circuit, RaceKind::Checkpoint, RaceKind::CrashCourse,
                           RaceKind::Cruise};
constexpr int kKindCount = kRacePageKinds;
constexpr float kMph = 2.23694f;

// Where the game puts its race messages and its list on a 640 x 480
// screen (measured on its own screens): the list's lines 24 apart from
// y 23, its labels green; the countdown and messages over the road at
// y 110; the time after a checkpoint under the car.
constexpr int kListTop = 23, kListStep = 24, kMessageY = 110, kSplitY = 394;
constexpr Color kListLabel{130, 230, 120}, kMessage{255, 230, 120};

// A lap's or a split's time as the game shows it: "0:45:21".
std::string splitText(float seconds) {
    const int hundredths = std::max(0, int(seconds * 100));
    char text[24];
    std::snprintf(text, sizeof text, "%d:%02d:%02d", hundredths / 6000, hundredths / 100 % 60, hundredths % 100);
    return text;
}

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

RaceKind racePageKind(int index) { return kKinds[std::clamp(index, 0, kKindCount - 1)]; }

RaceViewer::RaceViewer(ViewerContext& ctx) : Page(ctx), cities_(findCities(ctx.files)), cars_(findCars(ctx.files)) {}

RaceViewer::~RaceViewer() { deactivate(); }

void RaceViewer::deactivate() {
    net_.reset(); // out of any session
    netGame_ = netMenus_ = garageForNet_ = chatting_ = false;
    showCar_.reset();
    showCarIndex_ = -1;
    life_.stop();
    engine_.stop();
    sounds_.stop();
    music_.stop();
    racers_.clear();
    flares_.reset();
    smoke_.reset();
    hudMap_.unload();
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
    applySoundOptions(loadOptions(ctx_.savePath), ctx_.mixer);
    strings_.load(ctx_.files);
    roster_.load(ctx_.savePath);
    record_.load(ctx_.savePath, roster_.driver().file);
    level_ = roster_.driver().professional ? 1 : 0;
    front_ = std::make_unique<FrontEnd>(ctx_.files, cities_, cars_, strings_, record_);
    if (!front_->loaded())
        front_.reset(); // the text menu instead
    if (front_) {
        net_ = std::make_unique<NetMenus>(ctx_.sys, ctx_.files, cities_, cars_, strings_);
        if (!net_->loaded())
            net_.reset();
        front_->multiplayer = net_ != nullptr;
        front_->setDrivers(&roster_);
        front_->choice().professional = roster_.driver().professional;
        game_ = std::make_unique<GameMenus>(ctx_.sys, ctx_.files, strings_, ctx_.mixer, ctx_.savePath);
        if (!game_->loaded())
            game_.reset();
        else {
            game_->setDrivers(&roster_, &record_, &cities_);
            game_->setFrontEnd(front_.get());
        }
    }
    quit_ = false;
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
    smoke_.reset();
    const std::string& map = cities_[size_t(city_)].map;
    error_ = scene_->load(map).message;
    flares_ = std::make_unique<FlareDrawer>(*renderer_, scene_->textures());
    smoke_ = std::make_unique<SmokePuffs>(*renderer_, scene_->textures());
    smoke_->loadWheelEffects(ctx_.files);
    hudMap_.load(*renderer_, ctx_.files, scene_->models(), map);
    pictures_.load(ctx_.files);
    buildCityCollision(ctx_.files, scene_->psdl(), scene_->mesh(), scene_->instances(), world_);
    roads_.build(scene_->psdl());
    roads_.penalizeBlocked(world_);
    if (Error err = life_.load(ctx_.files, map); err && error_.empty())
        error_ = err.message;
    botRoads_.build(life_.roadMap());
    loadedCity_ = city_;
    raceNames_ = raceNames(ctx_.files, map, kKinds[kind_]);
    raceIndex_ = std::min(raceIndex_, std::max(0, int(raceNames_.size()) - 1));
    // Show where the chosen race starts.
    if (!raceNames_.empty())
        loadRace(ctx_.files, map, kKinds[kind_], raceIndex_, race_);
}

bool RaceViewer::takesKey(const Event& ev) const {
    // In the game's menus Esc goes back (and on the main menu asks to
    // quit); in a race it's the race's.
    // In a race, Tab is the game's too (its Map Toggle), not the next page.
    return ev.type == EventType::KeyDown &&
           (((state_ != State::Menu || netMenus_ || front_) && ev.key == key::Escape) ||
            (state_ != State::Menu && !racers_.empty() && ev.key == key::Tab));
}

bool RaceViewer::raceIsOpen(int index) const {
    if (openAll_)
        return true;
    const std::string& map = cities_[size_t(city_)].map;
    const RaceUnlocking u = raceUnlocking(ctx_.files, map);
    return raceOpen(record_, map, kKinds[kind_], index, level_ == 1, u.group, u.mustPlace);
}

bool RaceViewer::carIsOpen(int car) const {
    if (openAll_)
        return true;
    const SearchSet& files = ctx_.files;
    return unlocked(
        record_, cars_[size_t(car)].base, -1, level_ == 1,
        [&files](const std::string& city, RaceKind kind) { return int(raceNames(files, city, kind).size()); });
}

void RaceViewer::switchDriver() {
    // Another driver: their record, their level.
    record_.load(ctx_.savePath, roster_.driver().file);
    level_ = roster_.driver().professional ? 1 : 0;
    if (front_) {
        front_->choice().professional = roster_.driver().professional;
        front_->refreshChoice(); // for what's open to them
    }
}

void RaceViewer::recordResult() {
    // Once a race: its place (a Blitz finished in time, first), or the
    // lesson passed; saved at once.
    // Not a race run in changed conditions: "the race statistics are
    // recorded for races under default conditions only" (jpg/hoff_dlg).
    if (recorded_ || racers_.empty() || state_ != State::Finished || netGame_ || replaying_ ||
        (conditionsChanged_ && !lesson()))
        return;
    recorded_ = true;
    const std::string& map = cities_[size_t(city_)].map;
    const Racer& player = racers_.front();
    // What it opens, for the results screen: the city's rewards open
    // before the result and after.
    std::vector<Reward> rewards;
    if (std::vector<uint8_t> data; !ctx_.files.read("race/" + map + "/" + map + "_rewards.csv", data))
        rewards = parseRewards(std::string(data.begin(), data.end()));
    const SearchSet& files = ctx_.files;
    const RaceCount count = [&files](const std::string& city, RaceKind kind) {
        return int(raceNames(files, city, kind).size());
    };
    // Each reward is the unlock rule for its car, city and kind (the
    // rewards' paint job numbers don't always match the lock pictures':
    // the fire truck's 2 is its _lck4).
    auto ruleFor = [&](const Reward& r) -> const UnlockRule* {
        const RaceKind kind = r.kind == "blitz"     ? RaceKind::Blitz
                              : r.kind == "circuit" ? RaceKind::Circuit
                              : r.kind == "race"    ? RaceKind::Checkpoint
                                                    : RaceKind::CrashCourse;
        for (const UnlockRule& u : unlockRules())
            if (u.car == r.car && u.city == map && u.kind == kind && (u.paintJob < 0) == (r.paintJob == 0))
                return &u;
        return nullptr;
    };
    auto opened = [&] {
        std::vector<bool> open;
        for (const Reward& r : rewards) {
            const UnlockRule* u = ruleFor(r);
            open.push_back(!u || unlocked(record_, u->car, u->paintJob, level_ == 1, count));
        }
        return open;
    };
    const std::vector<bool> before = opened();
    if (lesson()) {
        if (judge_.result() != LessonJudge::Result::Passed)
            return;
        record_.passed(map, raceIndex_);
    } else {
        if (player.finishTime < 0)
            return;
        int place = 1;
        for (const Racer& r : racers_)
            if (!r.player && r.finishTime >= 0 && r.finishTime < player.finishTime)
                ++place;
        record_.raced(map, race_.kind, raceIndex_, level_ == 1, race_.kind == RaceKind::Blitz ? 1 : place,
                      player.finishTime);
    }
    if (Error err = record_.save(); err && !ctx_.savePath.empty())
        error_ = err.message;
    const std::vector<bool> after = opened();
    for (size_t i = 0; i < rewards.size(); ++i)
        if (after[i] && !before[i])
            unlockMessage_ += (unlockMessage_.empty() ? "" : " ") + rewards[i].message;
}

void RaceViewer::startRace() {
    const std::string& map = cities_[size_t(city_)].map;
    if (raceNames_.empty())
        return;
    if (!replaying_ && (!raceIsOpen(raceIndex_) || !carIsOpen(car_))) {
        error_ = "Locked: see the menu";
        return;
    }
    recorded_ = false;
    if (Error err = loadRace(ctx_.files, map, kKinds[kind_], raceIndex_, race_)) {
        error_ = err.message;
        return;
    }
    error_.clear();
    // The conditions the menus changed (or the replay's): over the race's.
    // A cruise has none of its own: always the menu's.
    const bool changed = replaying_ ? playback_.changed : conditionsChanged_ || race_.kind == RaceKind::Cruise;
    if (changed && !lesson()) {
        RaceSettings& s = race_.settings[size_t(level_)];
        const FrontEnd::Choice::Conditions c =
            replaying_ ? FrontEnd::Choice::Conditions{playback_.laps,      playback_.opponents,
                                                      playback_.timeOfDay, playback_.weather,
                                                      playback_.cops,      playback_.traffic,
                                                      playback_.peds,      false}
                       : conditions_;
        s.laps = c.laps;
        s.opponents = c.opponents;
        s.timeOfDay = c.timeOfDay;
        s.weather = c.weather;
        s.cops = c.cops;
        s.ambient = c.traffic;
        s.peds = c.hidePeds ? 0 : c.peds;
    }
    const RaceSettings& set = race_.settings[size_t(level_)];
    scene_->setLighting(std::clamp(set.timeOfDay, 0, 3) * 4 + std::clamp(set.weather, 0, 3));
    if (race_.kind == RaceKind::Cruise)
        startCruise();

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
    if (Error err =
            player.car->load(*renderer_, scene_->textures(), ctx_.files, cars_[size_t(car_)].base, uint32_t(paint_)))
        error_ = err.message;
    const Waypoint& start = race_.waypoints[0];
    player.car->sim.reset(player.car->setup, start.position + Vec3{0, 0.5f, 0}, headingToYaw(start.heading));
    wear_.load(ctx_.files, cars_[size_t(car_)].base, *player.car);
    if (smoke_)
        smoke_->clear();
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
    // A lesson has the ones its aimap lists, in that order.
    const auto& paths = race_.opponents[size_t(level_)];
    const size_t count = lesson() ? paths.size() : std::min(paths.size(), size_t(std::max(0, set.opponents)));
    for (size_t i = 0; i < count; ++i) {
        std::string base = cars_[(size_t(car_) + 1 + i * 3) % cars_.size()].base;
        float skill = kSkill[level_];
        const std::string opp = "-" + std::to_string(i) + ".opp";
        for (size_t k = 0; k < ai.opponents.size(); ++k) {
            const auto& o = ai.opponents[k];
            const bool mine = lesson() ? k == i
                                       : o.path.size() > opp.size() &&
                                             o.path.compare(o.path.size() - opp.size(), opp.size(), opp) == 0;
            if (mine) {
                base = o.model;
                skill = std::clamp(o.skill, 0.6f, 1.0f);
            }
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
        r.driver.start(routePath(roads_, o.start, o.path, circuit, loopFrom, &botRoads_), circuit ? laps : 0, skill,
                       loopFrom);
        if (!lesson()) {
            std::vector<Vec3> gates;
            for (size_t k = 0; k < targets_.size(); ++k)
                gates.push_back(targetPoint(k).position);
            r.gatePoints = pathPointsNearest(r.driver, gates);
        }
        racers_.push_back(std::move(r));
    }

    // Traffic, pedestrians and police as the race's settings say (a
    // lesson's traffic as its own row says; its police only in a cop
    // chase, all of its aimap's).
    CityLife::Options life;
    life.traffic = lesson() ? race_.lesson.ambient : set.ambient;
    life.peds = set.peds * (replaying_ ? playback_.pedestrians : pedestrianShare());
    life.cops = std::max(0, set.cops);
    if (lesson())
        life.cops = race_.lesson.event == LessonEvent::CopChase ? -1 : 0;
    life.rain = set.weather == 3;
    life.seed = uint32_t(raceIndex_ * 7 + level_ + 1);
    life.variant = raceStem(race_.kind, raceIndex_); // its own bridges and ferries where it has them
    if (life_.loaded()) {
        if (Error err = life_.start(*renderer_, scene_->textures(), ctx_.files, ai, life, ctx_.mixer))
            error_ = err.message;
        // What the city has across the drawbridges' gaps goes: the leaves
        // are the road there.
        auto inGap = [&](const Vec3& a, const Vec3& b, const Vec3& c) { return life_.gizmos().inGap(a, b, c); };
        scene_->hide(inGap);
        world_.removeIf(inGap);
    }
    fill_ = true;

    engine_.stop();
    sounds_.stop();
    if (ctx_.mixer) {
        engine_.start(*ctx_.mixer, ctx_.files, cars_[size_t(car_)].base);
        sounds_.start(*ctx_.mixer, ctx_.files, cars_[size_t(car_)].base, set.weather == 3);
        // The race music: a tune picked by the race (aud/dmusic).
        // A cruise's, one of the cruising tunes.
        if (Error err = music_.start(ctx_.files, *ctx_.mixer, !cruising(),
                                     uint32_t(raceIndex_ * 7 + city_ * 3 + int(race_.kind) + car_)))
            error_ = "music: " + err.message;
    }
    cameras_ = loadCarCameras(ctx_.files, cars_[size_t(car_)].base);
    camera_ = std::min(camera_, int(cameras_.size()) - 1);
    dash_.load(ctx_.files, scene_->models(), scene_->textures(), cars_[size_t(car_)].base);
    cameraReset_ = true;
    clock_ = cruising() ? 0 : -kCountdown; // a cruise is off at once
    outcome_.clear();
    beatenAll_.clear();
    finalLapFor_ = 0;
    lastLap_ = 0;
    lapEnds_.clear();
    splitFor_ = 0;
    targetStill_ = 0;
    unlockMessage_.clear();
    cleared_.assign(targets_.size(), false);
    lastCleared_ = 0;
    picked_ = -1;
    if (lesson())
        judge_.start(race_.lesson);
    keys_ = 0;
    state_ = State::Countdown;
    setTitle(race_.name + " (" + raceKindName(race_.kind) + ", " + cities_[size_t(city_)].name + ")");
    if (!replaying_ && !netGame_) {
        // The last race run (Quick Race's), kept.
        record_.setLastRace(map, race_.kind, raceIndex_);
        record_.save();
    }
    if (!replaying_) {
        // Recorded as it's driven, from here.
        recording_ = Replay{};
        recording_.city = map;
        recording_.kind = int(race_.kind);
        recording_.race = raceIndex_;
        recording_.level = level_;
        recording_.car = cars_[size_t(car_)].base;
        recording_.paint = paint_;
        recording_.autoReverse = currentOptions().autoReverse;
        recording_.steerRate = currentOptions().steerRate();
        recording_.pedestrians = pedestrianShare();
        recording_.changed = changed && !lesson();
        recording_.laps = set.laps;
        recording_.opponents = set.opponents;
        recording_.timeOfDay = set.timeOfDay;
        recording_.weather = set.weather;
        recording_.cops = set.cops;
        recording_.traffic = set.ambient;
        recording_.peds = set.peds;
    }
}

void RaceViewer::endRace() {
    replaying_ = false;
    paused_ = results_ = false;
    if (front_)
        front_->afterRace();
    life_.stop();
    engine_.stop();
    music_.stop();
    racers_.clear();
    state_ = State::Menu;
    scene_->setLighting(4);
}

float RaceViewer::progressOf(const Racer& r) const {
    // Checkpoints reached, and how near the next.
    if (r.next >= targets_.size())
        return float(targets_.size()) + 1 - r.finishTime / 1e5f;
    const float d = flatDistance(r.car->sim.position, targetPoint(targetOf(r)).position);
    return float(r.next) + 1 / (1 + d / 50);
}

bool RaceViewer::anyOrder() const {
    return !netGame_ && !lesson() && (race_.kind == RaceKind::Checkpoint || race_.kind == RaceKind::Blitz) &&
           targets_.size() > 1 && cleared_.size() == targets_.size();
}

std::vector<size_t> RaceViewer::remaining() const {
    // The finish only once the rest are cleared.
    std::vector<size_t> out;
    for (size_t k = 0; k + 1 < targets_.size(); ++k)
        if (!cleared_[k])
            out.push_back(k);
    if (out.empty() && !targets_.empty() && !cleared_.back())
        out.push_back(targets_.size() - 1);
    return out;
}

size_t RaceViewer::targetOf(const Racer& r) const {
    if (!r.player || !anyOrder() || r.next >= targets_.size())
        return r.next;
    const std::vector<size_t> left = remaining();
    if (left.empty())
        return targets_.size();
    if (picked_ >= 0 && size_t(picked_) < cleared_.size() && !cleared_[size_t(picked_)] &&
        std::find(left.begin(), left.end(), size_t(picked_)) != left.end())
        return size_t(picked_);
    // The nearest.
    size_t best = left.front();
    float nearest = 1e30f;
    for (size_t k : left)
        if (const float d = flatDistance(r.car->sim.position, targetPoint(k).position); d < nearest) {
            nearest = d;
            best = k;
        }
    return best;
}

void RaceViewer::pickCheckpoint(int step) {
    // The arrow to the next (or the one before) in the list of those left.
    if (!anyOrder() || racers_.empty())
        return;
    const std::vector<size_t> left = remaining();
    if (left.empty())
        return;
    const size_t now = targetOf(racers_.front());
    const auto at = std::find(left.begin(), left.end(), now);
    const size_t i = at == left.end() ? 0 : size_t(at - left.begin());
    picked_ = int(left[(i + left.size() + (step > 0 ? 1 : left.size() - 1)) % left.size()]);
}

void RaceViewer::stepRace(float dt) {
    const bool wasStarted = clock_ >= 0;
    if (netGame_) {
        // The host's clock; waiting for everyone to load, before the
        // countdown.
        NetSession* session = net_ ? net_->session() : nullptr;
        clock_ = session && session->counting() ? session->clock(nowMs_) : -kCountdown - 1;
    } else
        clock_ += dt;
    const bool started = clock_ >= 0;
    if (lesson() && started && !wasStarted && race_.lesson.event == LessonEvent::CopChase)
        life_.police().chaseAll(); // after the player from the start
    bool checkpoint = false;       // the player reached one this frame
    float impact = 0;
    for (Racer& r : racers_) {
        if (r.peer != kNoPlayer) {
            driveRemote(r, dt);
            continue;
        }
        CarSim& sim = r.car->sim;
        CarControls c;
        if (started) {
            if (r.player) {
                // Driving on, unless finished (a cruise has no end, and the
                // stunt ends once stopped).
                c = r.next < targets_.size() || targets_.empty() || stunt() ? controlsOf(chatting_ ? 0 : keys_)
                                                                            : CarControls{0, 1, 0, false};
                if (wear_.wrecked())
                    c.throttle = 0; // the engine's dead
                c.shift = shift_;
                shift_ = 0;
                sim.manual = manual_;
                if (replaying_) {
                    sim.autoReverse = playback_.autoReverse;
                    sim.steerRate = playback_.steerRate;
                } else
                    applyControlOptions(sim);
            } else {
                if (r.botId != kNoPlayer && net_ && net_->session() &&
                    !NetMenus::hasRace(net_->session()->settings().mode))
                    planBot(r, dt, netCars());
                c = r.driver.drive(sim, dt);
            }
        } else {
            c.brake = 0; // waiting on the line
        }
        r.throttle = sim.gear > 0 || sim.manual ? c.throttle : c.brake;
        sim.step(c, world_, dt);
        r.car->lamps = {scene_->lampsLit(), c.brake > 0 && sim.forwardSpeed() > 0.5f, sim.gear < 0};
        // (A computer player outside a race is put back on the road by its
        // own mind: planBot.)
        const bool minded =
            r.botId != kNoPlayer && net_ && net_->session() && !NetMenus::hasRace(net_->session()->settings().mode);
        // The car to destroy stays where it's been rammed to.
        const bool target = lesson() && race_.lesson.event == LessonEvent::Destroy && &r == &racers_[1];
        if (!r.player && !minded && !target &&
            (r.driver.wantsReset() || sim.position[1] < scene_->psdl().min[1] - 50)) {
            sim.reset(r.car->setup, r.driver.resetPoint() + Vec3{0, 0.5f, 0}, r.driver.resetYaw());
            r.driver.resetDone();
            ++r.resets;
            r.putBackAt = clock_;
        }
        if (smoke_)
            smoke_->wheels(dt, sim);
        if (r.player) {
            engine_.update(sim.rpm, sim.gear > 0 || sim.manual ? c.throttle : c.brake);
            sounds_.update(dt, sim, started && (keys_ & kDriveHorn));
            wear_.update(dt, *r.car, smoke_.get());
            impact = sim.impact;
            sim.impact = 0;
            if (sim.position[1] < scene_->psdl().min[1] - 50) { // fallen out: back to the last checkpoint
                const size_t last = anyOrder() ? lastCleared_ : r.next - 1;
                const Waypoint& back = r.next > 0 ? targetPoint(last) : race_.waypoints[0];
                sim.reset(r.car->setup, back.position + Vec3{0, 0.5f, 0}, headingToYaw(back.heading));
                ++r.resets;
                r.putBackAt = clock_;
            }
        }
        // Checkpoints.
        if (started && r.player && anyOrder() && r.next < targets_.size()) {
            // Any of those left, the finish last.
            for (size_t k : remaining()) {
                const Waypoint& w = targetPoint(k);
                const float radius = (w.radius > 0 ? w.radius : kCircuitRadius) + 2;
                if (flatDistance(sim.position, w.position) < radius &&
                    std::fabs(sim.position[1] - w.position[1]) < 10) {
                    cleared_[k] = true;
                    lastCleared_ = k;
                    checkpoint = true;
                    if (++r.next == targets_.size())
                        r.finishTime = clock_;
                    break;
                }
            }
        } else if (started && r.next < targets_.size()) {
            const Waypoint& w = targetPoint(r.next);
            const float radius = (w.radius > 0 ? w.radius : kCircuitRadius) + (r.player ? 2 : kOpponentReach);
            // An opponent also once it's gone on past where its own path
            // is nearest the checkpoint (some paths take another way).
            const bool byPath = r.next < r.gatePoints.size() && r.driver.passed() > r.gatePoints[r.next];
            if (byPath ||
                (flatDistance(sim.position, w.position) < radius && std::fabs(sim.position[1] - w.position[1]) < 10)) {
                checkpoint = checkpoint || r.player;
                if (++r.next == targets_.size())
                    r.finishTime = clock_;
            }
        }
    }
    for (size_t i = 0; i < racers_.size(); ++i)
        for (size_t j = i + 1; j < racers_.size(); ++j)
            collideCars(racers_[i].car->sim, racers_[j].car->sim);
    if (smoke_)
        smoke_->update(dt);
    std::vector<CarSim*> others;
    for (size_t i = 1; i < racers_.size(); ++i)
        others.push_back(&racers_[i].car->sim);
    life_.setLamps(scene_->lampsLit(), scene_->night());
    life_.update(dt, racers_.front().car->sim, others, world_, roads_, fill_);
    // What flies out of the props knocked down: coins, papers, leaves.
    for (const Props::Burst& b : life_.takeBursts())
        if (smoke_)
            smoke_->burst(b.rule, b.at, b.velocity, smoke_->look(b.texture));
    fill_ = false;

    if (state_ == State::Countdown && started)
        state_ = State::Racing;
    // A checkpoint passed: the time so far, a lap's own at its end.
    splitFor_ = std::max(0.0f, splitFor_ - dt);
    if (checkpoint && !lesson() && !racers_.empty()) {
        const Racer& player = racers_.front();
        const size_t perLap = race_.waypoints.size();
        splitIsLap_ = race_.kind == RaceKind::Circuit && perLap && player.next % perLap == 0;
        const float lapStart = lapEnds_.empty() ? 0 : lapEnds_.back();
        split_ = race_.kind == RaceKind::Circuit ? clock_ - lapStart : clock_;
        if (splitIsLap_)
            lapEnds_.push_back(clock_);
        splitFor_ = 3;
    }
    if (netGame_) {
        stepNet(dt);
        return;
    }
    // Into a circuit's last lap: "Final lap!" for a moment.
    finalLapFor_ = std::max(0.0f, finalLapFor_ - dt);
    if (race_.kind == RaceKind::Circuit && !racers_.empty()) {
        const size_t perLap = race_.waypoints.size();
        const int laps = std::max(1, race_.settings[size_t(level_)].laps);
        const int lap = perLap ? int(racers_.front().next / perLap) : 0;
        if (lap != lastLap_ && lap == laps - 1 && laps > 1)
            finalLapFor_ = 3;
        lastLap_ = lap;
    }
    if (lesson()) {
        judgeLesson(dt, checkpoint, impact);
        recordResult();
        return;
    }
    if (race_.kind == RaceKind::Cruise) {
        stepCruise(dt);
        return;
    }
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
        // The game's words: "You Won!", "You finished 2nd", "You are a loaf"
        // past eighth.
        static const char* const kPlaces[] = {"1st!", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
        if (race_.kind == RaceKind::Blitz)
            outcome_ = raceMessage(6, "You Won!") + "  " + clockText(player.finishTime);
        else if (place <= 8)
            outcome_ = raceMessage(3 + place, std::string("You finished ") + kPlaces[place - 1]) + "  " +
                       clockText(player.finishTime);
        else
            outcome_ = raceMessage(12, "You are a loaf");
        state_ = State::Finished;
    } else if (race_.kind == RaceKind::Blitz && set.timeLimit > 0 && clock_ > set.timeLimit) {
        outcome_ = raceMessage(3, "Time's up!");
        state_ = State::Finished;
    } else if (life_.police().busted()) {
        // Blitz and Checkpoint groups have their "Game over!"; Circuit's none.
        const int over = race_.kind == RaceKind::Blitz ? 4 : race_.kind == RaceKind::Checkpoint ? 3 : -1;
        outcome_ = "Busted! " + (over >= 0 ? raceMessage(over, "Game over!") : std::string("Game over!"));
        state_ = State::Finished;
    } else if (wear_.wrecked()) {
        outcome_ = "Wrecked! Game over!";
        state_ = State::Finished;
    }
    const bool justFinished = state_ == State::Finished && !recorded_;
    recordResult();
    // Every race of the kind won (at this level): the game's word for it.
    if (justFinished && race_.kind != RaceKind::Checkpoint) {
        const std::string& map = cities_[size_t(city_)].map;
        bool all = !raceNames_.empty();
        for (int i = 0; i < int(raceNames_.size()) && all; ++i)
            all = record_.bestPlace(map, race_.kind, i, level_ == 1) == 1;
        if (all)
            beatenAll_ = raceMessage(race_.kind == RaceKind::Blitz ? 5 : 13, "");
    }
}

std::string RaceViewer::raceMessage(int offset, const std::string& fallback) const {
    // Each event's group (mmlang.dll): Blitz from 158 (Ready, Set, Go,
    // Time's up, Game over, all beaten, You Won!), Circuit from 165
    // (Ready, Set, Go, a penalty, finished 1st-8th, a loaf, all beaten),
    // Checkpoint from 179 (Ready, Set, Go, Game over, finished 1st-8th, a
    // loaf).
    // The stunt's from 200 (to the end of the pier, don't fall in, Go!).
    const int base = race_.kind == RaceKind::Blitz     ? 158
                     : race_.kind == RaceKind::Circuit ? 165
                     : race_.kind == RaceKind::Cruise  ? 200
                                                       : 179;
    return strings_.get(uint32_t(base + offset), fallback);
}

void RaceViewer::drawCentred(const std::string& text, int y, Color color, int scale) {
    const int w = int(text.size()) * font::kAdvance * scale;
    font::drawText(overlay_, (kFrameW - w) / 2, y, text, color, scale);
}

void RaceViewer::judgeLesson(float dt, bool checkpoint, float impact) {
    const Racer& player = racers_.front();
    LessonJudge::Frame f;
    f.dt = dt;
    f.clock = clock_;
    f.checkpoint = checkpoint;
    f.finished = player.next >= targets_.size();
    f.speedMph = std::fabs(player.car->sim.forwardSpeed()) * kMph;
    f.impact = std::max(impact, player.car->sim.impact); // against the city, and the traffic since
    f.carTouches = life_.carTouches();
    f.chasing = life_.police().chasing();
    f.busted = life_.police().busted();
    f.wrecked = wear_.wrecked();
    // The car to follow: the first opponent.
    if (racers_.size() > 1) {
        f.targetDistance = flatDistance(player.car->sim.position, racers_[1].car->sim.position);
        f.targetDone = racers_[1].driver.finished();
        // Rammed into submission: turned over, or held stopped 2 s.
        const CarSim& target = racers_[1].car->sim;
        targetStill_ = clock_ > 1 && length(target.velocity) < 1 ? targetStill_ + dt : 0;
        f.targetStopped = target.orientation.y[1] < 0.5f || targetStill_ >= 2;
    }
    f.police = int(life_.police().count());
    const auto before = judge_.result();
    const auto now = judge_.step(f);
    if (state_ != State::Finished && now != before && now != LessonJudge::Result::Going) {
        outcome_ = now == LessonJudge::Result::Passed ? "Lesson passed" : "Lesson failed";
        if (now == LessonJudge::Result::Passed && player.finishTime < 0)
            racers_.front().finishTime = clock_;
        state_ = State::Finished;
    }
}

void RaceViewer::handleEvent(const Event& ev) {
    if (!renderer_)
        return;
    if (help_ && help_->isOpen()) {
        help_->handle(ev);
        return;
    }
    if (game_ && game_->isOpen()) {
        gameMenuEvent(ev);
        return;
    }
    if (netEvent(ev))
        return;
    if (ev.type == EventType::KeyUp) {
        keys_ &= ~driveKeyOf(ev.key);
        return;
    }
    if (ev.type == EventType::MouseMove) {
        if (state_ == State::Menu && front_)
            menuEvent(ev); // pointed at
        return;
    }
    if (ev.type == EventType::MouseDown) {
        // A click: on the game's menus, the race's pause menu, results or
        // replay bar.
        if (ev.button != MouseButton::Left)
            return;
        if (state_ == State::Menu) {
            if (front_)
                menuEvent(ev);
        } else if (replaying_)
            replayEvent(ev);
        else if (results_)
            resultsEvent(ev);
        else if (paused_)
            pauseEvent(ev);
        return;
    }
    if (ev.type != EventType::KeyDown)
        return;
    if (state_ == State::Menu && front_) {
        menuEvent(ev);
        return;
    }
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
            case 1:
                kind_ = wrap(kind_ + step, kKindCount);
                break;
            case 2:
                raceIndex_ = wrap(raceIndex_ + step, int(raceNames_.size()));
                break;
            case 3:
                level_ = wrap(level_ + step, 2);
                break;
            case 4:
                car_ = wrap(car_ + step, int(cars_.size()));
                break;
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
        } else if (ev.key == 'u') {
            openAll_ = !openAll_;
            error_.clear();
        }
        return;
    }
    if (replaying_) {
        replayEvent(ev);
        return;
    }
    if (results_) {
        resultsEvent(ev);
        return;
    }
    if (paused_) {
        pauseEvent(ev);
        return;
    }
    if (!netGame_ && ev.key == key::Escape) {
        // Esc stops the race: the game's in-race menu.
        paused_ = true;
        pauseFocus_ = 0;
        keys_ = 0;
        return;
    }
    if (!netGame_ && state_ == State::Finished && (ev.key == key::Return || ev.key == key::KpEnter)) {
        results_ = true; // the game's results screen
        resultsFocus_ = nextRaceOpen() ? 1 : 0;
        return;
    }
    if (state_ == State::Finished && !netGame_ && ev.key == 'i' && !recording_.empty()) {
        instantReplay(ReplayReturn::Results); // the instant replay
        return;
    }
    if (state_ == State::Finished && lesson() && (ev.key == 'r' || ev.key == key::Return || ev.key == key::KpEnter)) {
        // Restart the lesson (R), or (Enter) go on to the next once passed.
        const bool next =
            ev.key != 'r' && judge_.result() == LessonJudge::Result::Passed && raceIndex_ + 1 < int(raceNames_.size());
        if (ev.key == 'r' || next) {
            raceIndex_ += next ? 1 : 0;
            life_.stop();
            startRace();
            return;
        }
    }
    if (ev.key == key::Escape || (state_ == State::Finished && (ev.key == key::Return || ev.key == key::KpEnter))) {
        endRace();
        return;
    }
    const Control control = controlOf(ev.key);
    if (const uint32_t d = driveKeyOf(ev.key)) {
        keys_ |= d;
    } else if (control == Control::ChangeCamera) {
        camera_ = (camera_ + 1) % int(cameras_.size());
        cameraReset_ = true;
        blend_.start(lastView_, cameras_[size_t(camera_)].blendTime);
    } else if (control == Control::MapZoom) {
        hudMap_.toggleZoom();
    } else if (control == Control::FullScreenMap) {
        hudMap_.toggleFullScreen(); // in a race alone, it stops the race (see update)
        keys_ = 0;
    } else if (control == Control::MapToggle) {
        hudMap_.toggleShown();
    } else if (control == Control::HudToggle) {
        hudShown_ = !hudShown_;
    } else if (control == Control::RearViewMirror) {
        mirror_ = !mirror_;
    } else if (control == Control::NextCheckpoint || control == Control::PrevCheckpoint) {
        pickCheckpoint(control == Control::NextCheckpoint ? 1 : -1);
    } else if (control == Control::Transmission) {
        manual_ = !manual_;
    } else if (control == Control::ShiftUp || control == Control::ShiftDown) {
        shift_ = control == Control::ShiftUp ? 1 : -1;
    }
}

void RaceViewer::menuEvent(const Event& ev) {
    const FrontEnd::Action action = front_->handle(ev);
    if (action == FrontEnd::Action::Minimise) {
        ctx_.sys.minimizeWindow();
        return;
    }
    if (action == FrontEnd::Action::Help) {
        // The game's help file, read the first time it's asked for.
        if (!help_) {
            help_ = std::make_unique<HelpViewer>();
            if (Error err = help_->load(ctx_.files))
                error_ = err.message;
        }
        help_->open();
        return;
    }
    if (action == FrontEnd::Action::Replays && game_) {
        game_->open(GameMenus::Screen::ReplayList);
        return;
    }
    if (game_ && (action == FrontEnd::Action::NewDriver || action == FrontEnd::Action::DeleteDriver ||
                  action == FrontEnd::Action::DriverStats || action == FrontEnd::Action::RaceRecords)) {
        game_->open(action == FrontEnd::Action::NewDriver      ? GameMenus::Screen::NewDriver
                    : action == FrontEnd::Action::DeleteDriver ? GameMenus::Screen::DeleteDriver
                    : action == FrontEnd::Action::DriverStats  ? GameMenus::Screen::DriverStats
                                                               : GameMenus::Screen::RaceRecords);
        if (const std::string clip = game_->takeVoice(); !clip.empty())
            playVoice(clip); // "You cannot delete your only driver"
        return;
    }
    if (action == FrontEnd::Action::DriverChanged) {
        switchDriver();
        return;
    }
    if (game_ && (action == FrontEnd::Action::RaceOptions || action == FrontEnd::Action::CityChoice ||
                  action == FrontEnd::Action::Locked)) {
        game_->open(action == FrontEnd::Action::RaceOptions  ? GameMenus::Screen::RaceOptions
                    : action == FrontEnd::Action::CityChoice ? GameMenus::Screen::CitySelection
                                                             : GameMenus::Screen::Locked);
        if (const std::string clip = game_->takeVoice(); !clip.empty())
            playVoice(clip);
        return;
    }
    openAll_ = front_->openAll;
    if (action == FrontEnd::Action::Options || action == FrontEnd::Action::Quit) {
        if (game_)
            game_->open(action == FrontEnd::Action::Options ? GameMenus::Screen::Options : GameMenus::Screen::Quit);
        else if (action == FrontEnd::Action::Quit)
            quit_ = true;
        return;
    }
    if (action == FrontEnd::Action::Multiplayer && net_) {
        netMenus_ = true;
        net_->enter();
        return;
    }
    if (garageForNet_) {
        // The lobby's car: chosen (GO DRIVE), or left as it was (Esc).
        const FrontEnd::Choice& c = front_->choice();
        if (action == FrontEnd::Action::Start || front_->screen() != FrontEnd::Screen::Garage) {
            garageForNet_ = false;
            netMenus_ = true;
            if (action == FrontEnd::Action::Start)
                net_->carChosen(cars_[size_t(c.car)].base, c.paintJob);
            else
                net_->showLobby();
        }
        return;
    }
    if (action != FrontEnd::Action::Start)
        return;
    // The menus' choice: the city (loaded now if it's another), the race,
    // the level, the car, its colour and gearbox.
    const FrontEnd::Choice& c = front_->choice();
    conditions_ = c.conditions;
    conditionsChanged_ = c.changed && c.kind != RaceKind::CrashCourse;
    for (int k = 0; k < kKindCount; ++k)
        if (kKinds[k] == c.kind)
            kind_ = k;
    level_ = c.professional ? 1 : 0;
    car_ = c.car;
    paint_ = c.paintJob;
    manual_ = c.manual;
    if (city_ != c.city) {
        city_ = c.city;
        loadCity();
    }
    raceNames_ = raceNames(ctx_.files, cities_[size_t(city_)].map, kKinds[kind_]);
    raceIndex_ = std::clamp(c.race, 0, std::max(0, int(raceNames_.size()) - 1));
    showCar_.reset();
    showCarIndex_ = -1;
    startRace();
}

void RaceViewer::drawGarageCar(float dt) {
    // The chosen car turning in the garage's window, on dark blue.
    const FrontEnd::Rect r = front_->carWindow();
    const FrontEnd::Choice& c = front_->choice();
    if (!showCar_ || showCarIndex_ != c.car || showPaint_ != c.paintJob) {
        showCar_ = std::make_unique<CarActor>();
        if (showCar_->load(*renderer_, scene_->textures(), ctx_.files, cars_[size_t(c.car)].base,
                           uint32_t(c.paintJob))) {
            showCar_.reset();
            return;
        }
        showCarIndex_ = c.car;
        showPaint_ = c.paintJob;
    }
    orbit_ += dt * 0.5f;
    showCar_->sim.reset(showCar_->setup, {0, 0, 0}, orbit_);
    const bool show = front_->screen() == FrontEnd::Screen::Showcase;
    const Vec3 eye = show ? Vec3{0, 2.4f, 13.0f} : Vec3{0, 2.2f, 7.5f}, at{0, 0.7f, 0};
    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(35 * kPi / 180, float(r.w) / float(r.h), 0.1f, 100),
                                             gfx::lookAlong(eye, normalized(at - eye), {0, 1, 0}));
    renderer_->setView(viewProj, eye);
    renderer_->setViewport(r.x, r.y, r.w, r.h);
    SceneLight light;
    showCar_->draw(light);
    renderer_->setViewport(0, 0, 0, 0);
}

void RaceViewer::drawCheckpoints() {
    // The game's gates: pt_check (pt_finish at the finish line) is a whole
    // gate, two posts and a banner, 2 across and 1 high about its middle,
    // scaled to the checkpoint (as on the game's screens: across the road,
    // half as high), across the way its heading points. Lessons' have the
    // Crash Course banner (their second paint job). The next two, or in a
    // race in any order all those left.
    if (racers_.empty())
        return;
    const Racer& player = racers_.front();
    std::vector<size_t> gates;
    if (anyOrder())
        gates = remaining();
    else
        for (size_t k = player.next; k < targets_.size() && k < player.next + 2; ++k)
            gates.push_back(k);
    for (size_t k : gates) {
        const bool finish = k + 1 == targets_.size() || targets_[k] == 0;
        const ModelCache::Model& gate = scene_->models().get(finish ? "pt_finish" : "pt_check", lesson() ? 1 : 0);
        if (!gate.ok)
            continue;
        const Waypoint& w = targetPoint(k);
        const float radius = w.radius > 0 ? w.radius : kCircuitRadius;
        const Mat3 turn = Mat3::rotationY(headingToYaw(w.heading));
        const Mat3 sized{turn.x * radius, turn.y * radius, turn.z * radius};
        // Opaque, then its see-through parts (the gate's texture has alpha).
        for (bool translucent : {false, true})
            drawModelMesh(*renderer_, gate.levels.front(), matrixOf(sized, w.position + Vec3{0, radius / 2, 0}),
                          scene_->light(), translucent);
    }
}

void RaceViewer::drawArrow() {
    // The game's HUD arrow (hudarrow01, hudarrow_blitz01 in a Blitz) above
    // the car, pointing to the next checkpoint (in any order, the nearest
    // or the one picked).
    const Racer& player = racers_.front();
    const size_t k = targetOf(player);
    if (k >= targets_.size())
        return;
    // No way to point while there's a car to destroy (as on the game's
    // screens): the car's marker shows it.
    if (lesson() && race_.lesson.event == LessonEvent::Destroy && !judge_.destroyed())
        return;
    drawArrowTo(targetPoint(k).position, race_.kind == RaceKind::Blitz);
}

void RaceViewer::drawArrowTo(const Vec3& target, bool blitz) {
    const Racer& player = racers_.front();
    const Vec3 at = player.car->sim.position + Vec3{0, 2.8f, 0};
    Vec3 to = target - at;
    to[1] = 0;
    // Its second paint job (yellow) when the way is behind the car, as the
    // game's loading tips say.
    Vec3 ahead = -player.car->sim.orientation.z;
    ahead[1] = 0;
    const bool behind = dot(ahead, to) < 0;
    const ModelCache::Model& arrow = scene_->models().get(blitz ? "hudarrow_blitz01" : "hudarrow01", behind ? 1 : 0);
    if (!arrow.ok)
        return;
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

void RaceViewer::drawOpponentMarkers() {
    // Over each opponent the game's marker: a purple triangle pointing down
    // at it and, once the race is on, its place in opp_icon's digits above
    // (measured on the game's screens: the triangle 1.5 m across, 2.3 m
    // tall, its point 4.5 m over the car; the digit as wide, over it).
    constexpr float kTip = 4.5f, kTop = 6.8f, kHalfWidth = 0.75f, kDigit = 8.0f, kDigitSize = 1.4f;
    constexpr uint8_t kPurple[3] = {192, 16, 255};
    std::vector<const Racer*> order;
    for (const Racer& r : racers_)
        order.push_back(&r);
    std::stable_sort(order.begin(), order.end(),
                     [&](const Racer* a, const Racer* b) { return progressOf(*a) > progressOf(*b); });
    struct Marker {
        float depth, x, tipY, topY, pixelsPerMetre;
        int place;
    };
    std::vector<Marker> markers;
    for (size_t i = 0; i < order.size(); ++i) {
        const Racer& r = *order[i];
        if (r.player)
            continue;
        const Vec3 at = r.car->sim.position;
        const Vec3 tip = gfx::transformPoint(viewProj_, at + Vec3{0, kTip, 0});
        const Vec3 top = gfx::transformPoint(viewProj_, at + Vec3{0, kTop, 0});
        if (tip[2] <= 0 || tip[2] >= 1 || top[2] <= 0 || top[2] >= 1)
            continue; // behind the eye, or too far
        auto px = [](float ndc, int size) { return (ndc * 0.5f + 0.5f) * float(size); };
        const float tipY = float(kFrameH) - px(tip[1], kFrameH), topY = float(kFrameH) - px(top[1], kFrameH);
        markers.push_back({tip[2], px(tip[0], kFrameW), tipY, topY, (tipY - topY) / (kTop - kTip), int(i + 1)});
    }
    // The farthest first, so the nearer go over them.
    std::sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.depth > b.depth; });
    for (const Marker& m : markers) {
        if (m.x < -200 || m.x > kFrameW + 200 || m.pixelsPerMetre <= 0)
            continue;
        const float half = kHalfWidth * m.pixelsPerMetre;
        for (int y = std::max(0, int(m.topY)); y < std::min(kFrameH, int(m.tipY)); ++y) {
            const float w = half * (m.tipY - float(y)) / std::max(1.0f, m.tipY - m.topY);
            uint8_t* row = overlay_.row(y);
            for (int x = std::max(0, int(m.x - w)); x < std::min(kFrameW, int(m.x + w) + 1); ++x) {
                uint8_t* d = row + x * 4;
                d[0] = kPurple[0];
                d[1] = kPurple[1];
                d[2] = kPurple[2];
                d[3] = 255;
            }
        }
        if (clock_ >= 0 && m.place <= 8)
            pictures_.place(overlay_, m.place, int(m.x), int(m.tipY - (kDigit - kTip) * m.pixelsPerMetre),
                            std::max(4, int(kDigitSize * m.pixelsPerMetre)));
    }
}

void RaceViewer::drawCheckpointMarkers() {
    // In a race in any order, over each checkpoint left a light blue
    // diamond, long and narrow (measured on the game's screens: about
    // 2.2 m across, 5.5 m tall, 13 m up), drawn over everything.
    if (!anyOrder())
        return;
    constexpr float kMiddle = 13, kHalfHeight = 2.75f, kHalfWidth = 1.1f, kWidest = 0.4f;
    constexpr uint8_t kBlue[3] = {10, 230, 240};
    for (size_t k : remaining()) {
        const Vec3 at = targetPoint(k).position;
        const Vec3 top = gfx::transformPoint(viewProj_, at + Vec3{0, kMiddle + kHalfHeight, 0});
        const Vec3 bottom = gfx::transformPoint(viewProj_, at + Vec3{0, kMiddle - kHalfHeight, 0});
        if (top[2] <= 0 || top[2] >= 1 || bottom[2] <= 0 || bottom[2] >= 1)
            continue;
        const float x = (top[0] * 0.5f + 0.5f) * float(kFrameW);
        const float y0 = float(kFrameH) - (top[1] * 0.5f + 0.5f) * float(kFrameH);
        const float y1 = float(kFrameH) - (bottom[1] * 0.5f + 0.5f) * float(kFrameH);
        const float tall = y1 - y0;
        if (tall <= 0 || x < -50 || x > kFrameW + 50)
            continue;
        const float half = std::max(1.0f, kHalfWidth * tall / (2 * kHalfHeight));
        const float widestY = y0 + tall * kWidest;
        for (int y = std::max(0, int(y0)); y < std::min(kFrameH, int(y1) + 1); ++y) {
            const float fy = float(y);
            const float w = fy < widestY ? half * (fy - y0) / std::max(1.0f, widestY - y0)
                                         : half * (y1 - fy) / std::max(1.0f, y1 - widestY);
            uint8_t* row = overlay_.row(y);
            for (int xx = std::max(0, int(x - w)); xx < std::min(kFrameW, int(x + w) + 1); ++xx) {
                uint8_t* d = row + xx * 4;
                d[0] = kBlue[0];
                d[1] = kBlue[1];
                d[2] = kBlue[2];
                d[3] = 255;
            }
        }
    }
}

void RaceViewer::drawMap(float dt) {
    // The checkpoints still to reach (the next green, the finish its own),
    // the opponents, the police and the player.
    hudMap_.update(dt);
    const Racer& player = racers_.front();
    std::vector<HudMapIcon> icons;
    std::vector<size_t> left;
    if (anyOrder())
        left = remaining();
    else
        for (size_t i = player.next; i < targets_.size(); ++i)
            left.push_back(i);
    const size_t aim = targetOf(player);
    for (size_t i : left)
        icons.push_back({targetPoint(i).position,
                         {0, 0, -1},
                         i + 1 == targets_.size() ? hudcolor::kFinish
                         : i == aim               ? hudcolor::kNext
                                                  : hudcolor::kCheckpoint,
                         true});
    for (CarSim* c : life_.police().cars())
        icons.push_back({c->position, -c->orientation.z, hudcolor::kPolice, false});
    if (netGame_ && net_ && net_->session() && net_->session()->settings().mode == NetMode::CopsRobbers) {
        // The gold, and the bases: ours the finish's colour.
        const NetSession& session = *net_->session();
        const NetPlayer* me = session.me();
        GoldGame::Car self{session.myId(), me ? me->team : uint8_t(0), {}, {}};
        icons.push_back({gold_.goldPosition(), {0, 0, -1}, hudcolor::kNext, true});
        for (uint8_t side = 0; side < (gold_.teams() ? 2 : 1); ++side)
            icons.push_back({gold_.base(side),
                             {0, 0, -1},
                             side == gold_.sideOf(self) || !gold_.teams() ? hudcolor::kFinish : hudcolor::kCheckpoint,
                             true});
    }
    for (const Racer& r : racers_)
        if (!r.player)
            icons.push_back({r.car->sim.position, -r.car->sim.orientation.z, hudcolor::kOpponent, false});
    const CarSim& sim = player.car->sim;
    icons.push_back({sim.position, -sim.orientation.z, hudcolor::kPlayer, false});
    hudMap_.draw(*renderer_, scene_->models(), sim.position, -sim.orientation.z, icons, kFrameW, kFrameH);
}

void RaceViewer::drawMenu() {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    const int lh = font::kLineHeight * 2;
    int y = 60;
    font::drawText(overlay_, 40, 20, "RACE", {255, 210, 80}, 3);
    const std::string kind = raceKindName(kKinds[kind_]);
    std::string raceName = raceNames_.empty() ? "(none)" : raceNames_[size_t(raceIndex_)];
    const std::string& map = cities_[size_t(city_)].map;
    if (!raceNames_.empty()) {
        // Locked, or what the player's done in it.
        static const char* const kPlaces[] = {"", "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
        const int best = record_.bestPlace(map, kKinds[kind_], raceIndex_, level_ == 1);
        if (!raceIsOpen(raceIndex_))
            raceName += "  [locked]";
        else if (lesson() ? record_.hasPassed(map, raceIndex_) : best > 0)
            raceName += lesson() ? "  (passed)" : std::string("  (best: ") + kPlaces[std::min(best, 8)] + ")";
    }
    const bool carOpen = carIsOpen(car_);
    const std::string lines[5] = {"City:   " + cities_[size_t(city_)].name, "Event:  " + kind, "Race:   " + raceName,
                                  std::string("Level:  ") + (level_ ? "Professional" : "Amateur"),
                                  "Car:    " + cars_[size_t(car_)].description + (carOpen ? "" : "  [locked]")};
    for (int i = 0; i < 5; ++i) {
        const Color c = i == field_ ? Color{255, 255, 120} : Color{230, 230, 230};
        font::drawText(overlay_, 40, y, (i == field_ ? "> " : "  ") + lines[i], c, 2);
        y += lh + 4;
    }
    if (!carOpen)
        for (const UnlockRule& r : unlockRules())
            if (r.car == cars_[size_t(car_)].base && r.paintJob < 0) {
                font::drawText(overlay_, 40, y, unlockText(r, level_ == 1), {255, 230, 120});
                y += font::kLineHeight + 4;
            }
    if (!raceNames_.empty() && lesson()) {
        static const char* const kAsks[] = {"Take the jump: every checkpoint in time",
                                            "",
                                            "Follow that car, and don't let it escape",
                                            "Lose the police before the finish",
                                            "Keep up the speed at every checkpoint",
                                            "Through the traffic without touching a car",
                                            "",
                                            "Every checkpoint in time",
                                            "Drive cleanly: each knock costs 5 seconds",
                                            "Find your way to every checkpoint in time"};
        const int e = int(race_.lesson.event);
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s; %zu checkpoints, time limit %.0f s",
                      e >= 0 && e < 10 && kAsks[e][0] ? kAsks[e] : "Every checkpoint", race_.waypoints.size() - 1,
                      race_.lesson.timeLimit);
        font::drawText(overlay_, 40, y + 10, buf, {180, 180, 180});
    } else if (!raceNames_.empty()) {
        const RaceSettings& s = race_.settings[size_t(level_)];
        char buf[200];
        std::snprintf(buf, sizeof buf, "%zu checkpoints, %d opponents, %d laps, time limit %.0f s",
                      race_.waypoints.size() - 1, s.opponents, race_.kind == RaceKind::Circuit ? s.laps : 1,
                      s.timeLimit);
        font::drawText(overlay_, 40, y + 10, buf, {180, 180, 180});
        y += font::kLineHeight + 4;
        std::snprintf(buf, sizeof buf, "traffic %.0f%%, pedestrians %.0f%%, police %d", s.ambient * 100, s.peds * 100,
                      s.cops);
        font::drawText(overlay_, 40, y + 10, buf, {180, 180, 180});
    }
    font::drawText(overlay_, 40, kFrameH - 40,
                   std::string("Up/Down: choose  Left/Right: change  Enter: race  U: open everything (") +
                       (openAll_ ? "on" : "off") + ")",
                   {150, 200, 255});
    if (!error_.empty())
        font::drawText(overlay_, 40, kFrameH - 24, error_, {255, 120, 120});
}

void RaceViewer::drawRaceHud() {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    if (netGame_ && !NetMenus::hasRace(net_ && net_->session() ? net_->session()->settings().mode : NetMode::Cruise)) {
        drawNetHud();
        return;
    }
    if (cruising()) {
        drawCruiseHud();
        return;
    }
    const Racer& player = racers_.front();
    const RaceSettings& set = race_.settings[size_t(level_)];
    drawCheckpointMarkers();
    drawOpponentMarkers();
    // Time, or time left in a Blitz or a lesson with a limit: the game's
    // digits, top middle.
    const float lessonLeft = lesson() ? judge_.timeLeft(clock_) : -1;
    const bool blitz = (race_.kind == RaceKind::Blitz && set.timeLimit > 0) || lessonLeft >= 0;
    const float shown = lessonLeft >= 0 ? lessonLeft
                        : blitz         ? set.timeLimit - std::max(0.0f, clock_)
                                        : std::max(0.0f, clock_);
    if (!hudShown_)
        ; // the HUD Toggle's off: no clock, list or gauges
    else if (pictures_.loaded())
        pictures_.clock(overlay_, shown, kFrameW / 2, 0);
    else
        drawCentred(clockText(shown), 4, blitz && shown < 10 ? Color{255, 90, 90} : Color{120, 255, 120}, 3);
    // The game's list, top left (its words: Place, Check, Lap): the place
    // against opponents, the checkpoints this lap, the lap, and each lap's
    // time, the one under way last.
    const bool circuit = race_.kind == RaceKind::Circuit;
    const size_t perLap = std::max<size_t>(1, race_.waypoints.size() - (circuit ? 0 : 1));
    const int laps = std::max(1, set.laps);
    const int lap = std::min(int(player.next / perLap), laps - 1);
    char buf[80];
    int listY = kListTop;
    auto listLine = [&](const std::string& label, const std::string& value, int valueX) {
        if (!hudShown_)
            return;
        font::drawText(overlay_, 2, listY, label, kListLabel);
        font::drawText(overlay_, valueX, listY, value, {255, 255, 255});
        listY += kListStep;
    };
    auto word = [&](uint32_t id, const char* fallback) {
        std::string w = strings_.get(id, fallback);
        while (!w.empty() && w.back() == ' ')
            w.pop_back();
        return w;
    };
    if (racers_.size() > 1 && !lesson()) {
        int place = 1;
        const float mine = progressOf(player);
        for (const Racer& r : racers_)
            if (!r.player && progressOf(r) > mine)
                ++place;
        std::snprintf(buf, sizeof buf, "%d/%zu", place, racers_.size());
        listLine(word(259, "Place:"), buf, 50);
    }
    if (circuit) {
        std::snprintf(buf, sizeof buf, "%zu/%zu", player.next >= targets_.size() ? perLap : player.next % perLap,
                      perLap);
        listLine(word(260, "Check:"), buf, 50);
        std::snprintf(buf, sizeof buf, "%d/%d", lap + 1, laps);
        listLine(word(261, "Lap:"), buf, 50);
        for (size_t i = 0; i < lapEnds_.size(); ++i)
            listLine(std::to_string(i + 1) + ".", splitText(lapEnds_[i] - (i ? lapEnds_[i - 1] : 0)), 28);
        if (lapEnds_.size() < size_t(laps) && clock_ > 0)
            listLine(std::to_string(lapEnds_.size() + 1) + ".",
                     splitText(clock_ - (lapEnds_.empty() ? 0 : lapEnds_.back())), 28);
    } else {
        std::snprintf(buf, sizeof buf, "%zu/%zu", std::min(player.next, targets_.size()), targets_.size());
        listLine(word(255, "Check:"), buf, 50);
    }
    // After each checkpoint, the time so far (this lap's in a circuit)
    // under the car; at a lap's end with "Lap time" over it.
    if (splitFor_ > 0) {
        drawCentred(splitText(split_), kSplitY, kMessage, 1);
        if (splitIsLap_)
            drawCentred(strings_.get(63, "Lap time"), kSplitY - 22, kMessage, 1);
    }
    if (lesson()) {
        // The game's message for the moment.
        if (const uint32_t id = judge_.message()) {
            std::string text = strings_.get(id);
            if (text.find("%.0f") != std::string::npos) {
                char line[160];
                std::snprintf(line, sizeof line, text.c_str(), double(judge_.minimumSpeed()));
                text = line;
            }
            int y = kMessageY;
            for (size_t at = 0; at <= text.size();) { // a string may have line breaks
                size_t end = text.find('\n', at);
                if (end == std::string::npos)
                    end = text.size();
                std::string line = text.substr(at, end - at);
                while (!line.empty() && line.front() == ' ')
                    line.erase(line.begin());
                drawCentred(line, y, kMessage, 2);
                y += font::kLineHeight * 2 + 4;
                at = end + 1;
            }
        }
        if (judge_.penalty() > 0) {
            std::snprintf(buf, sizeof buf, "PENALTY %.0f s", judge_.penalty());
            font::drawText(overlay_, 2, listY, buf, {255, 120, 90});
        }
    }
    // The countdown, as the game words it: Ready... Set... Go! (The stunt's
    // words after the start too.) Where the game shows its messages: over
    // the middle of the road.
    if (stunt()) {
        if (clock_ < -kCountdown / 2)
            drawCentred(raceMessage(0, "Race to the end of the pier!"), kMessageY, kMessage, 2);
        else if (clock_ < 0)
            drawCentred(raceMessage(1, "But don't fall in!"), kMessageY, kMessage, 2);
        else if (clock_ < 1)
            drawCentred(raceMessage(2, "Go!"), kMessageY, kMessage, 2);
        else if (stuntMessageFor_ > 0)
            drawCentred(strings_.get(stuntMessage_), kMessageY, kMessage, 2);
    } else if (!lesson()) {
        if (clock_ < -kCountdown / 2)
            drawCentred(raceMessage(0, "Ready..."), kMessageY, kMessage, 2);
        else if (clock_ < 0)
            drawCentred(raceMessage(1, "Set..."), kMessageY, kMessage, 2);
        else if (clock_ < 1)
            drawCentred(raceMessage(2, "Go!"), kMessageY, kMessage, 2);
    } else if (clock_ < 0) {
        drawCentred(std::to_string(int(std::ceil(-clock_))), kMessageY, kMessage, 2);
    }
    if (finalLapFor_ > 0)
        drawCentred(strings_.get(62, "Final lap!"), kSplitY - 22, kMessage, 1);
    if (state_ == State::Finished && lesson()) {
        font::drawText(overlay_, 60, 170, outcome_, {255, 255, 120}, 3);
        const bool passed = judge_.result() == LessonJudge::Result::Passed;
        const bool more = raceIndex_ + 1 < int(raceNames_.size());
        font::drawText(overlay_, 60, 210,
                       passed && more ? "Enter: next lesson   R: restart lesson   Esc: back to the menu"
                                      : "R: restart lesson   Enter: back to the menu",
                       {150, 200, 255});
    } else if (state_ == State::Finished) {
        font::drawText(overlay_, 60, 120, outcome_, {255, 255, 120}, 3);
        if (!beatenAll_.empty())
            font::drawText(overlay_, 60, 150, beatenAll_, {120, 255, 120}, 2);
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
        const bool hosting = netGame_ && net_ && net_->session() && net_->session()->isHost();
        font::drawText(overlay_, 60, y + 10,
                       !netGame_ ? (replaying_ ? "" : "Enter: back to the menu   I: instant replay")
                       : hosting ? "Enter: everyone back to the lobby"
                                 : "Waiting for the host (Esc: leave the session)",
                       {150, 200, 255});
    }
    if (life_.police().chasing() > 0 && state_ != State::Finished)
        font::drawText(overlay_, 4, kFrameH - 150, "POLICE CHASING", {255, 90, 90}, 2); // above the speedometer
    if (netGame_)
        drawNetLines();
    if (hudShown_)
        drawSpeedometer(overlay_, player.car->sim,
                        hudMap_.loaded() ? int(hudMap_.setup().left * float(kFrameW)) - 8 : kFrameW - 8,
                        wear_.fraction(), pictures_);
}

void RaceViewer::update(uint64_t nowMs) {
    if (!renderer_ || cars_.empty())
        return;
    nowMs_ = nowMs;
    if (net_)
        netUpdate(nowMs);
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000) : 0;
    lastMs_ = nowMs;

    Vec3 eye, target, up{0, 1, 0};
    float fov = 70, nearClip = 0.1f;
    if (state_ == State::Menu || racers_.empty()) {
        // Round the chosen race's start.
        orbit_ += dt * 0.15f;
        const Vec3 centre = race_.waypoints.empty() ? scene_->start : race_.waypoints[0].position;
        eye = centre + Vec3{std::sin(orbit_) * 40, 18, std::cos(orbit_) * 40};
        target = centre;
    } else {
        // After the finish the others race on to theirs, for the results
        // (stopped while paused, or on the results screen).
        if (replaying_)
            stepReplay(dt);
        else if (!paused_ && !results_ && !(hudMap_.fullScreen() && !netGame_)) {
            // (The full-screen map stops a race alone, as the game's does.)
            recordFrame(dt);
            stepRace(dt);
            markRecording();
        }
        GameMusic::State music;
        // Waiting for the start counts as on the move, not stopped.
        music.speed = state_ == State::Countdown ? 100 : std::fabs(racers_.front().car->sim.forwardSpeed());
        music.chased = life_.police().chasing() > 0;
        music.finished = state_ == State::Finished;
        music.paused = paused_;
        music_.update(dt, music);
        // A second off the ground: the tune's big-air motif, once a jump.
        {
            const CarSim& air = racers_.front().car->sim;
            const bool off =
                std::none_of(air.wheels.begin(), air.wheels.end(), [](const CarSim::Wheel& w) { return w.contact; });
            airFor_ = off ? airFor_ + dt : 0;
            if (airFor_ >= kBigAir && !airPlayed_) {
                music_.bigAir();
                airPlayed_ = true;
            }
            airPlayed_ = airPlayed_ && off;
        }
        const CarCamera& cam = cameras_[size_t(camera_)];
        fov = cam.fov;
        const CameraView view =
            replaying_ ? replayCamera(dt, fov)
                       : blend_.apply(updateCarCamera(cam, racers_.front().car->sim, dt, cameraPos_, cameraReset_), dt);
        lastView_ = view;
        nearClip = cam.nearClip;
        cameraReset_ = false;
        eye = view.eye;
        target = view.target;
        up = view.up;
    }
    const gfx::Mat4 viewProj = gfx::multiply(
        gfx::perspective(fov * kPi / 180, float(kFrameW) / float(kFrameH), nearClip, currentOptions().farClip()),
        gfx::lookAlong(eye, normalized(target - eye), up));
    viewProj_ = viewProj;
    if ((state_ == State::Menu || racers_.empty()) && front_) {
        // The game's menus, over a dark frame (the garage's car in its
        // window), with their music and voices.
        menuSounds();
        renderer_->beginFrame(viewProj, eye, {10, 14, 40});
        if (netMenus_ && net_) {
            net_->draw(overlay_, nowMs);
        } else if (game_ && game_->isOpen() && !game_->isDialog()) {
            game_->draw(overlay_, nowMs);
        } else {
            if (front_->screen() == FrontEnd::Screen::Garage || front_->screen() == FrontEnd::Screen::Showcase)
                drawGarageCar(dt);
            front_->draw(overlay_);
            if (game_ && game_->isOpen())
                game_->draw(overlay_, nowMs); // a dialog, over the menu
        }
        if (help_)
            help_->draw(overlay_);
        renderer_->endFrame(&overlay_);
        return;
    }
    renderer_->beginFrame(viewProj, eye, {150, 170, 200});
    applyGraphicsOptions(*scene_, life_);
    scene_->draw(viewProj, eye, true);
    if (state_ == State::Menu || racers_.empty()) {
        drawMenu();
    } else {
        const bool inside = cameras_[size_t(camera_)].inside;
        if (scene_->lampsLit() && headlightPoolsOn()) {
            std::vector<gfx::DynamicLight> pools;
            gfx::DynamicLight pool;
            for (const Racer& r : racers_)
                if (r.car->headlightPool(pool))
                    pools.push_back(pool);
            life_.headlightPools(pools, eye);
            scene_->drawDynamicLights(pools, eye);
        }
        life_.draw(*renderer_, scene_->models(), scene_->light(), viewProj, eye, flares_.get());
        scene_->drawBeacons(*flares_, eye);
        for (const Racer& r : racers_)
            if (!(r.player && inside)) {
                r.car->draw(scene_->light());
                r.car->drawFlares(*flares_, eye);
            }
        drawCheckpoints();
        if (netGame_)
            drawGold();
        if (smoke_)
            smoke_->draw(eye);
        if (inside && dash_.loaded())
            dash_.draw(*renderer_, scene_->models(), racers_.front().car->sim, wear_.fraction(), eye,
                       normalized(target - eye), up, float(kFrameW) / float(kFrameH), scene_->light());
        if (netGame_ && net_ && net_->session() && net_->session()->settings().mode == NetMode::CopsRobbers) {
            const NetSession& session = *net_->session();
            const NetPlayer* me = session.me();
            const bool mine = gold_.holder() == session.myId();
            GoldGame::Car self{session.myId(), me ? me->team : uint8_t(0), {}, {}};
            drawArrowTo(mine ? gold_.base(gold_.sideOf(self)) : gold_.goldPosition(), mine);
        } else
            drawArrow();
        if (mirror_)
            drawRearMirror(*renderer_, racers_.front().car->sim, currentOptions().farClip(),
                           [&](const gfx::Mat4& vp, const Vec3& eye) {
                               scene_->draw(vp, eye, true, true);
                               life_.draw(*renderer_, scene_->models(), scene_->light(), vp, eye);
                               for (const Racer& r : racers_)
                                   if (!r.player)
                                       r.car->draw(scene_->light());
                               drawCheckpoints();
                           });
        drawMap(dt);
        drawRaceHud();
        if (replaying_)
            drawReplayBar();
        if (paused_)
            drawPauseMenu();
        if (results_)
            drawResults();
        if (game_ && game_->isOpen())
            game_->draw(overlay_, nowMs); // the Options, or the replay's save dialogs
    }
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::age
