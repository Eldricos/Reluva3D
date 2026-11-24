// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/dplay4_wire.h"

#include <cctype>
#include <cstdio>

namespace reluva::x86::dp4 {

namespace {

// DPSP_MSG_HEADER: the size (20 bits) and token (12), the sender's
// SOCKADDR_IN, "play", the dialect and the command.
constexpr size_t kHeader = 28;
constexpr size_t kSignature = 20;  // where "play" is; the messages' offsets count from here
constexpr uint32_t kToken = 0xfab; // sent from a remote DirectPlay machine
constexpr uint16_t kEnumSessionsReply = 0x0001;
constexpr uint16_t kEnumSessions = 0x0002;
constexpr size_t kSessionDesc = 80; // DPSESSIONDESC2

void put16(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
}

void put32(std::vector<uint8_t> &v, uint32_t x) {
    put16(v, x & 0xffff);
    put16(v, x >> 16);
}

uint32_t get32(const std::vector<uint8_t> &v, size_t at) {
    return uint32_t(v[at]) | uint32_t(v[at + 1]) << 8 | uint32_t(v[at + 2]) << 16 | uint32_t(v[at + 3]) << 24;
}

// The header, its size filled in by finish(). The address is left 0: the
// receiver takes the one the message came from.
std::vector<uint8_t> header(uint16_t command, uint16_t port) {
    std::vector<uint8_t> v;
    put32(v, 0);
    put16(v, 2);                                            // AF_INET
    v.insert(v.end(), {uint8_t(port >> 8), uint8_t(port)}); // in network order
    v.insert(v.end(), 12, 0);                               // the address and padding
    v.insert(v.end(), {'p', 'l', 'a', 'y'});
    put16(v, kDialect);
    put16(v, command);
    return v;
}

void finish(std::vector<uint8_t> &v) {
    const uint32_t word = kToken << 20 | (uint32_t(v.size()) & 0xfffff);
    for (int i = 0; i < 4; ++i)
        v[size_t(i)] = uint8_t(word >> (8 * i));
}

// Text as UTF-16 with its terminator; only ASCII is kept, as the games'
// names are, others become '?'.
void putText(std::vector<uint8_t> &v, const std::string &s) {
    for (unsigned char c : s)
        put16(v, c < 0x80 ? c : '?');
    put16(v, 0);
}

std::string getText(const std::vector<uint8_t> &v, size_t at) {
    std::string s;
    for (; at + 1 < v.size(); at += 2) {
        const uint16_t c = uint16_t(v[at] | v[at + 1] << 8);
        if (!c)
            break;
        s += c < 0x80 ? char(c) : '?';
    }
    return s;
}

// Where "play" is: 20 in a whole message, 0 in one the reliable protocol
// carries; kNone in neither.
constexpr size_t kNone = ~size_t(0);
size_t signatureAt(const std::vector<uint8_t> &m) {
    const auto play = [&](size_t at) {
        return m.size() >= at + 8 && m[at] == 'p' && m[at + 1] == 'l' && m[at + 2] == 'a' && m[at + 3] == 'y';
    };
    if (play(kSignature))
        return kSignature;
    if (play(0))
        return 0;
    return kNone;
}

uint16_t get16(const std::vector<uint8_t> &v, size_t at) {
    return uint16_t(v[at] | v[at + 1] << 8);
}

// The bytes a name takes as UTF-16 with its terminator; 0 for none.
uint32_t textBytes(const std::string &s) {
    return s.empty() ? 0 : uint32_t(2 * (s.size() + 1));
}

void putAddress(std::vector<uint8_t> &v, const net::Address &a) {
    put16(v, 2); // AF_INET
    v.insert(v.end(), {uint8_t(a.port >> 8), uint8_t(a.port)});
    v.insert(v.end(), {uint8_t(a.ip >> 24), uint8_t(a.ip >> 16), uint8_t(a.ip >> 8), uint8_t(a.ip)});
    v.insert(v.end(), 8, 0);
}

net::Address getAddress(const std::vector<uint8_t> &v, size_t at) {
    return {uint32_t(v[at + 4]) << 24 | uint32_t(v[at + 5]) << 16 | uint32_t(v[at + 6]) << 8 | v[at + 7],
            uint16_t(v[at + 2] << 8 | v[at + 3])};
}

void putSessionDesc(std::vector<uint8_t> &v, const dplay::SessionInfo &s, uint32_t idSeed) {
    put32(v, kSessionDesc);
    put32(v, s.flags);
    uint8_t guid[16] = {};
    guidBytes(s.guid, guid);
    v.insert(v.end(), guid, guid + 16);
    uint8_t application[16] = {};
    guidBytes(s.application, application);
    v.insert(v.end(), application, application + 16);
    put32(v, s.maxPlayers);
    put32(v, s.players);
    put32(v, 0); // the name's and password's places in the sender's memory
    put32(v, 0);
    put32(v, idSeed);
    put32(v, 0);
    for (uint32_t u : s.user)
        put32(v, u);
}

void readSessionDesc(const std::vector<uint8_t> &m, size_t d, dplay::SessionInfo &out, uint32_t &idSeed) {
    out.flags = get32(m, d + 4);
    out.guid = guidText(m.data() + d + 8);
    out.application = guidText(m.data() + d + 24);
    out.maxPlayers = get32(m, d + 40);
    out.players = get32(m, d + 44);
    idSeed = get32(m, d + 56);
    for (size_t i = 0; i < 4; ++i)
        out.user[i] = get32(m, d + 64 + 4 * i);
}

// A length as the super-packed record gives it: 1, 2 or 4 bytes, by its
// two bits of the mask.
int lengthCode(uint32_t n) {
    return n < 0x100 ? 1 : n < 0x10000 ? 2 : 3;
}

void putLength(std::vector<uint8_t> &v, int code, uint32_t n) {
    if (code == 1)
        v.push_back(uint8_t(n));
    else if (code == 2)
        put16(v, n);
    else
        put32(v, n);
}

bool getLength(const std::vector<uint8_t> &v, size_t &at, int code, uint32_t &n) {
    const size_t size = code == 1 ? 1 : code == 2 ? 2 : 4;
    if (at + size > v.size())
        return false;
    n = size == 1 ? v[at] : size == 2 ? get16(v, at) : get32(v, at);
    at += size;
    return true;
}

// A name in a record: UTF-16 to its terminator, moved past.
bool getName(const std::vector<uint8_t> &v, size_t &at, std::string &out) {
    out.clear();
    for (; at + 1 < v.size(); at += 2) {
        const uint16_t c = get16(v, at);
        if (!c) {
            at += 2;
            return true;
        }
        out += c < 0x80 ? char(c) : '?';
    }
    return false;
}

// The fixed part of the messages that name a player: IDTo, PlayerID,
// GroupID, CreateOffset and PasswordOffset.
void putPlayerFields(std::vector<uint8_t> &v, uint32_t to, uint32_t player, uint32_t createOffset,
                     uint32_t passwordOffset) {
    put32(v, to);
    put32(v, player);
    put32(v, 0);
    put32(v, createOffset);
    put32(v, passwordOffset);
}

// A message of `command` read: where its fields start (after the
// command), or kNone if it isn't one or is shorter than `size` past it.
size_t body(const std::vector<uint8_t> &m, uint16_t want, size_t size) {
    const size_t at = signatureAt(m);
    if (at == kNone || get16(m, at + 6) != want || m.size() < at + 8 + size)
        return kNone;
    return at + 8;
}

constexpr uint32_t kCreateOffset = 28; // from the signature: its 8 bytes, then the five fields

} // namespace

bool guidBytes(const std::string &text, uint8_t out[16]) {
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned d[8] = {};
    if (std::sscanf(text.c_str(), "{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}", &a, &b, &c, &d[0], &d[1], &d[2], &d[3],
                    &d[4], &d[5], &d[6], &d[7]) != 11)
        return false;
    for (int i = 0; i < 4; ++i)
        out[i] = uint8_t(a >> (8 * i));
    out[4] = uint8_t(b), out[5] = uint8_t(b >> 8);
    out[6] = uint8_t(c), out[7] = uint8_t(c >> 8);
    for (int i = 0; i < 8; ++i)
        out[8 + i] = uint8_t(d[i]);
    return true;
}

std::string guidText(const uint8_t *g) {
    char s[40];
    std::snprintf(s, sizeof s, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                  unsigned(g[0] | g[1] << 8 | g[2] << 16 | unsigned(g[3]) << 24), unsigned(g[4] | g[5] << 8),
                  unsigned(g[6] | g[7] << 8), g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return s;
}

std::optional<uint16_t> command(const std::vector<uint8_t> &m) {
    const size_t at = signatureAt(m);
    if (at == kNone)
        return std::nullopt;
    return uint16_t(m[at + 6] | m[at + 7] << 8);
}

uint16_t senderPort(const std::vector<uint8_t> &m) {
    if (m.size() < kHeader || signatureAt(m) != kSignature)
        return 0;
    return uint16_t(m[6] << 8 | m[7]);
}

bool isSystem(const std::vector<uint8_t> &m) {
    return signatureAt(m) != kNone;
}

std::vector<uint8_t> bare(const std::vector<uint8_t> &m) {
    if (signatureAt(m) != kSignature)
        return m;
    return {m.begin() + long(kSignature), m.end()};
}

int messageLength(const std::vector<uint8_t> &stream) {
    if (stream.size() < 4)
        return 0;
    const uint32_t word = get32(stream, 0);
    const uint32_t token = word >> 20;
    const uint32_t size = word & 0xfffff;
    if ((token != 0xfab && token != 0xcab && token != 0xbab) || size < 4)
        return -1;
    return stream.size() >= size ? int(size) : 0;
}

std::vector<uint8_t> enumSessions(const EnumRequest &r) {
    std::vector<uint8_t> v = header(kEnumSessions, r.replyPort);
    if (r.replyIp)
        for (int i = 0; i < 4; ++i)
            v[8 + size_t(i)] = uint8_t(r.replyIp >> (24 - 8 * i));
    uint8_t guid[16] = {};
    guidBytes(r.application, guid);
    v.insert(v.end(), guid, guid + 16);
    // The password follows the fixed part, its offset from the signature.
    put32(v, r.password.empty() ? 0 : uint32_t(kHeader + 24 - kSignature));
    put32(v, r.flags);
    if (!r.password.empty())
        putText(v, r.password);
    finish(v);
    return v;
}

bool readEnumSessions(const std::vector<uint8_t> &m, EnumRequest &out) {
    if (signatureAt(m) != kSignature || command(m) != kEnumSessions || m.size() < kHeader + 24)
        return false;
    out = EnumRequest();
    out.replyPort = uint16_t(m[6] << 8 | m[7]);
    out.replyIp = uint32_t(m[8]) << 24 | uint32_t(m[9]) << 16 | uint32_t(m[10]) << 8 | m[11];
    out.application = guidText(m.data() + kHeader);
    const uint32_t passwordAt = get32(m, kHeader + 16);
    out.flags = get32(m, kHeader + 20);
    if (passwordAt)
        out.password = getText(m, kSignature + passwordAt);
    return true;
}

std::vector<uint8_t> enumSessionsReply(const dplay::SessionInfo &s, uint16_t port, uint32_t idSeed) {
    std::vector<uint8_t> v = header(kEnumSessionsReply, port);
    putSessionDesc(v, s, idSeed);
    put32(v, s.name.empty() ? 0 : uint32_t(v.size() + 4 - kSignature));
    if (!s.name.empty())
        putText(v, s.name);
    finish(v);
    return v;
}

bool readEnumSessionsReply(const std::vector<uint8_t> &m, dplay::SessionInfo &out) {
    if (signatureAt(m) != kSignature || command(m) != kEnumSessionsReply || m.size() < kHeader + kSessionDesc + 4)
        return false;
    out = dplay::SessionInfo();
    const size_t d = kHeader;
    uint32_t idSeed = 0;
    readSessionDesc(m, d, out, idSeed);
    const uint32_t nameAt = get32(m, d + kSessionDesc);
    if (nameAt && kSignature + nameAt < m.size())
        out.name = getText(m, kSignature + nameAt);
    // DPSESSION_PASSWORDREQUIRED (0x400): a password is wanted, not given.
    if (out.flags & 0x400)
        out.password = "?";
    return true;
}

void putPackedPlayer(std::vector<uint8_t> &v, const Player &p) {
    const uint32_t shortBytes = textBytes(p.shortName);
    const uint32_t longBytes = textBytes(p.longName);
    const uint32_t spBytes = p.addresses ? 32 : 0;
    put32(v, 48 + shortBytes + longBytes + spBytes + uint32_t(p.data.size()) + 4 * uint32_t(p.players.size()));
    put32(v, p.flags);
    put32(v, p.id);
    put32(v, shortBytes);
    put32(v, longBytes);
    put32(v, spBytes);
    put32(v, uint32_t(p.data.size()));
    put32(v, uint32_t(p.players.size()));
    put32(v, p.systemPlayer);
    put32(v, 48); // FixedSize
    put32(v, p.version);
    put32(v, p.parent);
    if (shortBytes)
        putText(v, p.shortName);
    if (longBytes)
        putText(v, p.longName);
    if (p.addresses) {
        putAddress(v, p.stream);
        putAddress(v, p.datagram);
    }
    v.insert(v.end(), p.data.begin(), p.data.end());
    for (uint32_t id : p.players)
        put32(v, id);
}

bool readPackedPlayer(const std::vector<uint8_t> &v, size_t at, Player &out) {
    if (at + 48 > v.size())
        return false;
    out = Player();
    out.flags = get32(v, at + 4);
    out.id = get32(v, at + 8);
    const uint32_t shortBytes = get32(v, at + 12);
    const uint32_t longBytes = get32(v, at + 16);
    const uint32_t spBytes = get32(v, at + 20);
    const uint32_t dataBytes = get32(v, at + 24);
    const uint32_t count = get32(v, at + 28);
    out.systemPlayer = get32(v, at + 32);
    const uint32_t fixed = get32(v, at + 36);
    out.version = get32(v, at + 40);
    out.parent = get32(v, at + 44);
    if (fixed < 48 || count > 0x10000)
        return false;
    size_t p = at + fixed;
    const uint64_t need = uint64_t(shortBytes) + longBytes + spBytes + dataBytes + 4ull * count;
    if (p + need > v.size())
        return false;
    if (shortBytes) {
        size_t n = p;
        getName(v, n, out.shortName);
    }
    p += shortBytes;
    if (longBytes) {
        size_t n = p;
        getName(v, n, out.longName);
    }
    p += longBytes;
    if (spBytes >= 32) {
        out.addresses = true;
        out.stream = getAddress(v, p);
        out.datagram = getAddress(v, p + 16);
    }
    p += spBytes;
    out.data.assign(v.begin() + long(p), v.begin() + long(p + dataBytes));
    p += dataBytes;
    for (uint32_t i = 0; i < count; ++i, p += 4)
        out.players.push_back(get32(v, p));
    return true;
}

// The mask's bits: names (1, 2), the lengths of the service provider's
// data (<< 2), the player's data (<< 4) and a group's players (<< 6),
// the parent (0x100) and shortcuts (<< 9).
void putSuperPackedPlayer(std::vector<uint8_t> &v, const Player &p) {
    const bool system = p.flags & kSystemPlayer;
    const uint32_t spBytes = p.addresses ? 32 : 0;
    uint32_t mask = 0;
    if (!p.shortName.empty())
        mask |= 1;
    if (!p.longName.empty())
        mask |= 2;
    if (spBytes)
        mask |= uint32_t(lengthCode(spBytes)) << 2;
    if (!p.data.empty())
        mask |= uint32_t(lengthCode(uint32_t(p.data.size()))) << 4;
    if (!p.players.empty())
        mask |= uint32_t(lengthCode(uint32_t(p.players.size()))) << 6;
    if (p.parent)
        mask |= 0x100;
    put32(v, 16);
    put32(v, p.flags);
    put32(v, p.id);
    put32(v, mask);
    put32(v, system ? p.version : p.systemPlayer);
    if (mask & 1)
        putText(v, p.shortName);
    if (mask & 2)
        putText(v, p.longName);
    if (mask & 0x30) {
        putLength(v, int(mask >> 4 & 3), uint32_t(p.data.size()));
        v.insert(v.end(), p.data.begin(), p.data.end());
    }
    if (mask & 0x0c) {
        putLength(v, int(mask >> 2 & 3), spBytes);
        putAddress(v, p.stream);
        putAddress(v, p.datagram);
    }
    if (mask & 0xc0) {
        putLength(v, int(mask >> 6 & 3), uint32_t(p.players.size()));
        for (uint32_t id : p.players)
            put32(v, id);
    }
    if (mask & 0x100)
        put32(v, p.parent);
}

bool readSuperPackedPlayer(const std::vector<uint8_t> &v, size_t &at, Player &out) {
    if (at + 20 > v.size())
        return false;
    out = Player();
    const uint32_t size = get32(v, at);
    out.flags = get32(v, at + 4);
    out.id = get32(v, at + 8);
    const uint32_t mask = get32(v, at + 12);
    const uint32_t versionOrSystem = get32(v, at + 16);
    if (out.flags & kSystemPlayer) {
        out.version = versionOrSystem;
        out.systemPlayer = out.id;
    } else {
        out.systemPlayer = versionOrSystem;
    }
    if (size < 16)
        return false;
    size_t p = at + size + 4;
    if ((mask & 1) && !getName(v, p, out.shortName))
        return false;
    if ((mask & 2) && !getName(v, p, out.longName))
        return false;
    uint32_t n = 0;
    if (mask & 0x30) {
        if (!getLength(v, p, int(mask >> 4 & 3), n) || p + n > v.size())
            return false;
        out.data.assign(v.begin() + long(p), v.begin() + long(p + n));
        p += n;
    }
    if (mask & 0x0c) {
        if (!getLength(v, p, int(mask >> 2 & 3), n) || p + n > v.size())
            return false;
        if (n >= 32) {
            out.addresses = true;
            out.stream = getAddress(v, p);
            out.datagram = getAddress(v, p + 16);
        }
        p += n;
    }
    if (mask & 0xc0) {
        if (!getLength(v, p, int(mask >> 6 & 3), n) || p + 4ull * n > v.size())
            return false;
        for (uint32_t i = 0; i < n; ++i, p += 4)
            out.players.push_back(get32(v, p));
    }
    if (mask & 0x100) {
        if (p + 4 > v.size())
            return false;
        out.parent = get32(v, p);
        p += 4;
    }
    if (mask & 0x600) { // shortcuts: counted, their ids skipped
        if (!getLength(v, p, int(mask >> 9 & 3), n) || p + 4ull * n > v.size())
            return false;
        p += 4ull * n;
    }
    at = p;
    return true;
}

std::vector<uint8_t> requestPlayerId(uint16_t port, uint32_t flags) {
    std::vector<uint8_t> v = header(kRequestPlayerId, port);
    put32(v, flags);
    finish(v);
    return v;
}

bool readRequestPlayerId(const std::vector<uint8_t> &m, uint32_t &flags) {
    const size_t at = body(m, kRequestPlayerId, 4);
    if (at == kNone)
        return false;
    flags = get32(m, at);
    return true;
}

// The security description is all zeros, and no providers named: a
// session that isn't secure ([MC-DPL4CS] 3.2.5.4).
std::vector<uint8_t> requestPlayerReply(uint16_t port, uint32_t id, uint32_t result) {
    std::vector<uint8_t> v = header(kRequestPlayerReply, port);
    put32(v, id);
    v.insert(v.end(), 24 + 8, 0);
    put32(v, result);
    finish(v);
    return v;
}

bool readRequestPlayerReply(const std::vector<uint8_t> &m, uint32_t &id, uint32_t &result) {
    const size_t at = body(m, kRequestPlayerReply, 40);
    if (at == kNone)
        return false;
    id = get32(m, at);
    result = get32(m, at + 36);
    return true;
}

std::vector<uint8_t> addForwardRequest(uint16_t port, uint32_t to, const Player &system, const std::string &password,
                                       uint32_t tick) {
    std::vector<uint8_t> v = header(kAddForwardRequest, port);
    std::vector<uint8_t> packed;
    putPackedPlayer(packed, system);
    putPlayerFields(v, to, system.id, kCreateOffset, kCreateOffset + uint32_t(packed.size()));
    v.insert(v.end(), packed.begin(), packed.end());
    // The password is there even when empty: its terminator alone.
    for (unsigned char c : password)
        put16(v, c < 0x80 ? c : '?');
    put16(v, 0);
    put32(v, tick);
    finish(v);
    return v;
}

bool readAddForwardRequest(const std::vector<uint8_t> &m, Player &system, std::string &password) {
    const size_t at = body(m, kAddForwardRequest, 20);
    if (at == kNone)
        return false;
    const size_t sig = at - 8;
    if (!readPackedPlayer(m, sig + get32(m, at + 12), system))
        return false;
    password.clear();
    const uint32_t passwordAt = get32(m, at + 16);
    if (passwordAt && sig + passwordAt < m.size()) {
        size_t p = sig + passwordAt;
        getName(m, p, password);
    }
    return true;
}

std::vector<uint8_t> addForward(uint16_t port, uint32_t to, const Player &system) {
    std::vector<uint8_t> v = header(kAddForward, port);
    putPlayerFields(v, to, system.id, kCreateOffset, 0);
    putPackedPlayer(v, system);
    finish(v);
    return v;
}

bool readAddForward(const std::vector<uint8_t> &m, Player &system) {
    const size_t at = body(m, kAddForward, 20);
    return at != kNone && readPackedPlayer(m, at - 8 + get32(m, at + 12), system);
}

std::vector<uint8_t> addForwardAck(uint16_t port, uint32_t id) {
    std::vector<uint8_t> v = header(kAddForwardAck, port);
    put32(v, id);
    finish(v);
    return v;
}

bool readAddForwardAck(const std::vector<uint8_t> &m, uint32_t &id) {
    const size_t at = body(m, kAddForwardAck, 4);
    if (at == kNone)
        return false;
    id = get32(m, at);
    return true;
}

std::vector<uint8_t> addForwardReply(uint16_t port, uint32_t error) {
    std::vector<uint8_t> v = header(kAddForwardReply, port);
    put32(v, error);
    finish(v);
    return v;
}

bool readAddForwardReply(const std::vector<uint8_t> &m, uint32_t &error) {
    const size_t at = body(m, kAddForwardReply, 4);
    if (at == kNone)
        return false;
    error = get32(m, at);
    return true;
}

std::vector<uint8_t> createPlayer(uint16_t port, const Player &player) {
    std::vector<uint8_t> v = header(kCreatePlayer, port);
    putPlayerFields(v, 0, player.id, kCreateOffset, 0);
    putPackedPlayer(v, player);
    v.insert(v.end(), 6, 0); // Reserved1 and Reserved2
    finish(v);
    return v;
}

bool readCreatePlayer(const std::vector<uint8_t> &m, Player &player) {
    size_t at = body(m, kCreatePlayer, 20);
    if (at == kNone)
        at = body(m, kCreatePlayerVerify, 20); // laid out as one, and taken as one ([MC-DPL4CS] 3.1.5.13)
    return at != kNone && readPackedPlayer(m, at - 8 + get32(m, at + 12), player);
}

std::vector<uint8_t> deletePlayer(uint16_t port, uint32_t id) {
    std::vector<uint8_t> v = header(kDeletePlayer, port);
    putPlayerFields(v, 0, id, 0, 0);
    finish(v);
    return v;
}

bool readDeletePlayer(const std::vector<uint8_t> &m, uint32_t &id) {
    const size_t at = body(m, kDeletePlayer, 8);
    if (at == kNone)
        return false;
    id = get32(m, at + 4);
    return true;
}

std::vector<uint8_t> ping(uint16_t port, uint32_t from, uint32_t tick, bool reply) {
    std::vector<uint8_t> v = header(reply ? kPingReply : kPing, port);
    put32(v, from);
    put32(v, tick);
    finish(v);
    return v;
}

bool readPing(const std::vector<uint8_t> &m, uint32_t &from, uint32_t &tick) {
    size_t at = body(m, kPing, 8);
    if (at == kNone)
        at = body(m, kPingReply, 8);
    if (at == kNone)
        return false;
    from = get32(m, at);
    tick = get32(m, at + 4);
    return true;
}

// Its offsets, like the others', count from the signature: the session
// right after the seven fields, its name and password, then the players.
std::vector<uint8_t> superEnumPlayersReply(uint16_t port, const dplay::SessionInfo &session, uint32_t idSeed,
                                           const std::vector<Player> &players) {
    std::vector<uint8_t> v = header(kSuperEnumPlayersReply, port);
    uint32_t playerCount = 0;
    for (const Player &p : players)
        playerCount += p.players.empty() ? 1 : 0;
    const uint32_t descAt = 8 + 28;
    const uint32_t nameAt = session.name.empty() ? 0 : descAt + kSessionDesc;
    const uint32_t nameBytes = textBytes(session.name);
    const bool password = !session.password.empty() && session.password != "?";
    const uint32_t passwordAt = password ? descAt + kSessionDesc + nameBytes : 0;
    const uint32_t packedAt = descAt + kSessionDesc + nameBytes + (password ? textBytes(session.password) : 0);
    put32(v, playerCount);
    put32(v, uint32_t(players.size()) - playerCount);
    put32(v, packedAt);
    put32(v, 0); // shortcuts
    put32(v, descAt);
    put32(v, nameAt);
    put32(v, passwordAt);
    putSessionDesc(v, session, idSeed);
    if (nameBytes)
        putText(v, session.name);
    if (password)
        putText(v, session.password);
    // Players first, then groups, as the message's order is fixed.
    for (const Player &p : players)
        if (p.players.empty())
            putSuperPackedPlayer(v, p);
    for (const Player &p : players)
        if (!p.players.empty())
            putSuperPackedPlayer(v, p);
    finish(v);
    return v;
}

bool readSuperEnumPlayersReply(const std::vector<uint8_t> &m, dplay::SessionInfo &session, uint32_t &idSeed,
                               std::vector<Player> &players) {
    const size_t at = body(m, kSuperEnumPlayersReply, 28);
    if (at == kNone)
        return false;
    const size_t sig = at - 8;
    const uint64_t count = uint64_t(get32(m, at)) + get32(m, at + 4) + get32(m, at + 12);
    const uint32_t packedAt = get32(m, at + 8);
    const uint32_t descAt = get32(m, at + 16);
    const uint32_t nameAt = get32(m, at + 20);
    if (sig + descAt + kSessionDesc > m.size() || count > 0x10000)
        return false;
    session = dplay::SessionInfo();
    readSessionDesc(m, sig + descAt, session, idSeed);
    if (nameAt) {
        size_t p = sig + nameAt;
        getName(m, p, session.name);
    }
    if (get32(m, at + 24))
        session.password = "?";
    players.clear();
    size_t p = sig + packedAt;
    for (uint64_t i = 0; i < count; ++i) {
        Player player;
        if (!readSuperPackedPlayer(m, p, player))
            return false;
        players.push_back(std::move(player));
    }
    return true;
}

// A player's message has no signature of its own: the size and token,
// the address's room, then the ids and the data.
std::vector<uint8_t> playerMessage(uint16_t port, uint32_t from, uint32_t to, const std::vector<uint8_t> &data,
                                   bool ids) {
    std::vector<uint8_t> v;
    put32(v, 0);
    put16(v, 2);
    v.insert(v.end(), {uint8_t(port >> 8), uint8_t(port)});
    v.insert(v.end(), 12, 0);
    if (ids) {
        put32(v, from);
        put32(v, to);
    }
    v.insert(v.end(), data.begin(), data.end());
    finish(v);
    // Data that puts "play" where a system message has it would be taken
    // for one: it goes wrapped ([MC-DPL4CS] 3.1.4.16.2).
    if (signatureAt(v) == kSignature) {
        std::vector<uint8_t> w = header(kPlayerWrapper, port);
        w.insert(w.end(), v.begin(), v.end());
        finish(w);
        return w;
    }
    return v;
}

bool readPlayerMessage(const std::vector<uint8_t> &m, bool ids, uint32_t &from, uint32_t &to,
                       std::vector<uint8_t> &data) {
    // A wrapper's message is a player's, whatever it looks like.
    size_t base = 0;
    if (const size_t at = body(m, kPlayerWrapper, 0); at != kNone)
        base = at;
    else if (isSystem(m))
        return false;
    const size_t start = base + kSignature + (ids ? 8 : 0);
    if (m.size() < start)
        return false;
    from = ids ? get32(m, base + kSignature) : 0;
    to = ids ? get32(m, base + kSignature + 4) : 0;
    data.assign(m.begin() + long(start), m.end());
    return true;
}

} // namespace reluva::x86::dp4
