// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/dplay.h"

#include "emulation/x86/com.h"
#include "emulation/x86/dplay_net.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::x86 {

namespace {

const char *const kClsidLobby = "{2fe8f810-b2a5-11d0-a787-0000f803abfc}";
const char *const kIidLobby3A = "{2db72491-652c-11d1-a7a8-0000f803abfc}";
const char *const kClsidPlay = "{d1eb6d20-8923-11d0-9d97-00a0c90a43cb}";
const char *const kIidPlay4A = "{0ab1c531-4745-11d1-a7a1-0000f803abfc}";
// The service providers Windows has, as EnumConnections lists them.
struct Provider {
    const char *guid;
    const char *name;
};
const Provider kProviders[] = {
    {"{685bc400-9d2c-11cf-a9cd-00aa006886e3}", "IPX Connection For DirectPlay"},
    {"{36e95ee0-8577-11cf-960c-0080c7534e82}", "Internet TCP/IP Connection For DirectPlay"},
    {"{44eaa760-cb68-11cf-9c4e-00a0c905425e}", "Modem Connection For DirectPlay"},
    {"{0f1d6860-88d9-11cf-9c4e-00a0c905425e}", "Serial Connection For DirectPlay"},
};
// The address chunks that name a provider.
const char *const kAidTotalSize = "{1318f560-912c-11d0-9daa-00a0c90a43cb}";
const char *const kAidServiceProvider = "{07d916c0-e0af-11cf-9c4e-00a0c905425e}";
// A host's name or address, as text: none or empty, the local network.
const char *const kAidINet = "{c4a54da0-e0af-11cf-9c4e-00a0c905425e}";
const char *const kSpTcpIp = "{36e95ee0-8577-11cf-960c-0080c7534e82}";

constexpr uint32_t kClassNotRegistered = 0x80040154u;
// DirectPlay's HRESULTs: facility 0x877, as MAKE_DPHRESULT makes them.
constexpr uint32_t kDpErrNotLobbied = 0x88770000u + 1070;
constexpr uint32_t kDpErrInvalidParams = kEInvalidArg;
constexpr uint32_t kDpErrBufferTooSmall = 0x88770000u + 30;
constexpr uint32_t kDpErrNoMessages = 0x88770000u + 190;
// The largest message Send takes, in bytes.
constexpr uint32_t kMaxMessage = 65536;
constexpr uint32_t kDpErrInvalidPlayer = 0x88770000u + 150;
constexpr uint32_t kDpErrAlreadyInitialized = 0x88770000u + 5;
constexpr uint32_t kDpErrAccessDenied = 0x88770000u + 10;
constexpr uint32_t kDpErrNoConnection = 0x88770000u + 180;
constexpr uint32_t kDpErrNoSessions = 0x88770000u + 220;

// System messages' types, as programs tell them apart.
constexpr uint32_t kSysCreate = 0x0003;
constexpr uint32_t kSysDestroy = 0x0005;
constexpr uint32_t kSysSetData = 0x0102;
constexpr uint32_t kSysSessionLost = 0x0031;
constexpr uint32_t kSysSetSessionDesc = 0x0104;
constexpr uint32_t kSysSetName = 0x0106;
constexpr uint32_t kSysHost = 0x0101;
constexpr uint32_t kEnumAsync = 0x10;
constexpr uint32_t kEnumStopAsync = 0x20;

constexpr uint32_t kOpenCreate = 2;
constexpr uint32_t kSendAsync = 0x200;
constexpr uint32_t kSendNoComplete = 0x400;
constexpr uint32_t kDpErrPending = 0x8000000au;
constexpr uint32_t kReceiveToPlayer = 2;
constexpr uint32_t kReceiveFromPlayer = 4;
constexpr uint32_t kReceivePeek = 8;
// Players' IDs: any number but DirectPlay's own (0, the system; 1, a
// session's server player).
constexpr uint32_t kFirstPlayer = 0x00100001;

// IDirectPlayLobby, then what IDirectPlayLobby2 and 3 add, each with its
// argument words after `this`.
ComInterface makeLobby3A() {
    return {"IDirectPlayLobby3A",
            kIidLobby3A,
            {{"QueryInterface", 2},
             {"AddRef", 0},
             {"Release", 0},
             {"Connect", 3},
             {"CreateAddress", 6},
             {"EnumAddress", 4},
             {"EnumAddressTypes", 4},
             {"EnumLocalApplications", 3},
             {"GetConnectionSettings", 3},
             {"ReceiveLobbyMessage", 5},
             {"RunApplication", 4},
             {"SendLobbyMessage", 4},
             {"SetConnectionSettings", 3},
             {"SetLobbyMessageEvent", 3},
             {"CreateCompoundAddress", 4},
             {"ConnectEx", 4},
             {"RegisterApplication", 2},
             {"UnregisterApplication", 2},
             {"WaitForConnectionSettings", 1}}};
}

// IDirectPlay2's methods, then what IDirectPlay3 and 4 add.
ComInterface makePlay4A() {
    return {"IDirectPlay4A",
            kIidPlay4A,
            {{"QueryInterface", 2},
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
             {"GetGroupOwner", 2},
             {"SetGroupOwner", 2},
             {"SendEx", 9},
             {"GetMessageQueue", 5},
             {"CancelMessage", 2},
             {"CancelPriority", 3}}};
}

} // namespace

struct DirectPlay::State {
    explicit State(Win32Process &p) : process(p), com(p) {}
    Win32Process &process;
    ComHost com;
    ComInterface lobby3A = makeLobby3A();
    ComInterface play4A = makePlay4A();
    uint32_t guids = 0;
    std::shared_ptr<net::Sockets> sockets;
    std::vector<uint32_t> broadcast;
    uint16_t port = dplay::kPort;
    /** A GUID for a new session: different each time, but the same each run. */
    std::string newGuid() {
        char text[40];
        std::snprintf(text, sizeof(text), "{%08x-5245-4c55-8641-%012x}", ++guids,
                      process.milliseconds ? process.milliseconds() : 0u);
        return text;
    }
};

