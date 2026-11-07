// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/zip.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

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

// Whether a pure server takes a client's answer (`words`: "cp" and the
// rest, as pureCommand makes it; a level id before the checksums, as
// Quake III's later clients send, is skipped): its client game's and
// menus' checksums those the server expects, then "@", the checksums of
// the paks it reads from (none twice, each one of the server's own,
// `serverPaks`, all with the feed), and the encoded count; six words at
// least.
bool pureAnswerValid(const std::vector<std::string>& words, int32_t cgame, int32_t ui,
                     const std::set<int32_t>& serverPaks, int32_t feed);

// Whether reading `name` from a pak counts as the level using that pak,
// for the paks a server lists (sv_referencedPaks): not for the files the
// original reads from every pak (shaders, text, configs, bot and arena
// lists, menus, levelshots) nor for the server's own game code.
bool referencesPak(const std::string& name);

} // namespace reluva::idtech3
