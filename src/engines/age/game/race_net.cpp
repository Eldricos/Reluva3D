// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The race screen's multiplayer games: a session's Cruise, Blitz,
// Checkpoint, Circuit or Cops & Robbers. Each player drives their own car
// on their own machine and sends where it is; the others' cars are drawn
// where they were a tenth of a second ago, between the states that came.
// The host keeps the clock, says when it's over, and runs the gold. See
// docs/engines/age/games/mm2/systems/multiplayer.md.

#include "engines/age/ui/screen.h"

#include "audio/decode.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640;
constexpr int kFrameH = 480;
constexpr float kCountdown = 3;
constexpr float kNetDelay = 0.1f; // seconds behind: the others' cars are drawn between states received
constexpr float kRaceOn = 60;     // seconds the others have to finish after the first
constexpr float kMessageFor = 3;  // seconds a game message stays up
constexpr float kLinesFor = 10;   // and the session's lines
constexpr float kPi = 3.14159265358979f;
constexpr float kBotSkill = 0.9f; // computer players' share of their cars' top speed
constexpr float kPutBackFor = 3;  // seconds a car put back can't take the gold
constexpr float kHeardTo = 120;   // metres another car's engine can be heard to
constexpr float kGateRadius = 15; // a checkpoint without a radius (circuits')
constexpr float kGateSlack = 4;   // metres wider the host counts another's checkpoint

std::string clockText(float seconds) {
    if (seconds < 0)
        seconds = 0;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(seconds, 60.0f));
    return buf;
}

float yawTowards(const Vec3 &from, const Vec3 &to) {
    const Vec3 d = to - from;
    return std::fabs(d[0]) + std::fabs(d[2]) > 1e-3f ? std::atan2(-d[0], -d[2]) : 0;
}

} // namespace

void RaceScreen::netUpdate(uint64_t nowMs) {
    switch (_net->update(nowMs)) {
    case NetMenus::Action::kLoad:
        startNetGame();
        break;
    case NetMenus::Action::kLobby:
        // The game's over (or the session is): back to the menus.
        if (_netGame)
            endNetGame();
        _garageForNet = false;
        _netMenus = true;
        _net->showLobby();
        break;
    default:
        break;
    }
}

Vec3 RaceScreen::gridSlot(const Vec3 &centre, float yaw, size_t slot) const {
    // Two abreast, 8 m between rows, set down on the ground.
    const Vec3 local{slot % 2 ? 2.5f : -2.5f, 0, float(slot / 2) * 8};
    Vec3 at = centre + Mat3::rotationY(yaw) * local;
    CollisionWorld::RayHit hit;
    if (_world.raycast(at + Vec3{0, 6, 0}, {0, -1, 0}, 20, hit))
        at[1] = hit.point[1];
    return at + Vec3{0, 0.5f, 0};
}

