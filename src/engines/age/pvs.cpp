// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Worked out from the retail files; see docs/formats/age/pvs.md.

#include "engines/age/pvs.h"

#include <cstring>
#include <string>

namespace reluva::age {

namespace {

uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

} // namespace

bool Pvs::sees(size_t from, size_t to) const {
    if (from == 0 || from > rows.size() || to >= blocks)
        return true;
    const std::vector<uint8_t>& row = rows[from - 1];
    const size_t byte = to / 4;
    return byte < row.size() && (row[byte] >> (2 * (to % 4)) & 3) != 0;
}

Error readCpvs(const std::vector<uint8_t>& data, Pvs& out) {
    out = Pvs{};
    if (data.size() < 8 || std::memcmp(data.data(), "PVS0", 4) != 0)
        return Error::fail("not a visibility file (no PVS0)");
    // The PSDL's block count (plus one, as it stores it), then where each
    // block's row starts and where the last ends.
    const uint32_t count = u32(data.data() + 4);
    if (count < 2 || uint64_t(count - 1) * 4 + 8 > data.size())
        return Error::fail("visibility table runs past the end");
    const size_t base = 8 + size_t(count - 1) * 4;
    out.blocks = count - 1;
    for (uint32_t k = 0; k + 1 < count - 1; ++k) {
        const uint64_t from = base + uint64_t(u32(data.data() + 8 + 4 * k));
        const uint64_t to = base + uint64_t(u32(data.data() + 12 + 4 * k));
        if (from > to || to > data.size())
            return Error::fail("visibility row " + std::to_string(k + 1) + " runs past the end");
        // Runs: a byte under 0x80 is that many of the next byte; from 0x80
        // up, 0x7f fewer bytes follow as they are. Rows stop at their last
        // seen block.
        std::vector<uint8_t> row;
        for (uint64_t i = from; i < to;) {
            const uint8_t c = data[i++];
            if (c < 0x80) {
                if (i >= to)
                    return Error::fail("visibility row " + std::to_string(k + 1) + " ends in a run");
                row.insert(row.end(), c, data[i++]);
            } else {
                const size_t n = c - 0x7f;
                if (i + n > to)
                    return Error::fail("visibility row " + std::to_string(k + 1) + " runs past its end");
                row.insert(row.end(), data.begin() + std::ptrdiff_t(i), data.begin() + std::ptrdiff_t(i + n));
                i += n;
            }
        }
        out.rows.push_back(std::move(row));
    }
    return Error::success();
}

} // namespace reluva::age
