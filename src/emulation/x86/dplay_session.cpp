// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/dplay_session.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

namespace reluva::x86::dplay {

namespace {

// Every datagram starts "RLDP", the protocol's version, and its kind.
constexpr uint8_t kVersion = 1;
enum Kind : uint8_t {
    kEnumRequest = 1, // application
    kEnumReply = 2,   // the session
    kData = 3,        // link, sequence, whether it ends a message, a piece of it
    kAck = 4,         // link, every piece up to this one has come
    kUnreliable = 5,  // link, a whole message
    kPing = 6,        // link
    kBye = 7,         // link: it's closed
};

// The messages on a link. A joiner asks the host for changes with the
// same messages the host tells them with.
enum Message : uint8_t {
    kJoin = 1,    // guid, password
    kCreate = 2,  // id, member
    kDestroy = 3, // id
    kAdd = 4,     // group, player
    kRemove = 5,  // group, player
    kSend = 6,    // from, to, guaranteed, data: to the host, to pass on
    kLeave = 7,
    kWelcome = 10, // node, session, members
    kRefuse = 11,
    kDeliver = 12, // from, to, guaranteed, data: from the host
    kLost = 13,    // the host has gone
};

constexpr size_t kPiece = 1024;           // a datagram's share of a long message, well inside any network's
constexpr uint64_t kResendMs = 200;       // a piece not acknowledged by then goes again
constexpr uint64_t kPingMs = 1000;        // a link quiet this long says it's still there
constexpr uint64_t kFoundForgetMs = 3000; // a session found that hasn't answered since is forgotten
constexpr uint32_t kHostNode = 1;
constexpr uint64_t kPublishMs = 2000; // a hosted session's listing renewed, with its players now

class Writer {
public:
    std::vector<uint8_t> b;
    explicit Writer(uint8_t kind) { b = {'R', 'L', 'D', 'P', kVersion, kind}; }
    Writer() = default;
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.push_back(uint8_t(v >> (8 * i)));
    }
    void bytes(const std::vector<uint8_t> &v) {
        u32(uint32_t(v.size()));
        b.insert(b.end(), v.begin(), v.end());
    }
    void str(const std::string &s) { bytes(std::vector<uint8_t>(s.begin(), s.end())); }
    void member(const Member &m) {
        u8(m.group ? 1 : 0);
        str(m.shortName);
        str(m.longName);
        bytes(m.data);
        u32(m.flags);
        u32(uint32_t(m.members.size()));
        for (uint32_t id : m.members)
            u32(id);
        u32(m.node);
    }
    void session(const SessionInfo &s) {
        str(s.guid);
        str(s.application);
        u32(s.flags);
        u32(s.maxPlayers);
        u32(s.players);
        str(s.name);
        u8(s.password.empty() ? 0 : 1);
        for (uint32_t v : s.user)
            u32(v);
    }
};

class Reader {
public:
    Reader(const std::vector<uint8_t> &data, size_t at) : _d(data), _at(at) {}
    bool ok = true;
    bool done() const { return _at >= _d.size(); }
    uint8_t u8() {
        if (_at + 1 > _d.size()) {
            ok = false;
            return 0;
        }
        return _d[_at++];
    }
    uint32_t u32() {
        if (_at + 4 > _d.size()) {
            ok = false;
            return 0;
        }
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= uint32_t(_d[_at++]) << (8 * i);
        return v;
    }
    std::vector<uint8_t> bytes() {
        const uint32_t n = u32();
        if (!ok || n > _d.size() - _at) {
            ok = false;
            return {};
        }
        std::vector<uint8_t> v(_d.begin() + long(_at), _d.begin() + long(_at + n));
        _at += n;
        return v;
    }
    std::string str() {
        const std::vector<uint8_t> v = bytes();
        return {v.begin(), v.end()};
    }
    std::vector<uint8_t> rest() {
        std::vector<uint8_t> v(_d.begin() + long(std::min(_at, _d.size())), _d.end());
        _at = _d.size();
        return v;
    }
    Member member() {
        Member m;
        m.group = u8() != 0;
        m.shortName = str();
        m.longName = str();
        m.data = bytes();
        m.flags = u32();
        const uint32_t n = u32();
        for (uint32_t i = 0; ok && i < n; ++i)
            m.members.insert(u32());
        m.node = u32();
        return m;
    }
    SessionInfo session() {
        SessionInfo s;
        s.guid = str();
        s.application = str();
        s.flags = u32();
        s.maxPlayers = u32();
        s.players = u32();
        s.name = str();
        if (u8())
            s.password = "?"; // only that there is one
        for (uint32_t &v : s.user)
            v = u32();
        return s;
    }

private:
    const std::vector<uint8_t> &_d;
    size_t _at;
};

uint32_t randomNumber() {
    static std::mutex mutex;
    static std::mt19937 engine{std::random_device{}()};
    std::lock_guard<std::mutex> lock(mutex);
    uint32_t v = 0;
    while (v == 0)
        v = uint32_t(engine());
    return v;
}

const char *const kNullGuid = "{00000000-0000-0000-0000-000000000000}";

} // namespace

