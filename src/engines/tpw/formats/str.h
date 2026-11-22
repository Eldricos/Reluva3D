// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * The game's own character set (`MBToUni.dat`, "BFMU"): what each byte of
 * its text is in Unicode. See docs/engines/tpw/games/simcoaster/formats/str.md.
 */
class CharacterSet {
public:
    /** Reads the table. Fails without "BFMU", or if it's cut short. */
    Error parse(const std::vector<uint8_t> &data);

    /** How many codes it has; codes run from 1. */
    size_t size() const { return _unicode.size(); }
    /** A code's character, or 0 for a code it hasn't (0 among them). */
    char32_t unicode(uint8_t code) const { return code > 0 && code <= _unicode.size() ? _unicode[code - 1] : 0; }
    /** Text in the game's codes as UTF-8; a code it hasn't is U+FFFD. */
    std::string toUtf8(const uint8_t *codes, size_t count) const;

private:
    std::vector<char32_t> _unicode;
};

/**
 * A table of the game's text (`.str`, "BFST"), as SimCoaster's
 * `Data/Language/American/UITEXT.str`: strings by number, each text with
 * places for parameters.
 */
class StringTable {
public:
    /** A piece of a string: text, or the place of a parameter. */
    struct Part {
        bool parameter = false;
        int number = 0;   // the parameter's, from 0
        std::string text; // UTF-8
    };

    /** Reads the table. Fails without "BFST", or on a string past its end or not ended. */
    Error parse(const std::vector<uint8_t> &data, const CharacterSet &characters);

    /** The table's number (1000 for MONTHS.str, 1007 for UITEXT.str). */
    uint32_t id() const { return _id; }
    /** How many strings it has. */
    size_t size() const { return _strings.size(); }
    /** A string's pieces; empty for a number past the end. */
    const std::vector<Part> &parts(size_t index) const;
    /** A string as text, each parameter's place written "{n}". */
    std::string text(size_t index) const;

private:
    uint32_t _id = 0;
    std::vector<std::vector<Part>> _strings;
};

} // namespace reluva::tpw
