// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/dplay_net.h"

#include <algorithm>
#include <cstring>

namespace reluva::x86::dplay {

namespace {

using Result = net::Sockets::Result;
using Clock = std::chrono::steady_clock;

// A datagram's first four bytes: a question, or a host's answer.
const char kQuestion[4] = {'R', 'D', 'P', 'Q'};
const char kAnswer[4] = {'R', 'D', 'P', 'S'};
// Players' IDs: the host's from kFirstId, each joining program's a block
// of 0x10000 after.
constexpr uint32_t kFirstId = 0x00100001;
constexpr uint32_t kBlock = 0x10000;
constexpr size_t kMaxFrame = 1u << 20;
// DirectPlay's errors for a join refused (MAKE_DPHRESULT's).
constexpr uint32_t kNoSessions = 0x88770000u + 220;
constexpr uint32_t kAccessDenied = 0x88770000u + 10;
constexpr uint32_t kNoConnection = 0x88770000u + 180;
constexpr uint32_t kTimeout = 0x88770000u + 290;

struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i)
            u8(uint8_t(v >> (8 * i)));
    }
    void bytes(const std::vector<uint8_t> &v) {
        u32(uint32_t(v.size()));
        b.insert(b.end(), v.begin(), v.end());
    }
    void str(const std::string &s) { bytes(std::vector<uint8_t>(s.begin(), s.end())); }
};

struct Reader {
    const std::vector<uint8_t> &b;
    size_t at = 0;
    bool bad = false;
    bool need(size_t n) {
        if (b.size() - at < n || at > b.size())
            bad = true;
        return !bad;
    }
    uint8_t u8() { return need(1) ? b[at++] : 0; }
    uint16_t u16() {
        const uint16_t lo = u8();
        return uint16_t(lo | u8() << 8);
    }
    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= uint32_t(u8()) << (8 * i);
        return v;
    }
    std::vector<uint8_t> bytes() {
        const uint32_t n = u32();
        if (n > kMaxFrame || !need(n))
            return {};
        std::vector<uint8_t> v(b.begin() + long(at), b.begin() + long(at + n));
        at += n;
        return v;
    }
    std::string str() {
        const std::vector<uint8_t> v = bytes();
        return std::string(v.begin(), v.end());
    }
};

void putSession(Writer &w, const SessionInfo &s) {
    w.str(s.instance);
    w.str(s.application);
    w.str(s.name);
    w.u8(s.hasPassword || !s.password.empty() ? 1 : 0);
    w.u32(s.flags);
    w.u32(s.maxPlayers);
    w.u32(s.currentPlayers);
    for (uint32_t u : s.user)
        w.u32(u);
}
SessionInfo getSession(Reader &r) {
    SessionInfo s;
    s.instance = r.str();
    s.application = r.str();
    s.name = r.str();
    s.hasPassword = r.u8() != 0;
    s.flags = r.u32();
    s.maxPlayers = r.u32();
    s.currentPlayers = r.u32();
    for (uint32_t &u : s.user)
        u = r.u32();
    return s;
}
void putPlayer(Writer &w, const PlayerInfo &p) {
    w.u32(p.id);
    w.u32(p.flags);
    w.str(p.shortName);
    w.str(p.longName);
    w.bytes(p.data);
}
PlayerInfo getPlayer(Reader &r) {
    PlayerInfo p;
    p.id = r.u32();
    p.flags = r.u32();
    p.shortName = r.str();
    p.longName = r.str();
    p.data = r.bytes();
    return p;
}

bool anyApplication(const std::string &g) {
    return g.empty() || g == "{00000000-0000-0000-0000-000000000000}" || g == "{}";
}

} // namespace

Node::Node(std::shared_ptr<net::Sockets> sockets, std::vector<uint32_t> broadcast, uint16_t port)
    : _net(std::move(sockets)), _broadcast(std::move(broadcast)), _port(port) {
}

