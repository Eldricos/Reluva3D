// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/city_life.h"

namespace reluva::age {

Error CityLife::load(const SearchSet& files, const std::string& map) {
    stop();
    roads_ = AiRoadMap{};
    city_ = AiMapSettings{};
    map_ = map;
    std::vector<uint8_t> data;
    if (Error err = files.read("city/" + map + ".bai", data))
        return err;
    if (Error err = readBai(data, roads_))
        return Error::fail("city/" + map + ".bai: " + err.message);
    if (!files.read("city/" + map + ".aimap", data))
        parseAiMap(std::string(data.begin(), data.end()), city_);
    return Error::success();
}

Error CityLife::start(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files,
                      const AiMapSettings& settings, const Options& options, audio::Mixer* mixer) {
    stop();
    current_ = settings;
    traffic_.setup(files, roads_, settings, options.seed);
    traffic_.setDensity(options.traffic);
    lights_.build(roads_, settings);
    traffic_.setLights(&lights_);
    traffic_.setLaneChanges(options.laneChanges || settings.laneChanges);
    Error first = peds_.setup(renderer, files, roads_, settings, options.rain, options.seed + 1);
    peds_.setDensity(options.peds);
    // San Francisco's police sound as the game's computer-driven ones do;
    // London's with its own two-tone siren (the player's table for there:
    // the opponents' has only the American ones).
    const std::string siren = map_ == "london" ? "aud/cardata/player/londonpolicesiren.csv"
                                               : "aud/cardata/opponent/policesiren.csv";
    if (Error err = police_.setup(renderer, textures, files, settings, options.cops, mixer, siren); err && !first)
        first = err;
    police_.setPatrols(options.patrols ? &traffic_.network() : nullptr, &lights_);
    gizmos_.load(files, map_, options.variant);
    return first;
}

void CityLife::stop() {
    gizmos_.clear();
    traffic_.clear();
    peds_.unload();
    police_.clear();
}

void CityLife::setLamps(bool lit, bool night) {
    lampsLit_ = lit;
    night_ = night;
    police_.setLampsLit(lit);
}

void CityLife::update(float dt, CarSim& player, const std::vector<CarSim*>& others, CollisionWorld& world,
                      const RoadGraph& roads, bool fill) {
    lights_.update(dt);
    gizmos_.update(dt, world);
    // The police keep behind what's ahead on patrol: the traffic and the
    // other cars.
    std::vector<RoadUser> ahead{{player.position, player.velocity}};
    for (CarSim* c : others)
        ahead.push_back({c->position, c->velocity});
    traffic_.users(ahead);
    police_.users(ahead); // each other (a car never counts itself: it's not ahead of itself)
    police_.update(dt, player, world, roads, current_.speedLimit, ahead);
    // Every driven car bumps the traffic; the police bump the opponents.
    std::vector<CarSim*> cops = police_.cars();
    traffic_.collide(player);
    for (CarSim* c : others)
        traffic_.collide(*c);
    for (CarSim* c : cops) {
        traffic_.collide(*c);
        for (CarSim* o : others)
            collideCars(*c, *o);
    }
    std::vector<RoadUser> users{{player.position, player.velocity}};
    for (CarSim* c : others)
        users.push_back({c->position, c->velocity});
    police_.users(users);
    gizmos_.users(users); // open bridges
    traffic_.update(dt, player.position, users, world, fill);
    traffic_.users(users);
    peds_.update(dt, player.position, users, world, fill);
}

void CityLife::draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
                    const Vec3& eye, const FlareDrawer* flares) {
    lights_.draw(renderer, models, light, viewProj, eye, night_);
    gizmos_.draw(renderer, models, light, viewProj, eye);
    traffic_.draw(renderer, models, light, viewProj, eye, lampsLit_, flares);
    peds_.draw(viewProj, eye, light);
    police_.draw(light);
    if (flares)
        police_.drawFlares(*flares, eye);
}

void CityLife::headlightPools(std::vector<gfx::DynamicLight>& out, const Vec3& eye) const {
    if (!lampsLit_)
        return;
    traffic_.headlightPools(out, eye);
    police_.headlightPools(out);
}

} // namespace reluva::age
