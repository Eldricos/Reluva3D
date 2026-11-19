// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Clue over a network: Reluva's own protocol over UDP (the original's was
// DirectPlay's, and its Internet Game Zone is gone; see
// docs/engines/clue/systems/network.md). A host and up to five others: the
// host keeps the lobby, relays chat, and puts the players' actions in one order, which
// every machine then plays out the same, from the same deal.

#include "common/net.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::clue::netplay {

/** The UDP port games are hosted on and looked for at. */
constexpr uint16_t kDefaultPort = 23398;
/** How many seats a game has: one a suspect. */
constexpr int kSeats = 6;

/** A message, as bytes. */
using Bytes = std::vector<uint8_t>;

/**
 * Messages sent whole, in order, over UDP to one other machine: each one
 * numbered and sent again until it's acknowledged.
 */
class Channel {
public:
    /** A channel to `peer`. */
    explicit Channel(net::Address peer = {}) : _peer(peer) {}
    /** The other machine's address. */
    const net::Address &peer() const { return _peer; }

    /** Queues a message, sent by the next update(). */
    void send(Bytes message) { _outgoing.push_back({_nextSend++, std::move(message), 0}); }
    /**
     * A datagram of this channel's (Data or Ack) arrived: the messages it
     * completes, in order, are added to `out`.
     */
    void received(const Bytes &datagram, uint64_t nowMs, std::vector<Bytes> &out);
    /**
     * Sends what is due: new messages, those not acknowledged in time,
     * and now and then an acknowledgement, to keep the connection alive.
     */
    void update(uint64_t nowMs, net::UdpSocket &socket);
    /** Whether nothing has been heard from the other machine for kTimeoutMs: it has gone. */
    bool timedOut(uint64_t nowMs) const { return _lastHeard != 0 && nowMs - _lastHeard > kTimeoutMs; }
    /** Notes that the other machine was heard from at `nowMs`. */
    void heard(uint64_t nowMs) { _lastHeard = nowMs; }

    /** When a message is sent again, an acknowledgement sent to keep alive, and the other given up on, in ms. */
    ///@{
    static constexpr uint64_t kResendMs = 250;
    static constexpr uint64_t kKeepAliveMs = 1000;
    static constexpr uint64_t kTimeoutMs = 15000;
    ///@}

private:
    struct Pending {
        uint32_t sequence;
        Bytes message;
        uint64_t sentAt;
    };
    net::Address _peer;
    std::deque<Pending> _outgoing;
    std::map<uint32_t, Bytes> _early; // arrived out of order
    uint32_t _nextSend = 1;
    uint32_t _nextReceive = 1;
    uint64_t _lastSent = 0;
    uint64_t _lastHeard = 0;
    bool _ackDue = false;
};

/** Who plays a suspect. */
struct Seat {
    /** Nobody, a person or the computer. */
    enum Kind : uint8_t { kNone, kHuman, kComputer } kind = kNone;
    uint8_t level = 1; // a computer's: 0 novice, 1 normal, 2 expert
    int8_t owner = -1; // a human's machine: 0 the host, then each that joined
    std::string name;
};

/** The game being set up: its seats, its name and its rules. */
struct Lobby {
    std::array<Seat, kSeats> seats{};
    std::string gameName;
    uint8_t maxHumans = 6;
    bool noDieRoll = false;
    bool started = false;
};

/** A player's action, as every machine plays it out. */
struct Command {
    /** What the action is. */
    enum Type : uint8_t { kRoll, kWalk, kPassage, kSuggest, kAccuse, kEndTurn, kShow, kTakeOver, kRoomWalk };
    uint8_t type = kRoll;
    uint8_t seat = 0;
    // Walk: room or x, y for each step; Suggest/Accuse: suspect, weapon(,
    // room); Show: card; RoomWalk: the world x, y walked to in the suspect's
    // room
    std::vector<int16_t> args;
};

/** A game found on the local network. */
struct Found {
    net::Address address;
    std::string gameName;
    std::string hostName;
    int humans = 0;
    int maxHumans = 0;
    bool started = false;
};

/**
 * This machine's part in a network game: hosting it, joining it or
 * looking for one, then the lobby and the game. Nothing happens but in
 * update(), which the caller runs often; it isn't for several threads.
 */
