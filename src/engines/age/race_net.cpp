// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Race page's multiplayer games: a session's Cruise, Blitz,
// Checkpoint, Circuit or Cops & Robbers. Each player drives their own car
// on their own machine and sends where it is; the others' cars are drawn
// where they were a tenth of a second ago, between the states that came.
// The host keeps the clock, says when it's over, and runs the gold. See
// docs/engines/age-multiplayer.md.

#include "engines/age/viewers.h"

#include "common/audio/decode.h"
#include "common/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kCountdown = 3;
constexpr float kNetDelay = 0.1f; // seconds behind: the others' cars are drawn between states received
constexpr float kRaceOn = 60;     // seconds the others have to finish after the first
constexpr float kMessageFor = 3;  // seconds a game message stays up
constexpr float kLinesFor = 10;   // and the session's lines
constexpr float kPi = 3.14159265358979f;

std::string clockText(float seconds) {
    if (seconds < 0)
        seconds = 0;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(seconds, 60.0f));
    return buf;
}

float yawTowards(const Vec3& from, const Vec3& to) {
    const Vec3 d = to - from;
    return std::fabs(d[0]) + std::fabs(d[2]) > 1e-3f ? std::atan2(-d[0], -d[2]) : 0;
}

} // namespace

void RaceViewer::netUpdate(uint64_t nowMs) {
    switch (net_->update(nowMs)) {
    case NetMenus::Action::Load:
        startNetGame();
        break;
    case NetMenus::Action::Lobby:
        // The game's over (or the session is): back to the menus.
        if (netGame_)
            endNetGame();
        garageForNet_ = false;
        netMenus_ = true;
        net_->showLobby();
        break;
    default:
        break;
    }
}

Vec3 RaceViewer::gridSlot(const Vec3& centre, float yaw, size_t slot) const {
    // Two abreast, 8 m between rows, set down on the ground.
    const Vec3 local{slot % 2 ? 2.5f : -2.5f, 0, float(slot / 2) * 8};
    Vec3 at = centre + Mat3::rotationY(yaw) * local;
    CollisionWorld::RayHit hit;
    if (world_.raycast(at + Vec3{0, 6, 0}, {0, -1, 0}, 20, hit))
        at[1] = hit.point[1];
    return at + Vec3{0, 0.5f, 0};
}