void RaceScreen::startNetGame() {
    NetSession *session = _net ? _net->session() : nullptr;
    if (!session || !session->me())
        return;
    const NetSettings set = session->settings();
    _netMenus = false;
    _garageForNet = false;
    _netGame = true;
    _chatting = false;
    _ctx.sys.setTextInput(false);
    _life.stop();
    _city = std::clamp(int(set.city), 0, int(_cities.size()) - 1);
    loadCity();
    const std::string &map = _cities[size_t(_city)].map;
    _error.clear();

    // The race, if it's one; Cruise and Cops & Robbers drive the city.
    _race = RaceDefinition{};
    const bool racing = NetMenus::hasRace(set.mode);
    if (racing) {
        const RaceKind kind = NetMenus::raceKind(set.mode);
        _kind = kind == RaceKind::kBlitz ? 0 : kind == RaceKind::kCircuit ? 1 : 2; // the race screen's order
        _raceIndex = set.race;
        if (Error err = loadRace(_ctx.files, map, kind, _raceIndex, _race))
            _error = err.message;
    }
    _race.name = racing && !_race.name.empty() ? _race.name : _net->modeName(set.mode);
    _scene->setLighting(int(set.timeOfDay & 3) * 4 + int(set.weather & 3));
    _targets.clear();
    const int laps = _race.kind == RaceKind::kCircuit ? std::max(1, int(set.laps)) : 1;
    if (racing && !_race.waypoints.empty())
        for (int lap = 0; lap < laps; ++lap) {
            for (size_t i = 1; i < _race.waypoints.size(); ++i)
                _targets.push_back(i);
            if (_race.kind == RaceKind::kCircuit)
                _targets.push_back(0);
        }
    // A circuit's laps are the host's.
    for (RaceSettings &r : _race.settings)
        r.laps = laps;

    // Where each starts: the race's grid; in a cruise spread over the
    // city's Cops & Robbers places; in Cops & Robbers by their side's base.
    std::vector<Waypoint> points;
    loadGoldPoints(_ctx.files, map, points);
    _places = points;
    if (set.mode == NetMode::kCopsRobbers)
        _gold.start(set, points, session->seed());
    _unloadable.clear();
    _progress.clear();
    std::vector<NetPlayer> players = session->players();
    std::sort(players.begin(), players.end(), [](const NetPlayer &a, const NetPlayer &b) { return a.id < b.id; });
    std::map<uint8_t, int> bySide; // how many already at each base
    auto startOf = [&](const NetPlayer &p, size_t slot, float &yaw) {
        if (racing && !_race.waypoints.empty()) {
            yaw = headingToYaw(_race.waypoints[0].heading);
            return gridSlot(_race.waypoints[0].position, yaw, slot);
        }
        if (set.mode == NetMode::kCopsRobbers) {
            // Lined up along the road, the way the base's place faces.
            GoldGame::Car c{p.id, p.team, {}, {}};
            const uint8_t side = _gold.teams() ? _gold.sideOf(c) : 0;
            const Vec3 base = _gold.base(side);
            yaw = yawTowards(base, _gold.home());
            for (const Waypoint &w : points)
                if (length(w.position - base) < 0.5f)
                    yaw = headingToYaw(w.heading);
            return gridSlot(base, yaw, size_t(bySide[side]++));
        }
        if (!points.empty()) {
            const Waypoint &w = points[(session->seed() + slot * 5) % points.size()];
            yaw = headingToYaw(w.heading);
            return gridSlot(w.position, yaw, 0);
        }
        yaw = 0;
        return gridSlot(_scene->start, 0, slot);
    };

    _racers.clear();
    std::vector<Racer> others;
    for (size_t slot = 0; slot < players.size(); ++slot) {
        const NetPlayer &p = players[slot];
        const bool me = p.id == session->myId();
        const bool bot = p.bot && session->isHost(); // ours to drive
        Racer r;
        r.car = std::make_unique<CarActor>();
        r.player = me;
        r.peer = me || bot ? kNoPlayer : p.id;
        r.botId = bot ? p.id : kNoPlayer;
        r.name = p.name + (me ? " (you)" : "");
        const std::string car = _net->carFor(p);
        if (Error err = r.car->load(*_renderer, _scene->textures(), _ctx.files, car, p.paint)) {
            if (me) {
                _error = err.message;
                return;
            }
            continue;
        }
        float yaw = 0;
        const Vec3 at = startOf(p, slot, yaw);
        r.car->sim.reset(r.car->setup, at, yaw);
        if (bot && racing) {
            // Through the race's checkpoints, along the roads.
            std::vector<Vec3> through;
            for (size_t k : _targets)
                through.push_back(_race.waypoints[k].position);
            size_t loopFrom = 0;
            r.driver.start(routePath(_roads, at, through, false, loopFrom, &_botRoads), 0, kBotSkill);
        } else if (bot)
            r.bot.start(session->seed() ^ (uint32_t(p.id) * 2654435761u));
        if (me) {
            if (!racing) // where to put the car back if it falls out of the city
                _race.waypoints = {Waypoint{at, -yaw * 180 / kPi, 0}};
            _car = 0;
            for (size_t k = 0; k < _cars.size(); ++k)
                if (_cars[k].base == car)
                    _car = int(k);
            _paint = p.paint;
            _racers.insert(_racers.begin(), std::move(r));
        } else
            others.push_back(std::move(r));
    }
    for (Racer &r : others)
        _racers.push_back(std::move(r));
    if (_racers.empty() || !_racers.front().player)
        return;
    const std::string &base = _cars[size_t(_car)].base;
    _wear.load(_ctx.files, base, *_racers.front().car);
    if (_smoke)
        _smoke->clear();

    // No traffic or police in a multiplayer game: the pedestrians the host
    // asked for.
    CityLife::Options life;
    life.traffic = 0;
    life.cops = 0;
    life.peds = set.peds * pedestrianShare();
    life.rain = set.weather == 3;
    life.seed = session->seed();
    life.variant = racing ? raceStem(_race.kind, _raceIndex) : std::string();
    if (_life.loaded()) {
        if (Error err = _life.start(*_renderer, _scene->textures(), _ctx.files, _life.citySettings(), life, _ctx.mixer))
            _error = err.message;
        auto inGap = [&](const Vec3 &a, const Vec3 &b, const Vec3 &c) { return _life.gizmos().inGap(a, b, c); };
        _scene->hide(inGap);
        _world.removeIf(inGap);
    }
    _fill = true;

    _engine.stop();
    _sounds.stop();
    if (_ctx.mixer) {
        _engine.start(*_ctx.mixer, _ctx.files, base);
        _sounds.start(*_ctx.mixer, _ctx.files, base, set.weather == 3);
        if (Error err = _music.start(_ctx.files, *_ctx.mixer, racing, session->seed()))
            _error = "music: " + err.message;
    }
    // The announcer's Cops & Robbers lines (aud/spchdata/CNR<city>.csv).
    _speech.clear();
    if (set.mode == NetMode::kCopsRobbers) {
        std::string upper = map;
        for (char &ch : upper)
            ch = char(std::toupper(static_cast<unsigned char>(ch)));
        std::vector<uint8_t> data;
        if (!_ctx.files.read("aud/spchdata/CNR" + upper + ".csv", data))
            _speech = parseSpeechTable(std::string(data.begin(), data.end()));
    }
    _cameras = loadCarCameras(_ctx.files, base);
    _camera = std::min(_camera, int(_cameras.size()) - 1);
    _dash.load(_ctx.files, _scene->models(), _scene->textures(), base);
    _cameraReset = true;
    _clock = -kCountdown - 1;
    _outcome.clear();
    _beatenAll.clear();
    _finalLapFor = 0;
    _lastLap = 0;
    _goldSeen = 0;
    _minutesSaid = 0;
    _netMessage.clear();
    _netMessageFor = 0;
    _keys = 0;
    _state = State::kCountdown;
    session->setLoaded();
    _lastMs = 0; // the load took a while: no step for it
    setTitle(_race.name + " (" + _net->modeName(set.mode) + ", " + _cities[size_t(_city)].name + ")");
}

