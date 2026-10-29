// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/video/avi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace reluva::video {

namespace {

uint16_t u16(const std::vector<uint8_t>& d, size_t o) { return uint16_t(d[o] | d[o + 1] << 8); }
uint32_t u32(const std::vector<uint8_t>& d, size_t o) { return uint32_t(u16(d, o)) | uint32_t(u16(d, o + 2)) << 16; }
bool is(const std::vector<uint8_t>& d, size_t o, const char* id) { return std::memcmp(d.data() + o, id, 4) == 0; }

// Calls f(chunk offset, data offset, data size) for each chunk from p to
// end; a size running past the end is cut to it.
template <typename F>
void forEachChunk(const std::vector<uint8_t>& d, size_t p, size_t end, F f) {
    while (p < end && end - p >= 8) {
        const size_t size = std::min<size_t>(u32(d, p + 4), end - p - 8);
        f(p, p + 8, size);
        p += 8 + size + (size & 1); // chunks are padded to even sizes
    }
}

} // namespace

Error AviFile::open(std::vector<uint8_t> data) {
    *this = {};
    data_ = std::move(data);
    if (data_.size() < 12 || !is(data_, 0, "RIFF") || !is(data_, 8, "AVI "))
        return Error::fail("not an AVI file");
    // Some files have bytes after the RIFF form; they're left alone.
    const size_t end = std::min<size_t>(data_.size(), size_t(8) + u32(data_, 4));
    bool movie = false;
    Error err;
    forEachChunk(data_, 12, end, [&](size_t c, size_t p, size_t size) {
        if (!is(data_, c, "LIST") || size < 4)
            return;
        if (is(data_, p, "hdrl") && !err)
            err = readHeaders(p + 4, p + size);
        else if (is(data_, p, "movi")) {
            readMovie(p + 4, p + size);
            movie = true;
        }
    });
    if (err)
        return err;
    if (videoStream_ < 0)
        return Error::fail("AVI file without video");
    if (!movie)
        return Error::fail("AVI file without its movie data");
    return Error::success();
}

Error AviFile::readHeaders(size_t p, size_t end) {
    uint32_t usPerFrame = 0;
    int stream = 0;
    forEachChunk(data_, p, end, [&](size_t c, size_t q, size_t size) {
        if (is(data_, c, "avih") && size >= 4)
            usPerFrame = u32(data_, q);
        if (!is(data_, c, "LIST") || size < 4 || !is(data_, q, "strl"))
            return;
        // One stream: its header (strh), then its format (strf).
        std::string type, handler;
        uint32_t scale = 0, rate = 0;
        forEachChunk(data_, q + 4, q + size, [&](size_t c2, size_t r, size_t n) {
            if (is(data_, c2, "strh") && n >= 28) {
                type.assign(reinterpret_cast<const char*>(data_.data() + r), 4);
                handler.assign(reinterpret_cast<const char*>(data_.data() + r + 4), 4);
                scale = u32(data_, r + 20);
                rate = u32(data_, r + 24);
            } else if (is(data_, c2, "strf")) {
                if (type == "vids" && videoStream_ < 0 && n >= 20) {
                    videoStream_ = stream;
                    width_ = int(int32_t(u32(data_, r + 4)));
                    height_ = std::abs(int(int32_t(u32(data_, r + 8))));
                    videoCodec_.assign(reinterpret_cast<const char*>(data_.data() + r + 16), 4);
                    if (videoCodec_[0] == 0)
                        videoCodec_ = handler;
                    for (char& ch : videoCodec_)
                        ch = char(std::tolower(static_cast<unsigned char>(ch)));
                    if (scale && rate)
                        fps_ = double(rate) / scale;
                } else if (type == "auds" && audioStream_ < 0 && n >= 16) {
                    audioStream_ = stream;
                    audioFormat_ = u16(data_, r);
                    audioChannels_ = u16(data_, r + 2);
                    audioRate_ = int(u32(data_, r + 4));
                    audioBits_ = u16(data_, r + 14);
                }
            }
        });
        ++stream;
    });
    if (fps_ <= 0 && usPerFrame)
        fps_ = 1e6 / usPerFrame;
    if (fps_ <= 0 || fps_ > 1000)
        return Error::fail("AVI frame rate out of range");
    return Error::success();
}

