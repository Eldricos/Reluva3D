// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's four worlds' files', worked out from them:
// docs/engines/tpw/games/simcoaster/formats/omp.md.

#include "engines/tpw/formats/omp.h"

#include <cstring>

namespace reluva::tpw {

namespace {

constexpr size_t kRecordSize = 60;
// Positions, radii and the extra numbers are thousandths of a unit.
constexpr float kUnit = 1000;

uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}
float thousandths(const std::vector<uint8_t> &d, size_t at) {
    return float(int32_t(le32(d, at))) / kUnit;
}

} // namespace

Error ObjectMap::parse(const std::vector<uint8_t> &data) {
    *this = {};
    if (data.size() < 12 || std::memcmp(data.data(), "OBJ_", 4) != 0)
        return Error::fail("not a world's objects (OBJ_)");
    const size_t count = le32(data, 4);
    const size_t size = le32(data, 8);
    if (size < kRecordSize || count > (data.size() - 12) / size)
        return Error::fail("the objects run past the end");
    for (size_t i = 0; i < count; ++i) {
        const size_t at = 12 + i * size;
        Object o;
        o.kind = le32(data, at);
        o.variant = le32(data, at + 4);
        o.number = le32(data, at + 8);
        o.position = {thousandths(data, at + 12), thousandths(data, at + 16), thousandths(data, at + 20)};
        o.radius = thousandths(data, at + 24);
        o.extra[0] = thousandths(data, at + 28);
        o.extra[1] = thousandths(data, at + 32);
        _objects.push_back(o);
    }
    // Then an INCL chunk: a count, and for each name its length (with its
    // ending zero) and the name.
    size_t at = 12 + count * size;
    if (at + 8 <= data.size() && std::memcmp(&data[at], "INCL", 4) == 0) {
        const size_t names = le32(data, at + 4);
        at += 8;
        for (size_t i = 0; i < names; ++i) {
            if (at + 4 > data.size())
                return Error::fail("the objects' names run past the end");
            const size_t length = le32(data, at);
            if (length > data.size() - at - 4)
                return Error::fail("the objects' names run past the end");
            std::string name(reinterpret_cast<const char *>(&data[at + 4]), length);
            name.resize(std::strlen(name.c_str()));
            _includes.push_back(std::move(name));
            at += 4 + length;
        }
    }
    return Error::success();
}

} // namespace reluva::tpw
