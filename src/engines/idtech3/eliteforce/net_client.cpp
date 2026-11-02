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
                  kSvcDownload = 6, kSvcSnapshot = 7;
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

NetClient::NetClient(std::vector<uint8_t> demo)
    : qport_(0), state_(State::Connected), chan_(true, 0), demo_(true), demoData_(std::move(demo)) {}

void NetClient::sendText(const std::string& text) {
    if (!socket_)
        return;
    std::vector<uint8_t> d(4, 0xff);
    d.insert(d.end(), text.begin(), text.end());
    socket_->send(server_, d);
}

void NetClient::frame(uint64_t now) {
    if (demo_) {
        readDemo(now);
        return;
    }
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
        incoming_ = chan_.incomingSequence();
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
    // At cl_maxpackets while playing; 20 a second while downloading, as
    // the original's client acknowledges blocks; otherwise one a second.
    const uint64_t interval = state_ == State::Active ? 1000 / kMaxPackets : downloading() ? 50 : kWaitingMs;
    if (!lastPacket_ || now - lastPacket_ >= interval)
        sendPacket(now);
}

void NetClient::readDemo(uint64_t now) {
    // As the original's client plays one: the messages up to the game
    // state at once; then, once the level is loaded, one a frame until
    // the first snapshot, and after that the next whenever the time shown
    // reaches the newest snapshot.
    while (state_ == State::Connected)
        if (!readDemoMessage(now))
            return;
    if (state_ != State::Active)
        return;
    if (!haveSnapshot_) {
        readDemoMessage(now);
        return;
    }
    serverTime_ = std::max(serverTime_, int32_t(int64_t(now) + timeDelta_));
    while (state_ == State::Active && serverTime_ >= newestSnapshotTime_)
        if (!readDemoMessage(now))
            return;
}

bool NetClient::readDemoMessage(uint64_t now) {
    auto word = [&](size_t at) {
        int32_t v;
        std::memcpy(&v, demoData_.data() + at, 4);
        return v;
    };
    // The end: -1 for both, or the file running out.
    if (demoAt_ + 8 > demoData_.size() || word(demoAt_ + 4) < 0 ||
        size_t(word(demoAt_ + 4)) > demoData_.size() - demoAt_ - 8 || word(demoAt_ + 4) > int32_t(wire::Netchan::kMaxMessage)) {
        state_ = State::Disconnected;
        return false;
    }
    incoming_ = word(demoAt_);
    const auto length = size_t(word(demoAt_ + 4));
    wire::MessageReader r(std::vector<uint8_t>(demoData_.begin() + long(demoAt_ + 8),
                                               demoData_.begin() + long(demoAt_ + 8 + length)));
    demoAt_ += 8 + length;
    parseMessage(r, now);
    return true;
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
        } else if (op == kSvcDownload) {
            parseDownload(r, now);
        } else if (op == kSvcNop) {
            continue;
        } else {
            break; // the end (protocol 24 sends no end marker)
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
    // Configstrings that changed, kept here too (the level's id in the
    // system info goes back with every packet), as the original's client
    // keeps its game state.
    const std::vector<std::string> words = splitCommand(text);
    if (words.size() >= 2 && (words[0] == "cs" || words[0] == "bcs0" || words[0] == "bcs1" || words[0] == "bcs2")) {
        const int32_t index = std::atoi(words[1].c_str());
        const std::string value = words.size() >= 3 ? words[2] : std::string();
        if (words[0] == "bcs0")
            bigConfigstring_ = value;
        else if (words[0] == "bcs1")
            bigConfigstring_ += value;
        else if (words[0] == "bcs2")
            configstrings_[index] = bigConfigstring_ + value;
        else
            configstrings_[index] = value;
    }
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
    gamestateMessage_ = incoming_;
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
    snap.message = incoming_;
    snap.serverTime = r.longValue();
    const int32_t deltaNum = r.byte();
    const int32_t flags = r.byte();
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
    if (r.overflowed() || !snap.valid || downloading())
        return; // while downloading, they carry nothing (the acknowledgement)
    snaps_[size_t(snap.message & 31)] = snap;
    lastSnapMessage_ = snap.message;
    GameHost::Snapshot out;
    out.serverTime = snap.serverTime;
    out.playerState = snap.playerState;
    for (const auto& [number, state] : snap.entities)
        out.entities.push_back(state);
    out.serverCommandSequence = serverCommandSequence_;
    out.messageNum = snap.message;
    out.areaMask = snap.areaMask;
    out.snapFlags = flags;
    newSnapshots_.push_back(std::move(out));
    adjustTime(snap.serverTime, now);
}

void NetClient::download(const std::string& remoteName) {
    downloadName_ = remoteName;
    downloadData_.clear();
    downloadSize_ = -1;
    downloadBlock_ = 0;
    addClientCommand("download " + remoteName);
}

bool NetClient::takeDownload(std::string& name, std::vector<uint8_t>& data) {
    if (!finished_)
        return false;
    finished_ = false;
    name = std::move(finishedName_);
    data = std::move(finishedData_);
    return true;
}

void NetClient::parseDownload(wire::MessageReader& r, uint64_t now) {
    // A block: its number, for block 0 the file's size (-1: refused), its
    // length (0: the end) and its bytes; acknowledged in order (nextdl).
    const auto block = int32_t(uint16_t(r.shortValue()));
    if (block == 0 && downloadBlock_ == 0) {
        downloadSize_ = r.longValue();
        if (downloadSize_ < 0) {
            state_ = State::Disconnected;
            message_ = "Couldn't download " + downloadName_ + ": the server won't send it";
            downloadName_.clear();
            return;
        }
    }
    const int32_t length = r.shortValue();
    if (length < 0 || length > 16384)
        return;
    std::vector<uint8_t> data;
    if (length > 0)
        data = r.data(size_t(length));
    if (downloadName_.empty() || block != (downloadBlock_ & 0xffff))
        return; // not asked for, or out of order: sent again
    downloadData_.insert(downloadData_.end(), data.begin(), data.end());
    addClientCommand("nextdl " + std::to_string(downloadBlock_));
    ++downloadBlock_;
    if (length == 0) {
        // The end: acknowledged at once, twice, so the server stops.
        finishedName_ = std::move(downloadName_);
        finishedData_ = std::move(downloadData_);
        downloadName_.clear();
        downloadData_.clear();
        finished_ = true;
        sendPacket(now);
        sendPacket(now);
    }
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
    if (demo_)
        return; // a demo's clock keeps the first setting, as the original's
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
    if (!socket_)
        return;
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
