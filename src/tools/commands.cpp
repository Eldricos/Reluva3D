// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's shared commands for looking at any file's bytes.

#include "tools/tool.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

using namespace reluva::tool;

// A payload counts as a container if it splits exactly into chunks whose
// tags look plausible. Small tag limit keeps random data from qualifying.
bool tilesIntoChunks(const Bytes& d, size_t begin, size_t end) {
    if (end - begin < 6)
        return false;
    size_t p = begin;
    while (p < end) {
        if (p + 6 > end)
            return false;
        const uint16_t tag = rd16(d, p);
        const uint32_t size = rd32(d, p + 2);
        if (tag > 0x400 || size > end - p - 6)
            return false;
        p += 6 + size;
    }
    return p == end;
}

std::string describeWords(const Bytes& d, size_t p, size_t end, int count) {
    std::string out;
    char buf[64];
    for (int i = 0; i < count && p + 4 <= end; ++i, p += 4) {
        const uint32_t v = rd32(d, p);
        float f;
        std::memcpy(&f, &v, 4);
        const bool plausibleFloat = (v & 0x7f800000) != 0 && (v & 0x7f800000) != 0x7f800000 && f > -1e6f &&
                                    f < 1e6f && (f > 1e-4f || f < -1e-4f);
        if (plausibleFloat)
            std::snprintf(buf, sizeof buf, " %gf", double(f));
        else
            std::snprintf(buf, sizeof buf, " %u", v);
        out += buf;
    }
    return out;
}

void printChunks(const Bytes& d, size_t begin, size_t end, int depth) {
    size_t p = begin;
    while (p < end) {
        const uint16_t tag = rd16(d, p);
        const uint32_t size = rd32(d, p + 2);
        const size_t payload = p + 6, payloadEnd = payload + size;
        const bool container = tilesIntoChunks(d, payload, payloadEnd);
        std::printf("%*s%06zx tag 0x%04x size %u%s", depth * 2, "", p, tag, size, container ? "  {" : "");
        if (!container)
            std::printf("  :%s%s", describeWords(d, payload, payloadEnd, 6).c_str(), size > 24 ? " ..." : "");
        std::printf("\n");
        if (container)
            printChunks(d, payload, payloadEnd, depth + 1);
        p = payloadEnd;
    }
}

int cmdHexdump(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool hexdump <file> [offset] [length]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    long long off = argc > 3 ? parseNumber(argv[3]) : 0;
    if (off < 0)
        off += static_cast<long long>(d.size());
    size_t len = argc > 4 ? size_t(parseNumber(argv[4])) : 256;
    hexdump(d, size_t(std::max(0LL, off)), len);
    return 0;
}

int cmdChunks(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool chunks <file>\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    std::printf("%s: %zu bytes\n", argv[2], d.size());
    if (!tilesIntoChunks(d, 0, d.size())) {
        std::printf("doesn't divide into chunks from the start; first words:%s\n",
                    describeWords(d, 0, d.size(), 12).c_str());
        // Maybe there's a header: report the first offset from which it does.
        for (size_t start = 1; start < std::min<size_t>(d.size(), 64); ++start)
            if (tilesIntoChunks(d, start, d.size())) {
                std::printf("but it does from offset %zu:\n", start);
                printChunks(d, start, d.size(), 1);
                return 0;
            }
        return 1;
    }
    printChunks(d, 0, d.size(), 0);
    return 0;
}

int cmdWords(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool words <file> [offset] [count]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    size_t off = argc > 3 ? size_t(parseNumber(argv[3])) : 0;
    const size_t count = argc > 4 ? size_t(parseNumber(argv[4])) : 64;
    std::printf("%8s  %8s  %11s  %14s  %s\n", "offset", "hex", "u32", "float", "u16 pair");
    for (size_t i = 0; i < count && off + 4 <= d.size(); ++i, off += 4) {
        const uint32_t v = rd32(d, off);
        float f;
        std::memcpy(&f, &v, 4);
        std::printf("%8zx  %08x  %11u  %14g  %u, %u\n", off, v, v, double(f), v & 0xffff, v >> 16);
    }
    return 0;
}

int cmdTable(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool table <file> [values per row] [offset]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    const size_t perRow = argc > 3 ? std::max<size_t>(1, size_t(parseNumber(argv[3]))) : 8;
    size_t off = argc > 4 ? size_t(parseNumber(argv[4])) : 0;
    for (; off + 4 <= d.size(); off += perRow * 4) {
        std::printf("%6zx:", off);
        for (size_t i = 0; i < perRow && off + i * 4 + 4 <= d.size(); ++i) {
            const uint32_t v = rd32(d, off + i * 4);
            float f;
            std::memcpy(&f, &v, 4);
            const bool isFloat = (v & 0x7f800000) != 0 && (v & 0x7f800000) != 0x7f800000 && f > -1e7f && f < 1e7f &&
                                 (f > 1e-6f || f < -1e-6f);
            if (isFloat)
                std::printf(" %10.4gf", double(f));
            else
                std::printf(" %11u", v);
        }
        std::printf("\n");
    }
    if (d.size() % 4)
        std::printf("(%zu trailing bytes)\n", d.size() % 4);
    return 0;
}

const Commands commands("", {
    {"hexdump", "<file> [offset] [length]",
     "show a file's bytes as hex and text; the offset may be hex (0x..) or negative (from the end)",
     cmdHexdump},
    {"chunks", "<file>",
     "try to read any file as a (u16 tag, u32 size) chunk tree",
     cmdChunks},
    {"words", "<file> [offset] [count]",
     "show 4-byte values as hex, integer and float",
     cmdWords},
    {"table", "<file> [values per row] [offset]",
     "show 4-byte values compactly, N per row",
     cmdTable},
});

} // namespace
