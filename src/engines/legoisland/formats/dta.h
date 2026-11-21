// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::legoisland {

/** What an animation's model is, by its kind byte (docs/engines/legoisland/formats/dta.md). */
enum class DtaModelKind : uint8_t {
    kModel = 1,     // a model of WORLD.WDB, by name, perhaps with digits after
    kCharacter = 2, // a character: infoman, mama, a two-letter extra
    kCopy = 3,      // "*" and a model's or part's name and digits
    kCopy4 = 4,     // "*" and a name, as 3; what tells them apart isn't known
    kPlant = 5,     // a plant, a fence, a gate: a model's or part's name and digits
};

/** A model an animation uses, and where it starts. */
struct DtaModel {
    std::string name;
    uint8_t kind = 0; // a DtaModelKind
    std::array<float, 3> location{};
    std::array<float, 3> direction{};
    std::array<float, 3> up{};
    uint8_t flag = 0; // 0 or 1
};

/** One of a world's scripted animations: its SI object, and the models it moves. */
struct DtaAnimation {
    std::string name;               // 8 letters: "sjs012in"; its SI object is "<name>_RunAnim"
    uint32_t id = 0;                // that object's id in the world's SI file, from 500 (formats/dta.md)
    uint16_t spot = 0;              // 0xFFFF, or 1 to 68 in some worlds
    std::array<uint8_t, 4> flags{}; // 0 or 1; 0 or 1; a mask of 5 bits; 0 or 1 (formats/dta.md)
    std::array<float, 3> center{};  // where it plays, and how far round
    float radius = 0;
    std::vector<DtaModel> models;
};

/**
 * A world's `<world>INF.DTA`: the scripted animations in its SI file, and
 * the models each uses (docs/engines/legoisland/formats/dta.md).
 */
struct DtaFile {
    uint32_t version = 0; // 3 in every file
    std::vector<DtaAnimation> animations;

    /** Reads the file, which must end where its last animation does. */
    Error read(const std::filesystem::path &path);
    /** The same from its bytes. */
    Error parse(const std::vector<uint8_t> &data);
};

} // namespace reluva::legoisland
