// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/draw.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace reluva::smush {

/**
 * LucasArts' SMUSH video (.san), as Mysteries of the Sith's cut scenes
 * have it (docs/common/video/smush.md): 8-bit pictures of codec 48 with a
 * palette that fades, and 16-bit stereo sound in the frames' IACT chunks.
 * Other codecs, which these files don't use, leave the picture as it was.
 */
class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(Decoder &&) noexcept;
    Decoder &operator=(Decoder &&) noexcept;

    /** Reads the file's header and finds its frames; false if it isn't SMUSH. */
    bool load(std::vector<uint8_t> data);

    /** The picture's size: the first frame object's. */
    int width() const { return _width; }
    int height() const { return _height; }
    int frameCount() const { return int(_frames.size()); }
    /** The frame last decoded; -1 before the first. */
    int currentFrame() const { return _frame; }
    uint32_t frameMicros() const { return _frameMicros; }
    bool endOfVideo() const { return _frame + 1 >= frameCount(); }

    /** Decodes the next frame; false at the end, or if its picture doesn't decode. */
    bool decodeNextFrame();
    /** The picture, palette indexes. */
    const Surface &surface() const { return _surface; }
    /** 256 RGB triples, 0 to 255. */
    const uint8_t *palette() const { return _palette.data(); }

    bool hasAudio() const { return _audioRate > 0; }
    int audioRate() const { return _audioRate; }
    bool audioStereo() const { return true; }
    /** The sound decoded since the last call: 16-bit stereo samples. */
    std::vector<int16_t> takeAudio();

    /** The subtitle the frame shows (its TRES chunk's string number), or -1. */
    int subtitle() const { return _subtitle; }

private:
    struct Codec48;

    void frameObject(const uint8_t *data, size_t size);
    void palette(const uint8_t *data, size_t size);
    void audio(const uint8_t *data, size_t size);

    std::vector<uint8_t> _data;
    std::vector<size_t> _frames; // each FRME's contents, from its offset
    std::vector<uint32_t> _frameSizes;
    int _frame = -1;
    int _width = 0;
    int _height = 0;
    uint32_t _frameMicros = 83333;
    int _audioRate = 0;
    Surface _surface;
    std::array<uint8_t, 768> _palette{};
    // The palette's fade: each channel times 128, and its step a frame.
    std::array<int16_t, 768> _fade{};
    std::array<int16_t, 768> _fadeStep{};
    std::vector<uint8_t> _stored; // the frame object STOR keeps, for FTCH
    bool _storeNext = false;
    std::vector<uint8_t> _packet; // sound, as IACT chunks give it, until a packet is whole
    std::vector<int16_t> _audio;
    int _subtitle = -1;
    std::unique_ptr<Codec48> _codec48;
};

} // namespace reluva::smush
