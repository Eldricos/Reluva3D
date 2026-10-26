// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/refpack.h"

namespace reluva::nfs {

bool isRefPack(const std::vector<uint8_t>& data) {
    return data.size() >= 5 && (data[0] & 0x7e) == 0x10 && data[1] == 0xfb;
}

Error decompressRefPack(const std::vector<uint8_t>& data, std::vector<uint8_t>& out) {
    out.clear();
    if (!isRefPack(data))
        return Error::fail("not RefPack data");
    // Flags: 0x80 sizes are 4 bytes (else 3), 0x01 the compressed size
    // comes before the uncompressed one. Sizes are big-endian.
    const int sizeBytes = data[0] & 0x80 ? 4 : 3;
    size_t pos = 2 + (data[0] & 0x01 ? sizeBytes : 0);
    if (pos + sizeBytes > data.size())
        return Error::fail("RefPack header cut short");
    uint32_t size = 0;
    for (int i = 0; i < sizeBytes; ++i)
        size = size << 8 | data[pos++];
    out.reserve(size);

    auto damaged = [&](const char* why) {
        out.clear();
        return Error::fail(std::string("RefPack data damaged: ") + why);
    };
    // Each command copies `literal` bytes from the input, then `length`
    // bytes from `distance` back in the output (which may overlap).
    while (true) {
        if (pos >= data.size())
            return damaged("no end code");
        const uint8_t b0 = data[pos];
        uint32_t literal = 0, length = 0, distance = 0;
        bool end = false;
        if (b0 < 0x80) {
            if (pos + 2 > data.size())
                return damaged("cut short");
            const uint8_t b1 = data[pos + 1];
            pos += 2;
            literal = b0 & 0x03;
            length = ((b0 & 0x1c) >> 2) + 3;
            distance = ((b0 & 0x60) << 3) + b1 + 1;
        } else if (b0 < 0xc0) {
            if (pos + 3 > data.size())
                return damaged("cut short");
            const uint8_t b1 = data[pos + 1], b2 = data[pos + 2];
            pos += 3;
            literal = b1 >> 6;
            length = (b0 & 0x3f) + 4;
            distance = ((b1 & 0x3f) << 8) + b2 + 1;
        } else if (b0 < 0xe0) {
            if (pos + 4 > data.size())
                return damaged("cut short");
            const uint8_t b1 = data[pos + 1], b2 = data[pos + 2], b3 = data[pos + 3];
            pos += 4;
            literal = b0 & 0x03;
            length = ((b0 & 0x0c) << 6) + b3 + 5;
            distance = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1;
        } else if (b0 < 0xfc) {
            pos += 1;
            literal = ((b0 & 0x1f) << 2) + 4;
        } else {
            pos += 1;
            literal = b0 & 0x03;
            end = true;
        }

        if (pos + literal > data.size())
            return damaged("literal runs past the end");
        if (out.size() + literal + length > size)
            return damaged("more data than its header gives");
        out.insert(out.end(), data.begin() + pos, data.begin() + pos + literal);
        pos += literal;
        if (end)
            break;
        if (distance > out.size())
            return damaged("copy from before the start");
        for (uint32_t i = 0; i < length; ++i)
            out.push_back(out[out.size() - distance]);
    }
    if (out.size() != size)
        return damaged("less data than its header gives");
    return Error::success();
}

Error unpackIfRefPack(std::vector<uint8_t>& data) {
    if (!isRefPack(data))
        return Error::success();
    std::vector<uint8_t> out;
    if (Error err = decompressRefPack(data, out))
        return err;
    data = std::move(out);
    return Error::success();
}

} // namespace reluva::nfs
