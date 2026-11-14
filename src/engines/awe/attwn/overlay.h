// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/ui/game_overlay.h"

#include <memory>

// And Then There Were None's own windows over its scenes, together: the
// journal at the right edge (journal.h), the inventory at the left
// (inventory.h).

namespace reluva::awe::attwn {

/** The game's windows, as GameInfo::overlay makes them. */
std::unique_ptr<GameOverlay> makeOverlay(OverlayHost &host);

} // namespace reluva::awe::attwn