Node::~Node() {
    if (!_net)
        return;
    // What's still to be sent goes first (a program leaving sends its last
    // word, then closes), for up to half a second.
    const Clock::time_point until = Clock::now() + std::chrono::milliseconds(500);
    for (auto &[i, c] : _connections)
        while (!c.out.empty() && Clock::now() < until) {
            flush(c);
            if (!c.out.empty())
                _net->ready(c.socket, 50);
        }
    for (auto &[i, c] : _connections)
        _net->close(c.socket);
    if (_udp >= 0)
        _net->close(_udp);
    if (_listen >= 0)
        _net->close(_listen);
}

bool Node::host(const SessionInfo &session) {
    if (!_net || open())
        return false;
    // Its questions answered on the port, its players joining there.
    const int udp = _net->open(net::Sockets::kDatagram);
    const int tcp = _net->open(net::Sockets::kStream);
    bool ok = udp >= 0 && tcp >= 0;
    if (ok) {
        _net->setOption(udp, net::Sockets::kBroadcast, 1);
        _net->setOption(udp, net::Sockets::kReuseAddress, 1);
        _net->setOption(tcp, net::Sockets::kReuseAddress, 1);
        ok = _net->bind(udp, {0, _port}) == Result::kOk && _net->bind(tcp, {0, _port}) == Result::kOk &&
             _net->listen(tcp, 8) == Result::kOk;
    }
    if (!ok) {
        if (udp >= 0)
            _net->close(udp);
        if (tcp >= 0)
            _net->close(tcp);
        return false;
    }
    if (_udp >= 0)
        _net->close(_udp);
    _udp = udp;
    _listen = tcp;
    _hosting = true;
    _session = session;
    _nextBlock = 1;
    return true;
}

void Node::find(const std::string &to, const std::string &application, int waitMs) {
    if (!_net)
        return;
    if (_udp < 0) {
        _udp = _net->open(net::Sockets::kDatagram);
        if (_udp < 0)
            return;
        _net->setOption(_udp, net::Sockets::kBroadcast, 1);
        _net->bind(_udp, {0, 0});
    }
    const Clock::time_point now = Clock::now();
    if (!_askedOnce || now - _asked >= std::chrono::seconds(1)) {
        _asked = now;
        _askedOnce = true;
        Writer w;
        w.b.assign(kQuestion, kQuestion + 4);
        w.str(application);
        std::vector<uint32_t> ips;
        uint32_t ip = 0;
        if (!to.empty()) {
            net::Address a;
            if (net::parseAddress(to, _port, a))
                ips.push_back(a.ip);
            else if (_net->resolve(to, ip))
                ips.push_back(ip);
        } else {
            ips = _broadcast;
            ips.push_back(0xffffffffu);
            ips.push_back(0x7f000001u); // copies on this machine
        }
        for (uint32_t target : ips)
            _net->sendTo(_udp, {target, _port}, w.b.data(), w.b.size());
    }
    const Clock::time_point until = now + std::chrono::milliseconds(std::max(waitMs, 0));
    do {
        readAnswers();
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
        if (left <= 0)
            break;
        _net->ready(_udp, int(std::min<long long>(left, 50)));
    } while (true);
    // Kept to the application asked for.
    if (!anyApplication(application))
        for (auto it = _found.begin(); it != _found.end();)
            it = it->second.info.application == application ? std::next(it) : _found.erase(it);
}

void Node::readAnswers() {
    if (_udp < 0)
        return;
    uint8_t buffer[2048];
    size_t got = 0;
    net::Address from;
    while (_net->receiveFrom(_udp, buffer, sizeof(buffer), got, from, false) == Result::kOk) {
        const std::vector<uint8_t> d(buffer, buffer + got);
        if (got < 4)
            continue;
        if (_hosting && std::memcmp(buffer, kQuestion, 4) == 0) {
            Reader r{d, 4};
            const std::string application = r.str();
            if (r.bad || (!anyApplication(application) && application != _session.application))
                continue;
            Writer w;
            w.b.assign(kAnswer, kAnswer + 4);
            w.u16(_port);
            SessionInfo s = _session;
            s.currentPlayers = uint32_t(_players.size());
            putSession(w, s);
            _net->sendTo(_udp, from, w.b.data(), w.b.size());
        } else if (std::memcmp(buffer, kAnswer, 4) == 0) {
            Reader r{d, 4};
            FoundSession f;
            f.host = {from.ip, r.u16()};
            f.info = getSession(r);
            f.heard = Clock::now();
            if (!r.bad && !f.info.instance.empty())
                _found[f.info.instance] = f;
        }
    }
}

