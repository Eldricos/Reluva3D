// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::bink {

/**
 * A Bink movie (RAD Game Tools' video format, .bik; docs/common/video/bink.md),
 * revisions f to i: its frames decoded one after another to YUV 4:2:0
 * planes (RGBA on request), with its first audio track's samples. Elite
 * Force's cinematics and Murder on the Orient Express's movies and video
 * loops (all revision i) use it.
 */
class Movie {
public:
    /**
     * Reads the header and frame index (the file is kept).
     * @return An error if it isn't Bink, is another revision, or its header
     *         is damaged or cut short.
     */
    Error open(std::vector<uint8_t> file);

    /** The picture's size in pixels, and how many frames there are. */
    ///@{
    int width() const { return _width; }
    int height() const { return _height; }
    uint32_t frameCount() const { return uint32_t(_frames.size()); }
    ///@}
    /** Frames a second, as a fraction. */
    ///@{
    uint32_t rateNumerator() const { return _rateNum; }
    uint32_t rateDenominator() const { return _rateDen; }
    ///@}
    /** The format's revision, 'f' to 'i'. */
    char revision() const { return _revision; }

    /** The header's flags (bit 20: an alpha plane, bit 17: grey). */
    uint32_t flags() const { return _flags; }
    /** Whether it has an alpha plane. */
    bool hasAlpha() const { return _alpha; }
    /** A frame as stored: its size in the file and whether it's a keyframe. */
    struct FrameInfo {
        uint32_t size = 0;
        bool key = false;
    };
    /** Frame `i` as stored; all zero past the last. */
    FrameInfo frameInfo(uint32_t i) const {
        return i < _frames.size() ? FrameInfo{_frames[i].size, _frames[i].key} : FrameInfo{};
    }
    /** The largest frame's size in the file, in bytes. */
    uint32_t largestFrame() const;

    /** The first audio track's rate and channels: 0 if there's none. */
    ///@{
    int audioRate() const { return _audio.empty() ? 0 : _audio[0].rate; }
    int audioChannels() const { return _audio.empty() ? 0 : _audio[0].outChannels; }
    ///@}
    /** An audio track, as audioTracks() gives it. */
    struct Track {
        int rate = 0;
        int channels = 0;
        bool dct = false; // the DCT form, else the RDFT one
    };
    /** Every audio track (only the first is decoded). */
    std::vector<Track> audioTracks() const;

    /**
     * Decodes the next frame: its picture into the planes and its sound (the
     * first track's, interleaved) appended to `sound` if given.
     * @param more Set false, with nothing decoded, past the last frame.
     * @return An error if the frame runs past the file or its audio past it.
     */
    Error nextFrame(bool &more, std::vector<int16_t> *sound = nullptr);
    /** The last frame decoded (-1 before the first). */
    int frameNumber() const { return _next - 1; }
    /**
     * Back to the first frame, which the next nextFrame() decodes again,
     * from pictures as they were when the movie was opened: for a movie
     * that loops.
     */
    void rewind();
    /**
     * The first audio track whole (interleaved), read ahead of the frames
     * (which then skip it).
     */
    Error decodeAllAudio(std::vector<int16_t> &sound);

    /**
     * The last frame decoded's planes: Y full size, U and V half (alpha 3);
     * their pitches.
     */
    ///@{
    const std::vector<uint8_t> &decodedPlane(int i) const { return _old[size_t(i)]; }
    int pitch(int i) const { return i == 0 || i == 3 ? _lumaPitch : _chromaPitch; }
    ///@}
    /**
     * How toRgba converts. Rounded: each pixel in fixed point, rounded
     * once. Binkw32: as binkw32.dll's MMX path converts (each term
     * truncated on its own; the AWE engine's movies were checked against it
     * frame by frame).
     */
    enum class Rgb { kRounded, kBinkw32 };
    /**
     * The last frame decoded as RGBA, width x height, rows top first
     * (BT.601, studio range), its alpha the alpha plane's if it has one,
     * else 255.
     */
    void toRgba(std::vector<uint8_t> &out, Rgb how = Rgb::kRounded) const;

    // Internals, public for the decoder's helpers.

    /** A bit reader: least significant bit first, from little-endian bytes. */
    struct Bits {
        const uint8_t *data = nullptr;
        size_t size = 0; // in bits
        size_t pos = 0;  // in bits
        /** The next `n` bits (up to 32), the first read lowest; 0s past the end. */
        uint32_t get(int n);
        /** The next bit. */
        uint32_t bit() { return get(1); }
        /** Skips `n` bits. */
        void skip(size_t n) { pos += n; }
        /** On to the next 32-bit boundary. */
        void align32() { pos = (pos + 31) & ~size_t(31); }
        /** Whether every bit has been read. */
        bool done() const { return pos >= size; }
    };
    /** A code tree chosen (0-15) and the symbols it gives, reordered. */
    struct Tree {
        int index = 0;
        std::array<uint8_t, 16> symbols{};
    };

private:
    struct Frame {
        uint32_t offset = 0;
        uint32_t size = 0;
        bool key = false;
    };
    struct Audio {
        int rate = 0;
        int outChannels = 1;
        int channels = 1;
        bool dct = false;
        int frameLength = 0;
        int overlap = 0;
        int blockSize = 0;
        int bandCount = 0;
        float root = 0;
        std::vector<int> bands;
        bool first = true;
        std::vector<float> coeffs[2];
        std::vector<int16_t> previous;
    };
    // A bundle: one kind of value for the blocks, decoded a row at a time.
    struct Bundle {
        std::vector<int16_t> data;
        size_t decoded = 0;    // a position in data
        size_t read = 0;       // a position in data
        bool finished = false; // no more for this plane
        int countBits[2] = {}; // luma, chroma
        int bits = 0;
        Tree tree;
    };
    enum Source {
        kBlockTypes,
        kSubBlockTypes,
        kColors,
        kPattern,
        kXOff,
        kYOff,
        kIntraDC,
        kInterDC,
        kRun,
        kSourceCount
    };

    void clearPictures();
    void decodePlane(Bits &b, int plane, bool chroma);
    void readTree(Bits &b, Tree &t);
    int symbol(Bits &b, const Tree &t);
    uint32_t bundleCount(Bits &b, Bundle &bundle);
    void readBundle(Bits &b, Source s);
    int value(Source s);
    void readDCTCoefficients(Bits &b, int32_t block[64], bool intra);
    void readResidue(Bits &b, int32_t block[64], int masks);
    void decodeAudio(Audio &a, Bits &b, std::vector<int16_t> *sound);
    // An audio packet (its decoded size, then the blocks) into `sound`.
    void decodePacket(Audio &a, const uint8_t *packet, uint32_t length, std::vector<int16_t> &sound);
    void readAudioCoefficients(Audio &a, Bits &b, float *coeffs);

    std::vector<uint8_t> _file;
    char _revision = 0;
    int _width = 0;
    int _height = 0;
    uint32_t _rateNum = 0;
    uint32_t _rateDen = 1;
    uint32_t _flags = 0;
    bool _alpha = false;
    bool _swapChroma = false;
    std::vector<Frame> _frames;
    std::vector<Audio> _audio;
    int _next = 0;
    int _lumaPitch = 0;
    int _chromaPitch = 0;
    int _lumaRows = 0;
    int _chromaRows = 0;
    std::array<std::vector<uint8_t>, 4> _cur;
    std::array<std::vector<uint8_t>, 4> _old;
    std::array<Bundle, kSourceCount> _bundles;
    std::array<Tree, 16> _colorHigh;
    int _lastColorHigh = 0;
};

} // namespace reluva::bink
