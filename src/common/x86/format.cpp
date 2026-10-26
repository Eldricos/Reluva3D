// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/x86/format.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace reluva::x86 {

std::string formatPrintf(const Cpu& cpu, uint32_t format, uint32_t args) {
    const std::string f = cpu.string(format, 8192);
    std::string out;
    uint32_t next = args;
    auto word = [&] {
        const uint32_t v = cpu.read32(next);
        next += 4;
        return v;
    };
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] != '%') {
            out += f[i];
            continue;
        }
        // The specification, rebuilt for the host's snprintf.
        std::string spec = "%";
        size_t j = i + 1;
        while (j < f.size() && std::strchr("-+ #0", f[j]))
            spec += f[j++];
        auto number = [&] {
            if (j < f.size() && f[j] == '*') {
                spec += std::to_string(int32_t(word()));
                ++j;
                return;
            }
            while (j < f.size() && f[j] >= '0' && f[j] <= '9')
                spec += f[j++];
        };
        number();
        if (j < f.size() && f[j] == '.') {
            spec += f[j++];
            number();
        }
        bool wide = false; // 64-bit
        while (j < f.size()) {
            if (f.compare(j, 2, "ll") == 0 || f.compare(j, 3, "I64") == 0) {
                wide = true;
                j += f[j] == 'l' ? 2 : 3;
            } else if (f[j] == 'h' || f[j] == 'l' || f[j] == 'L') {
                ++j;
            } else {
                break;
            }
        }
        if (j >= f.size())
            break;
        const char c = f[j];
        char buf[512];
        buf[0] = 0;
        switch (c) {
        case '%': std::strcpy(buf, "%"); break;
        case 'd':
        case 'i':
            if (wide) {
                const uint64_t lo = word(), hi = word();
                std::snprintf(buf, sizeof buf, (spec + "lld").c_str(), (long long)(int64_t(hi << 32 | lo)));
            } else {
                std::snprintf(buf, sizeof buf, (spec + "d").c_str(), int(int32_t(word())));
            }
            break;
        case 'u':
        case 'x':
        case 'X':
        case 'o':
            if (wide) {
                const uint64_t lo = word(), hi = word();
                std::snprintf(buf, sizeof buf, (spec + "ll" + c).c_str(), (unsigned long long)(hi << 32 | lo));
            } else {
                std::snprintf(buf, sizeof buf, (spec + c).c_str(), unsigned(word()));
            }
            break;
        case 'p': std::snprintf(buf, sizeof buf, "%08X", unsigned(word())); break;
        case 'c': std::snprintf(buf, sizeof buf, (spec + "c").c_str(), int(word() & 0xff)); break;
        case 's': {
            const uint32_t p = word();
            const std::string s = p ? cpu.string(p, 8192) : "(null)";
            // Long strings directly; snprintf only for padding.
            if (spec == "%")
                out += s;
            else {
                std::vector<char> big(s.size() + 512);
                std::snprintf(big.data(), big.size(), (spec + "s").c_str(), s.c_str());
                out += big.data();
            }
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G': {
            const uint64_t lo = word(), hi = word();
            const uint64_t bits = hi << 32 | lo;
            double d;
            std::memcpy(&d, &bits, 8);
            std::snprintf(buf, sizeof buf, (spec + c).c_str(), d);
            break;
        }
        case 'n': word(); break;
        default: out += spec + c; break;
        }
        out += buf;
        i = j;
    }
    return out;
}

} // namespace reluva::x86
