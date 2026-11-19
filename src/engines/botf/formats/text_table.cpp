// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/text_table.h"

#include "common/text.h"

#include <string_view>

namespace reluva::botf {

namespace {

constexpr size_t kHeaderSize = 16;
constexpr size_t kRecordSize = 16;

uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24;
}

} // namespace

Error TextTable::load(const std::vector<uint8_t> &data) {
    _entries.clear();
    if (data.size() < kHeaderSize)
        return Error::fail("too small for a text table");
    const uint32_t count = u32(data.data());
    _longest = u32(data.data() + 8);
    if (count > (data.size() - kHeaderSize) / kRecordSize)
        return Error::fail(std::to_string(count) + " records don't fit in " + std::to_string(data.size()) + " bytes");
    const size_t base = kHeaderSize + size_t(count) * kRecordSize;
    const size_t textSize = data.size() - base;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *r = data.data() + kHeaderSize + size_t(i) * kRecordSize;
        const uint32_t key = u32(r);
        const uint32_t length = u32(r + 4);
        const uint32_t offset = u32(r + 12);
        if (length == 0 || offset > textSize || length > textSize - offset)
            return Error::fail("string " + std::to_string(i) + " (" + std::to_string(length) + " bytes at " +
                               std::to_string(offset) + ") isn't inside the text");
        const char *s = reinterpret_cast<const char *>(data.data() + base + offset);
        if (s[length - 1] != 0)
            return Error::fail("string " + std::to_string(i) + " doesn't end in a zero byte");
        _entries.push_back({key, text::windows1252ToUtf8(std::string_view(s, length - 1))});
    }
    return Error::success();
}

const TextTable::Entry *TextTable::find(uint32_t key) const {
    for (const Entry &e : _entries)
        if (e.key == key)
            return &e;
    return nullptr;
}

std::string displayText(const std::string &text) {
    size_t a = 0;
    size_t b = text.size();
    if (a < b && text[a] == ' ')
        ++a;
    while (b > a && (text[b - 1] == '\n' || text[b - 1] == '\r'))
        --b;
    return text.substr(a, b - a);
}

} // namespace reluva::botf
