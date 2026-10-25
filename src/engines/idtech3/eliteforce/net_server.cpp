// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/net_server.h"

#include "engines/idtech3/bindings.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/info_string.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

// Server message opcodes (Quake III's numbers).
constexpr uint32_t kSvcGamestate = 2, kSvcConfigstring = 3, kSvcBaseline = 4, kSvcServerCommand = 5,
                   kSvcSnapshot = 7;
// Client message opcodes.
constexpr int32_t kClcNop = 1, kClcMove = 2, kClcMoveNoDelta = 3, kClcClientCommand = 4;
constexpr int kMaxPacketCommands = 32;
constexpr int32_t kPlayersConfigstring = 544; // CS_PLAYERS: a player's info, "\n\<name>\..."
constexpr int32_t kPersistantOffset = 268;   // playerState persistant[], 1.2's layout; [0] the score
constexpr size_t kConfigstringPiece = 1000;  // longer configstrings go in pieces (bcs0, bcs1, bcs2)
constexpr uint64_t kZombieMs = 2000;
constexpr uint64_t kChallengeMs = 60000;

} // namespace

std::vector<std::string> configstringCommands(const std::map<int32_t, std::string>& before,
                                              const std::map<int32_t, std::string>& now) {
    std::vector<std::string> out;
    for (const auto& [index, text] : now) {
        const auto it = before.find(index);
        if (it != before.end() && it->second == text)
            continue;
        const std::string i = std::to_string(index);
        if (text.size() < kConfigstringPiece) {
            out.push_back("cs " + i + " \"" + text + "\"");
            continue;
        }
        for (size_t at = 0; at < text.size(); at += kConfigstringPiece) {
            const char* kind = at == 0 ? "bcs0 " : at + kConfigstringPiece >= text.size() ? "bcs2 " : "bcs1 ";
            out.push_back(kind + i + " \"" + text.substr(at, kConfigstringPiece) + "\"");
        }
    }
    for (const auto& [index, text] : before)
        if (!now.count(index))
            out.push_back("cs " + std::to_string(index) + " \"\"");
    return out;
}

NetServer::NetServer(GameHost& game, CvarStore& cvars, std::unique_ptr<reluva::net::UdpSocket> socket)
    : game_(game), cvars_(cvars), socket_(std::move(socket)) {
    cvars_.create("sv_timeout", "200", 0);
    clients_.resize(64);
    random_ ^= uint32_t(reinterpret_cast<uintptr_t>(this));
}

NetServer::~NetServer() {
    // As the original when it shuts down: a last message to everyone,
    // telling them it's gone.
    for (Client& c : clients_)
        if (c.state >= State::Connected)
            finalMessage(c, "Server shut down", lastNow_);
}

void NetServer::finalMessage(Client& c, const std::string& reason, uint64_t now) {
    // As the original: the reason as a command, and twice, a snapshot
    // with it (the client game reads commands as snapshots say they're
    // there; the retail client acts on disconnect only then).
    if (c.reliableSequence - c.reliableAcknowledge < kMaxReliable) {
        ++c.reliableSequence;
        c.reliable[size_t(c.reliableSequence & (kMaxReliable - 1))] = "disconnect " + reason;
    }
    for (int i = 0; i < 2; ++i) {
        if (c.state >= State::Primed) {
            sendSnapshot(c, now);
        } else {
            wire::MessageWriter w;
            writeReliable(c, w);
            transmit(c, w);
        }
    }
}

uint32_t NetServer::nextRandom() {
    random_ = random_ * 1103515245u + 12345u;
    return random_ >> 1;
}