namespace {

// The lobby: the program learns it wasn't started by one.
class Lobby : public ComObject {
public:
    DirectPlay::State *s = nullptr;
    const ComInterface *query(const std::string &iid) override { return iid == s->lobby3A.iid ? &s->lobby3A : nullptr; }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // GetConnectionSettings(appId, data, size): no lobby started it,
        // so there are none.
        if (m == "GetConnectionSettings")
            return kDpErrNotLobbied;
        // CreateCompoundAddress(elements (type, size, data*), count, address,
        // size*): a total-size chunk, then each element's.
        if (m == "CreateCompoundAddress") {
            const uint32_t elements = c.arg(1);
            const uint32_t count = c.arg(2);
            const uint32_t address = c.arg(3);
            const uint32_t sizeAt = c.arg(4);
            if (!c.valid(sizeAt, 4) || count > 64 || (count && !c.valid(elements, count * 24)))
                return kDpErrInvalidParams;
            uint32_t total = 24;
            for (uint32_t i = 0; i < count; ++i)
                total += 20 + c.read32(elements + i * 24 + 16);
            const uint32_t room = c.read32(sizeAt);
            c.write32(sizeAt, total);
            if (!address || room < total)
                return kDpErrBufferTooSmall;
            if (!c.valid(address, total))
                return kDpErrInvalidParams;
            ComHost::writeGuid(c, address, kAidTotalSize);
            c.write32(address + 16, 4);
            c.write32(address + 20, total);
            uint32_t at = address + 24;
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t e = elements + i * 24;
                const uint32_t length = c.read32(e + 16);
                const uint32_t data = c.read32(e + 20);
                ComHost::writeGuid(c, at, ComHost::guid(c, e));
                c.write32(at + 16, length);
                for (uint32_t b = 0; b < length; ++b)
                    c.write8(at + 20 + b, c.read8(data + b));
                at += 20 + length;
            }
            return kSOk;
        }
        // EnumAddress(callback(chunk's type, its size, its data, context),
        // address, its size, context): each chunk in turn.
        if (m == "EnumAddress") {
            const uint32_t callback = c.arg(1);
            const uint32_t address = c.arg(2);
            const uint32_t size = c.arg(3);
            const uint32_t context = c.arg(4);
            if (!address || !c.valid(address, size))
                return kDpErrInvalidParams;
            const uint32_t type = s->process.memory().alloc(16);
            for (uint32_t at = 0; at + 20 <= size;) {
                const uint32_t length = c.read32(address + at + 16);
                if (length > size - at - 20)
                    break;
                // The type copied out, as the callback may keep it.
                ComHost::writeGuid(c, type, ComHost::guid(c, address + at));
                uint32_t keepGoing = 0;
                if (c.call(callback, {type, length, address + at + 20, context}, keepGoing) || !keepGoing)
                    break;
                at += 20 + length;
            }
            s->process.memory().free(type);
            return kSOk;
        }
        return std::nullopt;
    }
};

// The connection EnumConnections gives: a DirectPlay address (chunks of a
// GUID, the data's size and the data) naming the TCP/IP provider.
constexpr uint32_t kConnectionSize = (20 + 4) + (20 + 16); // each chunk: type, size, data
void writeConnection(Cpu &c, uint32_t at, const char *provider) {
    ComHost::writeGuid(c, at, kAidTotalSize);
    c.write32(at + 16, 4);
    c.write32(at + 20, kConnectionSize);
    ComHost::writeGuid(c, at + 24, kAidServiceProvider);
    c.write32(at + 40, 16);
    ComHost::writeGuid(c, at + 44, provider);
}

