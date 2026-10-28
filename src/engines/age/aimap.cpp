// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Worked out from the files, which are commented text; see
// docs/formats/age/aimap.md.

#include "engines/age/aimap.h"

#include <cstdlib>
#include <map>
#include <sstream>

namespace reluva::age {

namespace {

// Each [Section]'s lines, without comments and blank lines.
std::map<std::string, std::vector<std::string>> sectionsOf(const std::string& text) {
    std::map<std::string, std::vector<std::string>> out;
    std::vector<std::string>* current = nullptr;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;
        line = line.substr(first);
        if (line[0] == '[') {
            const size_t close = line.find(']');
            current = &out[line.substr(1, close == std::string::npos ? std::string::npos : close - 1)];
            continue;
        }
        if (current)
            current->push_back(line);
    }
    return out;
}

std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string w;
    while (in >> w)
        out.push_back(w);
    return out;
}

float num(const std::vector<std::string>& w, size_t i) { return i < w.size() ? std::strtof(w[i].c_str(), nullptr) : 0; }

// A section's rows after its count line.
std::vector<std::vector<std::string>> rows(const std::vector<std::string>& lines) {
    std::vector<std::vector<std::string>> out;
    if (lines.empty())
        return out;
    const int count = std::atoi(lines[0].c_str());
    for (size_t i = 1; i < lines.size() && int(out.size()) < count; ++i)
        out.push_back(words(lines[i]));
    return out;
}

} // namespace

Error parseAiMap(const std::string& text, AiMapSettings& out) {
    const auto sections = sectionsOf(text);
    if (sections.empty())
        return Error::fail("no sections");
    auto first = [&](const char* name, float& v) {
        const auto it = sections.find(name);
        if (it != sections.end() && !it->second.empty())
            v = std::strtof(it->second[0].c_str(), nullptr);
    };
    float f = out.driveOnLeft ? 1 : 0;
    first("Ambients Drive On The Left", f);
    out.driveOnLeft = f != 0;
    f = out.laneChanges ? 1 : 0;
    first("AmbientLaneChanges", f);
    out.laneChanges = f != 0;
    first("Speed Limit", out.speedLimit);
    first("Density", out.density);
    first("CopChaseDistance", out.copChaseDistance);
    for (const auto& [name, lines] : sections) {
        if (name == "Traffic Lights" && !lines.empty()) { // two names, no count
            const auto w = words(lines[0]);
            if (!w.empty())
                out.trafficLights = {w[0], w.size() > 1 ? w[1] : w[0]};
            continue;
        }
        const auto list = rows(lines);
        if (name == "Ambient Types/Density") {
            out.ambients.clear();
            for (const auto& w : list)
                if (!w.empty())
                    out.ambients.push_back({w[0], num(w, 1)});
        } else if (name.rfind("GoodWeatherPedName", 0) == 0) {
            out.peds.clear();
            for (const auto& w : list)
                if (!w.empty())
                    out.peds.push_back({w[0], w.size() > 1 ? w[1] : w[0]});
        } else if (name == "Exceptions") {
            out.exceptions.clear();
            for (const auto& w : list)
                out.exceptions.push_back({int(num(w, 0)), num(w, 1), num(w, 2)});
        } else if (name == "Police") {
            out.police.clear();
            out.policeGiven = true;
            for (const auto& w : list) {
                if (w.size() < 5)
                    continue;
                AiMapSettings::Police p{w[0], {num(w, 1), num(w, 2), num(w, 3)}, num(w, 4), {}};
                for (size_t i = 5; i < w.size(); ++i)
                    p.rest.push_back(num(w, i));
                out.police.push_back(p);
            }
        } else if (name == "Opponent") {
            out.opponents.clear();
            out.opponentsGiven = true;
            for (const auto& w : list)
                if (w.size() >= 3)
                    out.opponents.push_back({w[0], w[1], num(w, 2)});
        }
    }
    return Error::success();
}

} // namespace reluva::age