void NetServer::levelStarted() {
    // The entities as the level settled are the baselines new players'
    // snapshots are made from, as the original takes them.
    baselines_.clear();
    for (const int32_t n : game_.linked())
        if (std::vector<uint8_t> s = game_.entityState(n); !s.empty())
            baselines_[n] = std::move(s);
    configstrings_ = game_.configstrings();
    serverCommandsSeen_ = game_.serverCommands().size();
    dropsSeen_ = game_.drops().size();
    checksumFeed_ = int32_t(nextRandom());
    ++levelCount_;
    for (Client& c : clients_)
        if (c.state >= State::Connected) {
            c.state = State::Connected;
            c.gamestateMessage = -1;
            c.deltaMessage = -1;
            c.nextSnapshotTime = 0;
        }
}

std::vector<std::pair<int32_t, std::string>> NetServer::players() const {
    std::vector<std::pair<int32_t, std::string>> out;
    for (const Client& c : clients_)
        if (c.state >= State::Connected)
            out.emplace_back(c.number, c.name);
    return out;
}

void NetServer::sendText(const reluva::net::Address& to, const std::string& text) {
    std::vector<uint8_t> d(4, 0xff);
    d.insert(d.end(), text.begin(), text.end());
    socket_->send(to, d);
}

std::string NetServer::infoString(const std::string& challenge) const {
    // What the server list shows (eliteforce-network.md): Elite Force's
    // servers send it quoted on one line.
    const int maxClients = std::clamp(cvars_.integer("sv_maxclients"), 1, 64);
    int clients = 0, humans = 0;
    for (int32_t n = 0; n < maxClients; ++n)
        if (!game_.clientSlotFree(n)) {
            ++clients;
            humans += !game_.isBot(n);
        }
    std::string info;
    setInfoValue(info, "challenge", challenge);
    setInfoValue(info, "protocol", "24");
    setInfoValue(info, "hostname", cvars_.string("sv_hostname"));
    setInfoValue(info, "mapname", cvars_.string("mapname"));
    setInfoValue(info, "clients", std::to_string(clients));
    setInfoValue(info, "g_humanplayers", std::to_string(humans));
    setInfoValue(info, "sv_maxclients", std::to_string(maxClients));
    setInfoValue(info, "gametype", std::to_string(cvars_.integer("g_gametype")));
    setInfoValue(info, "pure", "0");
    return info;
}

Error NetServer::receive(uint64_t now) {
    lastNow_ = now;
    reluva::net::Address from;
    std::vector<uint8_t> d;
    while (socket_->receive(from, d)) {
        if (d.size() >= 4 && d[0] == 0xff && d[1] == 0xff && d[2] == 0xff && d[3] == 0xff) {
            outOfBand(from, std::string(d.begin() + 4, d.end()), now);
            continue;
        }
        uint16_t qport = 0;
        if (!wire::Netchan::clientQport(d, qport))
            continue;
        if (Client* c = clientFor(from, qport))
            if (Error err = packet(*c, d, now))
                return err;
    }
    return Error::success();
}

NetServer::Client* NetServer::clientFor(const reluva::net::Address& from, uint16_t qport) {
    for (Client& c : clients_)
        if (c.state != State::Free && c.address.ip == from.ip && c.qport == qport) {
            c.address.port = from.port; // a router may have changed it
            return &c;
        }
    return nullptr;
}

