// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/refpack.h"

#include <cstdio>
#include <string>

namespace reluva::nfs {

bool isRefPack(const std::vector<uint8_t>& data) {
    return data.size() >= 5 && (data[0] & 0x7e) == 0x10 && data[1] == 0xfb;
}

bool isOtherEaCompression(const std::vector<uint8_t>& data) {
    return data.size() >= 2 && data[1] == 0xfb && !isRefPack(data) && !isEaHuffman(data);
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

bool isEaHuffman(const std::vector<uint8_t>& data) {
    return data.size() >= 5 && data[1] == 0xfb && (data[0] == 0x30 || data[0] == 0x32 || data[0] == 0x34 ||
                                                   data[0] == 0x31 || data[0] == 0x33 || data[0] == 0x35);
}

// Worked out from nfs3.exe's own routine (0x50ed50) and checked against it
// on every file so compressed (reluva-tool nfs-huff-check): refpack.md.
Error decompressEaHuffman(const std::vector<uint8_t>& data, std::vector<uint8_t>& out) {
    out.clear();
    auto damaged = [](const char* why) { return Error::fail(std::string("EA Huffman data is damaged: ") + why); };
    // Bits most significant first, past the end reading as 0.
    size_t bit = 0;
    const size_t bits = data.size() * 8;
    auto read = [&](int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i, ++bit)
            v = v << 1 | (bit < bits ? (data[bit >> 3] >> (7 - (bit & 7))) & 1u : 0u);
        return v;
    };
    // A number: z zero bits, a one, then z + 2 bits v; v + 2^(z+2) - 4.
    auto number = [&]() -> uint32_t {
        int zeros = 0;
        while (bit < bits && !read(1))
            ++zeros;
        if (zeros > 28)
            return 0;
        const int length = zeros + 2;
        return read(length) + (1u << length) - 4;
    };

    const uint32_t type = read(16);
    const uint32_t size = read(24);
    if (type & 0x100)
        read(24); // a field the game skips
    const uint32_t escape = read(8);

    // The code: how many codes of each length, from 1 bit up, until the
    // code space is full.
    std::vector<uint32_t> count(1), firstCode(1), firstIndex(1);
    uint32_t code = 0, total = 0;
    for (int length = 1;; ++length) {
        if (length > 24)
            return damaged("a code longer than 24 bits");
        code <<= 1;
        firstCode.push_back(code);
        firstIndex.push_back(total);
        const uint32_t n = number();
        count.push_back(n);
        code += n;
        total += n;
        if (n && ((code << (16 - std::min(length, 16))) & 0xffff) == 0 && length <= 16)
            break;
        if (n && length > 16 && code == (1u << length))
            break;
    }
    const int maxLength = static_cast<int>(count.size()) - 1;
    if (total == 0 || total > 256)
        return damaged("a code of no symbols, or more than 256");

    // The symbols, in code order: each n + 1 unused byte values after the
    // one before, round from 255 to 0.
    std::vector<uint8_t> symbols;
    bool used[256] = {};
    uint8_t symbol = 0xff;
    for (uint32_t i = 0; i < total; ++i) {
        uint32_t steps = number() + 1;
        int guard = 0;
        while (steps) {
            ++symbol;
            if (!used[symbol])
                --steps;
            if (++guard > 256 * 2)
                return damaged("too few symbols left");
        }
        used[symbol] = true;
        symbols.push_back(symbol);
    }

    // The data: codes (canonical, by length), the escape symbol bringing
    // a count: n > 0 repeats the last byte n times; 0 then a bit 1 ends,
    // a bit 0 gives the next 8 bits as a byte.
    out.reserve(size);
    while (true) {
        if (bit >= bits + 64)
            return damaged("no end");
        uint32_t c = 0, s = 0;
        int length = 0;
        bool found = false;
        while (length < maxLength) {
            c = c << 1 | read(1);
            ++length;
            if (c - firstCode[size_t(length)] < count[size_t(length)]) {
                s = symbols[firstIndex[size_t(length)] + (c - firstCode[size_t(length)])];
                found = true;
                break;
            }
        }
        if (!found)
            return damaged("a code that isn't in the table");
        if (s != escape) {
            out.push_back(static_cast<uint8_t>(s));
            continue;
        }
        const uint32_t n = number();
        if (n) {
            if (out.empty())
                return damaged("a repeat before any byte");
            out.insert(out.end(), n, out.back());
            continue;
        }
        if (read(1))
            break;
        out.push_back(static_cast<uint8_t>(read(8)));
    }
    if (out.size() != size)
        return damaged("not the size its header gives");
    // Stored as differences: once (32 FB) or twice (34 FB).
    const uint32_t kind = type >> 8 & 0xfe;
    if (kind == 0x32 || kind == 0x34) {
        uint8_t sum = 0, sum2 = 0;
        for (uint8_t& b : out) {
            sum = static_cast<uint8_t>(sum + b);
            if (kind == 0x34) {
                sum2 = static_cast<uint8_t>(sum2 + sum);
                b = sum2;
            } else {
                b = sum;
            }
        }
    }
    return Error::success();
}

Error unpackIfRefPack(std::vector<uint8_t>& data) {
    if (isEaHuffman(data)) {
        std::vector<uint8_t> out;
        if (Error err = decompressEaHuffman(data, out))
            return err;
        data = std::move(out);
        return Error::success();
    }
    if (isOtherEaCompression(data)) {
        char header[8];
        std::snprintf(header, sizeof header, "%02X FB", data[0]);
        return Error::fail(std::string("compressed with an EA scheme Reluva doesn't read yet (") + header + ")");
    }
    if (!isRefPack(data))
        return Error::success();
    std::vector<uint8_t> out;
    if (Error err = decompressRefPack(data, out))
        return err;
    data = std::move(out);
    return Error::success();
}

} // namespace reluva::nfs
