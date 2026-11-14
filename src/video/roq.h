// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::roq {

// A RoQ movie (Id Software's and Raven's cinematics, .roq;
// docs/common/video/roq.md): vector-quantised frames, each built from
// codebooks of 2x2 and 4x4 cells and the frame before, with DPCM sound.
// Quake III's, Jedi Outcast's and Jedi Academy's videos use it.
class Movie {
public:
    // Reads the header and the size of the frames (the file is kept).
    Error open(std::vector<uint8_t> file);

    int width() const { return _width; }
    int height() const { return _height; }
    int framesPerSecond() const { return _fps; }
    // The sound: 22,050 samples a second, one or two channels (0: none),
    // as the file's first sound chunk says.
    static constexpr int kAudioRate = 22050;
    int audioChannels() const { return _channels; }

    // The next frame into the planes, the sound before it appended to
    // `sound` (interleaved) if given. False at the end.
    Error nextFrame(bool &more, std::vector<int16_t> *sound = nullptr);
    int frameNumber() const { return _frame - 1; } // the last one decoded
    // Back to the first frame, with pictures as they were when opened.
    void rewind();

    // The last frame's planes, Y, U and V, each full size (a 2x2 cell's one
    // chroma pair spread over its four pixels).
    const std::vector<uint8_t> &plane(int i) const { return _planes[size_t(i)]; }
    // As RGBA, rows top first (full-range BT.601), alpha 255.
    void toRgba(std::vector<uint8_t> &out) const;

private:
    struct Cell2 {
        std::array<uint8_t, 4> y{}; // top left, top right, bottom left, bottom right
        uint8_t u = 128;
        uint8_t v = 128;
    };
    using Cell4 = std::array<uint8_t, 4>; // four 2x2 cells, the same order

    Error decodeFrame(const uint8_t *data, size_t size, uint16_t arg);
    void decodeSound(const uint8_t *data, size_t size, uint16_t arg, bool stereo, std::vector<int16_t> &out);
    void putCell2(int x, int y, const Cell2 &c, int scale);
    void putCell4(int x, int y, const Cell4 &c, int scale);
    void copyFromLast(int x, int y, int size, int dx, int dy);

    std::vector<uint8_t> _file;
    size_t _start = 0; // the first chunk after the header and size
    size_t _pos = 0;
    int _width = 0;
    int _height = 0;
    int _fps = 30;
    int _channels = 0;
    int _frame = 0;
    std::array<Cell2, 256> _cells2{};
    std::array<Cell4, 256> _cells4{};
    std::array<std::vector<uint8_t>, 3> _planes; // the frame being made, then the last one made
    std::array<std::vector<uint8_t>, 3> _last;   // the one before
};

} // namespace reluva::roq
