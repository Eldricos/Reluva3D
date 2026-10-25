// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/net_client.h"

#include "engines/idtech3/bindings.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/info_string.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

constexpr int32_t kSvcNop = 1, kSvcGamestate = 2, kSvcConfigstring = 3, kSvcBaseline = 4, kSvcServerCommand = 5,
                  kSvcSnapshot = 7;
constexpr uint32_t kClcMove = 2, kClcMoveNoDelta = 3, kClcClientCommand = 4;
constexpr uint64_t kResendMs = 3000;   // getchallenge and connect, until answered
constexpr uint64_t kWaitingMs = 1000;  // a packet a second while waiting for the level
constexpr uint64_t kTimeoutMs = 200000; // cl_timeout
constexpr int kMaxPackets = 43;        // Elite Force's cl_maxpackets
constexpr size_t kMaxPacketCommands = 32;

} // namespace

NetClient::NetClient(std::unique_ptr<reluva::net::UdpSocket> socket, const reluva::net::Address& server,
                     std::string userinfo, uint16_t qport)
    : socket_(std::move(socket)), server_(server), userinfo_(std::move(userinfo)), qport_(qport),
      chan_(true, qport) {}

void NetClient::sendText(const std::string& text) {
    std::vector<uint8_t> d(4, 0xff);
    d.insert(d.end(), text.begin(), text.end());
    socket_->send(server_, d);
}

void NetClient::frame(uint64_t now) {
    reluva::net::Address from;
    std::vector<uint8_t> d;
    while (socket_->receive(from, d)) {
        if (from != server_)
            continue;
        if (d.size() >= 4 && d[0] == 0xff && d[1] == 0xff && d[2] == 0xff && d[3] == 0xff) {
            outOfBand(std::string(d.begin() + 4, d.end()), now);
            continue;
        }
        std::vector<uint8_t> message;
        if (state_ < State::Connected || !chan_.process(std::move(d), message))
            continue;
        lastReceived_ = now;
        wire::MessageReader r(std::move(message));
        parseMessage(r, now);
    }
    switch (state_) {
    case State::Disconnected: return;
    case State::Connecting:
        if (!lastSent_ || now - lastSent_ >= kResendMs) {
            lastSent_ = now;
            ++connectPackets_;
            sendText("getchallenge");
        }
        return;
    case State::Challenging:
        if (!lastSent_ || now - lastSent_ >= kResendMs) {
            lastSent_ = now;
            ++connectPackets_;
            std::string info = userinfo_;
            setInfoValue(info, "protocol", "24");
            setInfoValue(info, "qport", std::to_string(qport_));
            setInfoValue(info, "challenge", std::to_string(challenge_));
            sendText("connect \"" + info + "\"");
        }
        return;
    default: break;
    }
    if (now - lastReceived_ > kTimeoutMs) {
        state_ = State::Disconnected;
        message_ = "Server connection timed out.";
        return;
    }
    if (haveSnapshot_) {
        // The level's clock runs on with ours from the last setting.
        serverTime_ = std::max(serverTime_, int32_t(int64_t(now) + timeDelta_));
        if (serverTime_ >= newestSnapshotTime_ - 5)
            extrapolated_ = true;
    }
    const uint64_t interval = state_ == State::Active ? 1000 / kMaxPackets : kWaitingMs;
    if (!lastPacket_ || now - lastPacket_ >= interval)
        sendPacket(now);
}

void NetClient::outOfBand(const std::string& text, uint64_t now) {
    if (text.rfind("print\n", 0) == 0) {
        message_ = text.substr(6);
        while (!message_.empty() && message_.back() == '\n')
            message_.pop_back();
        return;
    }
    const std::vector<std::string> words = splitCommand(text.substr(0, text.find('\n')));
    if (words.empty())
        return;
    if (words[0] == "challengeResponse" && state_ == State::Connecting && words.size() >= 2) {
        challenge_ = std::atoi(words[1].c_str());
        state_ = State::Challenging;
        lastSent_ = 0;
        connectPackets_ = 0;
    } else if (words[0] == "connectResponse" && state_ == State::Challenging) {
        state_ = State::Connected;
        chan_ = wire::Netchan(true, qport_);
        lastSent_ = lastPacket_ = 0;
        lastReceived_ = now;
        message_.clear();
    }
}

