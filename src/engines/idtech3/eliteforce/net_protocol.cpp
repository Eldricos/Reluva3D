// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/net_protocol.h"

#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/jk2/net_fields.h"

namespace reluva::idtech3::eliteforce {

namespace {

// Elite Force's commands have no key.
void writeEliteForceCommand(wire::MessageWriter &w, int32_t, const GameHost::UserCommand &from,
                            const GameHost::UserCommand &to) {
    writeUserCommand(w, from, to);
}

GameHost::UserCommand readEliteForceCommand(wire::MessageReader &r, int32_t, const GameHost::UserCommand &from) {
    return readUserCommand(r, from);
}

NetProtocol makeEliteForce() {
    NetProtocol p;
    p.gameName = "EliteForce";
    p.writeEntityDelta = writeEntityDelta;
    p.writeEntityRemoved = writeEntityRemoved;
    p.readEntityDelta = readEntityDelta;
    p.writePlayerDelta = writePlayerDelta;
    p.readPlayerDelta = readPlayerDelta;
    p.writeUserCommand = writeEliteForceCommand;
    p.readUserCommand = readEliteForceCommand;
    return p;
}

NetProtocol makeJediOutcast() {
    NetProtocol p;
    p.version = 15;
    p.code = &jk2::messageHuffman();
    p.scramble = false;
    p.keyed = true;
    // svc_mapchange is 8, so the end is 9 (jk2mp.exe's game state, 0x449568).
    p.serverEnd = 9;
    p.clientEnd = 5;
    p.compressedConnect = true;
    p.quotedInfo = false;
    p.downloadReason = true;
    p.scoreOffset = 280;
    p.entityStateSize = jk2::kNetEntityStateSize;
    p.playerStateSize = jk2::kNetPlayerStateSize;
    p.writeEntityDelta = jk2::writeEntityDelta;
    p.writeEntityRemoved = jk2::writeEntityRemoved;
    p.readEntityDelta = jk2::readEntityDelta;
    p.writePlayerDelta = jk2::writePlayerDelta;
    p.readPlayerDelta = jk2::readPlayerDelta;
    p.writeUserCommand = jk2::writeUserCommand;
    p.readUserCommand = jk2::readUserCommand;
    return p;
}

} // namespace

const NetProtocol &eliteForceProtocol() {
    static const NetProtocol kProtocol = makeEliteForce();
    return kProtocol;
}

const NetProtocol &jediOutcastProtocol() {
    static const NetProtocol kProtocol = makeJediOutcast();
    return kProtocol;
}

const NetProtocol &netProtocol(GameCode game) {
    return game == GameCode::kJediOutcast ? jediOutcastProtocol() : eliteForceProtocol();
}

void xorMessage(std::vector<uint8_t> &message, size_t start, uint8_t key, const std::string &text) {
    size_t index = 0;
    for (size_t i = start; i < message.size(); ++i) {
        if (index >= text.size() || !text[index])
            index = 0;
        auto c = index < text.size() ? uint8_t(text[index]) : uint8_t(0);
        if (c > 127 || c == '%')
            c = '.';
        key = uint8_t(key ^ (c << (i & 1)));
        ++index;
        message[i] ^= key;
    }
}

} // namespace reluva::idtech3::eliteforce
