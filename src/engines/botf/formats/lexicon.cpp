// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/lexicon.h"

#include "common/text.h"

#include <cstring>
#include <string_view>

namespace reluva::botf {

namespace {

uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24;
}

} // namespace

// A u32: where the table is. The strings, zero-terminated. The table: a u32
// (not known), a u16 count, then per entry a u32 offset and a u32 length
// (counting the zero).
Error Lexicon::load(const std::vector<uint8_t> &data) {
    _entries.clear();
    if (data.size() < 4)
        return Error::fail("too small");
    const size_t table = u32(data.data());
    if (table + 6 > data.size())
        return Error::fail("the table is past the end");
    const size_t count = size_t(data[table + 4] | data[table + 5] << 8);
    if (table + 6 + count * 8 > data.size())
        return Error::fail(std::to_string(count) + " entries don't fit");
    _entries.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *e = data.data() + table + 6 + i * 8;
        const size_t off = u32(e);
        const size_t len = u32(e + 4);
        if (off > data.size() || len > data.size() - off)
            return Error::fail("entry " + std::to_string(i) + " is past the end");
        const char *s = reinterpret_cast<const char *>(data.data() + off);
        _entries.push_back(text::windows1252ToUtf8(std::string_view(s, strnlen(s, len))));
    }
    return Error::success();
}

const std::string &Lexicon::operator[](size_t i) const {
    static const std::string kEmpty;
    return i < _entries.size() ? _entries[i] : kEmpty;
}

} // namespace reluva::botf
