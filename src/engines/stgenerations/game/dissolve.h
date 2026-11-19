// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stgenerations/canvas.h"
#include "engines/stgenerations/game/random.h"

namespace reluva::stgenerations {

// The mission transitions' blends (the display's slots 0x64, 0x68 and 0x6C),
// on 8-bit surfaces of the same size. Amounts are in 1024ths: how much of
// each row is changed.
namespace Dissolve {

// 0x40BC05: speckles of a colour over the surface. Style 1 sets single
// pixels at random, style 2 short streaks (a sixteenth of the width) on
// random rows. Past half, the colour fills the surface and the picture
// shows through instead.
void speckle(Canvas& surface, uint8_t colour, int amount, int style, RandomSource& random);

// 0x40C852: from one picture to another. Up to half, the first with that
// share of each row's pixels taken from the second at random; past it, the
// second with the rest taken from the first. The surface may be the first
// picture, not the second.
void crossfade(Canvas& surface, const Canvas& from, const Canvas& to, int amount,
               RandomSource& random);

} // namespace Dissolve

} // namespace reluva::stgenerations

