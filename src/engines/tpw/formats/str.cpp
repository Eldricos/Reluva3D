// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layouts are SimCoaster's 37 string tables' and its character set's,
// worked out from them: docs/engines/tpw/games/simcoaster/formats/str.md.

#include "engines/tpw/formats/str.h"

namespace reluva::tpw {

namespace {

constexpr uint8_t kEnd = 0;
constexpr uint8_t kText = 1;
constexpr uint8_t kParameter = 2;

uint32_t le16(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8;
}
uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return le16(d, at) | le16(d, at + 2) << 16;
}

void appendUtf8(std::string &out, char32_t c) {
    if (c < 0x80) {
        out.push_back(char(c));
    } else if (c < 0x800) {
        out.push_back(char(0xc0 | c >> 6));
        out.push_back(char(0x80 | (c & 0x3f)));
    } else {
        out.push_back(char(0xe0 | c >> 12));
        out.push_back(char(0x80 | (c >> 6 & 0x3f)));
        out.push_back(char(0x80 | (c & 0x3f)));
    }
}

bool hasMagic(const std::vector<uint8_t> &d, const char *magic) {
    return d.size() >= 4 && d[0] == uint8_t(magic[0]) && d[1] == uint8_t(magic[1]) && d[2] == uint8_t(magic[2]) &&
           d[3] == uint8_t(magic[3]);
}

} // namespace

Error CharacterSet::parse(const std::vector<uint8_t> &data) {
    _unicode.clear();
    if (data.size() < 8 || !hasMagic(data, "BFMU"))
        return Error::fail("not a character set (BFMU)");
    const size_t count = le16(data, 6);
    if (data.size() < 8 + 2 * count)
        return Error::fail("the character set is cut short");
    for (size_t i = 0; i < count; ++i)
        _unicode.push_back(char32_t(le16(data, 8 + 2 * i)));
    return Error::success();
}

std::string CharacterSet::toUtf8(const uint8_t *codes, size_t count) const {
    std::string out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const char32_t c = unicode(codes[i]);
        appendUtf8(out, c ? c : U'�');
    }
    return out;
}

Error StringTable::parse(const std::vector<uint8_t> &data, const CharacterSet &characters) {
    _strings.clear();
    if (data.size() < 12 || !hasMagic(data, "BFST"))
        return Error::fail("not a string table (BFST)");
    _id = le32(data, 4);
    const size_t count = le32(data, 8);
    // Each string's offset counts from the offsets' own start.
    const size_t table = 12;
    if (count > (data.size() - table) / 4)
        return Error::fail("the string table is cut short");
    _strings.resize(count);
    for (size_t i = 0; i < count; ++i) {
        size_t at = table + le32(data, table + 4 * i);
        std::vector<Part> &parts = _strings[i];
        auto fail = [&](const char *why) { return Error::fail("string " + std::to_string(i) + ": " + why); };
        while (true) {
            if (at + 4 > data.size())
                return fail("not ended");
            const uint8_t kind = data[at];
            if (kind == kEnd)
                break;
            Part part;
            if (kind == kText) {
                const size_t length = le16(data, at + 1);
                if (at + 4 + length > data.size())
                    return fail("its text runs past the end");
                part.text = characters.toUtf8(data.data() + at + 4, length);
                at += (4 + length + 3) / 4 * 4;
            } else if (kind == kParameter) {
                part.parameter = true;
                part.number = data[at + 1];
                at += 4;
            } else {
                return fail("a part of a kind not known");
            }
            parts.push_back(std::move(part));
        }
    }
    return Error::success();
}

const std::vector<StringTable::Part> &StringTable::parts(size_t index) const {
    static const std::vector<Part> kNone;
    return index < _strings.size() ? _strings[index] : kNone;
}

std::string StringTable::text(size_t index) const {
    std::string out;
    for (const Part &p : parts(index))
        out += p.parameter ? "{" + std::to_string(p.number) + "}" : p.text;
    return out;
}

} // namespace reluva::tpw
