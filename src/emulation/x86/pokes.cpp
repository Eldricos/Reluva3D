// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/pokes.h"

#include <cstdio>
#include <cstdlib>

namespace reluva::x86 {

namespace {

// The fields of "a:b:c...", at most `max` (the last takes the rest).
std::vector<std::string> fields(const std::string &text, size_t max) {
    std::vector<std::string> out;
    size_t at = 0;
    while (out.size() + 1 < max) {
        const size_t colon = text.find(':', at);
        if (colon == std::string::npos)
            break;
        out.push_back(text.substr(at, colon - at));
        at = colon + 1;
    }
    out.push_back(text.substr(at));
    return out;
}

bool number(const std::string &s, uint64_t &value, bool allowNegative = false) {
    if (s.empty())
        return false;
    char *end = nullptr;
    if (allowNegative && s[0] == '-')
        value = uint64_t(std::strtoll(s.c_str(), &end, 0));
    else if (s[0] != '-')
        value = std::strtoull(s.c_str(), &end, 0);
    else
        return false;
    return end && *end == 0;
}

bool parseWhen(const std::string &s, Poke &poke, std::string &error) {
    uint64_t v = 0;
    if (s.rfind("ms=", 0) == 0) {
        if (!number(s.substr(3), v))
            return error = "not a millisecond: " + s, false;
        poke.ms = v;
        poke.atAddress = 0;
        return true;
    }
    if (s.rfind("at=", 0) == 0) {
        const std::string rest = s.substr(3);
        const size_t hash = rest.find('#');
        if (!number(rest.substr(0, hash), v) || v == 0 || v > 0xffffffffu)
            return error = "not an instruction address: " + s, false;
        poke.atAddress = uint32_t(v);
        poke.nth = 1;
        if (hash != std::string::npos) {
            if (!number(rest.substr(hash + 1), v) || v == 0 || v > 0xffffffffu)
                return error = "not a count: " + s, false;
            poke.nth = uint32_t(v);
        }
        return true;
    }
    return error = "the moment should be ms=N or at=ADDR[#N]: " + s, false;
}

bool parseAddress(const std::string &s, uint32_t &address, std::string &error) {
    uint64_t v = 0;
    if (!number(s, v) || v > 0xffffffffu)
        return error = "not an address: " + s, false;
    address = uint32_t(v);
    return true;
}

std::string hex(const std::vector<uint8_t> &bytes) {
    std::string out;
    char b[4];
    for (uint8_t x : bytes) {
        std::snprintf(b, sizeof b, "%02x", x);
        out += b;
    }
    return out;
}

} // namespace

bool parsePoke(const std::string &text, bool every, Poke &poke, std::string &error) {
    poke = Poke{};
    poke.every = every;
    const auto f = fields(text, 4);
    if (f.size() < 3)
        return error = "a poke is WHEN:ADDR:VALUE[:SIZE]: " + text, false;
    if (!parseWhen(f[0], poke, error) || !parseAddress(f[1], poke.address, error))
        return false;
    if (f[2].rfind("bytes=", 0) == 0) {
        const std::string h = f[2].substr(6);
        if (h.empty() || h.size() % 2 || f.size() > 3)
            return error = "bytes= takes an even number of hex digits, and no size: " + text, false;
        for (size_t i = 0; i < h.size(); i += 2) {
            char *end = nullptr;
            const std::string pair = h.substr(i, 2);
            const unsigned long b = std::strtoul(pair.c_str(), &end, 16);
            if (!end || *end)
                return error = "not hex: " + pair, false;
            poke.bytes.push_back(uint8_t(b));
        }
        return true;
    }
    uint64_t value = 0, size = 4;
    if (!number(f[2], value, true))
        return error = "not a value: " + f[2], false;
    if (f.size() > 3 && (!number(f[3], size) || (size != 1 && size != 2 && size != 4)))
        return error = "the size is 1, 2 or 4: " + text, false;
    for (uint64_t i = 0; i < size; ++i)
        poke.bytes.push_back(uint8_t(value >> (8 * i)));
    return true;
}

bool parseDump(const std::string &text, Poke &poke, std::string &error) {
    poke = Poke{};
    const auto f = fields(text, 4);
    if (f.size() < 4 || f[3].empty())
        return error = "a dump is WHEN:ADDR:SIZE:file: " + text, false;
    uint64_t size = 0;
    if (!parseWhen(f[0], poke, error) || !parseAddress(f[1], poke.address, error))
        return false;
    if (!number(f[2], size) || size == 0 || size > 0xffffffffu)
        return error = "not a size: " + f[2], false;
    poke.dumpSize = uint32_t(size);
    poke.dumpFile = f[3];
    return true;
}

bool Pokes::needsTrace() const {
    for (const auto &p : list)
        if (p.atAddress)
            return true;
    return false;
}

void Pokes::instruction(Cpu &cpu, uint64_t nowMs) {
    for (auto &p : list)
        if (p.atAddress == cpu.eip && ++p.reached >= p.nth && (p.every || !p.applied))
            apply(p, cpu, nowMs);
}

void Pokes::time(Cpu &cpu, uint64_t nowMs) {
    for (auto &p : list)
        if (!p.atAddress && nowMs >= p.ms && (p.every || !p.applied))
            apply(p, cpu, nowMs);
}

void Pokes::apply(Poke &p, Cpu &cpu, uint64_t nowMs) {
    ++p.applied;
    char line[256];
    char when[64];
    if (p.atAddress)
        std::snprintf(when, sizeof when, "at %08x (time %u)", p.atAddress, p.reached);
    else
        std::snprintf(when, sizeof when, "eip %08x", cpu.eip);
    if (!p.dumpFile.empty()) {
        const bool ok = cpu.valid(p.address, p.dumpSize);
        if (ok && save)
            save(p.dumpFile,
                 std::vector<uint8_t>(cpu.memory().begin() + p.address, cpu.memory().begin() + p.address + p.dumpSize));
        std::snprintf(line, sizeof line, "  dumpmem %08x, %u bytes -> %s at %llu ms, %s%s", p.address, p.dumpSize,
                      p.dumpFile.c_str(), (unsigned long long)nowMs, when, ok ? "" : ": not in memory");
        if (log)
            log(line);
        return;
    }
    const auto size = uint32_t(p.bytes.size());
    const bool ok = cpu.valid(p.address, size);
    std::vector<uint8_t> was;
    if (ok) {
        was.assign(cpu.memory().begin() + p.address, cpu.memory().begin() + p.address + size);
        std::copy(p.bytes.begin(), p.bytes.end(), cpu.memory().begin() + p.address);
        cpu.noteWrite(p.address, size); // the decoded-instruction cache sees it, if it's code
    }
    if (log && (p.applied <= 3 || !ok)) {
        std::snprintf(line, sizeof line, "  poke %08x <- %s (%u bytes, was %s) at %llu ms, %s%s", p.address,
                      hex(p.bytes).c_str(), size, ok ? hex(was).c_str() : "-", (unsigned long long)nowMs, when,
                      !ok                         ? ": not in memory"
                      : p.applied == 3 && p.every ? "; further times not logged"
                                                  : "");
        log(line);
    }
}

} // namespace reluva::x86