void RaceScreen::endNetGame() {
    _netGame = false;
    _chatting = false;
    _ctx.sys.setTextInput(false);
    _life.stop();
    _engine.stop();
    _music.stop();
    _racers.clear();
    _state = State::kMenu;
    _scene->setLighting(4);
}

void RaceScreen::driveRemote(Racer &r, float dt) {
    // Where its driver's car was a moment ago.
    NetSession *session = _net ? _net->session() : nullptr;
    if (!session)
        return;
    const auto &cars = session->cars();
    const auto it = cars.find(r.peer);
    NetCarState s;
    if (it == cars.end() || !carStateAt(it->second, _clock - kNetDelay, s))
        return;
    CarSim &sim = r.car->sim;
    sim.position = s.position;
    sim.orientation = matrixOfQuaternion(s.rotation);
    sim.velocity = s.velocity;
    sim.angularVelocity = {0, 0, 0};
    const float along = sim.forwardSpeed();
    for (size_t k = 0; k < 4; ++k) {
        CarSim::Wheel &w = sim.wheels[k];
        w.compression = s.compression[k];
        w.steer = k < 2 ? s.steer : 0;
        w.spin += along / std::max(0.1f, r.car->setup.wheelRadius[k]) * dt;
        w.contact = true;
    }
    r.car->lamps = {(s.lamps & kLampHead) != 0, (s.lamps & kLampBrake) != 0, (s.lamps & kLampReverse) != 0};
    r.car->siren = s.lamps & kLampSiren ? 1 + int(_clock * 4) % 2 : 0;
    if (s.resets != r.seenResets) { // put back: no checkpoints on the way there
        r.seenResets = s.resets;
        r.putBackAt = _clock;
        r.seen = false;
    }
    remoteLook(r, s, dt);
    // How far it's got: the host counts it from where the car went; the
    // others take the host's word (else its own machine's, until the host
    // has spoken).
    if (session->isHost()) {
        checkProgress(r, s);
    } else if (const auto p = _progress.find(r.peer); p != _progress.end()) {
        r.next = std::min(p->second.first, _targets.size());
        r.finishTime = p->second.second;
    } else {
        r.next = std::min<size_t>(s.next, _targets.size());
        r.finishTime = s.finishTime;
    }
}

void RaceScreen::checkProgress(Racer &r, const NetCarState &s) {
    // Through its next checkpoint if the way it went since it was last
    // seen passes within the checkpoint's reach (a little wider than its
    // own machine's count).
    // In a race in any order, through any of those it has left (the
    // finish last).
    if (r.seen && r.next < _targets.size()) {
        const std::vector<size_t> gates = anyOrder() ? remaining(r) : std::vector<size_t>{r.next};
        for (size_t k : gates) {
            const Waypoint &w = targetPoint(k);
            const Vec3 a{r.seenAt[0], 0, r.seenAt[2]};
            const Vec3 b{s.position[0], 0, s.position[2]};
            const Vec3 c{w.position[0], 0, w.position[2]};
            const Vec3 ab = b - a;
            const float t = dot(ab, ab) > 1e-6f ? std::clamp(dot(c - a, ab) / dot(ab, ab), 0.0f, 1.0f) : 0;
            const Vec3 nearest = a + ab * t;
            const float height = r.seenAt[1] + (s.position[1] - r.seenAt[1]) * t;
            if (length(c - nearest) < (w.radius > 0 ? w.radius : kGateRadius) + kGateSlack &&
                std::fabs(height - w.position[1]) < 10) {
                if (anyOrder())
                    clearCheckpoint(r, k);
                if (++r.next == _targets.size())
                    r.finishTime = s.clock;
                break;
            }
        }
    }
    r.seenAt = s.position;
    r.seen = true;
}

