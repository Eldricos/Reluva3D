// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Builds EA movies (.tgq) and their TQI frames for tests, laid out as
// docs/common/video/tqi.md says.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::test {

// A frame of `width` x `height` at quantiser `q`, its codes given as a
// string of bits, packed into 32-bit little-endian words from their top.
inline std::vector<uint8_t> makeTqiFrame(int width, int height, int q, std::string bits) {
    std::vector<uint8_t> f = {
        uint8_t(width), uint8_t(width >> 8), uint8_t(height), uint8_t(height >> 8), uint8_t(q), 0, 0, 0};
    while (bits.size() % 32)
        bits += '0';
    for (size_t at = 0; at < bits.size(); at += 32) {
        const uint32_t word = uint32_t(std::stoul(bits.substr(at, 32), nullptr, 2));
        for (int i = 0; i < 4; ++i)
            f.push_back(uint8_t(word >> (8 * i)));
    }
    return f;
}

// One macroblock: the first luma block's DC 100 (size 7) with an AC
// coefficient of 2 (code 0100) and an escaped one (run 3, level -5); the
// other luma blocks' DC unchanged; both chroma blocks' DC 128 (size 8).
inline const std::string kTqiMacroblock = std::string("111110") + "1100100" + "0100" + "0" + "000001" + "000011" +
                                          "11111011" + "10" +
                                          "100"
                                          "10" +
                                          "100"
                                          "10" +
                                          "100"
                                          "10" +
                                          "11111110" + "10000000" + "10" + "11111110" + "10000000" + "10";

// A chunk of an EA movie: its tag, its size with these 8 bytes, its bytes.
inline void movieChunk(std::vector<uint8_t> &out, const char *tag, const std::vector<uint8_t> &body) {
    out.insert(out.end(), tag, tag + 4);
    const uint32_t size = uint32_t(8 + body.size());
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(size >> (8 * i)));
    out.insert(out.end(), body.begin(), body.end());
}

// A movie of two frames and a sound of 2 stereo PCM samples, the frames
// between the sound's chunks.
inline std::vector<uint8_t> makeEaMovie() {
    const std::vector<uint8_t> frame = makeTqiFrame(16, 16, 99, kTqiMacroblock);
    std::vector<uint8_t> m;
    movieChunk(m, "SCHl", {'P', 'T', 0, 0, 0x82, 1, 2, 0x85, 1, 2, 0xff});
    movieChunk(m, "pIQT", frame);
    movieChunk(m, "SCCl", {1, 0, 0, 0});
    movieChunk(m, "SCDl", {2, 0, 0, 0, 1, 0, 0xff, 0xff, 2, 0, 0xfe, 0xff});
    movieChunk(m, "pIQT", frame);
    movieChunk(m, "SCEl", {});
    return m;
}

} // namespace reluva::test
