// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game/netplay.h"

#include <algorithm>

namespace reluva::clue::netplay {

namespace {

// Every datagram begins "CLU" and the protocol's version, then its kind.
constexpr uint8_t kMagic[3] = {'C', 'L', 'U'};
constexpr uint8_t kVersion = 2; // 2: RoomWalk
enum Datagram : uint8_t { kAsk = 1, kInfo, kConnect, kAccept, kRefuse, kData, kAck, kBye };
// The messages a channel carries.
enum Message : uint8_t { kLobbyState = 10, kChatLine, kTake, kStart, kOrder };
constexpr uint64_t kConnectRetryMs = 500, kAskRetryMs = 2000;
constexpr size_t kMaxText = 200;

struct Writer {
    Bytes b;
    Writer &u8(uint32_t v) {
        b.push_back(uint8_t(v));
        return *this;
    }
    Writer &u16(uint32_t v) { return u8(v & 0xff).u8(v >> 8); }
    Writer &u32(uint32_t v) { return u16(v & 0xffff).u16(v >> 16); }
    Writer &str(const std::string &s) {
        const size_t n = std::min(s.size(), kMaxText);
        u8(uint32_t(n));
        b.insert(b.end(), s.begin(), s.begin() + long(n));
        return *this;
    }
    Writer &bytes(const Bytes &s) {
        b.insert(b.end(), s.begin(), s.end());
        return *this;
    }
};

struct Reader {
    const Bytes &b;
    size_t at = 0;
    bool ok = true;
    uint32_t u8() {
        if (at >= b.size()) {
            ok = false;
            return 0;
        }
        return b[at++];
    }
    uint32_t u16() {
        const uint32_t lo = u8();
        return lo | (u8() << 8);
    }
    uint32_t u32() {
        const uint32_t lo = u16();
        return lo | (u16() << 16);
    }
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

void writeLobby(Writer &w, const Lobby &l) {
    w.str(l.gameName).u8(l.maxHumans).u8(l.noDieRoll).u8(l.started);
    for (const Seat &s : l.seats)
        w.u8(s.kind).u8(s.level).u8(uint8_t(s.owner)).str(s.name);
}

bool readLobby(Reader &r, Lobby &l) {
    l.gameName = r.str();
    l.maxHumans = uint8_t(r.u8());
    l.noDieRoll = r.u8() != 0;
    l.started = r.u8() != 0;
    for (Seat &s : l.seats) {
        const uint32_t kind = r.u8();
        s.kind = kind <= Seat::kComputer ? Seat::Kind(kind) : Seat::kNone;
        s.level = uint8_t(std::min<uint32_t>(r.u8(), 2));
        s.owner = int8_t(r.u8());
        s.name = r.str();
    }
    return r.ok;
}

} // namespace

// ---- Commands -----------------------------------------------------------------

Bytes encodeCommand(const Command &c) {
    Writer w;
    w.u8(c.type).u8(c.seat).u8(uint32_t(std::min<size_t>(c.args.size(), 64)));
    for (size_t i = 0; i < c.args.size() && i < 64; ++i)
        w.u16(uint16_t(c.args[i]));
    return w.b;
}

bool decodeCommand(const Bytes &b, size_t &at, Command &c) {
    Reader r{b, at};
    c.type = uint8_t(r.u8());
    c.seat = uint8_t(r.u8());
    c.args.assign(r.u8(), 0);
    for (int16_t &a : c.args)
        a = int16_t(r.u16());
    at = r.at;
    return r.ok && c.type <= Command::kRoomWalk && c.seat < kSeats;
}

// ---- The channel ------------------------------------------------------------------

void Channel::received(const Bytes &d, uint64_t nowMs, std::vector<Bytes> &out) {
    _lastHeard = nowMs;
    Reader r{d, 5};
    const uint8_t kind = d.size() > 4 ? d[4] : 0;
    if (kind != kData && kind != kAck)
        return;
    uint32_t sequence = 0;
    if (kind == kData)
        sequence = r.u32();
    const uint32_t ack = r.u32();
    if (!r.ok)
        return;
    // Everything before `ack` arrived.
    while (!_outgoing.empty() && _outgoing.front().sequence < ack)
        _outgoing.pop_front();
    if (kind != kData)
        return;
    _ackDue = true;
    if (sequence < _nextReceive)
        return; // again: already had it
    _early[sequence] = Bytes(d.begin() + long(r.at), d.end());
    for (auto it = _early.find(_nextReceive); it != _early.end(); it = _early.find(_nextReceive)) {
        out.push_back(std::move(it->second));
        _early.erase(it);
        ++_nextReceive;
    }
}

void Channel::update(uint64_t nowMs, net::UdpSocket &socket) {
    int sent = 0;
    for (Pending &p : _outgoing) {
        if (sent >= 32)
            break;
        if (p.sentAt != 0 && nowMs - p.sentAt < kResendMs)
            continue;
        Writer w = datagram(kData);
        w.u32(p.sequence).u32(_nextReceive).bytes(p.message);
        socket.send(_peer, w.b);
        p.sentAt = nowMs;
        _lastSent = nowMs;
        _ackDue = false;
        ++sent;
    }
    if (_ackDue || nowMs - _lastSent >= kKeepAliveMs) {
        Writer w = datagram(kAck);
        w.u32(_nextReceive);
        socket.send(_peer, w.b);
        _lastSent = nowMs;
        _ackDue = false;
    }
}

// ---- The session ------------------------------------------------------------------

std::unique_ptr<Session> Session::host(std::unique_ptr<net::UdpSocket> socket, const std::string &gameName,
                                       const std::string &playerName, int maxHumans) {
    std::unique_ptr<Session> s(new Session);
    s->_socket = std::move(socket);
    s->_host = true;
    s->_me = 0;
    s->_myName = playerName;
    s->_state = State::kLobby;
    s->_lobby.gameName = gameName;
    s->_lobby.maxHumans = uint8_t(std::clamp(maxHumans, 2, kSeats));
    return s;
}

std::unique_ptr<Session> Session::join(std::unique_ptr<net::UdpSocket> socket, const net::Address &address,
                                       const std::string &playerName) {
    std::unique_ptr<Session> s(new Session);
    s->_socket = std::move(socket);
    s->_myName = playerName;
    s->_hostAddress = address;
    s->_state = State::kConnecting;
    return s;
}

std::unique_ptr<Session> Session::browse(std::unique_ptr<net::UdpSocket> socket,
                                         const std::vector<net::Address> &askAt) {
    std::unique_ptr<Session> s(new Session);
    s->_socket = std::move(socket);
    s->_askAt = askAt;
    s->_state = State::kBrowsing;
    return s;
}

void Session::refresh() {
    _found.clear();
    _askedAt = 0;
}

void Session::sendTo(int peer, const Bytes &message) {
    auto it = _peers.find(peer);
    if (it != _peers.end())
        it->second.send(message);
}

void Session::broadcast(const Bytes &message) {
    for (auto &[id, channel] : _peers)
        channel.send(message);
}

void Session::sendLobby() {
    if (!_host)
        return;
    Writer w;
    w.u8(kLobbyState);
    writeLobby(w, _lobby);
    broadcast(w.b);
}

void Session::update(uint64_t nowMs) {
    _now = nowMs;
    if (!_socket)
        return;
    receive(nowMs);
    if (_state == State::kBrowsing && (_askedAt == 0 || nowMs - _askedAt >= kAskRetryMs)) {
        for (const net::Address &a : _askAt)
            _socket->send(a, datagram(kAsk).b);
        _askedAt = nowMs;
    }
    if (_state == State::kConnecting && _connectStart == 0)
        _connectStart = nowMs;
    if (_state == State::kConnecting && nowMs - _connectStart > Channel::kTimeoutMs) {
        _refusal = "No game answered at|that address";
        _state = State::kRefused;
        return;
    }
    if (_state == State::kConnecting && (_askedAt == 0 || nowMs - _askedAt >= kConnectRetryMs)) {
        Writer w = datagram(kConnect);
        w.str(_myName);
        _socket->send(_hostAddress, w.b);
        _askedAt = nowMs;
    }
    for (auto it = _peers.begin(); it != _peers.end();) {
        Channel &c = it->second;
        if (c.timedOut(nowMs)) {
            const int peer = it->first;
            ++it;
            dropPeer(peer);
            continue;
        }
        c.update(nowMs, *_socket);
        ++it;
    }
}

void Session::dropPeer(int peer) {
    _peers.erase(peer);
    if (!_host) {
        _state = State::kLost; // the host has gone
        return;
    }
    // Its players: given back in the lobby; in the game, to the computer.
    for (int s = 0; s < kSeats; ++s) {
        Seat &seat = _lobby.seats[size_t(s)];
        if (seat.kind != Seat::kHuman || seat.owner != peer)
            continue;
        if (_lobby.started) {
            _left.push_back(s);
            Command c;
            c.type = Command::kTakeOver;
            c.seat = uint8_t(s);
            submit(c);
        } else
            seat = {};
    }
    if (!_lobby.started) {
        sendLobby();
        _chat.push_back("A player has left.");
    }
}

void Session::receive(uint64_t nowMs) {
    net::Address from;
    Bytes d;
    while (_socket->receive(from, d)) {
        if (d.size() < 5 || !std::equal(kMagic, kMagic + 3, d.begin()) || d[3] != kVersion)
            continue;
        const uint8_t kind = d[4];
        Reader r{d, 5};
        if (kind == kAsk && _host) {
            Writer w = datagram(kInfo);
            w.str(_lobby.gameName).str(_myName).u8(uint32_t(_peers.size() + 1)).u8(_lobby.maxHumans).u8(_lobby.started);
            _socket->send(from, w.b);
        } else if (kind == kInfo && _state == State::kBrowsing) {
            Found f;
            f.address = from;
            f.gameName = r.str();
            f.hostName = r.str();
            f.humans = int(r.u8());
            f.maxHumans = int(r.u8());
            f.started = r.u8() != 0;
            if (r.ok && std::none_of(_found.begin(), _found.end(), [&](const Found &g) { return g.address == from; }))
                _found.push_back(f);
        } else if (kind == kConnect && _host) {
            const std::string name = r.str();
            const auto known =
                std::find_if(_peers.begin(), _peers.end(), [&](const auto &p) { return p.second.peer() == from; });
            if (known != _peers.end()) {
                Writer w = datagram(kAccept); // its Accept was lost
                w.u8(uint32_t(known->first));
                _socket->send(from, w.b);
            } else if (_lobby.started || int(_peers.size()) + 1 >= _lobby.maxHumans) {
                Writer w = datagram(kRefuse);
                w.str(_lobby.started ? "The game has already begun." : "The game is full.");
                _socket->send(from, w.b);
            } else {
                const int id = _nextPeer++;
                _peers.emplace(id, Channel(from)).first->second.heard(nowMs);
                Writer w = datagram(kAccept);
                w.u8(uint32_t(id));
                _socket->send(from, w.b);
                _chat.push_back(name + " has joined.");
                Writer c;
                c.u8(kChatLine).str(name + " has joined.");
                broadcast(c.b);
                sendLobby();
            }
        } else if (kind == kAccept && _state == State::kConnecting && from == _hostAddress) {
            _me = int(r.u8());
            _peers.emplace(0, Channel(from)).first->second.heard(nowMs);
            _state = State::kLobby;
        } else if (kind == kRefuse && _state == State::kConnecting && from == _hostAddress) {
            _refusal = r.str();
            _state = State::kRefused;
        } else if (kind == kBye) {
            for (auto &[id, c] : _peers)
                if (c.peer() == from) {
                    const int peer = id;
                    dropPeer(peer);
                    break;
                }
        } else if (kind == kData || kind == kAck) {
            for (auto &[id, c] : _peers)
                if (c.peer() == from) {
                    std::vector<Bytes> messages;
                    c.received(d, nowMs, messages);
                    const int peer = id;
                    for (const Bytes &m : messages)
                        handle(peer, m);
                    break;
                }
        }
    }
}

void Session::handle(int from, const Bytes &m) {
    if (m.empty())
        return;
    Reader r{m, 1};
    switch (m[0]) {
    case kLobbyState:
        if (!_host) {
            Lobby l;
            if (readLobby(r, l))
                _lobby = l;
        }
        break;
    case kChatLine: {
        const std::string text = r.str();
        if (!r.ok)
            break;
        if (_host) {
            // From a player: said by them, to everyone.
            std::string name = "?";
            for (const Seat &s : _lobby.seats)
                if (s.kind == Seat::kHuman && s.owner == from)
                    name = s.name;
            const std::string line = name + ": " + text;
            _chat.push_back(line);
            Writer w;
            w.u8(kChatLine).str(line);
            broadcast(w.b);
        } else
            _chat.push_back(text);
        break;
    }
    case kTake:
        if (_host && !_lobby.started) {
            const int seat = int(int8_t(r.u8()));
            const std::string name = r.str();
            if (!r.ok)
                break;
            for (Seat &s : _lobby.seats)
                if (s.kind == Seat::kHuman && s.owner == from)
                    s = {};
            if (seat >= 0 && seat < kSeats && _lobby.seats[size_t(seat)].kind != Seat::kHuman)
                _lobby.seats[size_t(seat)] = {Seat::kHuman, 1, int8_t(from), name};
            sendLobby();
        }
        break;
    case kStart:
        if (!_host) {
            _seed = r.u32();
            Lobby l;
            if (r.ok && readLobby(r, l)) {
                _lobby = l;
                _state = State::kPlaying;
            }
        }
        break;
    case kOrder: {
        Command c;
        size_t at = 1;
        if (!decodeCommand(m, at, c))
            break;
        if (_host) {
            // A player's: only for their own suspects.
            const Seat &s = _lobby.seats[c.seat];
            if (_lobby.started && s.kind == Seat::kHuman && s.owner == from)
                submit(c);
        } else
            _commands.push_back(c);
        break;
    }
    default:
        break;
    }
}

void Session::chat(const std::string &text) {
    if (text.empty())
        return;
    if (_host) {
        const std::string line = _myName + ": " + text;
        _chat.push_back(line);
        Writer w;
        w.u8(kChatLine).str(line);
        broadcast(w.b);
    } else {
        Writer w;
        w.u8(kChatLine).str(text);
        sendTo(0, w.b);
    }
}

bool Session::nextChat(std::string &line) {
    if (_chat.empty())
        return false;
    line = _chat.front();
    _chat.pop_front();
    return true;
}

void Session::take(int seat) {
    if (_lobby.started)
        return;
    if (_host) {
        for (Seat &s : _lobby.seats)
            if (s.kind == Seat::kHuman && s.owner == 0)
                s = {};
        if (seat >= 0 && seat < kSeats && _lobby.seats[size_t(seat)].kind != Seat::kHuman)
            _lobby.seats[size_t(seat)] = {Seat::kHuman, 1, 0, _myName};
        sendLobby();
        return;
    }
    Writer w;
    w.u8(kTake).u8(uint8_t(int8_t(seat))).str(_myName);
    sendTo(0, w.b);
}

void Session::setSeat(int seat, Seat::Kind kind, int level) {
    if (!_host || _lobby.started || seat < 0 || seat >= kSeats)
        return;
    Seat &s = _lobby.seats[size_t(seat)];
    if (s.kind == Seat::kHuman && s.owner != 0)
        return; // a player's own
    if (kind == Seat::kHuman) {
        take(seat);
        return;
    }
    s = {kind, uint8_t(std::clamp(level, 0, 2)), -1, {}};
    sendLobby();
}

void Session::setNoDieRoll(bool on) {
    if (!_host || _lobby.started)
        return;
    _lobby.noDieRoll = on;
    sendLobby();
}

bool Session::canStart() const {
    if (!_host || _lobby.started)
        return false;
    int playing = 0;
    for (const Seat &s : _lobby.seats)
        playing += s.kind != Seat::kNone;
    // Three suspects at least; every machine that joined has one.
    for (const auto &[id, c] : _peers)
        if (std::none_of(_lobby.seats.begin(), _lobby.seats.end(),
                         [&](const Seat &s) { return s.kind == Seat::kHuman && s.owner == id; }))
            return false;
    return playing >= 3;
}

void Session::start(uint32_t seed) {
    if (!canStart())
        return;
    _seed = seed;
    _lobby.started = true;
    _state = State::kPlaying;
    Writer w;
    w.u8(kStart).u32(seed);
    writeLobby(w, _lobby);
    broadcast(w.b);
}

void Session::submit(const Command &c) {
    if (_host) {
        // In order: for everyone, this machine too.
        _commands.push_back(c);
        Writer w;
        w.u8(kOrder).bytes(encodeCommand(c));
        broadcast(w.b);
    } else {
        Writer w;
        w.u8(kOrder).bytes(encodeCommand(c));
        sendTo(0, w.b);
    }
}

bool Session::nextCommand(Command &c) {
    if (_commands.empty())
        return false;
    c = _commands.front();
    _commands.pop_front();
    return true;
}

int Session::nextLeft() {
    if (_left.empty())
        return -1;
    const int s = _left.front();
    _left.pop_front();
    return s;
}

void Session::leave() {
    if (!_socket)
        return;
    // Three times over: one may well be lost (and if all are, the others
    // find out when it stops answering).
    for (int k = 0; k < 3; ++k)
        for (auto &[id, c] : _peers)
            _socket->send(c.peer(), datagram(kBye).b);
    _peers.clear();
    _socket.reset();
    _state = State::kLost;
}

} // namespace reluva::clue::netplay