// A connection between a joiner and its host, with its own numbering of
// the pieces sent: each is sent again until acknowledged, and they're put
// back in order, so a message sent on it arrives whole, once, in order.
struct Endpoint::Link {
    net::Address address;
    uint32_t id = 0;   // the joiner's choice, so an old connection's datagrams aren't taken for it
    uint32_t node = 0; // at the host: the joiner's
    uint32_t nextSeq = 1;
    struct Piece {
        std::vector<uint8_t> datagram;
        uint64_t sentMs = 0;
    };
    std::map<uint32_t, Piece> unacked;
    uint32_t received = 0; // every piece up to this one has come
    std::map<uint32_t, std::pair<bool, std::vector<uint8_t>>> early;
    std::vector<uint8_t> partial;
    uint64_t heardMs = 0;
    uint64_t sentMs = 0;
};

Endpoint::Endpoint(Transport transport) : _transport(std::move(transport)) {
    if (_transport.thread)
        _thread = std::thread([this] {
            while (true) {
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    if (_stopping)
                        return;
                }
                update();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
}

Endpoint::~Endpoint() {
    leave();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
    }
    if (_thread.joinable())
        _thread.join();
}

uint64_t Endpoint::now() const {
    if (_transport.clock)
        return _transport.clock();
    return uint64_t(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// A socket, through the meeting server if there's one; its sessions are
// listed apart from other programs'.
std::unique_ptr<net::UdpSocket> Endpoint::openSocket(uint16_t port, const std::string &application) {
    _meeting = nullptr;
    if (!_transport.open)
        return nullptr;
    std::unique_ptr<net::UdpSocket> socket = _transport.open(port);
    if (!socket || !_transport.meeting.ip)
        return socket;
    auto meeting = std::make_unique<net::MeetingSocket>(std::move(socket), _transport.meeting, "dplay " + application);
    if (_transport.clock)
        meeting->setClock(_transport.clock);
    _meeting = meeting.get();
    return meeting;
}

void Endpoint::openSearchSocket(const std::string &application) {
    if (!_socket)
        _socket = openSocket(0, application);
}

void Endpoint::sendDatagram(const net::Address &to, const std::vector<uint8_t> &data) {
    if (_socket)
        _socket->send(to, data);
}

void Endpoint::search(const std::string &application, const net::Address &where) {
    std::lock_guard<std::mutex> lock(_mutex);
    openSearchSocket(application);
    if (_meeting && _role != Role::kHost)
        _meeting->requestList();
    Writer w(kEnumRequest);
    w.str(application);
    // At the port asked for, and the transport's, where a host that wasn't
    // told one listens.
    std::vector<uint16_t> ports{where.port ? where.port : _transport.port};
    if (ports[0] != _transport.port)
        ports.push_back(_transport.port);
    for (uint16_t port : ports) {
        if (where.ip)
            sendDatagram({where.ip, port}, w.b);
        else
            for (uint32_t ip : _transport.broadcast)
                sendDatagram({ip, port}, w.b);
    }
}

std::vector<SessionInfo> Endpoint::found() {
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<SessionInfo> list;
    const uint64_t t = now();
    for (auto it = _found.begin(); it != _found.end();) {
        if (t - it->second.heardMs > kFoundForgetMs) {
            it = _found.erase(it);
        } else {
            list.push_back(it->second.info);
            ++it;
        }
    }
    return list;
}

uint64_t Endpoint::answers() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _answers;
}

Endpoint::Status Endpoint::host(SessionInfo &info, uint16_t port) {
    leave();
    std::lock_guard<std::mutex> lock(_mutex);
    _socket.reset();
    _socket = openSocket(port ? port : _transport.port, info.application);
    if (!_socket) // the port's taken: a joiner can still be told it
        _socket = openSocket(0, info.application);
    if (!_socket)
        return Status::kNoSocket;
    if (_transport.mapPort)
        _mapping = _transport.mapPort(_socket->port());
    _publishedMs = 0;
    char guid[48];
    std::snprintf(guid, sizeof guid, "{%08x-5245-4c55-%04x-%04x%08x}", randomNumber(), randomNumber() & 0xffff,
                  randomNumber() & 0xffff, randomNumber());
    info.guid = guid;
    _session = info;
    _members.clear();
    _queue.clear();
    _node = kHostNode;
    _nextId = 1;
    _nextNode = kHostNode + 1;
    _role = Role::kHost;
    return Status::kOk;
}

Endpoint::Status Endpoint::join(const std::string &guid, const std::string &password) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_session.guid == guid) {
        if (_role == Role::kJoined)
            return Status::kOk;
        if (_role == Role::kJoining)
            return Status::kConnecting;
        if (_refused) {
            _refused = false;
            _session = {};
            return Status::kRefused;
        }
    }
    auto it = _found.find(guid);
    if (it == _found.end() || !_socket)
        return Status::kNoSession;
    if (!it->second.address.ip && it->second.listed && _meeting)
        it->second.address = _meeting->connect(it->second.listed);
    closeLinks();
    _session = it->second.info;
    _members.clear();
    _queue.clear();
    _refused = false;
    _role = Role::kJoining;
    auto link = std::make_unique<Link>();
    link->address = it->second.address;
    link->id = randomNumber();
    link->heardMs = now();
    _links.push_back(std::move(link));
    Writer w;
    w.u8(kJoin);
    w.str(guid);
    w.str(password);
    sendReliable(*_links.back(), w.b);
    return Status::kConnecting;
}

