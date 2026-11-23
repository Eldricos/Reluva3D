// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectPlay of DirectX 6, from the SDK's documentation (dplay.h and
// dplobby.h's interfaces and their methods' order); its sessions over the
// network are dplay_session.cpp's. See docs/common/x86/dplay.md.

#include "emulation/x86/dplay.h"

#include "emulation/x86/com.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace reluva::x86 {

namespace {

constexpr uint32_t kDpOk = 0;
constexpr uint32_t kDpENoInterface = 0x80004002u;
constexpr uint32_t kDpClassNotRegistered = 0x80040154u;
constexpr uint32_t kDpErrInvalidParams = 0x80070057u;
// MAKE_DPHRESULT(code): 0x88770000 + code.
constexpr uint32_t kDpErrBufferTooSmall = 0x8877001eu;    // 30
constexpr uint32_t kDpErrUnavailable = 0x887700d2u;       // 210
constexpr uint32_t kDpErrNotLobbied = 0x8877042eu;        // 1070
constexpr uint32_t kDpErrInvalidPlayer = 0x88770096u;     // 150
constexpr uint32_t kDpErrNoMessages = 0x887700beu;        // 190
constexpr uint32_t kDpErrNoConnection = 0x887700b4u;      // 180
constexpr uint32_t kDpErrPending = 0x8000000au;           // E_PENDING
constexpr uint32_t kDpErrGeneric = 0x80004005u;           // E_FAIL
constexpr uint32_t kDpErrConnecting = 0x8877015eu;        // 350
constexpr uint32_t kDpErrCantCreateSession = 0x887700a0u; // 160
// How long a search waits for answers when the program doesn't say.
constexpr uint32_t kSearchMs = 300;
// The service providers that find each other on a network.
const char *const kSpTcpIp = "{36e95ee0-8577-11cf-960c-0080c7534e82}";
const char *const kSpIpx = "{685bc400-9d2c-11cf-a9cd-00aa006886e3}";
const char *const kDpaidInet = "{c4a54da0-e0af-11cf-9c4e-00a0c905425e}";
const char *const kDpaidInetPort = "{e4524541-8ea5-11d1-8a96-006097b01411}";
const char *const kNullGuid = "{00000000-0000-0000-0000-000000000000}";

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
    // For summary(): the endpoint of the session it's in, if any (its
    // object's own, which clears it when it goes).
    std::mutex mutex;
    DirectPlay::Summary summary;
    dplay::Endpoint *inSession = nullptr;
    // The players in the session now, kept as the most there have been:
    // at each message sent or looked for.
    void notePlayers() {
        std::lock_guard<std::mutex> lock(mutex);
        if (inSession)
            if (const auto session = inSession->session())
                summary.players = std::max(summary.players, session->players);
    }
};

DirectPlay::Summary DirectPlay::summary() const {
    _s->notePlayers();
    std::lock_guard<std::mutex> lock(_s->mutex);
    return _s->summary;
}

std::shared_ptr<DirectPlay::Network> DirectPlay::Network::inMemory() {
    auto network = std::make_shared<Network>();
    auto loopback = net::LoopbackNetwork::create();
    network->open = [loopback](uint16_t port) { return loopback->open(port); };
    return network;
}

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

// Reads an address led by its total size, as InitializeConnection is given
// one (with no size beside it).
std::vector<Element> readAddress(Cpu &c, uint32_t at) {
    std::vector<Element> elements;
    if (!at || !c.valid(at, 24) || ComHost::guid(c, at) != kDpaidTotalSize)
        return elements;
    const uint32_t total = c.read32(at + 20);
    uint32_t p = at;
    while (p + 20 <= at + total && c.valid(p, 20)) {
        Element e{ComHost::guid(c, p), {}};
        const uint32_t n = c.read32(p + 16);
        if (p + 20 + n > at + total || !c.valid(p + 20, n))
            break;
        for (uint32_t i = 0; i < n; ++i)
            e.data.push_back(c.read8(p + 20 + i));
        elements.push_back(e);
        p += 20 + n;
    }
    return elements;
}

