// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "audio/stream.h"
#include "common/system.h"
#include "common/types.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace reluva::audio {

/**
 * What a sound is, for the per-type volumes (a game's music, speech and
 * effects settings).
 */
enum class SoundType { kMusic, kSpeech, kEffects };
/** How many SoundTypes there are. */
constexpr int kSoundTypes = 3;

/** A playing sound, as returned by Mixer::play; 0 (kNoSound) is never one. */
using SoundHandle = uint32_t;
/** The handle of no sound: what Mixer::play gives for one it can't play. */
constexpr SoundHandle kNoSound = 0;

/** A sound's volume as recorded: volumes run 0 (silent) to this. */
constexpr int kMaxVolume = 256;
/**
 * The player's volumes (the master volume and each type's) go up to twice
 * as loud; kMaxVolume, the default, plays sounds as they were recorded.
 */
constexpr int kMaxBoostVolume = 2 * kMaxVolume;
/** Pan runs -kMaxPan (left) to kMaxPan (right). */
constexpr int kMaxPan = 127;

/**
 * Sound made as it plays (a program filling a buffer as it's heard): its
 * producer adds samples, the mixer takes them. Unlike a QueuingStream
 * (stream.h), which plays silence when it runs dry, the mixer waits for a
 * little of it to be queued first, and again whenever it runs dry, so a
 * producer a little late isn't heard as gaps (Mixer::playStream).
 * Thread-safe.
 */
class SoundStream {
public:
    /** A stream of `channels` (1 or 2) at `rate` frames a second. */
    SoundStream(int rate, int channels) : _rate(rate), _channels(channels) {}
    /** Its frames a second and its channels. */
    ///@{
    int rate() const { return _rate; }
    int channels() const { return _channels; }
    ///@}
    /** Adds `count` interleaved samples (whole frames). */
    void push(const int16_t *samples, size_t count);
    /** Says no more will come: the stream ends once what's queued has played. */
    void end();
    /** Frames queued and not yet played. */
    size_t queued() const;

private:
    friend class Mixer;
    const int _rate;
    const int _channels;
    mutable std::mutex _mutex;
    std::vector<int16_t> _samples; // queued, oldest first
    size_t _read = 0;              // the _samples before this have been taken
    bool _ended = false;
};

/**
 * Sound made as it plays at the output's rate (an emulated sound card's
 * mix of its buffers, say), pulled by the mixer on the audio thread
 * (Mixer::playOutput): it resamples, as such a card would, and plays until
 * stopped.
 */
class OutputStream {
public:
    virtual ~OutputStream() = default;
    /**
     * Fills `frames` stereo frames (interleaved signed 16-bit) at `rate`
     * frames a second. Runs on the audio thread.
     */
    virtual void read(int16_t *out, int frames, int rate) = 0;
};

/**
 * Sounds played one after another with nothing between them: a queue
 * the engine fills (ahead of time) while the mixer reads it. Each sound's
 * last frame leads into the next one's first, so music in pieces plays as
 * one. Each sound is tagged with a number of the engine's, for telling
 * which is playing. Silence when it's empty. Thread-safe.
 */
class SoundQueue : public OutputStream {
public:
    /** Adds a sound after the last one queued; the queue shares it. */
    void push(std::shared_ptr<const Sound> sound, int tag);
    /**
     * The tag of the sound playing (-1: none left), and how many sounds are
     * queued, the one playing among them.
     */
    ///@{
    int playing() const;
    size_t queued() const;
    ///@}
    void read(int16_t *out, int frames, int rate) override;

private:
    struct Entry {
        std::shared_ptr<const Sound> sound;
        int tag;
    };
    mutable std::mutex _mutex;
    std::deque<Entry> _queue;
    double _position = 0; // in the first sound's frames
};

/**
 * Mixes any number of decoded sounds and streams into the System's stereo
 * output: each resampled (linearly) to the output rate, scaled by its own
 * volume and pan, its type's volume and the master volume, summed and
 * clamped to 16 bits. Shared by every engine. Thread-safe: mix() runs on
 * the audio thread while the engine plays and stops sounds; a finished
 * sound's channel is freed by the next mix().
 *
 * The setters ignore a handle that isn't playing, and the getters give it
 * as stopped, not paused, at 0.
 */
class Mixer {
public:
    /** A mixer for output at `outputRate` frames a second (at least 1). */
    explicit Mixer(int outputRate);

