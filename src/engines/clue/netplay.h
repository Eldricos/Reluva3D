// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Clue over a network: Reluva's own protocol over UDP (the original's was
// DirectPlay's, and its Internet Game Zone is gone; see
// docs/engines/clue.md). A host and up to five others: the host keeps the
// lobby, relays chat, and puts the players' actions in one order, which
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

constexpr uint16_t kDefaultPort = 23398;
constexpr int kSeats = 6;

// A message, as bytes.
using Bytes = std::vector<uint8_t>;

// Messages sent whole, in order, over UDP to one other machine: each one
// numbered and sent again until it's acknowledged.
class Channel {
public:
    explicit Channel(net::Address peer = {}) : peer_(peer) {}
    const net::Address& peer() const { return peer_; }

    void send(Bytes message) { outgoing_.push_back({nextSend_++, std::move(message), 0}); }
    // A datagram of this channel's (Data or Ack) arrived: the messages it
    // completes, in order, are added to `out`.
    void received(const Bytes& datagram, uint64_t nowMs, std::vector<Bytes>& out);
    // Sends what is due: new messages, those not acknowledged in time,
    // and now and then an acknowledgement, to keep the connection alive.
    void update(uint64_t nowMs, net::UdpSocket& socket);
    // Nothing heard from the other machine for this long: it has gone.
    bool timedOut(uint64_t nowMs) const { return lastHeard_ != 0 && nowMs - lastHeard_ > kTimeoutMs; }
    void heard(uint64_t nowMs) { lastHeard_ = nowMs; }

    static constexpr uint64_t kResendMs = 250, kKeepAliveMs = 1000, kTimeoutMs = 15000;

private:
    struct Pending {
        uint32_t sequence;
        Bytes message;
        uint64_t sentAt;
    };
    net::Address peer_;
    std::deque<Pending> outgoing_;
    std::map<uint32_t, Bytes> early_; // arrived out of order
    uint32_t nextSend_ = 1, nextReceive_ = 1;
    uint64_t lastSent_ = 0, lastHeard_ = 0;
    bool ackDue_ = false;
};

// Who plays each suspect.
struct Seat {
    enum Kind : uint8_t { None, Human, Computer } kind = None;
    uint8_t level = 1; // a computer's: 0 novice, 1 normal, 2 expert
    int8_t owner = -1; // a human's machine: 0 the host, then each that joined
    std::string name;
};

struct Lobby {
    std::array<Seat, kSeats> seats{};
    std::string gameName;
    uint8_t maxHumans = 6;
    bool noDieRoll = false;
    bool started = false;
};

// A player's action, as every machine plays it out.
struct Command {
    enum Type : uint8_t { Roll, Walk, Passage, Suggest, Accuse, EndTurn, Show, TakeOver };
    uint8_t type = Roll;
    uint8_t seat = 0;
    std::vector<int16_t> args; // Walk: room or x, y for each step; Suggest/Accuse: suspect, weapon(, room); Show: card
};

// A game found on the local network.
struct Found {
    net::Address address;
    std::string gameName, hostName;
    int humans = 0, maxHumans = 0;
    bool started = false;
};

class Session {
public:
    // Hosting: listening on `port` (0 for kDefaultPort).
    static std::unique_ptr<Session> host(std::unique_ptr<net::UdpSocket> socket, const std::string& gameName,
                                         const std::string& playerName, int maxHumans);
    // Joining the game at `address`.
    static std::unique_ptr<Session> join(std::unique_ptr<net::UdpSocket> socket, const net::Address& address,
                                         const std::string& playerName);
    // Looking for games on the local network: asks at each address
    // (broadcasts) and lists those that answer in found().
    static std::unique_ptr<Session> browse(std::unique_ptr<net::UdpSocket> socket,
                                           const std::vector<net::Address>& askAt);

    enum class State { Browsing, Connecting, Lobby, Playing, Refused, Lost };
    State state() const { return state_; }
    bool isHost() const { return host_; }
    int me() const { return me_; } // this machine: 0 the host
    const std::string& refusal() const { return refusal_; }
    const Lobby& lobby() const { return lobby_; }
    const std::vector<Found>& found() const { return found_; }
    void refresh(); // browsing: ask again

    void update(uint64_t nowMs);

    // The lobby: talk; take a suspect (or -1 to give it back); the host
    // also sets the suspects nobody has taken, the variant, and starts.
    void chat(const std::string& text);
    bool nextChat(std::string& line);
    void take(int seat);
    void setSeat(int seat, Seat::Kind kind, int level);
    void setNoDieRoll(bool on);
    bool canStart() const;
    void start(uint32_t seed);
    uint32_t seed() const { return seed_; }

    // The game: an action of one of this machine's players, for the host
    // to put in order; and each action in that order, as it comes.
    void submit(const Command& command);
    bool nextCommand(Command& command);
    // A human's machine gone (they're given to the computer, by the host's
    // TakeOver); -1 when none.
    int nextLeft();
    void leave();

private:
    Session() = default;
    void receive(uint64_t nowMs);
    void handle(int from, const Bytes& message);
    void sendTo(int peer, const Bytes& message);
    void broadcast(const Bytes& message);
    void sendLobby();
    void dropPeer(int peer);

    std::unique_ptr<net::UdpSocket> socket_;
    State state_ = State::Lobby;
    bool host_ = false;
    int me_ = 0;
    std::string myName_, refusal_;
    Lobby lobby_;
    uint32_t seed_ = 0;
    // The host: each machine that joined, by its number; a player: the host's, as 0.
    std::map<int, Channel> peers_;
    int nextPeer_ = 1;
    net::Address hostAddress_;
    uint64_t now_ = 0, askedAt_ = 0, connectStart_ = 0;
    std::vector<net::Address> askAt_;
    std::vector<Found> found_;
    std::deque<std::string> chat_;
    std::deque<Command> commands_;
    std::deque<int> left_;
};

// Messages as bytes (exposed for tests).
Bytes encodeCommand(const Command& c);
bool decodeCommand(const Bytes& b, size_t& at, Command& c);

} // namespace reluva::clue::netplay