void NetClient::parseMessage(wire::MessageReader& r, uint64_t now) {
    while (!r.atEnd()) {
        const int32_t op = r.byte();
        if (op == kSvcServerCommand) {
            const int32_t sequence = r.longValue();
            const std::string text = r.string();
            if (!r.overflowed())
                serverCommand(sequence, text);
        } else if (op == kSvcGamestate) {
            parseGamestate(r);
        } else if (op == kSvcSnapshot) {
            parseSnapshot(r, now);
        } else if (op == kSvcNop) {
            continue;
        } else {
            // The end (protocol 24 sends no end marker), or a download
            // (kSvcDownload), which isn't asked for.
            break;
        }
        if (r.overflowed())
            break;
    }
}

void NetClient::serverCommand(int32_t sequence, const std::string& text) {
    if (sequence <= serverCommandSequence_)
        return;
    serverCommandSequence_ = sequence;
    if (serverCommands_.size() < size_t(sequence))
        serverCommands_.resize(size_t(sequence));
    serverCommands_[size_t(sequence - 1)] = text;
    if (text.rfind("disconnect", 0) == 0) {
        // Protocol 24's reason is the rest of the line, unquoted.
        state_ = State::Disconnected;
        message_ = text.size() > 11 ? text.substr(11) : "The server disconnected.";
    }
}

void NetClient::parseGamestate(wire::MessageReader& r) {
    gamestateSequence_ = r.longValue();
    serverCommandSequence_ = gamestateSequence_;
    serverCommands_.assign(size_t(std::max(0, gamestateSequence_)), std::string());
    configstrings_.clear();
    baselines_.clear();
    while (!r.atEnd()) {
        const int32_t op = r.byte();
        if (op == kSvcConfigstring) {
            const int32_t index = r.shortValue();
            configstrings_[index] = r.bigString();
        } else if (op == kSvcBaseline) {
            const auto number = int32_t(r.bits(kEntityNumberBits));
            std::vector<uint8_t> state;
            if (!readEntityDelta(r, number, {}, state))
                baselines_[number] = std::move(state);
        } else {
            break; // 0 ends the list
        }
        if (r.overflowed())
            break;
    }
    checksumFeed_ = r.longValue();
    gamestateMessage_ = chan_.incomingSequence();
    newGamestate_ = true;
    state_ = State::Primed;
    snaps_ = {};
    lastSnapMessage_ = -1;
    haveSnapshot_ = false;
    commands_.clear();
    commandsInLastPacket_ = 0;
}

void NetClient::parseSnapshot(wire::MessageReader& r, uint64_t now) {
    const int32_t acknowledged = r.longValue();
    if (acknowledged <= reliableSequence_ && acknowledged > reliableAcknowledge_)
        reliableAcknowledge_ = acknowledged;
    Snap snap;
    snap.message = chan_.incomingSequence();
    snap.serverTime = r.longValue();
    const int32_t deltaNum = r.byte();
    r.byte(); // snap flags
    const int32_t maskBytes = std::clamp(r.byte(), 0, 32);
    const std::vector<uint8_t> mask = r.data(size_t(maskBytes));
    std::copy(mask.begin(), mask.end(), snap.areaMask.begin());
    // A change from an older one, if it's still kept.
    const Snap* old = nullptr;
    snap.valid = true;
    if (deltaNum > 0) {
        const int32_t oldMessage = snap.message - deltaNum;
        const Snap& o = snaps_[size_t(oldMessage & 31)];
        if (o.valid && o.message == oldMessage)
            old = &o;
        else
            snap.valid = false; // read, then thrown away: the next is asked for whole
    }
    static const std::vector<uint8_t> none;
    readPlayerDelta(r, old ? old->playerState : none, snap.playerState);
    static const std::map<int32_t, std::vector<uint8_t>> noEntities;
    const auto& before = old ? old->entities : noEntities;
    auto o = before.begin();
    while (!r.overflowed()) {
        const auto number = int32_t(r.bits(kEntityNumberBits));
        if (number == kNoEntity)
            break;
        // Those before it are unchanged.
        for (; o != before.end() && o->first < number; ++o)
            snap.entities.insert(*o);
        std::vector<uint8_t> state;
        bool removed;
        if (o != before.end() && o->first == number) {
            removed = readEntityDelta(r, number, o->second, state);
            ++o;
        } else {
            const auto base = baselines_.find(number);
            removed = readEntityDelta(r, number, base != baselines_.end() ? base->second : none, state);
        }
        if (!removed)
            snap.entities[number] = std::move(state);
    }
    for (; o != before.end(); ++o)
        snap.entities.insert(*o);
    if (r.overflowed() || !snap.valid)
        return;
    snaps_[size_t(snap.message & 31)] = snap;
    lastSnapMessage_ = snap.message;
    GameHost::Snapshot out;
    out.serverTime = snap.serverTime;
    out.playerState = snap.playerState;
    for (const auto& [number, state] : snap.entities)
        out.entities.push_back(state);
    out.serverCommandSequence = serverCommandSequence_;
    out.areaMask = snap.areaMask;
    newSnapshots_.push_back(std::move(out));
    adjustTime(snap.serverTime, now);
}