void RaceScreen::remoteLook(Racer &r, const NetCarState &s, float dt) {
    // Its engine, quieter with distance and to the side it's on; its
    // damage: the dents its machine made, its smoke.
    NetSession *session = _net ? _net->session() : nullptr;
    const NetPlayer *p = session ? session->player(r.peer) : nullptr;
    if (!p || _racers.empty())
        return;
    const std::string base = _net->carFor(*p);
    if (_ctx.mixer && !r.engine) {
        r.engine = std::make_unique<EngineSound>();
        if (r.engine->start(*_ctx.mixer, _ctx.files, base))
            r.engine.reset();
    }
    if (r.engine) {
        const CarSim &me = _racers.front().car->sim;
        const Vec3 d = r.car->sim.position - me.position;
        const float far = length(d);
        const float near = std::clamp(1 - far / kHeardTo, 0.0f, 1.0f);
        const float pan = far > 1 ? dot(d * (1 / far), me.orientation.x) : 0;
        r.engine->update(s.rpm, s.throttle, near * near * 0.8f, pan);
    }
    if (!r.wear) {
        r.wear = std::make_unique<CarWear>();
        r.wear->load(_ctx.files, base, *r.car);
    }
    if (s.damage + 0.02f < r.damageShown) { // mended
        r.car->mend();
        r.dentsKnown = false;
    }
    if (!r.dentsKnown) { // the dents there are, as far as we're told
        r.dentsSeen = uint8_t(s.dents - std::min<uint8_t>(s.dents, 4));
        r.dentsKnown = true;
    }
    const int fresh = std::min<int>(uint8_t(s.dents - r.dentsSeen), 4);
    for (int i = fresh - 1; i >= 0; --i)
        r.car->dent(s.dentAt[size_t(i)], r.wear->setup.textureRadius);
    r.dentsSeen = s.dents;
    r.damageShown = s.damage;
    r.wear->damage.set(s.damage * r.wear->setup.maxDamage);
    r.wear->puff(dt, *r.car, _smoke.get());
}

void RaceScreen::syncNetRacers() {
    // Players who've left (or a host that's gone, and its computer
    // players) take their cars with them; players who've joined since the
    // start bring theirs.
    NetSession *session = _net ? _net->session() : nullptr;
    if (!session || _racers.empty())
        return;
    _racers.erase(std::remove_if(_racers.begin() + 1, _racers.end(),
                                 [&](const Racer &r) { return !session->player(netIdOf(r)); }),
                  _racers.end());
    for (const NetPlayer &p : session->players()) {
        if (p.id == session->myId() || _unloadable.count(p.id))
            continue;
        bool have = false;
        for (const Racer &r : _racers)
            have = have || netIdOf(r) == p.id;
        if (have)
            continue;
        Racer r;
        r.car = std::make_unique<CarActor>();
        r.peer = p.id;
        r.name = p.name;
        if (r.car->load(*_renderer, _scene->textures(), _ctx.files, _net->carFor(p), p.paint)) {
            _unloadable.insert(p.id);
            continue;
        }
        const Waypoint &at = _race.waypoints.empty() ? Waypoint{_scene->start, 0, 0} : _race.waypoints[0];
        r.car->sim.reset(r.car->setup, at.position + Vec3{0, 0.5f, 0}, headingToYaw(at.heading));
        _racers.push_back(std::move(r));
    }
}

uint8_t RaceScreen::netIdOf(const Racer &r) const {
    const NetSession *session = _net ? _net->session() : nullptr;
    return r.player ? (session ? session->myId() : kNoPlayer) : r.peer != kNoPlayer ? r.peer : r.botId;
}

std::vector<GoldGame::Car> RaceScreen::netCars() const {
    std::vector<GoldGame::Car> cars;
    const NetSession *session = _net ? _net->session() : nullptr;
    if (!session)
        return cars;
    for (const Racer &r : _racers)
        if (const NetPlayer *p = session->player(netIdOf(r)))
            cars.push_back(
                {p->id, p->team, r.car->sim.position, r.car->sim.velocity, _clock - r.putBackAt < kPutBackFor});
    return cars;
}

NetCarState RaceScreen::carStateOf(const Racer &r) const {
    const CarSim &sim = r.car->sim;
    NetCarState s;
    s.clock = _clock;
    s.position = sim.position;
    s.velocity = sim.velocity;
    s.rotation = quaternionOf(sim.orientation);
    s.steer = sim.wheels[0].steer;
    for (size_t k = 0; k < 4; ++k)
        s.compression[k] = sim.wheels[k].compression;
    s.lamps = uint8_t((r.car->lamps.head ? kLampHead : 0) | (r.car->lamps.brake ? kLampBrake : 0) |
                      (r.car->lamps.reverse ? kLampReverse : 0) | (r.car->siren ? kLampSiren : 0));
    s.next = uint16_t(std::min<size_t>(r.next, 65535));
    s.finishTime = r.finishTime;
    s.rpm = sim.rpm;
    s.throttle = r.throttle;
    s.resets = r.resets;
    if (r.player) {
        s.damage = _wear.fraction();
        s.dents = _wear.dents;
        s.dentAt = _wear.recentDents;
    }
    return s;
}

