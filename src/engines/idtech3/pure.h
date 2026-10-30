// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/zip.h"

#include <cstdint>
#include <optional>
#include <string>

namespace reluva::idtech3 {

// The checksums id Tech 3 games tell their .pk3 files apart by
// (docs/engines/idtech3/formats/pk3.md): the MD4 of the CRC-32 of every file in
// it that isn't empty, in the central directory's order (as 32-bit
// little-endian words), its four words XORed together. A pure server's
// checksum puts its checksum feed (a number it picks for each level)
// before the CRCs.
int32_t pakChecksum(const ZipArchive& pak, std::optional<int32_t> feed = std::nullopt);

// The pure check a client answers a server's game state with (Elite
// Force's protocol 24, eliteforce-network.md): "cp", the pure checksums of
// the paks it has its client game and menus from, "@", those of the paks
// it reads from (here: every pak in `files` the server says it has, by
// their checksums in its sv_paks; all of them if it gives none), and the
// feed XOR all those XOR their count. Empty if the client game or menus
// don't come from a pak.
std::string pureCommand(const SearchSet& files, int32_t feed, const std::string& serverPaks);

} // namespace reluva::idtech3
