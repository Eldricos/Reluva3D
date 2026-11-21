// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/meeting.h"

#include <algorithm>
#include <chrono>
#include <random>

namespace reluva::net {

namespace {

// Every datagram of the meeting protocol: "RLVM", a type, its fields,
// little-endian; strings a length byte and their bytes.
enum Type : uint8_t {
    kPublish = 1, // host -> server: game, name, info
    kPublished,   // server -> host: the session's id
    kUnpublish,   // host -> server
    kList,        // anyone -> server: game
    kSessions,    // server -> asker: count, then each id, name, info
    kConnect,     // joiner -> server: session id
    kPeer,        // server -> both: connection id, the other's address, role (0: you host)
    kNoSession,   // server -> joiner: session id
    kPunch,       // side -> side, directly: connection id
    kPunchBack,   // side -> side, directly: connection id
    kRelay,       // side -> server: connection id, payload
    kRelayed,     // server -> other side: connection id, payload
    kKeep,        // side -> server: connection id (keeps it and the router's way open)
    kData,        // side -> side, directly: connection id, payload
};

constexpr uint8_t kMagic[4] = {'R', 'L', 'V', 'M'};
constexpr size_t kMaxSessions = 256;
constexpr size_t kMaxConnections = 1024;
constexpr size_t kMaxInfo = 1000;
constexpr size_t kMaxPayload = 1400;
constexpr size_t kListBytes = 1200;           // a Sessions datagram's most
constexpr uint64_t kSessionLifeMs = 15000;    // a host silent this long is gone
constexpr uint64_t kConnectionLifeMs = 60000; // a connection silent this long is gone
constexpr uint64_t kPublishEveryMs = 3000;
constexpr uint64_t kKeepEveryMs = 5000;
constexpr uint64_t kPunchEveryMs = 250;
constexpr uint64_t kPunchForMs = 5000;
constexpr uint64_t kConnectEveryMs = 1000;
constexpr uint16_t kHostSidePort = 0xFFFF; // meeting addresses: a connection seen by its host
constexpr uint16_t kJoinSidePort = 0xFFFE; // and by the one who joined

class Writer {
public:
    explicit Writer(uint8_t type) : out(std::begin(kMagic), std::end(kMagic)) { out.push_back(type); }
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        u16(uint16_t(v));
        u16(uint16_t(v >> 16));
    }
    void str(const std::string &s) {
        const size_t n = std::min<size_t>(s.size(), 255);
        u8(uint8_t(n));
        out.insert(out.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
    }
    void bytes(const std::vector<uint8_t> &b, size_t most) {
        const size_t n = std::min(b.size(), most);
        u16(uint16_t(n));
        out.insert(out.end(), b.begin(), b.begin() + std::ptrdiff_t(n));
    }
    std::vector<uint8_t> out;
};

class Reader {
public:
    explicit Reader(const std::vector<uint8_t> &d) : data(d) {}
    uint8_t u8() {
        if (at >= data.size()) {
            ok = false;
            return 0;
        }
        return data[at++];
    }
    uint16_t u16() { return uint16_t(u8() | u8() << 8); }
    uint32_t u32() { return uint32_t(u16()) | uint32_t(u16()) << 16; }
    std::string str() {
        const size_t n = u8();
        if (!ok || at + n > data.size()) {
            ok = false;
            return {};
        }
        std::string s(data.begin() + std::ptrdiff_t(at), data.begin() + std::ptrdiff_t(at + n));
        at += n;
        return s;
    }
    std::vector<uint8_t> bytes() {
        const size_t n = u16();
        if (!ok || at + n > data.size()) {
            ok = false;
            return {};
        }
        std::vector<uint8_t> b(data.begin() + std::ptrdiff_t(at), data.begin() + std::ptrdiff_t(at + n));
        at += n;
        return b;
    }
    const std::vector<uint8_t> &data;
    size_t at = 5;
    bool ok = true;
};

// The meeting protocol's type, or 0 for anything else (a game's own).
uint8_t typeOf(const std::vector<uint8_t> &d) {
    return d.size() >= 5 && std::equal(std::begin(kMagic), std::end(kMagic), d.begin()) ? d[4] : 0;
}

Address hostSideAddress(uint32_t connection) {
    return {connection & 0xFFFFFF, kHostSidePort};
}

Address joinSideAddress(uint32_t session) {
    return {session & 0xFFFFFF, kJoinSidePort};
}

} // namespace

bool isMeetingAddress(const Address &a) {
    return (a.ip >> 24) == 0 && a.ip != 0 && (a.port == kHostSidePort || a.port == kJoinSidePort);
}

// --- The server ---------------------------------------------------------

MeetingServer::MeetingServer(std::unique_ptr<UdpSocket> socket) : _socket(std::move(socket)) {
}

void MeetingServer::update(uint64_t nowMs) {
    Address from;
    std::vector<uint8_t> data;
    for (int n = 0; n < 4096 && _socket->receive(from, data); ++n)
        handle(from, data, nowMs);
    expire(nowMs);
}

void MeetingServer::handle(const Address &from, const std::vector<uint8_t> &data, uint64_t nowMs) {
    Reader r(data);
    switch (typeOf(data)) {
    case kPublish: {
        const std::string game = r.str();
        const std::string name = r.str();
        std::vector<uint8_t> info = r.bytes();
        if (!r.ok || game.empty())
            return;
        auto it = std::find_if(_sessions.begin(), _sessions.end(),
                               [&](const auto &s) { return s.second.host == from && s.second.game == game; });
        if (it == _sessions.end()) {
            if (_sessions.size() >= kMaxSessions)
                return;
            // Ids stay below 2^24, so each fits a meeting address.
            const uint32_t id = _nextSession;
            _nextSession = _nextSession % 0xFFFFFF + 1;
            it = _sessions.emplace(id, Session{id, game, {}, {}, from, 0}).first;
        }
        it->second.name = name;
        info.resize(std::min(info.size(), kMaxInfo));
        it->second.info = std::move(info);
        it->second.heardMs = nowMs;
        Writer w(kPublished);
        w.u32(it->first);
        _socket->send(from, w.out);
        // A host's word keeps its connections too.
        for (auto &[id, c] : _connections)
            if (c.host == from)
                c.heardMs = nowMs;
        return;
    }
    case kUnpublish:
        for (auto it = _sessions.begin(); it != _sessions.end();)
            it = it->second.host == from ? _sessions.erase(it) : std::next(it);
        return;
    case kList: {
        const std::string game = r.str();
        if (!r.ok)
            return;
        Writer w(kSessions);
        std::vector<const Session *> list;
        size_t bytes = w.out.size() + 1;
        for (const auto &[id, s] : _sessions) {
            const size_t size = 4 + 1 + std::min<size_t>(s.name.size(), 255) + 2 + s.info.size();
            if (s.game != game || bytes + size > kListBytes)
                continue;
            bytes += size;
            list.push_back(&s);
        }
        w.u8(uint8_t(std::min<size_t>(list.size(), 255)));
        for (const Session *s : list) {
            w.u32(s->id);
            w.str(s->name);
            w.bytes(s->info, kMaxInfo);
        }
        _socket->send(from, w.out);
        return;
    }
    case kConnect: {
        const uint32_t sessionId = r.u32();
        if (!r.ok)
            return;
        const auto session = _sessions.find(sessionId);
        if (session == _sessions.end()) {
            Writer w(kNoSession);
            w.u32(sessionId);
            _socket->send(from, w.out);
            return;
        }
        // Asked again (an answer lost): the same connection.
        auto it = std::find_if(_connections.begin(), _connections.end(),
                               [&](const auto &c) { return c.second.session == sessionId && c.second.joiner == from; });
        if (it == _connections.end()) {
            if (_connections.size() >= kMaxConnections)
                return;
            const uint32_t id = _nextConnection;
            _nextConnection = _nextConnection % 0xFFFFFF + 1;
            it = _connections.emplace(id, Connection{id, sessionId, session->second.host, from, 0, 0}).first;
        }
        it->second.heardMs = nowMs;
        const Connection &c = it->second;
        auto tell = [&](const Address &to, const Address &other, uint8_t role) {
            Writer w(kPeer);
            w.u32(c.id);
            w.u32(c.session);
            w.u32(other.ip);
            w.u16(other.port);
            w.u8(role);
            _socket->send(to, w.out);
        };
        tell(c.host, c.joiner, 0);
        tell(c.joiner, c.host, 1);
        return;
    }
    case kRelay:
    case kKeep: {
        const uint32_t id = r.u32();
        const auto it = _connections.find(id);
        if (!r.ok || it == _connections.end())
            return;
        Connection &c = it->second;
        if (from != c.host && from != c.joiner)
            return; // only its two sides use a connection
        c.heardMs = nowMs;
        if (typeOf(data) == kKeep)
            return;
        std::vector<uint8_t> payload = r.bytes();
        if (!r.ok || payload.size() > kMaxPayload)
            return;
        Writer w(kRelayed);
        w.u32(id);
        w.bytes(payload, kMaxPayload);
        _socket->send(from == c.host ? c.joiner : c.host, w.out);
        ++c.relayed;
        return;
    }
    default:
        return;
    }
}

void MeetingServer::expire(uint64_t nowMs) {
    for (auto it = _sessions.begin(); it != _sessions.end();)
        it = nowMs > it->second.heardMs + kSessionLifeMs ? _sessions.erase(it) : std::next(it);
    for (auto it = _connections.begin(); it != _connections.end();)
        it = nowMs > it->second.heardMs + kConnectionLifeMs ? _connections.erase(it) : std::next(it);
}

// --- The client side ----------------------------------------------------

struct MeetingSocket::State {
    uint32_t id = 0;
    Address address; // its meeting address
    Address peer;
    bool host = false; // this side hosts the session
    bool direct = false;
    uint64_t knownMs = 0;
    uint64_t lastPunchMs = 0;
    uint64_t lastKeepMs = 0;
};

MeetingSocket::MeetingSocket(std::unique_ptr<UdpSocket> inner, const Address &server, std::string game)
    : _inner(std::move(inner)), _server(server), _game(std::move(game)) {
}

MeetingSocket::~MeetingSocket() = default;

uint64_t MeetingSocket::now() const {
    if (_clock)
        return _clock();
    return uint64_t(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void MeetingSocket::publish(const std::string &name, const std::vector<uint8_t> &info) {
    const bool changed = !_publishing || name != _name || info != _info;
    _name = name;
    _info = info;
    _publishing = true;
    if (changed)
        _publishedMs = 0; // tell the server now
    tick();
}

void MeetingSocket::unpublish() {
    if (!_publishing)
        return;
    _publishing = false;
    _published = false;
    for (int i = 0; i < 2; ++i)
        _inner->send(_server, Writer(kUnpublish).out);
}

void MeetingSocket::requestList() {
    Writer w(kList);
    w.str(_game);
    _inner->send(_server, w.out);
}

Address MeetingSocket::connect(uint32_t id) {
    const Address address = joinSideAddress(id);
    if (!_links.count(address)) {
        _links[address] = Link{address, {}, false, false};
        _waitingFor.push_back(id);
    }
    tick();
    return address;
}

void MeetingSocket::tick() {
    const uint64_t t = now();
    if (_publishing && (!_publishedMs || t >= _publishedMs + kPublishEveryMs)) {
        _publishedMs = std::max<uint64_t>(t, 1);
        Writer w(kPublish);
        w.str(_game);
        w.str(_name);
        w.bytes(_info, kMaxInfo);
        _inner->send(_server, w.out);
    }
    if (_lastTickMs && t < _lastTickMs + 50)
        return; // the rest a few times a second is plenty
    _lastTickMs = std::max<uint64_t>(t, 1);
    if (!_waitingFor.empty() && (!_connectSentMs || t >= _connectSentMs + kConnectEveryMs)) {
        _connectSentMs = t;
        for (const uint32_t id : _waitingFor) {
            Writer w(kConnect);
            w.u32(id);
            _inner->send(_server, w.out);
        }
    }
    for (auto &[id, s] : _states) {
        // Both sides send to the other's router at once: each opens its
        // own router's way for the other's to come in (hole punching).
        if (!s->direct && t < s->knownMs + kPunchForMs && t >= s->lastPunchMs + kPunchEveryMs) {
            s->lastPunchMs = t;
            Writer w(kPunch);
            w.u32(id);
            _inner->send(s->peer, w.out);
        }
        if (t >= s->lastKeepMs + kKeepEveryMs) {
            s->lastKeepMs = t;
            Writer w(kKeep);
            w.u32(id);
            _inner->send(_server, w.out);
        }
    }
}

bool MeetingSocket::send(const Address &to, const std::vector<uint8_t> &data) {
    tick();
    if (!isMeetingAddress(to))
        return _inner->send(to, data);
    for (auto &[id, s] : _states)
        if (s->address == to) {
            Writer w(s->direct ? kData : kRelay);
            w.u32(id);
            w.bytes(data, kMaxPayload);
            return _inner->send(s->direct ? s->peer : _server, w.out);
        }
    return false; // not introduced yet: the game sends again, as it does
}

bool MeetingSocket::receive(Address &from, std::vector<uint8_t> &data) {
    tick();
    Address at;
    std::vector<uint8_t> d;
    while (_inner->receive(at, d)) {
        const uint8_t type = typeOf(d);
        if (!type) {
            from = at;
            data = std::move(d);
            return true;
        }
        Reader r(d);
        if (at == _server) {
            _heardMs = std::max<uint64_t>(now(), 1);
            if (type == kRelayed) {
                const uint32_t id = r.u32();
                std::vector<uint8_t> payload = r.bytes();
                const auto it = _states.find(id);
                if (r.ok && it != _states.end()) {
                    from = it->second->address;
                    data = std::move(payload);
                    return true;
                }
                continue;
            }
            control(d);
            continue;
        }
        // From another side, directly: its connection's, only from where
        // the server said it is.
        const uint32_t id = r.u32();
        const auto it = _states.find(id);
        if (!r.ok || it == _states.end() || at != it->second->peer)
            continue;
        State &s = *it->second;
        if (type == kPunch || type == kPunchBack) {
            if (!s.direct) {
                s.direct = true;
                _links[s.address].direct = true;
            }
            if (type == kPunch) {
                Writer w(kPunchBack);
                w.u32(id);
                _inner->send(at, w.out);
            }
            continue;
        }
        if (type == kData) {
            std::vector<uint8_t> payload = r.bytes();
            if (!r.ok)
                continue;
            from = s.address;
            data = std::move(payload);
            return true;
        }
    }
    return false;
}

void MeetingSocket::control(const std::vector<uint8_t> &data) {
    Reader r(data);
    switch (typeOf(data)) {
    case kPublished:
        r.u32();
        _published = r.ok && _publishing;
        return;
    case kSessions: {
        const size_t n = r.u8();
        std::vector<Listed> list;
        for (size_t i = 0; i < n && r.ok; ++i) {
            Listed l;
            l.id = r.u32();
            l.name = r.str();
            l.info = r.bytes();
            if (r.ok)
                list.push_back(std::move(l));
        }
        if (r.ok)
            _listed = std::move(list);
        return;
    }
    case kPeer: {
        const uint32_t id = r.u32();
        const uint32_t session = r.u32();
        Address peer;
        peer.ip = r.u32();
        peer.port = r.u16();
        const bool host = r.u8() == 0;
        if (!r.ok)
            return;
        auto &s = _states[id];
        if (!s) {
            s = std::make_unique<State>();
            s->id = id;
            s->host = host;
            s->address = host ? hostSideAddress(id) : joinSideAddress(session);
            s->knownMs = std::max<uint64_t>(now(), 1);
        }
        s->peer = peer;
        Link &l = _links[s->address];
        l.address = s->address;
        l.peer = peer;
        l.known = true;
        if (!host)
            _waitingFor.erase(std::remove(_waitingFor.begin(), _waitingFor.end(), session), _waitingFor.end());
        return;
    }
    case kNoSession: {
        const uint32_t session = r.u32();
        if (r.ok)
            _waitingFor.erase(std::remove(_waitingFor.begin(), _waitingFor.end(), session), _waitingFor.end());
        return;
    }
    default:
        return;
    }
}

} // namespace reluva::net
