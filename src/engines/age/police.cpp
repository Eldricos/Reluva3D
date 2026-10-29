// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The police as Reluva runs them; see docs/engines/age-traffic.md.

#include "engines/age/police.h"

#include "common/audio/decode.h"
#include "engines/age/race.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kRam = 30;        // metres within which a chasing car drives straight at the player
constexpr float kBeside = 9;      // metres from the player counting towards a bust
constexpr float kReroute = 5;     // seconds between routes along the roads at the most
constexpr float kMoved = 20;      // metres the player goes for a new route sooner
constexpr float kSirenHeard = 250; // metres
constexpr float kRest = 200;      // metres beyond which a waiting car that has settled isn't simulated
constexpr float kPatrolLength = 1000; // metres of lanes a patrol is planned ahead
constexpr float kKeepBack = 18;   // metres a patrolling car keeps behind what's ahead

float flatDistance(const Vec3& a, const Vec3& b) {
    const float dx = a[0] - b[0], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

// Nothing of the city between two cars (a metre up).
bool clearBetween(const CollisionWorld& world, const Vec3& a, const Vec3& b) {
    const Vec3 from = a + Vec3{0, 1, 0};
    const Vec3 to = b + Vec3{0, 1, 0} - from;
    const float d = length(to);
    CollisionWorld::RayHit hit;
    return d < 1 || !world.raycast(from, to * (1 / d), d, hit);
}

} // namespace

Error Police::setup(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files,
                    const AiMapSettings& settings, int count, audio::Mixer* mixer, const std::string& sirenTable) {
    clear();
    noticeDistance_ = settings.copChaseDistance > 0 ? settings.copChaseDistance : kNotice;
    Error first = Error::success();
    for (const auto& p : settings.police) {
        if (count >= 0 && int(cops_.size()) >= count)
            break;
        Cop cop;
        cop.car = std::make_unique<CarActor>();
        if (Error err = cop.car->load(renderer, textures, files, p.model, 0)) {
            if (!first)
                first = err;
            continue;
        }
        cop.car->sim.reset(cop.car->setup, p.position + Vec3{0, 0.5f, 0}, headingToYaw(p.heading));
        cops_.push_back(std::move(cop));
    }
    mixer_ = mixer;
    if (mixer_) {
        if (Error err = siren_.load(*mixer_, files, sirenTable); err && !first)
            first = err;
        std::vector<uint8_t> data;
        auto sound = std::make_shared<audio::Sound>();
        if ((!files.read("aud/aud22/arrest.22k.wav", data) || !files.read("aud/aud11/arrest.11k.wav", data)) &&
            !audio::decodeSound(data, *sound))
            arrest_ = sound;
    }
    return first;
}

void Police::clear() {
    siren_.stop();
    cops_.clear();
    busted_ = false;
    bustTimer_ = 0;
}

void Police::forgive() {
    for (Cop& c : cops_) {
        c.chasing = false;
        c.car->siren = 0;
    }
    busted_ = false;
    bustTimer_ = 0;
}

void Police::chaseAll() {
    for (Cop& c : cops_) {
        c.chasing = true;
        c.patrolling = false;
        c.settled = c.reroute = c.behind = 0;
    }
}

int Police::patrolling() const {
    int n = 0;
    for (const Cop& c : cops_)
        n += c.patrolling && !c.chasing;
    return n;
}

void Police::startPatrol(Cop& cop, float speedLimit) {
    // From the nearest lane going its way, on through the junctions,
    // turning at random (not back along the same road where there's a
    // choice).
    cop.patrolling = false;
    if (!lanes_ || lanes_->lanes().empty())
        return;
    const auto& lanes = lanes_->lanes();
    const CarSim& sim = cop.car->sim;
    Vec3 ahead = -sim.orientation.z;
    ahead[1] = 0;
    int bestLane = -1;
    size_t bestPoint = 0;
    float best = 80; // metres, and worse for a lane the other way
    for (size_t l = 0; l < lanes.size(); ++l) {
        const auto& pts = lanes[l].points;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const Vec3 way = pts[i + 1] - pts[i];
            const float score = flatDistance(pts[i], sim.position) + (dot(way, ahead) < 0 ? 25.0f : 0.0f);
            if (score < best) {
                best = score;
                bestLane = int(l);
                bestPoint = i + 1;
            }
        }
    }
    if (bestLane < 0)
        return;
    std::vector<Vec3> path;
    cop.laneEnds.clear();
    int lane = bestLane;
    size_t from = bestPoint;
    float planned = 0;
    for (int steps = 0; steps < 30 && planned < kPatrolLength; ++steps) {
        const TrafficLane& l = lanes[size_t(lane)];
        for (size_t i = from; i < l.points.size(); ++i) {
            if (!path.empty())
                planned += flatDistance(path.back(), l.points[i]);
            path.push_back(l.points[i]);
            cop.laneEnds.push_back(i + 1 == l.points.size() ? lane : -1);
        }
        const std::vector<uint32_t> ways = lanes_->waysOn(lane);
        if (ways.empty())
            break;
        lane = int(ways[std::uniform_int_distribution<size_t>(0, ways.size() - 1)(rng_)]);
        from = 0;
    }
    if (path.size() < 2)
        return;
    cop.driver.start(path, 0, 1.0f);
    cop.driver.setSpeedLimit(speedLimit);
    cop.patrolling = true;
}

