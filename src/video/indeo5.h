// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::video {

// Intel Indeo Video Interactive 5 (fourcc IV50): a picture in YUV 4:1:0
// (chroma a quarter the size each way), each plane split into bands
// (luma optionally into four wavelet bands), each band into tiles and
// macroblocks of 8x8 or 4x4 blocks: slant-transformed, quantised,
// run-length and Huffman coded, with motion compensation from the frame
// before. See docs/common/video/indeo5.md.
class Indeo5Decoder {
public:
    Indeo5Decoder();
    // Decodes a frame. An empty one (a dropped frame) keeps the picture.
    Error decode(const uint8_t *data, size_t size);
    int width() const { return _planes[0].width; }
    int height() const { return _planes[0].height; }
    // The picture: luma, then the two chroma planes (Cb, Cr), each
    // width × height bytes, 0..255.
    void yuv(std::vector<uint8_t> &y, std::vector<uint8_t> &cb, std::vector<uint8_t> &cr) const;
    // The picture as RGBA, `pitch` bytes a row (BT.601 video range).
    void rgba(uint8_t *out, int pitch) const;

private:
    struct Macroblock {
        int x = 0;
        int y = 0;
        int offset = 0;
        // type: 0 intra, 1 inter
        uint8_t type = 0;
        uint8_t cbp = 0;
        int8_t qDelta = 0;
        int8_t mvX = 0;
        int8_t mvY = 0;
    };
    struct Tile {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        std::vector<Macroblock> mbs;
        const std::vector<Macroblock> *ref = nullptr; // the first luma band's, for inheriting
    };
    enum class Transform : uint8_t { kSlant8x8, kRowSlant8, kColSlant8, kPut8x8, kSlant4x4 };
    struct Band {
        int plane = 0;
        int number = 0;
        int width = 0;
        int height = 0;
        int pitch = 0;
        int alignedHeight = 0;
        std::array<std::vector<int16_t>, 3> bufs;
        int16_t *buf = nullptr;
        const int16_t *ref = nullptr;
        int mbSize = 16;
        int blkSize = 8;
        bool halfPel = false;
        bool inheritMv = false;
        bool inheritQDelta = false;
        bool qDeltaPresent = false;
        int globalQuant = 0;
        Transform transform = Transform::kSlant8x8;
        const uint8_t *scan = nullptr;
        const uint16_t *intraBase = nullptr;
        const uint16_t *interBase = nullptr;
        const uint8_t *intraScale = nullptr;
        const uint8_t *interScale = nullptr;
        int codebook = 7; // 0..7, or 8: the band's own
        std::array<uint8_t, 16> ownCodebook{};
        int ownRows = 0;
        int runValueMap = 8;
        std::vector<std::pair<uint8_t, uint8_t>> corrections;
        std::vector<Tile> tiles;
    };
    struct Plane {
        int width = 0;
        int height = 0;
        std::vector<Band> bands;
    };
    struct Bits;

    Error decodeGop(Bits &b);
    Error decodePictureHeader(Bits &b);
    Error decodeBand(Bits &b, Band &band);
    Error decodeMacroblocks(Bits &b, Band &band, Tile &tile);
    Error decodeBlocks(Bits &b, Band &band, Tile &tile);
    Error emptyTile(Band &band, Tile &tile);
    void layOut(int width, int height, int lumaBands, int chromaBands, int tileSize);
    void switchBuffers();
    void output(int plane, std::vector<uint8_t> &out) const;
    void recompose53(std::vector<uint8_t> &out) const;

    std::array<Plane, 3> _planes;
    int _frameType = 0;
    int _prevFrameType = 0;
    int _frameFlags = 0;
    int _gopFlags = 0;
    bool _gopValid = false;
    bool _scalable = false;
    bool _interScalable = false;
    int _lumaBands = 1;
    int _chromaBands = 1;
    int _tileSize = 0;
    int _bufSwitch = 0;
    int _dstBuf = 0;
    int _refBuf = 1;
    int _ref2Buf = 2;
    bool _decoded = false; // a picture is ready
    int _mbCodebook = 7;   // as Band::codebook, for macroblock signals
    std::array<uint8_t, 16> _mbOwnCodebook{};
    int _mbOwnRows = 0;
};

} // namespace reluva::video