void Endpoint::closeLinks() {
    for (const auto &link : _links) {
        Writer w(kBye);
        w.u32(link->id);
        // Sent more than once: nothing says it arrived, and the other end
        // would otherwise wait out the timeout.
        for (int i = 0; i < 3; ++i)
            sendDatagram(link->address, w.b);
    }
    _links.clear();
}

void Endpoint::leave() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_role == Role::kHost) {
        Writer w;
        w.u8(kLost);
        toOthers(w.b, 0);
        if (_meeting)
            _meeting->unpublish();
        _mapping.reset();
    } else if (_role == Role::kJoined && !_links.empty()) {
        Writer w;
        w.u8(kLeave);
        sendReliable(*_links.front(), w.b);
    }
    closeLinks();
    _role = Role::kNone;
    _session = {};
    _members.clear();
    _queue.clear();
    _node = 0;
}

bool Endpoint::hosting() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _role == Role::kHost;
}

uint16_t Endpoint::port() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _socket ? _socket->port() : 0;
}

SessionInfo Endpoint::current() const {
    SessionInfo s = _session;
    s.players =
        uint32_t(std::count_if(_members.begin(), _members.end(), [](const auto &m) { return !m.second.group; }));
    return s;
}

std::optional<SessionInfo> Endpoint::session() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_role == Role::kNone || _role == Role::kJoining)
        return std::nullopt;
    return current();
}

Endpoint::Status Endpoint::create(const Member &member, uint32_t &id) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_role != Role::kHost && _role != Role::kJoined)
        return Status::kNotInSession;
    // Unique without asking the host: the node's number above, a count below.
    id = (_node << 16) | (_nextId++ & 0xffff);
    Member m = member;
    m.node = _node;
    m.members.clear();
    _members[id] = m;
    Writer w;
    w.u8(kCreate);
    w.u32(id);
    w.member(m);
    if (_role == Role::kHost)
        toOthers(w.b, 0);
    else
        sendReliable(*_links.front(), w.b);
    return Status::kOk;
}