void RaceViewer::startNetGame() {
    NetSession* session = net_ ? net_->session() : nullptr;
    if (!session || !session->me())
        return;
    const NetSettings set = session->settings();
    netMenus_ = false;
    garageForNet_ = false;
    netGame_ = true;
    chatting_ = false;
    ctx_.sys.setTextInput(false);
    life_.stop();
    city_ = std::clamp(int(set.city), 0, int(cities_.size()) - 1);
    loadCity();
    const std::string& map = cities_[size_t(city_)].map;
    error_.clear();

    // The race, if it's one; Cruise and Cops & Robbers drive the city.
    race_ = RaceDefinition{};
    const bool racing = NetMenus::hasRace(set.mode);
    if (racing) {
        const RaceKind kind = NetMenus::raceKind(set.mode);
        kind_ = kind == RaceKind::Blitz ? 0 : kind == RaceKind::Circuit ? 1 : 2; // the Race page's order
        raceIndex_ = set.race;
        if (Error err = loadRace(ctx_.files, map, kind, raceIndex_, race_))
            error_ = err.message;
    }
    race_.name = racing && !race_.name.empty() ? race_.name : net_->modeName(set.mode);
    scene_->setLighting(int(set.timeOfDay & 3) * 4 + int(set.weather & 3));
    targets_.clear();
    const int laps = race_.kind == RaceKind::Circuit ? std::max(1, int(set.laps)) : 1;
    if (racing && !race_.waypoints.empty())
        for (int lap = 0; lap < laps; ++lap) {
            for (size_t i = 1; i < race_.waypoints.size(); ++i)
                targets_.push_back(i);
            if (race_.kind == RaceKind::Circuit)
                targets_.push_back(0);
        }
    // A circuit's laps are the host's.
    for (RaceSettings& r : race_.settings)
        r.laps = laps;

    // Where each starts: the race's grid; in a cruise spread over the
    // city's Cops & Robbers places; in Cops & Robbers by their side's base.
    std::vector<Waypoint> points;
    loadGoldPoints(ctx_.files, map, points);
    if (set.mode == NetMode::CopsRobbers)
        gold_.start(set, points, session->seed());
    std::vector<NetPlayer> players = session->players();
    std::sort(players.begin(), players.end(), [](const NetPlayer& a, const NetPlayer& b) { return a.id < b.id; });
    std::map<uint8_t, int> bySide; // how many already at each base
    auto startOf = [&](const NetPlayer& p, size_t slot, float& yaw) {
        if (racing && !race_.waypoints.empty()) {
            yaw = headingToYaw(race_.waypoints[0].heading);
            return gridSlot(race_.waypoints[0].position, yaw, slot);
        }
        if (set.mode == NetMode::CopsRobbers) {
            // Lined up along the road, the way the base's place faces.
            GoldGame::Car c{p.id, p.team, {}, {}};
            const uint8_t side = gold_.teams() ? gold_.sideOf(c) : 0;
            const Vec3 base = gold_.base(side);
            yaw = yawTowards(base, gold_.home());
            for (const Waypoint& w : points)
                if (length(w.position - base) < 0.5f)
                    yaw = headingToYaw(w.heading);
            return gridSlot(base, yaw, size_t(bySide[side]++));
        }
        if (!points.empty()) {
            const Waypoint& w = points[(session->seed() + slot * 5) % points.size()];
            yaw = headingToYaw(w.heading);
            return gridSlot(w.position, yaw, 0);
        }
        yaw = 0;
        return gridSlot(scene_->start, 0, slot);
    };

    racers_.clear();
    std::vector<Racer> others;
    for (size_t slot = 0; slot < players.size(); ++slot) {
        const NetPlayer& p = players[slot];
        const bool me = p.id == session->myId();
        Racer r;
        r.car = std::make_unique<CarActor>();
        r.player = me;
        r.peer = me ? kNoPlayer : p.id;
        r.name = p.name + (me ? " (you)" : "");
        const std::string car = net_->carFor(p);
        if (Error err = r.car->load(*renderer_, scene_->textures(), ctx_.files, car, p.paint)) {
            if (me) {
                error_ = err.message;
                return;
            }
            continue;
        }
        float yaw = 0;
        const Vec3 at = startOf(p, slot, yaw);
        r.car->sim.reset(r.car->setup, at, yaw);
        if (me) {
            if (!racing) // where to put the car back if it falls out of the city
                race_.waypoints = {Waypoint{at, -yaw * 180 / kPi, 0}};
            car_ = 0;
            for (size_t k = 0; k < cars_.size(); ++k)
                if (cars_[k].base == car)
                    car_ = int(k);
            paint_ = p.paint;
            racers_.insert(racers_.begin(), std::move(r));
        } else
            others.push_back(std::move(r));
    }
    for (Racer& r : others)
        racers_.push_back(std::move(r));
    if (racers_.empty() || !racers_.front().player)
        return;
    const std::string& base = cars_[size_t(car_)].base;
    wear_.load(ctx_.files, base, *racers_.front().car);
    if (smoke_)
        smoke_->clear();

    // No traffic or police in a multiplayer game: the pedestrians the host
    // asked for.
    CityLife::Options life;
    life.traffic = 0;
    life.cops = 0;
    life.peds = set.peds;
    life.rain = set.weather == 3;
    life.seed = session->seed();
    life.variant = racing ? raceStem(race_.kind, raceIndex_) : std::string();
    if (life_.loaded()) {
        if (Error err = life_.start(*renderer_, scene_->textures(), ctx_.files, life_.citySettings(), life, ctx_.mixer))
            error_ = err.message;
        auto inGap = [&](const Vec3& a, const Vec3& b, const Vec3& c) { return life_.gizmos().inGap(a, b, c); };
        scene_->hide(inGap);
        world_.removeIf(inGap);
    }
    fill_ = true;

    engine_.stop();
    sounds_.stop();
    if (ctx_.mixer) {
        engine_.start(*ctx_.mixer, ctx_.files, base);
        sounds_.start(*ctx_.mixer, ctx_.files, base, set.weather == 3);
        if (Error err = music_.start(ctx_.files, *ctx_.mixer, racing, session->seed()))
            error_ = "music: " + err.message;
    }
    // The announcer's Cops & Robbers lines (aud/spchdata/CNR<city>.csv).
    speech_.clear();
    if (set.mode == NetMode::CopsRobbers) {
        std::string upper = map;
        for (char& ch : upper)
            ch = char(std::toupper(static_cast<unsigned char>(ch)));
        std::vector<uint8_t> data;
        if (!ctx_.files.read("aud/spchdata/CNR" + upper + ".csv", data))
            speech_ = parseSpeechTable(std::string(data.begin(), data.end()));
    }
    cameras_ = loadCarCameras(ctx_.files, base);
    camera_ = std::min(camera_, int(cameras_.size()) - 1);
    dash_.load(ctx_.files, scene_->models(), scene_->textures(), base);
    cameraReset_ = true;
    clock_ = -kCountdown - 1;
    outcome_.clear();
    beatenAll_.clear();
    finalLapFor_ = 0;
    lastLap_ = 0;
    goldSeen_ = 0;
    minutesSaid_ = 0;
    netMessage_.clear();
    netMessageFor_ = 0;
    keys_ = 0;
    state_ = State::Countdown;
    session->setLoaded();
    lastMs_ = 0; // the load took a while: no step for it
    setTitle(race_.name + " (" + net_->modeName(set.mode) + ", " + cities_[size_t(city_)].name + ")");
}

