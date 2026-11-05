// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include <cstdint>
namespace reluva::boggle {
// Native seeds for ordinary play. Distinct within this process until uint32
// wraps; different seeds may still produce the same board. Not the retail RNG.
uint32_t freshClassicSeed();
}
