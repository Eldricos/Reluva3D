// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// IDirectPlay2 and IDirectPlay3, in their Unicode and ANSI forms, as the
// DirectX 5 SDK's dplay.h declares them, and the protocol Reluva plays
// their sessions over (docs/common/x86/dplay.md).

#include "emulation/x86/dplay.h"

#include "emulation/x86/com.h"

#include <array>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>

namespace reluva::x86 {

namespace {

const char *const kClsidDirectPlay = "{d1eb6d20-8923-11d0-9d97-00a0c90a43cb}";
const char *const kIidDirectPlay2 = "{2b74f7c0-9154-11cf-a9cd-00aa006886e3}";
const char *const kIidDirectPlay2A = "{9d460580-a822-11cf-960c-0080c7534e82}";
const char *const kIidDirectPlay3 = "{133efe40-32dc-11d0-9cfb-00a0c90a43cb}";
const char *const kIidDirectPlay3A = "{133efe41-32dc-11d0-9cfb-00a0c90a43cb}";
// The TCP/IP service provider, and the address chunks naming it.
const char *const kTcpIp = "{36e95ee0-8577-11cf-960c-0080c7534e82}";
const char *const kAddressTotalSize = "{1318f560-912c-11d0-9daa-00a0c90a43cb}";
const char *const kAddressServiceProvider = "{07d916c0-e0af-11cf-9c4e-00a0c905425e}";

// IDirectPlay3's methods after IUnknown's; IDirectPlay2's are the first 29.
const std::vector<ComInterface::Method> kDirectPlay3 = {
    {"QueryInterface", 2},
    {"AddRef", 0},
    {"Release", 0},
    {"AddPlayerToGroup", 2},
    {"Close", 0},
    {"CreateGroup", 5},
    {"CreatePlayer", 6},
    {"DeletePlayerFromGroup", 2},
    {"DestroyGroup", 1},
    {"DestroyPlayer", 1},
    {"EnumGroupPlayers", 5},
    {"EnumGroups", 4},
    {"EnumPlayers", 4},
    {"EnumSessions", 5},
    {"GetCaps", 2},
    {"GetGroupData", 4},
    {"GetGroupName", 3},
    {"GetMessageCount", 2},
    {"GetPlayerAddress", 3},
    {"GetPlayerCaps", 3},
    {"GetPlayerData", 4},
    {"GetPlayerName", 3},
    {"GetSessionDesc", 2},
    {"Initialize", 1},
    {"Open", 2},
    {"Receive", 5},
    {"Send", 5},
    {"SetGroupData", 4},
    {"SetGroupName", 3},
    {"SetPlayerData", 4},
    {"SetPlayerName", 3},
    {"SetSessionDesc", 2},
    {"AddGroupToGroup", 2}, // IDirectPlay3
    {"CreateGroupInGroup", 6},
    {"DeleteGroupFromGroup", 2},
    {"EnumConnections", 4},
    {"EnumGroupsInGroup", 5},
    {"GetGroupConnectionSettings", 4},
    {"InitializeConnection", 2},
    {"SecureOpen", 4},
    {"SendChatMessage", 4},
    {"SetGroupConnectionSettings", 3},
    {"StartSession", 2},
    {"GetGroupFlags", 2},
    {"GetGroupParent", 2},
    {"GetPlayerAccount", 4},
    {"GetPlayerFlags", 2},
};
constexpr size_t kDirectPlay2Methods = 32;

ComInterface makeInterface(const char *name, const char *iid, size_t methods) {
    return {name, iid, std::vector<ComInterface::Method>(kDirectPlay3.begin(), kDirectPlay3.begin() + long(methods))};
}

// DirectPlay's HRESULTs: 0x88770000 and the code dplay.h gives.
constexpr uint32_t dpError(uint32_t code) {
    return 0x88770000u | code;
}
constexpr uint32_t kAlreadyInitialized = dpError(5);
constexpr uint32_t kAccessDenied = dpError(10);
constexpr uint32_t kBufferTooSmall = dpError(30);
constexpr uint32_t kCantAddPlayer = dpError(40);
constexpr uint32_t kInvalidPlayer = dpError(150);
constexpr uint32_t kInvalidGroup = dpError(155);
constexpr uint32_t kNoConnection = dpError(170);
constexpr uint32_t kNoMessages = dpError(190);
constexpr uint32_t kNoSessions = dpError(220);
constexpr uint32_t kSendTooBig = dpError(230);
constexpr uint32_t kTimeout = dpError(240);
constexpr uint32_t kUnavailable = dpError(250);
constexpr uint32_t kCannotCreateServer = dpError(290);
constexpr uint32_t kSessionLost = dpError(310);
constexpr uint32_t kUninitialized = dpError(320);
constexpr uint32_t kNoNewPlayers = dpError(330);
constexpr uint32_t kInvalidPassword = dpError(340);

// Flags and message types, from dplay.h.
constexpr uint32_t kOpenJoin = 1;
constexpr uint32_t kOpenCreate = 2;
constexpr uint32_t kEnumAvailable = 1;
constexpr uint32_t kEnumAsync = 0x10;
constexpr uint32_t kEnumStopAsync = 0x20;
constexpr uint32_t kEnumPasswordRequired = 0x40;
constexpr uint32_t kTimedOut = 1; // DPESC_TIMEDOUT
constexpr uint32_t kPlayersLocal = 8;
constexpr uint32_t kPlayersRemote = 0x10;
constexpr uint32_t kPlayersGroup = 0x20;
constexpr uint32_t kPlayersSession = 0x80;
constexpr uint32_t kSessionNewPlayersDisabled = 1;
constexpr uint32_t kSessionJoinDisabled = 0x20;
constexpr uint32_t kSessionPasswordRequired = 0x400;
constexpr uint32_t kSendGuaranteed = 1;
constexpr uint32_t kReceiveToPlayer = 2;
constexpr uint32_t kReceiveFromPlayer = 4;
constexpr uint32_t kReceivePeek = 8;
constexpr uint32_t kCapsIsHost = 2;
constexpr uint32_t kTypeGroup = 0; // DPPLAYERTYPE_GROUP
constexpr uint32_t kTypePlayer = 1;
constexpr uint32_t kSysCreate = 3;
constexpr uint32_t kSysDestroy = 5;
constexpr uint32_t kSysAddToGroup = 7;
constexpr uint32_t kSysDeleteFromGroup = 0x21;
constexpr uint32_t kSysSessionLost = 0x31;
constexpr uint32_t kSessionDescSize = 0x50;

// What a message may hold: about what a UDP datagram carries unbroken,
// less the protocol's header.
constexpr uint32_t kMaxMessage = 1300;

// The protocol's timings, in milliseconds.
constexpr uint64_t kResendMs = 250;
constexpr uint64_t kKeepAliveMs = 1000;
constexpr uint64_t kLostMs = 15000;
constexpr uint64_t kEnumEveryMs = 1000;
constexpr uint64_t kFoundForMs = 5000;
constexpr uint64_t kJoinResendMs = 500;
constexpr uint64_t kJoinTimeoutMs = 5000;
constexpr uint64_t kEnumWaitMs = 1500; // a synchronous EnumSessions' wait when it names none

uint64_t nowMs() {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

using Guid = std::array<uint8_t, 16>;

Guid readGuid(Cpu &c, uint32_t a) {
    Guid g{};
    for (size_t i = 0; i < 16; ++i)
        g[i] = c.read8(a + uint32_t(i));
    return g;
}

void writeGuid(Cpu &c, uint32_t a, const Guid &g) {
    for (size_t i = 0; i < 16; ++i)
        c.write8(a + uint32_t(i), g[i]);
}

Guid randomGuid() {
    static std::mt19937 random{std::random_device{}()};
    Guid g{};
    for (uint8_t &b : g)
        b = uint8_t(random());
    return g;
}

// A string the program gave, ANSI or UTF-16 by the interface it called
// (only its low bytes kept).
std::string readText(Cpu &c, uint32_t a, bool wide) {
    if (!a)
        return {};
    if (!wide)
        return c.string(a, 256);
    std::string s;
    for (uint32_t i = 0; i < 256; ++i) {
        const uint16_t ch = c.read16(a + 2 * i);
        if (!ch)
            break;
        s += char(ch < 0x100 ? ch : '?');
    }
    return s;
}

uint32_t textBytes(const std::string &s, bool wide) {
    return uint32_t(s.size() + 1) * (wide ? 2 : 1);
}

void writeText(Cpu &c, uint32_t a, const std::string &s, bool wide) {
    if (!wide) {
        c.writeString(a, s, uint32_t(s.size() + 1));
        return;
    }
    for (size_t i = 0; i < s.size(); ++i)
        c.write16(a + 2 * uint32_t(i), uint8_t(s[i]));
    c.write16(a + 2 * uint32_t(s.size()), 0);
}

// The protocol's datagrams: "RvDP", its version, then the kind.
enum Kind : uint8_t {
    kEnumRequest = 1, // application guid
    kSessionFound,    // session, then its players
    kJoin,            // instance guid, password, nonce
    kRefused,         // nonce, HRESULT
    kWelcome,         // nonce, link, session
    kReliable,        // sequence, event
    kAck,             // every sequence up to this one
    kData,            // from, to, bytes
    kBye,
    kKeepAlive,
};
// What a reliable datagram carries.
enum Event : uint8_t {
    kEventPlayer = 1, // player: made
    kEventDestroy,    // id
    kEventJoinGroup,  // group, player
    kEventLeaveGroup, // group, player
    kEventSession,    // session
    kEventData,       // from, to, bytes
    kEventSynced,     // the session as it was when joined has all come
};
constexpr uint8_t kVersion = 1;

struct Writer {
    std::vector<uint8_t> b;
    explicit Writer(uint8_t kind) { b = {'R', 'v', 'D', 'P', kVersion, kind}; }
    Writer() = default;
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.push_back(uint8_t(v >> (8 * i)));
    }
    void guid(const Guid &g) { b.insert(b.end(), g.begin(), g.end()); }
    void bytes(const std::vector<uint8_t> &v) {
        u32(uint32_t(v.size()));
        b.insert(b.end(), v.begin(), v.end());
    }
    void text(const std::string &s) { bytes(std::vector<uint8_t>(s.begin(), s.end())); }
};

struct Reader {
    const std::vector<uint8_t> &b;
    size_t at = 0;
    bool ok = true;
    uint8_t u8() {
        if (at + 1 > b.size()) {
            ok = false;
            return 0;
        }
        return b[at++];
    }
    uint32_t u32() {
        if (at + 4 > b.size()) {
            ok = false;
            return 0;
        }
        const uint32_t v = b[at] | b[at + 1] << 8 | b[at + 2] << 16 | uint32_t(b[at + 3]) << 24;
        at += 4;
        return v;
    }
    Guid guid() {
        Guid g{};
        if (at + 16 > b.size()) {
            ok = false;
            return g;
        }
        std::copy(b.begin() + long(at), b.begin() + long(at + 16), g.begin());
        at += 16;
        return g;
    }
    std::vector<uint8_t> bytes() {
        const uint32_t n = u32();
        if (!ok || n > b.size() - at) {
            ok = false;
            return {};
        }
        std::vector<uint8_t> v(b.begin() + long(at), b.begin() + long(at + n));
        at += n;
        return v;
    }
    std::string text() {
        const std::vector<uint8_t> v = bytes();
        return {v.begin(), v.end()};
    }
};

/** A session as DPSESSIONDESC2 has it. */
struct SessionInfo {
    uint32_t flags = 0;
    Guid instance{};
    Guid application{};
    uint32_t maxPlayers = 0; // 0: no limit
    uint32_t currentPlayers = 0;
    std::array<uint32_t, 4> user{};
    std::string name;
    std::string password; // the host's only; others learn that there is one (flags)

