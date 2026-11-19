// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/audio.h"

#include "audio/wav.h"
#include "common/bytes.h"

#include <algorithm>

namespace reluva::stgenerations {

SoundStream::SoundStream(std::shared_ptr<const audio::Sound> sound, int loops)
    : _sound(std::move(sound)), _loops(loops) {
}

int SoundStream::read(int16_t *out, int samples) {
    const size_t total = _sound->samples.size();
    int done = 0;
    while (done < samples && total > 0 && !ended()) {
        const size_t n = std::min(size_t(samples - done), total - _pos);
        std::copy_n(_sound->samples.data() + _pos, n, out + done);
        done += int(n);
        _pos += n;
        if (_pos >= total) {
            ++_played;
            if (_loops == 0 || _played < _loops)
                _pos = 0;
        }
    }
    return done;
}

bool SoundStream::ended() const {
    return _sound->samples.empty() || (_loops != 0 && _played >= _loops);
}

uint32_t SoundStream::lengthMs() const {
    if (!_sound->rate)
        return 0;
    return uint32_t(uint64_t(_sound->frames()) * 1000 / uint64_t(_sound->rate));
}

std::unique_ptr<SoundStream> makeWavStream(SeekableReadStream &file) {
    file.seek(0);
    const std::vector<uint8_t> data = file.readAll();
    auto sound = std::make_shared<audio::Sound>();
    if (audio::decodeWav(data, *sound) || sound->rate <= 0 || sound->channels < 1 || sound->channels > 2)
        return nullptr;
    return std::make_unique<SoundStream>(std::move(sound));
}

std::unique_ptr<audio::Stream> makeLooping(std::unique_ptr<SoundStream> stream, int loops) {
    if (stream)
        stream->setLoops(loops);
    return stream;
}

std::unique_ptr<audio::Stream> makeRawStream(std::vector<uint8_t> data, int rate, uint8_t flags) {
    audio::RawStream::Format format = audio::RawStream::Format::kU8;
    if (flags & kRaw16Bits)
        format = (flags & kRawLittleEndian) ? audio::RawStream::Format::kS16LE : audio::RawStream::Format::kS16BE;
    else if (!(flags & kRawUnsigned))
        format = audio::RawStream::Format::kS8;
    return std::make_unique<audio::RawStream>(std::move(data), format, rate, (flags & kRawStereo) != 0);
}

bool readWavInfo(SeekableReadStream &file, WavInfo &info) {
    info = WavInfo{};
    file.seek(0);
    if (file.readUint32LE() != tagLE('R', 'I', 'F', 'F'))
        return false;
    file.readUint32LE();
    if (file.readUint32LE() != tagLE('W', 'A', 'V', 'E'))
        return false;
    bool haveFormat = false;
    while (!file.err() && file.pos() + 8 <= file.size()) {
        const uint32_t id = file.readUint32LE();
        const uint32_t size = file.readUint32LE();
        const int64_t next = file.pos() + size + (size & 1);
        if (id == tagLE('f', 'm', 't', ' ') && size >= 16) {
            file.readUint16LE(); // format
            info.channels = file.readUint16LE();
            info.sampleRate = int(file.readUint32LE());
            file.readUint32LE(); // bytes a second
            file.readUint16LE(); // block align
            info.bitsPerSample = file.readUint16LE();
            haveFormat = true;
        } else if (id == tagLE('d', 'a', 't', 'a')) {
            info.dataSize = uint32_t(std::min<int64_t>(size, file.size() - file.pos()));
            return haveFormat;
        }
        file.seek(next);
    }
    return false;
}

audio::SoundType Mixer::toType(SoundType type) {
    switch (type) {
    case kMusicSoundType:
        return audio::SoundType::kMusic;
    case kSpeechSoundType:
        return audio::SoundType::kSpeech;
    default:
        return audio::SoundType::kEffects;
    }
}

void Mixer::playStream(SoundType type, SoundHandle *handle, std::unique_ptr<audio::Stream> stream, int id,
                       uint8_t volume, int8_t balance) {
    (void)id;
    SoundHandle h;
    if (stream && _enabled)
        h.val = _mixer.play(std::shared_ptr<audio::Stream>(std::move(stream)), toType(type), toVolume(volume),
                            std::clamp<int>(balance, -audio::kMaxPan, audio::kMaxPan));
    if (handle)
        *handle = h;
}

void Mixer::setVolumeForSoundType(SoundType type, int volume) {
    volume = std::clamp(volume, 0, kMaxMixerVolume);
    _typeVolume[type] = volume;
    // Plain sounds share the effects' volume in Reluva's mixer; the game
    // plays none.
    if (type != kPlainSoundType)
        _mixer.setTypeVolume(toType(type), volume * audio::kMaxVolume / kMaxMixerVolume);
}

int Mixer::getVolumeForSoundType(SoundType type) const {
    return _typeVolume[type];
}

} // namespace reluva::stgenerations