    /**
     * Starts a sound, sharing it. A looping sound plays until stopped.
     * @param volume 0 to kMaxVolume.
     * @param pan    -kMaxPan (left) to kMaxPan (right).
     * @return kNoSound for one it can't play: empty, without a rate, or of more than 2 channels.
     */
    SoundHandle play(std::shared_ptr<const Sound> sound, SoundType type, int volume = kMaxVolume, int pan = 0,
                     bool loop = false);
    /**
     * Starts a stream (stream.h), pulled as it plays; it ends when the
     * stream does, or is stopped. The mixer keeps a reference: the engine
     * may keep one too, to feed it.
     */
    SoundHandle play(std::shared_ptr<Stream> stream, SoundType type, int volume = kMaxVolume, int pan = 0);
    /**
     * Starts a SoundStream. It waits until `prebufferMs` of it are queued,
     * and again whenever it runs dry.
     */
    SoundHandle playStream(std::shared_ptr<SoundStream> stream, SoundType type, int volume = kMaxVolume, int pan = 0,
                           int prebufferMs = 100);
    /** Starts an OutputStream, which plays until stopped. */
    SoundHandle playOutput(std::shared_ptr<OutputStream> stream, SoundType type, int volume = kMaxVolume);
    /** Stops a sound, every sound of a type, or every sound. */
    ///@{
    void stop(SoundHandle handle);
    void stopType(SoundType type);
    void stopAll();
    ///@}

    /** Whether a sound, or any of a type, is playing (paused ones count). */
    ///@{
    bool isPlaying(SoundHandle handle) const;
    bool isTypePlaying(SoundType type) const;
    ///@}
    /**
     * How far a sound (or a stream) has played, in its own frames (0 if it
     * isn't playing).
     */
    double position(SoundHandle handle) const;

    /**
     * A paused sound keeps its place and plays on from there when
     * unpaused; it still counts as playing.
     */
    ///@{
    void setPaused(SoundHandle handle, bool paused);
    bool isPaused(SoundHandle handle) const;
    ///@}
    /**
     * Every sound held where it is (the game paused), apart from each
     * sound's own pause; mix() gives silence meanwhile.
     */
    ///@{
    void setAllPaused(bool paused);
    bool allPaused() const;
    ///@}

    /** A sound's volume (0 to kMaxVolume) and pan (-kMaxPan to kMaxPan), clamped. */
    ///@{
    void setVolume(SoundHandle handle, int volume);
    void setPan(SoundHandle handle, int pan);
    ///@}
    /**
     * Plays a decoded sound faster or slower (and higher or lower) by
     * `factor`, clamped to 0.05-20. Streams play at their own rate.
     */
    void setRate(SoundHandle handle, double factor);
    /**
     * How muffled a decoded sound is, 0 (as recorded) to 1: a low-pass
     * filter whose cutoff falls to about a kilohertz (a sound heard through
     * a wall).
     */
    void setMuffle(SoundHandle handle, float muffle);
    /** The filters setFilter puts on a sound. */
    enum class Filter { kNone, kLowPass, kHighPass };
    /**
     * A 2nd-order Butterworth filter on a decoded sound: low-pass or
     * high-pass at `hz` (clamped to 20 Hz - half the output rate); kind
     * kNone removes it.
     */
    void setFilter(SoundHandle handle, Filter kind, float hz);
    /**
     * A reverb on a decoded sound (a Schroeder reverb: four combs, two
     * all-passes), `mix` of it wet (0 none): `seconds` for its tail to fall
     * 60 dB, the wet sound low-passed at `dampHz`, after `predelay` seconds
     * (0-0.25). `tail`: how long it goes on through silence once the sound
     * has ended (not looping), its echoes dying away.
     */
    struct Reverb {
        float mix = 0;
        float seconds = 3;
        float dampHz = 5000;
        float predelay = 0;
        float tail = 0;
    };
    /** Puts the reverb on a sound, or takes it off (a `mix` of 0 or less). */
    void setReverb(SoundHandle handle, const Reverb &reverb);
    /**
     * Moves a decoded sound to `frame` of its own frames (past the end: it
     * ends, or wraps if it loops).
     */
    void seek(SoundHandle handle, double frame);
    /**
     * A type's volume (the player's music, speech and effects settings),
     * 0 to kMaxBoostVolume.
     */
    ///@{
    void setTypeVolume(SoundType type, int volume);
    int typeVolume(SoundType type) const;
    ///@}
    /** Every sound's volume, 0 to kMaxBoostVolume (kMaxVolume by default). */
    ///@{
    void setMasterVolume(int volume);
    int masterVolume() const;
    ///@}
    /**
     * The whole mix towards the left (-kMaxPan) or right (kMaxPan): the
     * far side quieter, linearly (a game's balance setting).
     */
    void setBalance(int balance);

