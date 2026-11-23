// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/miles.h"

#include "audio/mixer.h"
#include "audio/sound.h"
#include "audio/wav.h"

#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace reluva::x86 {

namespace {

const char *const kDll = "mss32.dll";

// A sample's format (AIL_set_sample_type): bits for 16-bit samples,
// stereo, and IMA ADPCM; its flags: 8-bit samples signed.
constexpr uint32_t kFormat16Bits = 0x1;
constexpr uint32_t kFormatStereo = 0x2;
constexpr uint32_t kFormatAdpcm = 0x4;
constexpr uint32_t kFlagSigned = 0x1;

// AIL_sample_status.
constexpr uint32_t kStatusDone = 2;
constexpr uint32_t kStatusPlaying = 4;

// Volumes and pans run 0 to 127; a pan of 64 is the middle.
constexpr int kMilesMaxVolume = 127;
constexpr int kMilesCentre = 64;

// What AIL_init_sample sets: 8-bit unsigned mono at 11,025 Hz, at full
// volume, in the middle.
constexpr int kDefaultRate = 11025;

// How much a buffer should hold at least, in seconds: as long as the
// mixer waits for before it starts a stream, and more.
constexpr double kMinimumBufferSeconds = 0.1;

} // namespace

struct Miles::State {
    State(Win32Process &p, Miles &m) : process(p), cpu(p.cpu()), owner(m) {}

    // A sample handle's state.
    struct Sample {
        uint32_t format = 0;
        uint32_t flags = 0;
        uint32_t adpcmBlock = 0;
        int rate = kDefaultRate;
        int volume = kMilesMaxVolume;
        int pan = kMilesCentre;
        std::map<uint32_t, int32_t> userData;
        // The two buffers: whether each is loaded and not yet heard, and
        // those loaded in the order they play, with their lengths in frames.
        bool loaded[2] = {false, false};
        struct Queued {
            int buffer;
            double frames;
        };
        std::deque<Queued> queue;
        int next = 0;         // the buffer AIL_sample_buffer_ready offers next
        double played = 0;    // frames of the first queued buffer heard
        uint32_t clockMs = 0; // the clock when `played` was last moved on
        // What the mixer plays, while the sample does.
        std::shared_ptr<audio::SoundStream> stream;
        audio::SoundHandle sound = audio::kNoSound;
        int streamRate = 0;
    };

    Win32Process &process;
    Cpu &cpu;
    Miles &owner;                       // its mixer, set after it's made
    std::map<uint32_t, Sample> samples; // by handle: a block of the process's memory
    int masterVolume = kMilesMaxVolume;
    uint32_t emptyText = 0; // "", for AIL_last_error
    uint32_t redistDir = 0; // the copy of what AIL_set_redist_directory was given last
    uint32_t driver = 0;    // the one driver AIL_waveOutOpen gives

    audio::Mixer *mixer() const { return owner.mixer; }
    uint32_t now() const { return process.milliseconds ? process.milliseconds() : 0; }

    Sample *sample(uint32_t handle) {
        auto it = samples.find(handle);
        return it == samples.end() ? nullptr : &it->second;
    }

    int mixerVolume(const Sample &s) const {
        return s.volume * masterVolume * audio::kMaxVolume / (kMilesMaxVolume * kMilesMaxVolume);
    }
    static int mixerPan(const Sample &s) {
        return std::clamp((s.pan - kMilesCentre) * audio::kMaxPan / (kMilesMaxVolume - kMilesCentre), -audio::kMaxPan,
                          audio::kMaxPan);
    }

    // Moves the play position on by the clock: each buffer heard to its end
    // comes free. A sample that ran out of buffers waits, its position not
    // moving, until another is loaded.
    void advance(Sample &s) const {
        const uint32_t t = now();
        if (s.queue.empty()) {
            s.clockMs = t;
            return;
        }
        s.played += double(t - s.clockMs) / 1000.0 * double(s.rate);
        s.clockMs = t;
        while (!s.queue.empty() && s.played >= s.queue.front().frames) {
            s.played -= s.queue.front().frames;
            s.loaded[s.queue.front().buffer] = false;
            s.queue.pop_front();
        }
        if (s.queue.empty())
            s.played = 0;
    }