void NetServer::outOfBand(const reluva::net::Address& from, const std::string& text, uint64_t now) {
    std::string line = text;
    if (const size_t nl = line.find('\n'); nl != std::string::npos)
        line.resize(nl);
    const std::vector<std::string> words = splitCommand(line);
    if (words.empty())
        return;
    const std::string& verb = words[0];
    if (verb == "getinfo") {
        sendText(from, "infoResponse \"" + infoString(words.size() > 1 ? words[1] : "") + "\"");
    } else if (verb == "getstatus") {
        std::string info = game_.configstrings().count(0) ? game_.configstrings().at(0) : std::string();
        setInfoValue(info, "challenge", words.size() > 1 ? words[1] : "");
        std::string reply = "statusResponse\n" + info + "\n";
        const int maxClients = std::clamp(cvars_.integer("sv_maxclients"), 1, 64);
        for (int32_t n = 0; n < maxClients; ++n) {
            if (game_.clientSlotFree(n))
                continue;
            const auto it = game_.configstrings().find(kPlayersConfigstring + n);
            const std::string name = it != game_.configstrings().end() ? infoValue(it->second, "n") : "";
            const GameHost::Snapshot s = game_.snapshot(n);
            int32_t score = 0;
            if (s.playerState.size() >= size_t(kPersistantOffset + 4))
                std::memcpy(&score, s.playerState.data() + kPersistantOffset, 4);
            int ping = 0;
            for (const Client& c : clients_)
                if (c.state >= State::Connected && c.number == n)
                    ping = c.ping;
            reply += std::to_string(score) + " " + std::to_string(ping) + " \"" + name + "\"\n";
        }
        sendText(from, reply);
    } else if (verb == "getchallenge") {
        Challenge& ch = challenges_[from];
        if (!ch.value || now - ch.time > kChallengeMs)
            ch = {int32_t(nextRandom() & 0x7fffffff) | 1, now};
        sendText(from, "challengeResponse " + std::to_string(ch.value));
    } else if (verb == "connect" && words.size() >= 2) {
        connect(from, words[1], now);
    }
    // "disconnect" and anything else: nothing to answer.
}

void NetServer::connect(const reluva::net::Address& from, const std::string& userinfo, uint64_t now) {
    const std::string protocol = infoValue(userinfo, "protocol");
    if (protocol != "24") {
        sendText(from, "print\nServer uses protocol version 24 (yours is " + protocol + ").\n");
        return;
    }
    // Clients on the same machine needn't have asked for a challenge.
    if (!from.isLoopback()) {
        const auto it = challenges_.find(from);
        if (it == challenges_.end() || std::atoi(infoValue(userinfo, "challenge").c_str()) != it->second.value) {
            sendText(from, "print\nNo or bad challenge for your address.\n");
            return;
        }
    }
    const auto qport = uint16_t(std::atoi(infoValue(userinfo, "qport").c_str()));
    // A player connecting again (the same address and qport) keeps their slot.
    int32_t number = -1;
    Client* slot = nullptr;
    for (Client& c : clients_)
        if (c.state >= State::Connected && c.address.ip == from.ip && c.qport == qport) {
            if (now - c.lastReceived < 3000 && c.state == State::Connected) {
                sendText(from, "connectResponse");
                return; // its own repeat
            }
            number = c.number;
            game_.disconnectClient(number);
            slot = &c;
        }
    const int maxClients = std::clamp(cvars_.integer("sv_maxclients"), 1, 64);
    if (number < 0)
        for (int32_t n = 0; n < maxClients && number < 0; ++n) {
            if (reserved_.count(n) || !game_.clientSlotFree(n))
                continue;
            bool ours = false;
            for (const Client& c : clients_)
                ours = ours || (c.state != State::Free && c.number == n);
            if (!ours)
                number = n;
        }
    if (number < 0) {
        sendText(from, "print\nServer is full.\n");
        return;
    }
    if (!slot)
        for (Client& c : clients_)
            if (c.state == State::Free) {
                slot = &c;
                break;
            }
    if (!slot) {
        sendText(from, "print\nServer is full.\n");
        return;
    }
    std::string info = userinfo;
    setInfoValue(info, "ip", from.toString());
    std::string refused;
    if (Error err = game_.connectClient(number, info, refused); err || !refused.empty()) {
        sendText(from, "print\n" + (err ? err.message : refused) + "\n");
        return;
    }
    *slot = Client{};
    slot->state = State::Connected;
    slot->number = number;
    slot->address = from;
    slot->qport = qport;
    slot->userinfo = info;
    slot->lastReceived = now;
    applyUserinfo(*slot);
    sendText(from, "connectResponse");
    if (print)
        print(slot->name + " connected from " + from.toString() + "\n");
}

