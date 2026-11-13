// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/ground_textures.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>

namespace reluva::newworlds {

namespace {

enum Role { kTop, kMiddle, kLow, kSlope, kPit, kRoles };

// Heights of the levels built into the ground models: 0, 3264 and 7360.
Role faceRole(float flatness, float z) {
    if (flatness < 0.9f)
        return z < -1000 ? kPit : kSlope;
    if (z > 5500)
        return kTop;
    if (z > 2000)
        return kMiddle;
    return z > -1000 ? kLow : kPit;
}

class Names {
public:
    explicit Names(const std::vector<std::string> &stems) : _stems(stems) {}
    bool has(const std::string &s) const { return std::find(_stems.begin(), _stems.end(), s) != _stems.end(); }
    // The first family with a texture numbered like the model, else its 01,
    // else any texture of the family.
    std::string pick(std::initializer_list<const char *> families, const std::string &number) const {
        for (const char *f : families)
            if (!number.empty() && has(f + number))
                return f + number;
        for (const char *f : families)
            if (has(std::string(f) + "01"))
                return std::string(f) + "01";
        for (const char *f : families)
            for (const std::string &s : _stems)
                if (s.rfind(f, 0) == 0)
                    return s;
        return {};
    }

private:
    const std::vector<std::string> &_stems;
};

} // namespace

std::vector<std::string> chooseGroundTextures(const std::string &modelStem, const GeoModel &model,
                                              const std::vector<std::string> &planetStems) {
    const Names names(planetStems);
    // "alpin_07" -> family "alpin", number "07".
    std::string family = modelStem;
    std::string number;
    if (const size_t u = modelStem.rfind('_'); u != std::string::npos) {
        family = modelStem.substr(0, u);
        number = modelStem.substr(u + 1);
        if (number.size() == 1)
            number = "0" + number; // needle_0
        if (!std::all_of(number.begin(), number.end(), [](unsigned char c) { return std::isdigit(c); }))
            family = modelStem, number.clear();
    }

    // Only the plain ground tiles (alpin_07, des_07, ice_07) share numbers with
    // their textures; crack_03 isn't low03.
    static const char *const kSpecial[] = {"hole",  "water", "concrete", "concact", "needle",
                                           "arche", "brid",  "crack",    "ramp",    "mount"};
    const bool plain =
        std::none_of(std::begin(kSpecial), std::end(kSpecial), [&](const char *f) { return family == f; });
    const std::string roleNumber = plain ? number : std::string();

    // Each slot's role: the one covering most of its faces' area.
    size_t slots = 0;
    for (const auto &face : model.faces)
        slots = std::max<size_t>(slots, face.corners.back().extra[4] + 1);
    std::vector<std::array<float, kRoles>> area(slots, std::array<float, kRoles>{});
    for (const auto &face : model.faces) {
        const auto &verts = model.parts[face.part].vertices;
        // Newell's method
        float nx = 0;
        float ny = 0;
        float nz = 0;
        float z = 0;
        for (size_t i = 0; i < face.corners.size(); ++i) {
            const auto &a = verts[face.corners[i].vertex];
            const auto &b = verts[face.corners[(i + 1) % face.corners.size()].vertex];
            nx += (a.y - b.y) * (a.z + b.z);
            ny += (a.z - b.z) * (a.x + b.x);
            nz += (a.x - b.x) * (a.y + b.y);
            z += a.z;
        }
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len <= 0)
            continue;
        z /= float(face.corners.size());
        area[face.corners.back().extra[4]][faceRole(std::fabs(nz) / len, z)] += len / 2;
    }

    // Flat low ground: the model's own number (low07, ice07), else the plain
    // flat texture (low07, tile 07's), else the planet's plain variant
    // (green01, sand01, blue01; the other three have patterns drawn for
    // particular tiles).
    auto low = [&] {
        for (const char *f : {"low", "ice"})
            if (!roleNumber.empty() && names.has(f + roleNumber))
                return f + roleNumber;
        if (names.has("low07"))
            return std::string("low07");
        return names.pick({"green", "sand", "blue", "low", "ice"}, "");
    };
    auto byRole = [&](Role r) -> std::string {
        switch (r) {
        case kTop:
            return names.pick({"top", "snow"}, roleNumber);
        case kMiddle:
            return names.pick({"med", "rock"}, roleNumber);
        case kLow:
            return low();
        case kPit:
        case kSlope:
        case kRoles:
            break;
        }
        return names.pick({"cliff"}, roleNumber);
    };

    std::vector<std::string> out;
    for (size_t s = 0; s < slots; ++s) {
        const auto &a = area[s];
        const Role role = Role(std::max_element(a.begin(), a.end()) - a.begin());
        const bool slope = role == kSlope || role == kPit;
        std::string pick;
        if (family == "hole" || family == "water")
            pick = names.has(family + number) ? family + number : names.pick({family.c_str()}, "");
        else if (family == "concrete" || family == "concact")
            pick = "concrete";
        else if ((family == "needle" || family == "arche") && slope)
            pick = names.pick({family.c_str()}, ""); // flat ground round them by role
        else if (family == "brid" && !slope)
            pick = names.has("brid" + number)    ? "brid" + number // icewrld has bramp01-02 instead
                   : names.has("bramp" + number) ? "bramp" + number
                                                 : names.pick({"brid", "bramp"}, "");
        else if (family == "crack" && slope)
            pick = names.pick({"crack"}, "");
        else if (family == "ramp" && slope)
            pick = names.pick({"ramp"}, "");
        if (pick.empty() || !names.has(pick))
            pick = byRole(role);
        out.push_back(pick);
    }
    return out;
}

} // namespace reluva::newworlds