    // Stops it at once: nothing queued, nothing playing.
    void stop(Sample &s) const {
        s.queue.clear();
        s.loaded[0] = s.loaded[1] = false;
        s.next = 0;
        s.played = 0;
        if (s.sound != audio::kNoSound && mixer())
            mixer()->stop(s.sound);
        if (s.stream)
            s.stream->end();
        s.stream.reset();
        s.sound = audio::kNoSound;
        s.streamRate = 0;
    }

    // A buffer's bytes as 16-bit samples, by the sample's format.
    bool decode(const Sample &s, uint32_t at, uint32_t size, int channels, std::vector<int16_t> &out) const {
        if (!cpu.valid(at, size))
            return false;
        const uint8_t *p = cpu.memory().data() + at;
        if (s.format & kFormatAdpcm) {
            audio::Sound sound;
            const size_t block = s.adpcmBlock ? s.adpcmBlock : size;
            if (audio::decodeImaAdpcm(p, size, channels, std::max(s.rate, 1), block, 0, sound))
                return false;
            out = std::move(sound.samples);
            return true;
        }
        if (s.format & kFormat16Bits) {
            out.resize(size / 2);
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = int16_t(uint16_t(p[2 * i] | p[2 * i + 1] << 8));
        } else {
            out.resize(size);
            for (size_t i = 0; i < size; ++i)
                out[i] = (s.flags & kFlagSigned) ? int16_t(int8_t(p[i]) * 256) : int16_t((int(p[i]) - 128) * 256);
        }
        out.resize(out.size() - out.size() % size_t(channels));
        return true;
    }

    // AIL_load_sample_buffer: the buffer queued to play after the others,
    // and handed to the mixer.
    void load(uint32_t handle, Sample &s, int buffer, uint32_t at, uint32_t size) const {
        advance(s);
        const int channels = (s.format & kFormatStereo) ? 2 : 1;
        std::vector<int16_t> pcm;
        if (!size || !decode(s, at, size, channels, pcm) || pcm.empty())
            return;
        if (s.queue.empty()) {
            s.played = 0;
            s.clockMs = now();
        }
        s.loaded[buffer] = true;
        s.queue.push_back({buffer, double(pcm.size() / size_t(channels))});
        s.next = buffer ^ 1;
        if (owner.bufferLoaded)
            owner.bufferLoaded(handle, pcm, s.rate, channels);
        audio::Mixer *m = mixer();
        if (!m)
            return;
        if (s.stream && (s.stream->channels() != channels || !m->isPlaying(s.sound))) {
            s.stream->end();
            s.stream.reset();
        }
        if (!s.stream) {
            s.stream = std::make_shared<audio::SoundStream>(s.rate, channels);
            s.streamRate = s.rate;
            s.sound = m->playStream(s.stream, audio::SoundType::kEffects, mixerVolume(s), mixerPan(s));
        }
        s.stream->push(pcm.data(), pcm.size());
    }

    void updateMixer(const Sample &s) const {
        audio::Mixer *m = mixer();
        if (!m || s.sound == audio::kNoSound)
            return;
        m->setVolume(s.sound, mixerVolume(s));
        m->setPan(s.sound, mixerPan(s));
        if (s.streamRate)
            m->setRate(s.sound, double(s.rate) / double(s.streamRate));
    }

    // A copy of a string in the process's memory, for a function that
    // returns one.
    uint32_t text(const std::string &t) {
        const uint32_t at = process.memory().alloc(uint32_t(t.size() + 1));
        if (at) {
            std::copy(t.begin(), t.end(), cpu.memory().data() + at);
            cpu.memory()[at + t.size()] = 0;
        }
        return at;
    }
};

