// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "common/audio/sound.h"
#include "common/audio/stream.h"
#include "common/stream.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace reluva::stgenerations {

// The game's sounds on Reluva's mixer, in the terms the game's code was
// written in (it came from STG-Engine): handles that stay valid after a
// sound ends, 0..255 volumes, -127..127 balance, and four kinds of sound.

struct SoundHandle {
    audio::SoundHandle val = audio::kNoSound;
};

// A WAV file's sound, decoded, played from the start `loops` times (0:
// until stopped).
class SoundStream : public audio::Stream {
public:
    SoundStream(std::shared_ptr<const audio::Sound> sound, int loops = 1);
    int read(int16_t* out, int samples) override;
    bool stereo() const override { return sound_->channels == 2; }
    int rate() const override { return sound_->rate; }
    bool ended() const override;
    void setLoops(int loops) { loops_ = loops; }
    const audio::Sound& sound() const { return *sound_; }
    // Its length in milliseconds, once through.
    uint32_t lengthMs() const;

private:
    std::shared_ptr<const audio::Sound> sound_;
    size_t pos_ = 0;
    int loops_;
    int played_ = 0;
};

// A WAV file as a stream; nullptr if it isn't one Reluva reads.
std::unique_ptr<SoundStream> makeWavStream(SeekableReadStream& file);
// The same stream, played `loops` times (0: until stopped).
std::unique_ptr<audio::Stream> makeLooping(std::unique_ptr<SoundStream> stream, int loops);

// Raw PCM (the movies' sound).
enum { kRaw16Bits = 1 << 0, kRawLittleEndian = 1 << 1, kRawStereo = 1 << 2, kRawUnsigned = 1 << 3 };
std::unique_ptr<audio::Stream> makeRawStream(std::vector<uint8_t> data, int rate, uint8_t flags);

// What a WAV file's header says.
struct WavInfo {
    int sampleRate = 0, channels = 0, bitsPerSample = 0;
    uint32_t dataSize = 0;
};
bool readWavInfo(SeekableReadStream& file, WavInfo& info);

class Mixer {
public:
    enum SoundType { kPlainSoundType, kMusicSoundType, kSFXSoundType, kSpeechSoundType };
    static constexpr int kMaxChannelVolume = 255;
    static constexpr int kMaxMixerVolume = 256;

    explicit Mixer(audio::Mixer& mixer) : mixer_(mixer) {}

    // `volume` 0..255, `balance` -127 (left) .. 127 (right).
    void playStream(SoundType type, SoundHandle* handle, std::unique_ptr<audio::Stream> stream, int id = -1,
                    uint8_t volume = kMaxChannelVolume, int8_t balance = 0);
    void stopAll() { mixer_.stopAll(); }
    void stopHandle(SoundHandle handle) { mixer_.stop(handle.val); }
    void pauseHandle(SoundHandle handle, bool paused) { mixer_.setPaused(handle.val, paused); }
    bool isSoundHandleActive(SoundHandle handle) const { return mixer_.isPlaying(handle.val); }
    bool isSoundTypeActive(SoundType type) const { return mixer_.isTypePlaying(toType(type)); }
    void setChannelVolume(SoundHandle handle, uint8_t volume) { mixer_.setVolume(handle.val, toVolume(volume)); }
    void setChannelBalance(SoundHandle handle, int8_t balance) { mixer_.setPan(handle.val, balance); }
    // 0..kMaxMixerVolume, as the player's settings set them.
    void setVolumeForSoundType(SoundType type, int volume);
    int getVolumeForSoundType(SoundType type) const;

    audio::Mixer& mixer() { return mixer_; }

private:
    static audio::SoundType toType(SoundType type);
    static int toVolume(int volume) { return (volume < 0 ? 0 : volume > 255 ? 255 : volume) * audio::kMaxVolume / 255; }

    audio::Mixer& mixer_;
    int typeVolume_[4] = {kMaxMixerVolume, kMaxMixerVolume, kMaxMixerVolume, kMaxMixerVolume};
};

} // namespace reluva::stgenerations