void RaceViewer::endNetGame() {
    netGame_ = false;
    chatting_ = false;
    ctx_.sys.setTextInput(false);
    life_.stop();
    engine_.stop();
    music_.stop();
    racers_.clear();
    state_ = State::Menu;
    scene_->setLighting(4);
}

void RaceViewer::driveRemote(Racer& r, float dt) {
    // Where its driver's car was a moment ago.
    NetSession* session = net_ ? net_->session() : nullptr;
    if (!session)
        return;
    const auto& cars = session->cars();
    const auto it = cars.find(r.peer);
    NetCarState s;
    if (it == cars.end() || !carStateAt(it->second, clock_ - kNetDelay, s))
        return;
    CarSim& sim = r.car->sim;
    sim.position = s.position;
    sim.orientation = matrixOfQuaternion(s.rotation);
    sim.velocity = s.velocity;
    sim.angularVelocity = {0, 0, 0};
    const float along = sim.forwardSpeed();
    for (size_t k = 0; k < 4; ++k) {
        CarSim::Wheel& w = sim.wheels[k];
        w.compression = s.compression[k];
        w.steer = k < 2 ? s.steer : 0;
        w.spin += along / std::max(0.1f, r.car->setup.wheelRadius[k]) * dt;
        w.contact = true;
    }
    r.car->lamps = {(s.lamps & kLampHead) != 0, (s.lamps & kLampBrake) != 0, (s.lamps & kLampReverse) != 0};
    r.car->siren = s.lamps & kLampSiren ? 1 + int(clock_ * 4) % 2 : 0;
    r.next = std::min<size_t>(s.next, targets_.size());
    r.finishTime = s.finishTime;
}

void RaceViewer::netSay(const std::string& text) {
    if (text.empty())
        return;
    netMessage_ = text;
    netMessageFor_ = kMessageFor;
}

