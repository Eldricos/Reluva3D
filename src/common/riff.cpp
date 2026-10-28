// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/riff.h"

#include <algorithm>

namespace reluva {

namespace {

std::string fourcc(const uint8_t* p) { return std::string(reinterpret_cast<const char*>(p), 4); }

uint32_t read32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }

Error readChildren(const uint8_t* data, size_t size, RiffChunk& parent, int depth) {
    if (depth > 32)
        return Error::fail("RIFF chunks nested too deep");
    size_t at = 0;
    while (at + 8 <= size) {
        RiffChunk c;
        c.id = fourcc(data + at);
        const size_t length = read32(data + at + 4);
        if (length > size - at - 8)
            return Error::fail("chunk '" + c.id + "' runs past its parent");
        c.data = data + at + 8;
        c.size = length;
        if (c.isList()) {
            if (length < 4)
                return Error::fail("list '" + c.id + "' without a type");
            c.type = fourcc(c.data);
            c.data += 4;
            c.size -= 4;
            if (Error err = readChildren(c.data, c.size, c, depth + 1))
                return err;
        }
        parent.children.push_back(std::move(c));
        at += 8 + length + (length & 1);
    }
    return Error::success();
}

} // namespace

const RiffChunk* RiffChunk::find(const std::string& want) const {
    for (const RiffChunk& c : children)
        if (c.id == want)
            return &c;
    return nullptr;
}

const RiffChunk* RiffChunk::findList(const std::string& want) const {
    for (const RiffChunk& c : children)
        if (c.isList() && c.type == want)
            return &c;
    return nullptr;
}

std::vector<const RiffChunk*> RiffChunk::lists(const std::string& want) const {
    std::vector<const RiffChunk*> out;
    for (const RiffChunk& c : children)
        if (c.isList() && c.type == want)
            out.push_back(&c);
    return out;
}

std::string RiffChunk::text16() const {
    std::string out;
    for (size_t i = 0; i + 1 < size; i += 2) {
        const uint16_t ch = u16(i);
        if (!ch)
            break;
        out += ch < 0x80 ? char(ch) : '?';
    }
    return out;
}

Error readRiff(const std::vector<uint8_t>& data, RiffChunk& out) {
    out = RiffChunk{};
    if (data.size() < 12 || fourcc(data.data()) != "RIFF")
        return Error::fail("not a RIFF file");
    RiffChunk top;
    if (Error err =
            readChildren(data.data(), std::min<size_t>(data.size(), 8 + size_t(read32(data.data() + 4))), top, 0))
        return err;
    if (top.children.empty())
        return Error::fail("not a RIFF file");
    out = std::move(top.children.front());
    return Error::success();
}

} // namespace reluva
