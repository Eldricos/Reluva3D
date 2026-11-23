// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/cpu.h"

#include <cstdint>

namespace reluva::legoisland {

/** The player turns as fast by time as it does by the frame at this many frames a second (a tester's choice). */
constexpr double kTurnFramesPerSecond = 20;
/** The longest frame a turn is worked out for, in seconds: a longer one (a load) turns as this long a one. */
constexpr double kLongestTurnFrame = 0.25;
/** Where LEGO1.DLL's turn is, from its base: the 21 bytes turnByTime looks for. */
constexpr uint32_t kTurnCodeOffset = 0x54f91;

/**
 * Makes LEGO1.DLL, loaded at `dllBase` in `cpu`'s memory, turn the player by
 * time rather than a set amount each frame (systems/running.md, Turning is
 * by the frame): its untimed turn becomes a call to a native that works it
 * out the same way, times the frame's time and kTurnFramesPerSecond.
 * @return False, changing nothing, where its code isn't as expected.
 */
bool turnByTime(x86::Cpu &cpu, uint32_t dllBase);

} // namespace reluva::legoisland
