// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectPlay of DirectX 6, from the SDK's documentation (dplay.h and
// dplobby.h's interfaces and their methods' order). Without a network: see
// docs/common/x86/dplay.md.

#include "emulation/x86/dplay.h"

#include "emulation/x86/com.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::x86 {

namespace {

constexpr uint32_t kDpOk = 0;
constexpr uint32_t kDpENoInterface = 0x80004002u;
constexpr uint32_t kDpClassNotRegistered = 0x80040154u;
constexpr uint32_t kDpErrInvalidParams = 0x80070057u;
// MAKE_DPHRESULT(code): 0x88770000 + code.
constexpr uint32_t kDpErrBufferTooSmall = 0x8877001eu; // 30
constexpr uint32_t kDpErrUnavailable = 0x887700d2u;    // 210
constexpr uint32_t kDpErrNotLobbied = 0x8877042eu;     // 1070
constexpr uint32_t kDpErrInvalidPlayer = 0x88770096u;  // 150
constexpr uint32_t kDpErrNoMessages = 0x887700beu;     // 190
constexpr uint32_t kDpErrNoConnection = 0x887700b4u;   // 180
constexpr uint32_t kDpErrPending = 0x8000000au;        // E_PENDING
constexpr uint32_t kDpErrGeneric = 0x80004005u;        // E_FAIL
// The service providers that find each other on a network.
const char *const kSpTcpIp = "{36e95ee0-8577-11cf-960c-0080c7534e82}";
const char *const kSpIpx = "{685bc400-9d2c-11cf-a9cd-00aa006886e3}";
const char *const kDpaidInet = "{c4a54da0-e0af-11cf-9c4e-00a0c905425e}";

// As loco.exe's own GUID table has them (IDirectPlay4's, the Unicode one,
// is its ANSI twin's less one, as the SDK numbers such pairs).
const char *const kIidDirectPlay = "{5454e9a0-db65-11ce-921c-00aa006c4972}";
const char *const kIidDirectPlay4 = "{0ab1c530-4745-11d1-a7a1-0000f803abfc}";
const char *const kIidDirectPlay4A = "{0ab1c531-4745-11d1-a7a1-0000f803abfc}";
const char *const kIidLobby = "{af465c71-9588-11cf-a020-00aa006157ac}";
const char *const kIidLobbyA = "{26c66a70-b367-11cf-a024-00aa006157ac}";
const char *const kIidLobby2 = "{0194c220-a303-11d0-9c4f-00a0c905425e}";
const char *const kIidLobby2A = "{1bb4af80-a303-11d0-9c4f-00a0c905425e}";
const char *const kIidLobby3 = "{2db72490-652c-11d1-a7a8-0000f803abfc}";
const char *const kIidLobby3A = "{2db72491-652c-11d1-a7a8-0000f803abfc}";
const char *const kClsidDirectPlay = "{d1eb6d20-8923-11d0-9d97-00a0c90a43cb}";
const char *const kClsidDirectPlayLobby = "{2fe8f810-b2a5-11d0-a787-0000f803abfc}";
// A DirectPlay address's elements' types.
const char *const kDpaidTotalSize = "{1318f560-912c-11d0-9daa-00a0c90a43cb}";
const char *const kDpaidServiceProvider = "{07d916c0-e0af-11cf-9c4e-00a0c905425e}";

using Methods = std::vector<ComInterface::Method>;

Methods withUnknown(Methods m) {
    m.insert(m.begin(), {{"QueryInterface", 2}, {"AddRef", 0}, {"Release", 0}});
    return m;
}

// IDirectPlay, DirectX 3's first interface, which DirectPlayCreate gives.
Methods directPlay1() {
    return withUnknown({{"AddPlayerToGroup", 2},
                        {"Close", 0},
                        {"CreatePlayer", 4},
                        {"CreateGroup", 3},
                        {"DeletePlayerFromGroup", 2},
                        {"DestroyPlayer", 1},
                        {"DestroyGroup", 1},
                        {"EnableNewPlayers", 1},
                        {"EnumGroupPlayers", 4},
                        {"EnumGroups", 4},
                        {"EnumPlayers", 4},
                        {"EnumSessions", 5},
                        {"GetCaps", 1},
                        {"GetMessageCount", 2},
                        {"GetPlayerCaps", 2},
                        {"GetPlayerName", 5},
                        {"Initialize", 1},
                        {"Open", 1},
                        {"Receive", 5},
                        {"SaveSession", 1},
                        {"Send", 5},
                        {"SetPlayerName", 3}});
}

// IDirectPlay4's: IDirectPlay2's, then 3's additions, then 4's.
Methods directPlay4() {
    return withUnknown({
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
        // IDirectPlay3
        {"AddGroupToGroup", 2},
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
        // IDirectPlay4
        {"GetGroupOwner", 2},
        {"SetGroupOwner", 2},
        {"SendEx", 9},
        {"GetMessageQueue", 5},
        {"CancelMessage", 2},
        {"CancelPriority", 3},
    });
}

// IDirectPlayLobby3's: the lobby's, then 2's addition, then 3's.
Methods lobby(int version) {
    Methods m = {{"Connect", 3},
                 {"CreateAddress", 6},
                 {"EnumAddress", 4},
                 {"EnumAddressTypes", 4},
                 {"EnumLocalApplications", 3},
                 {"GetConnectionSettings", 3},
                 {"ReceiveLobbyMessage", 5},
                 {"RunApplication", 4},
                 {"SendLobbyMessage", 4},
                 {"SetConnectionSettings", 3},
                 {"SetLobbyMessageEvent", 3}};
    if (version >= 2)
        m.push_back({"CreateCompoundAddress", 4});
    if (version >= 3)
        for (const ComInterface::Method &x : {ComInterface::Method{"ConnectEx", 4},
                                              {"RegisterApplication", 2},
                                              {"UnregisterApplication", 2},
                                              {"WaitForConnectionSettings", 1}})
            m.push_back(x);
    return withUnknown(m);
}

} // namespace

