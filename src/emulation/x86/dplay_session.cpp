// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/dplay_session.h"

#include "emulation/x86/dplay4_reliable.h"
#include "emulation/x86/dplay4_wire.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <random>

namespace reluva::x86::dplay {

namespace {

// Every datagram starts "RLDP", the protocol's version, and its kind.
constexpr uint8_t kVersion = 2;
enum Kind : uint8_t {
    kEnumRequest = 1, // application
    kEnumReply = 2,   // the port to join at, the session
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
    kRejoin = 8,   // guid, node: to a new host, from an endpoint already in the session
    kWelcome = 10, // node, the host's node, session, members, nodes
    kRefuse = 11,
    kDeliver = 12, // from, to, guaranteed, data: from the host
    kLost = 13,    // the host has gone
    kNode = 14,    // node, ip, port: where an endpoint is; port 0 when it's gone
};

// DPSESSION_MIGRATEHOST: when the host goes, another takes over.
constexpr uint32_t kMigrateHost = 4;
// DPSESSION_NOMESSAGEID: players' messages go without their ids.
constexpr uint32_t kNoMessageId = 8;
// DPSESSION_DIRECTPLAYPROTOCOL: everything between joined machines goes
// by the reliable protocol ([MC-DPL4R]).
constexpr uint32_t kReliableProtocol = 0x2000;

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
        closeOriginal();
    }
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
    if (_transport.tcp)
        searchOriginal(application, where);
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
    // The original's searches come to its own port.
    _idSeed = randomNumber();
    if (_transport.tcp && _socket->port() != dp4::kEnumPort && _transport.open)
        _originalEnum = _transport.open(dp4::kEnumPort);
    // The original's joiners come to its TCP port, then talk over its UDP.
    if (_transport.tcp) {
        openOriginalListen();
        openDatagramSocket();
    }
    _original = false;
    _peers.clear();
    _arriving.clear();
    _reserved.clear();
    _system = 0;
    _hostSystem = 0;
    _uniqueness = uint16_t(randomNumber());
    _publishedMs = 0;
    char guid[48];
    std::snprintf(guid, sizeof guid, "{%08x-5245-4c55-%04x-%04x%08x}", randomNumber(), randomNumber() & 0xffff,
                  randomNumber() & 0xffff, randomNumber());
    info.guid = guid;
    _session = info;
    _members.clear();
    _queue.clear();
    _node = kHostNode;
    _hostNode = kHostNode;
    _nodes.clear();
    _awaiting.clear();
    _seekingMs = 0;
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
    if (it->second.original) {
        // Its host is asked for this machine's system player, then told
        // where to find it ([MC-DPL4CS] 3.1.4.2).
        if (!_transport.tcp || !openOriginalListen() || !openDatagramSocket())
            return Status::kNoSocket;
        closeLinks();
        _session = it->second.info;
        _members.clear();
        _peers.clear();
        _queue.clear();
        _refused = false;
        _original = true;
        _role = Role::kJoining;
        _hostStream = it->second.address;
        _password = password;
        _step = Step::kAskedId;
        _stepMs = now();
        streamTo(_hostStream, dp4::requestPlayerId(_originalPort, dp4::kSystemPlayer | dp4::kLocal));
        return Status::kConnecting;
    }
    if (!it->second.address.ip && it->second.listed && _meeting)
        it->second.address = _meeting->connect(it->second.listed);
    closeLinks();
    _session = it->second.info;
    _members.clear();
    _nodes.clear();
    _seekingMs = 0;
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
    if (_original || !_peers.empty())
        leaveOriginal();
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
    _listen.reset();
    _originalEnum.reset();
    _role = Role::kNone;
    _session = {};
    _members.clear();
    _nodes.clear();
    _awaiting.clear();
    _seekingMs = 0;
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
    std::unique_lock<std::mutex> lock(_mutex);
    if (_role != Role::kHost && _role != Role::kJoined)
        return Status::kNotInSession;
    if (_original || !_peers.empty())
        return createOriginal(lock, member, id);
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
    else if (!_links.empty())
        sendReliable(*_links.front(), w.b);
    return Status::kOk;
}

Endpoint::Status Endpoint::destroy(uint32_t id) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_role != Role::kHost && _role != Role::kJoined)
        return Status::kNotInSession;
    if (!_members.count(id))
        return Status::kInvalidMember;
    if (_original || !_peers.empty()) {
        // Only its own: DirectPlay 4 has no way to destroy another's.
        if (_members[id].node != _node)
            return Status::kInvalidMember;
        _members.erase(id);
        for (auto &[system, peer] : _peers)
            toPeer(peer, dp4::deletePlayer(_originalPort, id), true);
        return Status::kOk;
    }
    Writer w;
    w.u8(kDestroy);
    w.u32(id);
    apply(w.b, false);
    if (_role == Role::kHost)
        toOthers(w.b, 0);
    else if (!_links.empty())
        sendReliable(*_links.front(), w.b);
    return Status::kOk;
}

