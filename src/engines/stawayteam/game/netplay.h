// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Away Team over a network: Reluva's own protocol over UDP, in place of the
// program's DirectPlay sessions over IPX or TCP/IP, which a Reluva copy
// can't speak (docs/engines/stawayteam/systems/network.md). Two players: the
// one who made the game keeps the lobby (its players, the mission, the chat
// and the start's countdown) and sends it to the other as it changes.

#include "common/net.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stawayteam::netplay {

/** The UDP port games are made on and looked for at (Reluva's). */
constexpr uint16_t kDefaultPort = 23418;
/** How many play a network game: the program keeps two players (0x72e738). */
constexpr int kPlayers = 2;
/** The longest name a player can type: its field takes 16 (0x511aae). */
constexpr size_t kMostName = 16;
/** The start's countdown, in ms (0x515c6c). */
constexpr uint64_t kCountdownMs = 5000;

/** A game found on the local network. */
struct Found {
    net::Address address;
    std::string gameName; // "<host>'s Game" (0x640e10)
    int players = 0;
    bool open = false; // not full or started: it can be joined
};

/** A line of the lobby's chat: who said it (their place in players()) and what. */
struct ChatLine {
    int from = 0;
    std::string text;
};

/**
 * This machine's part in a network game: making it, joining it or looking
 * for one, then its lobby. Nothing happens but in update(), which the
 * caller runs often; it isn't for several threads.
 */
class Session {
public:
    /** Making a game on `socket` (bound, as a rule, to kDefaultPort), named for `playerName`. */
    static std::unique_ptr<Session> host(std::unique_ptr<net::UdpSocket> socket, const std::string &playerName);
    /** Joining the game at `address`. */
    static std::unique_ptr<Session> join(std::unique_ptr<net::UdpSocket> socket, const net::Address &address,
                                         const std::string &playerName);
    /** Looking for games: asks at each address (broadcasts) and lists those that answer in found(). */
    static std::unique_ptr<Session> browse(std::unique_ptr<net::UdpSocket> socket, std::vector<net::Address> askAt);

    /**
     * Where it is: looking, joining, in the lobby, the countdown run out,
     * refused by the game (or no answer), or cut off from it.
     */
    enum class State { kBrowsing, kConnecting, kLobby, kStarted, kRefused, kLost };
    State state() const { return _state; }
    bool isHost() const { return _host; }
    /** This machine's place in players(): the host 0. */
    int me() const { return _me; }

    /** Browsing: the games that answered. */
    const std::vector<Found> &found() const { return _found; }
    /** Browsing: forgets the games found and asks again. */
    void refresh();

    /** The lobby: its players' names, the host's first. */
    const std::vector<std::string> &players() const { return _players; }
    /** The mission chosen (1 to 18); 0 for none yet. */
    int mission() const { return _mission; }
    /** The host chooses the mission; that stops a countdown. */
    void chooseMission(int number);
    /** Says `text` to the lobby. */
    void chat(const std::string &text);
    /** The next line of the chat not taken yet; false when there's none. */
    bool nextChat(ChatLine &line);
    /** The host starts the countdown to the mission, if one's chosen. */
    void start();
    /** Whether the countdown's running, and the ms it has left. */
    ///@{
    bool counting() const { return _countdownEnd != 0; }
    uint64_t countdownLeft() const { return _countdownEnd > _now ? _countdownEnd - _now : 0; }
    ///@}

    /** Sends and receives what's due. */
    void update(uint64_t nowMs);
    /** Tells the other it's gone; the session is kLost. */
    void leave();

    /** How long the other may stay silent before it's taken to have gone, and how often each speaks, in ms. */
    ///@{
    static constexpr uint64_t kTimeoutMs = 10000;
    static constexpr uint64_t kSendMs = 250;
    static constexpr uint64_t kJoinTimeoutMs = 5000;
    ///@}

private:
    Session() = default;
    void received(const net::Address &from, const std::vector<uint8_t> &data);
    void sendState();
    void stopCountdown();

    std::unique_ptr<net::UdpSocket> _socket;
    State _state = State::kBrowsing;
    bool _host = false;
    int _me = 0;
    uint64_t _now = 0;
    uint64_t _lastSent = 0;
    uint64_t _heard = 0; // when the other was last heard from; 0 before
    bool _changed = false;

    std::vector<net::Address> _askAt;
    int _asksLeft = 0; // asked again in case they're lost
    std::vector<Found> _found;

    net::Address _peer; // the host, or the player who joined
    bool _hasPeer = false;
    uint32_t _nonce = 0; // the join's, so an answer to an old one isn't taken
    std::vector<std::string> _players;
    int _mission = 0;
    uint64_t _countdownEnd = 0;

    // The host's: the whole chat; each line's number its place in it plus
    // one. The player's: those it has had, and its own not yet taken in.
    std::vector<ChatLine> _log;
    uint32_t _linesHad = 0;
    std::deque<std::string> _toSend; // the first numbered _taken + 1
    uint32_t _taken = 0;             // the player's lines the host has taken in
    uint32_t _peerSent = 0;          // the host's: of the player's, it has taken so many
    std::deque<ChatLine> _inbox;
};

} // namespace reluva::stawayteam::netplay
