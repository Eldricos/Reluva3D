// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/age/vecmath.h"

#include <string>
#include <vector>

namespace reluva::age {

// The settings for a city's computer-driven cars, people and police
// (.aimap text: city/<map>.aimap for the city, race/<city>/<race>.aimap
// and .aimap_p for each race's amateur and professional runs,
// race/<city>/roam.aimap for cruising). Sections a file leaves out keep
// the values given. See docs/formats/age/aimap.md.
struct AiMapSettings {
    struct Ambient {
        std::string model;  // geometry/<model>.pkg, tune/vehicle/<model>.aiVehicleData
        float upTo = 1;     // cumulative share: picked when a random 0-1 is below this
    };
    struct PedPair {
        std::string good, bad; // models in fine and in rainy weather
    };
    struct RoadException {
        int road = 0;
        float density = 0, speedLimit = 0;
    };
    struct Police {
        std::string model;
        Vec3 position{};
        float heading = 0;  // degrees, as waypoints'
        std::vector<float> rest; // four more values, not known yet
    };
    struct Opponent {
        std::string model, path; // the car and its .opp file
        float skill = 1;         // 0.7 to 1
    };

    float speedLimit = 15;
    bool driveOnLeft = false;
    float density = -1; // [Density] where given (cruising, some modes)
    float copChaseDistance = 0;
    bool laneChanges = false;
    std::vector<Ambient> ambients;
    std::vector<PedPair> peds;
    std::vector<RoadException> exceptions;
    std::vector<Police> police;
    std::vector<Opponent> opponents;
    bool policeGiven = false, opponentsGiven = false;
};

// Reads `text` over `out` (so a race's file can follow the city's).
Error parseAiMap(const std::string& text, AiMapSettings& out);

} // namespace reluva::age