void NetServer::applyUserinfo(Client& c) {
    c.name = infoValue(c.userinfo, "name");
    const int fps = std::max(1, cvars_.integer("sv_fps") ? cvars_.integer("sv_fps") : 20);
    int snaps = std::atoi(infoValue(c.userinfo, "snaps").c_str());
    snaps = snaps <= 0 ? fps : std::clamp(snaps, 1, fps);
    c.snapMs = 1000 / snaps;
}

Error NetServer::packet(Client& c, const std::vector<uint8_t>& datagram, uint64_t now) {
    std::vector<uint8_t> message;
    if (c.state == State::Zombie || !c.chan.process(datagram, message))
        return Error::success();
    c.lastReceived = now;
    wire::MessageReader r(std::move(message));
    const int32_t serverId = r.longValue();
    c.messageAcknowledge = r.longValue();
    const int32_t reliableAcknowledge = r.longValue();
    if (r.overflowed())
        return Error::success();
    if (reliableAcknowledge <= c.reliableSequence && reliableAcknowledge >= c.reliableSequence - kMaxReliable)
        c.reliableAcknowledge = std::max(c.reliableAcknowledge, reliableAcknowledge);
    const Frame& acked = c.frames[size_t(c.messageAcknowledge & (kPacketBackup - 1))];
    if (acked.message == c.messageAcknowledge && acked.sentMs)
        c.ping = int(now - acked.sentMs);
    // Commands from before the level it has: the game state again once
    // it has had a message since the last one.
    if (serverId != cvars_.integer("sv_serverid")) {
        if (c.messageAcknowledge > c.gamestateMessage)
            sendGamestate(c, now);
        return Error::success();
    }
    while (!r.atEnd()) {
        const int32_t op = r.byte();
        if (op == kClcClientCommand) {
            const int32_t sequence = r.longValue();
            const std::string text = r.string();
            if (r.overflowed() || sequence <= c.lastClientCommand)
                continue;
            if (sequence > c.lastClientCommand + 1) {
                drop(c, "Lost reliable commands", now);
                return Error::success();
            }
            c.lastClientCommand = sequence;
            if (Error err = clientCommand(c, text))
                return err;
            if (c.state == State::Zombie)
                return Error::success();
        } else if (op == kClcMove || op == kClcMoveNoDelta) {
            const int count = r.byte();
            if (count < 1 || count > kMaxPacketCommands)
                break;
            std::vector<GameHost::UserCommand> commands;
            GameHost::UserCommand previous;
            for (int i = 0; i < count; ++i)
                commands.push_back(previous = readUserCommand(r, previous));
            if (r.overflowed())
                break;
            c.deltaMessage = op == kClcMove ? c.messageAcknowledge : -1;
            if (c.state == State::Primed) {
                // Its first move puts the player in the level.
                c.state = State::Active;
                c.deltaMessage = -1;
                c.lastCommand = GameHost::UserCommand{};
                if (Error err = game_.beginClient(c.number))
                    return err;
            }
            if (c.state != State::Active)
                break;
            for (const GameHost::UserCommand& cmd : commands) {
                if (cmd.serverTime <= c.lastCommand.serverTime)
                    continue;
                c.lastCommand = cmd;
                if (Error err = game_.clientThink(c.number, cmd))
                    return err;
            }
            break;
        } else if (op == kClcNop) {
            continue;
        } else {
            break; // the end (kClcEof isn't sent in protocol 24)
        }
    }
    return Error::success();
}

