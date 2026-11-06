// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/video/avi.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::avi {

bool Decoder::load(std::unique_ptr<SeekableReadStream> stream) {
    stream_ = std::move(stream);
    cinepak_.reset();
    frames_.clear();
    audio_.clear();
    width_ = height_ = 0;
    frameMicros_ = 41666;
    videoCodec_ = 0;
    streams_ = 0;
    videoStream_ = audioStream_ = -1;
    audioRate_ = audioChannels_ = audioBits_ = 0;
    current_ = -1;
    if (!stream_ || stream_->readUint32LE() != fourcc("RIFF")) {
        stream_.reset();
        return false;
    }
    const uint32_t size = stream_->readUint32LE();
    if (stream_->readUint32LE() != fourcc("AVI ")) {
        stream_.reset();
        return false;
    }
    const uint32_t end = uint32_t(std::min<int64_t>(int64_t(size) + 8, stream_->size()));
    if (!readList(end, 0) || videoStream_ < 0 || width_ <= 0 || height_ <= 0) {
        stream_.reset();
        return false;
    }
    if (audioStream_ < 0)
        audio_.clear();
    if (videoCodec_ == fourcc("cvid") || videoCodec_ == fourcc("CVID"))
        cinepak_ = std::make_unique<cinepak::Decoder>(width_, height_);
    return true;
}

bool Decoder::readList(uint32_t end, int depth) {
    if (depth > 8)
        return false;
    int streamType = -1; // within a strl list: 0 video, 1 audio
    while (uint32_t(stream_->pos()) + 8 <= end) {
        const uint32_t id = stream_->readUint32LE();
        const uint32_t size = stream_->readUint32LE();
        const uint32_t start = uint32_t(stream_->pos());
        const uint32_t next = start + size + (size & 1);
        if (stream_->err() || start + size > end)
            break;

        if (id == fourcc("LIST")) {
            const uint32_t type = stream_->readUint32LE();
            if (type == fourcc("strl"))
                ++streams_;
            if (!readList(start + size, depth + 1))
                return false;
        } else if (id == fourcc("avih")) {
            frameMicros_ = stream_->readUint32LE();
            if (!frameMicros_)
                frameMicros_ = 41666;
        } else if (id == fourcc("strh")) {
            const uint32_t type = stream_->readUint32LE();
            const uint32_t handler = stream_->readUint32LE();
            if (type == fourcc("vids") && videoStream_ < 0) {
                videoStream_ = streams_ - 1;
                videoCodec_ = handler;
                streamType = 0;
            } else if (type == fourcc("auds") && audioStream_ < 0) {
                audioStream_ = streams_ - 1;
                streamType = 1;
            } else {
                streamType = -1;
            }
        } else if (id == fourcc("strf")) {
            if (streamType == 0 && size >= 12) {
                stream_->readUint32LE(); // biSize
                width_ = int32_t(stream_->readUint32LE());
                height_ = std::abs(int32_t(stream_->readUint32LE()));
            } else if (streamType == 1 && size >= 16) {
                const uint16_t format = stream_->readUint16LE();
                audioChannels_ = stream_->readUint16LE();
                audioRate_ = int(stream_->readUint32LE());
                stream_->readUint32LE(); // bytes a second
                stream_->readUint16LE(); // block align
                audioBits_ = stream_->readUint16LE();
                if (format != 1) // PCM only
                    audioStream_ = -2;
            }
        } else if ((id >> 16) == fourcc("..dc") >> 16 || (id >> 16) == fourcc("..db") >> 16 ||
                   (id >> 16) == fourcc("..id") >> 16 || (id >> 16) == fourcc("..wb") >> 16) {
            // "nndc"/"nndb" video (some games' inter frames are "nnid"),
            // "nnwb" sound; nn is the stream's number.
            const int number = (int(id & 0xFF) - '0') * 10 + (int((id >> 8) & 0xFF) - '0');
            if ((id >> 16) == fourcc("..wb") >> 16) {
                if (number == audioStream_ && size) {
                    const size_t old = audio_.size();
                    audio_.resize(old + size);
                    audio_.resize(old + stream_->read(&audio_[old], size));
                }
            } else if (number == videoStream_) {
                frames_.push_back({start, size});
            }
        }
        stream_->seek(next);
    }
    return true;
}

bool Decoder::readFrame(int index, std::vector<uint8_t>& out) {
    out.clear();
    if (!stream_ || index < 0 || index >= frameCount())
        return false;
    const Chunk& chunk = frames_[size_t(index)];
    out.resize(chunk.size);
    if (!chunk.size)
        return true;
    return stream_->seek(chunk.offset) && stream_->read(out.data(), chunk.size) == chunk.size;
}

bool Decoder::decodeNextFrame() {
    if (!cinepak_ || endOfVideo())
        return false;
    ++current_;
    std::vector<uint8_t> data;
    if (!readFrame(current_, data))
        return false;
    cinepak_->decode(data.data(), uint32_t(data.size()));
    return true;
}

const Surface& Decoder::surface() const {
    static const Surface kEmpty;
    return cinepak_ ? cinepak_->surface() : kEmpty;
}

} // namespace reluva::avi