Endpoint::Status Endpoint::membership(uint32_t group, uint32_t player, bool add) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_role != Role::kHost && _role != Role::kJoined)
        return Status::kNotInSession;
    auto g = _members.find(group);
    if (g == _members.end() || !g->second.group || !_members.count(player) || _original || !_peers.empty())
        return Status::kInvalidMember;
    Writer w;
    w.u8(add ? kAdd : kRemove);
    w.u32(group);
    w.u32(player);
    apply(w.b, false);
    if (_role == Role::kHost)
        toOthers(w.b, 0);
    else if (!_links.empty())
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
    if (_original || !_peers.empty()) {
        // Peer to peer: each player's machine sent to straight.
        std::set<uint32_t> targets;
        if (to == 0) {
            for (const auto &[id, m] : _members)
                if (!m.group)
                    targets.insert(id);
        } else if (_members[to].group) {
            targets = _members[to].members;
        } else {
            targets.insert(to);
        }
        targets.erase(from);
        const bool ids = !(_session.flags & kNoMessageId);
        for (uint32_t t : targets) {
            const uint32_t node = _members[t].node;
            if (node == _node) {
                _queue.push_back({from, t, data, std::nullopt});
                continue;
            }
            auto peer = _peers.find(node);
            if (peer == _peers.end())
                continue;
            if (peer->second.link) {
                std::vector<uint8_t> payload = data;
                // Data that starts as DirectPlay's own messages do goes wrapped.
                if (dp4::isSystem(payload)) {
                    payload = {'p', 'l', 'a', 'y', uint8_t(dp4::kDialect), 0, uint8_t(dp4::kPlayerWrapper), 0};
                    payload.insert(payload.end(), data.begin(), data.end());
                }
                peer->second.link->send(payload, guaranteed, indexOf(from), indexOf(t));
                flushLink(peer->second);
            } else if (guaranteed) {
                streamTo(peer->second.stream, dp4::playerMessage(_originalPort, from, t, data, ids));
            } else if (_datagrams) {
                _datagrams->send(peer->second.datagram, dp4::playerMessage(_originalPort, from, t, data, ids));
            }
        }
        return Status::kOk;
    }
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
    // Between hosts, still looking for the new one, it's let go.
    if (!_links.empty())
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
    _nodes.erase(node);
    _awaiting.erase(node);
    tellNode(node, {}, node);
}

void Endpoint::tellNode(uint32_t node, const net::Address &address, uint32_t exceptNode) {
    Writer w;
    w.u8(kNode);
    w.u32(node);
    w.u32(address.ip);
    w.u8(uint8_t(address.port));
    w.u8(uint8_t(address.port >> 8));
    toOthers(w.b, exceptNode);
}

// The host has gone: the session's lost unless it lets another take over.
// Then the endpoint first in the session, of those left, does: its
// players' order of joining, as DirectPlay doesn't say whom it picks.
void Endpoint::hostGone() {
    if (!(_session.flags & kMigrateHost)) {
        lost();
        return;
    }
    const uint32_t old = _hostNode;
    std::vector<uint32_t> ids;
    for (const auto &[id, m] : _members)
        if (m.node == old)
            ids.push_back(id);
    for (uint32_t id : ids) {
        Writer w;
        w.u8(kDestroy);
        w.u32(id);
        apply(w.b, true);
    }
    _nodes.erase(old);
    // Where the old host listened, for searches to find the session at.
    uint16_t oldPort = _transport.port;
    if (!_links.empty() && !net::isMeetingAddress(_links.front()->address))
        oldPort = _links.front()->address.port;
    _links.clear();
    _seekingMs = 0;
    uint32_t next = _node;
    for (const auto &[node, address] : _nodes)
        next = std::min(next, node);
    _hostNode = next;
    if (next != _node) {
        // Straight to the address the old host knew it by, or, where that
        // was one the meeting server made, through the new host's listing.
        const net::Address host = _nodes[next];
        if (host.ip && !net::isMeetingAddress(host))
            rejoin(host);
        else if (_meeting)
            _seekingMs = now();
        else
            lost();
        return;
    }
    // This one takes over, and waits for the others to come back to it.
    _role = Role::kHost;
    _awaiting.clear();
    _nextNode = _node + 1;
    for (const auto &[node, address] : _nodes) {
        _awaiting[node] = now();
        _nextNode = std::max(_nextNode, node + 1);
    }
    for (const auto &[id, m] : _members)
        _nextNode = std::max(_nextNode, m.node + 1);
    _publishedMs = 0;
    if (_transport.open && oldPort != _socket->port())
        _listen = _transport.open(oldPort);
    Event e;
    e.kind = Event::kHost;
    _queue.push_back({0, 0, {}, e});
}

