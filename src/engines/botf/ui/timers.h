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

} // namespace reluva::botf
