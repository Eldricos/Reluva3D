// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/sprite.h"

#include <algorithm>

#include "engines/stgenerations/debug.h"

namespace reluva::stgenerations {

bool SpriteSet::load(SeekableReadStream &stream) {
    _frames.clear();
    _sequences.clear();

    uint32_t version = stream.readUint32LE();
    uint32_t frameCount = stream.readUint32LE();
    stream.readUint32LE();
    uint32_t sequenceCount = stream.readUint32LE();
    stream.readUint32LE();
    stream.readUint32LE(); // size hint
    if (version != 2) {
        log::warning("SpriteSet: version %u is not supported", version);
        return false;
    }
    if (frameCount > 4096 || sequenceCount > 256) {
        log::warning("SpriteSet: implausible header (%u frames, %u sequences)", frameCount, sequenceCount);
        return false;
    }

    for (uint32_t i = 0; i < sequenceCount; i++) {
        std::vector<uint8_t> sequence(stream.readByte());
        stream.read(sequence.data(), (uint32_t)sequence.size());
        for (uint8_t index : sequence) {
            if (index >= frameCount) {
                log::warning("SpriteSet: sequence %u refers to frame %u of %u", i, index, frameCount);
                return false;
            }
        }
        _sequences.push_back(std::move(sequence));
    }

    std::vector<uint8_t> columns;
    for (uint32_t i = 0; i < frameCount; i++) {
        uint32_t width = stream.readUint32LE();
        uint32_t columnCount = stream.readUint32LE();
        uint32_t height = stream.readUint32LE();
        if (stream.eos() || columnCount == 0 || columnCount > 1024 || height == 0 || height > 1024) {
            log::warning("SpriteSet: bad header for frame %u", i);
            return false;
        }

        columns.resize(columnCount * height);
        if (stream.read(columns.data(), (uint32_t)columns.size()) != columns.size()) {
            log::warning("SpriteSet: frame %u is truncated", i);
            return false;
        }
        // Per-column tables: first and last solid rows, then a class: 0
        // empty, 1 with holes, 2 one run, 3 solid. The original skips empty
        // columns whatever their texels hold, and blank frames such as
        // amargos1/007.gs2's frame 0 rely on it: 2 columns of colour 0.
        std::vector<uint8_t> tables(3 * columnCount);
        if (stream.read(tables.data(), (uint32_t)tables.size()) != tables.size()) {
            log::warning("SpriteSet: frame %u is truncated", i);
            return false;
        }

        Frame frame;
        frame.halfWidth = width / 65536.0;
        frame.surface.create((int16_t)columnCount, (int16_t)height, Canvas::kClut8);
        for (uint32_t x = 0; x < columnCount; x++) {
            const uint8_t *column = &columns[x * height];
            bool empty = tables[2 * columnCount + x] == 0;
            for (uint32_t y = 0; y < height; y++)
                frame.surface.setPixel(x, height - 1 - y, empty ? kTransparent : column[y]);
        }
        _frames.push_back(std::move(frame));
    }
    return true;
}

int SpriteSet::getMaxWidth() const {
    int width = 0;
    for (const Frame &frame : _frames)
        width = std::max<int>(width, frame.surface.w);
    return width;
}

int SpriteSet::getMaxHeight() const {
    int height = 0;
    for (const Frame &frame : _frames)
        height = std::max<int>(height, frame.surface.h);
    return height;
}

} // namespace reluva::stgenerations
