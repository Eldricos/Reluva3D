// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "video/avi.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::avi {

bool Decoder::load(std::unique_ptr<SeekableReadStream> stream) {
    _stream = std::move(stream);
    _cinepak.reset();
    _frames.clear();
    _audio.clear();
    _width = _height = 0;
    _frameMicros = 41666;
    _videoCodec = 0;
    _streams = 0;
    _videoStream = _audioStream = -1;
    _audioRate = _audioChannels = _audioBits = 0;
    _current = -1;
    if (!_stream || _stream->readUint32LE() != fourcc("RIFF")) {
        _stream.reset();
        return false;
    }
    const uint32_t size = _stream->readUint32LE();
    if (_stream->readUint32LE() != fourcc("AVI ")) {
        _stream.reset();
        return false;
    }
    const uint32_t end = uint32_t(std::min<int64_t>(int64_t(size) + 8, _stream->size()));
    if (!readList(end, 0) || _videoStream < 0 || _width <= 0 || _height <= 0) {
        _stream.reset();
        return false;
    }
    if (_audioStream < 0)
        _audio.clear();
    if (_videoCodec == fourcc("cvid") || _videoCodec == fourcc("CVID"))
        _cinepak = std::make_unique<cinepak::Decoder>(_width, _height);
    return true;
}

bool Decoder::readList(uint32_t end, int depth) {
    if (depth > 8)
        return false;
    int streamType = -1; // within a strl list: 0 video, 1 audio
    while (uint32_t(_stream->pos()) + 8 <= end) {
        const uint32_t id = _stream->readUint32LE();
        const uint32_t size = _stream->readUint32LE();
        const uint32_t start = uint32_t(_stream->pos());
        const uint32_t next = start + size + (size & 1);
        if (_stream->err() || start + size > end)
            break;

        if (id == fourcc("LIST")) {
            const uint32_t type = _stream->readUint32LE();
            if (type == fourcc("strl"))
                ++_streams;
            if (!readList(start + size, depth + 1))
                return false;
        } else if (id == fourcc("avih")) {
            _frameMicros = _stream->readUint32LE();
            if (!_frameMicros)
                _frameMicros = 41666;
        } else if (id == fourcc("strh")) {
            const uint32_t type = _stream->readUint32LE();
            const uint32_t handler = _stream->readUint32LE();
            if (type == fourcc("vids") && _videoStream < 0) {
                _videoStream = _streams - 1;
                _videoCodec = handler;
                streamType = 0;
            } else if (type == fourcc("auds") && _audioStream < 0) {
                _audioStream = _streams - 1;
                streamType = 1;
            } else {
                streamType = -1;
            }
        } else if (id == fourcc("strf")) {
            if (streamType == 0 && size >= 12) {
                _stream->readUint32LE(); // biSize
                _width = int32_t(_stream->readUint32LE());
                _height = std::abs(int32_t(_stream->readUint32LE()));
            } else if (streamType == 1 && size >= 16) {
                const uint16_t format = _stream->readUint16LE();
                _audioChannels = _stream->readUint16LE();
                _audioRate = int(_stream->readUint32LE());
                _stream->readUint32LE(); // bytes a second
                _stream->readUint16LE(); // block align
                _audioBits = _stream->readUint16LE();
                if (format != 1) // PCM only
                    _audioStream = -2;
            }
        } else if ((id >> 16) == fourcc("..dc") >> 16 || (id >> 16) == fourcc("..db") >> 16 ||
                   (id >> 16) == fourcc("..id") >> 16 || (id >> 16) == fourcc("..wb") >> 16) {
            // "nndc"/"nndb" video (some games' inter frames are "nnid"),
            // "nnwb" sound; nn is the stream's number.
            const int number = (int(id & 0xFF) - '0') * 10 + (int((id >> 8) & 0xFF) - '0');
            if ((id >> 16) == fourcc("..wb") >> 16) {
                if (number == _audioStream && size) {
                    const size_t old = _audio.size();
                    _audio.resize(old + size);
                    _audio.resize(old + _stream->read(&_audio[old], size));
                }
            } else if (number == _videoStream) {
                _frames.push_back({start, size});
            }
        }
        _stream->seek(next);
    }
    return true;
}

bool Decoder::readFrame(int index, std::vector<uint8_t> &out) {
    out.clear();
    if (!_stream || index < 0 || index >= frameCount())
        return false;
    const Chunk &chunk = _frames[size_t(index)];
    out.resize(chunk.size);
    if (!chunk.size)
        return true;
    return _stream->seek(chunk.offset) && _stream->read(out.data(), chunk.size) == chunk.size;
}

bool Decoder::decodeNextFrame() {
    if (!_cinepak || endOfVideo())
        return false;
    ++_current;
    std::vector<uint8_t> data;
    if (!readFrame(_current, data))
        return false;
    _cinepak->decode(data.data(), uint32_t(data.size()));
    return true;
}

const Surface &Decoder::surface() const {
    static const Surface kEmpty;
    return _cinepak ? _cinepak->surface() : kEmpty;
}

} // namespace reluva::avi