void RaceViewer::announce(const GoldEvent& e) {
    // The announcer's line for it: the robber teams by colour, cops and
    // robbers by side (a free-for-all's are the robbers').
    if (speech_.empty() || !ctx_.mixer)
        return;
    std::string name;
    const bool teams = gold_.mode() == GoldMode::RobberTeams;
    const bool cop = gold_.mode() == GoldMode::CopsAndRobbers && e.side == 1;
    switch (e.kind) {
    case GoldEvent::Kind::Got:
        name = teams ? (e.side ? "BLUETEAMHASGOLD" : "REDTEAMHASGOLD") : cop ? "COPGETLOOT" : "ROBGETLOOT";
        break;
    case GoldEvent::Kind::Recovered:
        name = teams ? (e.side ? "BLUETEAMHASGOLD" : "REDTEAMHASGOLD") : cop ? "COPRECOVERLOOT" : "ROBRECOVERLOOT";
        break;
    case GoldEvent::Kind::Dropped:
        name = teams ? (e.side ? "BLUETEAMDROPPEDGOLD" : "REDTEAMDROPPEDGOLD") : cop ? "COPDROPLOOT" : "ROBDROPLOOT";
        break;
    case GoldEvent::Kind::Delivered:
        name = teams ? (e.side ? "BLUETEAMSTASHEDGOLD" : "REDTEAMSTASHEDGOLD") : cop ? "COPSTASHLOOT" : "ROBSTASHLOOT";
        break;
    default:
        return;
    }
    const auto it = speech_.find(name);
    if (it == speech_.end() || it->second.empty())
        return;
    const std::string& clip = it->second[(e.seq * 7) % it->second.size()];
    std::vector<uint8_t> data;
    if (ctx_.files.read("aud/aud22/" + clip + ".22k.wav", data) &&
        ctx_.files.read("aud/aud11/" + clip + ".11k.wav", data))
        return;
    auto sound = std::make_shared<audio::Sound>();
    if (!audio::decodeSound(data, *sound))
        ctx_.mixer->play(sound, audio::SoundType::Speech);
}

