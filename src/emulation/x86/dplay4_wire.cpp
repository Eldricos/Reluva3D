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
    if (m.size() < kHeader || m[kSignature] != 'p' || m[kSignature + 1] != 'l' || m[kSignature + 2] != 'a' ||
        m[kSignature + 3] != 'y')
        return std::nullopt;
    return uint16_t(m[26] | m[27] << 8);
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
    if (command(m) != kEnumSessions || m.size() < kHeader + 24)
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
    put32(v, s.name.empty() ? 0 : uint32_t(v.size() + 4 - kSignature));
    if (!s.name.empty())
        putText(v, s.name);
    finish(v);
    return v;
}

bool readEnumSessionsReply(const std::vector<uint8_t> &m, dplay::SessionInfo &out) {
    if (command(m) != kEnumSessionsReply || m.size() < kHeader + kSessionDesc + 4)
        return false;
    out = dplay::SessionInfo();
    const size_t d = kHeader;
    out.flags = get32(m, d + 4);
    out.guid = guidText(m.data() + d + 8);
    out.application = guidText(m.data() + d + 24);
    out.maxPlayers = get32(m, d + 40);
    out.players = get32(m, d + 44);
    for (size_t i = 0; i < 4; ++i)
        out.user[i] = get32(m, d + 64 + 4 * i);
    const uint32_t nameAt = get32(m, d + kSessionDesc);
    if (nameAt && kSignature + nameAt < m.size())
        out.name = getText(m, kSignature + nameAt);
    // DPSESSION_PASSWORDREQUIRED (0x400): a password is wanted, not given.
    if (out.flags & 0x400)
        out.password = "?";
    return true;
}

} // namespace reluva::x86::dp4