CarControls Police::patrol(Cop& cop, const std::vector<RoadUser>& traffic, float dt) {
    CarSim& sim = cop.car->sim;
    const float speed = sim.forwardSpeed();
    bool stop = false;
    Vec3 fwd = -sim.orientation.z;
    fwd[1] = 0;
    fwd = normalized(fwd);
    // A light that isn't green at the next lane end still ahead (the
    // driver counts a point reached some way short of it), in braking
    // distance.
    const auto& lanes = lanes_->lanes();
    const size_t first = cop.driver.next() > 4 ? cop.driver.next() - 4 : 0;
    for (size_t k = first; k < cop.laneEnds.size() && k < cop.driver.next() + 40; ++k) {
        if (cop.laneEnds[k] < 0)
            continue;
        Vec3 to = cop.driver.path()[k] - sim.position;
        to[1] = 0;
        const float d = dot(to, fwd);
        if (d < 1)
            continue; // passed
        const TrafficLane& l = lanes[size_t(cop.laneEnds[k])];
        if (lights_ && l.control == 1 && lights_->has(l.road, l.toEnd) &&
            lights_->signal(l.road, l.toEnd) != Signal::Green && d < speed * speed / 12 + 10 && (d > 2 || speed < 4))
            stop = true;
        break;
    }
    // Something ahead in its way.
    for (const RoadUser& u : traffic) {
        Vec3 rel = u.position - sim.position;
        rel[1] = 0;
        const float along = dot(rel, fwd);
        if (along > 1 && along < kKeepBack && length(rel - fwd * along) < 2.5f)
            stop = true;
    }
    if (stop) {
        // Waiting (the driver isn't asked, so it doesn't count this as being
        // stuck): braking, but not into reverse once stopped.
        CarControls c;
        c.brake = speed > 0.5f ? 1.0f : 0.0f;
        c.handbrake = speed <= 0.5f;
        return c;
    }
    CarControls c = cop.driver.drive(sim, dt);
    if (cop.driver.wantsReset()) {
        sim.reset(cop.car->setup, cop.driver.resetPoint() + Vec3{0, 0.5f, 0}, cop.driver.resetYaw());
        cop.driver.resetDone();
    }
    return c;
}

int Police::chasing() const {
    int n = 0;
    for (const Cop& c : cops_)
        n += c.chasing;
    return n;
}