void RaceViewer::stepNet(float dt) {
    NetSession* session = net_ ? net_->session() : nullptr;
    if (!session || racers_.empty())
        return;
    const NetSettings& set = session->settings();
    Racer& player = racers_.front();
    CarSim& sim = player.car->sim;
    netMessageFor_ = std::max(0.0f, netMessageFor_ - dt);

    // Our car, to the others.
    if (session->counting()) {
        NetCarState mine;
        mine.clock = clock_;
        mine.position = sim.position;
        mine.velocity = sim.velocity;
        mine.rotation = quaternionOf(sim.orientation);
        mine.steer = sim.wheels[0].steer;
        for (size_t k = 0; k < 4; ++k)
            mine.compression[k] = sim.wheels[k].compression;
        mine.lamps = uint8_t((player.car->lamps.head ? kLampHead : 0) | (player.car->lamps.brake ? kLampBrake : 0) |
                             (player.car->lamps.reverse ? kLampReverse : 0) | (player.car->siren ? kLampSiren : 0));
        mine.next = uint16_t(std::min<size_t>(player.next, 65535));
        mine.finishTime = player.finishTime;
        session->sendCar(mine);
    }
    // A cop's siren, on the police car (H).
    if (player.car->hasSiren() && (keys_ & kDriveHorn) && state_ == State::Racing)
        player.car->siren = 1 + int(clock_ * 4) % 2;
    else if (player.car->hasSiren())
        player.car->siren = 0;

    if (set.mode == NetMode::CopsRobbers) {
        // The host runs the gold; the others hear how it is.
        if (session->isHost()) {
            if (state_ == State::Racing) {
                std::vector<GoldGame::Car> cars;
                for (const Racer& r : racers_) {
                    const uint8_t id = r.player ? session->myId() : r.peer;
                    const NetPlayer* p = session->player(id);
                    if (p)
                        cars.push_back({id, p->team, r.car->sim.position, r.car->sim.velocity});
                }
                gold_.step(clock_, dt, cars, scene_->psdl().min[1] - 50);
            }
            session->setGameState(gold_.encode(), gold_.over());
        } else if (!session->gameState().empty()) {
            gold_.decode(session->gameState());
        }
        // The gold's weight on the car that has it.
        sim.ballast = gold_.holder() == session->myId() ? float(set.goldMass) : 0;
        for (const GoldEvent& e : gold_.eventsAfter(goldSeen_)) {
            goldSeen_ = e.seq;
            const NetPlayer* p = session->player(e.player);
            const bool me = e.player == session->myId();
            const std::string who = p ? p->name : "";
            switch (e.kind) {
            case GoldEvent::Kind::Got:
            case GoldEvent::Kind::Recovered:
                netSay(me ? strings_.get(115, "You have the Gold!") : who + " " + strings_.get(133, "has the Gold!"));
                break;
            case GoldEvent::Kind::Dropped:
                netSay(me ? strings_.get(112, "You dropped the gold!")
                          : who + " " + strings_.get(136, "dropped the Gold!"));
                break;
            case GoldEvent::Kind::Delivered:
                netSay(me ? strings_.get(116, "Gold delivered!")
                          : who + " " + strings_.get(137, "delivered the Gold!"));
                break;
            case GoldEvent::Kind::Returned:
                netSay("The gold is back where it started");
                break;
            case GoldEvent::Kind::TimeUp:
                netSay(strings_.get(118, "Time's up!"));
                break;
            case GoldEvent::Kind::PointLimit:
                netSay(strings_.get(119, "Point limit reached"));
                break;
            }
            announce(e);
        }
        // "N minutes remaining", as the game counts down a timed game.
        if (set.limit == GoldLimit::Time && state_ == State::Racing) {
            static const int kMinutes[] = {20, 15, 10, 5, 1};
            const float left = float(set.limitValue) * 60 - clock_;
            for (int k = 0; k < 5; ++k)
                if (left <= float(kMinutes[k]) * 60 && left > float(kMinutes[k]) * 60 - 2 &&
                    minutesSaid_ != kMinutes[k] && float(set.limitValue) > float(kMinutes[k])) {
                    minutesSaid_ = kMinutes[k];
                    netSay(strings_.get(uint32_t(138 + k), std::to_string(kMinutes[k]) + " minutes remaining"));
                }
        }
        if (gold_.over() && state_ != State::Finished) {
            // The side with the most.
            GoldGame::Car self{session->myId(), session->me() ? session->me()->team : uint8_t(0), {}, {}};
            int best = -1;
            uint8_t winner = 0;
            for (const auto& [side, points] : gold_.scores())
                if (points > best) {
                    best = points;
                    winner = side;
                }
            outcome_ = best < 0 ? strings_.get(100, "Race over")
                       : winner == gold_.sideOf(self)
                           ? strings_.get(164, "You Won!")
                           : (gold_.teams() ? net_->teamName(winner)
                                            : (session->player(winner) ? session->player(winner)->name : "")) +
                                 " won";
            state_ = State::Finished;
        }
        return;
    }
    if (set.mode == NetMode::Cruise)
        return;

    // A race: the host says when it's over (everyone in, a minute after
    // the first, or a Blitz's time up).
    if (session->isHost() && state_ != State::Countdown) {
        bool all = true;
        float first = -1;
        for (const Racer& r : racers_) {
            all = all && r.finishTime >= 0;
            if (r.finishTime >= 0)
                first = first < 0 ? r.finishTime : std::min(first, r.finishTime);
        }
        const float limit = race_.settings[0].timeLimit;
        const bool over = all || (first >= 0 && clock_ > first + kRaceOn) ||
                          (race_.kind == RaceKind::Blitz && limit > 0 && clock_ > limit);
        session->setGameState({}, over);
    }
    if (session->over() && state_ != State::Finished) {
        if (player.finishTime >= 0) {
            int place = 1;
            for (const Racer& r : racers_)
                if (!r.player && r.finishTime >= 0 && r.finishTime < player.finishTime)
                    ++place;
            static const char* const kPlaces[] = {"1st!", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
            outcome_ =
                raceMessage(3 + std::min(place, 8), std::string("You finished ") + kPlaces[std::min(place, 8) - 1]) +
                "  " + clockText(player.finishTime);
            if (race_.kind == RaceKind::Blitz && place == 1)
                outcome_ = raceMessage(6, "You Won!") + "  " + clockText(player.finishTime);
        } else
            outcome_ = strings_.get(100, "Race over");
        state_ = State::Finished;
    }
}

bool RaceViewer::netEvent(const Event& ev) {
    // The multiplayer menus take everything while they're up.
    if (state_ == State::Menu && netMenus_ && net_) {
        switch (net_->handle(ev)) {
        case NetMenus::Action::Back:
            netMenus_ = false;
            break;
        case NetMenus::Action::Garage:
            netMenus_ = false;
            garageForNet_ = true;
            front_->showGarage();
            break;
        default:
            break;
        }
        return true;
    }
    if (!netGame_)
        return false;
    // Typing a line to the others (T, then Enter).
    if (chatting_) {
        if (ev.type == EventType::TextInput) {
            for (const char* c = ev.text; *c; ++c)
                if (static_cast<unsigned char>(*c) >= 32 && chatLine_.size() < 100)
                    chatLine_.push_back(*c);
        } else if (ev.type == EventType::KeyDown) {
            if (ev.key == key::Return || ev.key == key::KpEnter) {
                if (net_ && net_->session() && !chatLine_.empty())
                    net_->session()->say(chatLine_);
                chatting_ = false;
            } else if (ev.key == key::Escape) {
                chatting_ = false;
            } else if (ev.key == key::Backspace && !chatLine_.empty()) {
                chatLine_.pop_back();
            }
            if (!chatting_)
                ctx_.sys.setTextInput(false);
        }
        return true;
    }
    if (ev.type != EventType::KeyDown)
        return false;
    NetSession* session = net_ ? net_->session() : nullptr;
    if (ev.key == 't') {
        chatting_ = true;
        chatLine_.clear();
        keys_ = 0;
        ctx_.sys.setTextInput(true);
        return true;
    }
    const bool enter = ev.key == key::Return || ev.key == key::KpEnter;
    if (ev.key == key::Escape || (state_ == State::Finished && enter)) {
        // The host takes everyone back to the lobby; a player leaves (Esc)
        // or waits for the host (Enter).
        if (session && session->isHost())
            session->backToLobby();
        else if (ev.key == key::Escape) {
            net_->leave();
            endNetGame();
            netMenus_ = true;
            net_->showLobby();
        }
        return true;
    }
    return false;
}

void RaceViewer::drawGold() {
    NetSession* session = net_ ? net_->session() : nullptr;
    if (!session || session->settings().mode != NetMode::CopsRobbers)
        return;
    // The gold (geometry/wpobj_gold) turning where it lies, or above the
    // car that has it; the checkpoint stands either side of each base.
    const ModelCache::Model& gold = scene_->models().get("wpobj_gold");
    const float turn = clock_ * 2;
    Vec3 at = gold_.goldPosition() + Vec3{0, 0.6f, 0};
    for (const Racer& r : racers_)
        if ((r.player ? session->myId() : r.peer) == gold_.holder())
            at = r.car->sim.position + Vec3{0, 2.2f, 0};
    if (gold.ok)
        drawModelMesh(*renderer_, gold.levels.front(), matrixOf(Mat3::rotationY(turn), at), scene_->light(), false);
    const ModelCache::Model& stand = scene_->models().get("pt_check");
    if (!stand.ok)
        return;
    for (uint8_t side = 0; side < (gold_.teams() ? 2 : 1); ++side) {
        const Vec3 base = gold_.base(side);
        const Mat3 face = Mat3::rotationY(yawTowards(base, gold_.home()));
        for (float s : {-1.0f, 1.0f})
            drawModelMesh(*renderer_, stand.levels.front(), matrixOf(face, base + face * Vec3{s * 8, 0.5f, 0}),
                          scene_->light(), false);
    }
}

void RaceViewer::drawNetLines() {
    NetSession* session = net_ ? net_->session() : nullptr;
    if (!session)
        return;
    // Waiting for the others to load, then the countdown.
    if (!session->counting()) {
        int loading = 0;
        for (const NetPlayer& p : session->players())
            loading += p.loaded ? 0 : 1;
        drawCentred(loading > 0 ? strings_.get(uint32_t(30 + std::min(loading, 7)), "Waiting for players")
                                : std::string("Waiting for the host"),
                    kFrameH / 2 - 80, {255, 255, 255}, 2);
    }
    // What's been said, and who's come and gone.
    int y = 300;
    const auto& lines = net_->lines();
    for (size_t i = lines.size() > 4 ? lines.size() - 4 : 0; i < lines.size(); ++i)
        if (nowMs_ < lines[i].atMs + uint64_t(kLinesFor * 1000)) {
            font::drawText(overlay_, 4, y, lines[i].text,
                           lines[i].system ? Color{255, 200, 120} : Color{230, 230, 230});
            y += font::kLineHeight + 2;
        }
    if (chatting_)
        font::drawText(overlay_, 4, y + 4, strings_.get(309, "Enter Chat Msg") + ": " + chatLine_ + "_",
                       {255, 255, 120});
    else if (netMessageFor_ > 0)
        drawCentred(netMessage_, 110, {255, 230, 120}, 2);
}

void RaceViewer::drawNetHud() {
    NetSession* session = net_ ? net_->session() : nullptr;
    if (!session || racers_.empty())
        return;
    const NetSettings& set = session->settings();
    const Racer& player = racers_.front();
    char buf[120];
    font::drawText(overlay_, 4, 4, net_->modeName(set.mode) + "  (" + cities_[size_t(city_)].name + ")",
                   {150, 200, 255});
    const bool timed = set.mode == NetMode::CopsRobbers && set.limit == GoldLimit::Time;
    const float shown = timed ? float(set.limitValue) * 60 - std::max(0.0f, clock_) : std::max(0.0f, clock_);
    font::drawText(overlay_, 4, 18, (timed ? "LEFT " : "TIME ") + clockText(shown), {255, 255, 255}, 2);
    pictures_.clock(overlay_, shown, kFrameW - 8, 8);
    if (set.mode == NetMode::CopsRobbers) {
        // The score: each team's, or each player's (the best first).
        int y = 40;
        GoldGame::Car self{session->myId(), session->me() ? session->me()->team : uint8_t(0), {}, {}};
        if (gold_.teams()) {
            for (uint8_t side = 0; side < 2; ++side) {
                std::snprintf(buf, sizeof buf, "%-10s %d", net_->teamName(side).c_str(), gold_.score(side));
                font::drawText(overlay_, 4, y, buf,
                               side == gold_.sideOf(self) ? Color{255, 255, 120} : Color{230, 230, 230}, 2);
                y += 20;
            }
        } else {
            std::vector<std::pair<int, std::string>> rows;
            for (const NetPlayer& p : session->players())
                rows.push_back({gold_.score(p.id), p.name});
            std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < rows.size() && i < 4; ++i) {
                std::snprintf(buf, sizeof buf, "%-12.12s %d", rows[i].second.c_str(), rows[i].first);
                font::drawText(overlay_, 4, y, buf, {230, 230, 230}, 2);
                y += 20;
            }
        }
        if (gold_.holder() == session->myId() && state_ != State::Finished)
            drawCentred(strings_.get(115, "You have the Gold!"), 80, {255, 220, 60}, 2);
    }
    // The countdown.
    if (clock_ < -kCountdown / 2 && session->counting())
        drawCentred(raceMessage(0, "Ready..."), kFrameH / 2 - 40, {255, 255, 255}, 5);
    else if (clock_ < 0 && session->counting())
        drawCentred(raceMessage(1, "Set..."), kFrameH / 2 - 40, {255, 230, 120}, 5);
    else if (clock_ >= 0 && clock_ < 1)
        drawCentred(raceMessage(2, "Go!"), kFrameH / 2 - 40, {120, 255, 120}, 6);
    if (state_ == State::Finished) {
        font::drawText(overlay_, 60, 150, outcome_, {255, 255, 120}, 3);
        const bool hosting = session->isHost();
        font::drawText(overlay_, 60, 190,
                       hosting ? "Enter: everyone back to the lobby" : "Waiting for the host (Esc: leave the session)",
                       {150, 200, 255});
    }
    drawNetLines();
    drawSpeedometer(overlay_, player.car->sim,
                    hudMap_.loaded() ? int(hudMap_.setup().left * float(kFrameW)) - 8 : kFrameW - 8, wear_.fraction(),
                    pictures_);
}

} // namespace reluva::age
