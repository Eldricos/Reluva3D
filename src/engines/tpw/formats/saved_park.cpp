// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Worked out from the one save the game ships, its state read through to
// its tagged sections: docs/engines/tpw/games/simcoaster/formats/tpws.md.

#include "engines/tpw/formats/saved_park.h"

#include "common/inflate.h"

#include <algorithm>
#include <cstring>

namespace reluva::tpw {

namespace {

constexpr char kVersion[] = "Version: ";
// The state's chunk: its tag, the state's size inflated, the chunk's size
// from its tag on, four words not worked out, then a zlib stream.
constexpr char kStateTag[] = "BILZ";
constexpr size_t kStateHeader = 28;
constexpr size_t kZlibHeader = 2;
// A cell's record: its own 86 bytes, then −1; some have entries of 10
// bytes before.
constexpr size_t kCellBody = 86;
constexpr size_t kEntry = 10;
constexpr size_t kMostEntries = 16;
constexpr size_t kTileAt = 46;
constexpr size_t kBuiltAt = 13;
constexpr uint8_t kPlainCell = 3;
constexpr uint8_t kCellWithEntry = 7;
// The first tagged section after the grid ("CTAE" backwards), its size
// before it.
constexpr char kAfterGrid[] = "EATC";

uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}

// The grid's cells read from `start`: the body of each, if the records
// run on whole to `end`.
bool walkCells(const std::vector<uint8_t> &s, size_t start, size_t end, std::vector<size_t> &bodies) {
    bodies.clear();
    size_t at = start;
    const size_t cells = size_t(SavedPark::kSide) * SavedPark::kSide;
    while (bodies.size() < cells) {
        size_t entries = 0;
        while (at + kEntry * entries + kCellBody + 2 > end || s[at + kEntry * entries + kCellBody] != 0xFF ||
               s[at + kEntry * entries + kCellBody + 1] != 0xFF) {
            if (++entries > kMostEntries || at + kEntry * entries + kCellBody + 2 > end)
                return false;
        }
        const size_t body = at + kEntry * entries;
        if (s[body] != kPlainCell && s[body] != kCellWithEntry)
            return false;
        bodies.push_back(body);
        at = body + kCellBody + 2;
    }
    return at == end;
}

} // namespace

Error SavedPark::parse(const std::vector<uint8_t> &data) {
    *this = {};
    const auto version = std::search(data.begin(), data.end(), kVersion, kVersion + std::strlen(kVersion));
    if (version != data.end()) {
        auto stop = std::find_if(version + long(std::strlen(kVersion)), data.end(), [](uint8_t c) { return c < ' '; });
        _version.assign(version + long(std::strlen(kVersion)), stop);
    }
    const auto tag = std::search(data.begin(), data.end(), kStateTag, kStateTag + 4);
    if (tag == data.end() || size_t(data.end() - tag) < kStateHeader + kZlibHeader)
        return Error::fail("not a saved park: no state (BILZ)");
    const size_t at = size_t(tag - data.begin());
    const uint32_t size = le32(data, at + 4);
    const uint32_t chunk = le32(data, at + 8);
    if (chunk < kStateHeader + kZlibHeader || chunk > data.size() - at)
        return Error::fail("the saved park's state runs past its end");
    size_t consumed = 0;
    const size_t stream = at + kStateHeader + kZlibHeader;
    if (Error err = inflateStream(data.data() + stream, at + chunk - stream, _state, consumed, size))
        return err;
    if (_state.size() != size)
        return Error::fail("the saved park's state inflates to the wrong size");

    // The grid has no tag of its own: it is the run of whole cells that
    // ends where the first tagged section starts.
    const auto after = std::search(_state.begin(), _state.end(), kAfterGrid, kAfterGrid + 4);
    if (after == _state.end() || after - _state.begin() < 4)
        return Error::fail("no cells in the saved park (no EATC after them)");
    const size_t end = size_t(after - _state.begin()) - 4;
    std::vector<size_t> bodies;
    const size_t smallest = size_t(kSide) * kSide * (kCellBody + 2);
    if (end < smallest)
        return Error::fail("no cells in the saved park");
    bool found = false;
    for (size_t start = 0; start + smallest <= end && !found; ++start)
        found = walkCells(_state, start, end, bodies);
    if (!found)
        return Error::fail("no cells in the saved park");
    _cells.resize(bodies.size());
    for (size_t i = 0; i < bodies.size(); ++i) {
        Cell &c = _cells[i];
        c.kind = _state[bodies[i]];
        c.flags = _state[bodies[i] + 1];
        c.tile = _state[bodies[i] + kTileAt];
        c.built = Built(_state[bodies[i] + kBuiltAt]);
    }
    return Error::success();
}

} // namespace reluva::tpw
