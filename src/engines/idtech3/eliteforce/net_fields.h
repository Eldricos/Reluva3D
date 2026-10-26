// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/wire/message.h"

#include <cstdint>
#include <vector>

namespace reluva::idtech3::eliteforce {

// How Elite Force's protocol 24 sends entity states, player states and
// user commands: as changes from earlier ones, field by field, in the
// layouts of patch 1.2's game code (docs/formats/idtech3/eliteforce-network.md).
// States are the game's bytes (an entity state's 204, a player state's
// 468); an empty "from" counts as all zeros.

constexpr int32_t kNetEntityStateSize = 204;
constexpr int32_t kNetPlayerStateSize = 468; // patch 1.2's; 1.0's is 460, which this doesn't fit
constexpr int kEntityNumberBits = 10;
constexpr int32_t kNoEntity = 1023; // ends a snapshot's entity list

// An entity's change from `from` to `to` (its number taken from `to`):
// nothing if unchanged unless `force` (new entities, baselines).
void writeEntityDelta(wire::MessageWriter& w, const std::vector<uint8_t>& from, const std::vector<uint8_t>& to,
                      bool force);
// An entity gone since the old snapshot.
void writeEntityRemoved(wire::MessageWriter& w, int32_t number);
// After its number (already read): whether it was removed; if not, `to`
// is `from` with the changes.
bool readEntityDelta(wire::MessageReader& r, int32_t number, const std::vector<uint8_t>& from,
                     std::vector<uint8_t>& to);

void writePlayerDelta(wire::MessageWriter& w, const std::vector<uint8_t>& from, const std::vector<uint8_t>& to);
void readPlayerDelta(wire::MessageReader& r, const std::vector<uint8_t>& from, std::vector<uint8_t>& to);

// A command as a change from the one before it in the same packet.
void writeUserCommand(wire::MessageWriter& w, const GameHost::UserCommand& from, const GameHost::UserCommand& to);
GameHost::UserCommand readUserCommand(wire::MessageReader& r, const GameHost::UserCommand& from);

} // namespace reluva::idtech3::eliteforce