Endpoint::Status Endpoint::destroy(uint32_t id) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_role != Role::kHost && _role != Role::kJoined)
        return Status::kNotInSession;
    if (!_members.count(id))
        return Status::kInvalidMember;
    Writer w;
    w.u8(kDestroy);
    w.u32(id);
    apply(w.b, false);
    if (_role == Role::kHost)
        toOthers(w.b, 0);
    else
        sendReliable(*_links.front(), w.b);
    return Status::kOk;
}

Endpoint::Status Endpoint::membership(uint32_t group, uint32_t player, bool add) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_role != Role::kHost && _role != Role::kJoined)
        return Status::kNotInSession;
    auto g = _members.find(group);
    if (g == _members.end() || !g->second.group || !_members.count(player))
        return Status::kInvalidMember;
    Writer w;
    w.u8(add ? kAdd : kRemove);
    w.u32(group);
    w.u32(player);
    apply(w.b, false);
    if (_role == Role::kHost)
        toOthers(w.b, 0);
    else
        sendReliable(*_links.front(), w.b);
    return Status::kOk;
}

std::map<uint32_t, Member> Endpoint::members() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _members;
}

void Endpoint::deliver(uint32_t from, uint32_t to, const std::vector<uint8_t> &data, bool guaranteed,
                       uint32_t exceptNode) {
    std::set<uint32_t> targets;
    if (to == 0) {
        for (const auto &[id, m] : _members)
            if (!m.group)
                targets.insert(id);
    } else if (auto it = _members.find(to); it != _members.end()) {
        if (it->second.group)
            targets = it->second.members;
        else
            targets.insert(to);
    }
    targets.erase(from);
    for (uint32_t t : targets) {
        auto it = _members.find(t);
        if (it == _members.end() || it->second.node == exceptNode)
            continue;
        if (it->second.node == _node) {
            _queue.push_back({from, t, data, std::nullopt});
        } else if (_role == Role::kHost) {
            for (const auto &link : _links)
                if (link->node == it->second.node) {
                    Writer w;
                    w.u8(kDeliver);
                    w.u32(from);
                    w.u32(t);
                    w.u8(guaranteed ? 1 : 0);
                    w.bytes(data);
                    sendMessage(*link, w.b, guaranteed);
                }
        }
    }
}

Endpoint::Status Endpoint::send(uint32_t from, uint32_t to, const std::vector<uint8_t> &data, bool guaranteed) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_role != Role::kHost && _role != Role::kJoined)
        return Status::kNotInSession;
    if (to != 0 && !_members.count(to))
        return Status::kInvalidMember;
    if (_role == Role::kHost) {
        deliver(from, to, data, guaranteed, 0);
        return Status::kOk;
    }
    // Its own players get it here; the host passes it to the rest.
    deliver(from, to, data, guaranteed, ~0u);
    bool remote = to == 0;
    if (auto it = _members.find(to); it != _members.end())
        remote = remote || it->second.group || it->second.node != _node;
    if (!remote)
        return Status::kOk;
    Writer w;
    w.u8(kSend);
    w.u32(from);
    w.u32(to);
    w.u8(guaranteed ? 1 : 0);
    w.bytes(data);
    sendMessage(*_links.front(), w.b, guaranteed);
    return Status::kOk;
}

// A message not guaranteed goes once, as it is, unless it's too long for
// one datagram.
void Endpoint::sendMessage(Link &link, const std::vector<uint8_t> &message, bool guaranteed) {
    if (guaranteed || message.size() > kPiece) {
        sendReliable(link, message);
        return;
    }
    Writer d(kUnreliable);
    d.u32(link.id);
    d.b.insert(d.b.end(), message.begin(), message.end());
    sendDatagram(link.address, d.b);
    link.sentMs = now();
}

void Endpoint::post(Received message) {
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.push_back(std::move(message));
}

std::optional<Received> Endpoint::receive(const std::function<bool(const Received &)> &match, bool peek) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = std::find_if(_queue.begin(), _queue.end(), match);
    if (it == _queue.end())
        return std::nullopt;
    Received r = *it;
    if (!peek)
        _queue.erase(it);
    return r;
}

