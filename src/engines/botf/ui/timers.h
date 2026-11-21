// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <string>
#include <vector>

// The two timers' choices, as the game setup and options screens step
// through them with their arrows: none, then times (the user, in the game;
// docs/engines/botf/systems/screens.md).

namespace reluva::botf {

/** The strategic timer's choices, in seconds, 0 for none first. */
const std::vector<int> &strategicTimerChoices();
/** The tactical timer's choices, in seconds, 0 for none first. */
const std::vector<int> &tacticalTimerChoices();
/** A time as the screens show it: "01:00", "00:15". */
std::string timerText(int seconds);

/**
 * Stbof.ini keeps the strategic timer in minutes (TIMELIMIT) and the
 * tactical one in seconds (TACTIMELIMIT), 0 for none
 * (docs/engines/botf/systems/game-files.md).
 */
constexpr int kStrategicIniUnit = 60;
constexpr int kTacticalIniUnit = 1;

/**
 * The choice whose time Stbof.ini's `value` is, in `unit` seconds, or -1
 * if it's none of them.
 */
int timerChoiceFromIni(const std::string &value, const std::vector<int> &choices, int unit);
/** A choice as Stbof.ini keeps it. */
std::string timerIniValue(const std::vector<int> &choices, int choice, int unit);

} // namespace reluva::botf
