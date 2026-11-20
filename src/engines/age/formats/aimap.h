// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/age/vecmath.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::age {

/**
 * The settings for a city's computer-driven cars, people and police
 * (.aimap text: `city/<map>.aimap` for the city, `race/<city>/<race>.aimap`
 * and .aimap_p for each race's amateur and professional runs,
 * `race/<city>/roam.aimap` for cruising). Sections a file leaves out keep
 * the values given. See docs/engines/age/games/mm2/formats/aimap.md.
 */
struct AiMapSettings {
    /** A kind of traffic car, and how often it's picked. */
    struct Ambient {
        std::string model; // geometry/<model>.pkg, tune/vehicle/<model>.aiVehicleData
        float upTo = 1;    // cumulative share: picked when a random 0-1 is below this
    };
    /** A pedestrian's models. */
    struct PedPair {
        // models in fine and in rainy weather
        std::string good;
        std::string bad;
    };
    /** A road of the .bai road map with its own traffic density and speed limit. */
    struct RoadException {
        int road = 0;
        float density = 0;
        float speedLimit = 0;
    };
    /** A police car and where it starts. */
    struct Police {
        std::string model;
        Vec3 position{};
        float heading = 0;       // degrees, as waypoints'
        std::vector<float> rest; // four more values, not known yet
    };
    /** A race's computer-driven opponent. */
    struct Opponent {
        // the car and its .opp file
        std::string model;
        std::string path;
        float skill = 1; // 0.7 to 1
    };

    float speedLimit = 15;
    bool driveOnLeft = false;
    float density = -1; // [Density] where given (cruising, some modes)
    float copChaseDistance = 0;
    bool laneChanges = false;
    // The traffic lights' models, for narrow and for wide roads ([Traffic
    // Lights]; San Francisco's file has none, so these are its).
    std::array<std::string, 2> trafficLights{"sp_traflitsingle_f", "sp_traflitdual_f"};
    std::vector<Ambient> ambients;
    std::vector<PedPair> peds;
    std::vector<RoadException> exceptions;
    std::vector<Police> police;
    std::vector<Opponent> opponents;
    bool policeGiven = false;
    bool opponentsGiven = false;
};

/**
 * Reads `text` over `out` (so a race's file can follow the city's). Fails
 * only on text without a section.
 */
Error parseAiMap(const std::string &text, AiMapSettings &out);

} // namespace reluva::age
