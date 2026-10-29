// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/video/avi.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace reluva::video {

namespace {

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

} // namespace

Error AviFile::parse(const std::vector<uint8_t>& data) {
    *this = {};
    if (data.size() < 12 || le32(&data[0]) != fourcc("RIFF") || le32(&data[8]) != fourcc("AVI "))
        return Error::fail("not an AVI file");
    // Streams in the order their "strl" lists come; each's type and the
    // number its chunks are named by ("00dc", "01wb").
    int stream = 0, videoStream = -1, audioStream = -1;
    bool sawMovi = false;
    // Chunks and lists, depth first.
    struct Span {
        size_t at, end;
    };
    std::vector<Span> stack{{12, std::min(data.size(), size_t(le32(&data[4])) + 8)}};
    uint32_t lastStreamType = 0;
    while (!stack.empty()) {
        Span& span = stack.back();
        if (span.at + 8 > span.end) {
            stack.pop_back();
            continue;
        }
        const uint32_t id = le32(&data[span.at]);
        const size_t size = le32(&data[span.at + 4]);
        const size_t body = span.at + 8;
        if (body + size > data.size())
            return Error::fail("AVI: a chunk runs past the end");
        span.at = body + size + (size & 1);
        if (id == fourcc("LIST") && size >= 4) {
            const uint32_t type = le32(&data[body]);
            if (type == fourcc("movi"))
                sawMovi = true;
            if (type == fourcc("strl"))
                lastStreamType = 0;
            stack.push_back({body + 4, body + size}); // span is invalid from here
            continue;
        }
        if (id == fourcc("strh") && size >= 8) {
            lastStreamType = le32(&data[body]);
            if (lastStreamType == fourcc("vids") && videoStream < 0 && size >= 32) {
                videoStream = stream;
                videoCodec = le32(&data[body + 4]);
                const uint32_t scale = le32(&data[body + 20]), rate = le32(&data[body + 24]);
                framesPerSecond = scale ? double(rate) / scale : 0;
            } else if (lastStreamType == fourcc("auds") && audioStream < 0) {
                audioStream = stream;
            }
            ++stream;
        } else if (id == fourcc("strf")) {
            if (stream - 1 == videoStream && lastStreamType == fourcc("vids") && size >= 12 && !width) {
                width = int(le32(&data[body + 4]));
                height = std::abs(int(le32(&data[body + 8])));
            } else if (stream - 1 == audioStream && lastStreamType == fourcc("auds") && size >= 16 && !audioRate) {
                audioFormat = le16(&data[body]);
                audioChannels = le16(&data[body + 2]);
                audioRate = int(le32(&data[body + 4]));
                audioBits = le16(&data[body + 14]);
            }
        } else if (sawMovi) {
            // "NNdc"/"NNdb": a video frame; "NNwb": audio. The first two
            // characters are the stream's number in decimal.
            const char* c = reinterpret_cast<const char*>(&data[body - 8]);
            if (c[0] >= '0' && c[0] <= '9' && c[1] >= '0' && c[1] <= '9') {
                const int n = (c[0] - '0') * 10 + (c[1] - '0');
                if (n == videoStream && c[2] == 'd')
                    frames.push_back({body, size});
                else if (n == audioStream && c[2] == 'w' && c[3] == 'b')
                    audio.push_back({body, size});
            }
        }
    }
    if (videoStream < 0 && audioStream < 0)
        return Error::fail("AVI: no streams");
    return Error::success();
}

std::shared_ptr<audio::Sound> AviFile::sound(const std::vector<uint8_t>& data) const {
    if (audioFormat != 1 || audioBits != 16 || audioChannels < 1 || audioChannels > 2 || audio.empty())
        return nullptr;
    auto s = std::make_shared<audio::Sound>();
    s->rate = audioRate;
    s->channels = audioChannels;
    s->format = 1;
    size_t total = 0;
    for (const Chunk& c : audio)
        total += c.size / 2;
    s->samples.resize(total);
    size_t at = 0;
    for (const Chunk& c : audio) {
        std::memcpy(s->samples.data() + at, &data[c.offset], c.size / 2 * 2);
        at += c.size / 2;
    }
    s->samples.resize(s->samples.size() / size_t(audioChannels) * size_t(audioChannels));
    return s;
}

} // namespace reluva::video
