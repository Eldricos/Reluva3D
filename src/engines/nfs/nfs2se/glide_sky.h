// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/nfs2se/formats/hrz.h"

#include <array>

namespace reluva::nfs::nfs2se {

/**
 * The 3dfx program's table fog for a view (NFS2SEA.EXE 0x42b284,
 * 0x427690): Glide's exponential fog, 1 - e^(-density × depth), the depth
 * along the view in track units (docs/engines/nfs/games/nfs2se/formats/hrz.md,
 * The 3dfx fog).
 */
struct GlideFog {
    float density = 0;
    Rgb colour{};
};

/**
 * The main view's fog on track `track` (the NN of trNN) with its camera at
 * the virtual road's point `roadPoint`: one of the track's 20 tables and
 * its colour, by stretches of the road the program lists.
 */
GlideFog glideFog(int track, int roadPoint);

/**
 * The 17 colours round the 3dfx sky's ring at its midpoint row (0x42d0a3):
 * from the sun's colour at the file's angle toward the other's and back,
 * the far side 7/8 of the way; the 17th the first again.
 */
std::array<Rgb, 17> skyRingColours(const Sky &sky);

} // namespace reluva::nfs::nfs2se
