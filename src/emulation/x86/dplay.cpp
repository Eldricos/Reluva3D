// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectPlay of DirectX 6, from the SDK's documentation (dplay.h and
// dplobby.h's interfaces and their methods' order). Without a network: see
// docs/common/x86/dplay.md.

#include "emulation/x86/dplay.h"

#include "emulation/x86/com.h"

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
    explicit State(Win32Process &p) : process(p), cpu(p.cpu()), com(p) {}
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

class Play : public ComObject {
public:
    DirectPlay::State *s = nullptr;
    const ComInterface *query(const std::string &iid) override {
        for (const ComInterface *f : {&s->play1, &s->play4, &s->play4A})
            if (iid == f->iid)
                return f;
        return nullptr;
    }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // No service provider is there to connect through, and so no session.
        if (m == "EnumConnections" || m == "Close" || m == "Initialize")
            return kDpOk;
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
        if (m == "InitializeConnection" || m == "Open" || m == "SecureOpen")
            return kDpErrUnavailable;
        // GetPlayerAddress(player, buffer, *size): with no network there's
        // no address, of the local machine (player 0) or any player.
        if (m == "GetPlayerAddress") {
            if (c.valid(c.arg(3), 4))
                c.write32(c.arg(3), 0);
            return kDpErrUnavailable;
        }
        if (m == "GetCaps" || m == "GetPlayerCaps" || m == "GetMessageCount" || m == "GetMessageQueue")
            return kDpErrUnavailable;
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

DirectPlay::DirectPlay(Win32Process &process) : _s(std::make_unique<State>(process)) {
    State *s = _s.get();
    for (ComInterface *f :
         {&s->play1, &s->play4, &s->play4A, &s->lobby1, &s->lobby1A, &s->lobby2, &s->lobby2A, &s->lobby3, &s->lobby3A})
        s->com.define(*f);
    // The exports, by ordinal as programs import them: 1 DirectPlayCreate,
    // 2 DirectPlayEnumerateA, 3 DirectPlayEnumerateW, 4
    // DirectPlayLobbyCreateA, 5 DirectPlayLobbyCreateW.
    const char *dll = "DPLAYX.dll";
    for (const char *name : {"#1", "DirectPlayCreate"})
        process.provideStdcall(dll, name, 3, [s](Cpu &c) { return make<Play>(s, s->play1, c.arg(1)); });
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