void Police::update(float dt, CarSim& player, const CollisionWorld& world, const RoadGraph& roads, float speedLimit,
                    const std::vector<RoadUser>& traffic) {
    clock_ += dt;
    const float playerSpeed = std::fabs(player.forwardSpeed());
    float nearestChase = 1e9f;
    for (Cop& cop : cops_) {
        CarSim& sim = cop.car->sim;
        const float d = flatDistance(sim.position, player.position);
        // Run into, or sped past.
        const Vec3 before = sim.velocity;
        collideCars(player, sim);
        const bool hit = length(sim.velocity - before) > 1.5f;
        if (!cop.chasing && (hit || (d < noticeDistance_ && playerSpeed > speedLimit * 1.5f))) {
            cop.chasing = true;
            cop.patrolling = false;
            cop.settled = 0;
            cop.reroute = 0;
            cop.behind = 0;
        }
        CarControls c;
        if (!cop.chasing && lanes_) {
            // On patrol, a new one at the end of the last (or after a chase).
            if (!cop.patrolling || cop.driver.finished())
                startPatrol(cop, speedLimit);
            if (cop.patrolling)
                c = patrol(cop, traffic, dt);
            else
                c.brake = std::fabs(sim.forwardSpeed()) > 0.5f ? 1.0f : 0.0f;
        } else if (!cop.chasing) {
            cop.settled += dt;
            if (cop.settled > 3 && d > kRest) {
                cop.car->siren = 0;
                continue;
            }
            c.brake = std::fabs(sim.forwardSpeed()) > 0.5f ? 1.0f : 0.0f; // waiting
        } else if (cop.backing > 0) {
            cop.backing -= dt;
            c.brake = 1; // held when stopped: backs up
            c.steer = cop.stuck > 0 ? 1.0f : -1.0f;
        } else if (d < kRam && clearBetween(world, sim.position, player.position)) {
            // Straight at it, a little ahead of where it's going; boxing it
            // in once it's slow.
            Vec3 aim = player.position + player.velocity * 0.4f - sim.position;
            aim[1] = 0;
            Vec3 fwd = -sim.orientation.z, right = sim.orientation.x;
            fwd[1] = right[1] = 0;
            const float angle = std::atan2(dot(aim, normalized(right)), dot(aim, normalized(fwd)));
            c.steer = std::clamp(angle * 2, -1.0f, 1.0f);
            if (d < 7 && playerSpeed < 4)
                c.brake = 1;
            else
                c.throttle = std::fabs(angle) > 1.2f && sim.forwardSpeed() > 15 ? 0.3f : 1.0f;
            // Against something: back out a little.
            cop.stuck = c.throttle > 0 && std::fabs(sim.forwardSpeed()) < 1 ? cop.stuck + dt : 0;
            if (cop.stuck > 2) {
                cop.backing = 1.5f;
                cop.stuck = 0;
            }
            cop.reroute = 0;
        } else {
            // Along the roads to where the player is.
            cop.reroute -= dt;
            if (cop.reroute <= 0 || cop.driver.finished() || flatDistance(cop.routedTo, player.position) > kMoved) {
                size_t loopFrom = 0;
                cop.driver.start(routePath(roads, sim.position, {player.position}, false, loopFrom), 0, 1.0f);
                cop.reroute = kReroute;
                cop.routedTo = player.position;
            }
            c = cop.driver.drive(sim, dt);
            if (cop.driver.wantsReset()) {
                sim.reset(cop.car->setup, cop.driver.resetPoint() + Vec3{0, 0.5f, 0}, cop.driver.resetYaw());
                cop.driver.resetDone();
            }
        }
        sim.step(c, world, dt);
        cop.car->lamps = {lampsLit_, c.brake > 0 && sim.forwardSpeed() > 0.5f, sim.gear < 0};
        if (cop.chasing) {
            cop.behind = d > kGiveUp ? cop.behind + dt : 0;
            if (cop.behind > 8)
                cop.chasing = false;
        }
        cop.car->siren = cop.chasing ? 1 + int(clock_ * 4) % 2 : 0;
        if (cop.chasing)
            nearestChase = std::min(nearestChase, d);
    }
    // Busted: held stopped beside a chasing car.
    if (nearestChase < kBeside && playerSpeed < 2)
        bustTimer_ += dt;
    else
        bustTimer_ = std::max(0.0f, bustTimer_ - dt);
    if (bustTimer_ >= kBustTime && !busted_) {
        busted_ = true;
        if (mixer_ && arrest_)
            mixer_->play(arrest_, audio::SoundType::Effects);
    }
    // The siren of the nearest chasing car, quieter with distance.
    siren_.update(dt, nearestChase < kSirenHeard ? (1 - nearestChase / kSirenHeard) * 0.8f : 0.0f);
}

void Police::draw(const SceneLight& light) const {
    for (const Cop& c : cops_)
        c.car->draw(light);
}

void Police::drawFlares(const FlareDrawer& flares, const Vec3& eye) const {
    for (const Cop& c : cops_)
        c.car->drawFlares(flares, eye);
}

void Police::headlightPools(std::vector<gfx::DynamicLight>& out) const {
    gfx::DynamicLight l;
    for (const Cop& c : cops_)
        if (c.car->headlightPool(l))
            out.push_back(l);
}

void Police::users(std::vector<RoadUser>& out) const {
    for (const Cop& c : cops_)
        out.push_back({c.car->sim.position, c.car->sim.velocity});
}

std::vector<CarSim*> Police::cars() {
    std::vector<CarSim*> out;
    for (Cop& c : cops_)
        out.push_back(&c.car->sim);
    return out;
}

} // namespace reluva::age
