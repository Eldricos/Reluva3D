// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// IDirectPlay2 and IDirectPlay3, in their Unicode and ANSI forms, as the
// DirectX 5 SDK's dplay.h declares them.

#include "emulation/x86/dplay.h"

#include "emulation/x86/com.h"

#include <string_view>

namespace reluva::x86 {

namespace {

const char *const kClsidDirectPlay = "{d1eb6d20-8923-11d0-9d97-00a0c90a43cb}";
const char *const kIidDirectPlay2 = "{2b74f7c0-9154-11cf-a9cd-00aa006886e3}";
const char *const kIidDirectPlay2A = "{9d460580-a822-11cf-960c-0080c7534e82}";
const char *const kIidDirectPlay3 = "{133efe40-32dc-11d0-9cfb-00a0c90a43cb}";
const char *const kIidDirectPlay3A = "{133efe41-32dc-11d0-9cfb-00a0c90a43cb}";

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

} // namespace

struct DirectPlay::State {
    ComHost com;
    ComInterface directPlay2 = makeInterface("IDirectPlay2", kIidDirectPlay2, kDirectPlay2Methods);
    ComInterface directPlay2A = makeInterface("IDirectPlay2A", kIidDirectPlay2A, kDirectPlay2Methods);
    ComInterface directPlay3 = makeInterface("IDirectPlay3", kIidDirectPlay3, kDirectPlay3.size());
    ComInterface directPlay3A = makeInterface("IDirectPlay3A", kIidDirectPlay3A, kDirectPlay3.size());
    explicit State(Win32Process &p) : com(p) {}
};

namespace {

class Session : public ComObject {
public:
    DirectPlay::State *s = nullptr;

    const ComInterface *query(const std::string &iid) override {
        for (const ComInterface *f : {&s->directPlay3A, &s->directPlay3, &s->directPlay2A, &s->directPlay2})
            if (iid == f->iid)
                return f;
        return nullptr;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // EnumConnections(application guid, callback, context, flags): there
        // are none to list.
        if (m == "EnumConnections")
            return kSOk;
        if (m == "Close")
            return kSOk;
        return std::nullopt;
    }
};

} // namespace

DirectPlay::~DirectPlay() = default;

DirectPlay::DirectPlay(Win32Process &process) : _s(std::make_unique<State>(process)) {
    State *s = _s.get();
    for (ComInterface *f : {&s->directPlay2, &s->directPlay2A, &s->directPlay3, &s->directPlay3A})
        s->com.define(*f);
    auto earlier = process.createComObject;
    process.createComObject = [s, &process, earlier](const std::string &clsid, const std::string &iid, uint32_t out) {
        if (clsid != kClsidDirectPlay)
            return earlier ? earlier(clsid, iid, out) : kClassNotRegistered;
        auto session = std::make_shared<Session>();
        session->s = s;
        const ComInterface *f = session->query(iid);
        if (!f)
            return kENoInterface;
        if (process.cpu().valid(out, 4))
            process.cpu().write32(out, s->com.pointer(session, *f));
        return kSOk;
    };
}

} // namespace reluva::x86
