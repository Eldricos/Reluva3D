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
    Error decode(const uint8_t* data, size_t size);
    int width() const { return planes_[0].width; }
    int height() const { return planes_[0].height; }
    // The picture: luma, then the two chroma planes (Cb, Cr), each
    // width × height bytes, 0..255.
    void yuv(std::vector<uint8_t>& y, std::vector<uint8_t>& cb, std::vector<uint8_t>& cr) const;
    // The picture as RGBA, `pitch` bytes a row (BT.601 video range).
    void rgba(uint8_t* out, int pitch) const;

private:
    struct Macroblock {
        int x = 0, y = 0, offset = 0;
        uint8_t type = 0, cbp = 0; // type: 0 intra, 1 inter
        int8_t qDelta = 0, mvX = 0, mvY = 0;
    };
    struct Tile {
        int x = 0, y = 0, width = 0, height = 0;
        std::vector<Macroblock> mbs;
        const std::vector<Macroblock>* ref = nullptr; // the first luma band's, for inheriting
    };
    enum class Transform : uint8_t { Slant8x8, RowSlant8, ColSlant8, Put8x8, Slant4x4 };
    struct Band {
        int plane = 0, number = 0;
        int width = 0, height = 0, pitch = 0, alignedHeight = 0;
        std::array<std::vector<int16_t>, 3> bufs;
        int16_t* buf = nullptr;
        const int16_t* ref = nullptr;
        int mbSize = 16, blkSize = 8;
        bool halfPel = false, inheritMv = false, inheritQDelta = false, qDeltaPresent = false;
        int globalQuant = 0;
        Transform transform = Transform::Slant8x8;
        const uint8_t* scan = nullptr;
        const uint16_t *intraBase = nullptr, *interBase = nullptr;
        const uint8_t *intraScale = nullptr, *interScale = nullptr;
        int codebook = 7; // 0..7, or 8: the band's own
        std::array<uint8_t, 16> ownCodebook{};
        int ownRows = 0;
        int runValueMap = 8;
        std::vector<std::pair<uint8_t, uint8_t>> corrections;
        std::vector<Tile> tiles;
    };
    struct Plane {
        int width = 0, height = 0;
        std::vector<Band> bands;
    };
    struct Bits;

    Error decodeGop(Bits& b);
    Error decodePictureHeader(Bits& b);
    Error decodeBand(Bits& b, Band& band);
    Error decodeMacroblocks(Bits& b, Band& band, Tile& tile);
    Error decodeBlocks(Bits& b, Band& band, Tile& tile);
    Error emptyTile(Band& band, Tile& tile);
    void layOut(int width, int height, int lumaBands, int chromaBands, int tileSize);
    void switchBuffers();
    void output(int plane, std::vector<uint8_t>& out) const;
    void recompose53(std::vector<uint8_t>& out) const;

    std::array<Plane, 3> planes_;
    int frameType_ = 0, prevFrameType_ = 0;
    int frameFlags_ = 0, gopFlags_ = 0;
    bool gopValid_ = false, scalable_ = false, interScalable_ = false;
    int lumaBands_ = 1, chromaBands_ = 1, tileSize_ = 0;
    int bufSwitch_ = 0, dstBuf_ = 0, refBuf_ = 1, ref2Buf_ = 2;
    bool decoded_ = false; // a picture is ready
    int mbCodebook_ = 7;   // as Band::codebook, for macroblock signals
    std::array<uint8_t, 16> mbOwnCodebook_{};
    int mbOwnRows_ = 0;
};

} // namespace reluva::video