void Endpoint::waiting(const std::function<bool(const Received &)> &match, uint32_t &count, uint32_t &bytes) {
    std::lock_guard<std::mutex> lock(_mutex);
    count = 0;
    bytes = 0;
    for (const Received &r : _queue)
        if (match(r)) {
            ++count;
            bytes += uint32_t(r.data.size());
        }
}

void Endpoint::sendReliable(Link &link, const std::vector<uint8_t> &message) {
    size_t at = 0;
    do {
        const size_t n = std::min(kPiece, message.size() - at);
        Writer w(kData);
        w.u32(link.id);
        const uint32_t seq = link.nextSeq++;
        w.u32(seq);
        w.u8(at + n == message.size() ? 1 : 0);
        w.b.insert(w.b.end(), message.begin() + long(at), message.begin() + long(at + n));
        at += n;
        link.unacked[seq] = {w.b, now()};
        sendDatagram(link.address, w.b);
    } while (at < message.size());
    link.sentMs = now();
}

void Endpoint::toOthers(const std::vector<uint8_t> &message, uint32_t exceptNode) {
    for (const auto &link : _links)
        if (link->node != exceptNode)
            sendReliable(*link, message);
}

// Applies a change to the session's copy here, `tell`ing its own players
// as a system message; one that doesn't fit the session is let go.
void Endpoint::apply(const std::vector<uint8_t> &message, bool tell) {
    Reader r(message, 0);
    const uint8_t type = r.u8();
    const uint32_t to = [&] {
        for (const auto &[id, m] : _members)
            if (m.node == _node && !m.group)
                return id;
        return 0u;
    }();
    Event e;
    if (type == kCreate) {
        const uint32_t id = r.u32();
        Member m = r.member();
        if (!r.ok || _members.count(id))
            return;
        _members[id] = m;
        e.kind = Event::kCreated;
        e.id = id;
        e.member = m;
        e.players = current().players;
    } else if (type == kDestroy) {
        const uint32_t id = r.u32();
        auto it = _members.find(id);
        if (!r.ok || it == _members.end())
            return;
        e.kind = Event::kDestroyed;
        e.id = id;
        e.member = it->second;
        _members.erase(it);
        for (auto &[other, m] : _members)
            m.members.erase(id);
    } else if (type == kAdd || type == kRemove) {
        const uint32_t group = r.u32();
        const uint32_t player = r.u32();
        auto it = _members.find(group);
        if (!r.ok || it == _members.end() || !_members.count(player))
            return;
        if (type == kAdd)
            it->second.members.insert(player);
        else
            it->second.members.erase(player);
        e.kind = type == kAdd ? Event::kAdded : Event::kRemoved;
        e.id = group;
        e.player = player;
    } else {
        return;
    }
    if (tell)
        _queue.push_back({0, to, {}, e});
}

void Endpoint::dropNode(uint32_t node) {
    std::vector<uint32_t> ids;
    for (const auto &[id, m] : _members)
        if (m.node == node)
            ids.push_back(id);
    for (uint32_t id : ids) {
        Writer w;
        w.u8(kDestroy);
        w.u32(id);
        apply(w.b, true);
        toOthers(w.b, node);
    }
    _links.erase(std::remove_if(_links.begin(), _links.end(), [&](const auto &l) { return l->node == node; }),
                 _links.end());
}

void Endpoint::lost() {
    std::vector<uint32_t> ids;
    for (const auto &[id, m] : _members)
        if (m.node != _node)
            ids.push_back(id);
    for (uint32_t id : ids) {
        Writer w;
        w.u8(kDestroy);
        w.u32(id);
        apply(w.b, true);
    }
    Event e;
    e.kind = Event::kSessionLost;
    _queue.push_back({0, 0, {}, e});
    _links.clear();
    _role = Role::kLost;
}

