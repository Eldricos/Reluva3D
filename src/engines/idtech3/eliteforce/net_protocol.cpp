// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/net_protocol.h"

#include "engines/idtech3/eliteforce/game_info.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/jk2/game_info.h"
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
    p.masterServers = masterServers;
    p.writeEntityDelta = writeEntityDelta;
    p.writeEntityRemoved = writeEntityRemoved;
    p.readEntityDelta = readEntityDelta;
    p.writePlayerDelta = writePlayerDelta;
    p.readPlayerDelta = readPlayerDelta;
    p.writeUserCommand = writeEliteForceCommand;
    p.readUserCommand = readEliteForceCommand;
    return p;
}

NetProtocol makeJediOutcast(JediRelease release) {
    NetProtocol p;
    // 1.03 kept 15 though its tables changed; 1.04's is 16.
    p.version = release == JediRelease::k104 ? 16 : 15;
    p.code = &jk2::messageHuffman();
    p.scramble = false;
    p.keyed = true;
    // svc_mapchange is 8, so the end is 9 (jk2mp.exe's game state, 0x449568).
    p.serverEnd = 9;
    p.clientEnd = 5;
    p.compressedConnect = true;
    p.quotedInfo = false;
    p.downloadReason = true;
    // jk2mp.exe's: sv_master1 masterjk2.ravensoft.com (gone; the
    // community's follow), port 28060 when a name has none (0x44e52c).
    p.masterServers = jk2::masterServers;
    p.masterPort = 28060;
    p.quakeHeartbeat = true;
    p.scoreOffset = 280;
    p.entityStateSize = jk2::kNetEntityStateSize;
    const bool patched = release != JediRelease::k102a;
    p.jediPatchedInfo = patched;
    p.playerStateSize = patched ? jk2::kNetPlayerStateSizePatched : jk2::kNetPlayerStateSize;
    p.writeEntityDelta = patched ? jk2::writeEntityDeltaPatched : jk2::writeEntityDelta;
    p.writeEntityRemoved = jk2::writeEntityRemoved;
    p.readEntityDelta = patched ? jk2::readEntityDeltaPatched : jk2::readEntityDelta;
    p.writePlayerDelta = patched ? jk2::writePlayerDeltaPatched : jk2::writePlayerDelta;
    p.readPlayerDelta = patched ? jk2::readPlayerDeltaPatched : jk2::readPlayerDelta;
    p.writeUserCommand = jk2::writeUserCommand;
    p.readUserCommand = jk2::readUserCommand;
    return p;
}

} // namespace

const NetProtocol &eliteForceProtocol() {
    static const NetProtocol kProtocol = makeEliteForce();
    return kProtocol;
}

const NetProtocol &jediOutcastProtocol(JediRelease release) {
    static const NetProtocol k102a = makeJediOutcast(JediRelease::k102a);
    static const NetProtocol k103 = makeJediOutcast(JediRelease::k103);
    static const NetProtocol k104 = makeJediOutcast(JediRelease::k104);
    return release == JediRelease::k104 ? k104 : release == JediRelease::k103 ? k103 : k102a;
}

const NetProtocol &netProtocol(GameCode game, JediRelease release) {
    return game == GameCode::kJediOutcast ? jediOutcastProtocol(release) : eliteForceProtocol();
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
