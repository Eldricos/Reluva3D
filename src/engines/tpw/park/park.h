// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "graphics/gfx3d.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A park being played: what it is given at the start (its world's fixed
 * items, its mission, its money) and what it keeps as it runs (the date,
 * the money, the visitors). See
 * docs/engines/tpw/games/simcoaster/systems/park.md.
 */
class Park {
public:
    /** A park cell is 10 units square; cell (x, y) covers x 10x to 10x + 10 and z 10y to 10y + 10. */
    static constexpr float kCellUnits = 10;
    /** How long a day lasts at the game's own pace, in milliseconds (inferred from the recording). */
    static constexpr uint32_t kDayMillis = 4000;

    struct Cell {
        int x = 0;
        int y = 0;
        bool operator==(const Cell &o) const { return x == o.x && y == o.y; }
    };

    /** What every park of a world has from the start, by its settings' FixedItemInfo. */
    struct FixedItems {
        std::vector<Cell> ticketBooths;
        std::vector<int> ticketBoothAngles; // 0 to 1023 a turn, it seems (512 for all the game's)
        std::array<Cell, 2> entrance{};     // the two cells guests come in by
        std::array<Cell, 3> busStops{};
        std::array<Cell, 2> crossingBusSide{};
        std::array<Cell, 2> crossingParkSide{};
    };

    /** The mission the park is played for (`missions/missNN.sam`). */
    struct Mission {
        int number = -1;
        int statistic = 0; // what the objective counts: 8 the visitors in the park at once
        int target = 0;
        int obstacle = 0;           // the obstacle it clears, by its object number
        std::vector<Cell> boundary; // the park's land, as clicks from the entrance (inferred)
    };

    /**
     * Sets up a new park in the world of `folder` (`levels/science`) for
     * mission `mission`: the general and the world's settings, the
     * mission's. Fails if the general settings or the mission can't be read.
     */
    Error load(const SearchSet &files, const std::string &folder, int mission);

    const std::string &folder() const { return _folder; }
    const FixedItems &fixedItems() const { return _fixed; }
    const Mission &mission() const { return _mission; }

    /** Money in the bank. */
    int cash() const { return _cash; }
    /** What a visitor pays to come in. */
    int admissionFee() const { return _admissionFee; }
    /** How many visitors are in the park. */
    int visitors() const { return _visitors; }

    /** Days since the park opened on 1 January 2000. */
    uint32_t day() const { return uint32_t(_millis / kDayMillis); }
    /** Moves the park's clock on by so many milliseconds of play. */
    void advance(uint32_t ms) { _millis += ms; }

    /** A date in the game's calendar. */
    struct Date {
        int day;   // 1 to 31
        int month; // 0 to 11
        int year;  // 2000 on
    };
    /** The date `days` after 1 January 2000. */
    static Date dateOf(uint32_t days);

    /** The middle of a cell on the ground, in the world's units (y up). */
    static gfx::Vec3 cellCentre(const Cell &cell);

private:
    std::string _folder;
    FixedItems _fixed;
    Mission _mission;
    int _cash = 0;
    int _admissionFee = 0;
    int _visitors = 0;
    uint64_t _millis = 0;
};

} // namespace reluva::tpw
