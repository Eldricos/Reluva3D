// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Layout worked out from Midtown Madness 2's cities; see
// docs/formats/age/inst.md.

#include "engines/age/instances.h"

#include <cstring>

namespace reluva::age {

namespace {

constexpr uint8_t kTurnOnly = 0x80; // in the name's length: two floats instead of three axes

float f32(const uint8_t* p) {
    const uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

} // namespace

Error readInstances(const std::vector<uint8_t>& data, std::vector<Instance>& out) {
    out.clear();
    size_t at = 0;
    while (at < data.size()) {
        if (data.size() - at < 5)
            return Error::fail("truncated at byte " + std::to_string(at));
        Instance inst;
        const uint8_t* p = &data[at];
        inst.block = static_cast<uint16_t>(p[0] | p[1] << 8);
        inst.flags = static_cast<uint16_t>(p[2] | p[3] << 8);
        const bool turnOnly = p[4] & kTurnOnly;
        const size_t nameLength = p[4] & 0x7f;
        const size_t floats = turnOnly ? 5 : 12;
        if (data.size() - at - 5 < nameLength + floats * 4)
            return Error::fail("truncated at byte " + std::to_string(at));
        inst.model.assign(reinterpret_cast<const char*>(p + 5), nameLength);
        if (const size_t zero = inst.model.find('\0'); zero != std::string::npos)
            inst.model.resize(zero);
        const uint8_t* f = p + 5 + nameLength;
        if (turnOnly) {
            // Turned about the vertical only: the X axis's x and z, then the
            // position.
            const float x = f32(f), z = f32(f + 4);
            inst.matrix = {x, 0, z, 0, 1, 0, -z, 0, x, f32(f + 8), f32(f + 12), f32(f + 16)};
        } else {
            for (size_t i = 0; i < 12; ++i)
                inst.matrix[i] = f32(f + 4 * i);
        }
        at += 5 + nameLength + floats * 4;
        out.push_back(std::move(inst));
    }
    return Error::success();
}

} // namespace reluva::age