class Session {
public:
    /**
     * Hosting on `socket` (bound, as a rule, to kDefaultPort), for up to
     * `maxHumans` people (2-6), this one included.
     */
    static std::unique_ptr<Session> host(std::unique_ptr<net::UdpSocket> socket, const std::string &gameName,
                                         const std::string &playerName, int maxHumans);
    /** Joining the game at `address`. */
    static std::unique_ptr<Session> join(std::unique_ptr<net::UdpSocket> socket, const net::Address &address,
                                         const std::string &playerName);
    /**
     * Looking for games on the local network: asks at each address
     * (broadcasts) and lists those that answer in found().
     */
    static std::unique_ptr<Session> browse(std::unique_ptr<net::UdpSocket> socket,
                                           const std::vector<net::Address> &askAt);

    /** Where the session is: browsing, joining, in the lobby, playing, refused by the host, or cut off. */
    enum class State { kBrowsing, kConnecting, kLobby, kPlaying, kRefused, kLost };
    /** Where the session is. */
    State state() const { return _state; }
    /** Whether this machine hosts the game. */
    bool isHost() const { return _host; }
    /** This machine's number: 0 the host. */
    int me() const { return _me; }
    /** Why this machine couldn't join (refused, or no answer), once kRefused. */
    const std::string &refusal() const { return _refusal; }
    /** The game's lobby, as the host last sent it. */
    const Lobby &lobby() const { return _lobby; }
    /** Browsing: the games that answered. */
    const std::vector<Found> &found() const { return _found; }
    /** Browsing: forgets the games found and asks again. */
    void refresh();

    /** Sends and receives what is due. */
    void update(uint64_t nowMs);

    /**
     * The lobby: talk; take a suspect (or -1 to give it back); the host
     * also sets the suspects nobody has taken, the variant, and starts.
     * Only the host's calls set seats, the variant or start; a player's
     * take() asks the host.
     */
    ///@{
    void chat(const std::string &text);
    bool nextChat(std::string &line);
    void take(int seat);
    void setSeat(int seat, Seat::Kind kind, int level);
    void setNoDieRoll(bool on);
    ///@}
    /** Whether the host can start: three suspects played at least, one by every machine that joined. */
    bool canStart() const;
    /** The host starts the game, dealt from `seed`, if canStart(). */
    void start(uint32_t seed);
    /** The seed the game was started with. */
    uint32_t seed() const { return _seed; }

    /**
     * The game: an action of one of this machine's players, for the host
     * to put in order; and each action in that order, as it comes.
     */
    ///@{
    void submit(const Command &command);
    bool nextCommand(Command &command);
    ///@}
    /**
     * The seat of a person whose machine has gone (given to the computer,
     * by the host's TakeOver); -1 when none.
     */
    int nextLeft();
    /** Says goodbye to the others and closes the socket; the session is kLost. */
    void leave();

private:
    Session() = default;
    void receive(uint64_t nowMs);
    void handle(int from, const Bytes &message);
    void sendTo(int peer, const Bytes &message);
    void broadcast(const Bytes &message);
    void sendLobby();
    void dropPeer(int peer);

    std::unique_ptr<net::UdpSocket> _socket;
    State _state = State::kLobby;
    bool _host = false;
    int _me = 0;
    std::string _myName;
    std::string _refusal;
    Lobby _lobby;
    uint32_t _seed = 0;
    // The host: each machine that joined, by its number; a player: the host's, as 0.
    std::map<int, Channel> _peers;
    int _nextPeer = 1;
    net::Address _hostAddress;
    uint64_t _now = 0;
    uint64_t _askedAt = 0;
    uint64_t _connectStart = 0;
    std::vector<net::Address> _askAt;
    std::vector<Found> _found;
    std::deque<std::string> _chat;
    std::deque<Command> _commands;
    std::deque<int> _left;
};

/** A command as bytes, and back from `at` (moved past it; false if damaged). Exposed for tests. */
///@{
Bytes encodeCommand(const Command &c);
bool decodeCommand(const Bytes &b, size_t &at, Command &c);
///@}

} // namespace reluva::clue::netplay