namespace {

// A message waiting to be received: from and to whom, and its bytes. A
// system message (from DPID_SYSMSG) is laid out as its structure with its
// pointers as offsets into it, `pointers` saying where those are, so it
// can be written anywhere.
struct Message {
    uint32_t from = 0;
    uint32_t to = 0;
    std::vector<uint8_t> data;
    std::vector<uint32_t> pointers;
};

} // namespace

// One DirectPlay object in a session: its local players and the messages
// waiting for them.
struct Node {
    std::string session; // its guidInstance; empty when in none
    std::deque<Message> queue;
    std::set<uint32_t> players; // and groups it made
};

struct DirectPlay::Network::State {
    // A player or a group.
    struct Member {
        Node *node = nullptr;
        bool group = false;
        std::string shortName;
        std::string longName;
        std::vector<uint8_t> data;
        uint32_t flags = 0;
        std::set<uint32_t> members; // a group's players
    };
    struct Session {
        std::string application;
        uint32_t flags = 0;
        uint32_t maxPlayers = 0; // 0: as many as come
        std::string name;
        std::string password;
        uint32_t user[4] = {};
        Node *host = nullptr;
        std::set<Node *> nodes;
        std::map<uint32_t, Member> members;
        uint32_t players() const {
            return uint32_t(
                std::count_if(members.begin(), members.end(), [](const auto &m) { return !m.second.group; }));
        }
    };
    std::mutex mutex;
    std::map<std::string, Session> sessions; // by guidInstance
    uint32_t nextId = 0x4c4f0002;            // above DPID_SYSMSG (0) and DPID_SERVERPLAYER (1)
    uint32_t nextGuid = 1;
    uint32_t nextMessage = 1; // SendEx's ids

    // Queues `m` for every node of `session` but `except`, to its first
    // player (or none).
    void tell(Session &session, const Node *except, Message m) {
        for (Node *n : session.nodes)
            if (n != except) {
                m.to = n->players.empty() ? 0 : *n->players.begin();
                n->queue.push_back(m);
            }
    }
};

DirectPlay::Network::Network() : _s(std::make_unique<State>()) {
}

DirectPlay::Network::~Network() = default;

size_t DirectPlay::Network::sessions() const {
    std::lock_guard<std::mutex> lock(_s->mutex);
    return _s->sessions.size();
}

struct DirectPlay::State {
    DirectPlay &owner;
    Win32Process &process;
    Cpu &cpu;
    ComHost com;
    ComInterface play1{"IDirectPlay", kIidDirectPlay, directPlay1()};
    ComInterface play4{"IDirectPlay4", kIidDirectPlay4, directPlay4()};
    ComInterface play4A{"IDirectPlay4A", kIidDirectPlay4A, directPlay4()};
    ComInterface lobby1{"IDirectPlayLobby", kIidLobby, lobby(1)};
    ComInterface lobby1A{"IDirectPlayLobbyA", kIidLobbyA, lobby(1)};
    ComInterface lobby2{"IDirectPlayLobby2", kIidLobby2, lobby(2)};
    ComInterface lobby2A{"IDirectPlayLobby2A", kIidLobby2A, lobby(2)};
    ComInterface lobby3{"IDirectPlayLobby3", kIidLobby3, lobby(3)};
    ComInterface lobby3A{"IDirectPlayLobby3A", kIidLobby3A, lobby(3)};
    State(DirectPlay &o, Win32Process &p) : owner(o), process(p), cpu(p.cpu()), com(p) {}
};