// Little-endian writing into a message's bytes.
void put32(std::vector<uint8_t> &b, size_t at, uint32_t v) {
    if (b.size() < at + 4)
        b.resize(at + 4);
    for (int i = 0; i < 4; ++i)
        b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

// A system message laid out for the program: its pointers (to names and
// data) as offsets into it, `pointers` saying where those are, so it can be
// written anywhere.
struct Laid {
    std::vector<uint8_t> data;
    std::vector<uint32_t> pointers;
};

// Appends a string or bytes to a system message, its offset written at
// `field` unless it's empty.
void attach(Laid &m, size_t field, const std::vector<uint8_t> &bytes) {
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

// The system message an event is, as dplay.h lays them out.
Laid layOut(const dplay::Event &e) {
    Laid msg;
    switch (e.kind) {
    case dplay::Event::kCreated:
        // DPSYS_CREATEPLAYERORGROUP (3): type, player or group, id, players
        // now, its data, its DPNAME, its parent, flags.
        msg.data.resize(48);
        put32(msg.data, 0, 3);
        put32(msg.data, 4, e.member.group ? 0 : 1); // DPPLAYERTYPE_GROUP, _PLAYER
        put32(msg.data, 8, e.id);
        put32(msg.data, 12, e.players);
        put32(msg.data, 20, uint32_t(e.member.data.size()));
        put32(msg.data, 24, 16); // DPNAME's size
        put32(msg.data, 44, e.member.flags);
        attach(msg, 16, e.member.data);
        attach(msg, 32, text(e.member.shortName));
        attach(msg, 36, text(e.member.longName));
        break;
    case dplay::Event::kDestroyed:
        // DPSYS_DESTROYPLAYERORGROUP (5): type, player or group, id, its
        // local and remote data, its DPNAME, its parent, flags.
        msg.data.resize(52);
        put32(msg.data, 0, 5);
        put32(msg.data, 4, e.member.group ? 0 : 1);
        put32(msg.data, 8, e.id);
        put32(msg.data, 24, uint32_t(e.member.data.size()));
        put32(msg.data, 28, 16);
        put32(msg.data, 48, e.member.flags);
        attach(msg, 20, e.member.data);
        attach(msg, 36, text(e.member.shortName));
        attach(msg, 40, text(e.member.longName));
        break;
    case dplay::Event::kAdded:
    case dplay::Event::kRemoved:
        // DPSYS_ADDPLAYERTOGROUP (7), DPSYS_DELETEPLAYERFROMGROUP (0x21).
        msg.data.resize(12);
        put32(msg.data, 0, e.kind == dplay::Event::kAdded ? 7 : 0x21);
        put32(msg.data, 4, e.id);
        put32(msg.data, 8, e.player);
        break;
    case dplay::Event::kSessionLost:
        msg.data.resize(4);
        put32(msg.data, 0, 0x31); // DPSYS_SESSIONLOST
        break;
    case dplay::Event::kHost:
        msg.data.resize(4);
        put32(msg.data, 0, 0x101); // DPSYS_HOST
        break;
    }
    return msg;
}

class Play : public ComObject {
public:
    DirectPlay::State *s = nullptr;
    std::string provider; // the service provider's GUID
    std::unique_ptr<dplay::Endpoint> endpoint;
    net::Address where;       // the host's address and port given, if any
    uint32_t nextMessage = 1; // SendEx's ids

    ~Play() override {
        if (s && endpoint) {
            std::lock_guard<std::mutex> lock(s->mutex);
            if (s->inSession == endpoint.get())
                s->inSession = nullptr;
        }
    }

    const ComInterface *query(const std::string &iid) override {
        for (const ComInterface *f : {&s->play1, &s->play4, &s->play4A})
            if (iid == f->iid)
                return f;
        return nullptr;
    }

    // Whether its service provider finds a network: TCP/IP's and IPX's do
    // (both the same one), when the process has one.
    bool connected() const { return s->owner.network && (provider == kSpTcpIp || provider == kSpIpx); }

    // Waits up to `ms` for `ready`, letting the program's other threads
    // run; a program without threads of its own (a test) just waits.
    void pause(const std::function<bool()> &ready, uint32_t ms) const {
        if (s->process.currentThread()) {
            s->process.wait(ready, ms);
            return;
        }
        for (uint32_t waited = 0; waited < ms && !ready(); waited += 5)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Writes a DPSESSIONDESC2 (80 bytes) at `at`, its name at `name`.
    static void writeSession(Cpu &c, uint32_t at, uint32_t name, const dplay::SessionInfo &sess) {
        for (uint32_t i = 0; i < 80; i += 4)
            c.write32(at + i, 0);
        c.write32(at, 80);
        c.write32(at + 4, sess.flags);
        ComHost::writeGuid(c, at + 8, sess.guid);
        ComHost::writeGuid(c, at + 24, sess.application);
        c.write32(at + 40, sess.maxPlayers);
        c.write32(at + 44, sess.players);
        c.writeString(name, sess.name, uint32_t(sess.name.size() + 1));
        c.write32(at + 48, name);
        for (int i = 0; i < 4; ++i)
            c.write32(at + 64 + 4 * uint32_t(i), sess.user[size_t(i)]);
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
    void enumerate(Cpu &c, const std::vector<std::pair<uint32_t, dplay::Member>> &ids, uint32_t callback,
                   uint32_t context) const {
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

    // Tells whoever runs the program, if anyone listens.
    void say(const std::string &text) const {
        if (s->process.output)
            s->process.output(text + "\n");
    }

    static uint32_t result(dplay::Endpoint::Status status) {
        switch (status) {
        case dplay::Endpoint::Status::kOk:
            return kDpOk;
        case dplay::Endpoint::Status::kConnecting:
            return kDpErrConnecting;
        case dplay::Endpoint::Status::kInvalidMember:
            return kDpErrInvalidPlayer;
        case dplay::Endpoint::Status::kNoSocket:
            return kDpErrCantCreateSession;
        default:
            return kDpErrNoConnection;
        }
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (m == "Initialize")
            return kDpOk;
        if (m == "EnumConnections")
            return kDpOk;
        if (m == "Close") {
            if (endpoint)
                endpoint->leave();
            return kDpOk;
        }
        // CancelMessage(id, flags): every message has gone already.
        if (m == "CancelMessage")
            return kDpErrGeneric;
        // InitializeConnection(address, flags): the service provider the
        // address names, and the host's address and port, if it gives them.
        if (m == "InitializeConnection") {
            where = {};
            for (const Element &e : readAddress(c, c.arg(1))) {
                if (e.type == kDpaidServiceProvider && e.data.size() == 16) {
                    const uint32_t at = s->process.memory().alloc(16);
                    for (uint32_t i = 0; i < 16; ++i)
                        c.write8(at + i, e.data[i]);
                    provider = ComHost::guid(c, at);
                    s->process.memory().free(at);
                } else if (e.type == kDpaidInet) {
                    net::Address a;
                    if (net::parseAddress(std::string(e.data.begin(), std::find(e.data.begin(), e.data.end(), 0)), 0,
                                          a))
                        where.ip = a.ip;
                } else if (e.type == kDpaidInetPort && e.data.size() >= 2) {
                    where.port = uint16_t(e.data[0] | (e.data[1] << 8));
                }
            }
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
            const uint32_t spAt = s->process.memory().alloc(16);
            ComHost::writeGuid(c, spAt, provider);
            for (uint32_t i = 0; i < 16; ++i)
                sp.data.push_back(c.read8(spAt + i));
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
        if (!endpoint)
            endpoint = std::make_unique<dplay::Endpoint>(*s->owner.network);
        dplay::Endpoint &ep = *endpoint;
        // EnumSessions(desc, timeout, callback, context, flags): each
        // session of the desc's application (all for GUID_NULL) that has
        // answered, and with DPENUMSESSIONS_AVAILABLE (1) only those that
        // can be joined; then the timeout, again while the callback asks
        // for it.
        if (m == "EnumSessions") {
            const uint32_t desc = c.arg(1);
            const uint32_t callback = c.arg(3);
            const uint32_t context = c.arg(4);
            const uint32_t flags = c.arg(5);
            const uint32_t waitMs = std::min<uint32_t>(c.arg(2) ? c.arg(2) : kSearchMs, 5000);
            const std::string application = c.valid(desc, 80) ? ComHost::guid(c, desc + 24) : kNullGuid;
            for (int rounds = 0; callback && rounds < 1000; ++rounds) {
                const uint64_t before = ep.answers();
                ep.search(application, where);
                pause([&] { return ep.answers() > before; }, waitMs);
                const uint32_t block = s->process.memory().alloc(4 + 80 + 256);
                c.write32(block, c.arg(2));
                bool stop = false;
                for (const dplay::SessionInfo &sess : ep.found()) {
                    const bool closed = (sess.flags & 0x21) || (sess.maxPlayers && sess.players >= sess.maxPlayers);
                    if ((application != kNullGuid && application != sess.application) || ((flags & 1) && closed))
                        continue;
                    writeSession(c, block + 4, block + 84, sess);
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
            }
            return kDpOk;
        }
        // Open(desc, flags): DPOPEN_CREATE (2) a session this object hosts,
        // DPOPEN_JOIN (1) the one with the desc's guidInstance; with
        // DPOPEN_RETURNSTATUS (0x80) a join under way says so rather than
        // waiting.
        if (m == "Open") {
            const uint32_t desc = c.arg(1);
            const uint32_t flags = c.arg(2);
            if (!c.valid(desc, 80))
                return kDpErrInvalidParams;
            const std::string password = c.read32(desc + 52) ? c.string(c.read32(desc + 52)) : std::string();
            if (flags & 2) {
                dplay::SessionInfo sess;
                sess.application = ComHost::guid(c, desc + 24);
                sess.flags = c.read32(desc + 4);
                sess.maxPlayers = c.read32(desc + 40);
                if (c.read32(desc + 48))
                    sess.name = c.string(c.read32(desc + 48));
                sess.password = password;
                for (int i = 0; i < 4; ++i)
                    sess.user[size_t(i)] = c.read32(desc + 64 + 4 * uint32_t(i));
                const dplay::Endpoint::Status status = ep.host(sess, where.port);
                if (status == dplay::Endpoint::Status::kOk) {
                    ComHost::writeGuid(c, desc + 8, sess.guid);
                    say("DirectPlay: hosting \"" + sess.name + "\" on UDP port " + std::to_string(ep.port()));
                    std::lock_guard<std::mutex> lock(s->mutex);
                    s->summary.hosted = true;
                    s->inSession = &ep;
                }
                return result(status);
            }
            const std::string guid = ComHost::guid(c, desc + 8);
            dplay::Endpoint::Status status = ep.join(guid, password);
            if (!(flags & 0x80))
                for (int tries = 0; status == dplay::Endpoint::Status::kConnecting && tries < 200; ++tries) {
                    pause([] { return false; }, 50);
                    status = ep.join(guid, password);
                }
            if (status == dplay::Endpoint::Status::kOk) {
                say("DirectPlay: joined \"" + ep.session().value_or(dplay::SessionInfo()).name + "\"");
                std::lock_guard<std::mutex> lock(s->mutex);
                s->summary.joined = true;
                s->inSession = &ep;
            } else if (status == dplay::Endpoint::Status::kRefused)
                say("DirectPlay: the host turned down joining");
            return result(status);
        }
        if (m == "GetCaps") {
            // DPCAPS: size, flags (DPCAPS_ISHOST 2), the largest message,
            // queue, players, speed (hundreds of baud), latency, local
            // players, header, timeout.
            const uint32_t caps = c.arg(1);
            if (!c.valid(caps, 40))
                return kDpErrInvalidParams;
            const uint32_t values[10] = {40, ep.hosting() ? 2u : 0u, 0xffff, 0, 0xffff, 100000, 50, 0xffff, 0, 5000};
            for (int i = 0; i < 10; ++i)
                c.write32(caps + 4 * uint32_t(i), values[i]);
            return kDpOk;
        }
        // CreatePlayer(*id, DPNAME, event, data, size, flags) and
        // CreateGroup(*id, DPNAME, data, size, flags): the others told.
        if (m == "CreatePlayer" || m == "CreateGroup") {
            const bool group = m == "CreateGroup";
            if (!c.valid(c.arg(1), 4))
                return kDpErrInvalidParams;
            dplay::Member member;
            member.group = group;
            readName(c, c.arg(2), member.shortName, member.longName);
            member.data = readBytes(c, c.arg(group ? 3 : 4), c.arg(group ? 4 : 5));
            member.flags = c.arg(group ? 5 : 6);
            uint32_t id = 0;
            const dplay::Endpoint::Status status = ep.create(member, id);
            if (status == dplay::Endpoint::Status::kOk)
                c.write32(c.arg(1), id);
            return result(status);
        }
        if (m == "DestroyPlayer" || m == "DestroyGroup")
            return result(ep.destroy(c.arg(1)));
        // AddPlayerToGroup(group, player), DeletePlayerFromGroup.
        if (m == "AddPlayerToGroup" || m == "DeletePlayerFromGroup")
            return result(ep.membership(c.arg(1), c.arg(2), m == "AddPlayerToGroup"));
        // EnumPlayers(guid, callback, context, flags), EnumGroups, and
        // EnumGroupPlayers(group, guid, callback, context, flags).
        if (m == "EnumPlayers" || m == "EnumGroups" || m == "EnumGroupPlayers") {
            if (!ep.session())
                return kDpErrNoConnection;
            const std::map<uint32_t, dplay::Member> members = ep.members();
            const bool ofGroup = m == "EnumGroupPlayers";
            const dplay::Member *group = nullptr;
            if (ofGroup) {
                auto it = members.find(c.arg(1));
                if (it == members.end() || !it->second.group)
                    return kDpErrInvalidPlayer;
                group = &it->second;
            }
            std::vector<std::pair<uint32_t, dplay::Member>> ids;
            for (const auto &[id, member] : members)
                if (ofGroup ? group->members.count(id) != 0 : member.group == (m == "EnumGroups"))
                    ids.emplace_back(id, member);
            enumerate(c, ids, c.arg(ofGroup ? 3 : 2), c.arg(ofGroup ? 4 : 3));
            return kDpOk;
        }
        // Send(from, to, flags, data, size) and SendEx(..., priority,
        // timeout, context, *id): DPSEND_GUARANTEED (1) arrives, in order.
        // An asynchronous SendEx (DPSEND_ASYNC, 0x200) says it's pending,
        // and is told complete unless asked not to (0x400).
        if (m == "Send" || m == "SendEx") {
            const uint32_t from = c.arg(1);
            const uint32_t to = c.arg(2);
            const uint32_t flags = c.arg(3);
            const dplay::Endpoint::Status status =
                ep.send(from, to, readBytes(c, c.arg(4), c.arg(5)), (flags & 1) != 0);
            if (status != dplay::Endpoint::Status::kOk)
                return result(status);
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                ++s->summary.sent;
                s->summary.largestSent = std::max(s->summary.largestSent, c.arg(5));
            }
            s->notePlayers();
            if (m == "SendEx" && (flags & 0x200)) {
                const uint32_t id = nextMessage++;
                if (c.arg(9) && c.valid(c.arg(9), 4))
                    c.write32(c.arg(9), id);
                if (!(flags & 0x400)) {
                    // DPSYS_SENDCOMPLETE: from, to, flags, priority,
                    // timeout, context, the message's id, its result, time.
                    dplay::Received done;
                    done.data.resize(40);
                    const uint32_t fields[10] = {0x10d, from, to, flags, c.arg(6), c.arg(7), c.arg(8), id, kDpOk, 0};
                    for (int i = 0; i < 10; ++i)
                        put32(done.data, 4 * size_t(i), fields[i]);
                    done.to = from;
                    ep.post(done);
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
            const uint32_t wantFrom = (flags & 4) && c.valid(fromPtr, 4) ? c.read32(fromPtr) : 0;
            const uint32_t wantTo = (flags & 2) && c.valid(toPtr, 4) ? c.read32(toPtr) : 0;
            const auto match = [&](const dplay::Received &r) {
                return (!(flags & 4) || r.from == wantFrom) && (!(flags & 2) || r.to == wantTo);
            };
            s->notePlayers();
            std::optional<dplay::Received> got = ep.receive(match, true);
            if (!got)
                return kDpErrNoMessages;
            if (!c.valid(sizePtr, 4))
                return kDpErrInvalidParams;
            Laid laid;
            if (got->event)
                laid = layOut(*got->event);
            else
                laid.data = got->data;
            const uint32_t size = uint32_t(laid.data.size());
            const uint32_t room = c.read32(sizePtr);
            c.write32(sizePtr, size);
            if (!data || room < size || !c.valid(data, size))
                return kDpErrBufferTooSmall;
            if (!(flags & 8)) {
                ep.receive(match, false);
                if (!got->event && got->from != 0) {
                    std::lock_guard<std::mutex> lock(s->mutex);
                    ++s->summary.received;
                    s->summary.largestReceived = std::max(s->summary.largestReceived, size);
                }
            }
            for (uint32_t i = 0; i < size; ++i)
                c.write8(data + i, laid.data[i]);
            for (uint32_t field : laid.pointers)
                c.write32(data + field, data + c.read32(data + field));
            if (c.valid(fromPtr, 4))
                c.write32(fromPtr, got->from);
            if (c.valid(toPtr, 4))
                c.write32(toPtr, got->to);
            return kDpOk;
        }
        // GetMessageCount(player, *count).
        if (m == "GetMessageCount") {
            if (!c.valid(c.arg(2), 4))
                return kDpErrInvalidParams;
            const uint32_t player = c.arg(1);
            uint32_t count = 0;
            uint32_t bytes = 0;
            ep.waiting([&](const dplay::Received &r) { return player == 0 || r.to == player; }, count, bytes);
            c.write32(c.arg(2), count);
            return kDpOk;
        }
        // GetMessageQueue(from, to, flags, *count, *bytes): what's waiting
        // to be received (DPMESSAGEQUEUE_RECEIVE, 2); nothing waits to be
        // sent.
        if (m == "GetMessageQueue") {
            uint32_t count = 0;
            uint32_t bytes = 0;
            const uint32_t from = c.arg(1);
            const uint32_t to = c.arg(2);
            if (c.arg(3) & 2)
                ep.waiting([&](const dplay::Received &r) { return (!from || r.from == from) && (!to || r.to == to); },
                           count, bytes);
            if (c.arg(4) && c.valid(c.arg(4), 4))
                c.write32(c.arg(4), count);
            if (c.arg(5) && c.valid(c.arg(5), 4))
                c.write32(c.arg(5), bytes);
            return kDpOk;
        }
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
