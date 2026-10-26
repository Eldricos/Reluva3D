// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/text_table.h"

#include <algorithm>

namespace reluva::nfs {

namespace {

uint32_t le32(const uint8_t* p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

// Windows-1252's 0x80-0x9F (0: unassigned), from Microsoft's published
// code page table; the rest of the code page is Latin-1.
constexpr uint16_t kHigh[32] = {
    0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017d, 0,      0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
    0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178,
};

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | cp >> 6);
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
        out += static_cast<char>(0xe0 | cp >> 12);
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }
}

} // namespace

Error parseTextTable(const std::vector<uint8_t>& data, TextTable& out) {
    out = {};
    // A table of u32 offsets, one per string, then the strings, each ended
    // by a 0. The first offset is where the table ends.
    if (data.size() < 4)
        return Error::fail("Not a valid text table: too short");
    const uint32_t tableEnd = le32(data.data());
    if (tableEnd % 4 != 0 || tableEnd == 0 || tableEnd > data.size())
        return Error::fail("Not a valid text table: bad first offset");
    const size_t count = tableEnd / 4;
    out.strings.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t offset = le32(&data[i * 4]);
        if (offset < tableEnd || offset > data.size())
            return Error::fail("Not a valid text table: string " + std::to_string(i) + " is outside the file");
        // The last string may run to the end of the file without its 0.
        const auto begin = data.begin() + offset;
        const auto end = std::find(begin, data.end(), 0);
        out.strings.emplace_back(begin, end);
    }
    return Error::success();
}

std::string cp1252ToUtf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        const auto c = static_cast<uint8_t>(ch);
        if (c >= 0x80 && c < 0xa0)
            appendUtf8(out, kHigh[c - 0x80] ? kHigh[c - 0x80] : 0xfffd);
        else
            appendUtf8(out, c);
    }
    return out;
}

} // namespace reluva::nfs