Error NetServer::clientCommand(Client& c, const std::string& text) {
    // The engine's own, then the game's.
    const std::vector<std::string> words = splitCommand(text);
    if (words.empty())
        return Error::success();
    const std::string& verb = words[0];
    if (verb == "userinfo") {
        c.userinfo = words.size() > 1 ? words[1] : std::string();
        setInfoValue(c.userinfo, "ip", c.address.toString());
        applyUserinfo(c);
        return game_.userinfoChanged(c.number, c.userinfo);
    }
    if (verb == "disconnect") {
        drop(c, "disconnected", 0);
        return Error::success();
    }
    // The pure check (cp) and downloads: not done by this server.
    if (verb == "cp" || verb == "vdr" || verb == "download" || verb == "nextdl" || verb == "stopdl" ||
        verb == "donedl")
        return Error::success();
    if (c.state != State::Active)
        return Error::success();
    return game_.clientCommand(c.number, text);
}

void NetServer::addReliable(Client& c, const std::string& text) {
    if (c.reliableSequence - c.reliableAcknowledge >= kMaxReliable) {
        drop(c, "Server command overflow", 0);
        return;
    }
    ++c.reliableSequence;
    c.reliable[size_t(c.reliableSequence & (kMaxReliable - 1))] = text;
}

void NetServer::writeReliable(Client& c, wire::MessageWriter& w) {
    for (int32_t i = c.reliableAcknowledge + 1; i <= c.reliableSequence; ++i) {
        w.byte(kSvcServerCommand);
        w.longValue(i);
        w.string(c.reliable[size_t(i & (kMaxReliable - 1))]);
    }
}

void NetServer::transmit(Client& c, const wire::MessageWriter& w) {
    for (const std::vector<uint8_t>& d : c.chan.transmit(w.bytes()))
        socket_->send(c.address, d);
}

void NetServer::sendGamestate(Client& c, uint64_t now) {
    c.state = State::Primed;
    c.gamestateMessage = c.chan.outgoingSequence() + 1;
    c.gamestateMs = now;
    c.deltaMessage = -1;
    wire::MessageWriter w;
    writeReliable(c, w);
    w.byte(kSvcGamestate);
    w.longValue(c.reliableSequence);
    for (const auto& [index, text] : game_.configstrings()) {
        if (text.empty())
            continue;
        w.byte(kSvcConfigstring);
        w.shortValue(index);
        w.bigString(text);
    }
    const std::vector<uint8_t> none;
    for (const auto& [number, state] : baselines_) {
        w.byte(kSvcBaseline);
        writeEntityDelta(w, none, state, true);
    }
    w.byte(0); // the end of the list
    w.longValue(checksumFeed_);
    transmit(c, w);
}

void NetServer::sendSnapshot(Client& c, uint64_t now) {
    const int32_t sequence = c.chan.outgoingSequence() + 1;
    Frame& frame = c.frames[size_t(sequence & (kPacketBackup - 1))];
    const GameHost::Snapshot s = game_.snapshot(c.number);
    frame = Frame{};
    frame.message = sequence;
    frame.sentMs = now;
    frame.playerState = s.playerState;
    frame.serverTime = s.serverTime;
    for (const std::vector<uint8_t>& e : s.entities) {
        int32_t number = 0;
        std::memcpy(&number, e.data(), 4);
        frame.entities[number] = e;
    }
    // A change from the snapshot it last had, if it's still kept.
    const Frame* old = nullptr;
    int32_t deltaNum = 0;
    if (c.deltaMessage > 0 && sequence - c.deltaMessage < kPacketBackup - 3) {
        const Frame& f = c.frames[size_t(c.deltaMessage & (kPacketBackup - 1))];
        if (f.message == c.deltaMessage) {
            old = &f;
            deltaNum = sequence - c.deltaMessage;
        }
    }
    wire::MessageWriter w;
    writeReliable(c, w);
    w.byte(kSvcSnapshot);
    w.longValue(c.lastClientCommand);
    w.longValue(s.serverTime);
    w.byte(uint32_t(deltaNum));
    // Snap flags: 2 not active (still loading); 4 changes with each level.
    w.byte((c.state != State::Active ? 2u : 0u) | (levelCount_ & 1 ? 4u : 0u));
    const int maskBytes = std::min(32, (game_.areaCount() + 7) / 8);
    w.byte(uint32_t(maskBytes));
    w.data(s.areaMask.data(), size_t(maskBytes));
    static const std::vector<uint8_t> none;
    writePlayerDelta(w, old ? old->playerState : none, frame.playerState);
    // The entities: the old and new lists merged by number.
    static const std::map<int32_t, std::vector<uint8_t>> noEntities;
    const auto& before = old ? old->entities : noEntities;
    auto o = before.begin();
    auto n = frame.entities.begin();
    while (o != before.end() || n != frame.entities.end()) {
        if (n != frame.entities.end() && (o == before.end() || n->first < o->first)) {
            const auto base = baselines_.find(n->first);
            writeEntityDelta(w, base != baselines_.end() ? base->second : none, n->second, true);
            ++n;
        } else if (o != before.end() && (n == frame.entities.end() || o->first < n->first)) {
            writeEntityRemoved(w, o->first);
            ++o;
        } else {
            writeEntityDelta(w, o->second, n->second, false);
            ++o;
            ++n;
        }
    }
    w.bits(uint32_t(kNoEntity), kEntityNumberBits);
    transmit(c, w);
}