// Goes back into the session through its new host.
void Endpoint::rejoin(const net::Address &host) {
    auto link = std::make_unique<Link>();
    link->address = host;
    link->id = randomNumber();
    link->heardMs = now();
    _links.push_back(std::move(link));
    Writer w;
    w.u8(kRejoin);
    w.str(_session.guid);
    w.u32(_node);
    sendReliable(*_links.back(), w.b);
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
    if (link.node == 0 && type == kRejoin) {
        // One of the session's, back after this endpoint took over.
        const std::string guid = r.str();
        const uint32_t node = r.u32();
        if (!r.ok || guid != _session.guid || !_awaiting.count(node)) {
            Writer w;
            w.u8(kRefuse);
            sendReliable(link, w.b);
            return;
        }
        _awaiting.erase(node);
        link.node = node;
        _nodes[node] = link.address;
        tellNode(node, link.address, node);
        return;
    }
    if (link.node == 0) {
        // A new link: it must be asking to join.
        const std::string guid = type == kJoin ? r.str() : std::string();
        const std::string password = r.str();
        const SessionInfo s = current();
        const bool full = s.maxPlayers && s.players >= s.maxPlayers;
        // DPSESSION_NEWPLAYERSDISABLED (1), DPSESSION_JOINDISABLED (0x20)
        // A session of the original's protocol takes no joiner of ours.
        const bool original = !_peers.empty() || !_arriving.empty();
        if (!r.ok || guid != _session.guid || (_session.flags & 0x21) || full || password != _session.password ||
            original) {
            Writer w;
            w.u8(kRefuse);
            sendReliable(link, w.b);
            return;
        }
        link.node = _nextNode++;
        Writer w;
        w.u8(kWelcome);
        w.u32(link.node);
        w.u32(_node);
        w.session(s);
        w.u32(uint32_t(_members.size()));
        for (const auto &[id, m] : _members) {
            w.u32(id);
            w.member(m);
        }
        w.u32(uint32_t(_nodes.size()));
        for (const auto &[node, address] : _nodes) {
            w.u32(node);
            w.u32(address.ip);
            w.u32(address.port);
        }
        sendReliable(link, w.b);
        _nodes[link.node] = link.address;
        tellNode(link.node, link.address, link.node);
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
            const uint32_t node = r.u32();
            const uint32_t hostNode = r.u32();
            const SessionInfo s = r.session();
            const uint32_t n = r.u32();
            std::map<uint32_t, Member> members;
            for (uint32_t i = 0; r.ok && i < n; ++i) {
                const uint32_t id = r.u32();
                members[id] = r.member();
            }
            const uint32_t nodes = r.u32();
            std::map<uint32_t, net::Address> addresses;
            for (uint32_t i = 0; r.ok && i < nodes; ++i) {
                const uint32_t other = r.u32();
                const uint32_t ip = r.u32();
                addresses[other] = {ip, uint16_t(r.u32())};
            }
            if (!r.ok)
                return;
            _node = node;
            _hostNode = hostNode;
            _nodes = addresses;
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
    if (type == kRefuse) { // a new host that didn't know it
        lost();
    } else if (type == kNode) {
        const uint32_t node = r.u32();
        const uint32_t ip = r.u32();
        const uint16_t port = uint16_t(r.u8() | r.u8() << 8);
        if (r.ok && node != _node) {
            if (port)
                _nodes[node] = {ip, port};
            else
                _nodes.erase(node);
        }
    } else if (type == kCreate || type == kDestroy || type == kAdd || type == kRemove) {
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
        hostGone();
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

void Endpoint::datagram(const net::Address &from, const std::vector<uint8_t> &data, bool listener) {
    if (originalDatagram(from, data))
        return;
    if (data.size() < 6 || data[0] != 'R' || data[1] != 'L' || data[2] != 'D' || data[3] != 'P' || data[4] != kVersion)
        return;
    Reader r(data, 6);
    const uint8_t kind = data[5];
    if (kind == kEnumRequest) {
        const std::string application = r.str();
        if (r.ok && _role == Role::kHost && (application == kNullGuid || application == _session.application)) {
            Writer w(kEnumReply);
            w.u32(_socket->port());
            w.session(current());
            if (listener && _listen)
                _listen->send(from, w.b);
            else
                sendDatagram(from, w.b);
        }
        return;
    }
    if (kind == kEnumReply) {
        const uint16_t port = uint16_t(r.u32());
        const SessionInfo s = r.session();
        if (r.ok) {
            // A meeting address is the connection's own, whatever port it was asked at.
            const net::Address at = net::isMeetingAddress(from) ? from : net::Address{from.ip, port};
            _found[s.guid] = {s, at, now(), 0};
            ++_answers;
        }
        return;
    }
    if (listener)
        return;
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
            hostGone();
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
            w.u32(_node); // which endpoint hosts it: a host that took over lists it again
            _meeting->publish(_session.name, w.b);
            _publishedMs = t;
        }
        return;
    }
    if (_seekingMs && t - _publishedMs >= kPingMs) {
        _meeting->requestList();
        _publishedMs = t;
    }
    if (_meeting->heardFromServerMs() == _listHeardMs)
        return;
    _listHeardMs = _meeting->heardFromServerMs();
    for (const net::MeetingSocket::Listed &l : _meeting->listed()) {
        Reader r(l.info, 0);
        const SessionInfo s = r.session();
        if (!r.ok)
            continue;
        if (_seekingMs && s.guid == _session.guid && r.u32() == _hostNode && r.ok) {
            // The new host's listing, not the old one's, which the server
            // may keep a while after its host went.
            _seekingMs = 0;
            rejoin(_meeting->connect(l.id));
            continue;
        }
        Found &f = _found[s.guid];
        f.original = false;
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
        datagram(from, data, false);
    for (int i = 0; _listen && i < 1000 && _listen->receive(from, data); ++i)
        datagram(from, data, true);
    for (int i = 0; _originalEnum && i < 1000 && _originalEnum->receive(from, data); ++i)
        originalDatagram(from, data);
    for (int i = 0; _datagrams && i < 1000 && _datagrams->receive(from, data); ++i)
        originalDatagramFrom(from, data);
    if (_transport.tcp) {
        originalWork();
        originalUpdate();
    }
    if (_meeting)
        meetingWork();
    const uint64_t t = now();
    std::vector<uint32_t> gone;
    bool hostLost = false;
    for (const auto &link : _links) {
        if (t - link->heardMs > _transport.timeoutMs) {
            if (_role == Role::kHost)
                gone.push_back(link->node);
            else
                hostLost = true;
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
    // A new host stops waiting for those that haven't come back.
    for (auto it = _awaiting.begin(); it != _awaiting.end();) {
        const uint32_t node = it->first;
        const bool late = t - it->second > _transport.timeoutMs;
        ++it;
        if (late)
            dropNode(node);
    }
    if (_seekingMs && t - _seekingMs > _transport.timeoutMs)
        hostLost = true;
    if (hostLost) {
        if (_role == Role::kJoining) {
            _refused = true;
            _role = Role::kNone;
            _links.clear();
        } else if (_role == Role::kJoined) {
            hostGone();
        }
    }
}

// DirectPlay 4's own search, beside ours: to its hosts' port, by UDP,
// the answers to come to a TCP port of ours in its players' range.
void Endpoint::searchOriginal(const std::string &application, const net::Address &where) {
    if (!openOriginalListen())
        return;
    dp4::EnumRequest request;
    request.application = application;
    // DPENUMSESSIONS_AVAILABLE, and DPENUMSESSIONS_PASSWORDREQUIRED: those
    // needing a password are listed, as our own search lists them.
    request.flags = 1 | 0x40;
    request.replyPort = _originalPort;
    const std::vector<uint8_t> message = dp4::enumSessions(request);
    if (where.ip)
        sendDatagram({where.ip, dp4::kEnumPort}, message);
    else
        for (uint32_t ip : _transport.broadcast)
            sendDatagram({ip, dp4::kEnumPort}, message);
}

// A search of the original's, answered by a host: its session if it's of
// that application, not full when only those with room are wanted, and
// its password given, unless those needing one are wanted too.
bool Endpoint::originalDatagram(const net::Address &from, const std::vector<uint8_t> &data) {
    if (!_transport.tcp || !dp4::command(data))
        return false;
    dp4::EnumRequest request;
    if (_role != Role::kHost || !dp4::readEnumSessions(data, request))
        return true;
    auto lower = [](std::string text) {
        for (char &c : text)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    const SessionInfo s = current();
    const bool full = s.maxPlayers && s.players >= s.maxPlayers;
    const bool application = request.application == kNullGuid || lower(request.application) == lower(s.application);
    const bool password = s.password.empty() || (request.flags & 0x40) || request.password == s.password;
    if (!application || !password || ((request.flags & 1) && !(request.flags & 2) && full))
        return true;
    Stream answer;
    answer.socket = _transport.tcp->open(net::Sockets::kStream);
    if (answer.socket < 0)
        return true;
    answer.peer = {request.replyIp ? request.replyIp : from.ip, request.replyPort};
    if (!openOriginalListen()) {
        _transport.tcp->close(answer.socket);
        return true;
    }
    // Its header says where the joiner then comes: our TCP port.
    answer.pending = dp4::enumSessionsReply(s, _originalPort, _idSeed);
    answer.connecting = true;
    answer.outgoing = true;
    answer.closeWhenSent = true;
    answer.sinceMs = now();
    const net::Sockets::Result r = _transport.tcp->connect(answer.socket, answer.peer);
    if (r != net::Sockets::kOk && r != net::Sockets::kInProgress && r != net::Sockets::kWouldBlock) {
        _transport.tcp->close(answer.socket);
        return true;
    }
    _streams.push_back(std::move(answer));
    return true;
}

// The connections of DirectPlay 4's: what's waiting sent once connected
// (an answer to a search, then closed), and what comes in read: answers
// to our searches found, the rest taken as the session's messages. One
// gone quiet a while is let go, unless it's one of the session's.
void Endpoint::originalWork() {
    constexpr uint64_t kStreamMs = 10000;
    net::Sockets &tcp = *_transport.tcp;
    if (_originalListen >= 0) {
        int accepted = -1;
        net::Address peer;
        while (tcp.accept(_originalListen, accepted, peer) == net::Sockets::kOk && accepted >= 0) {
            Stream in;
            in.socket = accepted;
            in.peer = peer;
            in.sinceMs = now();
            _streams.push_back(std::move(in));
        }
    }
    const uint64_t t = now();
    std::vector<std::pair<std::vector<uint8_t>, uint32_t>> messages;
    for (auto it = _streams.begin(); it != _streams.end();) {
        Stream &st = *it;
        const bool kept = _original && !st.closeWhenSent;
        bool done = !kept && t - st.sinceMs > kStreamMs;
        const net::Sockets::Ready ready = tcp.ready(st.socket);
        if (ready.failed)
            done = true;
        if (!done && st.connecting && ready.write)
            st.connecting = false;
        if (!done && !st.connecting && !st.pending.empty()) {
            size_t sent = 0;
            const net::Sockets::Result r = tcp.send(st.socket, st.pending.data(), st.pending.size(), sent);
            if (r == net::Sockets::kOk)
                st.pending.erase(st.pending.begin(), st.pending.begin() + long(sent));
            else if (r != net::Sockets::kWouldBlock)
                done = true;
            if (st.pending.empty() && st.closeWhenSent)
                done = true; // an answer is all there is to say
        }
        if (!done && ready.read) {
            uint8_t buffer[4096];
            size_t got = 0;
            const net::Sockets::Result r = tcp.receive(st.socket, buffer, sizeof buffer, got, false);
            if (r == net::Sockets::kOk && got == 0)
                done = true; // the other end has finished
            else if (r == net::Sockets::kOk)
                st.received.insert(st.received.end(), buffer, buffer + got);
            else if (r != net::Sockets::kWouldBlock)
                done = true;
            st.sinceMs = t;
        }
        for (int length = dp4::messageLength(st.received); length > 0; length = dp4::messageLength(st.received)) {
            messages.emplace_back(std::vector<uint8_t>(st.received.begin(), st.received.begin() + length), st.peer.ip);
            st.received.erase(st.received.begin(), st.received.begin() + length);
        }
        if (dp4::messageLength(st.received) < 0)
            done = true;
        if (done) {
            tcp.close(st.socket);
            it = _streams.erase(it);
        } else {
            ++it;
        }
    }
    // Taken once the streams are gone through, as what they do may add one.
    for (const auto &[message, ip] : messages) {
        SessionInfo found;
        if (dp4::readEnumSessionsReply(message, found)) {
            // A Reluva host answers both ways: its own answer is the one to join by.
            const auto known = _found.find(found.guid);
            const bool ours = known != _found.end() && !known->second.original;
            if (!ours) {
                Found &f = _found[found.guid];
                f.info = found;
                f.address = {ip, dp4::senderPort(message)};
                f.heardMs = t;
                f.original = true;
                ++_answers;
            }
            continue;
        }
        Peer *peer = nullptr;
        for (auto &[system, p] : _peers)
            if (p.stream.ip == ip && p.stream.port == dp4::senderPort(message))
                peer = &p;
        if (peer)
            peer->heardMs = t;
        if (dp4::isSystem(message) && dp4::command(message) != dp4::kPlayerWrapper) {
            originalMessage(message, ip, peer);
            continue;
        }
        uint32_t from = 0;
        uint32_t to = 0;
        std::vector<uint8_t> bytes;
        if (_original && dp4::readPlayerMessage(message, !(_session.flags & kNoMessageId), from, to, bytes))
            _queue.push_back({from, to, bytes, std::nullopt});
    }
}

bool Endpoint::openOriginalListen() {
    if (_originalListen >= 0)
        return true;
    if (!_transport.tcp)
        return false;
    net::Sockets &tcp = *_transport.tcp;
    _originalListen = tcp.open(net::Sockets::kStream);
    for (uint16_t port = dp4::kFirstPort; _originalListen >= 0 && port <= dp4::kLastPort; ++port)
        if (tcp.bind(_originalListen, {0, port}) == net::Sockets::kOk) {
            _originalPort = port;
            break;
        }
    if (!_originalPort || tcp.listen(_originalListen, 8) != net::Sockets::kOk) {
        if (_originalListen >= 0)
            tcp.close(_originalListen);
        _originalListen = -1;
        _originalPort = 0;
        return false;
    }
    return true;
}

// The machine's UDP, in the range DirectPlay 4's players use
// ([MC-DPL4CS] 2.1).
bool Endpoint::openDatagramSocket() {
    for (uint16_t port = dp4::kFirstPort; !_datagrams && _transport.open && port <= dp4::kLastPort; ++port)
        _datagrams = _transport.open(port);
    return _datagrams != nullptr;
}

// DirectPlay's Winsock provider sends on a connection of its own to the
// other's TCP port, kept for what follows.
void Endpoint::streamTo(const net::Address &to, const std::vector<uint8_t> &message) {
    for (Stream &st : _streams)
        if (st.outgoing && !st.closeWhenSent && st.peer == to) {
            st.pending.insert(st.pending.end(), message.begin(), message.end());
            return;
        }
    Stream st;
    st.socket = _transport.tcp->open(net::Sockets::kStream);
    if (st.socket < 0)
        return;
    st.peer = to;
    st.outgoing = true;
    st.connecting = true;
    st.sinceMs = now();
    st.pending = message;
    _transport.tcp->setOption(st.socket, net::Sockets::kNoDelay, 1);
    const net::Sockets::Result r = _transport.tcp->connect(st.socket, to);
    if (r != net::Sockets::kOk && r != net::Sockets::kInProgress && r != net::Sockets::kWouldBlock) {
        _transport.tcp->close(st.socket);
        return;
    }
    _streams.push_back(std::move(st));
}

// What's waiting goes now, as far as the connections take it, for a
// machine about to close them.
void Endpoint::flushStreams() {
    for (int tries = 0; tries < 50; ++tries) {
        bool waiting = false;
        for (Stream &st : _streams) {
            if (st.pending.empty())
                continue;
            const net::Sockets::Ready ready = _transport.tcp->ready(st.socket, tries ? 10 : 0);
            if (ready.failed)
                continue;
            if (st.connecting && ready.write)
                st.connecting = false;
            waiting = true;
            if (st.connecting)
                continue;
            size_t sent = 0;
            if (_transport.tcp->send(st.socket, st.pending.data(), st.pending.size(), sent) == net::Sockets::kOk)
                st.pending.erase(st.pending.begin(), st.pending.begin() + long(sent));
        }
        if (!waiting)
            return;
    }
}

uint16_t Endpoint::indexOf(uint32_t id) const {
    return uint16_t((id ^ _idSeed) & 0xffff);
}

uint32_t Endpoint::idAt(uint16_t index) const {
    if (indexOf(_system) == index)
        return _system;
    for (const auto &[id, m] : _members)
        if (indexOf(id) == index)
            return id;
    for (const auto &[system, peer] : _peers)
        if (indexOf(system) == index)
            return system;
    return 0;
}

// A new id, as a DirectPlay 4 host makes them: an index no other id has
// below, a count above, the whole masked with the session's seed
// ([MC-DPL4CS] 3.2.5.4).
uint32_t Endpoint::newId() {
    std::set<uint16_t> used{indexOf(_system)};
    for (const auto &[id, m] : _members)
        used.insert(indexOf(id));
    for (const auto &[system, peer] : _peers)
        used.insert(indexOf(system));
    for (uint32_t id : _reserved)
        used.insert(indexOf(id));
    uint16_t index = 0;
    while (used.count(index) && index < 0xffff)
        ++index;
    return (uint32_t(++_uniqueness) << 16 | index) ^ _idSeed;
}

// A member's machine as DirectPlay 4 names it: its system player. A
// host's own members are its node's.
uint32_t Endpoint::systemOf(uint32_t node) const {
    return node == _node ? _system : node;
}

dp4::Player Endpoint::record(uint32_t id, const Member &member) const {
    dp4::Player p;
    p.id = id;
    p.shortName = member.shortName;
    p.longName = member.longName;
    p.data = member.data;
    p.systemPlayer = systemOf(member.node);
    if (member.group)
        p.players.assign(member.members.begin(), member.members.end());
    return p;
}

// This machine's system player: its addresses 0.0.0.0, the others taking
// the one it's heard from ([MC-DPL4CS] 2.2.2).
dp4::Player Endpoint::ourSystem(bool nameServer) const {
    dp4::Player p;
    p.id = _system;
    p.systemPlayer = _system;
    p.flags = dp4::kSystemPlayer | dp4::kLocal | (nameServer ? uint32_t(dp4::kNameServer) : 0u);
    p.addresses = true;
    p.stream = {0, _originalPort};
    p.datagram = {0, _datagrams ? _datagrams->port() : uint16_t(0)};
    return p;
}

Endpoint::Peer *Endpoint::peerAt(const net::Address &datagram) {
    for (auto &[system, peer] : _peers)
        if (peer.datagram == datagram)
            return &peer;
    return nullptr;
}

// A machine joined to the session; its addresses' 0.0.0.0 is the one it
// was heard from.
Endpoint::Peer &Endpoint::addPeer(const dp4::Player &system, uint32_t fromIp, uint16_t streamPort) {
    Peer &peer = _peers[system.id];
    peer.system = system.id;
    peer.stream = {system.stream.ip ? system.stream.ip : fromIp, streamPort ? streamPort : system.stream.port};
    peer.datagram = {system.datagram.ip ? system.datagram.ip : fromIp, system.datagram.port};
    peer.heardMs = now();
    if ((_session.flags & kReliableProtocol) && !peer.link)
        peer.link = std::make_shared<dp4::ReliableLink>([this] { return now(); });
    return peer;
}

// A machine gone: its players with it. The host gone, the session is
// lost: DirectPlay 4's way of another taking over isn't followed.
void Endpoint::dropPeer(uint32_t system) {
    std::vector<uint32_t> ids;
    for (const auto &[id, m] : _members)
        if (m.node == system)
            ids.push_back(id);
    for (uint32_t id : ids) {
        Writer w;
        w.u8(kDestroy);
        w.u32(id);
        apply(w.b, true);
    }
    _peers.erase(system);
    _arriving.erase(system);
    for (auto &[joining, arriving] : _arriving)
        arriving.awaiting.erase(system);
    if (system == _hostSystem && _role != Role::kHost && _role != Role::kNone)
        lost();
}

// Between joined machines, by the reliable protocol where the session
// uses it (without the header's size and address), else guaranteed by
// TCP and the rest by UDP.
void Endpoint::toPeer(Peer &peer, const std::vector<uint8_t> &message, bool guaranteed) {
    if (peer.link) {
        peer.link->send(dp4::bare(message), guaranteed, indexOf(_system), indexOf(peer.system));
        flushLink(peer);
    } else if (guaranteed || !_datagrams) {
        streamTo(peer.stream, message);
    } else {
        _datagrams->send(peer.datagram, message);
    }
}

void Endpoint::flushLink(Peer &peer) {
    for (const std::vector<uint8_t> &frame : peer.link->takeFrames())
        if (_datagrams)
            _datagrams->send(peer.datagram, frame);
}

void Endpoint::originalDatagramFrom(const net::Address &from, const std::vector<uint8_t> &data) {
    Peer *peer = peerAt(from);
    if (peer)
        peer->heardMs = now();
    if (peer && peer->link) {
        peer->link->receive(data);
        const uint32_t system = peer->system;
        flushLink(*peer);
        while (true) {
            auto it = _peers.find(system);
            if (it == _peers.end())
                break;
            auto delivered = it->second.link->take();
            if (!delivered)
                break;
            originalDelivered(it->second, delivered->from, delivered->to, delivered->data);
        }
        return;
    }
    if (dp4::isSystem(data) && dp4::command(data) != dp4::kPlayerWrapper) {
        originalMessage(data, from.ip, peer);
        return;
    }
    uint32_t sender = 0;
    uint32_t to = 0;
    std::vector<uint8_t> bytes;
    if (dp4::readPlayerMessage(data, !(_session.flags & kNoMessageId), sender, to, bytes))
        _queue.push_back({sender, to, bytes, std::nullopt});
}

// A message the reliable protocol has brought whole: one of DirectPlay's,
// or a player's, its players' indexes in the frames.
void Endpoint::originalDelivered(Peer &peer, uint16_t from, uint16_t to, const std::vector<uint8_t> &data) {
    if (dp4::isSystem(data)) {
        if (dp4::command(data) != dp4::kPlayerWrapper) {
            originalMessage(data, peer.datagram.ip, &peer);
            return;
        }
        _queue.push_back({idAt(from), idAt(to), {data.begin() + 8, data.end()}, std::nullopt});
        return;
    }
    _queue.push_back({idAt(from), idAt(to), data, std::nullopt});
}

// DirectPlay 4's messages, whichever way they came, `peer` the machine
// they're from when it's known.
void Endpoint::originalMessage(const std::vector<uint8_t> &message, uint32_t fromIp, Peer *peer) {
    const std::optional<uint16_t> command = dp4::command(message);
    if (!command)
        return;
    const net::Address replyTo{fromIp, dp4::senderPort(message)};
    const auto reply = [&](const std::vector<uint8_t> &answer) {
        if (peer)
            toPeer(*peer, answer, true);
        else
            streamTo(replyTo, answer);
    };
    switch (*command) {
    case dp4::kRequestPlayerId: {
        uint32_t flags = 0;
        if (_role != Role::kHost || !dp4::readRequestPlayerId(message, flags))
            return;
        const SessionInfo s = current();
        const bool full = s.maxPlayers && s.players >= s.maxPlayers;
        // DPSESSION_NEWPLAYERSDISABLED (1); joining, DPSESSION_JOINDISABLED
        // (0x20) too, and a session of Reluva's own joiners.
        const bool system = flags & dp4::kSystemPlayer;
        const bool closed = (s.flags & 1) || (system && ((s.flags & 0x20) || !_links.empty()));
        if (full || closed) {
            reply(dp4::requestPlayerReply(_originalPort, 0, dp4::kNoNewPlayers));
            return;
        }
        if (!_system) // the host's own, when the first machine comes
            _system = newId();
        const uint32_t id = newId();
        if (system)
            _reserved.insert(id);
        reply(dp4::requestPlayerReply(_originalPort, id, 0));
        return;
    }
    case dp4::kRequestPlayerReply: {
        uint32_t id = 0;
        uint32_t result = 0;
        if (!dp4::readRequestPlayerReply(message, id, result))
            return;
        if (_role == Role::kJoining && _step == Step::kAskedId) {
            if (result || !id) {
                _refused = true;
                _role = Role::kNone;
                _step = Step::kNone;
                return;
            }
            _system = id;
            _node = id;
            _step = Step::kForwarded;
            _stepMs = now();
            streamTo(_hostStream,
                     dp4::addForwardRequest(_originalPort, 0, ourSystem(false), _password, uint32_t(now())));
        } else if (_asking) {
            _granted = result ? 0 : id;
            _asking = false;
        }
        return;
    }
    case dp4::kAddForwardRequest: {
        dp4::Player system;
        std::string password;
        if (_role != Role::kHost || !dp4::readAddForwardRequest(message, system, password) ||
            !_reserved.count(system.id))
            return;
        _reserved.erase(system.id);
        if (password != _session.password) {
            streamTo(replyTo, dp4::addForwardReply(_originalPort, dp4::kNoNewPlayers));
            return;
        }
        // The others told of it, its answer once they've all said they
        // have ([MC-DPL4CS] 3.2.5.5).
        Arriving arriving;
        arriving.stream = replyTo;
        arriving.sinceMs = now();
        for (auto &[other, p] : _peers)
            arriving.awaiting.insert(other);
        Peer &added = addPeer(system, fromIp, replyTo.port);
        dp4::Player told = system;
        told.flags &= ~uint32_t(dp4::kLocal);
        told.addresses = true;
        told.stream = added.stream;
        told.datagram = added.datagram;
        for (uint32_t other : arriving.awaiting)
            toPeer(_peers[other], dp4::addForward(_originalPort, other, told), true);
        _arriving[system.id] = arriving;
        if (arriving.awaiting.empty())
            welcome(system.id);
        return;
    }
    case dp4::kAddForward: {
        dp4::Player system;
        if (!dp4::readAddForward(message, system) || system.id == _system)
            return;
        addPeer(system, fromIp, 0);
        if (auto host = _peers.find(_hostSystem); host != _peers.end())
            toPeer(host->second, dp4::addForwardAck(_originalPort, system.id), true);
        return;
    }
    case dp4::kAddForwardAck: {
        uint32_t id = 0;
        if (!dp4::readAddForwardAck(message, id) || !peer)
            return;
        auto it = _arriving.find(id);
        if (it == _arriving.end())
            return;
        it->second.awaiting.erase(peer->system);
        if (it->second.awaiting.empty())
            welcome(id);
        return;
    }
    case dp4::kAddForwardReply:
        if (_role == Role::kJoining) {
            _refused = true;
            _role = Role::kNone;
            _step = Step::kNone;
        }
        return;
    case dp4::kSuperEnumPlayersReply: {
        SessionInfo session;
        uint32_t seed = 0;
        std::vector<dp4::Player> players;
        if (_role != Role::kJoining || _step != Step::kForwarded ||
            !dp4::readSuperEnumPlayersReply(message, session, seed, players))
            return;
        _idSeed = seed;
        _session.flags = session.flags;
        _session.maxPlayers = session.maxPlayers;
        _session.name = session.name;
        _session.user = session.user;
        _members.clear();
        _peers.clear();
        for (const dp4::Player &p : players) {
            if (p.flags & dp4::kSystemPlayer) {
                if (p.flags & dp4::kNameServer)
                    _hostSystem = p.id;
                if (p.id == _system)
                    continue;
                // The host's own addresses may be 0.0.0.0: it's where this came from.
                addPeer(p, fromIp, 0);
                continue;
            }
            Member m;
            m.group = !p.players.empty();
            m.shortName = p.shortName;
            m.longName = p.longName;
            m.data = p.data;
            m.members.insert(p.players.begin(), p.players.end());
            m.node = p.systemPlayer;
            _members[p.id] = m;
        }
        _step = Step::kNone;
        _nextId = 1;
        _role = Role::kJoined;
        return;
    }
    case dp4::kCreatePlayer:
    case dp4::kCreatePlayerVerify: {
        dp4::Player p;
        if (!dp4::readCreatePlayer(message, p) || (p.flags & dp4::kSystemPlayer) || _members.count(p.id))
            return;
        Writer w;
        w.u8(kCreate);
        w.u32(p.id);
        Member m;
        m.shortName = p.shortName;
        m.longName = p.longName;
        m.data = p.data;
        m.node = p.systemPlayer;
        w.member(m);
        apply(w.b, true);
        return;
    }
    case dp4::kDeletePlayer: {
        uint32_t id = 0;
        if (!dp4::readDeletePlayer(message, id))
            return;
        if (_peers.count(id)) {
            dropPeer(id);
            return;
        }
        Writer w;
        w.u8(kDestroy);
        w.u32(id);
        apply(w.b, true);
        return;
    }
    case dp4::kPing: {
        uint32_t from = 0;
        uint32_t tick = 0;
        if (peer && dp4::readPing(message, from, tick))
            toPeer(*peer, dp4::ping(_originalPort, from, tick, true), false);
        return;
    }
    case dp4::kYouAreDead:
        if (_role == Role::kJoined)
            lost();
        return;
    default:
        return;
    }
}

// The machine joining is sent the session as it is now: its system
// players, this one's as the name server, and every player and group.
void Endpoint::welcome(uint32_t system) {
    auto it = _arriving.find(system);
    if (it == _arriving.end())
        return;
    const net::Address to = it->second.stream;
    _arriving.erase(it);
    std::vector<dp4::Player> players{ourSystem(true)};
    for (const auto &[other, peer] : _peers) {
        dp4::Player p;
        p.id = other;
        p.systemPlayer = other;
        p.flags = dp4::kSystemPlayer;
        p.addresses = true;
        p.stream = peer.stream;
        p.datagram = peer.datagram;
        players.push_back(p);
    }
    for (const auto &[id, m] : _members)
        players.push_back(record(id, m));
    SessionInfo s = current();
    s.password = _session.password;
    streamTo(to, dp4::superEnumPlayersReply(_originalPort, s, _idSeed, players));
    _original = true;
}

// A wait for the host's answer, with the lock let go so the answer can
// come in: by the endpoint's own thread, or, without one, by updating
// here. DirectPlay waits 5 seconds ([MC-DPL4CS] 3.1.2.2).
void Endpoint::originalWait(std::unique_lock<std::mutex> &lock, const std::function<bool()> &done) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done() && std::chrono::steady_clock::now() < until) {
        lock.unlock();
        if (_transport.thread)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        else
            update();
        lock.lock();
    }
}

// A player made in a session of DirectPlay 4's: its id the host's to
// give, then every machine told ([MC-DPL4CS] 3.1.4.4, 3.1.5.1). Groups
// aren't carried.
Endpoint::Status Endpoint::createOriginal(std::unique_lock<std::mutex> &lock, const Member &member, uint32_t &id) {
    if (member.group)
        return Status::kInvalidMember;
    if (_role == Role::kHost) {
        if (!_system)
            _system = newId();
        id = newId();
    } else {
        auto host = _peers.find(_hostSystem);
        if (host == _peers.end())
            return Status::kNotInSession;
        _granted.reset();
        _asking = true;
        toPeer(host->second, dp4::requestPlayerId(_originalPort, 0), true);
        originalWait(lock, [this] { return _granted.has_value() || _role != Role::kJoined; });
        _asking = false;
        if (!_granted || !*_granted)
            return Status::kRefused;
        id = *_granted;
    }
    Member m = member;
    m.node = _node;
    m.members.clear();
    _members[id] = m;
    for (auto &[system, peer] : _peers)
        toPeer(peer, dp4::createPlayer(_originalPort, record(id, m)), true);
    return Status::kOk;
}

// Its players and its system player deleted on every machine, so they
// needn't wait for it to go quiet.
void Endpoint::leaveOriginal() {
    std::vector<uint32_t> ours;
    for (const auto &[id, m] : _members)
        if (m.node == _node)
            ours.push_back(id);
    if (_system)
        ours.push_back(_system);
    for (auto &[system, peer] : _peers)
        for (uint32_t id : ours)
            toPeer(peer, dp4::deletePlayer(_originalPort, id), true);
    if (_transport.tcp)
        flushStreams();
    _peers.clear();
    _arriving.clear();
    _reserved.clear();
    _original = false;
    _step = Step::kNone;
    _system = 0;
    _hostSystem = 0;
    for (Stream &st : _streams)
        if (!st.closeWhenSent)
            _transport.tcp->close(st.socket);
    _streams.erase(std::remove_if(_streams.begin(), _streams.end(), [](const Stream &st) { return !st.closeWhenSent; }),
                   _streams.end());
}

// Each machine's link kept going, the quiet ones pinged, the silent ones
// let go; a join that gets no answer given up.
void Endpoint::originalUpdate() {
    constexpr uint64_t kPingMs = 2000;
    constexpr uint64_t kPopulateMs = 15000; // the host's wait for the others to be told ([MC-DPL4CS] 3.2.2.1)
    const uint64_t t = now();
    if (_step != Step::kNone && t - _stepMs > _transport.timeoutMs) {
        _refused = true;
        _role = Role::kNone;
        _step = Step::kNone;
    }
    std::vector<uint32_t> late;
    for (const auto &[system, arriving] : _arriving)
        if (t - arriving.sinceMs > kPopulateMs)
            late.push_back(system);
    for (uint32_t system : late)
        welcome(system);
    std::vector<uint32_t> gone;
    for (auto &[system, peer] : _peers) {
        if (peer.link) {
            peer.link->update();
            flushLink(peer);
        }
        if (t - peer.heardMs > _transport.timeoutMs) {
            gone.push_back(system);
        } else if (t - peer.heardMs >= kPingMs && t - peer.pingedMs >= kPingMs) {
            toPeer(peer, dp4::ping(_originalPort, _system, uint32_t(t), false), false);
            peer.pingedMs = t;
        }
    }
    for (uint32_t system : gone)
        dropPeer(system);
}

void Endpoint::closeOriginal() {
    if (!_transport.tcp)
        return;
    for (const Stream &st : _streams)
        _transport.tcp->close(st.socket);
    _streams.clear();
    if (_originalListen >= 0)
        _transport.tcp->close(_originalListen);
    _originalListen = -1;
    _originalPort = 0;
    _originalEnum.reset();
    _datagrams.reset();
}

} // namespace reluva::x86::dplay
