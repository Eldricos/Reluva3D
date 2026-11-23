// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"

namespace reluva::tpw {

/**
 * The front end, from after the opening to a world chosen: the camera's
 * path through the hall and boardroom (front-end.md), the Personnel File
 * over the globe turning (personnel-file.md), then choosing a world on it
 * (globe.md), in docs/engines/tpw/games/simcoaster/systems/. Without the
 * scene, the Personnel File alone, over black, then the first world the
 * player has open.
 * @return The world chosen, by Globe::worlds()' order; −1 if the player
 *     quit, or the window was closed (or the launcher asked for).
 */
int runFrontEnd(System &sys, const SearchSet &files);

} // namespace reluva::tpw
