// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/menu_file.h"

#include "common/fs.h"
#include "common/text.h"

#include <cstring>
#include <string_view>

namespace reluva::botf {

namespace {

constexpr size_t kHeader = 8;
constexpr size_t kItem = 88;

int32_t i32(const uint8_t *p) {
    return int32_t(uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24);
}

std::string text(const uint8_t *p, size_t size) {
    const char *s = reinterpret_cast<const char *>(p);
    return text::windows1252ToUtf8(std::string_view(s, strnlen(s, size)));
}

} // namespace

// A u32 (1) and the number of items, then 88 bytes an item: its number, a
// 13-byte picture name ("none" for none), a 31-byte label, and values that
// are the same in every item (leftovers, and 359 twice and a 1).
Error MenuFile::load(const std::vector<uint8_t> &data) {
    _items.clear();
    if (data.size() < kHeader)
        return Error::fail("too small");
    const int32_t count = i32(data.data() + 4);
    if (count < 0 || kHeader + size_t(count) * kItem > data.size())
        return Error::fail(std::to_string(count) + " items don't fit in " + std::to_string(data.size()) + " bytes");
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t *p = data.data() + kHeader + size_t(i) * kItem;
        MenuItem item;
        item.id = i32(p);
        item.picture = fs::toLower(text(p + 4, 13));
        if (item.picture == "none")
            item.picture.clear();
        item.label = text(p + 17, 31);
        _items.push_back(std::move(item));
    }
    return Error::success();
}

} // namespace reluva::botf
