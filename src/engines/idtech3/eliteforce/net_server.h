// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/wire/message.h"
#include "engines/idtech3/wire/netchan.h"

#include "common/net.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// The server's side of Elite Force's network play (protocol 24, as the
// retail game patched to 1.2 speaks it: eliteforce-network.md): players
// on other machines find it (getinfo, getstatus), join it (getchallenge,
// connect) and play, each with a connection of their own. It drives the
// GameHost the page runs: their commands go to the game as they come,
// and after each of the game's frames they're sent what changed
// (configstrings, the game's commands to them) and, at their snapshot
// rate, snapshots as changes from the last they had.
//
// The page's own player isn't one of them: they keep their slot (or
// slots) in the game directly.
class NetServer {
public:
    NetServer(GameHost& game, CvarStore& cvars, std::unique_ptr<reluva::net::UdpSocket> socket);

    // Slots the network mustn't give out (the page's own player).
    void reserve(int32_t clientNum) { reserved_.insert(clientNum); }
    // The level has (re)started: baselines and configstrings taken anew,
    // and every player waits for the new game state.
    void levelStarted();
    // Reads and answers everything that has come; `now` in ms.
    Error receive(uint64_t now);
    // After a frame of the game's: what changed, and snapshots to those
    // due one; players not heard from for sv_timeout seconds are dropped.
    Error frame(uint64_t now);

    uint16_t port() const { return socket_->port(); }
    // The players connected over the network: their slot and name.
    std::vector<std::pair<int32_t, std::string>> players() const;
    std::function<void(const std::string&)> print;

    static constexpr int kPacketBackup = 32;
    static constexpr int kMaxReliable = 64;

private:
    enum class State { Free, Zombie, Connected, Primed, Active };
    struct Frame {
        int32_t message = -1; // the netchan sequence it went out with
        uint64_t sentMs = 0;
        std::vector<uint8_t> playerState;
        std::map<int32_t, std::vector<uint8_t>> entities; // by number
        int32_t serverTime = 0;
    };
    struct Client {
        State state = State::Free;
        int32_t number = -1;
        reluva::net::Address address;
        uint16_t qport = 0;
        wire::Netchan chan{false, 0};
        std::string userinfo, name;
        int snapMs = 50;
        uint64_t lastReceived = 0, zombieSince = 0;
        int32_t nextSnapshotTime = 0;
        // The commands for it, by sequence & 63, from reliableAcknowledge + 1
        // to reliableSequence.
        std::array<std::string, kMaxReliable> reliable;
        int32_t reliableSequence = 0, reliableAcknowledge = 0;
        int32_t lastClientCommand = 0;  // the last of its commands run
        int32_t messageAcknowledge = 0; // the last of our messages it has
        int32_t deltaMessage = -1;      // what to make the next snapshot a change from (-1: nothing)
        int32_t gamestateMessage = -1;
        uint64_t gamestateMs = 0;
        GameHost::UserCommand lastCommand;
        int ping = 0;
        std::array<Frame, kPacketBackup> frames;
    };

    void outOfBand(const reluva::net::Address& from, const std::string& text, uint64_t now);
    void sendText(const reluva::net::Address& to, const std::string& text);
    void connect(const reluva::net::Address& from, const std::string& userinfo, uint64_t now);
    std::string infoString(const std::string& challenge) const;
    Error packet(Client& c, const std::vector<uint8_t>& datagram, uint64_t now);
    Error clientCommand(Client& c, const std::string& text);
    void addReliable(Client& c, const std::string& text);
    void writeReliable(Client& c, wire::MessageWriter& w);
    void sendGamestate(Client& c, uint64_t now);
    void sendSnapshot(Client& c, uint64_t now);
    void transmit(Client& c, const wire::MessageWriter& w);
    void drop(Client& c, const std::string& reason, uint64_t now);
    void applyUserinfo(Client& c);
    Client* clientFor(const reluva::net::Address& from, uint16_t qport);

    GameHost& game_;
    CvarStore& cvars_;
    std::unique_ptr<reluva::net::UdpSocket> socket_;
    std::vector<Client> clients_;
    std::set<int32_t> reserved_;
    struct Challenge {
        int32_t value = 0;
        uint64_t time = 0;
    };
    std::map<reluva::net::Address, Challenge> challenges_;
    std::map<int32_t, std::vector<uint8_t>> baselines_;
    std::map<int32_t, std::string> configstrings_; // as last sent
    size_t serverCommandsSeen_ = 0, dropsSeen_ = 0;
    int32_t checksumFeed_ = 0;
    uint32_t random_ = 0x1234567u;
    uint32_t nextRandom();
};

} // namespace reluva::idtech3::eliteforce
