// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reluva::sfc {

/**
 * The game's settings text (docs/engines/sfc/games/sfc2/formats/settings.md):
 * the `.gf` files, the campaigns' `.mct`, and a map's header. Sections in
 * brackets, named by paths (`Clock/StartingDate`); `key = value` lines,
 * values in double quotes or not; `//` starts a comment, on a line of its
 * own or after a value outside quotes. Lines before the first section
 * belong to an unnamed one. A value whose closing quote is missing runs to
 * its line's end.
 */
struct SettingsFile {
    /** One `key = value` line. */
    struct Entry {
        std::string key;
        std::string value;   // without its quotes, or its spaces if it had none
        bool quoted = false; // whether it was in double quotes
    };
    /** A section, its entries in the file's order. */
    struct Section {
        std::string name; // empty for the lines before any section
        std::vector<Entry> entries;
    };
    std::vector<Section> sections;

    /** Reads text in Windows-1252, its lines ending with CR LF or LF. */
    Error read(const std::vector<uint8_t> &data);
    /** A section by name, compared without case; nullptr if there's none. */
    const Section *section(const std::string &name) const;
    /** A value by section and key, compared without case. */
    std::optional<std::string> value(const std::string &section, const std::string &key) const;
};

/**
 * A campaign map, a `.mvm` (docs/engines/sfc/games/sfc2/formats/mvm.md): a
 * settings header that declares its classes, then a grid of hexes.
 */
struct MetaverseMap {
    /** A hex, its fields as the header's class Cell declares them. */
    struct Cell {
        int32_t economic = 0;
        float impedence = 0; // the header's spelling
        int32_t strength = 0;
        int32_t region = 0;  // an index in regions
        int32_t terrain = 0; // in terrains
        int32_t planet = 0;  // in planets
        int32_t base = 0;    // in bases
    };
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<Cell> cells; // width * height, row by row
    /** The names the header gives each index: empires, terrain, planets, bases. */
    std::vector<std::string> regions;
    std::vector<std::string> terrains;
    std::vector<std::string> planets;
    std::vector<std::string> bases;

    /** Reads a whole file: an error if it isn't a map, or its cells don't fit its size. */
    Error read(const std::vector<uint8_t> &data);
};

} // namespace reluva::sfc