    /** The output's frames a second. */
    int outputRate() const { return _outputRate; }

    /**
     * Fills `frames` stereo frames (interleaved signed 16-bit) with every
     * playing sound; silence when none is. The System's audio callback.
     */
    void mix(int16_t *out, int frames);

private:
    struct Channel {
        SoundHandle handle;
        std::shared_ptr<const Sound> sound; // a decoded sound, or the stream below
        std::shared_ptr<Stream> stream;
        SoundType type;
        int volume;
        int pan;
        bool loop;
        bool paused = false;
        double position = 0; // in the sound's frames
        float muffle = 0;
        // the low-pass filter's last output
        double lowL = 0;
        double lowR = 0;
        bool done = false;
        // A stream's: the frames either side of `position`, and the next.
        int16_t cur[2] = {0, 0};
        int16_t next[2] = {0, 0};
        bool primed = false;
        // A SoundStream's (playStream), instead of a sound or a stream.
        std::shared_ptr<SoundStream> queue = nullptr;
        size_t prebuffer = 0; // frames it waits for
        bool waiting = true;  // it has run dry
        // An OutputStream's (playOutput), instead of the others.
        std::shared_ptr<OutputStream> output = nullptr;
        double rate = 1; // a factor on a sound's own rate (setRate)
        /** A biquad's coefficients and its state per side (x1 x2 y1 y2). */
        struct Biquad {
            double a0 = 1;
            double a1 = 0;
            double a2 = 0;
            double b1 = 0;
            double b2 = 0;
            double state[2][4] = {};
            /** One sample through the filter, on side 0 (left) or 1 (right). */
            double run(int side, double x);
        };
        Filter filter = Filter::kNone;
        float filterHz = 0;
        Biquad biquad{};
        /** A reverb's settings, and its filter's and delay lines' state. */
        struct ReverbState {
            Reverb params;
            Biquad damp;
            std::vector<double> pre;
            size_t preAt = 0;
            std::vector<double> comb[4];
            size_t combAt[4] = {};
            double combGain[4] = {};
            std::vector<double> pass1;
            std::vector<double> pass2[2];
            size_t pass1At = 0;
            size_t pass2At[2] = {};
        };
        std::shared_ptr<ReverbState> reverb{}; // none: dry
        double tailLeft = -1;                  // output frames of reverb tail still to go (-1: not begun)
    };
    void mixOutput(Channel &c, int frames, int64_t gain);
    std::vector<int16_t> _outputBuffer; // an OutputStream's frames, before mixing
    void mixSound(Channel &c, int frames, int64_t gain);
    void mixStream(Channel &c, int frames, int64_t gain);
    void mixQueue(Channel &c, int frames, int64_t gain);
    static bool readFrame(Stream &s, int16_t frame[2]);
    void setUpReverb(Channel &c, const Reverb &r) const;
    static void reverbFrame(Channel::ReverbState &rv, double &l, double &r);
    Channel *find(SoundHandle handle);
    const Channel *find(SoundHandle handle) const;

    const int _outputRate;
    mutable std::mutex _mutex;
    std::vector<Channel> _channels;
    SoundHandle _nextHandle = 1;
    int _typeVolume[kSoundTypes] = {kMaxVolume, kMaxVolume, kMaxVolume};
    int _masterVolume = kMaxVolume;
    int _balance = 0;
    bool _allPaused = false;
    std::vector<int32_t> _accumulator; // mix()'s working buffer, stereo
};

/**
 * Starts the System's audio output with the mixer as its source. The mixer
 * must outlive the output: call sys.stopAudio() before destroying it.
 */
Error startMixerOutput(System &sys, Mixer &mixer);

/**
 * What a Pro Logic decoder model (prologic.h) makes of the mixer's stereo
 * on the way out; an engine's surround= setting.
 */
enum class Surround {
    kOff,        // stereo, as mixed (the default; any other setting value)
    kProLogic,   // "prologic": decoded, to a 4.0 or 5.1 device when the default has 4 channels or more
    kPassive,    // "passive": as ProLogic with the passive matrix alone
    kHeadphones, // "headphones": decoded and folded back to stereo
};
/** The Surround a surround= setting's value names (kOff for any other). */
Surround surroundFromSetting(const std::string &value);

/**
 * startMixerOutput through the decoder. ProLogic and Passive open 6
 * channels when the device has 6 or more, 4 when it has 4 or 5, and fall
 * back to plain stereo otherwise, or if the device won't open with more.
 * `channels`, when given, gets the count opened.
 */
Error startMixerOutput(System &sys, Mixer &mixer, Surround surround, int *channels = nullptr);

} // namespace reluva::audio
