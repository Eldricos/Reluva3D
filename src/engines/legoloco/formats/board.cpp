// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoloco/formats/board.h"

#include "common/bytes.h"

#include <algorithm>

namespace reluva::legoloco {

namespace {

// The sizes the game reads them in (0x447ba0, 0x447db0, 0x447df0).
constexpr size_t kHeaderSize = 0x114;
constexpr size_t kPieceSize = 0x80;
constexpr size_t kTrainSize = 0x2c;
constexpr size_t kBackdropOffset = 0xe;
constexpr size_t kPieceNameOffset = 0x10;
constexpr size_t kMinifigsOffset = 0x1c;
constexpr size_t kMinifigSize = 0x14;
constexpr size_t kTrainNameOffset = 0x20;
constexpr size_t kNameSize = 12;

// A name in a fixed field, up to its first zero.
std::string fixedString(const uint8_t *p, size_t size) {
    return std::string(reinterpret_cast<const char *>(p), size_t(std::find(p, p + size, uint8_t(0)) - p));
}

} // namespace

Error parseBoard(std::span<const uint8_t> data, Board &out) {
    out = Board();
    if (data.size() < kHeaderSize)
        return Error::fail("too short for a board's header");
    const uint8_t *h = data.data();
    out.version = readLE16(h);
    out.columns = readLE16(h + 2);
    out.rows = readLE16(h + 4);
    out.declaredPieces = readLE32(h + 8);
    const uint16_t trains = readLE16(h + 12);
    out.backdrop = fixedString(h + kBackdropOffset, kHeaderSize - kBackdropOffset);

    const size_t squares = size_t(out.columns) * out.rows;
    if (data.size() - kHeaderSize < squares)
        return Error::fail("ends inside its " + std::to_string(out.columns) + " by " + std::to_string(out.rows) +
                           " squares");
    out.squares.assign(data.begin() + kHeaderSize, data.begin() + std::ptrdiff_t(kHeaderSize + squares));

    size_t p = kHeaderSize + squares;
    for (uint32_t i = 0; i < out.declaredPieces && data.size() - p >= kPieceSize; ++i, p += kPieceSize) {
        const uint8_t *r = data.data() + p;
        BoardPiece piece;
        piece.resource = readLE16(r);
        piece.column = readLE16(r + 2);
        piece.row = readLE16(r + 4);
        piece.value8 = readLE32(r + 8);
        piece.value12 = readLE32(r + 12);
        piece.name = fixedString(r + kPieceNameOffset, kNameSize);
        for (size_t m = 0; m < piece.minifigs.size(); ++m) {
            const uint8_t *slot = r + kMinifigsOffset + m * kMinifigSize;
            piece.minifigs[m].resource = readLE16(slot);
            piece.minifigs[m].value = readLE32(slot + 4);
            piece.minifigs[m].name = fixedString(slot + 8, kNameSize);
        }
        out.pieces.push_back(std::move(piece));
    }
    // The trains follow the pieces the header counts, which a short file doesn't have.
    if (out.pieces.size() < out.declaredPieces)
        return Error::success();
    for (uint16_t i = 0; i < trains && data.size() - p >= kTrainSize; ++i, p += kTrainSize) {
        BoardTrain train;
        std::copy_n(data.data() + p, train.fields.size(), train.fields.begin());
        train.name = fixedString(data.data() + p + kTrainNameOffset, kNameSize);
        out.trains.push_back(std::move(train));
    }
    return Error::success();
}

} // namespace reluva::legoloco
