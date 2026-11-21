// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/planet_populations.h"

#include "common/bytes.h"

#include <string>

namespace reluva::botf {

namespace {

constexpr size_t kRowSize = 40;
constexpr int kSome = 310;
constexpr int kMuch = 620;

} // namespace

int bonusLevel(int value) {
    return value >= kMuch ? 2 : value >= kSome ? 1 : 0;
}

Error PlanetPopulations::load(const std::vector<uint8_t> &data) {
    _rows.clear();
    if (data.size() % kRowSize != 0)
        return Error::fail("planspac.bin's size isn't rows of 40 bytes");
    for (size_t at = 0; at < data.size(); at += kRowSize) {
        // The key is big-endian: the type in its top byte, the size in the
        // next; then nine values, little-endian.
        const int type = data[at];
        const int size = data[at + 1];
        std::array<int, 9> &row = _rows[{type, size}];
        for (size_t i = 0; i < row.size(); ++i)
            row[i] = int(readLE32(&data[at + 4 + i * 4]));
    }
    return Error::success();
}

int PlanetPopulations::maxPopulation(int type, int size, int growthLevel, int energyLevel) const {
    const auto it = _rows.find({type, size});
    if (it == _rows.end() || growthLevel < 0 || growthLevel > 2 || energyLevel < 0 || energyLevel > 2)
        return 0;
    return it->second[size_t(growthLevel * 3 + energyLevel)];
}

} // namespace reluva::botf