std::vector<FoundSession> Node::found() {
    readAnswers();
    std::vector<FoundSession> out;
    const Clock::time_point now = Clock::now();
    for (auto it = _found.begin(); it != _found.end();)
        if (now - it->second.heard > std::chrono::seconds(5)) {
            it = _found.erase(it);
        } else {
            out.push_back(it->second);
            ++it;
        }
    return out;
}

uint32_t Node::join(const std::string &instance, const std::string &password, int waitMs, SessionInfo &session,
                    std::vector<PlayerInfo> &players, uint32_t &firstId) {
    const auto it = _found.find(instance);
    if (!_net || it == _found.end() || open())
        return kNoSessions;
    const int s = _net->open(net::Sockets::kStream);
    if (s < 0)
        return kNoConnection;
    _net->setOption(s, net::Sockets::kNoDelay, 1);
    const Clock::time_point until = Clock::now() + std::chrono::milliseconds(waitMs);
    auto left = [&] {
        return int(std::max<long long>(
            0, std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count()));
    };
    Result r = _net->connect(s, it->second.host);
    if (r == Result::kInProgress || r == Result::kWouldBlock) {
        const net::Sockets::Ready ready = _net->ready(s, left());
        r = ready.failed ? Result::kRefused : ready.write ? Result::kOk : Result::kWouldBlock;
    }
    if (r != Result::kOk) {
        _net->close(s);
        return r == Result::kWouldBlock ? kTimeout : kNoConnection;
    }
    const int index = _nextConnection++;
    Connection &c = _connections[index];
    c.socket = s;
    Writer w;
    w.str(instance);
    w.str(password);
    frame(c, kJoin, w.b);
    while (left() > 0) {
        flush(c);
        _net->ready(s, std::min(left(), 50));
        if (!read(c))
            break;
        Frame kind;
        std::vector<uint8_t> payload;
        if (!nextFrame(c, kind, payload))
            continue;
        if (kind != kWelcome)
            break;
        Reader rd{payload};
        const uint32_t result = rd.u32();
        if (result != 0) {
            _net->close(s);
            _connections.erase(index);
            return result;
        }
        session = getSession(rd);
        firstId = rd.u32();
        const uint32_t count = rd.u32();
        players.clear();
        for (uint32_t i = 0; i < count && !rd.bad; ++i)
            players.push_back(getPlayer(rd));
        if (rd.bad)
            break;
        c.joined = true;
        _hostConnection = index;
        _session = session;
        return 0;
    }
    _net->close(s);
    _connections.erase(index);
    return kTimeout;
}

void Node::frame(Connection &to, Frame kind, const std::vector<uint8_t> &payload) {
    Writer w;
    w.u32(uint32_t(payload.size() + 1));
    w.u8(kind);
    w.b.insert(w.b.end(), payload.begin(), payload.end());
    to.out.insert(to.out.end(), w.b.begin(), w.b.end());
    flush(to);
}

void Node::flush(Connection &c) {
    while (!c.out.empty()) {
        size_t done = 0;
        if (_net->send(c.socket, c.out.data(), c.out.size(), done) != Result::kOk || done == 0)
            return; // the rest when it can take it
        c.out.erase(c.out.begin(), c.out.begin() + long(done));
    }
}

bool Node::read(Connection &c) {
    uint8_t buffer[16384];
    while (true) {
        size_t got = 0;
        const Result r = _net->receive(c.socket, buffer, sizeof(buffer), got, false);
        if (r == Result::kWouldBlock)
            return true;
        if (r != Result::kOk || got == 0)
            return false;
        c.in.insert(c.in.end(), buffer, buffer + got);
    }
}

