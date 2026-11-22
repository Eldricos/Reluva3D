// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/colormap.h"

#include "common/bytes.h"

#include <cstring>

namespace reluva::sith {

namespace {

constexpr size_t kHeaderSize = 64;
constexpr uint32_t kVersion = 0x1e;
constexpr size_t kLightSize = size_t(Colormap::kLightLevels) * 256;
constexpr size_t kBlendSize = 256 * 256;

} // namespace

Error parseColormap(const std::vector<uint8_t> &data, Colormap &out) {
    out = Colormap{};
    if (data.size() < kHeaderSize || std::memcmp(data.data(), "CMP ", 4) != 0)
        return Error::fail("not a colormap: no CMP header");
    if (readLE32(&data[4]) != kVersion)
        return Error::fail("colormap version " + std::to_string(readLE32(&data[4])) + ", not 30");
    const bool hasBlend = readLE32(&data[8]) != 0;
    const size_t size = kHeaderSize + 256 * 3 + kLightSize + (hasBlend ? kBlendSize : 0);
    if (data.size() < size)
        return Error::fail("colormap cut short: " + std::to_string(data.size()) + " bytes of " + std::to_string(size));

    for (int i = 0; i < 3; ++i) {
        const uint32_t bits = readLE32(&data[12 + 4 * i]);
        std::memcpy(&out.tint[i], &bits, 4);
    }
    const uint8_t *p = &data[kHeaderSize];
    for (Color &c : out.palette) {
        c = {p[0], p[1], p[2]};
        p += 3;
    }
    out.light.assign(p, p + kLightSize);
    p += kLightSize;
    if (hasBlend)
        out.blend.assign(p, p + kBlendSize);
    return Error::success();
}

} // namespace reluva::sith