void RaceScreen::planBot(Racer &r, float dt, const std::vector<GoldGame::Car> &cars) {
    // A computer player outside a race: a new path when its mind wants one;
    // a cop's siren while it chases.
    const NetSession *session = _net ? _net->session() : nullptr;
    const NetPlayer *p = session ? session->player(r.botId) : nullptr;
    if (!p)
        return;
    const bool robbing = session->settings().mode == NetMode::kCopsRobbers;
    CarSim &sim = r.car->sim;
    std::vector<Vec3> path;
    if (r.bot.plan(dt, sim.position, r.botId, p->team, robbing ? &_gold : nullptr, cars, _places, _botRoads,
                   r.driver.finished() || r.driver.path().empty(), path))
        r.driver.follow(std::move(path), kBotSkill);
    // Wedged, or out of the city: back on the nearest road, facing its goal
    // (never nearer the goal than the road is).
    if (r.bot.stuck() || sim.position[1] < _scene->psdl().min[1] - 50) {
        Vec3 node = sim.position;
        _botRoads.nearestNode(sim.position, node, 1000);
        bool chasing = false;
        const Vec3 goal = robbing ? GoldBot::goal(r.botId, p->team, _gold, cars, chasing) : node;
        sim.reset(r.car->setup, node + Vec3{0, 0.5f, 0}, yawTowards(node, goal));
        r.bot.unstuck();
        ++r.resets;
        r.putBackAt = _clock;
    }
    if (r.car->hasSiren()) {
        bool chasing = false;
        if (robbing)
            GoldBot::goal(r.botId, p->team, _gold, cars, chasing);
        r.car->siren = chasing ? 1 + int(_clock * 4) % 2 : 0;
    }
}

void RaceScreen::netSay(const std::string &text) {
    if (text.empty())
        return;
    _netMessage = text;
    _netMessageFor = kMessageFor;
}

void RaceScreen::announce(const GoldEvent &e) {
    // The announcer's line for it: the robber teams by colour, cops and
    // robbers by side (a free-for-all's are the robbers').
    if (_speech.empty() || !_ctx.mixer)
        return;
    std::string name;
    const bool teams = _gold.mode() == GoldMode::kRobberTeams;
    const bool cop = _gold.mode() == GoldMode::kCopsAndRobbers && e.side == 1;
    switch (e.kind) {
    case GoldEvent::Kind::kGot:
        name = teams ? (e.side ? "BLUETEAMHASGOLD" : "REDTEAMHASGOLD") : cop ? "COPGETLOOT" : "ROBGETLOOT";
        break;
    case GoldEvent::Kind::kRecovered:
        name = teams ? (e.side ? "BLUETEAMHASGOLD" : "REDTEAMHASGOLD") : cop ? "COPRECOVERLOOT" : "ROBRECOVERLOOT";
        break;
    case GoldEvent::Kind::kDropped:
        name = teams ? (e.side ? "BLUETEAMDROPPEDGOLD" : "REDTEAMDROPPEDGOLD") : cop ? "COPDROPLOOT" : "ROBDROPLOOT";
        break;
    case GoldEvent::Kind::kDelivered:
        name = teams ? (e.side ? "BLUETEAMSTASHEDGOLD" : "REDTEAMSTASHEDGOLD") : cop ? "COPSTASHLOOT" : "ROBSTASHLOOT";
        break;
    default:
        return;
    }
    const auto it = _speech.find(name);
    if (it == _speech.end() || it->second.empty())
        return;
    const std::string &clip = it->second[(e.seq * 7) % it->second.size()];
    std::vector<uint8_t> data;
    if (_ctx.files.read("aud/aud22/" + clip + ".22k.wav", data) &&
        _ctx.files.read("aud/aud11/" + clip + ".11k.wav", data))
        return;
    auto sound = std::make_shared<audio::Sound>();
    if (!audio::decodeSound(data, *sound))
        _ctx.mixer->play(sound, audio::SoundType::kSpeech);
}

