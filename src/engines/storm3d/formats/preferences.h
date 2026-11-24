// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * The settings screens' values, as the game keeps them in `ARMADA.PRF`:
 * lines of numbers split by spaces, the display driver's name on a line
 * of its own. Each value is found by its line and place on it. See
 * docs/engines/storm3d/games/armada/formats/preferences.md.
 */
class Preferences {
public:
    /** The lines, from 0. */
    enum Line { kStamp, kGame, kSound, kDriver, kGraphics, kLast, kLines };

    /** As the game writes it for a new player, `driver` its display driver. */
    static Preferences defaults(std::string_view driver);
    /** Reads the file's text; lines missing keep their defaults. */
    static Preferences parse(std::string_view file, std::string_view driver);
    /** The file's text, as the game writes it (CR LF line ends). */
    std::string text() const;

    /** A value; 0 if there's none there. */
    double number(Line line, size_t field) const;
    /** Sets a value, written as C's %g writes it ("0.9", "10"). */
    void setNumber(Line line, size_t field, double value);
    /** A whole line, as the driver's name. */
    const std::string &line(Line line) const { return _lines[line]; }

private:
    std::vector<std::string> _lines = std::vector<std::string>(kLines);
};

} // namespace reluva::storm3d