namespace {

// An address's elements: a type and its data, DPADDRESS (the type's GUID
// and the data's size) then the data, one after another.
struct Element {
    std::string type;
    std::vector<uint8_t> data;
};

uint32_t addressSize(const std::vector<Element> &elements) {
    uint32_t n = 0;
    for (const Element &e : elements)
        n += 20 + uint32_t(e.data.size());
    return n;
}

// Writes an address led by its total size (DPAID_TotalSize), as
// CreateAddress and CreateCompoundAddress make them; *size is the room
// there, set to what it takes.
uint32_t writeAddress(Cpu &c, std::vector<Element> elements, uint32_t out, uint32_t sizePtr) {
    Element total{kDpaidTotalSize, std::vector<uint8_t>(4)};
    elements.insert(elements.begin(), total);
    const uint32_t n = addressSize(elements);
    for (int i = 0; i < 4; ++i)
        elements[0].data[size_t(i)] = uint8_t(n >> (8 * i));
    if (!c.valid(sizePtr, 4))
        return kDpErrInvalidParams;
    const uint32_t room = c.read32(sizePtr);
    c.write32(sizePtr, n);
    if (!out || room < n || !c.valid(out, n))
        return kDpErrBufferTooSmall;
    uint32_t at = out;
    for (const Element &e : elements) {
        ComHost::writeGuid(c, at, e.type);
        c.write32(at + 16, uint32_t(e.data.size()));
        for (size_t i = 0; i < e.data.size(); ++i)
            c.write8(at + 20 + uint32_t(i), e.data[i]);
        at += 20 + uint32_t(e.data.size());
    }
    return kDpOk;
}

// Little-endian writing into a message's bytes.
void put32(std::vector<uint8_t> &b, size_t at, uint32_t v) {
    if (b.size() < at + 4)
        b.resize(at + 4);
    for (int i = 0; i < 4; ++i)
        b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

// Appends a string or bytes to a system message, its offset written at
// `field` (a pointer, to be made the receiver's) unless it's empty.
void attach(Message &m, size_t field, const std::vector<uint8_t> &bytes) {
    if (bytes.empty())
        return;
    put32(m.data, field, uint32_t(m.data.size()));
    m.pointers.push_back(uint32_t(field));
    m.data.insert(m.data.end(), bytes.begin(), bytes.end());
}

std::vector<uint8_t> text(const std::string &s) {
    if (s.empty())
        return {};
    std::vector<uint8_t> b(s.begin(), s.end());
    b.push_back(0);
    return b;
}

// DPSYS_CREATEPLAYERORGROUP (3): type, player or group, id, players now,
// its data, its DPNAME, its parent, flags.
Message created(uint32_t id, const DirectPlay::Network::State::Member &m, uint32_t players) {
    Message msg;
    msg.data.resize(48);
    put32(msg.data, 0, 3);
    put32(msg.data, 4, m.group ? 0 : 1); // DPPLAYERTYPE_GROUP, _PLAYER
    put32(msg.data, 8, id);
    put32(msg.data, 12, players);
    put32(msg.data, 20, uint32_t(m.data.size()));
    put32(msg.data, 24, 16); // DPNAME's size
    put32(msg.data, 44, m.flags);
    attach(msg, 16, m.data);
    attach(msg, 32, text(m.shortName));
    attach(msg, 36, text(m.longName));
    return msg;
}

// DPSYS_DESTROYPLAYERORGROUP (5): type, player or group, id, its local
// and remote data, its DPNAME, its parent, flags.
Message destroyed(uint32_t id, const DirectPlay::Network::State::Member &m) {
    Message msg;
    msg.data.resize(52);
    put32(msg.data, 0, 5);
    put32(msg.data, 4, m.group ? 0 : 1);
    put32(msg.data, 8, id);
    put32(msg.data, 24, uint32_t(m.data.size()));
    put32(msg.data, 28, 16);
    put32(msg.data, 48, m.flags);
    attach(msg, 20, m.data);
    attach(msg, 36, text(m.shortName));
    attach(msg, 40, text(m.longName));
    return msg;
}

// DPSYS_ADDPLAYERTOGROUP (7) and DPSYS_DELETEPLAYERFROMGROUP (0x21).
Message membership(uint32_t type, uint32_t group, uint32_t player) {
    Message msg;
    msg.data.resize(12);
    put32(msg.data, 0, type);
    put32(msg.data, 4, group);
    put32(msg.data, 8, player);
    return msg;
}

class Play : public ComObject {
public:
    DirectPlay::State *s = nullptr;
    std::string provider; // the service provider's GUID
    std::shared_ptr<DirectPlay::Network> network;
    Node node;

    ~Play() override { leave(); }

    const ComInterface *query(const std::string &iid) override {
        for (const ComInterface *f : {&s->play1, &s->play4, &s->play4A})
            if (iid == f->iid)
                return f;
        return nullptr;
    }

    // Whether its service provider finds a network: TCP/IP's and IPX's do
    // (both the same one), when the process has one.
    bool connected() const { return s->owner.network && (provider == kSpTcpIp || provider == kSpIpx); }
    DirectPlay::Network::State &net() const { return s->owner.network->state(); }

    // Its session, or null; the network's lock held.
    DirectPlay::Network::State::Session *session() const {
        if (!network || node.session.empty())
            return nullptr;
        auto it = network->state().sessions.find(node.session);
        return it == network->state().sessions.end() ? nullptr : &it->second;
    }

    // Leaves its session: its players and groups gone, the others told;
    // the host gone, the session's lost to them.
    void leave() {
        if (!network)
            return;
        DirectPlay::Network::State &n = network->state();
        std::lock_guard<std::mutex> lock(n.mutex);
        DirectPlay::Network::State::Session *sess = session();
        if (sess) {
            for (uint32_t id : node.players)
                if (auto it = sess->members.find(id); it != sess->members.end()) {
                    n.tell(*sess, &node, destroyed(id, it->second));
                    sess->members.erase(it);
                }
            for (auto &[id, m] : sess->members)
                for (uint32_t p : node.players)
                    m.members.erase(p);
            sess->nodes.erase(&node);
            if (sess->host == &node) {
                Message lost;
                lost.data.resize(4);
                put32(lost.data, 0, 0x31); // DPSYS_SESSIONLOST
                n.tell(*sess, &node, lost);
                sess->host = nullptr;
            }
            if (sess->nodes.empty())
                n.sessions.erase(node.session);
        }
        node.players.clear();
        node.session.clear();
        node.queue.clear();
    }

    // Writes a DPSESSIONDESC2 (80 bytes) at `at`, its name at `name`.
    void writeSession(Cpu &c, uint32_t at, uint32_t name, const std::string &guid,
                      const DirectPlay::Network::State::Session &sess) {
        for (uint32_t i = 0; i < 80; i += 4)
            c.write32(at + i, 0);
        c.write32(at, 80);
        c.write32(at + 4, sess.flags);
        ComHost::writeGuid(c, at + 8, guid);
        ComHost::writeGuid(c, at + 24, sess.application);
        c.write32(at + 40, sess.maxPlayers);
        c.write32(at + 44, sess.players());
        c.writeString(name, sess.name, uint32_t(sess.name.size() + 1));
        c.write32(at + 48, name);
        for (int i = 0; i < 4; ++i)
            c.write32(at + 64 + 4 * uint32_t(i), sess.user[i]);
    }

    // Reads a DPNAME's short and long names.
    static void readName(Cpu &c, uint32_t dpname, std::string &shortName, std::string &longName) {
        if (!dpname || !c.valid(dpname, 16))
            return;
        if (c.read32(dpname + 8))
            shortName = c.string(c.read32(dpname + 8));
        if (c.read32(dpname + 12))
            longName = c.string(c.read32(dpname + 12));
    }

    static std::vector<uint8_t> readBytes(Cpu &c, uint32_t at, uint32_t size) {
        std::vector<uint8_t> b;
        if (at && size && c.valid(at, size))
            b.assign(c.memory().data() + at, c.memory().data() + at + size);
        return b;
    }

    // Calls an EnumPlayers-style callback(id, type, DPNAME, flags, context)
    // for each of `ids`, until it says to stop.
    void enumerate(Cpu &c, const std::vector<std::pair<uint32_t, DirectPlay::Network::State::Member>> &ids,
                   uint32_t callback, uint32_t context) const {
        for (const auto &[id, m] : ids) {
            const uint32_t name = s->process.memory().alloc(uint32_t(16 + m.shortName.size() + m.longName.size() + 2));
            const uint32_t shortAt = name + 16;
            const uint32_t longAt = shortAt + uint32_t(m.shortName.size()) + 1;
            c.write32(name, 16);
            c.write32(name + 4, 0);
            c.writeString(shortAt, m.shortName, uint32_t(m.shortName.size() + 1));
            c.writeString(longAt, m.longName, uint32_t(m.longName.size() + 1));
            c.write32(name + 8, shortAt);
            c.write32(name + 12, longAt);
            uint32_t more = 0;
            const bool failed = bool(s->cpu.call(callback, {id, m.group ? 0u : 1u, name, m.flags, context}, more));
            s->process.memory().free(name);
            if (failed || !more)
                break;
        }
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (m == "Initialize")
            return kDpOk;
        if (m == "EnumConnections")
            return kDpOk;
        if (m == "Close") {
            leave();
            return kDpOk;
        }
        // InitializeConnection(address, flags): the service provider the
        // address names.
        if (m == "InitializeConnection") {
            const uint32_t address = c.arg(1);
            if (address && c.valid(address, 40) && ComHost::guid(c, address + 20 + 4) == kDpaidServiceProvider)
                provider = ComHost::guid(c, address + 24 + 20);
            return connected() ? kDpOk : kDpErrUnavailable;
        }
        // GetPlayerAddress(player, address, *size): for the local machine
        // (player 0) on a network, the service provider and, for TCP/IP,
        // an IP address.
        if (m == "GetPlayerAddress") {
            if (!connected()) {
                if (c.valid(c.arg(3), 4))
                    c.write32(c.arg(3), 0);
                return kDpErrUnavailable;
            }
            Element sp{kDpaidServiceProvider, {}};
            for (int i = 0; i < 16; ++i)
                sp.data.push_back(0);
            const uint32_t spAt = s->process.memory().alloc(16);
            ComHost::writeGuid(c, spAt, provider);
            for (uint32_t i = 0; i < 16; ++i)
                sp.data[i] = c.read8(spAt + i);
            s->process.memory().free(spAt);
            std::vector<Element> elements{sp};
            if (provider == kSpTcpIp)
                elements.push_back({kDpaidInet, text("127.0.0.1")});
            return writeAddress(c, elements, c.arg(2), c.arg(3));
        }
        if (!connected()) {
            if (m == "EnumSessions") {
                // The enumeration times out at once: the callback hears so
                // (DPESC_TIMEDOUT, 1) and is called no more.
                const uint32_t callback = c.arg(3);
                if (callback) {
                    const uint32_t timeout = s->process.memory().alloc(4);
                    c.write32(timeout, 0);
                    uint32_t more = 0;
                    s->cpu.call(callback, {0, timeout, 1, c.arg(4)}, more);
                    s->process.memory().free(timeout);
                }
                return kDpOk;
            }
            if (m == "Open" || m == "SecureOpen" || m == "GetCaps" || m == "GetPlayerCaps" || m == "GetMessageCount" ||
                m == "GetMessageQueue")
                return kDpErrUnavailable;
            return std::nullopt;
        }
        network = s->owner.network;
        DirectPlay::Network::State &n = net();
        // EnumSessions(desc, timeout, callback, context, flags): each
        // session of the desc's application (all for GUID_NULL), and with
        // DPENUMSESSIONS_AVAILABLE (1) only those that can be joined; then
        // the timeout, again while the callback asks for it.
        if (m == "EnumSessions") {
            const uint32_t desc = c.arg(1);
            const uint32_t callback = c.arg(3);
            const uint32_t context = c.arg(4);
            const uint32_t flags = c.arg(5);
            const std::string application = c.valid(desc, 80) ? ComHost::guid(c, desc + 24) : std::string();
            for (int rounds = 0; callback && rounds < 1000; ++rounds) {
                std::vector<std::pair<std::string, DirectPlay::Network::State::Session>> found;
                {
                    std::lock_guard<std::mutex> lock(n.mutex);
                    for (const auto &[guid, sess] : n.sessions) {
                        const bool closed =
                            (sess.flags & 0x21) || (sess.maxPlayers && sess.players() >= sess.maxPlayers);
                        if (sess.host &&
                            (application == "{00000000-0000-0000-0000-000000000000}" ||
                             application == sess.application) &&
                            !((flags & 1) && closed))
                            found.emplace_back(guid, sess);
                    }
                }
                const uint32_t block = s->process.memory().alloc(4 + 80 + 256);
                c.write32(block, c.arg(2));
                bool stop = false;
                for (const auto &[guid, sess] : found) {
                    writeSession(c, block + 4, block + 84, guid, sess);
                    uint32_t more = 0;
                    if (s->cpu.call(callback, {block + 4, block, 0, context}, more) || !more) {
                        stop = true;
                        break;
                    }
                }
                uint32_t again = 0;
                if (!stop)
                    s->cpu.call(callback, {0, block, 1, context}, again);
                s->process.memory().free(block);
                if (stop || !again)
                    break;
                // Asked to keep looking: a while, as the timeout would be.
                s->process.wait([] { return false; }, 100);
            }
            return kDpOk;
        }
        // Open(desc, flags): DPOPEN_CREATE (2) a session this object hosts,
        // DPOPEN_JOIN (1) the one with the desc's guidInstance.
        if (m == "Open") {
            const uint32_t desc = c.arg(1);
            const uint32_t flags = c.arg(2);
            if (!c.valid(desc, 80))
                return kDpErrInvalidParams;
            leave();
            std::lock_guard<std::mutex> lock(n.mutex);
            if (flags & 2) {
                DirectPlay::Network::State::Session sess;
                sess.application = ComHost::guid(c, desc + 24);
                sess.flags = c.read32(desc + 4);
                sess.maxPlayers = c.read32(desc + 40);
                if (c.read32(desc + 48))
                    sess.name = c.string(c.read32(desc + 48));
                for (int i = 0; i < 4; ++i)
                    sess.user[i] = c.read32(desc + 64 + 4 * uint32_t(i));
                sess.host = &node;
                sess.nodes.insert(&node);
                char guid[40];
                std::snprintf(guid, sizeof guid, "{%08x-5245-4c55-5641-000000000000}", n.nextGuid++);
                n.sessions[guid] = sess;
                node.session = guid;
                ComHost::writeGuid(c, desc + 8, guid);
                return kDpOk;
            }
            const std::string guid = ComHost::guid(c, desc + 8);
            auto it = n.sessions.find(guid);
            if (it == n.sessions.end() || !it->second.host)
                return kDpErrNoConnection;
            it->second.nodes.insert(&node);
            node.session = guid;
            return kDpOk;
        }
        std::lock_guard<std::mutex> lock(n.mutex);
        DirectPlay::Network::State::Session *sess = session();
        if (m == "GetCaps") {
            // DPCAPS: size, flags (DPCAPS_ISHOST 2), the largest message,
            // queue, players, speed (hundreds of baud), latency, local
            // players, header, timeout.
            const uint32_t caps = c.arg(1);
            if (!c.valid(caps, 40))
                return kDpErrInvalidParams;
            const uint32_t values[10] = {
                40, sess && sess->host == &node ? 2u : 0u, 0xffff, 0, 0xffff, 100000, 50, 0xffff, 0, 5000};
            for (int i = 0; i < 10; ++i)
                c.write32(caps + 4 * uint32_t(i), values[i]);
            return kDpOk;
        }
        if (!sess)
            return kDpErrNoConnection;
        // CreatePlayer(*id, DPNAME, event, data, size, flags) and
        // CreateGroup(*id, DPNAME, data, size, flags): the others told.
        if (m == "CreatePlayer" || m == "CreateGroup") {
            const bool group = m == "CreateGroup";
            DirectPlay::Network::State::Member member;
            member.node = &node;
            member.group = group;
            readName(c, c.arg(2), member.shortName, member.longName);
            member.data = readBytes(c, c.arg(group ? 3 : 4), c.arg(group ? 4 : 5));
            member.flags = c.arg(group ? 5 : 6);
            if (!c.valid(c.arg(1), 4))
                return kDpErrInvalidParams;
            const uint32_t id = n.nextId++;
            sess->members[id] = member;
            node.players.insert(id);
            c.write32(c.arg(1), id);
            n.tell(*sess, &node, created(id, member, sess->players()));
            return kDpOk;
        }
        if (m == "DestroyPlayer" || m == "DestroyGroup") {
            auto it = sess->members.find(c.arg(1));
            if (it == sess->members.end())
                return kDpErrInvalidPlayer;
            n.tell(*sess, &node, destroyed(it->first, it->second));
            for (auto &[id, other] : sess->members)
                other.members.erase(it->first);
            node.players.erase(it->first);
            sess->members.erase(it);
            return kDpOk;
        }
        // AddPlayerToGroup(group, player), DeletePlayerFromGroup.
        if (m == "AddPlayerToGroup" || m == "DeletePlayerFromGroup") {
            auto group = sess->members.find(c.arg(1));
            if (group == sess->members.end() || !group->second.group || !sess->members.count(c.arg(2)))
                return kDpErrInvalidPlayer;
            const bool add = m == "AddPlayerToGroup";
            if (add)
                group->second.members.insert(c.arg(2));
            else
                group->second.members.erase(c.arg(2));
            n.tell(*sess, &node, membership(add ? 7 : 0x21, c.arg(1), c.arg(2)));
            return kDpOk;
        }
        // EnumPlayers(guid, callback, context, flags), EnumGroups, and
        // EnumGroupPlayers(group, guid, callback, context, flags).
        if (m == "EnumPlayers" || m == "EnumGroups" || m == "EnumGroupPlayers") {
            std::vector<std::pair<uint32_t, DirectPlay::Network::State::Member>> ids;
            const bool ofGroup = m == "EnumGroupPlayers";
            const DirectPlay::Network::State::Member *group = nullptr;
            if (ofGroup) {
                auto it = sess->members.find(c.arg(1));
                if (it == sess->members.end() || !it->second.group)
                    return kDpErrInvalidPlayer;
                group = &it->second;
            }
            for (const auto &[id, member] : sess->members)
                if (ofGroup ? group->members.count(id) != 0 : member.group == (m == "EnumGroups"))
                    ids.emplace_back(id, member);
            const uint32_t callback = c.arg(ofGroup ? 3 : 2);
            const uint32_t context = c.arg(ofGroup ? 4 : 3);
            n.mutex.unlock();
            enumerate(c, ids, callback, context);
            n.mutex.lock();
            return kDpOk;
        }
        // Send(from, to, flags, data, size) and SendEx(..., priority,
        // timeout, context, *id): to a player, a group's players, or every
        // player (DPID_ALLPLAYERS, 0), never back to the sender. Delivered
        // at once; an asynchronous SendEx (DPSEND_ASYNC, 0x200) says it's
        // pending, and is told complete unless asked not to (0x400).
        if (m == "Send" || m == "SendEx") {
            const uint32_t from = c.arg(1);
            const uint32_t to = c.arg(2);
            Message msg;
            msg.from = from;
            msg.data = readBytes(c, c.arg(4), c.arg(5));
            std::set<uint32_t> targets;
            if (to == 0) {
                for (const auto &[id, member] : sess->members)
                    if (!member.group && id != from)
                        targets.insert(id);
            } else if (auto it = sess->members.find(to); it != sess->members.end()) {
                if (it->second.group)
                    targets = it->second.members;
                else
                    targets.insert(to);
                targets.erase(from);
            } else {
                return kDpErrInvalidPlayer;
            }
            for (uint32_t t : targets) {
                auto it = sess->members.find(t);
                if (it == sess->members.end() || !it->second.node)
                    continue;
                msg.to = t;
                it->second.node->queue.push_back(msg);
            }
            if (m == "SendEx" && (c.arg(3) & 0x200)) {
                const uint32_t id = n.nextMessage++;
                if (c.arg(9) && c.valid(c.arg(9), 4))
                    c.write32(c.arg(9), id);
                if (!(c.arg(3) & 0x400)) {
                    // DPSYS_SENDCOMPLETE: from, to, flags, priority,
                    // timeout, context, the message's id, its result, time.
                    Message done;
                    done.data.resize(40);
                    const uint32_t fields[10] = {0x10d, from, to, c.arg(3), c.arg(6), c.arg(7), c.arg(8), id, kDpOk, 0};
                    for (int i = 0; i < 10; ++i)
                        put32(done.data, 4 * size_t(i), fields[i]);
                    done.to = from;
                    node.queue.push_back(done);
                }
                return kDpErrPending;
            }
            return kDpOk;
        }
        // Receive(*from, *to, flags, data, *size): DPRECEIVE_ALL (1),
        // _TOPLAYER (2), _FROMPLAYER (4), _PEEK (8).
        if (m == "Receive") {
            const uint32_t fromPtr = c.arg(1);
            const uint32_t toPtr = c.arg(2);
            const uint32_t flags = c.arg(3);
            const uint32_t data = c.arg(4);
            const uint32_t sizePtr = c.arg(5);
            auto it = std::find_if(node.queue.begin(), node.queue.end(), [&](const Message &msg) {
                if ((flags & 4) && c.valid(fromPtr, 4) && msg.from != c.read32(fromPtr))
                    return false;
                return !((flags & 2) && c.valid(toPtr, 4) && msg.to != c.read32(toPtr));
            });
            if (it == node.queue.end())
                return kDpErrNoMessages;
            if (!c.valid(sizePtr, 4))
                return kDpErrInvalidParams;
            const uint32_t size = uint32_t(it->data.size());
            const uint32_t room = c.read32(sizePtr);
            c.write32(sizePtr, size);
            if (!data || room < size || !c.valid(data, size))
                return kDpErrBufferTooSmall;
            for (uint32_t i = 0; i < size; ++i)
                c.write8(data + i, it->data[i]);
            for (uint32_t field : it->pointers)
                c.write32(data + field, data + c.read32(data + field));
            if (c.valid(fromPtr, 4))
                c.write32(fromPtr, it->from);
            if (c.valid(toPtr, 4))
                c.write32(toPtr, it->to);
            if (!(flags & 8))
                node.queue.erase(it);
            return kDpOk;
        }
        // GetMessageCount(player, *count).
        if (m == "GetMessageCount") {
            if (!c.valid(c.arg(2), 4))
                return kDpErrInvalidParams;
            c.write32(c.arg(2), uint32_t(std::count_if(node.queue.begin(), node.queue.end(), [&](const Message &msg) {
                          return c.arg(1) == 0 || msg.to == c.arg(1);
                      })));
            return kDpOk;
        }
        // GetMessageQueue(from, to, flags, *count, *bytes): what's waiting
        // to be received (DPMESSAGEQUEUE_RECEIVE, 2); nothing waits to be
        // sent.
        if (m == "GetMessageQueue") {
            uint32_t count = 0;
            uint32_t bytes = 0;
            if (c.arg(3) & 2)
                for (const Message &msg : node.queue)
                    if ((!c.arg(1) || msg.from == c.arg(1)) && (!c.arg(2) || msg.to == c.arg(2))) {
                        ++count;
                        bytes += uint32_t(msg.data.size());
                    }
            if (c.arg(4) && c.valid(c.arg(4), 4))
                c.write32(c.arg(4), count);
            if (c.arg(5) && c.valid(c.arg(5), 4))
                c.write32(c.arg(5), bytes);
            return kDpOk;
        }
        // CancelMessage(id, flags): every message has gone already.
        if (m == "CancelMessage")
            return kDpErrGeneric;
        return std::nullopt;
    }
};

class Lobby : public ComObject {
public:
    DirectPlay::State *s = nullptr;
    const ComInterface *query(const std::string &iid) override {
        for (const ComInterface *f : {&s->lobby1, &s->lobby1A, &s->lobby2, &s->lobby2A, &s->lobby3, &s->lobby3A})
            if (iid == f->iid)
                return f;
        return nullptr;
    }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // No lobby started the program.
        if (m == "GetConnectionSettings" || m == "Connect" || m == "ConnectEx" || m == "WaitForConnectionSettings" ||
            m == "ReceiveLobbyMessage" || m == "SendLobbyMessage" || m == "SetConnectionSettings")
            return kDpErrNotLobbied;
        // No other programs are registered, and no service provider has address types.
        if (m == "EnumLocalApplications" || m == "EnumAddressTypes" || m == "SetLobbyMessageEvent" ||
            m == "RegisterApplication" || m == "UnregisterApplication")
            return kDpOk;
        if (m == "RunApplication")
            return kDpErrUnavailable;
        // CreateAddress(service provider, type, data, size, address, *size).
        if (m == "CreateAddress") {
            if (!c.valid(c.arg(1), 16) || !c.valid(c.arg(2), 16))
                return kDpErrInvalidParams;
            Element sp{ComHost::guid(c, c.arg(1)), {}};
            for (int i = 0; i < 16; ++i)
                sp.data.push_back(c.read8(c.arg(1) + uint32_t(i)));
            Element data{ComHost::guid(c, c.arg(2)), {}};
            for (uint32_t i = 0; i < c.arg(4); ++i)
                data.data.push_back(c.read8(c.arg(3) + i));
            sp.type = kDpaidServiceProvider;
            return writeAddress(c, {sp, data}, c.arg(5), c.arg(6));
        }
        // CreateCompoundAddress(elements, count, address, *size): each
        // DPCOMPOUNDADDRESSELEMENT is a type's GUID, a size and a pointer.
        if (m == "CreateCompoundAddress") {
            std::vector<Element> elements;
            for (uint32_t i = 0; i < c.arg(2); ++i) {
                const uint32_t at = c.arg(1) + i * 24;
                Element e{ComHost::guid(c, at), {}};
                const uint32_t size = c.read32(at + 16);
                const uint32_t data = c.read32(at + 20);
                for (uint32_t j = 0; j < size; ++j)
                    e.data.push_back(c.read8(data + j));
                elements.push_back(std::move(e));
            }
            return writeAddress(c, elements, c.arg(3), c.arg(4));
        }
        // EnumAddress(callback, address, size, context): each element to
        // callback(type, size, data, context) until it returns FALSE.
        if (m == "EnumAddress") {
            const uint32_t callback = c.arg(1);
            const uint32_t end = c.arg(2) + c.arg(3);
            for (uint32_t at = c.arg(2); at + 20 <= end;) {
                const uint32_t size = c.read32(at + 16);
                uint32_t more = 0;
                if (s->cpu.call(callback, {at, size, at + 20, c.arg(4)}, more) || !more)
                    break;
                at += 20 + size;
            }
            return kDpOk;
        }
        return std::nullopt;
    }
};

// Makes an object and writes its pointer as `iface` to `out`.
template<class T>
uint32_t make(DirectPlay::State *s, const ComInterface &iface, uint32_t out) {
    auto o = std::make_shared<T>();
    o->s = s;
    if (!s->cpu.valid(out, 4))
        return kDpErrInvalidParams;
    s->cpu.write32(out, s->com.pointer(o, iface));
    return kDpOk;
}

} // namespace

