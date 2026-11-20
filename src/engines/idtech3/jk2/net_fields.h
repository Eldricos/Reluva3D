// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/wire/huffman.h"
#include "engines/idtech3/wire/message.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::idtech3::jk2 {

// How Jedi Outcast's protocol 15 sends entity states, player states and
// user commands: Quake III 1.32's way, field by field in the order of
// jk2mp.exe's own tables (docs/engines/idtech3/games/jk2/systems/network.md).
// States are the game's bytes (an entity state's 296, a player state's
// 1,368); an empty "from" counts as all zeros.

constexpr int32_t kNetEntityStateSize = 296;
constexpr int32_t kNetPlayerStateSize = 1368;
// 1.03's and 1.04's: 12 bytes inserted at 608 (jk2mp.exe 1.03, 0x564).
constexpr int32_t kNetPlayerStateSizePatched = 1380;

// The Huffman code of its messages: jk2mp.exe's byte counts (0x4f6310).
const wire::Huffman &messageHuffman();

void writeEntityDelta(wire::MessageWriter &w, const std::vector<uint8_t> &from, const std::vector<uint8_t> &to,
                      bool force);
void writeEntityRemoved(wire::MessageWriter &w, int32_t number);
bool readEntityDelta(wire::MessageReader &r, int32_t number, const std::vector<uint8_t> &from,
                     std::vector<uint8_t> &to);

void writePlayerDelta(wire::MessageWriter &w, const std::vector<uint8_t> &from, const std::vector<uint8_t> &to);
void readPlayerDelta(wire::MessageReader &r, const std::vector<uint8_t> &from, std::vector<uint8_t> &to);
// The same with 1.03's and 1.04's tables (modelGhoul2 wider, three player
// fields more, the player state 1,380 bytes).
void writeEntityDeltaPatched(wire::MessageWriter &w, const std::vector<uint8_t> &from, const std::vector<uint8_t> &to,
                             bool force);
bool readEntityDeltaPatched(wire::MessageReader &r, int32_t number, const std::vector<uint8_t> &from,
                            std::vector<uint8_t> &to);
void writePlayerDeltaPatched(wire::MessageWriter &w, const std::vector<uint8_t> &from, const std::vector<uint8_t> &to);
void readPlayerDeltaPatched(wire::MessageReader &r, const std::vector<uint8_t> &from, std::vector<uint8_t> &to);

// A command as a change from the one before it, its fields XORed with
// `key` (worked out alike at both ends from the level's checksum feed, the
// message acknowledged and the last server command; see hashKey).
void writeUserCommand(wire::MessageWriter &w, int32_t key, const eliteforce::GameHost::UserCommand &from,
                      const eliteforce::GameHost::UserCommand &to);
eliteforce::GameHost::UserCommand readUserCommand(wire::MessageReader &r, int32_t key,
                                                  const eliteforce::GameHost::UserCommand &from);

// A text's hash for the command key: its first `maxLength` characters,
// each signed (as the original's chars) and weighted by its place.
int32_t hashKey(const std::string &text, size_t maxLength);

} // namespace reluva::idtech3::jk2