void NetServer::drop(Client& c, const std::string& reason, uint64_t now) {
    if (c.state <= State::Zombie)
        return;
    // Its last message, while it's still in the game, then out.
    finalMessage(c, reason, now);
    game_.disconnectClient(c.number);
    if (print)
        print(c.name + " " + reason + "\n");
    c.state = State::Zombie;
    c.zombieSince = now;
}

Error NetServer::frame(uint64_t now) {
    lastNow_ = now;
    // Players the game dropped (kicked, a bad password...).
    const auto& drops = game_.drops();
    for (; dropsSeen_ < drops.size(); ++dropsSeen_)
        for (Client& c : clients_)
            if (c.state >= State::Connected && c.number == drops[dropsSeen_].first) {
                // As the original's engine when the game drops a player:
                // told why, and the game told they've gone (ClientDisconnect).
                drop(c, drops[dropsSeen_].second, now);
            }
    // Configstrings that changed, to those that have had a game state.
    const std::vector<std::string> changes = configstringCommands(configstrings_, game_.configstrings());
    configstrings_ = game_.configstrings();
    for (Client& c : clients_)
        if (c.state >= State::Primed)
            for (const std::string& change : changes)
                addReliable(c, change);
    // The game's commands to players.
    const auto& commands = game_.serverCommands();
    for (; serverCommandsSeen_ < commands.size(); ++serverCommandsSeen_) {
        const auto& [to, text] = commands[serverCommandsSeen_];
        for (Client& c : clients_)
            if (c.state >= State::Primed && (to == -1 || to == c.number))
                addReliable(c, text);
    }
    const uint64_t timeoutMs = uint64_t(std::max(1, cvars_.integer("sv_timeout"))) * 1000;
    const int32_t levelTime = game_.levelTime();
    for (Client& c : clients_) {
        if (c.state == State::Zombie) {
            if (now - c.zombieSince > kZombieMs)
                c.state = State::Free;
            continue;
        }
        if (c.state == State::Free)
            continue;
        if (now - c.lastReceived > timeoutMs) {
            drop(c, "timed out", now);
            continue;
        }
        // As the original: snapshots to those loading the level too
        // (flagged not active), whose clocks they set.
        if (c.state >= State::Primed && levelTime >= c.nextSnapshotTime) {
            c.nextSnapshotTime = levelTime + c.snapMs;
            sendSnapshot(c, now);
        } else if (c.state == State::Connected && now - c.gamestateMs >= 1000) {
            // Something now and then while it loads (its commands, if
            // any), so a lost game state is noticed: its acknowledgement
            // passes it.
            c.gamestateMs = now;
            wire::MessageWriter w;
            writeReliable(c, w);
            transmit(c, w);
        }
    }
    return Error::success();
}

} // namespace reluva::idtech3::eliteforce