DirectPlay::~DirectPlay() = default;

DirectPlay::DirectPlay(Win32Process &process) : _s(std::make_unique<State>(*this, process)) {
    State *s = _s.get();
    for (ComInterface *f :
         {&s->play1, &s->play4, &s->play4A, &s->lobby1, &s->lobby1A, &s->lobby2, &s->lobby2A, &s->lobby3, &s->lobby3A})
        s->com.define(*f);
    // The exports, by ordinal as programs import them: 1 DirectPlayCreate,
    // 2 DirectPlayEnumerateA, 3 DirectPlayEnumerateW, 4
    // DirectPlayLobbyCreateA, 5 DirectPlayLobbyCreateW.
    const char *dll = "DPLAYX.dll";
    // DirectPlayCreate(service provider, *object, outer).
    for (const char *name : {"#1", "DirectPlayCreate"})
        process.provideStdcall(dll, name, 3, [s](Cpu &c) {
            auto o = std::make_shared<Play>();
            o->s = s;
            if (c.arg(0) && c.valid(c.arg(0), 16))
                o->provider = ComHost::guid(c, c.arg(0));
            if (!c.valid(c.arg(1), 4))
                return kDpErrInvalidParams;
            c.write32(c.arg(1), s->com.pointer(o, s->play1));
            return kDpOk;
        });
    // DirectPlayEnumerate(callback, context): the service providers; there are none.
    for (const char *name : {"#2", "#3", "DirectPlayEnumerateA", "DirectPlayEnumerateW", "DirectPlayEnumerate"})
        process.provideStdcall(dll, name, 2, [](Cpu &) { return kDpOk; });
    for (const char *name : {"#4", "DirectPlayLobbyCreateA"})
        process.provideStdcall(dll, name, 5, [s](Cpu &c) { return make<Lobby>(s, s->lobby1A, c.arg(1)); });
    for (const char *name : {"#5", "DirectPlayLobbyCreateW"})
        process.provideStdcall(dll, name, 5, [s](Cpu &c) { return make<Lobby>(s, s->lobby1, c.arg(1)); });

    auto earlier = process.createComObject;
    process.createComObject = [s, earlier](const std::string &clsid, const std::string &iid, uint32_t out) -> uint32_t {
        std::shared_ptr<ComObject> o;
        if (clsid == kClsidDirectPlay) {
            auto p = std::make_shared<Play>();
            p->s = s;
            o = p;
        } else if (clsid == kClsidDirectPlayLobby) {
            auto l = std::make_shared<Lobby>();
            l->s = s;
            o = l;
        } else {
            return earlier ? earlier(clsid, iid, out) : kDpClassNotRegistered;
        }
        const ComInterface *f = o->query(iid);
        if (!f)
            return kDpENoInterface;
        if (!s->cpu.valid(out, 4))
            return kDpErrInvalidParams;
        s->cpu.write32(out, s->com.pointer(o, *f));
        return kDpOk;
    };
}

} // namespace reluva::x86
