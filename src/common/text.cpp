// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/text.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>

namespace reluva::text {

static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xc0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += char(0xe0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3f));
        out += char(0x80 | (cp & 0x3f));
    } else {
        out += char(0xf0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3f));
        out += char(0x80 | ((cp >> 6) & 0x3f));
        out += char(0x80 | (cp & 0x3f));
    }
}

std::string utf16leToUtf8(const uint16_t* units, size_t count) {
    std::string out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        uint32_t u = units[i];
        if (u >= 0xd800 && u < 0xdc00 && i + 1 < count && units[i + 1] >= 0xdc00 && units[i + 1] < 0xe000) {
            appendUtf8(out, 0x10000 + ((u - 0xd800) << 10) + (units[i + 1] - 0xdc00));
            ++i;
        } else if (u >= 0xd800 && u < 0xe000) {
            appendUtf8(out, 0xfffd);
        } else {
            appendUtf8(out, u);
        }
    }
    return out;
}

uint32_t nextUtf8(std::string_view s, size_t& i) {
    const auto b = [&](size_t k) { return uint8_t(s[k]); };
    const uint8_t c = b(i++);
    int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (c >= 0x80 && extra == 0)
        return 0xfffd;
    uint32_t cp = extra == 3 ? c & 0x07u : extra == 2 ? c & 0x0fu : extra == 1 ? c & 0x1fu : c;
    for (; extra > 0; --extra) {
        if (i >= s.size() || (b(i) & 0xc0) != 0x80)
            return 0xfffd;
        cp = cp << 6 | (b(i++) & 0x3fu);
    }
    return cp;
}

uint32_t toLatin1(uint32_t cp) {
    switch (cp) {
    case '\t': return ' ';
    case 0x2018: case 0x2019: return '\'';
    case 0x201c: case 0x201d: return '"';
    case 0x2013: case 0x2014: return '-';
    default: return cp <= 0xff ? cp : '?';
    }
}

std::string toUpper(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        b++;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        e--;
    return std::string(s.substr(b, e - b));
}

std::string format(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int len = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out;
    if (len > 0) {
        out.resize(size_t(len) + 1);
        std::vsnprintf(out.data(), out.size(), fmt, args);
        out.resize(size_t(len));
    }
    va_end(args);
    return out;
}

} // namespace reluva::text