// An address's chunk: its type and data.
struct Chunk {
    std::string type;
    std::vector<uint8_t> data;
};
// The chunks of an address `size` bytes long; a chunk running past it ends it.
std::vector<Chunk> readAddress(Cpu &c, uint32_t at, uint32_t size) {
    std::vector<Chunk> out;
    if (!c.valid(at, size))
        return out;
    for (uint32_t i = 0; i + 20 <= size;) {
        const uint32_t length = c.read32(at + i + 16);
        if (length > size - i - 20)
            break;
        Chunk k{ComHost::guid(c, at + i), {}};
        for (uint32_t b = 0; b < length; ++b)
            k.data.push_back(c.read8(at + i + 20 + b));
        out.push_back(std::move(k));
        i += 20 + length;
    }
    return out;
}
// A GUID's 16 bytes as ComHost::guid writes it.
std::string guidText(const uint8_t *g) {
    char text[40];
    std::snprintf(text, sizeof(text), "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                  unsigned(g[0] | g[1] << 8 | g[2] << 16 | uint32_t(g[3]) << 24), unsigned(g[4] | g[5] << 8),
                  unsigned(g[6] | g[7] << 8), g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return text;
}

// A player in the session.
struct Player {
    uint32_t id = 0;
    bool local = false;
    std::string shortName;
    std::string longName;
    std::vector<uint8_t> localData; // its own, which only its program sees
    std::vector<uint8_t> remoteData;
    uint32_t flags = 0; // CreatePlayer's
    uint32_t event = 0; // set as a message comes for it
};

// A message waiting to be received: the bytes as the program reads them,
// and where in them are pointers (offsets into the same bytes, made
// addresses as they're copied out).
struct Message {
    uint32_t from = 0; // 0: DirectPlay's own (a system message)
    uint32_t to = 0;
    std::vector<uint8_t> bytes;
    std::vector<uint32_t> pointers;
};

void put32(std::vector<uint8_t> &b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}
void set32(std::vector<uint8_t> &b, size_t at, uint32_t v) {
    for (size_t i = 0; i < 4; ++i)
        b[at + i] = uint8_t(v >> (8 * i));
}
// A string, its terminator too, appended; its offset, or 0 when it's empty
// and `none` (a null pointer).
uint32_t putString(std::vector<uint8_t> &b, const std::string &s, bool none) {
    if (s.empty() && none)
        return 0;
    const uint32_t at = uint32_t(b.size());
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
    return at;
}

// The session a DirectPlay object has opened.
struct Session {
    uint32_t flags = 0;
    std::string instance; // its GUID, made as it's created
    std::string application;
    uint32_t maxPlayers = 0;
    std::string name;
    std::string password;
    uint32_t user[4] = {};
    bool host = false;
    std::map<uint32_t, Player> players;
};

// A DirectPlay object: its connection, the session it's in, and its
// players' messages.
dplay::SessionInfo sessionInfo(const Session &s) {
    dplay::SessionInfo i;
    i.instance = s.instance;
    i.application = s.application;
    i.name = s.name;
    i.password = s.password;
    i.flags = s.flags;
    i.maxPlayers = s.maxPlayers;
    i.currentPlayers = uint32_t(s.players.size());
    std::copy(std::begin(s.user), std::end(s.user), i.user);
    return i;
}
void fromSessionInfo(const dplay::SessionInfo &i, Session &s) {
    s.instance = i.instance;
    s.application = i.application;
    s.name = i.name;
    s.flags = i.flags;
    s.maxPlayers = i.maxPlayers;
    std::copy(std::begin(i.user), std::end(i.user), s.user);
}
dplay::PlayerInfo playerInfo(const Player &p) {
    return {p.id, p.flags, p.shortName, p.longName, p.remoteData};
}

class Play : public ComObject {
public:
    DirectPlay::State *s = nullptr;
    std::string provider;              // InitializeConnection's, "" before it
    std::vector<Chunk> connection;     // its address
    std::string hostName;              // DPAID_INet's: "" the local network
    std::unique_ptr<dplay::Node> node; // TCP/IP's network, given one
    std::unique_ptr<Session> session;
    std::deque<Message> queue;
    uint32_t nextId = 0;

    const ComInterface *query(const std::string &iid) override { return iid == s->play4A.iid ? &s->play4A : nullptr; }

    // A system message to each local player but `except`.
    void systemMessage(const std::vector<uint8_t> &bytes, const std::vector<uint32_t> &pointers, uint32_t except) {
        for (const auto &[id, p] : session->players)
            if (p.local && id != except)
                deliver({0, id, bytes, pointers});
    }
    void deliver(Message m) {
        if (const Player *p = player(m.to); p && p->event)
            s->process.setEvent(p->event);
        queue.push_back(std::move(m));
    }
    // DPMSG_CREATEPLAYERORGROUP or DESTROY's bytes for player `p`, with its
    // name (DPNAME's strings) and data after the structure.
    static Message playerMessage(uint32_t type, const Player &p, uint32_t players) {
        Message m;
        std::vector<uint8_t> &b = m.bytes;
        put32(b, type);
        put32(b, 1); // DPPLAYERTYPE_PLAYER
        put32(b, p.id);
        if (type == kSysCreate) {
            put32(b, players);
            put32(b, 0); // lpData, below
            put32(b, uint32_t(p.remoteData.size()));
        } else {
            put32(b, 0); // its local data, which a remote player hasn't
            put32(b, 0);
            put32(b, 0); // its remote data, below
            put32(b, uint32_t(p.remoteData.size()));
        }
        const size_t name = b.size();
        put32(b, 16);
        put32(b, 0);
        put32(b, 0);
        put32(b, 0);
        put32(b, 0);                                        // dpIdParent
        put32(b, 0);                                        // dwFlags
        const size_t dataAt = type == kSysCreate ? 16 : 20; // lpData, or lpRemoteData
        if (!p.remoteData.empty()) {
            set32(b, dataAt, uint32_t(b.size()));
            m.pointers.push_back(uint32_t(dataAt));
            b.insert(b.end(), p.remoteData.begin(), p.remoteData.end());
        }
        for (const auto &[field, text] : {std::pair{name + 8, &p.shortName}, std::pair{name + 12, &p.longName}})
            if (const uint32_t at = putString(b, *text, true)) {
                set32(b, field, at);
                m.pointers.push_back(uint32_t(field));
            }
        return m;
    }

    // DPSESSIONDESC2 at `to`, its strings after it: false if they don't fit
    // in `room` (`needed` the bytes they take).
    bool writeSession(Cpu &c, uint32_t to, uint32_t room, uint32_t &needed) const {
        needed = 80 + uint32_t(session->name.size() + 1) + uint32_t(session->password.size() + 1);
        if (!to || room < needed || !c.valid(to, needed))
            return false;
        c.write32(to, 80);
        c.write32(to + 4, session->flags);
        ComHost::writeGuid(c, to + 8, session->instance);
        ComHost::writeGuid(c, to + 24, session->application);
        c.write32(to + 40, session->maxPlayers);
        c.write32(to + 44, uint32_t(session->players.size()));
        c.write32(to + 48, to + 80);
        c.writeString(to + 80, session->name, uint32_t(session->name.size() + 1));
        const uint32_t password = to + 80 + uint32_t(session->name.size() + 1);
        c.write32(to + 52, password);
        c.writeString(password, session->password, uint32_t(session->password.size() + 1));
        c.write32(to + 56, 0);
        c.write32(to + 60, 0);
        for (uint32_t i = 0; i < 4; ++i)
            c.write32(to + 64 + 4 * i, session->user[i]);
        return true;
    }
    // A DPSESSIONDESC2's values into the session.
    static void readSession(Cpu &c, uint32_t from, Session *session) {
        session->flags = c.read32(from + 4);
        session->application = ComHost::guid(c, from + 24);
        session->maxPlayers = c.read32(from + 40);
        session->name = c.read32(from + 48) ? c.string(c.read32(from + 48), 256) : std::string();
        session->password = c.read32(from + 52) ? c.string(c.read32(from + 52), 256) : std::string();
        for (uint32_t i = 0; i < 4; ++i)
            session->user[i] = c.read32(from + 64 + 4 * i);
    }
    Player *player(uint32_t id) const {
        if (!session)
            return nullptr;
        const auto it = session->players.find(id);
        return it == session->players.end() ? nullptr : &it->second;
    }

    uint32_t send(Cpu &c, uint32_t from, uint32_t to, uint32_t data, uint32_t size) {
        const Player *sender = player(from);
        if (!sender || !sender->local)
            return kDpErrInvalidPlayer;
        if (to != 0 && !player(to))
            return kDpErrInvalidPlayer;
        if (size > kMaxMessage || !c.valid(data, size))
            return kDpErrInvalidParams;
        Message msg{from, 0, std::vector<uint8_t>(size), {}};
        for (uint32_t i = 0; i < size; ++i)
            msg.bytes[i] = c.read8(data + i);
        for (const auto &[id, p] : session->players)
            if (p.local && (to == 0 ? id != from : id == to)) {
                msg.to = id;
                deliver(msg);
            }
        if (node && (to == 0 || !player(to)->local))
            node->send(from, to, msg.bytes);
        return kSOk;
    }

    // DPSESSIONDESC2 for a message, appended to `b`, its name after it.
    void putSession(std::vector<uint8_t> &b, std::vector<uint32_t> &pointers) const {
        const size_t start = b.size();
        b.resize(start + 80);
        set32(b, start, 80);
        set32(b, start + 4, session->flags);
        for (const auto &[offset, g] : {std::pair{8u, &session->instance}, std::pair{24u, &session->application}})
            for (size_t i = 0; i < 16 && g->size() == 38; ++i) {
                static const int kAt[16] = {7, 5, 3, 1, 12, 10, 17, 15, 20, 22, 25, 27, 29, 31, 33, 35};
                b[start + offset + i] = uint8_t(std::stoi(g->substr(size_t(kAt[i]), 2), nullptr, 16));
            }
        set32(b, start + 40, session->maxPlayers);
        set32(b, start + 44, uint32_t(session->players.size()));
        for (uint32_t i = 0; i < 4; ++i)
            set32(b, start + 64 + 4 * i, session->user[i]);
        if (const uint32_t name = putString(b, session->name, true)) {
            set32(b, start + 48, name);
            pointers.push_back(uint32_t(start + 48));
        }
    }

    // What the session's other programs did, made the messages DirectPlay
    // gives this one's players.
    void poll() {
        if (!node || !session)
            return;
        for (dplay::Event &e : node->poll()) {
            switch (e.kind) {
            case dplay::Event::kPlayerAdded: {
                if (player(e.player.id))
                    break;
                Player p;
                p.id = e.player.id;
                p.flags = e.player.flags;
                p.shortName = e.player.shortName;
                p.longName = e.player.longName;
                p.remoteData = e.player.data;
                session->players[p.id] = p;
                const Message made = playerMessage(kSysCreate, p, uint32_t(session->players.size()));
                systemMessage(made.bytes, made.pointers, 0);
                break;
            }
            case dplay::Event::kPlayerRemoved: {
                const Player *p = player(e.player.id);
                if (!p || p->local)
                    break;
                const Message gone = playerMessage(kSysDestroy, *p, 0);
                session->players.erase(e.player.id);
                systemMessage(gone.bytes, gone.pointers, 0);
                break;
            }
            case dplay::Event::kPlayerData: {
                Player *p = player(e.player.id);
                if (!p || p->local)
                    break;
                p->remoteData = e.player.data;
                const Message set = dataMessage(*p);
                systemMessage(set.bytes, set.pointers, 0);
                break;
            }
            case dplay::Event::kPlayerName: {
                Player *p = player(e.player.id);
                if (!p || p->local)
                    break;
                p->shortName = e.player.shortName;
                p->longName = e.player.longName;
                const Message set = nameMessage(*p);
                systemMessage(set.bytes, set.pointers, 0);
                break;
            }
            case dplay::Event::kMessage:
                for (const auto &[id, p] : session->players)
                    if (p.local && (e.to == 0 || id == e.to))
                        deliver({e.from, id, e.bytes, {}});
                break;
            case dplay::Event::kSession: {
                fromSessionInfo(e.session, *session);
                Message m;
                put32(m.bytes, kSysSetSessionDesc);
                putSession(m.bytes, m.pointers);
                systemMessage(m.bytes, m.pointers, 0);
                break;
            }
            case dplay::Event::kBecameHost: {
                // The host gone, this program hosts the session now
                // (DPSESSION_MIGRATEHOST): DPMSG_HOST, the type alone.
                session->host = true;
                Message m;
                put32(m.bytes, kSysHost);
                systemMessage(m.bytes, m.pointers, 0);
                break;
            }
            case dplay::Event::kSessionLost: {
                Message m;
                put32(m.bytes, kSysSessionLost);
                systemMessage(m.bytes, m.pointers, 0);
                break;
            }
            }
        }
    }
    // DPMSG_SETPLAYERORGROUPDATA for `p`'s data every player sees.
    // DPMSG_SETPLAYERORGROUPNAME for player `p`: its DPNAME, the strings
    // after the structure.
    static Message nameMessage(const Player &p) {
        Message m;
        std::vector<uint8_t> &b = m.bytes;
        put32(b, kSysSetName);
        put32(b, 1); // DPPLAYERTYPE_PLAYER
        put32(b, p.id);
        put32(b, 16);
        put32(b, 0);
        put32(b, 0);
        put32(b, 0);
        for (const auto &[field, text] : {std::pair{size_t(20), &p.shortName}, std::pair{size_t(24), &p.longName}})
            if (const uint32_t at = putString(b, *text, true)) {
                set32(b, field, at);
                m.pointers.push_back(uint32_t(field));
            }
        return m;
    }
    static Message dataMessage(const Player &p) {
        Message set;
        put32(set.bytes, kSysSetData);
        put32(set.bytes, 1);
        put32(set.bytes, p.id);
        put32(set.bytes, p.remoteData.empty() ? 0 : 20);
        put32(set.bytes, uint32_t(p.remoteData.size()));
        if (!p.remoteData.empty())
            set.pointers.push_back(12);
        set.bytes.insert(set.bytes.end(), p.remoteData.begin(), p.remoteData.end());
        return set;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override;
};

std::optional<uint32_t> Play::invoke(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    poll();
    // EnumConnections(application, callback(provider, connection, its size,
    // DPNAME*, flags, context), context, flags): the providers Windows has,
    // as programs expect them all.
    if (m == "EnumConnections") {
        const uint32_t callback = c.arg(2);
        const uint32_t context = c.arg(3);
        const uint32_t block = s->process.memory().alloc(16 + kConnectionSize + 16 + 64);
        const uint32_t address = block + 16;
        const uint32_t dpName = address + kConnectionSize;
        Error e;
        for (const Provider &p : kProviders) {
            ComHost::writeGuid(c, block, p.guid);
            writeConnection(c, address, p.guid);
            c.write32(dpName, 16);
            c.write32(dpName + 4, 0);
            c.write32(dpName + 8, dpName + 16);
            c.write32(dpName + 12, dpName + 16);
            c.writeString(dpName + 16, p.name, 64);
            uint32_t keepGoing = 0;
            e = c.call(callback, {block, address, kConnectionSize, dpName, 0, context}, keepGoing);
            if (e || !keepGoing)
                break;
        }
        s->process.memory().free(block);
        return e ? kEFail : kSOk;
    }
    // InitializeConnection(connection, flags): an address naming a provider;
    // the program may add to it (a host to reach).
    if (m == "InitializeConnection") {
        const uint32_t at = c.arg(1);
        std::string sp;
        if (!at || !c.valid(at, 24) || ComHost::guid(c, at) != kAidTotalSize)
            return kDpErrInvalidParams;
        connection = readAddress(c, at, c.read32(at + 20));
        for (const Chunk &k : connection)
            if (k.type == kAidServiceProvider && k.data.size() == 16)
                sp = guidText(k.data.data());
        if (sp.empty())
            return kDpErrInvalidParams;
        provider = sp;
        hostName.clear();
        for (const Chunk &k : connection)
            if (k.type == kAidINet)
                hostName = std::string(k.data.begin(), std::find(k.data.begin(), k.data.end(), uint8_t(0)));
        node.reset();
        if (provider == kSpTcpIp && s->sockets)
            node = std::make_unique<dplay::Node>(s->sockets, s->broadcast, s->port);
        return kSOk;
    }
    // GetPlayerAddress(player, data, size*): the connection's address, the
    // provider's alone (no modems, no host); programs ask with no buffer
    // first.
    if (m == "GetPlayerAddress") {
        const uint32_t data = c.arg(2);
        const uint32_t sizeAt = c.arg(3);
        if (provider.empty())
            return kEFail; // DPERR_GENERIC: no connection yet
        if (!c.valid(sizeAt, 4))
            return kDpErrInvalidParams;
        const uint32_t room = c.read32(sizeAt);
        c.write32(sizeAt, kConnectionSize);
        if (!data || room < kConnectionSize)
            return kDpErrBufferTooSmall;
        writeConnection(c, data, provider.c_str());
        return kSOk;
    }
    // GetCaps(DPCAPS*, flags): Reluva's own numbers (its messages go whole,
    // in order).
    if (m == "GetCaps") {
        const uint32_t caps = c.arg(1);
        if (!c.valid(caps, 40) || c.read32(caps) < 40)
            return kDpErrInvalidParams;
        // DPCAPS_ISHOST (2) in its flags while this program hosts.
        const uint32_t values[] = {40, session && session->host ? 2u : 0u, kMaxMessage, 0, 64, 0, 50, 64, 0, 5000};
        for (uint32_t i = 0; i < 10; ++i)
            c.write32(caps + 4 * i, values[i]);
        return kSOk;
    }
    // EnumSessions(DPSESSIONDESC2*, timeout, callback(DPSESSIONDESC2*,
    // timeout*, flags, context), context, flags): the sessions of the
    // application asked for that copies of Reluva host, then the callback
    // told the time is up (DPESC_TIMEDOUT, 1), once, as a callback asking to
    // go on would wait for good. Asynchronously (DPENUMSESSIONS_ASYNC,
    // 0x10) those heard of so far; else it waits the time given (at most
    // five seconds) for answers.
    if (m == "EnumSessions") {
        const uint32_t desc = c.arg(1);
        const uint32_t flags = c.arg(5);
        if (flags & kEnumStopAsync)
            return kSOk;
        std::vector<dplay::FoundSession> found;
        if (node && !node->open()) {
            const std::string application = c.valid(desc, 80) ? ComHost::guid(c, desc + 24) : std::string();
            const int waitMs = (flags & kEnumAsync) ? 0 : int(std::min<uint32_t>(c.arg(2) ? c.arg(2) : 1000, 5000));
            node->find(hostName, application, waitMs);
            found = node->found();
        }
        const uint32_t timeout = s->process.memory().alloc(4);
        c.write32(timeout, c.arg(2));
        Error e;
        uint32_t again = 0;
        for (const dplay::FoundSession &f : found) {
            const dplay::SessionInfo &i = f.info;
            const uint32_t sd = s->process.memory().alloc(80 + uint32_t(i.name.size()) + 1);
            for (uint32_t at = 0; at < 80; at += 4)
                c.write32(sd + at, 0);
            c.write32(sd, 80);
            c.write32(sd + 4, i.flags);
            ComHost::writeGuid(c, sd + 8, i.instance);
            ComHost::writeGuid(c, sd + 24, i.application);
            c.write32(sd + 40, i.maxPlayers);
            c.write32(sd + 44, i.currentPlayers);
            c.write32(sd + 48, sd + 80);
            c.writeString(sd + 80, i.name, uint32_t(i.name.size()) + 1);
            for (uint32_t u = 0; u < 4; ++u)
                c.write32(sd + 64 + 4 * u, i.user[u]);
            e = c.call(c.arg(3), {sd, timeout, 0, c.arg(4)}, again);
            s->process.memory().free(sd);
            if (e || !again)
                break;
        }
        if (!e && (found.empty() || again))
            e = c.call(c.arg(3), {0, timeout, 1, c.arg(4)}, again);
        s->process.memory().free(timeout);
        return e ? kEFail : kSOk;
    }
    // Open(DPSESSIONDESC2*, flags): DPOPEN_CREATE (2) makes a session this
    // object hosts, which copies of Reluva can join over TCP/IP; DPOPEN_JOIN
    // (1) joins one EnumSessions found, by its instance's GUID, waiting up
    // to five seconds to be let in.
    if (m == "Open") {
        const uint32_t desc = c.arg(1);
        const uint32_t flags = c.arg(2);
        if (session)
            return kDpErrAlreadyInitialized;
        if (provider.empty())
            return kEFail;
        if (!c.valid(desc, 80))
            return kDpErrInvalidParams;
        if (!(flags & kOpenCreate)) {
            if (!node)
                return kDpErrNoSessions;
            const std::string password = c.read32(desc + 52) ? c.string(c.read32(desc + 52), 256) : std::string();
            dplay::SessionInfo info;
            std::vector<dplay::PlayerInfo> players;
            uint32_t firstId = 0;
            const uint32_t r = node->join(ComHost::guid(c, desc + 8), password, 5000, info, players, firstId);
            if (r != 0)
                return r;
            session = std::make_unique<Session>();
            fromSessionInfo(info, *session);
            session->password = password;
            for (const dplay::PlayerInfo &pi : players) {
                Player p;
                p.id = pi.id;
                p.flags = pi.flags;
                p.shortName = pi.shortName;
                p.longName = pi.longName;
                p.remoteData = pi.data;
                session->players[p.id] = p;
            }
            nextId = firstId;
            return kSOk;
        }
        session = std::make_unique<Session>();
        session->host = true;
        readSession(c, desc, session.get());
        session->instance = s->newGuid();
        nextId = kFirstPlayer;
        // Hosted on the network; a port another program has taken (a
        // session hosted on this machine already) can't be.
        if (node && !node->host(sessionInfo(*session))) {
            session.reset();
            return kEFail; // DPERR_GENERIC
        }
        return kSOk;
    }
    if (m == "Close") {
        session.reset();
        queue.clear();
        if (node)
            node = std::make_unique<dplay::Node>(s->sockets, s->broadcast, s->port);
        return kSOk;
    }
    // CreatePlayer(id*, DPNAME*, event, data, size, flags): a local player,
    // whose creation the session's other players are told of.
    if (m == "CreatePlayer") {
        const uint32_t idAt = c.arg(1);
        const uint32_t name = c.arg(2);
        if (!session)
            return kDpErrInvalidParams;
        if (!c.valid(idAt, 4))
            return kDpErrInvalidParams;
        Player p;
        p.id = nextId++;
        p.local = true;
        p.flags = c.arg(6);
        p.event = c.arg(3);
        if (name && c.valid(name, 16)) {
            p.shortName = c.read32(name + 8) ? c.string(c.read32(name + 8), 256) : std::string();
            p.longName = c.read32(name + 12) ? c.string(c.read32(name + 12), 256) : std::string();
        }
        if (c.arg(4) && c.valid(c.arg(4), c.arg(5)))
            for (uint32_t i = 0; i < c.arg(5); ++i)
                p.remoteData.push_back(c.read8(c.arg(4) + i));
        session->players[p.id] = p;
        c.write32(idAt, p.id);
        if (node)
            node->addPlayer(playerInfo(p));
        const Message made = playerMessage(kSysCreate, p, uint32_t(session->players.size()));
        systemMessage(made.bytes, made.pointers, p.id);
        return kSOk;
    }
    // DestroyPlayer(id): a local player, the others told.
    if (m == "DestroyPlayer") {
        Player *p = player(c.arg(1));
        if (!p || !p->local)
            return kDpErrInvalidPlayer;
        const Message gone = playerMessage(kSysDestroy, *p, 0);
        session->players.erase(c.arg(1));
        std::erase_if(queue, [&](const Message &q) { return q.to == c.arg(1); });
        if (node)
            node->removePlayer(c.arg(1));
        systemMessage(gone.bytes, gone.pointers, 0);
        return kSOk;
    }
    // GetSessionDesc(data, size*).
    if (m == "GetSessionDesc") {
        const uint32_t sizeAt = c.arg(2);
        if (!session)
            return kDpErrNoConnection;
        if (!c.valid(sizeAt, 4))
            return kDpErrInvalidParams;
        uint32_t needed = 0;
        const bool written = writeSession(c, c.arg(1), c.read32(sizeAt), needed);
        c.write32(sizeAt, needed);
        return written ? kSOk : kDpErrBufferTooSmall;
    }
    // SetSessionDesc(DPSESSIONDESC2*, flags): the host's alone.
    if (m == "SetSessionDesc") {
        if (!session)
            return kDpErrNoConnection;
        if (!session->host)
            return kDpErrAccessDenied;
        if (!c.valid(c.arg(1), 80))
            return kDpErrInvalidParams;
        readSession(c, c.arg(1), session.get());
        if (node)
            node->setSession(sessionInfo(*session));
        return kSOk;
    }
    // Send(from, to, flags, data, size): to a player, or every one but the
    // sender (DPID_ALLPLAYERS, 0).
    if (m == "Send")
        return send(c, c.arg(1), c.arg(2), c.arg(4), c.arg(5));
    // SendEx(from, to, flags, data, size, priority, timeout, context, id*):
    // as Send. Sent asynchronously (DPSEND_ASYNC, 0x200), it's under way
    // (DPERR_PENDING, which is E_PENDING); the message saying it's been
    // sent isn't made, so a program must not ask for it (DPSEND_NOSENDCOMPLETE,
    // 0x400).
    if (m == "SendEx") {
        const uint32_t flags = c.arg(3);
        if ((flags & kSendAsync) && !(flags & kSendNoComplete))
            return std::nullopt;
        if (c.arg(9) && c.valid(c.arg(9), 4))
            c.write32(c.arg(9), 0);
        const uint32_t r = send(c, c.arg(1), c.arg(2), c.arg(4), c.arg(5));
        return r == kSOk && (flags & kSendAsync) ? kDpErrPending : r;
    }
    // Receive(from*, to*, flags, data, size*): the first message waiting,
    // or the first to or from a player (DPRECEIVE_TOPLAYER 2, FROMPLAYER 4);
    // DPRECEIVE_PEEK (8) leaves it waiting.
    if (m == "Receive") {
        const uint32_t fromAt = c.arg(1);
        const uint32_t toAt = c.arg(2);
        const uint32_t flags = c.arg(3);
        const uint32_t data = c.arg(4);
        const uint32_t sizeAt = c.arg(5);
        if (!c.valid(fromAt, 4) || !c.valid(toAt, 4) || !c.valid(sizeAt, 4))
            return kDpErrInvalidParams;
        const uint32_t wantFrom = c.read32(fromAt);
        const uint32_t wantTo = c.read32(toAt);
        auto it = std::find_if(queue.begin(), queue.end(), [&](const Message &q) {
            return (!(flags & kReceiveToPlayer) || q.to == wantTo) &&
                   (!(flags & kReceiveFromPlayer) || q.from == wantFrom);
        });
        if (it == queue.end())
            return kDpErrNoMessages;
        const uint32_t room = c.read32(sizeAt);
        c.write32(sizeAt, uint32_t(it->bytes.size()));
        if (!data || room < it->bytes.size())
            return kDpErrBufferTooSmall;
        if (!c.valid(data, uint32_t(it->bytes.size())))
            return kDpErrInvalidParams;
        std::vector<uint8_t> bytes = it->bytes;
        for (uint32_t at : it->pointers) {
            const uint32_t offset =
                uint32_t(bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16) | uint32_t(bytes[at + 3]) << 24;
            set32(bytes, at, data + offset);
        }
        for (size_t i = 0; i < bytes.size(); ++i)
            c.write8(data + uint32_t(i), bytes[i]);
        c.write32(fromAt, it->from);
        c.write32(toAt, it->to);
        if (!(flags & kReceivePeek))
            queue.erase(it);
        return kSOk;
    }
    // GetMessageCount(player, count*).
    if (m == "GetMessageCount") {
        if (!c.valid(c.arg(2), 4))
            return kDpErrInvalidParams;
        c.write32(c.arg(2), uint32_t(std::count_if(queue.begin(), queue.end(),
                                                   [&](const Message &q) { return q.to == c.arg(1); })));
        return kSOk;
    }
    // GetMessageQueue(from, to, flags, count*, bytes*): what waits to be
    // sent (flag 1), none as Reluva sends at once, or to be read (2).
    if (m == "GetMessageQueue") {
        uint32_t count = 0;
        uint32_t bytes = 0;
        if (c.arg(3) & 2)
            for (const Message &q : queue)
                if ((!c.arg(1) || q.from == c.arg(1)) && (!c.arg(2) || q.to == c.arg(2))) {
                    ++count;
                    bytes += uint32_t(q.bytes.size());
                }
        if (c.arg(4) && c.valid(c.arg(4), 4))
            c.write32(c.arg(4), count);
        if (c.arg(5) && c.valid(c.arg(5), 4))
            c.write32(c.arg(5), bytes);
        return kSOk;
    }
    // CancelMessage(message, flags): none waits to be sent.
    if (m == "CancelMessage")
        return kSOk;
    // GetPlayerFlags(player, flags*): CreatePlayer's flags (a spectator,
    // 0x200; the server player, 0x100), and DPPLAYER_LOCAL (8) for this
    // object's own.
    if (m == "GetPlayerFlags") {
        const Player *p = player(c.arg(1));
        if (!p)
            return kDpErrInvalidPlayer;
        if (!c.valid(c.arg(2), 4))
            return kDpErrInvalidParams;
        c.write32(c.arg(2), (p->flags & 0x300) | (p->local ? 8u : 0u));
        return kSOk;
    }
    // EnumPlayers(session, callback(id, DPPLAYERTYPE_PLAYER (1), DPNAME*,
    // flags, context), context, flags): every player in the session.
    if (m == "EnumPlayers") {
        if (!session)
            return kDpErrNoConnection;
        std::vector<uint32_t> ids;
        for (const auto &[id, p] : session->players)
            ids.push_back(id);
        const uint32_t name = s->process.memory().alloc(16 + 2 * 256);
        Error e;
        for (uint32_t id : ids) {
            const Player *p = player(id);
            if (!p)
                continue; // the callback destroyed it
            c.write32(name, 16);
            c.write32(name + 4, 0);
            c.write32(name + 8, name + 16);
            c.write32(name + 12, name + 16 + 256);
            c.writeString(name + 16, p->shortName, 256);
            c.writeString(name + 16 + 256, p->longName, 256);
            uint32_t keepGoing = 0;
            e = c.call(c.arg(2), {id, 1, name, 0, c.arg(3)}, keepGoing);
            if (e || !keepGoing)
                break;
        }
        s->process.memory().free(name);
        return e ? kEFail : kSOk;
    }
    // GetPlayerName(player, data, size*): DPNAME, its strings after it.
    if (m == "GetPlayerName") {
        const Player *p = player(c.arg(1));
        const uint32_t data = c.arg(2);
        const uint32_t sizeAt = c.arg(3);
        if (!p)
            return kDpErrInvalidPlayer;
        if (!c.valid(sizeAt, 4))
            return kDpErrInvalidParams;
        const uint32_t needed = 16 + uint32_t(p->shortName.size() + 1 + p->longName.size() + 1);
        const uint32_t room = c.read32(sizeAt);
        c.write32(sizeAt, needed);
        if (!data || room < needed || !c.valid(data, needed))
            return kDpErrBufferTooSmall;
        const uint32_t shortAt = data + 16;
        const uint32_t longAt = shortAt + uint32_t(p->shortName.size() + 1);
        c.write32(data, 16);
        c.write32(data + 4, 0);
        c.write32(data + 8, shortAt);
        c.write32(data + 12, longAt);
        c.writeString(shortAt, p->shortName, uint32_t(p->shortName.size() + 1));
        c.writeString(longAt, p->longName, uint32_t(p->longName.size() + 1));
        return kSOk;
    }
    // GetPlayerData(player, data, size*, flags): its local data
    // (DPGET_LOCAL, 1), else what every player sees.
    if (m == "GetPlayerData") {
        const Player *p = player(c.arg(1));
        const uint32_t data = c.arg(2);
        const uint32_t sizeAt = c.arg(3);
        if (!p)
            return kDpErrInvalidPlayer;
        if (!c.valid(sizeAt, 4))
            return kDpErrInvalidParams;
        const std::vector<uint8_t> &d = (c.arg(4) & 1) ? p->localData : p->remoteData;
        const uint32_t room = c.read32(sizeAt);
        c.write32(sizeAt, uint32_t(d.size()));
        if (d.empty())
            return kSOk;
        if (!data || room < d.size() || !c.valid(data, uint32_t(d.size())))
            return kDpErrBufferTooSmall;
        for (size_t i = 0; i < d.size(); ++i)
            c.write8(data + uint32_t(i), d[i]);
        return kSOk;
    }
    // SetPlayerData(player, data, size, flags): its local data (DPSET_LOCAL,
    // 1), else what every player sees, the session's others told
    // (DPSYS_SETPLAYERORGROUPDATA).
    // SetPlayerName(player, DPNAME*, flags): one of this program's players
    // renamed, for its other players and every other program's.
    if (m == "SetPlayerName") {
        Player *p = player(c.arg(1));
        if (!p || !p->local)
            return kDpErrInvalidPlayer;
        const uint32_t name = c.arg(2);
        p->shortName.clear();
        p->longName.clear();
        if (name) {
            if (!c.valid(name, 16))
                return kDpErrInvalidParams;
            p->shortName = c.read32(name + 8) ? c.string(c.read32(name + 8), 256) : std::string();
            p->longName = c.read32(name + 12) ? c.string(c.read32(name + 12), 256) : std::string();
        }
        const Message set = nameMessage(*p);
        systemMessage(set.bytes, set.pointers, p->id);
        if (node)
            node->setPlayerName(p->id, p->shortName, p->longName);
        return kSOk;
    }
    if (m == "SetPlayerData") {
        Player *p = player(c.arg(1));
        if (!p || !p->local)
            return kDpErrInvalidPlayer;
        if (c.arg(3) && !c.valid(c.arg(2), c.arg(3)))
            return kDpErrInvalidParams;
        std::vector<uint8_t> d(c.arg(3));
        for (uint32_t i = 0; i < c.arg(3); ++i)
            d[i] = c.read8(c.arg(2) + i);
        if (c.arg(4) & 1) {
            p->localData = std::move(d);
            return kSOk;
        }
        p->remoteData = d;
        const Message set = dataMessage(*p);
        systemMessage(set.bytes, set.pointers, p->id);
        if (node)
            node->setPlayerData(p->id, d);
        return kSOk;
    }
    return std::nullopt;
}

} // namespace

DirectPlay::~DirectPlay() = default;

void DirectPlay::setNetwork(std::shared_ptr<net::Sockets> sockets, std::vector<uint32_t> broadcast, uint16_t port) {
    _s->sockets = std::move(sockets);
    _s->broadcast = std::move(broadcast);
    _s->port = port ? port : dplay::kPort;
}

DirectPlay::DirectPlay(Win32Process &process) : _s(std::make_unique<State>(process)) {
    State *s = _s.get();
    s->com.define(s->lobby3A);
    s->com.define(s->play4A);
    auto earlier = process.createComObject;
    process.createComObject = [s, earlier](const std::string &clsid, const std::string &iid, uint32_t out) {
        std::shared_ptr<ComObject> object;
        if (clsid == kClsidLobby) {
            auto lobby = std::make_shared<Lobby>();
            lobby->s = s;
            object = lobby;
        } else if (clsid == kClsidPlay) {
            auto play = std::make_shared<Play>();
            play->s = s;
            object = play;
        } else {
            return earlier ? earlier(clsid, iid, out) : kClassNotRegistered;
        }
        const ComInterface *f = object->query(iid);
        if (!f)
            return kENoInterface;
        Cpu &c = s->process.cpu();
        if (c.valid(out, 4))
            c.write32(out, s->com.pointer(object, *f));
        return kSOk;
    };
}

} // namespace reluva::x86
