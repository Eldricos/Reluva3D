// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/city_life.h"

#include "engines/age/psdl.h"

#include <algorithm>

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
    // The road blocks' street furniture rules.
    blockRules_.clear();
    Psdl psdl;
    if (!files.read("city/" + map + ".psdl", data) && !readPsdl(data, psdl))
        for (const PsdlBlock& b : psdl.blocks)
            blockRules_.push_back(b.extra);
    return Error::success();
}

Error CityLife::start(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files,
                      const AiMapSettings& settings, const Options& options, audio::Mixer* mixer) {
    stop();
    current_ = settings;
    carTouches_ = 0;
    traffic_.setup(files, roads_, settings, options.seed);
    traffic_.setDensity(options.traffic);
    lights_.build(roads_, settings);
    traffic_.setLights(&lights_);
    traffic_.setLaneChanges(options.laneChanges || settings.laneChanges);
    // London's parked cars have their own models; San Francisco's lines
    // name none, so they're the traffic's.
    std::vector<std::string> parked;
    if (map_ == "london")
        parked = {"sp_pcar01_l", "sp_pcar02_l", "sp_pcar03_l"};
    traffic_.park(files, readCityPaths(files, map_, "parkedcar", options.variant), parked);
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
    gizmos_.load(files, map_, options.variant, mixer);
    ambient_.load(files, map_, mixer, options.seed + 2);
    // The props and the traffic lights, to knock down: their shapes once
    // the models are at hand (the first draw), in the world after.
    files_ = &files;
    props_.load(files, map_, options.variant);
    props_.addStreetProps(files, map_, roads_, blockRules_);
    props_.addLights(lights_);
    propsInWorld_ = false;
    return first;
}

void CityLife::stop() {
    gizmos_.clear();
    ambient_.clear();
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
    gizmos_.update(dt, world, player.position);
    ambient_.update(dt, player.position, player.orientation.x, player.velocity);
    if (props_.prepared() && !propsInWorld_) {
        props_.collide(world);
        propsInWorld_ = true;
    }
    {
        std::vector<CarSim*> drivers{&player};
        drivers.insert(drivers.end(), others.begin(), others.end());
        for (CarSim* c : police_.cars())
            drivers.push_back(c);
        props_.update(dt, drivers, world, lights_);
    }
    // The police keep behind what's ahead on patrol: the traffic and the
    // other cars.
    std::vector<RoadUser> ahead{{player.position, player.velocity}};
    for (CarSim* c : others)
        ahead.push_back({c->position, c->velocity});
    traffic_.users(ahead);
    gizmos_.users(ahead); // open bridges, trains
    police_.users(ahead); // each other (a car never counts itself: it's not ahead of itself)
    const Vec3 beforePolice = player.velocity; // the police move the player only by running into it
    police_.update(dt, player, world, roads, current_.speedLimit, ahead);
    if (length(player.velocity - beforePolice) > 0.05f)
        ++carTouches_;
    // Every driven car bumps the traffic and the trains; the police bump
    // the opponents; the trains shove the traffic aside.
    std::vector<CarSim*> cops = police_.cars();
    // The player's knocks against the traffic and the trains, as an
    // impulse for its sounds.
    // The knock is on the side it was pushed away from.
    auto knocked = [&](float change, const Vec3& before) {
        Vec3 push = player.velocity - before;
        push[1] = 0;
        if (change > 0 && length(push) > 1e-4f)
            player.knock(change * player.setup().mass, player.position - normalized(push) * 2);
    };
    Vec3 before = player.velocity;
    const float traffic = traffic_.collide(player);
    carTouches_ += traffic > 0;
    knocked(traffic, before);
    before = player.velocity;
    knocked(gizmos_.collide(player), before);
    for (CarSim* c : others) {
        traffic_.collide(*c);
        gizmos_.collide(*c);
    }
    for (CarSim* c : cops) {
        traffic_.collide(*c);
        gizmos_.collide(*c);
        for (CarSim* o : others)
            collideCars(*c, *o);
    }
    gizmos_.push(traffic_);
    std::vector<RoadUser> users{{player.position, player.velocity}};
    for (CarSim* c : others)
        users.push_back({c->position, c->velocity});
    police_.users(users);
    gizmos_.users(users); // open bridges, trains
    traffic_.update(dt, player.position, users, world, fill);
    traffic_.users(users);
    peds_.update(dt, player.position, users, world, fill);
}

void CityLife::draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
                    const Vec3& eye, const FlareDrawer* flares) {
    if (!props_.prepared() && files_)
        props_.prepare(*files_, models);
    props_.draw(renderer, models, light, viewProj, eye);
    lights_.draw(renderer, models, light, viewProj, eye, night_);
    gizmos_.draw(renderer, models, light, viewProj, eye);
    traffic_.draw(renderer, models, light, viewProj, eye, lampsLit_, flares);
    peds_.draw(viewProj, eye, light);
    police_.draw(light);
    if (flares) {
        police_.drawFlares(*flares, eye);
        if (night_)
            props_.drawGlows(*flares, eye);
    }
}

void CityLife::headlightPools(std::vector<gfx::DynamicLight>& out, const Vec3& eye) const {
    if (!lampsLit_)
        return;
    traffic_.headlightPools(out, eye);
    police_.headlightPools(out);
}

} // namespace reluva::age