void NetClient::adjustTime(int32_t snapshotTime, uint64_t now) {
    // As the original's client: the level's time is ours plus a
    // difference, reset when it's far out, halved towards the newest
    // snapshot when it's somewhat out, and otherwise nudged a little
    // each snapshot, back when the time had run past the newest one and
    // on otherwise, so it stays just behind the newest.
    newestSnapshotTime_ = snapshotTime;
    const int64_t newDelta = int64_t(snapshotTime) - int64_t(now);
    if (!haveSnapshot_) {
        haveSnapshot_ = true;
        timeDelta_ = newDelta;
        serverTime_ = snapshotTime;
        return;
    }
    const int64_t off = newDelta > timeDelta_ ? newDelta - timeDelta_ : timeDelta_ - newDelta;
    if (off > 500) {
        timeDelta_ = newDelta;
        serverTime_ = snapshotTime;
    } else if (off > 100) {
        timeDelta_ = (timeDelta_ + newDelta) / 2;
    } else if (extrapolated_) {
        extrapolated_ = false;
        timeDelta_ -= 2;
    } else {
        timeDelta_ += 1;
    }
}

void NetClient::addCommand(const GameHost::UserCommand& cmd) {
    if (state_ < State::Primed)
        return;
    commands_.push_back(cmd);
    while (commands_.size() > kMaxPacketCommands) {
        commands_.pop_front();
        if (commandsInLastPacket_)
            --commandsInLastPacket_;
    }
}

void NetClient::addClientCommand(const std::string& text) {
    if (reliableSequence_ - reliableAcknowledge_ >= int32_t(reliable_.size()))
        return; // too many waiting
    ++reliableSequence_;
    reliable_[size_t(reliableSequence_ & 63)] = text;
}

void NetClient::setUserinfo(const std::string& userinfo) {
    userinfo_ = userinfo;
    if (state_ >= State::Connected)
        addClientCommand("userinfo \"" + userinfo + "\"");
}

void NetClient::disconnect() {
    if (state_ >= State::Connected) {
        addClientCommand("disconnect");
        for (int i = 0; i < 3; ++i)
            sendPacket(lastPacket_ + 1);
    }
    state_ = State::Disconnected;
}

void NetClient::sendPacket(uint64_t now) {
    lastPacket_ = now;
    wire::MessageWriter w;
    const auto it = configstrings_.find(1);
    w.longValue(it != configstrings_.end() ? std::atoi(infoValue(it->second, "sv_serverid").c_str()) : 0);
    w.longValue(chan_.incomingSequence());
    w.longValue(serverCommandSequence_);
    for (int32_t i = reliableAcknowledge_ + 1; i <= reliableSequence_; ++i) {
        w.byte(kClcClientCommand);
        w.longValue(i);
        w.string(reliable_[size_t(i & 63)]);
    }
    if (!commands_.empty() && state_ >= State::Primed) {
        // A change from the last snapshot if this message is one we
        // could take as one; otherwise the next is asked for whole.
        const bool delta = lastSnapMessage_ >= 0 && lastSnapMessage_ == chan_.incomingSequence();
        w.byte(delta ? kClcMove : kClcMoveNoDelta);
        w.byte(uint32_t(commands_.size()));
        GameHost::UserCommand previous;
        for (const GameHost::UserCommand& cmd : commands_) {
            writeUserCommand(w, previous, cmd);
            previous = cmd;
        }
        // Each command goes out twice (in this packet and the next).
        for (size_t i = 0; i < commandsInLastPacket_ && !commands_.empty(); ++i)
            commands_.pop_front();
        commandsInLastPacket_ = commands_.size();
    }
    for (const std::vector<uint8_t>& d : chan_.transmit(w.bytes()))
        socket_->send(server_, d);
}

} // namespace reluva::idtech3::eliteforce
