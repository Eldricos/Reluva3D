// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/stream.h"

namespace reluva::audio {

RawStream::RawStream(std::vector<uint8_t> data, Format format, int rate, bool stereo, int loops)
    : data_(std::move(data)), format_(format), rate_(rate), stereo_(stereo), loopsLeft_(loops) {
    count_ = format == Format::S16LE || format == Format::S16BE ? data_.size() / 2 : data_.size();
    if (stereo_)
        count_ &= ~size_t(1); // whole frames only
}

int16_t RawStream::sampleAt(size_t i) const {
    switch (format_) {
    case Format::U8: return int16_t((int(data_[i]) - 128) * 256);
    case Format::S8: return int16_t(int(int8_t(data_[i])) * 256);
    case Format::S16BE: return int16_t((data_[i * 2] << 8) | data_[i * 2 + 1]);
    case Format::S16LE:
    default: return int16_t(data_[i * 2] | (data_[i * 2 + 1] << 8));
    }
}

int RawStream::read(int16_t* out, int samples) {
    int written = 0;
    while (written < samples) {
        if (pos_ >= count_) {
            if (loopsLeft_ == 1 || count_ == 0)
                break;
            if (loopsLeft_ > 1)
                loopsLeft_--;
            pos_ = 0;
        }
        out[written++] = sampleAt(pos_++);
    }
    return written;
}

void QueuingStream::queue(const std::vector<int16_t>& samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.insert(queue_.end(), samples.begin(), samples.end());
}

void QueuingStream::finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    finished_ = true;
}

size_t QueuingStream::queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

int QueuingStream::read(int16_t* out, int samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    int n = 0;
    while (n < samples && !queue_.empty()) {
        out[n++] = queue_.front();
        queue_.pop_front();
    }
    if (!finished_)
        while (n < samples)
            out[n++] = 0;
    return n;
}

bool QueuingStream::ended() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return finished_ && queue_.empty();
}

std::vector<int16_t> readAll(Stream& stream, size_t maxSamples) {
    std::vector<int16_t> out;
    int16_t buf[4096];
    while (out.size() < maxSamples) {
        const int n = stream.read(buf, 4096);
        if (n <= 0)
            break;
        out.insert(out.end(), buf, buf + n);
        if (n < 4096)
            break;
    }
    return out;
}

} // namespace reluva::audio