void RaceScreen::stepNet(float dt) {
    NetSession *session = _net ? _net->session() : nullptr;
    if (!session || _racers.empty())
        return;
    syncNetRacers();
    const NetSettings &set = session->settings();
    Racer &player = _racers.front();
    CarSim &sim = player.car->sim;
    _netMessageFor = std::max(0.0f, _netMessageFor - dt);

    // Our car, and our computer players', to the others.
    if (session->counting()) {
        session->sendCar(carStateOf(player));
        for (const Racer &r : _racers)
            if (r.botId != kNoPlayer)
                session->sendBotCar(r.botId, carStateOf(r));
    }
    // A cop's siren, on the police car (H).
    if (player.car->hasSiren() && (_keys & kDriveHorn) && _state == State::kRacing)
        player.car->siren = 1 + int(_clock * 4) % 2;
    else if (player.car->hasSiren())
        player.car->siren = 0;

    if (set.mode == NetMode::kCopsRobbers) {
        // The host runs the gold; the others hear how it is.
        if (session->isHost()) {
            if (_state == State::kRacing) {
                _gold.step(_clock, dt, netCars(), _scene->psdl().min[1] - 50);
            }
            session->setGameState(_gold.encode(), _gold.over());
        } else if (!session->gameState().empty()) {
            _gold.decode(session->gameState());
        }
        // The gold's weight on the car that has it.
        sim.ballast = _gold.holder() == session->myId() ? float(set.goldMass) : 0;
        for (Racer &r : _racers)
            if (r.botId != kNoPlayer)
                r.car->sim.ballast = _gold.holder() == r.botId ? float(set.goldMass) : 0;
        for (const GoldEvent &e : _gold.eventsAfter(_goldSeen)) {
            _goldSeen = e.seq;
            const NetPlayer *p = session->player(e.player);
            const bool me = e.player == session->myId();
            const std::string who = p ? p->name : "";
            switch (e.kind) {
            case GoldEvent::Kind::kGot:
            case GoldEvent::Kind::kRecovered:
                netSay(me ? _strings.get(115, "You have the Gold!") : who + " " + _strings.get(133, "has the Gold!"));
                break;
            case GoldEvent::Kind::kDropped:
                netSay(me ? _strings.get(112, "You dropped the gold!")
                          : who + " " + _strings.get(136, "dropped the Gold!"));
                break;
            case GoldEvent::Kind::kDelivered:
                netSay(me ? _strings.get(116, "Gold delivered!")
                          : who + " " + _strings.get(137, "delivered the Gold!"));
                break;
            case GoldEvent::Kind::kReturned:
                netSay("The gold is back where it started");
                break;
            case GoldEvent::Kind::kTimeUp:
                netSay(_strings.get(118, "Time's up!"));
                break;
            case GoldEvent::Kind::kPointLimit:
                netSay(_strings.get(119, "Point limit reached"));
                break;
            }
            announce(e);
        }
        // "N minutes remaining", as the game counts down a timed game.
        if (set.limit == GoldLimit::kTime && _state == State::kRacing) {
            static const int kMinutes[] = {20, 15, 10, 5, 1};
            const float left = float(set.limitValue) * 60 - _clock;
            for (int k = 0; k < 5; ++k)
                if (left <= float(kMinutes[k]) * 60 && left > float(kMinutes[k]) * 60 - 2 &&
                    _minutesSaid != kMinutes[k] && float(set.limitValue) > float(kMinutes[k])) {
                    _minutesSaid = kMinutes[k];
                    netSay(_strings.get(uint32_t(138 + k), std::to_string(kMinutes[k]) + " minutes remaining"));
                }
        }
        if (_gold.over() && _state != State::kFinished) {
            // The side with the most.
            GoldGame::Car self{session->myId(), session->me() ? session->me()->team : uint8_t(0), {}, {}};
            int best = -1;
            uint8_t winner = 0;
            for (const auto &[side, points] : _gold.scores())
                if (points > best) {
                    best = points;
                    winner = side;
                }
            _outcome = best < 0 ? _strings.get(100, "Race over")
                       : winner == _gold.sideOf(self)
                           ? _strings.get(164, "You Won!")
                           : (_gold.teams() ? _net->teamName(winner)
                                            : (session->player(winner) ? session->player(winner)->name : "")) +
                                 " won";
            _state = State::kFinished;
        }
        return;
    }
    if (set.mode == NetMode::kCruise)
        return;

    // A race: the host says when it's over (everyone in, a minute after
    // the first, or a Blitz's time up).
    if (session->isHost() && _state != State::kCountdown) {
        bool all = true;
        float first = -1;
        for (const Racer &r : _racers) {
            all = all && r.finishTime >= 0;
            if (r.finishTime >= 0)
                first = first < 0 ? r.finishTime : std::min(first, r.finishTime);
        }
        const float limit = _race.settings[0].timeLimit;
        const bool over = all || (first >= 0 && _clock > first + kRaceOn) ||
                          (_race.kind == RaceKind::kBlitz && limit > 0 && _clock > limit);
        // With everyone's progress as the host counts it.
        std::vector<uint8_t> state;
        for (const Racer &r : _racers) {
            const uint8_t id = netIdOf(r);
            const uint16_t next = uint16_t(std::min<size_t>(r.next, 65535));
            uint32_t finish;
            std::memcpy(&finish, &r.finishTime, 4);
            state.insert(state.end(), {id, uint8_t(next), uint8_t(next >> 8), uint8_t(finish), uint8_t(finish >> 8),
                                       uint8_t(finish >> 16), uint8_t(finish >> 24)});
        }
        session->setGameState(state, over);
    } else if (!session->isHost()) {
        const std::vector<uint8_t> &state = session->gameState();
        for (size_t at = 0; at + 7 <= state.size(); at += 7) {
            const uint32_t finish = uint32_t(state[at + 3]) | uint32_t(state[at + 4]) << 8 |
                                    uint32_t(state[at + 5]) << 16 | uint32_t(state[at + 6]) << 24;
            float f;
            std::memcpy(&f, &finish, 4);
            _progress[state[at]] = {size_t(state[at + 1] | state[at + 2] << 8), std::isfinite(f) ? f : -1};
        }
    }
    if (session->over() && _state != State::kFinished) {
        if (player.finishTime >= 0) {
            int place = 1;
            for (const Racer &r : _racers)
                if (!r.player && r.finishTime >= 0 && r.finishTime < player.finishTime)
                    ++place;
            static const char *const kPlaces[] = {"1st!", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
            _outcome =
                raceMessage(3 + std::min(place, 8), std::string("You finished ") + kPlaces[std::min(place, 8) - 1]) +
                "  " + clockText(player.finishTime);
            if (_race.kind == RaceKind::kBlitz && place == 1)
                _outcome = raceMessage(6, "You Won!") + "  " + clockText(player.finishTime);
        } else
            _outcome = _strings.get(100, "Race over");
        _state = State::kFinished;
    }
}

bool RaceScreen::netEvent(const Event &ev) {
    // The multiplayer menus take everything while they're up.
    if (_state == State::kMenu && _netMenus && _net) {
        switch (_net->handle(ev)) {
        case NetMenus::Action::kBack:
            _netMenus = false;
            break;
        case NetMenus::Action::kGarage:
            _netMenus = false;
            _garageForNet = true;
            _front->showGarage();
            break;
        default:
            break;
        }
        return true;
    }
    if (!_netGame)
        return false;
    // Typing a line to the others (T, then Enter).
    if (_chatting) {
        if (ev.type == EventType::kTextInput) {
            for (const char *c = ev.text; *c; ++c)
                if (static_cast<unsigned char>(*c) >= 32 && _chatLine.size() < 100)
                    _chatLine.push_back(*c);
        } else if (ev.type == EventType::kKeyDown) {
            if (ev.key == key::kReturn || ev.key == key::kKpEnter) {
                if (_net && _net->session() && !_chatLine.empty())
                    _net->session()->say(_chatLine);
                _chatting = false;
            } else if (ev.key == key::kEscape) {
                _chatting = false;
            } else if (ev.key == key::kBackspace && !_chatLine.empty()) {
                _chatLine.pop_back();
            }
            if (!_chatting)
                _ctx.sys.setTextInput(false);
        }
        return true;
    }
    if (ev.type != EventType::kKeyDown)
        return false;
    NetSession *session = _net ? _net->session() : nullptr;
    if (controlOf(ev.key) == Control::kChat) {
        _chatting = true;
        _chatLine.clear();
        _keys = 0;
        _ctx.sys.setTextInput(true);
        return true;
    }
    const bool enter = ev.key == key::kReturn || ev.key == key::kKpEnter;
    if (ev.key == key::kEscape || (_state == State::kFinished && enter)) {
        // The host takes everyone back to the lobby; a player leaves (Esc)
        // or waits for the host (Enter).
        if (session && session->isHost())
            session->backToLobby();
        else if (ev.key == key::kEscape) {
            _net->leave();
            endNetGame();
            _netMenus = true;
            _net->showLobby();
        }
        return true;
    }
    return false;
}

void RaceScreen::drawGold() {
    NetSession *session = _net ? _net->session() : nullptr;
    if (!session || session->settings().mode != NetMode::kCopsRobbers)
        return;
    // The gold (geometry/wpobj_gold) turning where it lies, or above the
    // car that has it; the checkpoint stands either side of each base.
    const ModelCache::Model &gold = _scene->models().get("wpobj_gold");
    const float turn = _clock * 2;
    Vec3 at = _gold.goldPosition() + Vec3{0, 0.6f, 0};
    for (const Racer &r : _racers)
        if ((r.player ? session->myId() : r.peer) == _gold.holder())
            at = r.car->sim.position + Vec3{0, 2.2f, 0};
    if (gold.ok)
        drawModelMesh(*_renderer, gold.levels.front(), matrixOf(Mat3::rotationY(turn), at), _scene->light(), false);
    const ModelCache::Model &stand = _scene->models().get("pt_check");
    if (!stand.ok)
        return;
    for (uint8_t side = 0; side < (_gold.teams() ? 2 : 1); ++side) {
        const Vec3 base = _gold.base(side);
        const Mat3 face = Mat3::rotationY(yawTowards(base, _gold.home()));
        for (float s : {-1.0f, 1.0f})
            drawModelMesh(*_renderer, stand.levels.front(), matrixOf(face, base + face * Vec3{s * 8, 0.5f, 0}),
                          _scene->light(), false);
    }
}

void RaceScreen::drawNetLines() {
    NetSession *session = _net ? _net->session() : nullptr;
    if (!session)
        return;
    // Waiting for the others to load, then the countdown.
    if (!session->counting()) {
        int loading = 0;
        for (const NetPlayer &p : session->players())
            loading += p.loaded ? 0 : 1;
        drawCentred(loading > 0 ? _strings.get(uint32_t(30 + std::min(loading, 7)), "Waiting for players")
                                : std::string("Waiting for the host"),
                    kFrameH / 2 - 80, {255, 255, 255}, 2);
    }
    // What's been said, and who's come and gone.
    int y = 300;
    const auto &lines = _net->lines();
    for (size_t i = lines.size() > 4 ? lines.size() - 4 : 0; i < lines.size(); ++i)
        if (_nowMs < lines[i].atMs + uint64_t(kLinesFor * 1000)) {
            font::drawText(_overlay, 4, y, lines[i].text,
                           lines[i].system ? Color{255, 200, 120} : Color{230, 230, 230});
            y += font::kLineHeight + 2;
        }
    if (_chatting)
        font::drawText(_overlay, 4, y + 4, _strings.get(309, "Enter Chat Msg") + ": " + _chatLine + "_",
                       {255, 255, 120});
    else if (_netMessageFor > 0)
        drawCentred(_netMessage, 110, {255, 230, 120}, 2);
}

void RaceScreen::drawNetHud() {
    NetSession *session = _net ? _net->session() : nullptr;
    if (!session || _racers.empty())
        return;
    const NetSettings &set = session->settings();
    const Racer &player = _racers.front();
    char buf[120];
    font::drawText(_overlay, 4, 4, _net->modeName(set.mode) + "  (" + _cities[size_t(_city)].name + ")",
                   {150, 200, 255});
    const bool timed = set.mode == NetMode::kCopsRobbers && set.limit == GoldLimit::kTime;
    const float shown = timed ? float(set.limitValue) * 60 - std::max(0.0f, _clock) : std::max(0.0f, _clock);
    if (_pictures.loaded())
        _pictures.clock(_overlay, shown, kFrameW / 2, 0);
    else
        font::drawText(_overlay, 4, 18, (timed ? "LEFT " : "TIME ") + clockText(shown), {255, 255, 255}, 2);
    if (set.mode == NetMode::kCopsRobbers) {
        // The score: each team's, or each player's (the best first).
        int y = 40;
        GoldGame::Car self{session->myId(), session->me() ? session->me()->team : uint8_t(0), {}, {}};
        if (_gold.teams()) {
            for (uint8_t side = 0; side < 2; ++side) {
                std::snprintf(buf, sizeof buf, "%-10s %d", _net->teamName(side).c_str(), _gold.score(side));
                font::drawText(_overlay, 4, y, buf,
                               side == _gold.sideOf(self) ? Color{255, 255, 120} : Color{230, 230, 230}, 2);
                y += 20;
            }
        } else {
            std::vector<std::pair<int, std::string>> rows;
            for (const NetPlayer &p : session->players())
                rows.push_back({_gold.score(p.id), p.name});
            std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            for (size_t i = 0; i < rows.size() && i < 4; ++i) {
                std::snprintf(buf, sizeof buf, "%-12.12s %d", rows[i].second.c_str(), rows[i].first);
                font::drawText(_overlay, 4, y, buf, {230, 230, 230}, 2);
                y += 20;
            }
        }
        if (_gold.holder() == session->myId() && _state != State::kFinished)
            drawCentred(_strings.get(115, "You have the Gold!"), 80, {255, 220, 60}, 2);
    }
    // The countdown.
    if (_clock < -kCountdown / 2 && session->counting())
        drawCentred(raceMessage(0, "Ready..."), kFrameH / 2 - 40, {255, 255, 255}, 5);
    else if (_clock < 0 && session->counting())
        drawCentred(raceMessage(1, "Set..."), kFrameH / 2 - 40, {255, 230, 120}, 5);
    else if (_clock >= 0 && _clock < 1)
        drawCentred(raceMessage(2, "Go!"), kFrameH / 2 - 40, {120, 255, 120}, 6);
    if (_state == State::kFinished) {
        font::drawText(_overlay, 60, 150, _outcome, {255, 255, 120}, 3);
        const bool hosting = session->isHost();
        font::drawText(_overlay, 60, 190,
                       hosting ? "Enter: everyone back to the lobby" : "Waiting for the host (Esc: leave the session)",
                       {150, 200, 255});
    }
    drawNetLines();
    if (_hudShown)
        drawSpeedometer(_overlay, player.car->sim,
                        _hudMap.loaded() ? int(_hudMap.setup().left * float(kFrameW)) - 8 : kFrameW - 8,
                        _wear.fraction(), _pictures);
}

} // namespace reluva::age
