// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/stream.h"

namespace reluva::audio {

RawStream::RawStream(std::vector<uint8_t> data, Format format, int rate, bool stereo, int loops)
    : _data(std::move(data)), _format(format), _rate(rate), _stereo(stereo), _loopsLeft(loops) {
    _count = format == Format::kS16LE || format == Format::kS16BE ? _data.size() / 2 : _data.size();
    if (_stereo)
        _count &= ~size_t(1); // whole frames only
}

int16_t RawStream::sampleAt(size_t i) const {
    switch (_format) {
    case Format::kU8:
        return int16_t((int(_data[i]) - 128) * 256);
    case Format::kS8:
        return int16_t(int(int8_t(_data[i])) * 256);
    case Format::kS16BE:
        return int16_t((_data[i * 2] << 8) | _data[i * 2 + 1]);
    case Format::kS16LE:
    default:
        return int16_t(_data[i * 2] | (_data[i * 2 + 1] << 8));
    }
}

int RawStream::read(int16_t *out, int samples) {
    int written = 0;
    while (written < samples) {
        if (_pos >= _count) {
            if (_loopsLeft == 1 || _count == 0)
                break;
            if (_loopsLeft > 1)
                _loopsLeft--;
            _pos = 0;
        }
        out[written++] = sampleAt(_pos++);
    }
    return written;
}

void QueuingStream::queue(const std::vector<int16_t> &samples) {
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.insert(_queue.end(), samples.begin(), samples.end());
}

void QueuingStream::finish() {
    std::lock_guard<std::mutex> lock(_mutex);
    _finished = true;
}

size_t QueuingStream::queued() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _queue.size();
}

int QueuingStream::read(int16_t *out, int samples) {
    std::lock_guard<std::mutex> lock(_mutex);
    int n = 0;
    while (n < samples && !_queue.empty()) {
        out[n++] = _queue.front();
        _queue.pop_front();
    }
    if (!_finished)
        while (n < samples)
            out[n++] = 0;
    return n;
}

bool QueuingStream::ended() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _finished && _queue.empty();
}

std::vector<int16_t> readAll(Stream &stream, size_t maxSamples) {
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