void Endpoint::hostMessage(Link &link, const std::vector<uint8_t> &data) {
    Reader r(data, 0);
    const uint8_t type = r.u8();
    if (link.node == 0) {
        // A new link: it must be asking to join.
        const std::string guid = type == kJoin ? r.str() : std::string();
        const std::string password = r.str();
        const SessionInfo s = current();
        const bool full = s.maxPlayers && s.players >= s.maxPlayers;
        // DPSESSION_NEWPLAYERSDISABLED (1), DPSESSION_JOINDISABLED (0x20)
        if (!r.ok || guid != _session.guid || (_session.flags & 0x21) || full || password != _session.password) {
            Writer w;
            w.u8(kRefuse);
            sendReliable(link, w.b);
            return;
        }
        link.node = _nextNode++;
        Writer w;
        w.u8(kWelcome);
        w.u32(link.node);
        w.session(s);
        w.u32(uint32_t(_members.size()));
        for (const auto &[id, m] : _members) {
            w.u32(id);
            w.member(m);
        }
        sendReliable(link, w.b);
        return;
    }
    if (type == kCreate || type == kDestroy || type == kAdd || type == kRemove) {
        const size_t before = _queue.size();
        apply(data, true);
        if (_queue.size() != before) // it fitted: the others are told
            toOthers(data, link.node);
    } else if (type == kSend) {
        const uint32_t from = r.u32();
        const uint32_t to = r.u32();
        const bool guaranteed = r.u8() != 0;
        const std::vector<uint8_t> bytes = r.bytes();
        if (r.ok)
            deliver(from, to, bytes, guaranteed, link.node);
    } else if (type == kLeave) {
        dropNode(link.node);
    }
}

void Endpoint::joinerMessage(const std::vector<uint8_t> &data) {
    Reader r(data, 0);
    const uint8_t type = r.u8();
    if (_role == Role::kJoining) {
        if (type == kRefuse) {
            _refused = true;
            _role = Role::kNone;
            _links.clear();
        } else if (type == kWelcome) {
            _node = r.u32();
            const SessionInfo s = r.session();
            const uint32_t n = r.u32();
            std::map<uint32_t, Member> members;
            for (uint32_t i = 0; r.ok && i < n; ++i) {
                const uint32_t id = r.u32();
                members[id] = r.member();
            }
            if (!r.ok)
                return;
            _session.flags = s.flags;
            _session.maxPlayers = s.maxPlayers;
            _session.name = s.name;
            _session.user = s.user;
            _members = members;
            _nextId = 1;
            _role = Role::kJoined;
        }
        return;
    }
    if (_role != Role::kJoined)
        return;
    if (type == kCreate || type == kDestroy || type == kAdd || type == kRemove) {
        apply(data, true);
    } else if (type == kDeliver) {
        const uint32_t from = r.u32();
        const uint32_t to = r.u32();
        r.u8();
        const std::vector<uint8_t> bytes = r.bytes();
        auto it = _members.find(to);
        if (r.ok && it != _members.end() && it->second.node == _node)
            _queue.push_back({from, to, bytes, std::nullopt});
    } else if (type == kLost) {
        lost();
    }
}

void Endpoint::message(Link &link, const std::vector<uint8_t> &data) {
    if (data.empty())
        return;
    if (_role == Role::kHost)
        hostMessage(link, data);
    else
        joinerMessage(data);
}

