// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/formats/can.h"

namespace reluva::nfs::nfs2se {

namespace {

uint16_t le16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}
int32_t s32(const uint8_t *p) {
    return static_cast<int32_t>(p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
                                static_cast<uint32_t>(p[3]) << 24);
}

constexpr size_t kHeaderSize = 8;
constexpr size_t kKeySize = 20;

} // namespace

Error parseAnimationScript(const std::vector<uint8_t> &data, AnimationScript &out) {
    out = {};
    auto bad = [](const std::string &why) { return Error::fail("Not a valid .can file: " + why); };
    if (data.size() < kHeaderSize)
        return bad("too short");
    const uint8_t *d = data.data();
    const uint16_t count = le16(d + 4);
    if (le16(d) != data.size() || data.size() != kHeaderSize + kKeySize * count)
        return bad("its size isn't its header's and its keys'");
    out.unknown = le16(d + 2);
    out.ticksPerKey = le16(d + 6);
    for (uint16_t k = 0; k < count; ++k) {
        const uint8_t *p = d + kHeaderSize + kKeySize * k;
        AnimationScript::Key key;
        for (int c = 0; c < 3; ++c)
            key.offset[size_t(c)] = static_cast<float>(s32(p + 4 * c)) / 65536.0f;
        for (int c = 0; c < 4; ++c)
            key.rotation[size_t(c)] = static_cast<int16_t>(le16(p + 12 + 2 * c));
        out.keys.push_back(key);
    }
    return Error::success();
}

} // namespace reluva::nfs::nfs2se
