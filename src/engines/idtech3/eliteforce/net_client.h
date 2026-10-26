// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/wire/message.h"
#include "engines/idtech3/wire/netchan.h"

#include "common/net.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// The player's side of Elite Force's network play (protocol 24,
// eliteforce-network.md): it joins a server (getchallenge, connect),
// takes its game state and snapshots, and sends the player's commands
// and what they type. The page runs the client game with what it
// gathers, as with its own server.
class NetClient {
public:
    // Quake III's connection states, which the menus' connecting screen
    // shows (uiClientState_t).
    enum class State : int32_t {
        Disconnected = 1,
        Connecting = 3,  // asking for a challenge
        Challenging = 4, // asking to connect
        Connected = 5,   // waiting for the game state
        Primed = 7,      // has it; loading the level
        Active = 8,      // playing
    };

    // `userinfo`: the player's settings (name, model...); the protocol,
    // qport and challenge are added.
    NetClient(std::unique_ptr<reluva::net::UdpSocket> socket, const reluva::net::Address& server,
              std::string userinfo, uint16_t qport);

    // Reads what has come and sends what's due; `now` in ms.
    void frame(uint64_t now);
    // The player's command for this moment (its serverTime from
    // serverTime()); they go out at most cl_maxpackets a second, each
    // packet with the ones since the one before the last.
    void addCommand(const GameHost::UserCommand& cmd);
    // Something the player typed for the server ("say hi"...), sent until
    // it's acknowledged.
    void addClientCommand(const std::string& text);
    // The player's settings changed (sent as a userinfo command).
    void setUserinfo(const std::string& userinfo);
    // Leaving: the server is told (three times, in case some are lost).
    void disconnect();
    // The player has loaded the level: from now on its moves are sent.
    void setLoaded() {
        if (state_ == State::Primed)
            state_ = State::Active;
    }

    // Downloading a pak the server has and this player hasn't (the
    // name as the server lists it: "baseEF/name.pk3"): asked for, then
    // its blocks taken and acknowledged one by one. Snapshots are ignored
    // meanwhile.
    void download(const std::string& remoteName);
    bool downloading() const { return !downloadName_.empty(); }
    const std::string& downloadName() const { return downloadName_; }
    int32_t downloadSize() const { return downloadSize_; } // -1 until known
    size_t downloadCount() const { return downloadData_.size(); }
    // A download that has finished: its name and bytes (false if none).
    bool takeDownload(std::string& name, std::vector<uint8_t>& data);

    State state() const { return state_; }
    const reluva::net::Address& server() const { return server_; }
    int connectPackets() const { return connectPackets_; }
    // Why it isn't connected, or what the server said ("print").
    const std::string& message() const { return message_; }

    // The game state: configstrings, the command sequence it started at
    // and the incoming message it came with (for the client game's
    // Init), whether it has come; a new one means a new level.
    const std::map<int32_t, std::string>& configstrings() const { return configstrings_; }
    int32_t gamestateCommandSequence() const { return gamestateSequence_; }
    int32_t gamestateMessage() const { return gamestateMessage_; }
    // The number the server's pure check seeds pak checksums with.
    int32_t checksumFeed() const { return checksumFeed_; }
    bool takeNewGamestate() {
        const bool got = newGamestate_;
        newGamestate_ = false;
        return got;
    }
    // Snapshots that came since the last call, oldest first.
    std::vector<GameHost::Snapshot> takeSnapshots() { return std::move(newSnapshots_); }
    // The commands the server sent, by sequence from 1 (those before the
    // game state's are empty).
    const std::vector<std::string>& serverCommands() const { return serverCommands_; }
    // The time to show the level at, following the server's clock as
    // the original's client does (a little behind the newest snapshot).
    int32_t serverTime() const { return serverTime_; }
    bool hasSnapshot() const { return haveSnapshot_; }

private:
    void sendText(const std::string& text);
    void outOfBand(const std::string& text, uint64_t now);
    void parseMessage(wire::MessageReader& r, uint64_t now);
    void parseGamestate(wire::MessageReader& r);
    void parseSnapshot(wire::MessageReader& r, uint64_t now);
    void parseDownload(wire::MessageReader& r, uint64_t now);
    std::string downloadName_, finishedName_;
    std::vector<uint8_t> downloadData_, finishedData_;
    int32_t downloadSize_ = -1, downloadBlock_ = 0;
    bool finished_ = false;
    void serverCommand(int32_t sequence, const std::string& text);
    void sendPacket(uint64_t now);
    void adjustTime(int32_t snapshotTime, uint64_t now);

    std::unique_ptr<reluva::net::UdpSocket> socket_;
    reluva::net::Address server_;
    std::string userinfo_;
    uint16_t qport_;
    State state_ = State::Connecting;
    std::string message_;
    uint64_t lastSent_ = 0, lastReceived_ = 0, lastPacket_ = 0;
    int connectPackets_ = 0;
    int32_t challenge_ = 0;
    wire::Netchan chan_;

    // From the server.
    std::map<int32_t, std::string> configstrings_;
    std::map<int32_t, std::vector<uint8_t>> baselines_;
    int32_t gamestateSequence_ = 0, gamestateMessage_ = 0, checksumFeed_ = 0;
    bool newGamestate_ = false;
    std::vector<std::string> serverCommands_;
    std::string bigConfigstring_; // one coming in pieces (bcs0, bcs1, bcs2)
    int32_t serverCommandSequence_ = 0; // the last received
    struct Snap {
        bool valid = false;
        int32_t message = 0, serverTime = 0;
        std::vector<uint8_t> playerState;
        std::map<int32_t, std::vector<uint8_t>> entities;
        std::array<uint8_t, 32> areaMask{};
    };
    std::array<Snap, 32> snaps_;
    int32_t lastSnapMessage_ = -1; // the newest valid snapshot's message
    std::vector<GameHost::Snapshot> newSnapshots_;

    // To the server.
    std::array<std::string, 64> reliable_;
    int32_t reliableSequence_ = 0, reliableAcknowledge_ = 0;
    std::deque<GameHost::UserCommand> commands_; // not yet sent twice
    size_t commandsInLastPacket_ = 0;

    // The clock.
    bool haveSnapshot_ = false;
    int64_t timeDelta_ = 0;
    int32_t serverTime_ = 0, newestSnapshotTime_ = 0;
    bool extrapolated_ = false;
};

} // namespace reluva::idtech3::eliteforce
