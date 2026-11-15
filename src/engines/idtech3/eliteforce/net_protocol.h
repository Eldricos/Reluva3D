// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/wire/huffman.h"
#include "engines/idtech3/wire/message.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

/**
 * What differs between the network protocols the shared server and client
 * speak: Elite Force's protocol 24
 * (docs/engines/idtech3/games/stvef/systems/network.md), and Jedi
 * Outcast's protocol 15, which is Quake III 1.32's with Raven's states
 * (docs/engines/idtech3/games/jk2/systems/network.md).
 */
struct NetProtocol {
    int32_t version = 24;
    // The messages' Huffman code (null: plain bits).
    const wire::Huffman *code = nullptr;
    // Elite Force's netchan scrambling.
    bool scramble = true;
    // Quake III 1.32's: every message XORed after its first bytes with a key
    // made from the challenge, its sequence and the last command's text;
    // a server message starts with the last client command received, not
    // its snapshot; messages end with an end marker; the game state names
    // the player; commands are keyed.
    bool keyed = false;
    // The end marker of a server message and of the game state's list (-1:
    // none, and 0 ends the list).
    int32_t serverEnd = -1;
    int32_t clientEnd = -1;
    // The connect packet's text compressed after its first 12 bytes.
    bool compressedConnect = false;
    // infoResponse's info string quoted on its line (Elite Force's), or on
    // the next (Quake III's).
    bool quotedInfo = true;
    // Put in the info string ("gamename"), if not empty.
    std::string gameName;
    // A refused download says why (Quake III's), or not (protocol 24).
    bool downloadReason = false;
    // Where a player's score is in the player state (persistant[0]).
    int32_t scoreOffset = 268;
    int32_t entityStateSize = 204;
    int32_t playerStateSize = 468;

    // The states and commands, as each game's tables have them; the key
    // is 0 when the protocol has none.
    void (*writeEntityDelta)(wire::MessageWriter &, const std::vector<uint8_t> &, const std::vector<uint8_t> &,
                             bool) = nullptr;
    void (*writeEntityRemoved)(wire::MessageWriter &, int32_t) = nullptr;
    bool (*readEntityDelta)(wire::MessageReader &, int32_t, const std::vector<uint8_t> &,
                            std::vector<uint8_t> &) = nullptr;
    void (*writePlayerDelta)(wire::MessageWriter &, const std::vector<uint8_t> &,
                             const std::vector<uint8_t> &) = nullptr;
    void (*readPlayerDelta)(wire::MessageReader &, const std::vector<uint8_t> &, std::vector<uint8_t> &) = nullptr;
    void (*writeUserCommand)(wire::MessageWriter &, int32_t key, const GameHost::UserCommand &,
                             const GameHost::UserCommand &) = nullptr;
    GameHost::UserCommand (*readUserCommand)(wire::MessageReader &, int32_t key,
                                             const GameHost::UserCommand &) = nullptr;

    wire::MessageWriter writer() const { return wire::MessageWriter(code); }
    wire::MessageReader reader(std::vector<uint8_t> bytes) const { return wire::MessageReader(std::move(bytes), code); }
};

const NetProtocol &eliteForceProtocol();
const NetProtocol &jediOutcastProtocol();
const NetProtocol &netProtocol(GameCode game);

// Quake III 1.32's XOR of a message (keyed protocols): each byte from
// `start` on with a key that starts at `key` and takes in `text`'s
// characters one by one (from its start again at its end), each shifted
// by one where the byte's place is odd; '%' and those above 127 count as
// '.'. Doing it twice undoes it. (The original counts places in the
// datagram, but its headers are of even length, so the shifts agree.)
void xorMessage(std::vector<uint8_t> &message, size_t start, uint8_t key, const std::string &text);

} // namespace reluva::idtech3::eliteforce
