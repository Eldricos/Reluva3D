// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "engines/age/formats/aimap.h"
#include "engines/age/audio/ambient.h"
#include "engines/age/formats/bai.h"
#include "engines/age/game/gizmos.h"
#include "engines/age/game/pedestrians.h"
#include "engines/age/game/police.h"
#include "engines/age/game/props.h"
#include "engines/age/game/road_graph.h"
#include "engines/age/game/traffic.h"
#include "engines/age/game/traffic_lights.h"

#include <string>
#include <vector>

namespace reluva::age {

/**
 * A city's traffic, pedestrians and police together, as the Drive and
 * the race screen run them round the player.
 */
class CityLife {
public:
    /**
     * Reads city/<map>.bai, and city/<map>.aimap and .psdl where they are;
     * fails only on the .bai. Stops what was running.
     */
    Error load(const SearchSet &files, const std::string &map);
    /** The city's own .aimap settings (the defaults without one). */
    const AiMapSettings &citySettings() const { return _city; }
    /** The city's road map, from its .bai. */
    const AiRoadMap &roadMap() const { return _roads; }
    /** Whether a road map with roads has been loaded. */
    bool loaded() const { return !_roads.roads.empty(); }

    /** What start() puts in the city, besides the settings. */
    struct Options {
        // densities, 0..1
        float traffic = 0;
        float peds = 0;
        int cops = -1;            // how many of the settings' police (-1: all)
        bool rain = false;        // the pedestrians dressed for it
        bool laneChanges = false; // traffic changes lanes (else as the settings say)
        bool patrols = false;     // the police patrol the lanes rather than wait
        std::string variant;      // a race's own path sets ("circuit0"...), over the city's
        uint32_t seed = 1;
    };
    /**
     * Starts afresh with `settings` (the city's, or a race's read over
     * them). Returns the first problem loading models, if any.
     * Keeps a pointer to `files`, which must outlive it (or the next start()).
     */
    Error start(gfx::Renderer3D &renderer, TextureCache &textures, const SearchSet &files,
                const AiMapSettings &settings, const Options &options, audio::Mixer *mixer);
    /** Takes the traffic, pedestrians, police, gizmos and ambient sounds away. */
    void stop();
    /** As Traffic::setDensity(). */
    void setTrafficDensity(float d) { _traffic.setDensity(d); }
    /** The pedestrians' density, 0..1. */
    void setPedDensity(float d) { _peds.setDensity(d); }
    /**
     * Evening, night, fog or rain: the cars' headlights on; `night`: the
     * traffic lights' night glows.
     */
    void setLamps(bool lit, bool night);

    /**
     * Moves everyone round the player; `others` are the other driven cars
     * (opponents). `fill` places the traffic and pedestrians anywhere
     * round the player rather than out of sight (just started). The
     * drawbridges' leaves go into `world` as they move.
     */
    void update(float dt, CarSim &player, const std::vector<CarSim *> &others, CollisionWorld &world,
                const RoadGraph &roads, bool fill = false);
    /**
     * Draws the props, traffic lights, gizmos, traffic, pedestrians and
     * police. The first draw makes the props' shapes from their models, for
     * update() to put in the world.
     */
    void draw(gfx::Renderer3D &renderer, ModelCache &models, const SceneLight &light, const gfx::Mat4 &viewProj,
              const Vec3 &eye, const FlareDrawer *flares = nullptr);
    /** Where the traffic's and the police's headlights light the road; none unless the lamps are lit. */
    void headlightPools(std::vector<gfx::DynamicLight> &out, const Vec3 &eye) const;

    /**
     * How many times the player has run into a car (the traffic's or the
     * police's) since starting.
     */
    int carTouches() const { return _carTouches; }

    /** The ambient cars and the parked ones. */
    Traffic &traffic() { return _traffic; }
    /** The people on the sidewalks. */
    Pedestrians &pedestrians() { return _peds; }
    /** The police cars, waiting or patrolling. */
    Police &police() { return _police; }
    /** The traffic lights at the road map's junctions. */
    const TrafficLights &lights() const { return _lights; }
    /** The boats, drawbridges and trains that move along the city's path sets. */
    const Gizmos &gizmos() const { return _gizmos; }
    /** The city's ambient sounds (aud/ambient). */
    const AmbientSounds &ambient() const { return _ambient; }
    /** The ambient sounds louder or quieter (the Audio Options' ambient: 0 off). */
    void setAmbientGain(float gain) { _ambient.setGain(gain); }
    /**
     * The props (city/<map>/props.pathset) and traffic lights, which cars
     * can knock down.
     */
    const Props &props() const { return _props; }
    /** What flew out of the props knocked down since last asked. */
    std::vector<Props::Burst> takeBursts() { return _props.takeBursts(); }

private:
    AiRoadMap _roads;
    std::vector<uint8_t> _blockRules; // each PSDL block's street furniture rule (0 none)
    std::string _map;
    AiMapSettings _city;
    AiMapSettings _current;
    Traffic _traffic;
    Pedestrians _peds;
    Police _police;
    TrafficLights _lights;
    Gizmos _gizmos;
    AmbientSounds _ambient;
    bool _lampsLit = false;
    bool _night = false;
    int _carTouches = 0;
    Props _props;
    const SearchSet *_files = nullptr;
    bool _propsInWorld = false;
};

} // namespace reluva::age