bool Node::nextFrame(Connection &c, Frame &kind, std::vector<uint8_t> &payload) {
    if (c.in.size() < 5)
        return false;
    const uint32_t size = uint32_t(c.in[0] | c.in[1] << 8 | c.in[2] << 16) | uint32_t(c.in[3]) << 24;
    if (size == 0 || size > kMaxFrame) {
        // Not Reluva's: nothing more is read from it.
        c.in.clear();
        kind = Frame(0);
        payload.clear();
        return true;
    }
    if (c.in.size() < 4 + size)
        return false;
    kind = Frame(c.in[4]);
    payload.assign(c.in.begin() + 5, c.in.begin() + 4 + long(size));
    c.in.erase(c.in.begin(), c.in.begin() + 4 + long(size));
    return true;
}

int Node::ownerOf(uint32_t id) const {
    const auto it = _owners.find(id);
    return it == _owners.end() ? -1 : it->second;
}

void Node::forward(Frame kind, const std::vector<uint8_t> &payload, int except) {
    for (auto &[i, c] : _connections)
        if (c.joined && i != except)
            frame(c, kind, payload);
}

void Node::addPlayer(const PlayerInfo &p) {
    Writer w;
    putPlayer(w, p);
    if (_hosting) {
        _players[p.id] = p;
        forward(kAdd, w.b, -1);
    } else if (_hostConnection >= 0) {
        frame(_connections[_hostConnection], kAdd, w.b);
    }
}

void Node::removePlayer(uint32_t id) {
    Writer w;
    w.u32(id);
    if (_hosting) {
        _players.erase(id);
        forward(kRemove, w.b, -1);
    } else if (_hostConnection >= 0) {
        frame(_connections[_hostConnection], kRemove, w.b);
    }
}

void Node::setPlayerData(uint32_t id, const std::vector<uint8_t> &data) {
    Writer w;
    w.u32(id);
    w.bytes(data);
    if (_hosting) {
        if (auto it = _players.find(id); it != _players.end())
            it->second.data = data;
        forward(kData, w.b, -1);
    } else if (_hostConnection >= 0) {
        frame(_connections[_hostConnection], kData, w.b);
    }
}

void Node::send(uint32_t from, uint32_t to, const std::vector<uint8_t> &bytes) {
    Writer w;
    w.u32(from);
    w.u32(to);
    w.bytes(bytes);
    if (_hosting) {
        if (to == 0) {
            forward(kMessage, w.b, -1);
        } else if (const int owner = ownerOf(to); owner >= 0 && _connections.count(owner)) {
            frame(_connections[owner], kMessage, w.b);
        }
    } else if (_hostConnection >= 0) {
        frame(_connections[_hostConnection], kMessage, w.b);
    }
}

void Node::setSession(const SessionInfo &session) {
    if (!_hosting)
        return;
    _session = session;
    Writer w;
    putSession(w, session);
    forward(kSession, w.b, -1);
}

std::vector<Event> Node::poll() {
    std::vector<Event> events;
    if (!_net)
        return events;
    readAnswers();
    if (_hosting) {
        int accepted = -1;
        net::Address from;
        while (_net->accept(_listen, accepted, from) == Result::kOk && accepted >= 0) {
            _net->setOption(accepted, net::Sockets::kNoDelay, 1);
            _connections[_nextConnection++].socket = accepted;
        }
    }
    std::vector<int> closed;
    for (auto &[i, c] : _connections) {
        flush(c);
        const bool alive = read(c);
        Frame kind;
        std::vector<uint8_t> payload;
        while (nextFrame(c, kind, payload))
            handle(i, kind, payload, events);
        if (!alive)
            closed.push_back(i);
    }
    for (int i : closed)
        dropConnection(i, events);
    return events;
}