    void write(Writer &w) const {
        w.u32(flags | (password.empty() ? 0 : kSessionPasswordRequired));
        w.guid(instance);
        w.guid(application);
        w.u32(maxPlayers);
        w.u32(currentPlayers);
        for (uint32_t u : user)
            w.u32(u);
        w.text(name);
    }
    void read(Reader &r) {
        flags = r.u32();
        instance = r.guid();
        application = r.guid();
        maxPlayers = r.u32();
        currentPlayers = r.u32();
        for (uint32_t &u : user)
            u = r.u32();
        name = r.text();
    }
    bool full() const { return maxPlayers && currentPlayers >= maxPlayers; }
};

/** A player or group of the session. */
struct Member {
    uint32_t id = 0;
    uint32_t link = 0; // whose: the link it was made on
    bool group = false;
    std::string shortName;
    std::string longName;
    std::vector<uint8_t> data;
    std::set<uint32_t> members; // a group's players

    void write(Writer &w) const {
        w.u32(id);
        w.u32(link);
        w.u8(group ? 1 : 0);
        w.text(shortName);
        w.text(longName);
        w.bytes(data);
    }
    void read(Reader &r) {
        id = r.u32();
        link = r.u32();
        group = r.u8() != 0;
        shortName = r.text();
        longName = r.text();
        data = r.bytes();
    }
};

/** A message waiting for Receive: the program's own, or the system's. */
struct Message {
    uint32_t from = 0; // 0: the system's (DPID_SYSMSG)
    uint32_t to = 0;
    std::vector<uint8_t> data;
    uint32_t type = 0; // a system message's DPSYS_ type
    Member member;     // the player or group it's about
    uint32_t group = 0;
    uint32_t currentPlayers = 0;
};

/** The other end of a session: the host, for one who joined; each who joined, for the host. */
struct Link {
    net::Address address;
    uint32_t nonce = 0; // the join it answered
    uint32_t nextSend = 1;
    uint32_t nextReceive = 1;
    std::map<uint32_t, std::pair<std::vector<uint8_t>, uint64_t>> unacked; // sequence: the datagram, when sent
    std::map<uint32_t, std::vector<uint8_t>> early;                        // arrived ahead of their turn
    uint64_t heardMs = 0;
    uint64_t sentMs = 0;
};

/** A session found by EnumSessions. */
struct Found {
    SessionInfo session;
    std::vector<Member> players;
    net::Address host;
    uint64_t heardMs = 0;
};

} // namespace

struct DirectPlay::State {
    ComHost com;
    ComInterface directPlay2 = makeInterface("IDirectPlay2", kIidDirectPlay2, kDirectPlay2Methods);
    ComInterface directPlay2A = makeInterface("IDirectPlay2A", kIidDirectPlay2A, kDirectPlay2Methods);
    ComInterface directPlay3 = makeInterface("IDirectPlay3", kIidDirectPlay3, kDirectPlay3.size());
    ComInterface directPlay3A = makeInterface("IDirectPlay3A", kIidDirectPlay3A, kDirectPlay3.size());
    DirectPlay &owner;
    std::recursive_mutex lock;
    std::vector<std::weak_ptr<ComObject>> objects;
    DirectPlay::Traffic traffic;
    State(Win32Process &p, DirectPlay &o) : com(p), owner(o) {}
};

namespace {

class Play : public ComObject {
public:
    DirectPlay::State *s = nullptr;

