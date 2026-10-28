// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/city_life.h"

namespace reluva::age {

Error CityLife::load(const SearchSet& files, const std::string& map) {
    stop();
    roads_ = AiRoadMap{};
    city_ = AiMapSettings{};
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
    Error first = peds_.setup(renderer, files, roads_, settings, options.rain, options.seed + 1);
    peds_.setDensity(options.peds);
    if (Error err = police_.setup(renderer, textures, files, settings, options.cops, mixer); err && !first)
        first = err;
    return first;
}

void CityLife::stop() {
    traffic_.clear();
    peds_.unload();
    police_.clear();
}

void CityLife::update(float dt, CarSim& player, const std::vector<CarSim*>& others, const CollisionWorld& world,
                      const RoadGraph& roads, bool fill) {
    police_.update(dt, player, world, roads, current_.speedLimit);
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
    traffic_.update(dt, player.position, users, world, fill);
    traffic_.users(users);
    peds_.update(dt, player.position, users, world, fill);
}

void CityLife::draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
                    const Vec3& eye) {
    traffic_.draw(renderer, models, light, viewProj, eye);
    peds_.draw(viewProj, eye, light);
    police_.draw(light);
}

} // namespace reluva::age
