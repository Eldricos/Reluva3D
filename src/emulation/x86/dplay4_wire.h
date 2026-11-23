// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// DirectPlay 4's own messages on the wire, as Microsoft publishes them
// ([MC-DPL4CS]), for finding the original's games and answering its
// searches: session enumeration only (docs/common/x86/dplay.md).

#include "emulation/x86/dplay_session.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reluva::x86::dp4 {

/** Where the original's hosts listen for searches (UDP) and its name server. */
constexpr uint16_t kEnumPort = 47624;
/** The range its players' own TCP and UDP sockets are in. */
constexpr uint16_t kFirstPort = 2300;
constexpr uint16_t kLastPort = 2400;
/** The protocol's dialect these messages say they're of: DX9VERSION, the latest. */
constexpr uint16_t kDialect = 14;

/** A search for sessions (DPSP_MSG_ENUMSESSIONS). */
struct EnumRequest {
    std::string application; ///< "{...}"; GUID_NULL for any
    uint32_t flags = 0;      ///< DPENUMSESSIONS_ flags: 1 available, 2 all, 0x40 with a password
    std::string password;
    uint16_t replyPort = 0; ///< the TCP port the answer goes to
    uint32_t replyIp = 0;   ///< and its address; 0: the one it came from
};

/** A command's number from its header, if `message` is one of DirectPlay 4's (its "play" signature). */
std::optional<uint16_t> command(const std::vector<uint8_t> &message);
/**
 * A stream's next whole message, by the size in its header: its length,
 * 0 while it isn't all there, or -1 if the stream can't be one of
 * DirectPlay 4's.
 */
int messageLength(const std::vector<uint8_t> &stream);

/** DPSP_MSG_ENUMSESSIONS, answered at `replyPort` (TCP) on the sending machine. */
std::vector<uint8_t> enumSessions(const EnumRequest &request);
/** Reads a DPSP_MSG_ENUMSESSIONS; false if it isn't one. */
bool readEnumSessions(const std::vector<uint8_t> &message, EnumRequest &out);

/**
 * DPSP_MSG_ENUMSESSIONSREPLY describing `session`, its players its count;
 * `port` is the sender's own TCP port, as its header gives it.
 * @param idSeed DPSESSIONDESC2's Reserved1, the host's own value for making ids.
 */
std::vector<uint8_t> enumSessionsReply(const dplay::SessionInfo &session, uint16_t port, uint32_t idSeed);
/** Reads a DPSP_MSG_ENUMSESSIONSREPLY; false if it isn't one. */
bool readEnumSessionsReply(const std::vector<uint8_t> &message, dplay::SessionInfo &out);

/** A GUID's 16 bytes as Windows lays them out, from "{...}"; false if it isn't one. */
bool guidBytes(const std::string &text, uint8_t out[16]);
/** The "{...}" of 16 bytes laid out as Windows lays a GUID out, in lower case as Reluva writes them. */
std::string guidText(const uint8_t *bytes);

} // namespace reluva::x86::dp4