    const ComInterface *query(const std::string &iid) override {
        for (const ComInterface *f : {&s->directPlay3A, &s->directPlay3, &s->directPlay2A, &s->directPlay2})
            if (iid == f->iid)
                return f;
        return nullptr;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override;
    void update();

private:
    enum class Role { kNone, kHost, kJoining, kJoined };
    bool _connection = false; // InitializeConnection called
    std::unique_ptr<net::UdpSocket> _socket;
    Role _role = Role::kNone;
    SessionInfo _session;
    std::map<uint32_t, Member> _members;
    uint32_t _myLink = 0;
    uint32_t _nextLink = 2; // the host is link 1
    uint32_t _nextId = 1;
    std::map<uint32_t, Link> _links;
    std::deque<Message> _queue;
    bool _synced = false;
    bool _lost = false;
    // Joining: the nonce sent, the host, and its refusal if it refused.
    uint32_t _joinNonce = 0;
    net::Address _joinHost;
    uint32_t _refusal = 0;
    // Enumerating.
    std::map<Guid, Found> _found;
    uint64_t _enumSentMs = 0;
    bool _enumerating = false;
    Guid _enumApplication{};

    bool wide(const ComInterface &iface) const { return &iface == &s->directPlay3 || &iface == &s->directPlay2; }
    Win32Process &process() const { return s->com.process(); }
    uint32_t alloc(uint32_t size) { return process().memory().alloc(std::max(size, 4u)); }
    void free(uint32_t a) { process().memory().free(a); }

    // Calls.
    uint32_t enumConnections(Cpu &c, bool wide);
    uint32_t initializeConnection(Cpu &c);
    uint32_t enumSessions(Cpu &c, bool wide);
    uint32_t open(Cpu &c, bool wide);
    uint32_t close();
    uint32_t createMember(Cpu &c, bool wide, bool group);
    uint32_t destroyMember(uint32_t id, bool group);
    uint32_t groupMembership(uint32_t group, uint32_t player, bool join);
    uint32_t enumPlayers(Cpu &c, bool wide);
    uint32_t enumGroupPlayers(Cpu &c, bool wide);
    uint32_t getSessionDesc(Cpu &c, bool wide);
    uint32_t setSessionDesc(Cpu &c, bool wide);
    uint32_t getCaps(Cpu &c);
    uint32_t send(Cpu &c);
    uint32_t receive(Cpu &c, bool wide);
    uint32_t getMessageCount(Cpu &c);

    // Memory the program reads.
    uint32_t writeName(Cpu &c, const Member &m, bool wide); // a DPNAME, freed by the caller
    uint32_t sessionDescBytes(const SessionInfo &d, bool wide) const;
    void writeSessionDesc(Cpu &c, uint32_t a, const SessionInfo &d, bool wide);
    SessionInfo readSessionDesc(Cpu &c, uint32_t a, bool wide);
    uint32_t systemBytes(const Message &m, bool wide) const;
    void writeSystem(Cpu &c, uint32_t a, const Message &m, bool wide);

    // The network.
    bool openSocket(uint16_t port);
    void sendTo(const net::Address &to, const Writer &w);
    void sendReliable(uint32_t link, const Writer &event);
    void sendEventToAll(const Writer &event, uint32_t except);
    void handle(const net::Address &from, const std::vector<uint8_t> &packet);
    void handleEvent(uint32_t link, const std::vector<uint8_t> &event);
    void handleJoin(const net::Address &from, Reader &r);
    void sessionFound(const net::Address &from, Reader &r);
    void route(uint32_t origin, uint32_t from, uint32_t to, const std::vector<uint8_t> &data, bool guaranteed);
    void linkLost(uint32_t link);
    void addMember(const Member &m, uint32_t origin);
    void removeMember(uint32_t id, uint32_t origin);
    void membership(uint32_t group, uint32_t player, bool join, uint32_t origin);
    uint32_t countPlayers() const;
    uint32_t linkOf(const net::Address &a) const;
    void sendEnumRequests();
    void wait() const;
    void reset();
    void log(const std::string &line) const {
        if (s->owner.log)
            s->owner.log(line);
    }
    bool open() const { return _role == Role::kHost || _role == Role::kJoined; }
    bool local(const Member &m) const { return m.link == _myLink; }
};

// The memory a DirectPlay address of the TCP/IP connection takes: its total
// size, and the service provider.
constexpr uint32_t kConnectionBytes = 2 * 20 + 4 + 16;

uint32_t Play::enumConnections(Cpu &c, bool wide) {
    const uint32_t callback = c.arg(2);
    const uint32_t context = c.arg(3);
    if (!s->owner.network.open)
        return kSOk;
    // EnumConnectionsCallback(guid*, connection, size, DPNAME*, flags, context)
    const uint32_t guid = alloc(16);
    const uint32_t address = alloc(kConnectionBytes);
    ComHost::writeGuid(c, guid, kTcpIp);
    ComHost::writeGuid(c, address, kAddressTotalSize);
    c.write32(address + 16, 4);
    c.write32(address + 20, kConnectionBytes);
    ComHost::writeGuid(c, address + 24, kAddressServiceProvider);
    c.write32(address + 40, 16);
    ComHost::writeGuid(c, address + 44, kTcpIp);
    Member named;
    named.shortName = "TCP/IP (Internet and local network)";
    const uint32_t name = writeName(c, named, wide);
    uint32_t keepGoing = 0;
    const Error e =
        c.call(callback, {guid, address, kConnectionBytes, name, 1 /* DPCONNECTION_DIRECTPLAY */, context}, keepGoing);
    free(name);
    free(address);
    free(guid);
    return e ? kEFail : kSOk;
}

uint32_t Play::initializeConnection(Cpu &c) {
    const uint32_t address = c.arg(1);
    if (_connection)
        return kAlreadyInitialized;
    if (!s->owner.network.open || !c.valid(address, 20))
        return kUnavailable;
    // Its chunks: a GUID naming each, its size, its data.
    for (uint32_t at = address, n = 0; n < 8 && c.valid(at, 20); ++n) {
        const uint32_t size = c.read32(at + 16);
        if (ComHost::guid(c, at) == kAddressServiceProvider && size == 16) {
            if (ComHost::guid(c, at + 20) != kTcpIp)
                return kUnavailable;
            _connection = true;
            return kSOk;
        }
        at += 20 + size;
    }
    return kUnavailable;
}

bool Play::openSocket(uint16_t port) {
    if (_socket && (port == 0 || _socket->port() == port))
        return true;
    _socket = s->owner.network.open ? s->owner.network.open(port) : nullptr;
    return _socket != nullptr;
}

void Play::sendTo(const net::Address &to, const Writer &w) {
    if (_socket && _socket->send(to, w.b))
        ++s->traffic.sent;
}

void Play::sendEnumRequests() {
    Writer w(kEnumRequest);
    w.guid(_enumApplication);
    for (const net::Address &a : s->owner.network.search)
        sendTo(a, w);
    _enumSentMs = nowMs();
}

uint32_t Play::enumSessions(Cpu &c, bool wide) {
    const uint32_t desc = c.arg(1);
    uint32_t timeout = c.arg(2);
    const uint32_t callback = c.arg(3);
    const uint32_t context = c.arg(4);
    const uint32_t flags = c.arg(5);
    if (!_connection)
        return kUninitialized;
    if (flags & kEnumStopAsync) {
        _enumerating = false;
        return kSOk;
    }
    if (!c.valid(desc, kSessionDescSize))
        return kEInvalidArg;
    if (!openSocket(0))
        return kNoConnection;
    _enumApplication = readGuid(c, desc + 0x18);
    _enumerating = (flags & kEnumAsync) != 0;
    update();
    if (nowMs() - _enumSentMs >= kEnumEveryMs)
        sendEnumRequests();
    if (!(flags & kEnumAsync)) {
        const uint64_t until = nowMs() + (timeout ? timeout : kEnumWaitMs);
        while (nowMs() < until) {
            wait();
            update();
        }
    }
    const uint64_t now = nowMs();
    std::erase_if(_found, [&](const auto &f) { return now - f.second.heardMs > kFoundForMs; });
    const Guid none{};
    const uint32_t timeoutAt = alloc(4);
    c.write32(timeoutAt, timeout);
    for (const auto &[instance, f] : _found) {
        const SessionInfo &d = f.session;
        if (_enumApplication != none && d.application != _enumApplication)
            continue;
        if ((flags & kEnumAvailable) && (d.full() || (d.flags & (kSessionNewPlayersDisabled | kSessionJoinDisabled))))
            continue;
        if ((d.flags & kSessionPasswordRequired) && !(flags & kEnumPasswordRequired))
            continue;
        const uint32_t a = alloc(sessionDescBytes(d, wide));
        writeSessionDesc(c, a, d, wide);
        uint32_t keepGoing = 0;
        const Error e = c.call(callback, {a, timeoutAt, 0, context}, keepGoing);
        free(a);
        if (e || !keepGoing) {
            free(timeoutAt);
            return e ? kEFail : kSOk;
        }
    }
    uint32_t ignored = 0;
    const Error e = c.call(callback, {0, timeoutAt, kTimedOut, context}, ignored);
    free(timeoutAt);
    return e ? kEFail : kSOk;
}

void Play::wait() const {
    if (s->owner.waiting)
        s->owner.waiting();
    else
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

uint32_t Play::open(Cpu &c, bool wide) {
    const uint32_t desc = c.arg(1);
    const uint32_t flags = c.arg(2);
    if (!_connection)
        return kUninitialized;
    if (_role != Role::kNone)
        return kAlreadyInitialized;
    if (!c.valid(desc, kSessionDescSize))
        return kEInvalidArg;
    if (flags & kOpenCreate) {
        if (!openSocket(kDirectPlayPort))
            return kCannotCreateServer;
        _session = readSessionDesc(c, desc, wide);
        _session.instance = randomGuid();
        _session.currentPlayers = 0;
        _role = Role::kHost;
        _myLink = 1;
        _synced = true;
        _lost = false;
        _enumerating = false;
        log("hosting \"" + _session.name + "\" on port " + std::to_string(_socket->port()));
        return kSOk;
    }
    if (!(flags & kOpenJoin))
        return kEInvalidArg;
    const auto found = _found.find(readGuid(c, desc + 8));
    if (found == _found.end())
        return kNoSessions;
    if (!openSocket(0))
        return kNoConnection;
    _joinHost = found->second.host;
    _joinNonce = uint32_t(std::random_device{}()) | 1;
    _refusal = 0;
    _role = Role::kJoining;
    _synced = false;
    _lost = false;
    const std::string password = readText(c, c.read32(desc + 0x34), wide);
    Writer w(kJoin);
    w.guid(found->first);
    w.text(password);
    w.u32(_joinNonce);
    const uint64_t start = nowMs();
    uint64_t sent = 0;
    while (true) {
        const uint64_t now = nowMs();
        if (_role == Role::kJoined && _synced) {
            _enumerating = false;
            log("joined \"" + _session.name + "\" at " + _joinHost.toString());
            return kSOk;
        }
        if (_refusal || now - start > kJoinTimeoutMs) {
            const uint32_t r = _refusal ? _refusal : kTimeout;
            if (_role == Role::kJoined) {
                Writer bye(kBye);
                sendTo(_joinHost, bye);
            }
            reset();
            return r;
        }
        if (_role == Role::kJoining && now - sent >= kJoinResendMs) {
            sendTo(_joinHost, w);
            sent = now;
        }
        wait();
        update();
    }
}

void Play::reset() {
    _role = Role::kNone;
    _members.clear();
    _links.clear();
    _queue.clear();
    _myLink = 0;
    _nextLink = 2;
    _synced = false;
    _joinNonce = 0;
}

uint32_t Play::close() {
    if (open())
        log("left the session");
    Writer bye(kBye);
    for (const auto &[id, link] : _links)
        sendTo(link.address, bye);
    reset();
    _lost = false;
    _socket.reset();
    return kSOk;
}

uint32_t Play::countPlayers() const {
    uint32_t n = 0;
    for (const auto &[id, m] : _members)
        n += m.group ? 0 : 1;
    return n;
}

uint32_t Play::createMember(Cpu &c, bool wide, bool group) {
    // CreatePlayer(id*, DPNAME*, event, data, size, flags), CreateGroup(id*, DPNAME*, data, size, flags)
    const uint32_t out = c.arg(1);
    const uint32_t name = c.arg(2);
    const uint32_t data = c.arg(group ? 3 : 4);
    const uint32_t size = c.arg(group ? 4 : 5);
    if (!open())
        return _lost ? kSessionLost : kNoConnection;
    if (!c.valid(out, 4) || (size && !c.valid(data, size)))
        return kEInvalidArg;
    if (!group && _session.full())
        return kCantAddPlayer;
    Member m;
    m.id = _myLink << 16 | _nextId++;
    m.link = _myLink;
    m.group = group;
    if (name && c.valid(name, 16)) {
        m.shortName = readText(c, c.read32(name + 8), wide);
        m.longName = readText(c, c.read32(name + 12), wide);
    }
    for (uint32_t i = 0; i < size; ++i)
        m.data.push_back(c.read8(data + i));
    addMember(m, _myLink);
    c.write32(out, m.id);
    return kSOk;
}

void Play::addMember(const Member &m, uint32_t origin) {
    if (_members.count(m.id))
        return;
    _members[m.id] = m;
    log(std::string(m.group ? "group " : "player ") + std::to_string(m.id) + " \"" + m.shortName + "\" made" +
        (origin == _myLink ? " here" : ""));
    if (_role == Role::kHost)
        _session.currentPlayers = countPlayers();
    Writer event;
    event.u8(kEventPlayer);
    m.write(event);
    if (_role == Role::kHost)
        sendEventToAll(event, origin);
    else if (origin == _myLink)
        sendReliable(1, event);
    if (origin != _myLink && _synced) {
        Message msg;
        msg.type = kSysCreate;
        msg.member = m;
        msg.currentPlayers = countPlayers();
        _queue.push_back(msg);
    }
}

uint32_t Play::destroyMember(uint32_t id, bool group) {
    if (!open())
        return _lost ? kSessionLost : kNoConnection;
    const auto it = _members.find(id);
    if (it == _members.end() || it->second.group != group || !local(it->second))
        return group ? kInvalidGroup : kInvalidPlayer;
    removeMember(id, _myLink);
    return kSOk;
}

void Play::removeMember(uint32_t id, uint32_t origin) {
    const auto it = _members.find(id);
    if (it == _members.end())
        return;
    const Member m = it->second;
    _members.erase(it);
    log(std::string(m.group ? "group " : "player ") + std::to_string(id) + " \"" + m.shortName + "\" gone");
    for (auto &[gid, g] : _members)
        g.members.erase(id);
    if (_role == Role::kHost)
        _session.currentPlayers = countPlayers();
    Writer event;
    event.u8(kEventDestroy);
    event.u32(id);
    if (_role == Role::kHost)
        sendEventToAll(event, origin);
    else if (origin == _myLink)
        sendReliable(1, event);
    if (origin != _myLink && _synced) {
        Message msg;
        msg.type = kSysDestroy;
        msg.member = m;
        _queue.push_back(msg);
    }
    std::erase_if(_queue, [&](const Message &q) { return q.type == 0 && q.to == id; });
}

uint32_t Play::groupMembership(uint32_t group, uint32_t player, bool join) {
    if (!open())
        return _lost ? kSessionLost : kNoConnection;
    const auto g = _members.find(group);
    if (g == _members.end() || !g->second.group)
        return kInvalidGroup;
    const auto p = _members.find(player);
    if (p == _members.end() || p->second.group)
        return kInvalidPlayer;
    membership(group, player, join, _myLink);
    return kSOk;
}

void Play::membership(uint32_t group, uint32_t player, bool join, uint32_t origin) {
    const auto g = _members.find(group);
    if (g == _members.end())
        return;
    if (join)
        g->second.members.insert(player);
    else
        g->second.members.erase(player);
    Writer event;
    event.u8(join ? kEventJoinGroup : kEventLeaveGroup);
    event.u32(group);
    event.u32(player);
    if (_role == Role::kHost)
        sendEventToAll(event, origin);
    else if (origin == _myLink)
        sendReliable(1, event);
    if (origin != _myLink && _synced) {
        Message msg;
        msg.type = join ? kSysAddToGroup : kSysDeleteFromGroup;
        msg.group = group;
        msg.member.id = player;
        _queue.push_back(msg);
    }
}

uint32_t Play::writeName(Cpu &c, const Member &m, bool wide) {
    const uint32_t a = alloc(16 + textBytes(m.shortName, wide) + textBytes(m.longName, wide));
    const uint32_t shortAt = a + 16;
    const uint32_t longAt = shortAt + textBytes(m.shortName, wide);
    c.write32(a, 16);
    c.write32(a + 4, 0);
    c.write32(a + 8, shortAt);
    c.write32(a + 12, longAt);
    writeText(c, shortAt, m.shortName, wide);
    writeText(c, longAt, m.longName, wide);
    return a;
}

uint32_t Play::enumPlayers(Cpu &c, bool wide) {
    // EnumPlayers(instance guid*, callback(id, type, DPNAME*, flags, context), context, flags)
    const uint32_t instance = c.arg(1);
    const uint32_t callback = c.arg(2);
    const uint32_t context = c.arg(3);
    const uint32_t flags = c.arg(4);
    std::vector<Member> list;
    if (flags & kPlayersSession) {
        if (!c.valid(instance, 16))
            return kEInvalidArg;
        const auto f = _found.find(readGuid(c, instance));
        if (f == _found.end())
            return kNoSessions;
        list = f->second.players;
    } else {
        if (!open())
            return _lost ? kSessionLost : kNoConnection;
        for (const auto &[id, m] : _members)
            list.push_back(m);
    }
    for (const Member &m : list) {
        const bool isLocal = !(flags & kPlayersSession) && local(m);
        if (m.group && !(flags & kPlayersGroup))
            continue;
        if (((flags & kPlayersLocal) && !isLocal) || ((flags & kPlayersRemote) && isLocal))
            continue;
        const uint32_t name = writeName(c, m, wide);
        uint32_t keepGoing = 0;
        const Error e =
            c.call(callback,
                   {m.id, m.group ? kTypeGroup : kTypePlayer, name, isLocal ? kPlayersLocal : kPlayersRemote, context},
                   keepGoing);
        free(name);
        if (e)
            return kEFail;
        if (!keepGoing)
            break;
    }
    return kSOk;
}

uint32_t Play::enumGroupPlayers(Cpu &c, bool wide) {
    // EnumGroupPlayers(group, instance guid*, callback, context, flags)
    const uint32_t group = c.arg(1);
    const uint32_t callback = c.arg(3);
    const uint32_t context = c.arg(4);
    if (!open())
        return _lost ? kSessionLost : kNoConnection;
    const auto g = _members.find(group);
    if (g == _members.end() || !g->second.group)
        return kInvalidGroup;
    const std::set<uint32_t> ids = g->second.members;
    for (uint32_t id : ids) {
        const auto p = _members.find(id);
        if (p == _members.end())
            continue;
        const uint32_t name = writeName(c, p->second, wide);
        uint32_t keepGoing = 0;
        const Error e = c.call(
            callback, {id, kTypePlayer, name, local(p->second) ? kPlayersLocal : kPlayersRemote, context}, keepGoing);
        free(name);
        if (e)
            return kEFail;
        if (!keepGoing)
            break;
    }
    return kSOk;
}

uint32_t Play::sessionDescBytes(const SessionInfo &d, bool wide) const {
    return kSessionDescSize + textBytes(d.name, wide);
}

void Play::writeSessionDesc(Cpu &c, uint32_t a, const SessionInfo &d, bool wide) {
    for (uint32_t i = 0; i < kSessionDescSize; i += 4)
        c.write32(a + i, 0);
    c.write32(a, kSessionDescSize);
    c.write32(a + 4, d.flags | (d.password.empty() ? 0 : kSessionPasswordRequired));
    writeGuid(c, a + 8, d.instance);
    writeGuid(c, a + 0x18, d.application);
    c.write32(a + 0x28, d.maxPlayers);
    c.write32(a + 0x2c, d.currentPlayers);
    c.write32(a + 0x30, a + kSessionDescSize);
    writeText(c, a + kSessionDescSize, d.name, wide);
    for (uint32_t i = 0; i < 4; ++i)
        c.write32(a + 0x40 + 4 * i, d.user[i]);
}

SessionInfo Play::readSessionDesc(Cpu &c, uint32_t a, bool wide) {
    SessionInfo d;
    d.flags = c.read32(a + 4) & ~kSessionPasswordRequired;
    d.instance = readGuid(c, a + 8);
    d.application = readGuid(c, a + 0x18);
    d.maxPlayers = c.read32(a + 0x28);
    d.currentPlayers = c.read32(a + 0x2c);
    d.name = readText(c, c.read32(a + 0x30), wide);
    d.password = readText(c, c.read32(a + 0x34), wide);
    for (uint32_t i = 0; i < 4; ++i)
        d.user[i] = c.read32(a + 0x40 + 4 * i);
    return d;
}

uint32_t Play::getSessionDesc(Cpu &c, bool wide) {
    const uint32_t buffer = c.arg(1);
    const uint32_t sizeAt = c.arg(2);
    if (!open())
        return _lost ? kSessionLost : kNoConnection;
    if (!c.valid(sizeAt, 4))
        return kEInvalidArg;
    SessionInfo d = _session;
    d.password.clear();
    d.currentPlayers = countPlayers();
    d.flags |= _session.password.empty() ? 0 : kSessionPasswordRequired;
    const uint32_t need = sessionDescBytes(d, wide);
    if (!buffer || c.read32(sizeAt) < need || !c.valid(buffer, need)) {
        c.write32(sizeAt, need);
        return kBufferTooSmall;
    }
    writeSessionDesc(c, buffer, d, wide);
    c.write32(sizeAt, need);
    return kSOk;
}

uint32_t Play::setSessionDesc(Cpu &c, bool wide) {
    const uint32_t desc = c.arg(1);
    if (!open())
        return _lost ? kSessionLost : kNoConnection;
    if (_role != Role::kHost)
        return kAccessDenied;
    if (!c.valid(desc, kSessionDescSize))
        return kEInvalidArg;
    const SessionInfo d = readSessionDesc(c, desc, wide);
    _session.flags = d.flags;
    _session.maxPlayers = d.maxPlayers;
    _session.name = d.name;
    _session.password = d.password;
    _session.user = d.user;
    Writer event;
    event.u8(kEventSession);
    _session.write(event);
    sendEventToAll(event, 0);
    return kSOk;
}

uint32_t Play::getCaps(Cpu &c) {
    const uint32_t caps = c.arg(1);
    if (!c.valid(caps, 0x28) || c.read32(caps) < 0x28)
        return kEInvalidArg;
    if (!open())
        return _lost ? kSessionLost : kNoConnection;
    // DPCAPS: size, flags, largest message, queue, players, speed (100 baud),
    // latency (ms), local players, header bytes, timeout (ms): a local
    // network's, as the protocol has them.
    const uint32_t values[] = {
        0x28, _role == Role::kHost ? kCapsIsHost : 0u, kMaxMessage, 0, 0, 100000, 50, 0, 26, uint32_t(kLostMs)};
    for (uint32_t i = 0; i < 10; ++i)
        c.write32(caps + 4 * i, values[i]);
    return kSOk;
}

uint32_t Play::send(Cpu &c) {
    // Send(from, to, flags, data, size)
    const uint32_t from = c.arg(1);
    const uint32_t to = c.arg(2);
    const uint32_t flags = c.arg(3);
    const uint32_t data = c.arg(4);
    const uint32_t size = c.arg(5);
    update();
    if (!open())
        return _lost ? kSessionLost : kNoConnection;
    const auto f = _members.find(from);
    if (f == _members.end() || f->second.group || !local(f->second))
        return kInvalidPlayer;
    if (to && !_members.count(to))
        return kInvalidPlayer;
    if (size > kMaxMessage)
        return kSendTooBig;
    if (size && !c.valid(data, size))
        return kEInvalidArg;
    std::vector<uint8_t> bytes(size);
    for (uint32_t i = 0; i < size; ++i)
        bytes[i] = c.read8(data + i);
    route(_myLink, from, to, bytes, (flags & kSendGuaranteed) != 0);
    return kSOk;
}

void Play::route(uint32_t origin, uint32_t from, uint32_t to, const std::vector<uint8_t> &data, bool guaranteed) {
    // Who it's for: everyone but the sender, a player, or a group's players.
    std::vector<uint32_t> recipients;
    if (to == 0) {
        for (const auto &[id, m] : _members)
            if (!m.group && id != from)
                recipients.push_back(id);
    } else if (const auto t = _members.find(to); t != _members.end()) {
        if (t->second.group) {
            for (uint32_t id : t->second.members)
                if (id != from && _members.count(id))
                    recipients.push_back(id);
        } else {
            recipients.push_back(to);
        }
    }
    std::set<uint32_t> links;
    for (uint32_t id : recipients) {
        const Member &m = _members[id];
        if (local(m)) {
            Message msg;
            msg.from = from;
            msg.to = id;
            msg.data = data;
            _queue.push_back(msg);
        } else {
            // One who joined reaches the others through the host.
            links.insert(_role == Role::kHost ? m.link : 1);
        }
    }
    links.erase(origin);
    if (links.empty())
        return;
    Writer w;
    if (guaranteed) {
        w.u8(kEventData);
    } else {
        w = Writer(kData);
    }
    w.u32(from);
    w.u32(to);
    w.bytes(data);
    for (uint32_t link : links) {
        if (guaranteed) {
            sendReliable(link, w);
        } else if (const auto l = _links.find(link); l != _links.end()) {
            sendTo(l->second.address, w);
            l->second.sentMs = nowMs();
        }
    }
}

uint32_t Play::systemBytes(const Message &m, bool wide) const {
    switch (m.type) {
    case kSysCreate:
        return 48 + uint32_t(m.member.data.size()) + textBytes(m.member.shortName, wide) +
               textBytes(m.member.longName, wide);
    case kSysDestroy:
        return 52 + uint32_t(m.member.data.size()) + textBytes(m.member.shortName, wide) +
               textBytes(m.member.longName, wide);
    case kSysAddToGroup:
    case kSysDeleteFromGroup:
        return 12;
    default:
        return 4;
    }
}

void Play::writeSystem(Cpu &c, uint32_t a, const Message &m, bool wide) {
    const uint32_t size = systemBytes(m, wide);
    for (uint32_t i = 0; i + 4 <= size; i += 4)
        c.write32(a + i, 0);
    c.write32(a, m.type);
    if (m.type == kSysAddToGroup || m.type == kSysDeleteFromGroup) {
        c.write32(a + 4, m.group);
        c.write32(a + 8, m.member.id);
        return;
    }
    if (m.type != kSysCreate && m.type != kSysDestroy)
        return;
    // DPMSG_CREATEPLAYERORGROUP: type, player type, id, players, data,
    // size, DPNAME, parent, flags; DPMSG_DESTROYPLAYERORGROUP: type,
    // player type, id, local data and size, remote data and size, DPNAME,
    // parent, flags. What they point to follows them.
    const bool create = m.type == kSysCreate;
    const uint32_t fixed = create ? 48 : 52;
    const uint32_t dataAt = a + fixed;
    const uint32_t shortAt = dataAt + uint32_t(m.member.data.size());
    const uint32_t longAt = shortAt + textBytes(m.member.shortName, wide);
    c.write32(a + 4, m.member.group ? kTypeGroup : kTypePlayer);
    c.write32(a + 8, m.member.id);
    const uint32_t name = create ? a + 0x18 : a + 0x1c;
    if (create) {
        c.write32(a + 0xc, m.currentPlayers);
        c.write32(a + 0x10, m.member.data.empty() ? 0 : dataAt);
        c.write32(a + 0x14, uint32_t(m.member.data.size()));
    } else {
        c.write32(a + 0x14, m.member.data.empty() ? 0 : dataAt);
        c.write32(a + 0x18, uint32_t(m.member.data.size()));
    }
    c.write32(name, 16);
    c.write32(name + 8, shortAt);
    c.write32(name + 12, longAt);
    for (size_t i = 0; i < m.member.data.size(); ++i)
        c.write8(dataAt + uint32_t(i), m.member.data[i]);
    writeText(c, shortAt, m.member.shortName, wide);
    writeText(c, longAt, m.member.longName, wide);
}

uint32_t Play::receive(Cpu &c, bool wide) {
    // Receive(from*, to*, flags, data, size*)
    const uint32_t fromAt = c.arg(1);
    const uint32_t toAt = c.arg(2);
    const uint32_t flags = c.arg(3);
    const uint32_t buffer = c.arg(4);
    const uint32_t sizeAt = c.arg(5);
    if (!c.valid(fromAt, 4) || !c.valid(toAt, 4) || !c.valid(sizeAt, 4))
        return kEInvalidArg;
    update();
    const uint32_t wantFrom = c.read32(fromAt);
    const uint32_t wantTo = c.read32(toAt);
    for (auto it = _queue.begin(); it != _queue.end(); ++it) {
        if ((flags & kReceiveToPlayer) && it->to != wantTo)
            continue;
        if ((flags & kReceiveFromPlayer) && it->from != wantFrom)
            continue;
        const bool system = it->type != 0;
        const uint32_t need = system ? systemBytes(*it, wide) : uint32_t(it->data.size());
        if (!buffer || c.read32(sizeAt) < need || !c.valid(buffer, need)) {
            c.write32(sizeAt, need);
            return kBufferTooSmall;
        }
        c.write32(fromAt, it->from);
        c.write32(toAt, it->to);
        if (system) {
            writeSystem(c, buffer, *it, wide);
        } else {
            for (size_t i = 0; i < it->data.size(); ++i)
                c.write8(buffer + uint32_t(i), it->data[i]);
        }
        c.write32(sizeAt, need);
        if (!(flags & kReceivePeek))
            _queue.erase(it);
        return kSOk;
    }
    return kNoMessages;
}

uint32_t Play::getMessageCount(Cpu &c) {
    const uint32_t player = c.arg(1);
    const uint32_t out = c.arg(2);
    if (!c.valid(out, 4))
        return kEInvalidArg;
    update();
    uint32_t n = 0;
    for (const Message &m : _queue)
        n += (m.to == player || m.type != 0) ? 1 : 0;
    c.write32(out, n);
    return kSOk;
}

// The network side.

void Play::sendReliable(uint32_t link, const Writer &event) {
    const auto l = _links.find(link);
    if (l == _links.end())
        return;
    Writer w(kReliable);
    w.u32(l->second.nextSend);
    w.b.insert(w.b.end(), event.b.begin(), event.b.end());
    const uint64_t now = nowMs();
    l->second.unacked[l->second.nextSend++] = {w.b, now};
    l->second.sentMs = now;
    if (_socket && _socket->send(l->second.address, w.b))
        ++s->traffic.sent;
}

void Play::sendEventToAll(const Writer &event, uint32_t except) {
    std::vector<uint32_t> ids;
    for (const auto &[id, link] : _links)
        if (id != except)
            ids.push_back(id);
    for (uint32_t id : ids)
        sendReliable(id, event);
}

uint32_t Play::linkOf(const net::Address &a) const {
    for (const auto &[id, link] : _links)
        if (link.address == a)
            return id;
    return 0;
}

void Play::update() {
    if (!_socket)
        return;
    net::Address from;
    std::vector<uint8_t> packet;
    for (int n = 0; n < 1000 && _socket && _socket->receive(from, packet); ++n) {
        ++s->traffic.received;
        handle(from, packet);
    }
    const uint64_t now = nowMs();
    std::vector<uint32_t> lost;
    for (auto &[id, link] : _links) {
        if (now - link.heardMs > kLostMs) {
            lost.push_back(id);
            continue;
        }
        for (auto &[seq, sent] : link.unacked) {
            if (now - sent.second >= kResendMs) {
                s->traffic.sent += _socket->send(link.address, sent.first) ? 1 : 0;
                sent.second = now;
                link.sentMs = now;
            }
        }
        if (now - link.sentMs >= kKeepAliveMs) {
            Writer w(kKeepAlive);
            s->traffic.sent += _socket->send(link.address, w.b) ? 1 : 0;
            link.sentMs = now;
        }
    }
    for (uint32_t id : lost)
        linkLost(id);
    if (_enumerating && now - _enumSentMs >= kEnumEveryMs)
        sendEnumRequests();
}

void Play::linkLost(uint32_t link) {
    if (!_links.erase(link))
        return;
    log(_role == Role::kHost ? "lost link " + std::to_string(link) : std::string("lost the host"));
    if (_role == Role::kHost) {
        std::vector<uint32_t> gone;
        for (const auto &[id, m] : _members)
            if (m.link == link)
                gone.push_back(id);
        for (uint32_t id : gone)
            removeMember(id, link);
        return;
    }
    // The host has gone: the session with it.
    const bool wasJoined = _role == Role::kJoined && _synced;
    reset();
    if (wasJoined) {
        _lost = true;
        Message msg;
        msg.type = kSysSessionLost;
        _queue.push_back(msg);
    }
}

void Play::handle(const net::Address &from, const std::vector<uint8_t> &packet) {
    if (packet.size() < 6 || packet[0] != 'R' || packet[1] != 'v' || packet[2] != 'D' || packet[3] != 'P' ||
        packet[4] != kVersion)
        return;
    Reader r{packet, 6};
    const uint32_t link = linkOf(from);
    if (link)
        _links[link].heardMs = nowMs();
    switch (packet[5]) {
    case kEnumRequest: {
        const Guid application = r.guid();
        const Guid none{};
        if (_role != Role::kHost || !r.ok || (application != none && application != _session.application))
            return;
        Writer w(kSessionFound);
        _session.write(w);
        uint32_t players = 0;
        for (const auto &[id, m] : _members)
            players += m.group ? 0 : 1;
        w.u32(players);
        for (const auto &[id, m] : _members)
            if (!m.group)
                m.write(w);
        sendTo(from, w);
        return;
    }
    case kSessionFound:
        sessionFound(from, r);
        return;
    case kJoin:
        handleJoin(from, r);
        return;
    case kRefused: {
        const uint32_t nonce = r.u32();
        const uint32_t result = r.u32();
        if (r.ok && _role == Role::kJoining && nonce == _joinNonce && from == _joinHost)
            _refusal = result ? result : kEFail;
        return;
    }
    case kWelcome: {
        const uint32_t nonce = r.u32();
        const uint32_t myLink = r.u32();
        SessionInfo session;
        session.read(r);
        if (!r.ok || _role != Role::kJoining || nonce != _joinNonce || from != _joinHost)
            return;
        _role = Role::kJoined;
        _myLink = myLink;
        _session = session;
        Link &host = _links[1];
        host.address = from;
        host.heardMs = host.sentMs = nowMs();
        return;
    }
    case kReliable: {
        if (!link)
            return;
        const uint32_t seq = r.u32();
        if (!r.ok)
            return;
        Link &l = _links[link];
        if (seq >= l.nextReceive)
            l.early[seq] = std::vector<uint8_t>(packet.begin() + long(r.at), packet.end());
        // Taken in order; an event may end the link.
        while (true) {
            const auto it = _links.find(link);
            if (it == _links.end())
                return;
            const auto next = it->second.early.find(it->second.nextReceive);
            if (next == it->second.early.end())
                break;
            const std::vector<uint8_t> event = std::move(next->second);
            it->second.early.erase(next);
            ++it->second.nextReceive;
            handleEvent(link, event);
        }
        const auto it = _links.find(link);
        if (it == _links.end())
            return;
        Writer done(kAck);
        done.u32(it->second.nextReceive - 1);
        sendTo(from, done);
        return;
    }
    case kAck: {
        const uint32_t upTo = r.u32();
        if (!link || !r.ok)
            return;
        auto &unacked = _links[link].unacked;
        unacked.erase(unacked.begin(), unacked.upper_bound(upTo));
        return;
    }
    case kData: {
        const uint32_t dpFrom = r.u32();
        const uint32_t to = r.u32();
        const std::vector<uint8_t> data = r.bytes();
        if (!link || !r.ok)
            return;
        const auto sender = _members.find(dpFrom);
        if (_role == Role::kHost && (sender == _members.end() || sender->second.link != link))
            return;
        route(link, dpFrom, to, data, false);
        return;
    }
    case kBye:
        if (link)
            linkLost(link);
        return;
    default:
        return;
    }
}

void Play::sessionFound(const net::Address &from, Reader &r) {
    Found f;
    f.session.read(r);
    const uint32_t players = r.u32();
    for (uint32_t i = 0; r.ok && i < players && i < 256; ++i) {
        Member m;
        m.read(r);
        f.players.push_back(m);
    }
    if (!r.ok)
        return;
    f.host = from;
    f.heardMs = nowMs();
    _found[f.session.instance] = f;
}

void Play::handleJoin(const net::Address &from, Reader &r) {
    const Guid instance = r.guid();
    const std::string password = r.text();
    const uint32_t nonce = r.u32();
    if (!r.ok || _role != Role::kHost || instance != _session.instance)
        return;
    // The same join again: the welcome was lost.
    if (const uint32_t known = linkOf(from); known) {
        if (_links[known].nonce == nonce) {
            Writer w(kWelcome);
            w.u32(nonce);
            w.u32(known);
            _session.write(w);
            sendTo(from, w);
        }
        return;
    }
    uint32_t refusal = 0;
    if (_session.flags & (kSessionNewPlayersDisabled | kSessionJoinDisabled))
        refusal = kNoNewPlayers;
    else if (!_session.password.empty() && password != _session.password)
        refusal = kInvalidPassword;
    if (refusal) {
        Writer w(kRefused);
        w.u32(nonce);
        w.u32(refusal);
        sendTo(from, w);
        return;
    }
    const uint32_t id = _nextLink++;
    Link &l = _links[id];
    l.address = from;
    l.nonce = nonce;
    l.heardMs = l.sentMs = nowMs();
    Writer w(kWelcome);
    w.u32(nonce);
    w.u32(id);
    _session.write(w);
    sendTo(from, w);
    // The session as it is, then word that it has all come.
    for (const auto &[mid, m] : _members) {
        Writer event;
        event.u8(kEventPlayer);
        m.write(event);
        sendReliable(id, event);
    }
    for (const auto &[gid, g] : _members) {
        for (uint32_t player : g.members) {
            Writer event;
            event.u8(kEventJoinGroup);
            event.u32(gid);
            event.u32(player);
            sendReliable(id, event);
        }
    }
    Writer synced;
    synced.u8(kEventSynced);
    sendReliable(id, synced);
}

void Play::handleEvent(uint32_t link, const std::vector<uint8_t> &event) {
    Reader r{event, 1};
    if (event.empty())
        return;
    // One who joined takes only the host's word; the host takes each one's
    // word for their own players.
    switch (event[0]) {
    case kEventPlayer: {
        Member m;
        m.read(r);
        if (!r.ok || (_role == Role::kHost && m.link != link))
            return;
        m.members.clear();
        addMember(m, link);
        return;
    }
    case kEventDestroy: {
        const uint32_t id = r.u32();
        const auto it = _members.find(id);
        if (!r.ok || it == _members.end() || (_role == Role::kHost && it->second.link != link))
            return;
        removeMember(id, link);
        return;
    }
    case kEventJoinGroup:
    case kEventLeaveGroup: {
        const uint32_t group = r.u32();
        const uint32_t player = r.u32();
        if (r.ok)
            membership(group, player, event[0] == kEventJoinGroup, link);
        return;
    }
    case kEventSession:
        if (_role == Role::kJoined) {
            const std::string password = _session.password;
            _session.read(r);
            _session.password = password;
        }
        return;
    case kEventData: {
        const uint32_t from = r.u32();
        const uint32_t to = r.u32();
        const std::vector<uint8_t> data = r.bytes();
        if (r.ok)
            route(link, from, to, data, true);
        return;
    }
    case kEventSynced:
        _synced = true;
        return;
    default:
        return;
    }
}

std::optional<uint32_t> Play::invoke(const ComInterface &iface, int index, Cpu &c) {
    std::lock_guard<std::recursive_mutex> guard(s->lock);
    const std::string_view m = iface.methods[size_t(index)].name;
    const bool w = wide(iface);
    if (m == "EnumConnections")
        return enumConnections(c, w);
    if (m == "InitializeConnection")
        return initializeConnection(c);
    if (m == "EnumSessions")
        return enumSessions(c, w);
    if (m == "Open")
        return open(c, w);
    if (m == "Close")
        return close();
    if (m == "CreatePlayer")
        return createMember(c, w, false);
    if (m == "CreateGroup")
        return createMember(c, w, true);
    if (m == "DestroyPlayer")
        return destroyMember(c.arg(1), false);
    if (m == "DestroyGroup")
        return destroyMember(c.arg(1), true);
    if (m == "AddPlayerToGroup")
        return groupMembership(c.arg(1), c.arg(2), true);
    if (m == "DeletePlayerFromGroup")
        return groupMembership(c.arg(1), c.arg(2), false);
    if (m == "EnumPlayers")
        return enumPlayers(c, w);
    if (m == "EnumGroupPlayers")
        return enumGroupPlayers(c, w);
    if (m == "GetSessionDesc")
        return getSessionDesc(c, w);
    if (m == "SetSessionDesc")
        return setSessionDesc(c, w);
    if (m == "GetCaps")
        return getCaps(c);
    if (m == "Send")
        return send(c);
    if (m == "Receive")
        return receive(c, w);
    if (m == "GetMessageCount")
        return getMessageCount(c);
    return std::nullopt;
}

} // namespace

DirectPlay::~DirectPlay() = default;

DirectPlay::DirectPlay(Win32Process &process) : _s(std::make_unique<State>(process, *this)) {
    State *s = _s.get();
    for (ComInterface *f : {&s->directPlay2, &s->directPlay2A, &s->directPlay3, &s->directPlay3A})
        s->com.define(*f);
    auto earlier = process.createComObject;
    process.createComObject = [s, &process, earlier](const std::string &clsid, const std::string &iid, uint32_t out) {
        if (clsid != kClsidDirectPlay)
            return earlier ? earlier(clsid, iid, out) : kClassNotRegistered;
        auto play = std::make_shared<Play>();
        play->s = s;
        const ComInterface *f = play->query(iid);
        if (!f)
            return kENoInterface;
        {
            std::lock_guard<std::recursive_mutex> guard(s->lock);
            std::erase_if(s->objects, [](const std::weak_ptr<ComObject> &o) { return o.expired(); });
            s->objects.push_back(play);
        }
        if (process.cpu().valid(out, 4))
            process.cpu().write32(out, s->com.pointer(play, *f));
        return kSOk;
    };
}

DirectPlay::Traffic DirectPlay::traffic() const {
    std::lock_guard<std::recursive_mutex> guard(_s->lock);
    return _s->traffic;
}

void DirectPlay::update() {
    std::lock_guard<std::recursive_mutex> guard(_s->lock);
    for (const std::weak_ptr<ComObject> &o : _s->objects)
        if (const auto play = std::dynamic_pointer_cast<Play>(o.lock()))
            play->update();
}

} // namespace reluva::x86
