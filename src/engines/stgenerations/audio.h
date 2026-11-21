// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "audio/sound.h"
#include "audio/stream.h"
#include "common/stream.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace reluva::stgenerations {

// The game's sounds on Reluva's mixer, in the terms the game's code was
// written in (it came from STG-Engine): handles that stay valid after a
// sound ends, 0..255 volumes, -127..127 balance, and four kinds of sound.

/** A playing sound; still safe to use once the sound has ended. */
struct SoundHandle {
    audio::SoundHandle val = audio::kNoSound;
};

/**
 * A WAV file's sound, decoded, played from the start `loops` times (0:
 * until stopped).
 */
class SoundStream : public audio::Stream {
public:
    /** Plays `sound`, which streams playing the same sound may share. */
    SoundStream(std::shared_ptr<const audio::Sound> sound, int loops = 1);
    /** The audio::Stream: the sound's samples, over again until the loops are played. */
    ///@{
    int read(int16_t *out, int samples) override;
    bool stereo() const override { return _sound->channels == 2; }
    int rate() const override { return _sound->rate; }
    bool ended() const override;
    ///@}
    /** How many times it plays, counting those already played; 0 until stopped. */
    void setLoops(int loops) { _loops = loops; }
    /** The decoded sound. */
    const audio::Sound &sound() const { return *_sound; }
    /** Its length in milliseconds, once through. */
    uint32_t lengthMs() const;

private:
    std::shared_ptr<const audio::Sound> _sound;
    size_t _pos = 0;
    int _loops;
    int _played = 0;
};

/**
 * A WAV file as a stream, read from its start; nullptr if it isn't one
 * Reluva reads (mono or stereo).
 */
std::unique_ptr<SoundStream> makeWavStream(SeekableReadStream &file);
/** The same stream, played `loops` times (0: until stopped); nullptr stays nullptr. */
std::unique_ptr<audio::Stream> makeLooping(std::unique_ptr<SoundStream> stream, int loops);

/**
 * makeRawStream's flags. Without kRaw16Bits samples are 8-bit, signed
 * unless kRawUnsigned; 16-bit ones are signed, big-endian unless
 * kRawLittleEndian.
 */
enum { kRaw16Bits = 1 << 0, kRawLittleEndian = 1 << 1, kRawStereo = 1 << 2, kRawUnsigned = 1 << 3 };
/** Raw PCM (the movies' sound), `rate` frames a second. */
std::unique_ptr<audio::Stream> makeRawStream(std::vector<uint8_t> data, int rate, uint8_t flags);

/** What a WAV file's header says. */
struct WavInfo {
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    uint32_t dataSize = 0;
};
/**
 * Reads the format and the data's size from a WAV file's header, from its
 * start; false if it isn't a WAV file or has no format before its data.
 */
bool readWavInfo(SeekableReadStream &file, WavInfo &info);

/** The game's sounds on Reluva's mixer, which it doesn't own. */
class Mixer {
public:
    /** The kinds of sound, each with its own volume. The game plays no plain ones. */
    enum SoundType { kPlainSoundType, kMusicSoundType, kSFXSoundType, kSpeechSoundType };
    static constexpr int kMaxChannelVolume = 255;
    static constexpr int kMaxMixerVolume = 256;

    /** Plays on `mixer`, which must outlive it. */
    explicit Mixer(audio::Mixer &mixer) : _mixer(mixer) {}

    /**
     * Plays a stream, unless it's nullptr or sound is off; `handle` (if
     * given) is then a handle to nothing.
     * @param id Unused.
     * @param volume 0..255.
     * @param balance -127 (left) .. 127 (right).
     */
    void playStream(SoundType type, SoundHandle *handle, std::unique_ptr<audio::Stream> stream, int id = -1,
                    uint8_t volume = kMaxChannelVolume, int8_t balance = 0);
    /** Stops every sound. */
    void stopAll() { _mixer.stopAll(); }
    /** Stops, pauses or resumes a sound. */
    ///@{
    void stopHandle(SoundHandle handle) { _mixer.stop(handle.val); }
    void pauseHandle(SoundHandle handle, bool paused) { _mixer.setPaused(handle.val, paused); }
    ///@}
    /** Whether a sound, or any sound of a kind, is playing. */
    ///@{
    bool isSoundHandleActive(SoundHandle handle) const { return _mixer.isPlaying(handle.val); }
    bool isSoundTypeActive(SoundType type) const { return _mixer.isTypePlaying(toType(type)); }
    ///@}
    /** A playing sound's volume (0..255) and balance (-127 left .. 127 right). */
    ///@{
    void setChannelVolume(SoundHandle handle, uint8_t volume) { _mixer.setVolume(handle.val, toVolume(volume)); }
    void setChannelBalance(SoundHandle handle, int8_t balance) { _mixer.setPan(handle.val, balance); }
    ///@}
    /** A kind of sound's volume, 0..kMaxMixerVolume, as the player's settings set them. */
    ///@{
    void setVolumeForSoundType(SoundType type, int volume);
    int getVolumeForSoundType(SoundType type) const;
    ///@}

    /** Reluva's mixer underneath. */
    audio::Mixer &mixer() { return _mixer; }
    /** Off, nothing is played (there's no sound device to pull the mixer). */
    void setEnabled(bool on) { _enabled = on; }

private:
    static audio::SoundType toType(SoundType type);
    static int toVolume(int volume) { return (volume < 0 ? 0 : volume > 255 ? 255 : volume) * audio::kMaxVolume / 255; }

    audio::Mixer &_mixer;
    bool _enabled = true;
    int _typeVolume[4] = {kMaxMixerVolume, kMaxMixerVolume, kMaxMixerVolume, kMaxMixerVolume};
};

} // namespace reluva::stgenerations
