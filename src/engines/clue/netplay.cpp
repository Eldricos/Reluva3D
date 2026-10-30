// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/netplay.h"

#include <algorithm>

namespace reluva::clue::netplay {

namespace {

// Every datagram begins "CLU" and the protocol's version, then its kind.
constexpr uint8_t kMagic[3] = {'C', 'L', 'U'};
constexpr uint8_t kVersion = 1;
enum Datagram : uint8_t { Ask = 1, Info, Connect, Accept, Refuse, Data, Ack, Bye };
// The messages a channel carries.
enum Message : uint8_t { LobbyState = 10, ChatLine, Take, Start, Order };
constexpr uint64_t kConnectRetryMs = 500, kAskRetryMs = 2000;
constexpr size_t kMaxText = 200;

struct Writer {
    Bytes b;
    Writer& u8(uint32_t v) { b.push_back(uint8_t(v)); return *this; }
    Writer& u16(uint32_t v) { return u8(v & 0xff).u8(v >> 8); }
    Writer& u32(uint32_t v) { return u16(v & 0xffff).u16(v >> 16); }
    Writer& str(const std::string& s) {
        const size_t n = std::min(s.size(), kMaxText);
        u8(uint32_t(n));
        b.insert(b.end(), s.begin(), s.begin() + long(n));
        return *this;
    }
    Writer& bytes(const Bytes& s) { b.insert(b.end(), s.begin(), s.end()); return *this; }
};

struct Reader {
    const Bytes& b;
    size_t at = 0;
    bool ok = true;
    uint32_t u8() {
        if (at >= b.size()) {
            ok = false;
            return 0;
        }
        return b[at++];
    }
    uint32_t u16() { const uint32_t lo = u8(); return lo | (u8() << 8); }
    uint32_t u32() { const uint32_t lo = u16(); return lo | (u16() << 16); }
    std::string str() {
        const size_t n = u8();
        if (at + n > b.size()) {
            ok = false;
            return {};
        }
        std::string s(b.begin() + long(at), b.begin() + long(at + n));
        at += n;
        return s;
    }
};

Writer datagram(Datagram kind) {
    Writer w;
    w.b.assign(kMagic, kMagic + 3);
    w.u8(kVersion).u8(kind);
    return w;
}

void writeLobby(Writer& w, const Lobby& l) {
    w.str(l.gameName).u8(l.maxHumans).u8(l.noDieRoll).u8(l.started);
    for (const Seat& s : l.seats)
        w.u8(s.kind).u8(s.level).u8(uint8_t(s.owner)).str(s.name);
}

bool readLobby(Reader& r, Lobby& l) {
    l.gameName = r.str();
    l.maxHumans = uint8_t(r.u8());
    l.noDieRoll = r.u8() != 0;
    l.started = r.u8() != 0;
    for (Seat& s : l.seats) {
        const uint32_t kind = r.u8();
        s.kind = kind <= Seat::Computer ? Seat::Kind(kind) : Seat::None;
        s.level = uint8_t(std::min<uint32_t>(r.u8(), 2));
        s.owner = int8_t(r.u8());
        s.name = r.str();
    }
    return r.ok;
}

} // namespace

// ---- Commands -----------------------------------------------------------------

Bytes encodeCommand(const Command& c) {
    Writer w;
    w.u8(c.type).u8(c.seat).u8(uint32_t(std::min<size_t>(c.args.size(), 64)));
    for (size_t i = 0; i < c.args.size() && i < 64; ++i)
        w.u16(uint16_t(c.args[i]));
    return w.b;
}

bool decodeCommand(const Bytes& b, size_t& at, Command& c) {
    Reader r{b, at};
    c.type = uint8_t(r.u8());
    c.seat = uint8_t(r.u8());
    c.args.assign(r.u8(), 0);
    for (int16_t& a : c.args)
        a = int16_t(r.u16());
    at = r.at;
    return r.ok && c.type <= Command::TakeOver && c.seat < kSeats;
}

// ---- The channel ------------------------------------------------------------------

void Channel::received(const Bytes& d, uint64_t nowMs, std::vector<Bytes>& out) {
    lastHeard_ = nowMs;
    Reader r{d, 5};
    const uint8_t kind = d.size() > 4 ? d[4] : 0;
    if (kind != Data && kind != Ack)
        return;
    uint32_t sequence = 0;
    if (kind == Data)
        sequence = r.u32();
    const uint32_t ack = r.u32();
    if (!r.ok)
        return;
    // Everything before `ack` arrived.
    while (!outgoing_.empty() && outgoing_.front().sequence < ack)
        outgoing_.pop_front();
    if (kind != Data)
        return;
    ackDue_ = true;
    if (sequence < nextReceive_)
        return; // again: already had it
    early_[sequence] = Bytes(d.begin() + long(r.at), d.end());
    for (auto it = early_.find(nextReceive_); it != early_.end(); it = early_.find(nextReceive_)) {
        out.push_back(std::move(it->second));
        early_.erase(it);
        ++nextReceive_;
    }
}

void Channel::update(uint64_t nowMs, net::UdpSocket& socket) {
    int sent = 0;
    for (Pending& p : outgoing_) {
        if (sent >= 32)
            break;
        if (p.sentAt != 0 && nowMs - p.sentAt < kResendMs)
            continue;
        Writer w = datagram(Data);
        w.u32(p.sequence).u32(nextReceive_).bytes(p.message);
        socket.send(peer_, w.b);
        p.sentAt = nowMs;
        lastSent_ = nowMs;
        ackDue_ = false;
        ++sent;
    }
    if (ackDue_ || nowMs - lastSent_ >= kKeepAliveMs) {
        Writer w = datagram(Ack);
        w.u32(nextReceive_);
        socket.send(peer_, w.b);
        lastSent_ = nowMs;
        ackDue_ = false;
    }
}

// ---- The session ------------------------------------------------------------------

std::unique_ptr<Session> Session::host(std::unique_ptr<net::UdpSocket> socket, const std::string& gameName,
                                       const std::string& playerName, int maxHumans) {
    std::unique_ptr<Session> s(new Session);
    s->socket_ = std::move(socket);
    s->host_ = true;
    s->me_ = 0;
    s->myName_ = playerName;
    s->state_ = State::Lobby;
    s->lobby_.gameName = gameName;
    s->lobby_.maxHumans = uint8_t(std::clamp(maxHumans, 2, kSeats));
    return s;
}

std::unique_ptr<Session> Session::join(std::unique_ptr<net::UdpSocket> socket, const net::Address& address,
                                       const std::string& playerName) {
    std::unique_ptr<Session> s(new Session);
    s->socket_ = std::move(socket);
    s->myName_ = playerName;
    s->hostAddress_ = address;
    s->state_ = State::Connecting;
    return s;
}

std::unique_ptr<Session> Session::browse(std::unique_ptr<net::UdpSocket> socket,
                                         const std::vector<net::Address>& askAt) {
    std::unique_ptr<Session> s(new Session);
    s->socket_ = std::move(socket);
    s->askAt_ = askAt;
    s->state_ = State::Browsing;
    return s;
}

void Session::refresh() {
    found_.clear();
    askedAt_ = 0;
}

void Session::sendTo(int peer, const Bytes& message) {
    auto it = peers_.find(peer);
    if (it != peers_.end())
        it->second.send(message);
}

void Session::broadcast(const Bytes& message) {
    for (auto& [id, channel] : peers_)
        channel.send(message);
}

void Session::sendLobby() {
    if (!host_)
        return;
    Writer w;
    w.u8(LobbyState);
    writeLobby(w, lobby_);
    broadcast(w.b);
}

void Session::update(uint64_t nowMs) {
    now_ = nowMs;
    if (!socket_)
        return;
    receive(nowMs);
    if (state_ == State::Browsing && (askedAt_ == 0 || nowMs - askedAt_ >= kAskRetryMs)) {
        for (const net::Address& a : askAt_)
            socket_->send(a, datagram(Ask).b);
        askedAt_ = nowMs;
    }
    if (state_ == State::Connecting && connectStart_ == 0)
        connectStart_ = nowMs;
    if (state_ == State::Connecting && nowMs - connectStart_ > Channel::kTimeoutMs) {
        refusal_ = "No game answered at|that address";
        state_ = State::Refused;
        return;
    }
    if (state_ == State::Connecting && (askedAt_ == 0 || nowMs - askedAt_ >= kConnectRetryMs)) {
        Writer w = datagram(Connect);
        w.str(myName_);
        socket_->send(hostAddress_, w.b);
        askedAt_ = nowMs;
    }
    for (auto it = peers_.begin(); it != peers_.end();) {
        Channel& c = it->second;
        if (c.timedOut(nowMs)) {
            const int peer = it->first;
            ++it;
            dropPeer(peer);
            continue;
        }
        c.update(nowMs, *socket_);
        ++it;
    }
}

void Session::dropPeer(int peer) {
    peers_.erase(peer);
    if (!host_) {
        state_ = State::Lost; // the host has gone
        return;
    }
    // Its players: given back in the lobby; in the game, to the computer.
    for (int s = 0; s < kSeats; ++s) {
        Seat& seat = lobby_.seats[size_t(s)];
        if (seat.kind != Seat::Human || seat.owner != peer)
            continue;
        if (lobby_.started) {
            left_.push_back(s);
            Command c;
            c.type = Command::TakeOver;
            c.seat = uint8_t(s);
            submit(c);
        } else
            seat = {};
    }
    if (!lobby_.started) {
        sendLobby();
        chat_.push_back("A player has left.");
    }
}

void Session::receive(uint64_t nowMs) {
    net::Address from;
    Bytes d;
    while (socket_->receive(from, d)) {
        if (d.size() < 5 || !std::equal(kMagic, kMagic + 3, d.begin()) || d[3] != kVersion)
            continue;
        const uint8_t kind = d[4];
        Reader r{d, 5};
        if (kind == Ask && host_) {
            Writer w = datagram(Info);
            w.str(lobby_.gameName).str(myName_).u8(uint32_t(peers_.size() + 1)).u8(lobby_.maxHumans).u8(lobby_.started);
            socket_->send(from, w.b);
        } else if (kind == Info && state_ == State::Browsing) {
            Found f;
            f.address = from;
            f.gameName = r.str();
            f.hostName = r.str();
            f.humans = int(r.u8());
            f.maxHumans = int(r.u8());
            f.started = r.u8() != 0;
            if (r.ok && std::none_of(found_.begin(), found_.end(), [&](const Found& g) { return g.address == from; }))
                found_.push_back(f);
        } else if (kind == Connect && host_) {
            const std::string name = r.str();
            const auto known = std::find_if(peers_.begin(), peers_.end(),
                                            [&](const auto& p) { return p.second.peer() == from; });
            if (known != peers_.end()) {
                Writer w = datagram(Accept); // its Accept was lost
                w.u8(uint32_t(known->first));
                socket_->send(from, w.b);
            } else if (lobby_.started || int(peers_.size()) + 1 >= lobby_.maxHumans) {
                Writer w = datagram(Refuse);
                w.str(lobby_.started ? "The game has already begun." : "The game is full.");
                socket_->send(from, w.b);
            } else {
                const int id = nextPeer_++;
                peers_.emplace(id, Channel(from)).first->second.heard(nowMs);
                Writer w = datagram(Accept);
                w.u8(uint32_t(id));
                socket_->send(from, w.b);
                chat_.push_back(name + " has joined.");
                Writer c;
                c.u8(ChatLine).str(name + " has joined.");
                broadcast(c.b);
                sendLobby();
            }
        } else if (kind == Accept && state_ == State::Connecting && from == hostAddress_) {
            me_ = int(r.u8());
            peers_.emplace(0, Channel(from)).first->second.heard(nowMs);
            state_ = State::Lobby;
        } else if (kind == Refuse && state_ == State::Connecting && from == hostAddress_) {
            refusal_ = r.str();
            state_ = State::Refused;
        } else if (kind == Bye) {
            for (auto& [id, c] : peers_)
                if (c.peer() == from) {
                    const int peer = id;
                    dropPeer(peer);
                    break;
                }
        } else if (kind == Data || kind == Ack) {
            for (auto& [id, c] : peers_)
                if (c.peer() == from) {
                    std::vector<Bytes> messages;
                    c.received(d, nowMs, messages);
                    const int peer = id;
                    for (const Bytes& m : messages)
                        handle(peer, m);
                    break;
                }
        }
    }
}

void Session::handle(int from, const Bytes& m) {
    if (m.empty())
        return;
    Reader r{m, 1};
    switch (m[0]) {
    case LobbyState:
        if (!host_) {
            Lobby l;
            if (readLobby(r, l))
                lobby_ = l;
        }
        break;
    case ChatLine: {
        const std::string text = r.str();
        if (!r.ok)
            break;
        if (host_) {
            // From a player: said by them, to everyone.
            std::string name = "?";
            for (const Seat& s : lobby_.seats)
                if (s.kind == Seat::Human && s.owner == from)
                    name = s.name;
            const std::string line = name + ": " + text;
            chat_.push_back(line);
            Writer w;
            w.u8(ChatLine).str(line);
            broadcast(w.b);
        } else
            chat_.push_back(text);
        break;
    }
    case Take:
        if (host_ && !lobby_.started) {
            const int seat = int(int8_t(r.u8()));
            const std::string name = r.str();
            if (!r.ok)
                break;
            for (Seat& s : lobby_.seats)
                if (s.kind == Seat::Human && s.owner == from)
                    s = {};
            if (seat >= 0 && seat < kSeats && lobby_.seats[size_t(seat)].kind != Seat::Human)
                lobby_.seats[size_t(seat)] = {Seat::Human, 1, int8_t(from), name};
            sendLobby();
        }
        break;
    case Start:
        if (!host_) {
            seed_ = r.u32();
            Lobby l;
            if (r.ok && readLobby(r, l)) {
                lobby_ = l;
                state_ = State::Playing;
            }
        }
        break;
    case Order: {
        Command c;
        size_t at = 1;
        if (!decodeCommand(m, at, c))
            break;
        if (host_) {
            // A player's: only for their own suspects.
            const Seat& s = lobby_.seats[c.seat];
            if (lobby_.started && s.kind == Seat::Human && s.owner == from)
                submit(c);
        } else
            commands_.push_back(c);
        break;
    }
    default: break;
    }
}

void Session::chat(const std::string& text) {
    if (text.empty())
        return;
    if (host_) {
        const std::string line = myName_ + ": " + text;
        chat_.push_back(line);
        Writer w;
        w.u8(ChatLine).str(line);
        broadcast(w.b);
    } else {
        Writer w;
        w.u8(ChatLine).str(text);
        sendTo(0, w.b);
    }
}

bool Session::nextChat(std::string& line) {
    if (chat_.empty())
        return false;
    line = chat_.front();
    chat_.pop_front();
    return true;
}

void Session::take(int seat) {
    if (lobby_.started)
        return;
    if (host_) {
        for (Seat& s : lobby_.seats)
            if (s.kind == Seat::Human && s.owner == 0)
                s = {};
        if (seat >= 0 && seat < kSeats && lobby_.seats[size_t(seat)].kind != Seat::Human)
            lobby_.seats[size_t(seat)] = {Seat::Human, 1, 0, myName_};
        sendLobby();
        return;
    }
    Writer w;
    w.u8(Take).u8(uint8_t(int8_t(seat))).str(myName_);
    sendTo(0, w.b);
}

void Session::setSeat(int seat, Seat::Kind kind, int level) {
    if (!host_ || lobby_.started || seat < 0 || seat >= kSeats)
        return;
    Seat& s = lobby_.seats[size_t(seat)];
    if (s.kind == Seat::Human && s.owner != 0)
        return; // a player's own
    if (kind == Seat::Human) {
        take(seat);
        return;
    }
    s = {kind, uint8_t(std::clamp(level, 0, 2)), -1, {}};
    sendLobby();
}

void Session::setNoDieRoll(bool on) {
    if (!host_ || lobby_.started)
        return;
    lobby_.noDieRoll = on;
    sendLobby();
}

bool Session::canStart() const {
    if (!host_ || lobby_.started)
        return false;
    int playing = 0;
    for (const Seat& s : lobby_.seats)
        playing += s.kind != Seat::None;
    // Three suspects at least; every machine that joined has one.
    for (const auto& [id, c] : peers_)
        if (std::none_of(lobby_.seats.begin(), lobby_.seats.end(),
                         [&](const Seat& s) { return s.kind == Seat::Human && s.owner == id; }))
            return false;
    return playing >= 3;
}

void Session::start(uint32_t seed) {
    if (!canStart())
        return;
    seed_ = seed;
    lobby_.started = true;
    state_ = State::Playing;
    Writer w;
    w.u8(Start).u32(seed);
    writeLobby(w, lobby_);
    broadcast(w.b);
}

void Session::submit(const Command& c) {
    if (host_) {
        // In order: for everyone, this machine too.
        commands_.push_back(c);
        Writer w;
        w.u8(Order).bytes(encodeCommand(c));
        broadcast(w.b);
    } else {
        Writer w;
        w.u8(Order).bytes(encodeCommand(c));
        sendTo(0, w.b);
    }
}

bool Session::nextCommand(Command& c) {
    if (commands_.empty())
        return false;
    c = commands_.front();
    commands_.pop_front();
    return true;
}

int Session::nextLeft() {
    if (left_.empty())
        return -1;
    const int s = left_.front();
    left_.pop_front();
    return s;
}

void Session::leave() {
    if (!socket_)
        return;
    // Three times over: one may well be lost (and if all are, the others
    // find out when it stops answering).
    for (int k = 0; k < 3; ++k)
        for (auto& [id, c] : peers_)
            socket_->send(c.peer(), datagram(Bye).b);
    peers_.clear();
    socket_.reset();
    state_ = State::Lost;
}

} // namespace reluva::clue::netplay