Miles::~Miles() = default;

Miles::Miles(Win32Process &process) : _s(std::make_unique<State>(process, *this)) {
    State *s = _s.get();
    // Each by its decorated name, as the programs import it: its arguments'
    // bytes after the @.
    auto fn = [&process](const char *name, std::function<uint32_t(Cpu &)> body) {
        const std::string n = name;
        const int args = std::stoi(n.substr(n.rfind('@') + 1)) / 4;
        process.provideStdcall(kDll, n, args, std::move(body));
    };
    auto sampleArg = [s](Cpu &c) { return s->sample(c.arg(0)); };

    fn("_AIL_startup@0", [](Cpu &) { return 1u; });
    fn("_AIL_shutdown@0", [s](Cpu &) {
        for (auto &[handle, sample] : s->samples)
            s->stop(sample);
        return 0u;
    });
    // Where Miles' own DLLs are: none are loaded. Returns the folder named
    // before.
    fn("_AIL_set_redist_directory@4", [s](Cpu &c) {
        const uint32_t before = s->redistDir ? s->redistDir : s->text("");
        s->redistDir = s->text(c.valid(c.arg(0), 1) ? c.string(c.arg(0), 260) : std::string());
        return before;
    });
    fn("_AIL_last_error@0", [s](Cpu &) {
        if (!s->emptyText)
            s->emptyText = s->text("");
        return s->emptyText;
    });
    fn("_AIL_serve@0", [](Cpu &) { return 0u; });
    fn("_AIL_mem_alloc_lock@4", [s](Cpu &c) { return s->process.memory().alloc(std::max(c.arg(0), 1u)); });
    fn("_AIL_mem_free_lock@4", [s](Cpu &c) {
        s->process.memory().free(c.arg(0));
        return 0u;
    });

    // AIL_waveOutOpen(&driver, &waveOut, device, format): 0, and a driver,
    // whatever the format asked for; the mixer plays each sample at its own.
    fn("_AIL_waveOutOpen@16", [s](Cpu &c) {
        if (!s->driver)
            s->driver = s->process.memory().alloc(16);
        if (c.valid(c.arg(0), 4))
            c.write32(c.arg(0), s->driver);
        if (c.arg(1) && c.valid(c.arg(1), 4))
            c.write32(c.arg(1), 0);
        return 0u;
    });
    fn("_AIL_waveOutClose@4", [](Cpu &) { return 0u; });
    fn("_AIL_set_DirectSound_HWND@8", [](Cpu &) { return 0u; });
    fn("_AIL_set_digital_master_volume@8", [s](Cpu &c) {
        s->masterVolume = std::clamp(int32_t(c.arg(1)), 0, kMilesMaxVolume);
        for (auto &[handle, sample] : s->samples)
            s->updateMixer(sample);
        return 0u;
    });
    // AIL_minimum_sample_buffer_size(driver, rate, format): a tenth of a
    // second of it, in whole 256-byte pieces.
    fn("_AIL_minimum_sample_buffer_size@12", [](Cpu &c) {
        const uint32_t rate = std::max(c.arg(1), 1u);
        const uint32_t format = c.arg(2);
        const uint32_t channels = (format & kFormatStereo) ? 2 : 1;
        // IMA ADPCM: 4 bits a sample.
        const double bytesPerFrame =
            (format & kFormatAdpcm) ? 0.5 * channels : double(channels * ((format & kFormat16Bits) ? 2 : 1));
        const auto bytes = uint32_t(double(rate) * kMinimumBufferSeconds * bytesPerFrame);
        return (bytes + 255) & ~255u;
    });

    fn("_AIL_allocate_sample_handle@4", [s](Cpu &) {
        const uint32_t handle = s->process.memory().alloc(16);
        if (handle)
            s->samples[handle] = State::Sample{};
        return handle;
    });
    fn("_AIL_release_sample_handle@4", [s, sampleArg](Cpu &c) {
        if (State::Sample *sample = sampleArg(c)) {
            s->stop(*sample);
            s->samples.erase(c.arg(0));
            s->process.memory().free(c.arg(0));
        }
        return 0u;
    });
    fn("_AIL_init_sample@4", [s, sampleArg](Cpu &c) {
        if (State::Sample *sample = sampleArg(c)) {
            s->stop(*sample);
            *sample = State::Sample{};
        }
        return 0u;
    });
    fn("_AIL_end_sample@4", [s, sampleArg](Cpu &c) {
        if (State::Sample *sample = sampleArg(c))
            s->stop(*sample);
        return 0u;
    });
    fn("_AIL_set_sample_type@12", [sampleArg](Cpu &c) {
        if (State::Sample *sample = sampleArg(c))
            sample->format = c.arg(1), sample->flags = c.arg(2);
        return 0u;
    });
    fn("_AIL_set_sample_adpcm_block_size@8", [sampleArg](Cpu &c) {
        if (State::Sample *sample = sampleArg(c))
            sample->adpcmBlock = c.arg(1);
        return 0u;
    });
    fn("_AIL_set_sample_playback_rate@8", [s, sampleArg](Cpu &c) {
        if (State::Sample *sample = sampleArg(c)) {
            s->advance(*sample);
            sample->rate = std::max(int32_t(c.arg(1)), 1);
            s->updateMixer(*sample);
        }
        return 0u;
    });
    fn("_AIL_set_sample_volume@8", [s, sampleArg](Cpu &c) {
        if (State::Sample *sample = sampleArg(c)) {
            sample->volume = std::clamp(int32_t(c.arg(1)), 0, kMilesMaxVolume);
            s->updateMixer(*sample);
        }
        return 0u;
    });
    fn("_AIL_set_sample_pan@8", [s, sampleArg](Cpu &c) {
        if (State::Sample *sample = sampleArg(c)) {
            sample->pan = std::clamp(int32_t(c.arg(1)), 0, kMilesMaxVolume);
            s->updateMixer(*sample);
        }
        return 0u;
    });
    fn("_AIL_set_sample_user_data@12", [sampleArg](Cpu &c) {
        if (State::Sample *sample = sampleArg(c))
            sample->userData[c.arg(1)] = int32_t(c.arg(2));
        return 0u;
    });
    // AIL_sample_buffer_ready: the buffer to fill next, as the two take
    // turns; -1 while it's still to be heard.
    fn("_AIL_sample_buffer_ready@4", [s, sampleArg](Cpu &c) {
        State::Sample *sample = sampleArg(c);
        if (!sample)
            return 0xffffffffu;
        s->advance(*sample);
        return sample->loaded[sample->next] ? 0xffffffffu : uint32_t(sample->next);
    });
    // AIL_load_sample_buffer(sample, buffer, data, bytes): starts it
    // playing, or goes on after what's queued.
    fn("_AIL_load_sample_buffer@16", [s, sampleArg](Cpu &c) {
        if (State::Sample *sample = sampleArg(c); sample && c.arg(1) < 2)
            s->load(c.arg(0), *sample, int(c.arg(1)), c.arg(2), c.arg(3));
        return 0u;
    });
    fn("_AIL_sample_status@4", [s, sampleArg](Cpu &c) {
        State::Sample *sample = sampleArg(c);
        if (!sample)
            return kStatusDone;
        s->advance(*sample);
        return sample->queue.empty() ? kStatusDone : kStatusPlaying;
    });
}

size_t Miles::samplesAllocated() const {
    return _s->samples.size();
}

size_t Miles::samplesPlaying() const {
    size_t n = 0;
    for (auto &[handle, sample] : _s->samples) {
        _s->advance(sample);
        n += !sample.queue.empty();
    }
    return n;
}

} // namespace reluva::x86
