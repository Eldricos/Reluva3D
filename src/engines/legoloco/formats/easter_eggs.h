// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace reluva::legoloco {

/**
 * A surprise shown between two dates and times: a `[TimeEvents]` line of
 * EE.INI. The fields' names are the file's own comment's.
 */
struct TimeEvent {
    int32_t startDay = 0;
    int32_t startMonth = 0; // 0 for any
    int32_t endDay = 0;
    int32_t endMonth = 0; // 0 for any
    int32_t startHour = 0;
    int32_t startMinute = 0;
    int32_t endHour = 0;
    int32_t endMinute = 0;
    int32_t resource = 0;
    int16_t frameSet = 0;
    int32_t period = 0;
    char type = 0; // W(orld), R(andom), C(entred), P(ort), S(tarboard)
    int32_t x = 0;
    int32_t y = 0;
};

/** A `[LoadEvents]` line of EE.INI: between two dates, one resource for another. */
struct LoadEvent {
    int32_t startDay = 0;
    int32_t startMonth = 0;
    int32_t endDay = 0;
    int32_t endMonth = 0;
    int32_t resource = 0;
    int32_t replacement = 0;
};

/** EE.INI: the date-based surprises (docs/engines/legoloco/formats/ee-ini.md). */
struct EasterEggs {
    std::vector<TimeEvent> timeEvents;
    std::vector<LoadEvent> loadEvents;
};

/**
 * Reads EE.INI's text as the game does: keys 001, 002... of each section,
 * stopping at the first missing or empty one.
 */
Error parseEasterEggs(std::string_view text, EasterEggs &out);

} // namespace reluva::legoloco
