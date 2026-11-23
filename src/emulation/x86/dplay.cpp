// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/dplay.h"

#include "emulation/x86/com.h"

#include <string>
#include <string_view>
#include <vector>

namespace reluva::x86 {

namespace {

const char *const kClsidLobby = "{2fe8f810-b2a5-11d0-a787-0000f803abfc}";
const char *const kIidLobby3A = "{2db72491-652c-11d1-a7a8-0000f803abfc}";

constexpr uint32_t kClassNotRegistered = 0x80040154u;
// DirectPlay's HRESULTs: facility 0x877, as MAKE_DPHRESULT makes them.
constexpr uint32_t kDpErrNotLobbied = 0x88770000u + 1070;

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

} // namespace

struct DirectPlay::State {
    explicit State(Win32Process &p) : process(p), com(p) {}
    Win32Process &process;
    ComHost com;
    ComInterface lobby3A = makeLobby3A();
};

namespace {

// The lobby: the program learns it wasn't started by one.
class Lobby : public ComObject {
public:
    DirectPlay::State *s = nullptr;
    const ComInterface *query(const std::string &iid) override { return iid == s->lobby3A.iid ? &s->lobby3A : nullptr; }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // GetConnectionSettings(appId, data, size): no lobby started it,
        // so there are none.
        if (m == "GetConnectionSettings")
            return kDpErrNotLobbied;
        return std::nullopt;
    }
};

} // namespace

DirectPlay::~DirectPlay() = default;

DirectPlay::DirectPlay(Win32Process &process) : _s(std::make_unique<State>(process)) {
    State *s = _s.get();
    s->com.define(s->lobby3A);
    auto earlier = process.createComObject;
    process.createComObject = [s, earlier](const std::string &clsid, const std::string &iid, uint32_t out) {
        if (clsid != kClsidLobby)
            return earlier ? earlier(clsid, iid, out) : kClassNotRegistered;
        auto lobby = std::make_shared<Lobby>();
        lobby->s = s;
        const ComInterface *f = lobby->query(iid);
        if (!f)
            return kENoInterface;
        Cpu &c = s->process.cpu();
        if (c.valid(out, 4))
            c.write32(out, s->com.pointer(lobby, *f));
        return kSOk;
    };
}

} // namespace reluva::x86