void Node::handle(int index, Frame kind, const std::vector<uint8_t> &payload, std::vector<Event> &events) {
    auto it = _connections.find(index);
    if (it == _connections.end())
        return;
    Connection &c = it->second;
    Reader r{payload};
    const bool fromHost = index == _hostConnection;
    switch (kind) {
    case kJoin: {
        if (!_hosting || c.joined)
            return;
        const std::string instance = r.str();
        const std::string password = r.str();
        Writer w;
        if (instance != _session.instance) {
            w.u32(kNoSessions);
        } else if (password != _session.password) {
            w.u32(kAccessDenied);
        } else if (_session.maxPlayers && _players.size() >= _session.maxPlayers) {
            w.u32(kAccessDenied); // full
        } else {
            c.joined = true;
            c.firstId = kFirstId + kBlock * _nextBlock++;
            w.u32(0);
            SessionInfo s = _session;
            s.currentPlayers = uint32_t(_players.size());
            putSession(w, s);
            w.u32(c.firstId);
            w.u32(uint32_t(_players.size()));
            for (const auto &[id, p] : _players)
                putPlayer(w, p);
        }
        frame(c, kWelcome, w.b);
        return;
    }
    case kAdd: {
        PlayerInfo p = getPlayer(r);
        if (r.bad || (!fromHost && !_hosting) || (_hosting && !c.joined))
            return;
        if (_hosting) {
            _owners[p.id] = index;
            _players[p.id] = p;
            forward(kAdd, payload, index);
        }
        Event e;
        e.kind = Event::kPlayerAdded;
        e.player = std::move(p);
        events.push_back(std::move(e));
        return;
    }
    case kRemove: {
        const uint32_t id = r.u32();
        if (r.bad || (_hosting && ownerOf(id) != index))
            return;
        if (_hosting) {
            _owners.erase(id);
            _players.erase(id);
            forward(kRemove, payload, index);
        }
        Event e;
        e.kind = Event::kPlayerRemoved;
        e.player.id = id;
        events.push_back(std::move(e));
        return;
    }
    case kData: {
        Event e;
        e.kind = Event::kPlayerData;
        e.player.id = r.u32();
        e.player.data = r.bytes();
        if (r.bad || (_hosting && ownerOf(e.player.id) != index))
            return;
        if (_hosting) {
            _players[e.player.id].data = e.player.data;
            forward(kData, payload, index);
        }
        events.push_back(std::move(e));
        return;
    }
    case kMessage: {
        Event e;
        e.kind = Event::kMessage;
        e.from = r.u32();
        e.to = r.u32();
        e.bytes = r.bytes();
        if (r.bad || (_hosting && ownerOf(e.from) != index))
            return;
        if (_hosting) {
            // Passed on to the program whose player it's for, or to every
            // other one; the host's own players take it here.
            if (e.to == 0)
                forward(kMessage, payload, index);
            else if (const int owner = ownerOf(e.to); owner >= 0 && owner != index && _connections.count(owner))
                frame(_connections[owner], kMessage, payload);
            if (e.to != 0 && ownerOf(e.to) >= 0)
                return;
        }
        events.push_back(std::move(e));
        return;
    }
    case kSession: {
        if (!fromHost)
            return;
        Event e;
        e.kind = Event::kSession;
        e.session = getSession(r);
        if (r.bad)
            return;
        _session = e.session;
        events.push_back(std::move(e));
        return;
    }
    default:
        // Not Reluva's: nothing more from it.
        c.in.clear();
        return;
    }
}

void Node::dropConnection(int index, std::vector<Event> &events) {
    auto it = _connections.find(index);
    if (it == _connections.end())
        return;
    _net->close(it->second.socket);
    _connections.erase(it);
    if (index == _hostConnection) {
        _hostConnection = -1;
        Event e;
        e.kind = Event::kSessionLost;
        events.push_back(std::move(e));
        return;
    }
    // Its players are gone, for everyone.
    std::vector<uint32_t> gone;
    for (const auto &[id, owner] : _owners)
        if (owner == index)
            gone.push_back(id);
    for (uint32_t id : gone) {
        _owners.erase(id);
        _players.erase(id);
        Writer w;
        w.u32(id);
        forward(kRemove, w.b, -1);
        Event e;
        e.kind = Event::kPlayerRemoved;
        e.player.id = id;
        events.push_back(std::move(e));
    }
}

} // namespace reluva::x86::dplay
