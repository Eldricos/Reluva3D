// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The race screen: the game's Blitz, Circuit and Checkpoint races, from its
// race files (race.h), against opponents driving their paths along the
// roads (opponent.h), and its Crash Course lessons (lesson.h), chosen in
// the game's menus (front_end.h; a menu of its own without them). The
// rules as Reluva plays them are in docs/engines/age-racing.md.

#include "engines/age/ui/screen.h"

#include "graphics/font.h"
#include "engines/age/game/city_collision.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kPi = 3.14159265358979f;
constexpr float kBigAir = 1;                      // seconds off the ground that count as big air (Reluva's)
constexpr float kCountdown = 3;                   // seconds before the start
constexpr float kStartHigh = 28, kStartBack = 14; // metres: the camera over the grid at the countdown's start
constexpr float kCircuitRadius = 15;              // circuits' waypoints give no radius
constexpr float kOpponentReach = 10;              // opponents count a checkpoint this much wider
constexpr float kSkill[2] = {0.85f, 0.95f};       // amateur, professional: opponents' share of top speed
const RaceKind kKinds[] = {RaceKind::kBlitz, RaceKind::kCircuit, RaceKind::kCheckpoint, RaceKind::kCrashCourse,
                           RaceKind::kCruise};
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

float flatDistance(const Vec3 &a, const Vec3 &b) {
    const float dx = a[0] - b[0], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

RaceKind racePageKind(int index) {
    return kKinds[std::clamp(index, 0, kKindCount - 1)];
}

RaceScreen::RaceScreen(ScreenContext &ctx) : Page(ctx), _cities(findCities(ctx.files)), _cars(findCars(ctx.files)) {
}

RaceScreen::~RaceScreen() {
    deactivate();
}

void RaceScreen::deactivate() {
    _net.reset(); // out of any session
    _netGame = _netMenus = _garageForNet = _chatting = false;
    _showCar.reset();
    _showCarIndex = -1;
    _life.stop();
    _engine.stop();
    _sounds.stop();
    _music.stop();
    _racers.clear();
    _flares.reset();
    _smoke.reset();
    _hudMap.unload();
    _scene.reset();
    _loadedCity = -1;
    _renderer = nullptr;
    _state = State::kMenu;
}

void RaceScreen::activate() {
    if (Error err = _ctx.sys.init3D(kFrameW, kFrameH, _renderer)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRGBA32);
        clear(16, 18, 24);
        text(4, 4, "The race screen needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    _scene = std::make_unique<CityScene>(*_renderer, _ctx.files);
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRGBA32);
    applySoundOptions(loadOptions(_ctx.savePath), _ctx.mixer);
    _strings.load(_ctx.files);
    _roster.load(_ctx.savePath);
    _record.load(_ctx.savePath, _roster.driver().file);
    _level = _roster.driver().professional ? 1 : 0;
    _front = std::make_unique<FrontEnd>(_ctx.files, _cities, _cars, _strings, _record);
    if (!_front->loaded())
        _front.reset(); // the text menu instead
    if (_front && _ctx.openingPlayed)
        _front->leaveSplash();
    if (_front) {
        _net = std::make_unique<NetMenus>(_ctx.sys, _ctx.files, _cities, _cars, _strings);
        if (!_net->loaded())
            _net.reset();
        _front->multiplayer = _net != nullptr;
        _front->setDrivers(&_roster);
        _front->choice().professional = _roster.driver().professional;
        _game = std::make_unique<GameMenus>(_ctx.sys, _ctx.files, _strings, _ctx.mixer, _ctx.savePath);
        if (!_game->loaded())
            _game.reset();
        else {
            _game->setDrivers(&_roster, &_record, &_cities);
            _game->setFrontEnd(_front.get());
        }
    }
    _quit = false;
    _keys = 0;
    _lastMs = 0;
    _state = State::kMenu;
    loadCity();
}

void RaceScreen::loadCity() {
    if (_loadedCity == _city)
        return;
    _racers.clear();
    _flares.reset();
    _smoke.reset();
    const std::string &map = _cities[size_t(_city)].map;
    _scene->textures().setHalvings(textureHalvings());
    _error = _scene->load(map).message;
    _flares = std::make_unique<FlareDrawer>(*_renderer, _scene->textures());
    _smoke = std::make_unique<SmokePuffs>(*_renderer, _scene->textures());
    _smoke->loadWheelEffects(_ctx.files);
    _hudMap.load(*_renderer, _ctx.files, _scene->models(), map);
    _pictures.load(_ctx.files);
    buildCityCollision(_ctx.files, _scene->psdl(), _scene->mesh(), _scene->instances(), _world);
    _roads.build(_scene->psdl());
    _roads.penalizeBlocked(_world);
    if (Error err = _life.load(_ctx.files, map); err && _error.empty())
        _error = err.message;
    _botRoads.build(_life.roadMap());
    _loadedCity = _city;
    _raceNames = raceNames(_ctx.files, map, kKinds[_kind]);
    _raceIndex = std::min(_raceIndex, std::max(0, int(_raceNames.size()) - 1));
    // Show where the chosen race starts.
    if (!_raceNames.empty())
        loadRace(_ctx.files, map, kKinds[_kind], _raceIndex, _race);
}

bool RaceScreen::takesKey(const Event &ev) const {
    // In the game's menus Esc goes back (and on the main menu asks to
    // quit); in a race it's the race's.
    // In a race, Tab is the game's too (its Map Toggle), not the next page.
    return ev.type == EventType::kKeyDown &&
           (((_state != State::kMenu || _netMenus || _front) && ev.key == key::kEscape) ||
            (_state != State::kMenu && !_racers.empty() && ev.key == key::kTab));
}

bool RaceScreen::raceIsOpen(int index) const {
    if (_openAll)
        return true;
    const std::string &map = _cities[size_t(_city)].map;
    const RaceUnlocking u = raceUnlocking(_ctx.files, map);
    return raceOpen(_record, map, kKinds[_kind], index, _level == 1, u.group, u.mustPlace);
}

bool RaceScreen::carIsOpen(int car) const {
    if (_openAll)
        return true;
    const SearchSet &files = _ctx.files;
    return unlocked(
        _record, _cars[size_t(car)].base, -1, _level == 1,
        [&files](const std::string &city, RaceKind kind) { return int(raceNames(files, city, kind).size()); });
}

void RaceScreen::switchDriver() {
    // Another driver: their record, their level.
    _record.load(_ctx.savePath, _roster.driver().file);
    _level = _roster.driver().professional ? 1 : 0;
    if (_front) {
        _front->choice().professional = _roster.driver().professional;
        _front->refreshChoice(); // for what's open to them
    }
}

void RaceScreen::recordResult() {
    // Once a race: its place (a Blitz finished in time, first), or the
    // lesson passed; saved at once.
    // Not a race run in changed conditions: "the race statistics are
    // recorded for races under default conditions only" (jpg/hoff_dlg).
    if (_recorded || _racers.empty() || _state != State::kFinished || _netGame || _replaying ||
        (_conditionsChanged && !lesson()))
        return;
    _recorded = true;
    const std::string &map = _cities[size_t(_city)].map;
    const Racer &player = _racers.front();
    // What it opens, for the results screen: the city's rewards open
    // before the result and after.
    std::vector<Reward> rewards;
    if (std::vector<uint8_t> data; !_ctx.files.read("race/" + map + "/" + map + "_rewards.csv", data))
        rewards = parseRewards(std::string(data.begin(), data.end()));
    const SearchSet &files = _ctx.files;
    const RaceCount count = [&files](const std::string &city, RaceKind kind) {
        return int(raceNames(files, city, kind).size());
    };
    // Each reward is the unlock rule for its car, city and kind (the
    // rewards' paint job numbers don't always match the lock pictures':
    // the fire truck's 2 is its _lck4).
    auto ruleFor = [&](const Reward &r) -> const UnlockRule * {
        const RaceKind kind = r.kind == "blitz"     ? RaceKind::kBlitz
                              : r.kind == "circuit" ? RaceKind::kCircuit
                              : r.kind == "race"    ? RaceKind::kCheckpoint
                                                    : RaceKind::kCrashCourse;
        for (const UnlockRule &u : unlockRules())
            if (u.car == r.car && u.city == map && u.kind == kind && (u.paintJob < 0) == (r.paintJob == 0))
                return &u;
        return nullptr;
    };
    auto opened = [&] {
        std::vector<bool> open;
        for (const Reward &r : rewards) {
            const UnlockRule *u = ruleFor(r);
            open.push_back(!u || unlocked(_record, u->car, u->paintJob, _level == 1, count));
        }
        return open;
    };
    const std::vector<bool> before = opened();
    if (lesson()) {
        if (_judge.result() != LessonJudge::Result::kPassed)
            return;
        _record.passed(map, _raceIndex);
    } else {
        if (player.finishTime < 0)
            return;
        int place = 1;
        for (const Racer &r : _racers)
            if (!r.player && r.finishTime >= 0 && r.finishTime < player.finishTime)
                ++place;
        _record.raced(map, _race.kind, _raceIndex, _level == 1, _race.kind == RaceKind::kBlitz ? 1 : place,
                      player.finishTime);
    }
    if (Error err = _record.save(); err && !_ctx.savePath.empty())
        _error = err.message;
    const std::vector<bool> after = opened();
    for (size_t i = 0; i < rewards.size(); ++i)
        if (after[i] && !before[i])
            _unlockMessage += (_unlockMessage.empty() ? "" : " ") + rewards[i].message;
}

void RaceScreen::startRace() {
    const std::string &map = _cities[size_t(_city)].map;
    if (_raceNames.empty())
        return;
    if (!_replaying && (!raceIsOpen(_raceIndex) || !carIsOpen(_car))) {
        _error = "Locked: see the menu";
        return;
    }
    _recorded = false;
    if (Error err = loadRace(_ctx.files, map, kKinds[_kind], _raceIndex, _race)) {
        _error = err.message;
        return;
    }
    _error.clear();
    // The conditions the menus changed (or the replay's): over the race's.
    // A cruise has none of its own: always the menu's.
    const bool changed = _replaying ? _playback.changed : _conditionsChanged || _race.kind == RaceKind::kCruise;
    if (changed && !lesson()) {
        RaceSettings &s = _race.settings[size_t(_level)];
        const FrontEnd::Choice::Conditions c =
            _replaying ? FrontEnd::Choice::Conditions{_playback.laps,      _playback.opponents,
                                                      _playback.timeOfDay, _playback.weather,
                                                      _playback.cops,      _playback.traffic,
                                                      _playback.peds,      false}
                       : _conditions;
        s.laps = c.laps;
        s.opponents = c.opponents;
        s.timeOfDay = c.timeOfDay;
        s.weather = c.weather;
        s.cops = c.cops;
        s.ambient = c.traffic;
        s.peds = c.hidePeds ? 0 : c.peds;
    }
    const RaceSettings &set = _race.settings[size_t(_level)];
    _scene->setLighting(std::clamp(set.timeOfDay, 0, 3) * 4 + std::clamp(set.weather, 0, 3));
    if (_race.kind == RaceKind::kCruise)
        startCruise();

    // The checkpoints in order: the waypoints after the start; a circuit's
    // lap ends back at the first, as many laps as it has.
    _targets.clear();
    const size_t n = _race.waypoints.size();
    const int laps = _race.kind == RaceKind::kCircuit ? std::max(1, set.laps) : 1;
    for (int lap = 0; lap < laps; ++lap) {
        for (size_t i = 1; i < n; ++i)
            _targets.push_back(i);
        if (_race.kind == RaceKind::kCircuit)
            _targets.push_back(0);
    }

    _racers.clear();
    Racer player;
    player.car = std::make_unique<CarActor>();
    player.player = true;
    player.name = _cars[size_t(_car)].description + " (you)";
    if (Error err =
            player.car->load(*_renderer, _scene->textures(), _ctx.files, _cars[size_t(_car)].base, uint32_t(_paint)))
        _error = err.message;
    const Waypoint &start = _race.waypoints[0];
    player.car->sim.reset(player.car->setup, start.position + Vec3{0, 0.5f, 0}, headingToYaw(start.heading));
    _wear.load(_ctx.files, _cars[size_t(_car)].base, *player.car);
    if (_smoke)
        _smoke->clear();
    _racers.push_back(std::move(player));

    // The race's aimap, read over the city's: the opponents' cars and
    // skill, the roads left without traffic, the police.
    AiMapSettings ai = _life.citySettings();
    std::vector<uint8_t> data;
    if (!_ctx.files.read(raceAiMapName(map, _race.kind, _raceIndex, _level == 1), data) ||
        !_ctx.files.read(raceAiMapName(map, _race.kind, _raceIndex, false), data))
        parseAiMap(std::string(data.begin(), data.end()), ai);

    // Opponents: as many as the race has paths for, in the cars its aimap
    // gives them (else others of the player's list).
    // A lesson has the ones its aimap lists, in that order.
    const auto &paths = _race.opponents[size_t(_level)];
    const size_t count = lesson() ? paths.size() : std::min(paths.size(), size_t(std::max(0, set.opponents)));
    for (size_t i = 0; i < count; ++i) {
        std::string base = _cars[(size_t(_car) + 1 + i * 3) % _cars.size()].base;
        float skill = kSkill[_level];
        const std::string opp = "-" + std::to_string(i) + ".opp";
        for (size_t k = 0; k < ai.opponents.size(); ++k) {
            const auto &o = ai.opponents[k];
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
        for (const CarInfo &info : _cars)
            if (info.base == base)
                r.name = info.description;
        if (r.car->load(*_renderer, _scene->textures(), _ctx.files, base, uint32_t(i)))
            continue;
        const auto &o = paths[i];
        r.car->sim.reset(r.car->setup, o.start + Vec3{0, 0.5f, 0}, o.startYaw);
        size_t loopFrom = 0;
        const bool circuit = _race.kind == RaceKind::kCircuit;
        r.driver.start(routePath(_roads, o.start, o.path, circuit, loopFrom, &_botRoads), circuit ? laps : 0, skill,
                       loopFrom);
        if (!lesson()) {
            std::vector<Vec3> gates;
            for (size_t k = 0; k < _targets.size(); ++k)
                gates.push_back(targetPoint(k).position);
            r.gatePoints = pathPointsNearest(r.driver, gates);
        }
        _racers.push_back(std::move(r));
    }

    // Traffic, pedestrians and police as the race's settings say (a
    // lesson's traffic as its own row says; its police only in a cop
    // chase, all of its aimap's).
    CityLife::Options life;
    life.traffic = lesson() ? _race.lesson.ambient : set.ambient;
    life.peds = set.peds * (_replaying ? _playback.pedestrians : pedestrianShare());
    life.cops = std::max(0, set.cops);
    if (lesson())
        life.cops = _race.lesson.event == LessonEvent::kCopChase ? -1 : 0;
    life.rain = set.weather == 3;
    life.seed = uint32_t(_raceIndex * 7 + _level + 1);
    life.variant = raceStem(_race.kind, _raceIndex); // its own bridges and ferries where it has them
    if (_life.loaded()) {
        if (Error err = _life.start(*_renderer, _scene->textures(), _ctx.files, ai, life, _ctx.mixer))
            _error = err.message;
        // What the city has across the drawbridges' gaps goes: the leaves
        // are the road there.
        auto inGap = [&](const Vec3 &a, const Vec3 &b, const Vec3 &c) { return _life.gizmos().inGap(a, b, c); };
        _scene->hide(inGap);
        _world.removeIf(inGap);
    }
    _fill = true;

    _engine.stop();
    _sounds.stop();
    if (_ctx.mixer) {
        _engine.start(*_ctx.mixer, _ctx.files, _cars[size_t(_car)].base);
        _sounds.start(*_ctx.mixer, _ctx.files, _cars[size_t(_car)].base, set.weather == 3);
        // The race music: a tune picked by the race (aud/dmusic).
        // A cruise's, one of the cruising tunes.
        if (Error err = _music.start(_ctx.files, *_ctx.mixer, !cruising(),
                                     uint32_t(_raceIndex * 7 + _city * 3 + int(_race.kind) + _car)))
            _error = "music: " + err.message;
    }
    _cameras = loadCarCameras(_ctx.files, _cars[size_t(_car)].base);
    _camera = std::min(_camera, int(_cameras.size()) - 1);
    _dash.load(_ctx.files, _scene->models(), _scene->textures(), _cars[size_t(_car)].base);
    _cameraReset = true;
    _clock = cruising() ? 0 : -kCountdown; // a cruise is off at once
    _outcome.clear();
    _beatenAll.clear();
    _finalLapFor = 0;
    _sunkFor = 0;
    _lastLap = 0;
    _lapEnds.clear();
    _splitFor = 0;
    _targetStill = 0;
    _unlockMessage.clear();
    for (Racer &r : _racers) {
        r.cleared.assign(_targets.size(), false);
        r.lastCleared = 0;
    }
    _picked = -1;
    if (lesson())
        _judge.start(_race.lesson);
    _keys = 0;
    _state = State::kCountdown;
    setTitle(_race.name + " (" + raceKindName(_race.kind) + ", " + _cities[size_t(_city)].name + ")");
    if (!_replaying && !_netGame) {
        // The last race run (Quick Race's), kept.
        _record.setLastRace(map, _race.kind, _raceIndex);
        _record.save();
    }
    if (!_replaying) {
        // Recorded as it's driven, from here.
        _recording = Replay{};
        _recording.city = map;
        _recording.kind = int(_race.kind);
        _recording.race = _raceIndex;
        _recording.level = _level;
        _recording.car = _cars[size_t(_car)].base;
        _recording.paint = _paint;
        _recording.autoReverse = currentOptions().autoReverse;
        _recording.steerRate = currentOptions().steerRate();
        _recording.pedestrians = pedestrianShare();
        _recording.changed = changed && !lesson();
        _recording.laps = set.laps;
        _recording.opponents = set.opponents;
        _recording.timeOfDay = set.timeOfDay;
        _recording.weather = set.weather;
        _recording.cops = set.cops;
        _recording.traffic = set.ambient;
        _recording.peds = set.peds;
    }
}

void RaceScreen::endRace() {
    _replaying = false;
    _paused = _results = false;
    if (_front)
        _front->afterRace();
    _life.stop();
    _engine.stop();
    _music.stop();
    _racers.clear();
    _state = State::kMenu;
    _scene->setLighting(4);
}

float RaceScreen::progressOf(const Racer &r) const {
    // Checkpoints reached, and how near the next.
    if (r.next >= _targets.size())
        return float(_targets.size()) + 1 - r.finishTime / 1e5f;
    const float d = flatDistance(r.car->sim.position, targetPoint(targetOf(r)).position);
    return float(r.next) + 1 / (1 + d / 50);
}

bool RaceScreen::anyOrder() const {
    return !lesson() && (_race.kind == RaceKind::kCheckpoint || _race.kind == RaceKind::kBlitz) && _targets.size() > 1;
}

std::vector<size_t> RaceScreen::remaining(const Racer &r) const {
    // The finish only once the rest are cleared.
    auto cleared = [&](size_t k) { return k < r.cleared.size() && r.cleared[k]; };
    std::vector<size_t> out;
    for (size_t k = 0; k + 1 < _targets.size(); ++k)
        if (!cleared(k))
            out.push_back(k);
    if (out.empty() && !_targets.empty() && !cleared(_targets.size() - 1))
        out.push_back(_targets.size() - 1);
    return out;
}

void RaceScreen::clearCheckpoint(Racer &r, size_t k) {
    r.cleared.resize(_targets.size(), false);
    if (k < r.cleared.size())
        r.cleared[k] = true;
    r.lastCleared = k;
}

size_t RaceScreen::targetOf(const Racer &r) const {
    if (!anyOrder() || r.next >= _targets.size())
        return r.next;
    const std::vector<size_t> left = remaining(r);
    if (left.empty())
        return _targets.size();
    if (r.player && _picked >= 0 && std::find(left.begin(), left.end(), size_t(_picked)) != left.end())
        return size_t(_picked);
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

void RaceScreen::pickCheckpoint(int step) {
    // The arrow to the next (or the one before) in the list of those left.
    if (!anyOrder() || _racers.empty())
        return;
    const std::vector<size_t> left = remaining();
    if (left.empty())
        return;
    const size_t now = targetOf(_racers.front());
    const auto at = std::find(left.begin(), left.end(), now);
    const size_t i = at == left.end() ? 0 : size_t(at - left.begin());
    _picked = int(left[(i + left.size() + (step > 0 ? 1 : left.size() - 1)) % left.size()]);
}

void RaceScreen::stepRace(float dt) {
    const bool wasStarted = _clock >= 0;
    if (_netGame) {
        // The host's clock; waiting for everyone to load, before the
        // countdown.
        NetSession *session = _net ? _net->session() : nullptr;
        _clock = session && session->counting() ? session->clock(_nowMs) : -kCountdown - 1;
    } else
        _clock += dt;
    const bool started = _clock >= 0;
    if (lesson() && started && !wasStarted && _race.lesson.event == LessonEvent::kCopChase)
        _life.police().chaseAll(); // after the player from the start
    bool checkpoint = false;       // the player reached one this frame
    float impact = 0;
    for (Racer &r : _racers) {
        if (r.peer != kNoPlayer) {
            driveRemote(r, dt);
            continue;
        }
        CarSim &sim = r.car->sim;
        CarControls c;
        if (started) {
            if (r.player) {
                // Driving on, unless finished (a cruise has no end, and the
                // stunt ends once stopped).
                if (r.next < _targets.size() || _targets.empty() || stunt()) {
                    c = controlsOf(_chatting ? 0 : _keys);
                    if (!_chatting)
                        _pad.apply(c);
                } else {
                    c = CarControls{0, 1, 0, false};
                }
                if (_wear.wrecked())
                    c.throttle = 0; // the engine's dead
                c.shift = _shift;
                _shift = 0;
                sim.manual = _manual;
                if (_replaying) {
                    sim.autoReverse = _playback.autoReverse;
                    sim.steerRate = _playback.steerRate;
                } else
                    applyControlOptions(sim);
            } else {
                if (r.botId != kNoPlayer && _net && _net->session() &&
                    !NetMenus::hasRace(_net->session()->settings().mode))
                    planBot(r, dt, netCars());
                c = r.driver.drive(sim, dt);
            }
        } else {
            c.brake = 0; // waiting on the line
        }
        r.throttle = sim.gear > 0 || sim.manual ? c.throttle : c.brake;
        sim.step(c, _world, dt);
        r.car->lamps = {_scene->lampsLit(), c.brake > 0 && sim.forwardSpeed() > 0.5f, sim.gear < 0};
        // (A computer player outside a race is put back on the road by its
        // own mind: planBot.)
        const bool minded =
            r.botId != kNoPlayer && _net && _net->session() && !NetMenus::hasRace(_net->session()->settings().mode);
        // The car to destroy stays where it's been rammed to.
        const bool target = lesson() && _race.lesson.event == LessonEvent::kDestroy && &r == &_racers[1];
        if (!r.player && !minded && !target &&
            (r.driver.wantsReset() || sim.position[1] < _scene->psdl().min[1] - 50)) {
            sim.reset(r.car->setup, r.driver.resetPoint() + Vec3{0, 0.5f, 0}, r.driver.resetYaw());
            r.driver.resetDone();
            ++r.resets;
            r.putBackAt = _clock;
        }
        if (_smoke)
            _smoke->wheels(dt, sim);
        if (r.player) {
            _engine.update(sim.rpm, sim.gear > 0 || sim.manual ? c.throttle : c.brake);
            _sounds.update(dt, sim, started && (_keys & kDriveHorn));
            _wear.update(dt, *r.car, _smoke.get());
            impact = sim.impact;
            if (const float shake = padRumble(impact); shake > 0 && currentOptions().forceFeedback && !_replaying)
                _shaker.knock(_ctx.sys, shake, _ctx.sys.getMillis()); // a knock shakes the game controller
            sim.impact = 0;
            if (sim.position[1] < _scene->psdl().min[1] - 50) { // fallen out: back to the last checkpoint
                _sunkFor = 3;                                   // (deep water's the way out of the city)
                const size_t last = anyOrder() ? r.lastCleared : r.next - 1;
                const Waypoint &back = r.next > 0 ? targetPoint(last) : _race.waypoints[0];
                sim.reset(r.car->setup, back.position + Vec3{0, 0.5f, 0}, headingToYaw(back.heading));
                ++r.resets;
                r.putBackAt = _clock;
            }
        }
        // Checkpoints.
        if (started && anyOrder() && r.next < _targets.size()) {
            // Any of those left, the finish last (an opponent also past
            // where its own path is nearest one).
            for (size_t k : remaining(r)) {
                const Waypoint &w = targetPoint(k);
                const float radius = (w.radius > 0 ? w.radius : kCircuitRadius) + (r.player ? 2 : kOpponentReach);
                const bool byPath = !r.player && k < r.gatePoints.size() && r.driver.passed() > r.gatePoints[k];
                if (byPath || (flatDistance(sim.position, w.position) < radius &&
                               std::fabs(sim.position[1] - w.position[1]) < 10)) {
                    clearCheckpoint(r, k);
                    checkpoint = checkpoint || r.player;
                    if (++r.next == _targets.size())
                        r.finishTime = _clock;
                    break;
                }
            }
        } else if (started && r.next < _targets.size()) {
            const Waypoint &w = targetPoint(r.next);
            const float radius = (w.radius > 0 ? w.radius : kCircuitRadius) + (r.player ? 2 : kOpponentReach);
            // An opponent also once it's gone on past where its own path
            // is nearest the checkpoint (some paths take another way).
            const bool byPath = r.next < r.gatePoints.size() && r.driver.passed() > r.gatePoints[r.next];
            if (byPath ||
                (flatDistance(sim.position, w.position) < radius && std::fabs(sim.position[1] - w.position[1]) < 10)) {
                checkpoint = checkpoint || r.player;
                if (++r.next == _targets.size())
                    r.finishTime = _clock;
            }
        }
    }
    for (size_t i = 0; i < _racers.size(); ++i)
        for (size_t j = i + 1; j < _racers.size(); ++j)
            collideCars(_racers[i].car->sim, _racers[j].car->sim);
    if (_smoke)
        _smoke->update(dt);
    std::vector<CarSim *> others;
    for (size_t i = 1; i < _racers.size(); ++i)
        others.push_back(&_racers[i].car->sim);
    _life.setLamps(_scene->lampsLit(), _scene->night());
    _life.update(dt, _racers.front().car->sim, others, _world, _roads, _fill);
    // What flies out of the props knocked down: coins, papers, leaves.
    for (const Props::Burst &b : _life.takeBursts())
        if (_smoke)
            _smoke->burst(b.rule, b.at, b.velocity, _smoke->look(b.texture));
    _fill = false;

    if (_state == State::kCountdown && started)
        _state = State::kRacing;
    // A checkpoint passed: the time so far, a lap's own at its end.
    _splitFor = std::max(0.0f, _splitFor - dt);
    if (checkpoint && !lesson() && !_racers.empty()) {
        const Racer &player = _racers.front();
        const size_t perLap = _race.waypoints.size();
        _splitIsLap = _race.kind == RaceKind::kCircuit && perLap && player.next % perLap == 0;
        const float lapStart = _lapEnds.empty() ? 0 : _lapEnds.back();
        _split = _race.kind == RaceKind::kCircuit ? _clock - lapStart : _clock;
        if (_splitIsLap)
            _lapEnds.push_back(_clock);
        _splitFor = 3;
    }
    if (_netGame) {
        stepNet(dt);
        return;
    }
    // Into a circuit's last lap: "Final lap!" for a moment.
    _finalLapFor = std::max(0.0f, _finalLapFor - dt);
    _sunkFor = std::max(0.0f, _sunkFor - dt);
    if (_race.kind == RaceKind::kCircuit && !_racers.empty()) {
        const size_t perLap = _race.waypoints.size();
        const int laps = std::max(1, _race.settings[size_t(_level)].laps);
        const int lap = perLap ? int(_racers.front().next / perLap) : 0;
        if (lap != _lastLap && lap == laps - 1 && laps > 1)
            _finalLapFor = 3;
        _lastLap = lap;
    }
    if (lesson()) {
        judgeLesson(dt, checkpoint, impact);
        recordResult();
        return;
    }
    if (_race.kind == RaceKind::kCruise) {
        stepCruise(dt);
        return;
    }
    if (_state == State::kFinished)
        return;
    // The end: the player finished, or ran out of time in a Blitz.
    Racer &player = _racers.front();
    const RaceSettings &set = _race.settings[size_t(_level)];
    if (player.finishTime >= 0) {
        int place = 1;
        for (const Racer &r : _racers)
            if (!r.player && r.finishTime >= 0 && r.finishTime < player.finishTime)
                ++place;
        // The game's words: "You Won!", "You finished 2nd", "You are a loaf"
        // past eighth.
        static const char *const kPlaces[] = {"1st!", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
        if (_race.kind == RaceKind::kBlitz)
            _outcome = raceMessage(6, "You Won!") + "  " + clockText(player.finishTime);
        else if (place <= 8)
            _outcome = raceMessage(3 + place, std::string("You finished ") + kPlaces[place - 1]) + "  " +
                       clockText(player.finishTime);
        else
            _outcome = raceMessage(12, "You are a loaf");
        _state = State::kFinished;
    } else if (_race.kind == RaceKind::kBlitz && set.timeLimit > 0 && _clock > set.timeLimit) {
        _outcome = raceMessage(3, "Time's up!");
        _state = State::kFinished;
    } else if (_life.police().busted()) {
        // Blitz and Checkpoint groups have their "Game over!"; Circuit's none.
        const int over = _race.kind == RaceKind::kBlitz ? 4 : _race.kind == RaceKind::kCheckpoint ? 3 : -1;
        _outcome = "Busted! " + (over >= 0 ? raceMessage(over, "Game over!") : std::string("Game over!"));
        _state = State::kFinished;
    } else if (_wear.wrecked()) {
        _outcome = "Wrecked! Game over!";
        _state = State::kFinished;
    }
    const bool justFinished = _state == State::kFinished && !_recorded;
    recordResult();
    // Every race of the kind won (at this level): the game's word for it.
    if (justFinished && _race.kind != RaceKind::kCheckpoint) {
        const std::string &map = _cities[size_t(_city)].map;
        bool all = !_raceNames.empty();
        for (int i = 0; i < int(_raceNames.size()) && all; ++i)
            all = _record.bestPlace(map, _race.kind, i, _level == 1) == 1;
        if (all)
            _beatenAll = raceMessage(_race.kind == RaceKind::kBlitz ? 5 : 13, "");
    }
}

std::string RaceScreen::raceMessage(int offset, const std::string &fallback) const {
    // Each event's group (mmlang.dll): Blitz from 158 (Ready, Set, Go,
    // Time's up, Game over, all beaten, You Won!), Circuit from 165
    // (Ready, Set, Go, a penalty, finished 1st-8th, a loaf, all beaten),
    // Checkpoint from 179 (Ready, Set, Go, Game over, finished 1st-8th, a
    // loaf).
    // The stunt's from 200 (to the end of the pier, don't fall in, Go!).
    const int base = _race.kind == RaceKind::kBlitz     ? 158
                     : _race.kind == RaceKind::kCircuit ? 165
                     : _race.kind == RaceKind::kCruise  ? 200
                                                        : 179;
    return _strings.get(uint32_t(base + offset), fallback);
}

void RaceScreen::drawCentred(const std::string &text, int y, Color color, int scale) {
    const int w = int(text.size()) * font::kAdvance * scale;
    font::drawText(_overlay, (kFrameW - w) / 2, y, text, color, scale);
}

void RaceScreen::judgeLesson(float dt, bool checkpoint, float impact) {
    const Racer &player = _racers.front();
    LessonJudge::Frame f;
    f.dt = dt;
    f.clock = _clock;
    f.checkpoint = checkpoint;
    f.finished = player.next >= _targets.size();
    f.speedMph = std::fabs(player.car->sim.forwardSpeed()) * kMph;
    f.impact = std::max(impact, player.car->sim.impact); // against the city, and the traffic since
    f.carTouches = _life.carTouches();
    f.chasing = _life.police().chasing();
    f.busted = _life.police().busted();
    f.wrecked = _wear.wrecked();
    // The car to follow: the first opponent.
    if (_racers.size() > 1) {
        f.targetDistance = flatDistance(player.car->sim.position, _racers[1].car->sim.position);
        f.targetDone = _racers[1].driver.finished();
        // Rammed into submission: turned over, or held stopped 2 s.
        const CarSim &target = _racers[1].car->sim;
        _targetStill = _clock > 1 && length(target.velocity) < 1 ? _targetStill + dt : 0;
        f.targetStopped = target.orientation.y[1] < 0.5f || _targetStill >= 2;
    }
    f.police = int(_life.police().count());
    const auto before = _judge.result();
    const auto now = _judge.step(f);
    if (_state != State::kFinished && now != before && now != LessonJudge::Result::kGoing) {
        _outcome = now == LessonJudge::Result::kPassed ? "Lesson passed" : "Lesson failed";
        if (now == LessonJudge::Result::kPassed && player.finishTime < 0)
            _racers.front().finishTime = _clock;
        _state = State::kFinished;
    }
}

void RaceScreen::handleEvent(const Event &ev) {
    if (!_renderer)
        return;
    if (_help && _help->isOpen()) {
        _help->handle(ev);
        return;
    }
    if (_game && _game->isOpen()) {
        gameMenuEvent(ev);
        return;
    }
    if (netEvent(ev))
        return;
    if (ev.type == EventType::kKeyUp) {
        _keys &= ~driveKeyOf(ev.key);
        _view.press(controlOf(ev.key), false);
        return;
    }
    if (ev.type == EventType::kMouseMove) {
        if (_state == State::kMenu && _front)
            menuEvent(ev); // pointed at
        return;
    }
    if (ev.type == EventType::kMouseDown) {
        // A click: on the game's menus, the race's pause menu, results or
        // replay bar.
        if (ev.button != MouseButton::kLeft)
            return;
        if (_state == State::kMenu) {
            if (_front)
                menuEvent(ev);
        } else if (_replaying)
            replayEvent(ev);
        else if (_results)
            resultsEvent(ev);
        else if (_paused)
            pauseEvent(ev);
        return;
    }
    if (ev.type != EventType::kKeyDown)
        return;
    if (_state == State::kMenu && _front) {
        menuEvent(ev);
        return;
    }
    if (_state == State::kMenu) {
        const int fields = 5;
        auto wrap = [](int v, int n) { return n > 0 ? (v % n + n) % n : 0; };
        if (ev.key == key::kUp)
            _field = wrap(_field - 1, fields);
        else if (ev.key == key::kDown)
            _field = wrap(_field + 1, fields);
        else if (ev.key == key::kLeft || ev.key == key::kRight) {
            const int step = ev.key == key::kRight ? 1 : -1;
            switch (_field) {
            case 0:
                _city = wrap(_city + step, int(_cities.size()));
                loadCity();
                break;
            case 1:
                _kind = wrap(_kind + step, kKindCount);
                break;
            case 2:
                _raceIndex = wrap(_raceIndex + step, int(_raceNames.size()));
                break;
            case 3:
                _level = wrap(_level + step, 2);
                break;
            case 4:
                _car = wrap(_car + step, int(_cars.size()));
                break;
            }
            if (_field == 1 || _field == 2) {
                const std::string &map = _cities[size_t(_city)].map;
                _raceNames = raceNames(_ctx.files, map, kKinds[_kind]);
                _raceIndex = std::min(_raceIndex, std::max(0, int(_raceNames.size()) - 1));
                if (!_raceNames.empty())
                    loadRace(_ctx.files, map, kKinds[_kind], _raceIndex, _race);
            }
        } else if (ev.key == key::kReturn || ev.key == key::kKpEnter) {
            startRace();
        } else if (ev.key == 'u') {
            _openAll = !_openAll;
            _error.clear();
        }
        return;
    }
    if (_replaying) {
        replayEvent(ev);
        return;
    }
    if (_results) {
        resultsEvent(ev);
        return;
    }
    if (_paused) {
        pauseEvent(ev);
        return;
    }
    if (!_netGame && ev.key == key::kEscape) {
        // Esc stops the race: the game's in-race menu.
        _paused = true;
        _pauseFocus = 0;
        _keys = 0;
        return;
    }
    if (!_netGame && _state == State::kFinished && (ev.key == key::kReturn || ev.key == key::kKpEnter)) {
        _results = true; // the game's results screen
        _resultsFocus = nextRaceOpen() ? 1 : 0;
        return;
    }
    if (_state == State::kFinished && !_netGame && ev.key == 'i' && !_recording.empty()) {
        instantReplay(ReplayReturn::kResults); // the instant replay
        return;
    }
    if (_state == State::kFinished && lesson() &&
        (ev.key == 'r' || ev.key == key::kReturn || ev.key == key::kKpEnter)) {
        // Restart the lesson (R), or (Enter) go on to the next once passed.
        const bool next =
            ev.key != 'r' && _judge.result() == LessonJudge::Result::kPassed && _raceIndex + 1 < int(_raceNames.size());
        if (ev.key == 'r' || next) {
            _raceIndex += next ? 1 : 0;
            _life.stop();
            startRace();
            return;
        }
    }
    if (ev.key == key::kEscape || (_state == State::kFinished && (ev.key == key::kReturn || ev.key == key::kKpEnter))) {
        endRace();
        return;
    }
    if (ev.key == key::kF1) {
        // The controls' list (a race alone stops while it's up).
        _view.controlsList = !_view.controlsList;
        _keys = 0;
        return;
    }
    const Control control = controlOf(ev.key);
    if (const uint32_t d = driveKeyOf(ev.key)) {
        _keys |= d;
    } else if (control == Control::kChangeCamera) {
        _camera = (_camera + 1) % int(_cameras.size());
        _cameraReset = true;
        _blend.start(_lastView, _cameras[size_t(_camera)].blendTime);
    } else if (control == Control::kMapZoom) {
        _hudMap.toggleZoom();
    } else if (control == Control::kFullScreenMap) {
        _hudMap.toggleFullScreen(); // in a race alone, it stops the race (see update)
        _keys = 0;
    } else if (control == Control::kMapToggle) {
        _hudMap.toggleShown();
    } else if (control == Control::kHudToggle) {
        _hudShown = !_hudShown;
    } else if (control == Control::kRearViewMirror) {
        _mirror = !_mirror;
    } else if (control == Control::kNextCheckpoint || control == Control::kPrevCheckpoint) {
        pickCheckpoint(control == Control::kNextCheckpoint ? 1 : -1);
    } else if (control == Control::kRotatingMap) {
        _hudMap.toggleRotating();
    } else if (_cd.press(control, _music, _ctx.files, _ctx.mixer)) {
    } else if (_view.press(control, true)) {
        _cameraReset = _cameraReset || control == Control::kThrillCam;
    } else if (control == Control::kTransmission) {
        _manual = !_manual;
    } else if (control == Control::kShiftUp || control == Control::kShiftDown) {
        _shift = control == Control::kShiftUp ? 1 : -1;
    }
}

void RaceScreen::menuEvent(const Event &ev) {
    const FrontEnd::Action action = _front->handle(ev);
    if (action == FrontEnd::Action::kMinimise) {
        _ctx.sys.minimizeWindow();
        return;
    }
    if (action == FrontEnd::Action::kHelp) {
        // The game's help file, read the first time it's asked for.
        if (!_help) {
            _help = std::make_unique<HelpViewer>();
            if (Error err = _help->load(_ctx.files))
                _error = err.message;
        }
        _help->open();
        return;
    }
    if (action == FrontEnd::Action::kReplays && _game) {
        _game->open(GameMenus::Screen::kReplayList);
        return;
    }
    if (_game && (action == FrontEnd::Action::kNewDriver || action == FrontEnd::Action::kDeleteDriver ||
                  action == FrontEnd::Action::kDriverStats || action == FrontEnd::Action::kRaceRecords)) {
        _game->open(action == FrontEnd::Action::kNewDriver      ? GameMenus::Screen::kNewDriver
                    : action == FrontEnd::Action::kDeleteDriver ? GameMenus::Screen::kDeleteDriver
                    : action == FrontEnd::Action::kDriverStats  ? GameMenus::Screen::kDriverStats
                                                                : GameMenus::Screen::kRaceRecords);
        if (const std::string clip = _game->takeVoice(); !clip.empty())
            playVoice(clip); // "You cannot delete your only driver"
        return;
    }
    if (action == FrontEnd::Action::kDriverChanged) {
        switchDriver();
        return;
    }
    if (_game && (action == FrontEnd::Action::kRaceOptions || action == FrontEnd::Action::kCityChoice ||
                  action == FrontEnd::Action::kLocked)) {
        _game->open(action == FrontEnd::Action::kRaceOptions  ? GameMenus::Screen::kRaceOptions
                    : action == FrontEnd::Action::kCityChoice ? GameMenus::Screen::kCitySelection
                                                              : GameMenus::Screen::kLocked);
        if (const std::string clip = _game->takeVoice(); !clip.empty())
            playVoice(clip);
        return;
    }
    _openAll = _front->openAll;
    if (action == FrontEnd::Action::kOptions || action == FrontEnd::Action::kQuit) {
        if (_game)
            _game->open(action == FrontEnd::Action::kOptions ? GameMenus::Screen::kOptions : GameMenus::Screen::kQuit);
        else if (action == FrontEnd::Action::kQuit)
            _quit = true;
        return;
    }
    if (action == FrontEnd::Action::kMultiplayer && _net) {
        _netMenus = true;
        _net->enter();
        return;
    }
    if (_garageForNet) {
        // The lobby's car: chosen (GO DRIVE), or left as it was (Esc).
        const FrontEnd::Choice &c = _front->choice();
        if (action == FrontEnd::Action::kStart || _front->screen() != FrontEnd::Screen::kGarage) {
            _garageForNet = false;
            _netMenus = true;
            if (action == FrontEnd::Action::kStart)
                _net->carChosen(_cars[size_t(c.car)].base, c.paintJob);
            else
                _net->showLobby();
        }
        return;
    }
    if (action != FrontEnd::Action::kStart)
        return;
    // The menus' choice: the city (loaded now if it's another), the race,
    // the level, the car, its colour and gearbox.
    const FrontEnd::Choice &c = _front->choice();
    _conditions = c.conditions;
    _conditionsChanged = c.changed && c.kind != RaceKind::kCrashCourse;
    for (int k = 0; k < kKindCount; ++k)
        if (kKinds[k] == c.kind)
            _kind = k;
    _level = c.professional ? 1 : 0;
    _car = c.car;
    _paint = c.paintJob;
    _manual = c.manual;
    if (_city != c.city) {
        _city = c.city;
        loadCity();
    }
    _raceNames = raceNames(_ctx.files, _cities[size_t(_city)].map, kKinds[_kind]);
    _raceIndex = std::clamp(c.race, 0, std::max(0, int(_raceNames.size()) - 1));
    _showCar.reset();
    _showCarIndex = -1;
    startRace();
}

void RaceScreen::drawGarageCar(float dt) {
    // The chosen car turning in the garage's window, on dark blue.
    const FrontEnd::Rect r = _front->carWindow();
    const FrontEnd::Choice &c = _front->choice();
    if (!_showCar || _showCarIndex != c.car || _showPaint != c.paintJob) {
        _showCar = std::make_unique<CarActor>();
        if (_showCar->load(*_renderer, _scene->textures(), _ctx.files, _cars[size_t(c.car)].base,
                           uint32_t(c.paintJob))) {
            _showCar.reset();
            return;
        }
        bool translucent = false; // the showroom's reflection
        if (const gfx::TextureId room = _scene->textures().get("refl_showroom", translucent, true))
            _showCar->reflectionMap = room;
        _showCarIndex = c.car;
        _showPaint = c.paintJob;
    }
    _orbit += dt * 0.5f;
    _showCar->sim.reset(_showCar->setup, {0, 0, 0}, _orbit);
    const bool show = _front->screen() == FrontEnd::Screen::kShowcase;
    const Vec3 eye = show ? Vec3{0, 2.4f, 13.0f} : Vec3{0, 2.2f, 7.5f}, at{0, 0.7f, 0};
    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(35 * kPi / 180, float(r.w) / float(r.h), 0.1f, 100),
                                             gfx::lookAlong(eye, normalized(at - eye), {0, 1, 0}));
    _renderer->setView(viewProj, eye);
    _renderer->setViewport(r.x, r.y, r.w, r.h);
    SceneLight light;
    _showCar->draw(light);
    _renderer->setViewport(0, 0, 0, 0);
}

void RaceScreen::drawCheckpoints() {
    // The game's gates: pt_check (pt_finish at the finish line) is a whole
    // gate, two posts and a banner, 2 across and 1 high about its middle,
    // scaled to the checkpoint (as on the game's screens: across the road,
    // half as high), across the way its heading points. Lessons' have the
    // Crash Course banner (their second paint job). The next two, or in a
    // race in any order all those left.
    if (_racers.empty())
        return;
    const Racer &player = _racers.front();
    std::vector<size_t> gates;
    if (anyOrder())
        gates = remaining();
    else
        for (size_t k = player.next; k < _targets.size() && k < player.next + 2; ++k)
            gates.push_back(k);
    for (size_t k : gates) {
        const bool finish = k + 1 == _targets.size() || _targets[k] == 0;
        const ModelCache::Model &gate = _scene->models().get(finish ? "pt_finish" : "pt_check", lesson() ? 1 : 0);
        if (!gate.ok)
            continue;
        const Waypoint &w = targetPoint(k);
        const float radius = w.radius > 0 ? w.radius : kCircuitRadius;
        const Mat3 turn = Mat3::rotationY(headingToYaw(w.heading));
        const Mat3 sized{turn.x * radius, turn.y * radius, turn.z * radius};
        // Opaque, then its see-through parts (the gate's texture has alpha).
        for (bool translucent : {false, true})
            drawModelMesh(*_renderer, gate.levels.front(), matrixOf(sized, w.position + Vec3{0, radius / 2, 0}),
                          _scene->light(), translucent);
    }
}

void RaceScreen::drawArrow() {
    // The game's HUD arrow (hudarrow01, hudarrow_blitz01 in a Blitz) above
    // the car, pointing to the next checkpoint (in any order, the nearest
    // or the one picked).
    const Racer &player = _racers.front();
    const size_t k = targetOf(player);
    if (k >= _targets.size())
        return;
    // No way to point while there's a car to destroy (as on the game's
    // screens): the car's marker shows it.
    if (lesson() && _race.lesson.event == LessonEvent::kDestroy && !_judge.destroyed())
        return;
    // The lessons with their row's second extra column set (The Knowledge,
    // Brake Dancing) have none: the map shows the way (as on the game's
    // screens).
    if (lesson() && _race.lesson.extra[1] != 0)
        return;
    drawArrowTo(targetPoint(k).position, _race.kind == RaceKind::kBlitz);
}

void RaceScreen::drawArrowTo(const Vec3 &target, bool blitz, bool little) {
    const Racer &player = _racers.front();
    const Vec3 at = player.car->sim.position + Vec3{0, little ? 2.0f : 2.8f, 0};
    Vec3 to = target - at;
    to[1] = 0;
    // Its second paint job (yellow) when the way is behind the car, as the
    // game's loading tips say.
    Vec3 ahead = -player.car->sim.orientation.z;
    ahead[1] = 0;
    const bool behind = dot(ahead, to) < 0 && !little;
    const ModelCache::Model &arrow = _scene->models().get(blitz ? "hudarrow_blitz01" : "hudarrow01", behind ? 1 : 0);
    if (!arrow.ok)
        return;
    const float yaw = length(to) > 0.1f ? std::atan2(-to[0], -to[2]) : 0;
    SceneLight bright;
    bright.ambient = {1, 1, 1};
    bright.directed = {0, 0, 0};
    for (const ModelDraw &d : arrow.levels.front().draws) {
        gfx::DrawCall call;
        call.mesh = arrow.levels.front().mesh;
        call.firstIndex = d.firstIndex;
        call.indexCount = d.indexCount;
        Mat3 turn = Mat3::rotationY(yaw);
        if (little)
            turn = Mat3{turn.x * 0.5f, turn.y * 0.5f, turn.z * 0.5f};
        call.model = matrixOf(turn, at);
        call.texture = d.texture;
        call.state.depthTest = false;
        call.state.cull = gfx::Cull::kNone;
        _renderer->draw(call);
    }
}

void RaceScreen::drawOpponentMarkers() {
    // Over each opponent the game's marker: a purple triangle pointing down
    // at it and, once the race is on, its place in opp_icon's digits above
    // (measured on the game's screens: the triangle 1.5 m across, 2.3 m
    // tall, its point 4.5 m over the car; the digit as wide, over it).
    constexpr float kTip = 4.5f, kTop = 6.8f, kHalfWidth = 0.75f, kDigit = 8.0f, kDigitSize = 1.4f;
    constexpr uint8_t kPurple[3] = {192, 16, 255};
    std::vector<const Racer *> order;
    for (const Racer &r : _racers)
        order.push_back(&r);
    std::stable_sort(order.begin(), order.end(),
                     [&](const Racer *a, const Racer *b) { return progressOf(*a) > progressOf(*b); });
    struct Marker {
        float depth, x, tipY, topY, pixelsPerMetre;
        int place;
    };
    std::vector<Marker> markers;
    for (size_t i = 0; i < order.size(); ++i) {
        const Racer &r = *order[i];
        if (r.player)
            continue;
        const Vec3 at = r.car->sim.position;
        const Vec3 tip = gfx::transformPoint(_viewProj, at + Vec3{0, kTip, 0});
        const Vec3 top = gfx::transformPoint(_viewProj, at + Vec3{0, kTop, 0});
        if (tip[2] <= 0 || tip[2] >= 1 || top[2] <= 0 || top[2] >= 1)
            continue; // behind the eye, or too far
        auto px = [](float ndc, int size) { return (ndc * 0.5f + 0.5f) * float(size); };
        const float tipY = float(kFrameH) - px(tip[1], kFrameH), topY = float(kFrameH) - px(top[1], kFrameH);
        markers.push_back({tip[2], px(tip[0], kFrameW), tipY, topY, (tipY - topY) / (kTop - kTip), int(i + 1)});
    }
    // The farthest first, so the nearer go over them.
    std::sort(markers.begin(), markers.end(), [](const Marker &a, const Marker &b) { return a.depth > b.depth; });
    for (const Marker &m : markers) {
        if (m.x < -200 || m.x > kFrameW + 200 || m.pixelsPerMetre <= 0)
            continue;
        const float half = kHalfWidth * m.pixelsPerMetre;
        for (int y = std::max(0, int(m.topY)); y < std::min(kFrameH, int(m.tipY)); ++y) {
            const float w = half * (m.tipY - float(y)) / std::max(1.0f, m.tipY - m.topY);
            uint8_t *row = _overlay.row(y);
            for (int x = std::max(0, int(m.x - w)); x < std::min(kFrameW, int(m.x + w) + 1); ++x) {
                uint8_t *d = row + x * 4;
                d[0] = kPurple[0];
                d[1] = kPurple[1];
                d[2] = kPurple[2];
                d[3] = 255;
            }
        }
        if (_clock >= 0 && m.place <= 8)
            _pictures.place(_overlay, m.place, int(m.x), int(m.tipY - (kDigit - kTip) * m.pixelsPerMetre),
                            std::max(4, int(kDigitSize * m.pixelsPerMetre)));
    }
}

void RaceScreen::drawCheckpointMarkers() {
    // In a race in any order, over each checkpoint left a light blue
    // diamond, long and narrow (measured on the game's screens: about
    // 2.2 m across, 5.5 m tall, 13 m up), drawn over everything.
    if (!anyOrder())
        return;
    constexpr float kMiddle = 13, kHalfHeight = 2.75f, kHalfWidth = 1.1f, kWidest = 0.4f;
    constexpr uint8_t kBlue[3] = {10, 230, 240};
    for (size_t k : remaining()) {
        const Vec3 at = targetPoint(k).position;
        const Vec3 top = gfx::transformPoint(_viewProj, at + Vec3{0, kMiddle + kHalfHeight, 0});
        const Vec3 bottom = gfx::transformPoint(_viewProj, at + Vec3{0, kMiddle - kHalfHeight, 0});
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
            uint8_t *row = _overlay.row(y);
            for (int xx = std::max(0, int(x - w)); xx < std::min(kFrameW, int(x + w) + 1); ++xx) {
                uint8_t *d = row + xx * 4;
                d[0] = kBlue[0];
                d[1] = kBlue[1];
                d[2] = kBlue[2];
                d[3] = 255;
            }
        }
    }
}

void RaceScreen::drawMap(float dt) {
    // The checkpoints still to reach (the next green, the finish its own),
    // the opponents, the police and the player.
    _hudMap.update(dt);
    const Racer &player = _racers.front();
    std::vector<HudMapIcon> icons;
    std::vector<size_t> left;
    if (anyOrder())
        left = remaining();
    else
        for (size_t i = player.next; i < _targets.size(); ++i)
            left.push_back(i);
    const size_t aim = targetOf(player);
    for (size_t i : left)
        icons.push_back({targetPoint(i).position,
                         {0, 0, -1},
                         i + 1 == _targets.size() ? hudcolor::kFinish
                         : i == aim               ? hudcolor::kNext
                                                  : hudcolor::kCheckpoint,
                         true});
    for (CarSim *c : _life.police().cars())
        icons.push_back({c->position, -c->orientation.z, hudcolor::kPolice, false});
    if (_netGame && _net && _net->session() && _net->session()->settings().mode == NetMode::kCopsRobbers) {
        // The gold, and the bases: ours the finish's colour.
        const NetSession &session = *_net->session();
        const NetPlayer *me = session.me();
        GoldGame::Car self{session.myId(), me ? me->team : uint8_t(0), {}, {}};
        icons.push_back({_gold.goldPosition(), {0, 0, -1}, hudcolor::kNext, true});
        for (uint8_t side = 0; side < (_gold.teams() ? 2 : 1); ++side)
            icons.push_back({_gold.base(side),
                             {0, 0, -1},
                             side == _gold.sideOf(self) || !_gold.teams() ? hudcolor::kFinish : hudcolor::kCheckpoint,
                             true});
    }
    for (const Racer &r : _racers)
        if (!r.player)
            icons.push_back({r.car->sim.position, -r.car->sim.orientation.z, hudcolor::kOpponent, false});
    const CarSim &sim = player.car->sim;
    icons.push_back({sim.position, -sim.orientation.z, hudcolor::kPlayer, false});
    _hudMap.draw(*_renderer, _scene->models(), sim.position, -sim.orientation.z, icons, kFrameW, kFrameH);
}

void RaceScreen::drawMenu() {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    const int lh = font::kLineHeight * 2;
    int y = 60;
    font::drawText(_overlay, 40, 20, "RACE", {255, 210, 80}, 3);
    const std::string kind = raceKindName(kKinds[_kind]);
    std::string raceName = _raceNames.empty() ? "(none)" : _raceNames[size_t(_raceIndex)];
    const std::string &map = _cities[size_t(_city)].map;
    if (!_raceNames.empty()) {
        // Locked, or what the player's done in it.
        static const char *const kPlaces[] = {"", "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
        const int best = _record.bestPlace(map, kKinds[_kind], _raceIndex, _level == 1);
        if (!raceIsOpen(_raceIndex))
            raceName += "  [locked]";
        else if (lesson() ? _record.hasPassed(map, _raceIndex) : best > 0)
            raceName += lesson() ? "  (passed)" : std::string("  (best: ") + kPlaces[std::min(best, 8)] + ")";
    }
    const bool carOpen = carIsOpen(_car);
    const std::string lines[5] = {"City:   " + _cities[size_t(_city)].name, "Event:  " + kind, "Race:   " + raceName,
                                  std::string("Level:  ") + (_level ? "Professional" : "Amateur"),
                                  "Car:    " + _cars[size_t(_car)].description + (carOpen ? "" : "  [locked]")};
    for (int i = 0; i < 5; ++i) {
        const Color c = i == _field ? Color{255, 255, 120} : Color{230, 230, 230};
        font::drawText(_overlay, 40, y, (i == _field ? "> " : "  ") + lines[i], c, 2);
        y += lh + 4;
    }
    if (!carOpen)
        for (const UnlockRule &r : unlockRules())
            if (r.car == _cars[size_t(_car)].base && r.paintJob < 0) {
                font::drawText(_overlay, 40, y, unlockText(r, _level == 1), {255, 230, 120});
                y += font::kLineHeight + 4;
            }
    if (!_raceNames.empty() && lesson()) {
        static const char *const kAsks[] = {"Take the jump: every checkpoint in time",
                                            "",
                                            "Follow that car, and don't let it escape",
                                            "Lose the police before the finish",
                                            "Keep up the speed at every checkpoint",
                                            "Through the traffic without touching a car",
                                            "",
                                            "Every checkpoint in time",
                                            "Drive cleanly: each knock costs 5 seconds",
                                            "Find your way to every checkpoint in time"};
        const int e = int(_race.lesson.event);
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s; %zu checkpoints, time limit %.0f s",
                      e >= 0 && e < 10 && kAsks[e][0] ? kAsks[e] : "Every checkpoint", _race.waypoints.size() - 1,
                      _race.lesson.timeLimit);
        font::drawText(_overlay, 40, y + 10, buf, {180, 180, 180});
    } else if (!_raceNames.empty()) {
        const RaceSettings &s = _race.settings[size_t(_level)];
        char buf[200];
        std::snprintf(buf, sizeof buf, "%zu checkpoints, %d opponents, %d laps, time limit %.0f s",
                      _race.waypoints.size() - 1, s.opponents, _race.kind == RaceKind::kCircuit ? s.laps : 1,
                      s.timeLimit);
        font::drawText(_overlay, 40, y + 10, buf, {180, 180, 180});
        y += font::kLineHeight + 4;
        std::snprintf(buf, sizeof buf, "traffic %.0f%%, pedestrians %.0f%%, police %d", s.ambient * 100, s.peds * 100,
                      s.cops);
        font::drawText(_overlay, 40, y + 10, buf, {180, 180, 180});
    }
    font::drawText(_overlay, 40, kFrameH - 40,
                   std::string("Up/Down: choose  Left/Right: change  Enter: race  U: open everything (") +
                       (_openAll ? "on" : "off") + ")",
                   {150, 200, 255});
    if (!_error.empty())
        font::drawText(_overlay, 40, kFrameH - 24, _error, {255, 120, 120});
}

void RaceScreen::drawRaceHud() {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    if (_netGame && !NetMenus::hasRace(_net && _net->session() ? _net->session()->settings().mode : NetMode::kCruise)) {
        drawNetHud();
        return;
    }
    if (cruising()) {
        drawCruiseHud();
        return;
    }
    const Racer &player = _racers.front();
    const RaceSettings &set = _race.settings[size_t(_level)];
    drawCheckpointMarkers();
    drawOpponentMarkers();
    // Time, or time left in a Blitz or a lesson with a limit: the game's
    // digits, top middle.
    const float lessonLeft = lesson() ? _judge.timeLeft(_clock) : -1;
    const bool blitz = (_race.kind == RaceKind::kBlitz && set.timeLimit > 0) || lessonLeft >= 0;
    const float shown = lessonLeft >= 0 ? lessonLeft
                        : blitz         ? set.timeLimit - std::max(0.0f, _clock)
                                        : std::max(0.0f, _clock);
    if (!_hudShown || (lesson() && _race.lesson.event == LessonEvent::kFollow))
        ; // the HUD Toggle's off (no clock, list or gauges), or a follow lesson's untimed
    else if (_pictures.loaded())
        _pictures.clock(_overlay, shown, kFrameW / 2, 0);
    else
        drawCentred(clockText(shown), 4, blitz && shown < 10 ? Color{255, 90, 90} : Color{120, 255, 120}, 3);
    // The game's list, top left (its words: Place, Check, Lap): the place
    // against opponents, the checkpoints this lap, the lap, and each lap's
    // time, the one under way last.
    const bool circuit = _race.kind == RaceKind::kCircuit;
    const size_t perLap = std::max<size_t>(1, _race.waypoints.size() - (circuit ? 0 : 1));
    const int laps = std::max(1, set.laps);
    const int lap = std::min(int(player.next / perLap), laps - 1);
    char buf[80];
    int listY = kListTop;
    auto listLine = [&](const std::string &label, const std::string &value, int valueX) {
        if (!_hudShown)
            return;
        font::drawText(_overlay, 2, listY, label, kListLabel);
        font::drawText(_overlay, valueX, listY, value, {255, 255, 255});
        listY += kListStep;
    };
    auto word = [&](uint32_t id, const char *fallback) {
        std::string w = _strings.get(id, fallback);
        while (!w.empty() && w.back() == ' ')
            w.pop_back();
        return w;
    };
    if (_racers.size() > 1 && !lesson()) {
        int place = 1;
        const float mine = progressOf(player);
        for (const Racer &r : _racers)
            if (!r.player && progressOf(r) > mine)
                ++place;
        std::snprintf(buf, sizeof buf, "%d/%zu", place, _racers.size());
        listLine(word(259, "Place:"), buf, 50);
    }
    if (circuit) {
        std::snprintf(buf, sizeof buf, "%zu/%zu", player.next >= _targets.size() ? perLap : player.next % perLap,
                      perLap);
        listLine(word(260, "Check:"), buf, 50);
        std::snprintf(buf, sizeof buf, "%d/%d", lap + 1, laps);
        listLine(word(261, "Lap:"), buf, 50);
        for (size_t i = 0; i < _lapEnds.size(); ++i)
            listLine(std::to_string(i + 1) + ".", splitText(_lapEnds[i] - (i ? _lapEnds[i - 1] : 0)), 28);
        if (_lapEnds.size() < size_t(laps) && _clock > 0)
            listLine(std::to_string(_lapEnds.size() + 1) + ".",
                     splitText(_clock - (_lapEnds.empty() ? 0 : _lapEnds.back())), 28);
    } else {
        std::snprintf(buf, sizeof buf, "%zu/%zu", std::min(player.next, _targets.size()), _targets.size());
        listLine(word(255, "Check:"), buf, 50);
    }
    // After each checkpoint, the time so far (this lap's in a circuit)
    // under the car; at a lap's end with "Lap time" over it.
    if (_splitFor > 0) {
        drawCentred(splitText(_split), kSplitY, kMessage, 1);
        if (_splitIsLap)
            drawCentred(_strings.get(63, "Lap time"), kSplitY - 22, kMessage, 1);
    }
    if (lesson()) {
        // The game's message for the moment.
        if (const uint32_t id = _judge.message()) {
            std::string text = _strings.get(id);
            if (text.find("%.0f") != std::string::npos) {
                char line[160];
                std::snprintf(line, sizeof line, text.c_str(), double(_judge.minimumSpeed()));
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
        if (_judge.penalty() > 0) {
            std::snprintf(buf, sizeof buf, "PENALTY %.0f s", _judge.penalty());
            font::drawText(_overlay, 2, listY, buf, {255, 120, 90});
        }
    }
    // The countdown, as the game words it: Ready... Set... Go! (The stunt's
    // words after the start too.) Where the game shows its messages: over
    // the middle of the road.
    if (stunt()) {
        if (_clock < -kCountdown / 2)
            drawCentred(raceMessage(0, "Race to the end of the pier!"), kMessageY, kMessage, 2);
        else if (_clock < 0)
            drawCentred(raceMessage(1, "But don't fall in!"), kMessageY, kMessage, 2);
        else if (_clock < 1)
            drawCentred(raceMessage(2, "Go!"), kMessageY, kMessage, 2);
        else if (_stuntMessageFor > 0)
            drawCentred(_strings.get(_stuntMessage), kMessageY, kMessage, 2);
    } else if (!lesson()) {
        if (_clock < -kCountdown / 2)
            drawCentred(raceMessage(0, "Ready..."), kMessageY, kMessage, 2);
        else if (_clock < 0)
            drawCentred(raceMessage(1, "Set..."), kMessageY, kMessage, 2);
        else if (_clock < 1)
            drawCentred(raceMessage(2, "Go!"), kMessageY, kMessage, 2);
    } else if (_clock < 0) {
        drawCentred(std::to_string(int(std::ceil(-_clock))), kMessageY, kMessage, 2);
    }
    if (_finalLapFor > 0)
        drawCentred(_strings.get(62, "Final lap!"), kSplitY - 22, kMessage, 1);
    // Put back after sinking: the game's word for it, San Francisco's
    // seen in the footage, London's (642, by the other) Reluva's reading.
    if (_sunkFor > 0)
        drawCentred(_cities[size_t(_city)].map == "london" ? _strings.get(642, "More tea, vicar?")
                                                           : _strings.get(30, "Sleep with the fishes!"),
                    kMessageY, kMessage, 2);
    if (_state == State::kFinished && lesson()) {
        font::drawText(_overlay, 60, 170, _outcome, {255, 255, 120}, 3);
        const bool passed = _judge.result() == LessonJudge::Result::kPassed;
        const bool more = _raceIndex + 1 < int(_raceNames.size());
        font::drawText(_overlay, 60, 210,
                       passed && more ? "Enter: next lesson   R: restart lesson   Esc: back to the menu"
                                      : "R: restart lesson   Enter: back to the menu",
                       {150, 200, 255});
    } else if (_state == State::kFinished) {
        font::drawText(_overlay, 60, 120, _outcome, {255, 255, 120}, 3);
        if (!_beatenAll.empty())
            font::drawText(_overlay, 60, 150, _beatenAll, {120, 255, 120}, 2);
        // The order the racers finished in, those still racing after.
        std::vector<const Racer *> order;
        for (const Racer &r : _racers)
            order.push_back(&r);
        std::stable_sort(order.begin(), order.end(),
                         [&](const Racer *a, const Racer *b) { return progressOf(*a) > progressOf(*b); });
        int y = 170;
        for (size_t i = 0; i < order.size(); ++i) {
            const Racer &r = *order[i];
            std::snprintf(buf, sizeof buf, "%zu. %-40s %s", i + 1, r.name.c_str(),
                          r.finishTime >= 0 ? clockText(r.finishTime).c_str() : "racing");
            font::drawText(_overlay, 60, y, buf, r.player ? Color{255, 255, 120} : Color{230, 230, 230});
            y += font::kLineHeight + 2;
        }
        const bool hosting = _netGame && _net && _net->session() && _net->session()->isHost();
        font::drawText(_overlay, 60, y + 10,
                       !_netGame ? (_replaying ? "" : "Enter: back to the menu   I: instant replay")
                       : hosting ? "Enter: everyone back to the lobby"
                                 : "Waiting for the host (Esc: leave the session)",
                       {150, 200, 255});
    }
    if (_life.police().chasing() > 0 && _state != State::kFinished)
        font::drawText(_overlay, 4, kFrameH - 150, "POLICE CHASING", {255, 90, 90}, 2); // above the speedometer
    if (_netGame)
        drawNetLines();
    if (_hudShown)
        drawSpeedometer(_overlay, player.car->sim,
                        _hudMap.loaded() ? int(_hudMap.setup().left * float(kFrameW)) - 8 : kFrameW - 8,
                        _wear.fraction(), _pictures);
}

void RaceScreen::update(uint64_t nowMs) {
    if (!_renderer || _cars.empty())
        return;
    _nowMs = nowMs;
    // The game controller: its sticks drive (not while a replay plays back
    // its own), its buttons press keys.
    {
        const bool driving = _state != State::kMenu && !_racers.empty() && !_paused && !_results && !_replaying &&
                             !(_game && _game->isOpen()) && !(_help && _help->isOpen());
        std::vector<Event> keys;
        pollPad(_ctx.sys, _pad, driving, !_replaying, currentOptions(), keys);
        for (const Event &ev : keys)
            handleEvent(ev);
        _shaker.update(_ctx.sys, nowMs);
    }
    if (_net)
        netUpdate(nowMs);
    const float dt = _lastMs ? std::min(0.1f, float(nowMs - _lastMs) / 1000) : 0;
    _resultsTime = _results ? _resultsTime + dt : 0;
    _lastMs = nowMs;

    Vec3 eye, target, up{0, 1, 0};
    float fov = 70, nearClip = 0.1f;
    if (_state == State::kMenu || _racers.empty()) {
        // Round the chosen race's start.
        _orbit += dt * 0.15f;
        const Vec3 centre = _race.waypoints.empty() ? _scene->start : _race.waypoints[0].position;
        eye = centre + Vec3{std::sin(_orbit) * 40, 18, std::cos(_orbit) * 40};
        target = centre;
    } else {
        // After the finish the others race on to theirs, for the results
        // (stopped while paused, or on the results screen).
        if (_replaying)
            stepReplay(dt);
        else if (!_paused && !_results && !((_hudMap.fullScreen() || _view.controlsList) && !_netGame)) {
            // (The full-screen map stops a race alone, as the game's does.)
            recordFrame(dt);
            stepRace(dt);
            markRecording();
        }
        GameMusic::State music;
        // Waiting for the start counts as on the move, not stopped.
        music.speed = _state == State::kCountdown ? 100 : std::fabs(_racers.front().car->sim.forwardSpeed());
        music.chased = _life.police().chasing() > 0;
        music.finished = _state == State::kFinished;
        music.paused = _paused;
        if (_state == State::kRacing && !lesson() && !_targets.empty()) {
            const Racer &me = _racers.front();
            const int laps = _race.settings[size_t(_level)].laps;
            if (_race.kind == RaceKind::kCircuit)
                music.lastStretch = laps > 1 && _lastLap == laps - 1;
            else if (anyOrder())
                music.lastStretch = remaining(me) == std::vector<size_t>{_targets.size() - 1};
            else
                music.lastStretch = me.next + 1 == _targets.size();
        }
        _music.update(dt, music);
        // A second off the ground: the tune's big-air motif, once a jump.
        {
            const CarSim &air = _racers.front().car->sim;
            const bool off =
                std::none_of(air.wheels.begin(), air.wheels.end(), [](const CarSim::Wheel &w) { return w.contact; });
            _airFor = off ? _airFor + dt : 0;
            if (_airFor >= kBigAir && !_airPlayed) {
                _music.bigAir();
                _airPlayed = true;
            }
            _airPlayed = _airPlayed && off;
        }
        const CarCamera &cam = _cameras[size_t(_camera)];
        fov = cam.fov;
        const CameraView view =
            _replaying
                ? replayCamera(dt, fov)
                : _view.apply(
                      _blend.apply(updateCarCamera(cam, _racers.front().car->sim, dt, _cameraPos, _cameraReset), dt),
                      cam, _racers.front().car->sim, dt);
        _lastView = view;
        nearClip = cam.nearClip;
        if (!_replaying)
            fov = _view.fov(fov);
        _cameraReset = false;
        eye = view.eye;
        target = view.target;
        up = view.up;
        // The start: from high over the grid, looking down on the car,
        // down to its place behind it as the countdown runs, as the
        // game's does (the footage: "Ready..." seen from above).
        if (_state == State::kCountdown && !_replaying && _clock < 0) {
            Vec3 back = eye - target;
            back[1] = 0;
            back = length(back) > 0.1f ? normalized(back) : Vec3{0, 0, 1};
            const Vec3 high = target + back * kStartBack + Vec3{0, kStartHigh, 0};
            const float f = std::clamp((_clock + kCountdown) / kCountdown, 0.0f, 1.0f);
            eye = high + (eye - high) * (f * f * (3 - 2 * f));
        }
    }
    const gfx::Mat4 viewProj = gfx::multiply(
        gfx::perspective(fov * kPi / 180, float(kFrameW) / float(kFrameH), nearClip, currentOptions().farClip()),
        gfx::lookAlong(eye, normalized(target - eye), up));
    _viewProj = viewProj;
    if ((_state == State::kMenu || _racers.empty()) && _front) {
        // The game's menus, over a dark frame (the garage's car in its
        // window), with their music and voices.
        menuSounds();
        _renderer->beginFrame(viewProj, eye, {10, 14, 40});
        if (_netMenus && _net) {
            _net->draw(_overlay, nowMs);
        } else if (_game && _game->isOpen() && !_game->isDialog()) {
            _game->draw(_overlay, nowMs);
        } else {
            if (_front->screen() == FrontEnd::Screen::kGarage || _front->screen() == FrontEnd::Screen::kShowcase)
                drawGarageCar(dt);
            _front->draw(_overlay);
            if (_game && _game->isOpen())
                _game->draw(_overlay, nowMs); // a dialog, over the menu
        }
        if (_help)
            _help->draw(_overlay);
        _renderer->endFrame(&_overlay);
        return;
    }
    _renderer->beginFrame(viewProj, eye, {150, 170, 200});
    applyGraphicsOptions(*_scene, _life);
    _scene->draw(viewProj, eye, true);
    if (_state == State::kMenu || _racers.empty()) {
        drawMenu();
    } else {
        const bool inside = _cameras[size_t(_camera)].inside && !_view.thrill;
        if (_scene->lampsLit() && headlightPoolsOn()) {
            std::vector<gfx::DynamicLight> pools;
            gfx::DynamicLight pool;
            for (const Racer &r : _racers)
                if (r.car->headlightPool(pool))
                    pools.push_back(pool);
            _life.headlightPools(pools, eye);
            _scene->drawDynamicLights(pools, eye);
        }
        _life.draw(*_renderer, _scene->models(), _scene->light(), viewProj, eye, _flares.get());
        _scene->drawBeacons(*_flares, eye);
        for (const Racer &r : _racers)
            if (!(r.player && inside)) {
                r.car->draw(_scene->light());
                r.car->drawFlares(*_flares, eye);
            }
        drawCheckpoints();
        if (_netGame)
            drawGold();
        if (_smoke)
            _smoke->draw(eye);
        if (inside && _dash.loaded() && _view.showDashboard() && _view.look == 0)
            _dash.draw(*_renderer, _scene->models(), _racers.front().car->sim, _wear.fraction(), eye,
                       normalized(target - eye), up, float(kFrameW) / float(kFrameH), _scene->light());
        if (_netGame && _net && _net->session() && _net->session()->settings().mode == NetMode::kCopsRobbers) {
            const NetSession &session = *_net->session();
            const NetPlayer *me = session.me();
            const bool mine = _gold.holder() == session.myId();
            GoldGame::Car self{session.myId(), me ? me->team : uint8_t(0), {}, {}};
            drawArrowTo(mine ? _gold.base(_gold.sideOf(self)) : _gold.goldPosition(), mine);
        } else
            drawArrow();
        if (_view.opponent && !_racers.empty()) {
            // Opponent Position: a little green arrow to the nearest.
            const Racer *nearest = nullptr;
            for (const Racer &r : _racers)
                if (!r.player &&
                    (!nearest || length(r.car->sim.position - _racers.front().car->sim.position) <
                                     length(nearest->car->sim.position - _racers.front().car->sim.position)))
                    nearest = &r;
            if (nearest)
                drawArrowTo(nearest->car->sim.position, false, true);
        }
        if (_mirror)
            drawRearMirror(*_renderer, _racers.front().car->sim, currentOptions().farClip(),
                           [&](const gfx::Mat4 &vp, const Vec3 &eye) {
                               _scene->draw(vp, eye, true, true);
                               _life.draw(*_renderer, _scene->models(), _scene->light(), vp, eye);
                               for (const Racer &r : _racers)
                                   if (!r.player)
                                       r.car->draw(_scene->light());
                               drawCheckpoints();
                           });
        drawMap(dt);
        drawRaceHud();
        _cd.update(dt);
        _cd.draw(_overlay, _pictures);
        if (_view.controlsList)
            drawControlsList(_overlay, currentOptions(),
                             [&](Control c) { return _strings.get(controlStringId(c), controlFallbackName(c)); });
        if (_replaying)
            drawReplayBar();
        if (_paused)
            drawPauseMenu();
        if (_results)
            drawResults();
        if (_game && _game->isOpen())
            _game->draw(_overlay, nowMs); // the Options, or the replay's save dialogs
    }
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::age
