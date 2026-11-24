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

/** A command's number from its header, if `message` is one of DirectPlay 4's (its "play" signature), whole or bare. */
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

/** The port in a message's header: where its sender takes TCP; 0 if it isn't one of DirectPlay 4's. */
uint16_t senderPort(const std::vector<uint8_t> &message);

/** DPSP_MSG_ commands besides the search's. */
enum Command : uint16_t {
    kRequestPlayerId = 0x05,
    kRequestPlayerReply = 0x07,
    kCreatePlayer = 0x08,
    kDeletePlayer = 0x0b,
    kAddForwardRequest = 0x13,
    kPing = 0x16,
    kPingReply = 0x17,
    kYouAreDead = 0x18,
    kPlayerWrapper = 0x19,
    kAddForwardReply = 0x24,
    kSuperEnumPlayersReply = 0x29,
    kAddForward = 0x2e,
    kAddForwardAck = 0x2f,
    kCreatePlayerVerify = 0x38,
};

/** DPLAYI_PLAYER_ flags in a player's record. */
enum PlayerFlags : uint32_t {
    kSystemPlayer = 1, ///< the machine's own, not a game's
    kNameServer = 2,   ///< the host's system player
    kInGroup = 4,
    kLocal = 8, ///< on the sending machine; means nothing on another
};

/** DPERR_NONEWPLAYERS: the host's answer to a join or a player it won't take. */
constexpr uint32_t kNoNewPlayers = 0x8877014a;

/**
 * A player or group as DirectPlay 4 sends it about (DPLAYI_PACKEDPLAYER,
 * DPLAYI_SUPERPACKEDPLAYER), with the Winsock service provider's data:
 * where its machine takes TCP and UDP.
 */
struct Player {
    uint32_t id = 0;
    uint32_t flags = 0; ///< PlayerFlags
    std::string shortName;
    std::string longName;
    std::vector<uint8_t> data;
    bool addresses = false; ///< whether the service provider's data is there
    net::Address stream;    ///< 0.0.0.0 for "the address it came from"
    net::Address datagram;
    uint32_t systemPlayer = 0;     ///< its machine's system player; a system player's own id
    uint32_t version = kDialect;   ///< a system player's dialect
    std::vector<uint32_t> players; ///< a group's
    uint32_t parent = 0;
};

/** DPLAYI_PACKEDPLAYER: appended to `v`; read at `at` (false if it isn't whole). */
///@{
void putPackedPlayer(std::vector<uint8_t> &v, const Player &p);
bool readPackedPlayer(const std::vector<uint8_t> &v, size_t at, Player &out);
///@}
/** DPLAYI_SUPERPACKEDPLAYER: appended to `v`; read at `at`, moved past it. */
///@{
void putSuperPackedPlayer(std::vector<uint8_t> &v, const Player &p);
bool readSuperPackedPlayer(const std::vector<uint8_t> &v, size_t &at, Player &out);
///@}

/**
 * The messages that join a machine to a session, make and destroy its
 * players and keep it there. Each built with the whole header, `port`
 * the sender's TCP port; bare() takes it off for the reliable protocol,
 * where it starts at "play". The readers take either.
 */
///@{
std::vector<uint8_t> requestPlayerId(uint16_t port, uint32_t flags);
bool readRequestPlayerId(const std::vector<uint8_t> &m, uint32_t &flags);
std::vector<uint8_t> requestPlayerReply(uint16_t port, uint32_t id, uint32_t result);
bool readRequestPlayerReply(const std::vector<uint8_t> &m, uint32_t &id, uint32_t &result);
/** @param tick the sender's milliseconds since its start */
std::vector<uint8_t> addForwardRequest(uint16_t port, uint32_t to, const Player &system, const std::string &password,
                                       uint32_t tick);
bool readAddForwardRequest(const std::vector<uint8_t> &m, Player &system, std::string &password);
std::vector<uint8_t> addForward(uint16_t port, uint32_t to, const Player &system);
bool readAddForward(const std::vector<uint8_t> &m, Player &system);
std::vector<uint8_t> addForwardAck(uint16_t port, uint32_t id);
bool readAddForwardAck(const std::vector<uint8_t> &m, uint32_t &id);
std::vector<uint8_t> addForwardReply(uint16_t port, uint32_t error);
bool readAddForwardReply(const std::vector<uint8_t> &m, uint32_t &error);
std::vector<uint8_t> createPlayer(uint16_t port, const Player &player);
bool readCreatePlayer(const std::vector<uint8_t> &m, Player &player);
std::vector<uint8_t> deletePlayer(uint16_t port, uint32_t id);
bool readDeletePlayer(const std::vector<uint8_t> &m, uint32_t &id);
std::vector<uint8_t> ping(uint16_t port, uint32_t from, uint32_t tick, bool reply);
bool readPing(const std::vector<uint8_t> &m, uint32_t &from, uint32_t &tick);
/**
 * DPSP_MSG_SUPERENUMPLAYERSREPLY: the session, its players and groups,
 * for a machine joining it.
 * @param idSeed DPSESSIONDESC2's Reserved1, which the ids are made with
 */
std::vector<uint8_t> superEnumPlayersReply(uint16_t port, const dplay::SessionInfo &session, uint32_t idSeed,
                                           const std::vector<Player> &players);
bool readSuperEnumPlayersReply(const std::vector<uint8_t> &m, dplay::SessionInfo &session, uint32_t &idSeed,
                               std::vector<Player> &players);
///@}

/**
 * A game's message between players (DPSP_MSG_PLAYERMESSAGE): the
 * header, then the players' ids unless the session has
 * DPSESSION_NOMESSAGEID, then the data; wrapped in DPSP_MSG_PLAYERWRAPPER
 * if it would look like one of DirectPlay's own.
 */
///@{
std::vector<uint8_t> playerMessage(uint16_t port, uint32_t from, uint32_t to, const std::vector<uint8_t> &data,
                                   bool ids);
bool readPlayerMessage(const std::vector<uint8_t> &m, bool ids, uint32_t &from, uint32_t &to,
                       std::vector<uint8_t> &data);
///@}

/** A message less its size, token and address, as the reliable protocol carries it. */
std::vector<uint8_t> bare(const std::vector<uint8_t> &m);
/** Whether `m` is one of DirectPlay's own, whole ("play" at 20) or bare (at 0). */
bool isSystem(const std::vector<uint8_t> &m);

/** A GUID's 16 bytes as Windows lays them out, from "{...}"; false if it isn't one. */
bool guidBytes(const std::string &text, uint8_t out[16]);
/** The "{...}" of 16 bytes laid out as Windows lays a GUID out, in lower case as Reluva writes them. */
std::string guidText(const uint8_t *bytes);

} // namespace reluva::x86::dp4