void Endpoint::datagram(const net::Address &from, const std::vector<uint8_t> &data) {
    if (data.size() < 6 || data[0] != 'R' || data[1] != 'L' || data[2] != 'D' || data[3] != 'P' || data[4] != kVersion)
        return;
    Reader r(data, 6);
    const uint8_t kind = data[5];
    if (kind == kEnumRequest) {
        const std::string application = r.str();
        if (r.ok && _role == Role::kHost && (application == kNullGuid || application == _session.application)) {
            Writer w(kEnumReply);
            w.session(current());
            sendDatagram(from, w.b);
        }
        return;
    }
    if (kind == kEnumReply) {
        const SessionInfo s = r.session();
        if (r.ok) {
            _found[s.guid] = {s, from, now(), 0};
            ++_answers;
        }
        return;
    }
    const uint32_t id = r.u32();
    if (!r.ok)
        return;
    Link *link = nullptr;
    for (const auto &l : _links)
        if (l->id == id && l->address == from)
            link = l.get();
    if (!link) {
        // Only a host takes a new link, from a joiner's first piece.
        if (_role != Role::kHost || kind != kData)
            return;
        auto made = std::make_unique<Link>();
        made->address = from;
        made->id = id;
        link = made.get();
        _links.push_back(std::move(made));
    }
    link->heardMs = now();
    const auto alive = [&](const Link *l) {
        return std::any_of(_links.begin(), _links.end(), [&](const auto &x) { return x.get() == l; });
    };
    if (kind == kAck) {
        const uint32_t upTo = r.u32();
        link->unacked.erase(link->unacked.begin(), link->unacked.upper_bound(upTo));
    } else if (kind == kBye) {
        if (_role == Role::kHost)
            dropNode(link->node);
        else if (_role == Role::kJoined)
            lost();
        else
            _links.clear();
    } else if (kind == kUnreliable) {
        message(*link, r.rest());
    } else if (kind == kData) {
        const uint32_t seq = r.u32();
        const bool ends = r.u8() != 0;
        if (!r.ok)
            return;
        if (seq > link->received && seq - link->received < 65536)
            link->early[seq] = {ends, r.rest()};
        std::vector<std::vector<uint8_t>> messages;
        for (auto it = link->early.find(link->received + 1); it != link->early.end();
             it = link->early.find(link->received + 1)) {
            link->partial.insert(link->partial.end(), it->second.second.begin(), it->second.second.end());
            if (it->second.first) {
                messages.push_back(std::move(link->partial));
                link->partial.clear();
            }
            link->early.erase(it);
            ++link->received;
        }
        Writer ack(kAck);
        ack.u32(link->id);
        ack.u32(link->received);
        sendDatagram(link->address, ack.b);
        for (const std::vector<uint8_t> &m : messages) {
            if (!alive(link))
                break;
            message(*link, m);
        }
    }
}

// A host keeps its session listed, with its players now; a searcher
// takes what the server lists as found, to be introduced when it joins.
void Endpoint::meetingWork() {
    const uint64_t t = now();
    if (_role == Role::kHost) {
        if (!_publishedMs || t - _publishedMs >= kPublishMs) {
            Writer w;
            w.session(current());
            _meeting->publish(_session.name, w.b);
            _publishedMs = t;
        }
        return;
    }
    if (_meeting->heardFromServerMs() == _listHeardMs)
        return;
    _listHeardMs = _meeting->heardFromServerMs();
    for (const net::MeetingSocket::Listed &l : _meeting->listed()) {
        Reader r(l.info, 0);
        const SessionInfo s = r.session();
        if (!r.ok)
            continue;
        Found &f = _found[s.guid];
        // One that answered on the local network keeps that address.
        if (!f.address.ip || f.listed) {
            f.info = s;
            f.listed = l.id;
        }
        f.heardMs = t;
        ++_answers;
    }
}

void Endpoint::update() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_socket)
        return;
    net::Address from;
    std::vector<uint8_t> data;
    for (int i = 0; i < 1000 && _socket->receive(from, data); ++i)
        datagram(from, data);
    if (_meeting)
        meetingWork();
    const uint64_t t = now();
    std::vector<uint32_t> gone;
    bool hostGone = false;
    for (const auto &link : _links) {
        if (t - link->heardMs > _transport.timeoutMs) {
            if (_role == Role::kHost)
                gone.push_back(link->node);
            else
                hostGone = true;
            continue;
        }
        int resent = 0;
        for (auto &[seq, piece] : link->unacked) {
            if (resent >= 64)
                break;
            if (t - piece.sentMs >= kResendMs) {
                sendDatagram(link->address, piece.datagram);
                piece.sentMs = t;
                link->sentMs = t;
                ++resent;
            }
        }
        if (t - link->sentMs >= kPingMs) {
            Writer w(kPing);
            w.u32(link->id);
            sendDatagram(link->address, w.b);
            link->sentMs = t;
        }
    }
    for (uint32_t node : gone)
        dropNode(node);
    if (hostGone) {
        if (_role == Role::kJoining) {
            _refused = true;
            _role = Role::kNone;
            _links.clear();
        } else if (_role == Role::kJoined) {
            lost();
        }
    }
}

} // namespace reluva::x86::dplay
