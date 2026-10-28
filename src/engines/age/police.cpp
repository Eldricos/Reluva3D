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
                    const AiMapSettings& settings, int count, audio::Mixer* mixer) {
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
        std::vector<uint8_t> data;
        auto sound = std::make_shared<audio::Sound>();
        if (!files.read("aud/aud11/police1fastsirenloop.11k.wav", data) && !audio::decodeSound(data, *sound))
            sirenSound_ = sound;
    }
    return first;
}

void Police::clear() {
    if (mixer_ && siren_ != audio::kNoSound)
        mixer_->stop(siren_);
    siren_ = audio::kNoSound;
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

int Police::chasing() const {
    int n = 0;
    for (const Cop& c : cops_)
        n += c.chasing;
    return n;
}

void Police::update(float dt, CarSim& player, const CollisionWorld& world, const RoadGraph& roads, float speedLimit) {
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
            cop.settled = 0;
            cop.reroute = 0;
            cop.behind = 0;
        }
        CarControls c;
        if (!cop.chasing) {
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
    if (bustTimer_ >= kBustTime)
        busted_ = true;
    // The siren of the nearest chasing car.
    if (mixer_ && sirenSound_) {
        const bool heard = nearestChase < kSirenHeard;
        if (heard && siren_ == audio::kNoSound)
            siren_ = mixer_->play(sirenSound_, audio::SoundType::Effects, 0, 0, true);
        if (!heard && siren_ != audio::kNoSound) {
            mixer_->stop(siren_);
            siren_ = audio::kNoSound;
        }
        if (siren_ != audio::kNoSound)
            mixer_->setVolume(siren_, int((1 - nearestChase / kSirenHeard) * 0.8f * audio::kMaxVolume));
    }
}

void Police::draw(const SceneLight& light) const {
    for (const Cop& c : cops_)
        c.car->draw(light);
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