void AviFile::readMovie(size_t p, size_t end) {
    forEachChunk(data_, p, end, [&](size_t c, size_t q, size_t size) {
        if (is(data_, c, "LIST")) { // "rec " groups a frame's chunks
            if (size >= 4)
                readMovie(q + 4, q + size);
            return;
        }
        // "00dc": stream 00, compressed video ("db": uncompressed, but the
        // name is used for key frames too); "01wb": stream 01, audio.
        const uint8_t* id = data_.data() + c;
        if (id[0] < '0' || id[0] > '9' || id[1] < '0' || id[1] > '9')
            return;
        const int stream = (id[0] - '0') * 10 + (id[1] - '0');
        if (stream == videoStream_ && id[2] == 'd' && (id[3] == 'c' || id[3] == 'b'))
            frames_.push_back({q, size});
        else if (stream == audioStream_ && id[2] == 'w' && id[3] == 'b')
            audioChunks_.push_back({q, size});
    });
}

void AviFile::frame(size_t i, const uint8_t*& data, size_t& size) const {
    data = data_.data() + frames_[i].offset;
    size = frames_[i].size;
}

Error AviFile::audio(audio::Sound& out) const {
    out = {};
    if (!hasAudio())
        return Error::fail("AVI file without sound");
    if (audioFormat_ != 1 || (audioBits_ != 8 && audioBits_ != 16) || audioChannels_ < 1 || audioChannels_ > 2)
        return Error::fail("AVI sound isn't 8- or 16-bit PCM");
    out.rate = audioRate_;
    out.channels = audioChannels_;
    out.format = 1;
    for (const Span& s : audioChunks_) {
        const uint8_t* p = data_.data() + s.offset;
        if (audioBits_ == 16)
            for (size_t i = 0; i + 1 < s.size; i += 2)
                out.samples.push_back(int16_t(p[i] | p[i + 1] << 8));
        else
            for (size_t i = 0; i < s.size; ++i)
                out.samples.push_back(int16_t((p[i] - 128) << 8));
    }
    // Whole frames of samples only.
    out.samples.resize(out.samples.size() / size_t(out.channels) * size_t(out.channels));
    return Error::success();
}

Error VideoPlayer::open(std::vector<uint8_t> data) {
    decoder_.reset();
    next_ = shown_ = 0;
    if (Error err = file_.open(std::move(data)))
        return err;
    if (file_.videoCodec() != "cvid")
        return Error::fail("AVI video in " + file_.videoCodec() + ", not Cinepak");
    return seek(0);
}

double VideoPlayer::durationSeconds() const {
    return file_.framesPerSecond() > 0 ? double(file_.frameCount()) / file_.framesPerSecond() : 0;
}

Error VideoPlayer::seek(double seconds) {
    if (file_.frameCount() == 0)
        return Error::success();
    const size_t target =
        std::min(file_.frameCount() - 1, size_t(std::max(0.0, std::floor(seconds * file_.framesPerSecond()))));
    // Frames build on the ones before: going back starts again.
    if (target + 1 < next_) {
        decoder_.reset();
        next_ = 0;
    }
    for (; next_ <= target; ++next_) {
        const uint8_t* data = nullptr;
        size_t size = 0;
        file_.frame(next_, data, size);
        if (Error err = decoder_.decode(data, size))
            return Error::fail("frame " + std::to_string(next_) + ": " + err.message);
    }
    shown_ = target;
    return Error::success();
}

} // namespace reluva::video
