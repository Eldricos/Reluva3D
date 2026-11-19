// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/game/city_life.h"

#include "engines/age/formats/psdl.h"

#include <algorithm>

namespace reluva::age {

Error CityLife::load(const SearchSet &files, const std::string &map) {
    stop();
    _roads = AiRoadMap{};
    _city = AiMapSettings{};
    _map = map;
    std::vector<uint8_t> data;
    if (Error err = files.read("city/" + map + ".bai", data))
        return err;
    if (Error err = readBai(data, _roads))
        return Error::fail("city/" + map + ".bai: " + err.message);
    if (!files.read("city/" + map + ".aimap", data))
        parseAiMap(std::string(data.begin(), data.end()), _city);
    // The road blocks' street furniture rules.
    _blockRules.clear();
    Psdl psdl;
    if (!files.read("city/" + map + ".psdl", data) && !readPsdl(data, psdl))
        for (const PsdlBlock &b : psdl.blocks)
            _blockRules.push_back(b.extra);
    return Error::success();
}

Error CityLife::start(gfx::Renderer3D &renderer, TextureCache &textures, const SearchSet &files,
                      const AiMapSettings &settings, const Options &options, audio::Mixer *mixer) {
    stop();
    _current = settings;
    _carTouches = 0;
    _traffic.setup(files, _roads, settings, options.seed);
    _traffic.setDensity(options.traffic);
    _lights.build(_roads, settings);
    _traffic.setLights(&_lights);
    _traffic.setLaneChanges(options.laneChanges || settings.laneChanges);
    // London's parked cars have their own models; San Francisco's lines
    // name none, so they're the traffic's.
    std::vector<std::string> parked;
    if (_map == "london")
        parked = {"sp_pcar01_l", "sp_pcar02_l", "sp_pcar03_l"};
    _traffic.park(files, readCityPaths(files, _map, "parkedcar", options.variant), parked);
    Error first = _peds.setup(renderer, files, _roads, settings, options.rain, options.seed + 1);
    _peds.setDensity(options.peds);
    // San Francisco's police sound as the game's computer-driven ones do;
    // London's with its own two-tone siren (the player's table for there:
    // the opponents' has only the American ones).
    const std::string siren =
        _map == "london" ? "aud/cardata/player/londonpolicesiren.csv" : "aud/cardata/opponent/policesiren.csv";
    if (Error err = _police.setup(renderer, textures, files, settings, options.cops, mixer, siren); err && !first)
        first = err;
    _police.setPatrols(options.patrols ? &_traffic.network() : nullptr, &_lights);
    _gizmos.load(files, _map, options.variant, mixer);
    _ambient.load(files, _map, mixer, options.seed + 2);
    // The props and the traffic lights, to knock down: their shapes once
    // the models are at hand (the first draw), in the world after.
    _files = &files;
    _props.load(files, _map, options.variant);
    _props.addStreetProps(files, _map, _roads, _blockRules);
    _props.addLights(_lights);
    _propsInWorld = false;
    return first;
}

void CityLife::stop() {
    _gizmos.clear();
    _ambient.clear();
    _traffic.clear();
    _peds.unload();
    _police.clear();
}

void CityLife::setLamps(bool lit, bool night) {
    _lampsLit = lit;
    _night = night;
    _police.setLampsLit(lit);
}

void CityLife::update(float dt, CarSim &player, const std::vector<CarSim *> &others, CollisionWorld &world,
                      const RoadGraph &roads, bool fill) {
    _lights.update(dt);
    _gizmos.update(dt, world, player.position);
    _ambient.update(dt, player.position, player.orientation.x, player.velocity);
    if (_props.prepared() && !_propsInWorld) {
        _props.collide(world);
        _propsInWorld = true;
    }
    {
        std::vector<CarSim *> drivers{&player};
        drivers.insert(drivers.end(), others.begin(), others.end());
        for (CarSim *c : _police.cars())
            drivers.push_back(c);
        _props.update(dt, drivers, world, _lights);
    }
    // The police keep behind what's ahead on patrol: the traffic and the
    // other cars.
    std::vector<RoadUser> ahead{{player.position, player.velocity}};
    for (CarSim *c : others)
        ahead.push_back({c->position, c->velocity});
    _traffic.users(ahead);
    _gizmos.users(ahead);                      // open bridges, trains
    _police.users(ahead);                      // each other (a car never counts itself: it's not ahead of itself)
    const Vec3 beforePolice = player.velocity; // the police move the player only by running into it
    _police.update(dt, player, world, roads, _current.speedLimit, ahead);
    if (length(player.velocity - beforePolice) > 0.05f)
        ++_carTouches;
    // Every driven car bumps the traffic and the trains; the police bump
    // the opponents; the trains shove the traffic aside.
    std::vector<CarSim *> cops = _police.cars();
    // The player's knocks against the traffic and the trains, as an
    // impulse for its sounds.
    // The knock is on the side it was pushed away from.
    auto knocked = [&](float change, const Vec3 &before) {
        Vec3 push = player.velocity - before;
        push[1] = 0;
        if (change > 0 && length(push) > 1e-4f)
            player.knock(change * player.setup().mass, player.position - normalized(push) * 2);
    };
    Vec3 before = player.velocity;
    const float traffic = _traffic.collide(player);
    _carTouches += traffic > 0;
    knocked(traffic, before);
    before = player.velocity;
    knocked(_gizmos.collide(player), before);
    for (CarSim *c : others) {
        _traffic.collide(*c);
        _gizmos.collide(*c);
    }
    for (CarSim *c : cops) {
        _traffic.collide(*c);
        _gizmos.collide(*c);
        for (CarSim *o : others)
            collideCars(*c, *o);
    }
    _gizmos.push(_traffic);
    std::vector<RoadUser> users{{player.position, player.velocity}};
    for (CarSim *c : others)
        users.push_back({c->position, c->velocity});
    _police.users(users);
    _gizmos.users(users); // open bridges, trains
    _traffic.update(dt, player.position, users, world, fill);
    _traffic.users(users);
    _peds.update(dt, player.position, users, world, fill);
}

void CityLife::draw(gfx::Renderer3D &renderer, ModelCache &models, const SceneLight &light, const gfx::Mat4 &viewProj,
                    const Vec3 &eye, const FlareDrawer *flares) {
    if (!_props.prepared() && _files)
        _props.prepare(*_files, models);
    _props.draw(renderer, models, light, viewProj, eye);
    _lights.draw(renderer, models, light, viewProj, eye, _night);
    _gizmos.draw(renderer, models, light, viewProj, eye);
    _traffic.draw(renderer, models, light, viewProj, eye, _lampsLit, flares);
    _peds.draw(viewProj, eye, light);
    _police.draw(light);
    if (flares) {
        _police.drawFlares(*flares, eye);
        if (_night)
            _props.drawGlows(*flares, eye);
    }
}

void CityLife::headlightPools(std::vector<gfx::DynamicLight> &out, const Vec3 &eye) const {
    if (!_lampsLit)
        return;
    _traffic.headlightPools(out, eye);
    _police.headlightPools(out);
}

} // namespace reluva::age
