// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectSound, from the DirectX SDK's documentation (dsound.h's interfaces
// and their methods' order, its structures and codes): see
// docs/common/x86/dsound.md.

#include "emulation/x86/dsound.h"

#include "audio/mixer.h"
#include "audio/sound.h"
#include "audio/wav.h"
#include "emulation/x86/com.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::x86 {

namespace {

constexpr uint32_t kDsOk = 0;
constexpr uint32_t kDsErrInvalidParam = 0x80070057u;
constexpr uint32_t kDsErrNoAggregation = 0x80040110u;
// MAKE_DSHRESULT(code): 0x88780000 + code.
constexpr uint32_t kDsErrControlUnavail = 0x8878001eu;     // 30
constexpr uint32_t kDsErrInvalidCall = 0x88780032u;        // 50
constexpr uint32_t kDsErrPrioLevelNeeded = 0x88780046u;    // 70
constexpr uint32_t kDsErrBadFormat = 0x88780064u;          // 100
constexpr uint32_t kDsErrNoDriver = 0x88780078u;           // 120
constexpr uint32_t kDsErrAlreadyInitialized = 0x88780082u; // 130

// DSBCAPS_*, a buffer's flags.
constexpr uint32_t kPrimary = 0x1;
constexpr uint32_t kLocSoftware = 0x8;
constexpr uint32_t kCtrlFrequency = 0x20;
constexpr uint32_t kCtrlPan = 0x40;
constexpr uint32_t kCtrlVolume = 0x80;
// DSBLOCK_*, DSBPLAY_LOOPING and DSBSTATUS_*.
constexpr uint32_t kLockFromWriteCursor = 0x1;
constexpr uint32_t kLockEntireBuffer = 0x2;
constexpr uint32_t kPlayLooping = 0x1;
constexpr uint32_t kStatusPlaying = 0x1;
constexpr uint32_t kStatusLooping = 0x4;
// DSSCL_WRITEPRIMARY: the only level that lets the program write the primary buffer.
constexpr uint32_t kWritePrimary = 4;
// DSBFREQUENCY_MIN and _MAX, and DSBSIZE_MIN and _MAX.
constexpr uint32_t kMinRate = 100;
constexpr uint32_t kMaxRate = 100000;
constexpr uint32_t kMinSize = 4;
constexpr uint32_t kMaxSize = 0x0fffffff;

const char *const kIidDirectSound = "{279afa83-4981-11ce-a521-0020af0be560}";
const char *const kIidDirectSoundBuffer = "{279afa85-4981-11ce-a521-0020af0be560}";
// Reluva's one device, as DirectSoundEnumerate names it after the primary
// driver: a GUID of its own, for programs that only take a device that has one.
const char *const kDeviceGuid = "{8c8b2a40-4e3b-4b2f-9a35-52454c555641}";

using Methods = std::vector<ComInterface::Method>;

Methods withUnknown(Methods m) {
    m.insert(m.begin(), {{"QueryInterface", 2}, {"AddRef", 0}, {"Release", 0}});
    return m;
}

Methods directSound() {
    return withUnknown({{"CreateSoundBuffer", 3},
                        {"GetCaps", 1},
                        {"DuplicateSoundBuffer", 2},
                        {"SetCooperativeLevel", 2},
                        {"Compact", 0},
                        {"GetSpeakerConfig", 1},
                        {"SetSpeakerConfig", 1},
                        {"Initialize", 1}});
}

Methods soundBuffer() {
    return withUnknown({{"GetCaps", 1},
                        {"GetCurrentPosition", 2},
                        {"GetFormat", 3},
                        {"GetVolume", 1},
                        {"GetPan", 1},
                        {"GetFrequency", 1},
                        {"GetStatus", 1},
                        {"Initialize", 2},
                        {"Lock", 7},
                        {"Play", 3},
                        {"SetCurrentPosition", 1},
                        {"SetFormat", 1},
                        {"SetVolume", 1},
                        {"SetPan", 1},
                        {"SetFrequency", 1},
                        {"Stop", 0},
                        {"Unlock", 4},
                        {"Restore", 0}});
}

// Volume and pan are attenuations in hundredths of a decibel, 0 down to
// -10000 (DSBVOLUME_MIN, silence).
double gain(int32_t hundredths) {
    if (hundredths <= -10000)
        return 0;
    return std::pow(10.0, std::min(hundredths, 0) / 2000.0);
}

// WAVEFORMATEX, PCM only.
struct Format {
    uint32_t rate = 22050;
    uint16_t channels = 2;
    uint16_t bits = 8;
    uint16_t blockAlign() const { return uint16_t(channels * bits / 8); }
};

// Reads a WAVEFORMATEX; nothing if it isn't PCM DirectSound can play.
std::optional<Format> readFormat(Cpu &c, uint32_t at) {
    if (!c.valid(at, 16) || c.read16(at) != 1)
        return std::nullopt;
    Format f;
    f.channels = c.read16(at + 2);
    f.rate = c.read32(at + 4);
    f.bits = c.read16(at + 14);
    if (f.channels < 1 || f.channels > 2 || (f.bits != 8 && f.bits != 16) || f.rate < kMinRate || f.rate > kMaxRate)
        return std::nullopt;
    return f;
}

void writeFormat(Cpu &c, uint32_t at, const Format &f) {
    c.write16(at, 1);
    c.write16(at + 2, f.channels);
    c.write32(at + 4, f.rate);
    c.write32(at + 8, f.rate * f.blockAlign());
    c.write16(at + 12, f.blockAlign());
    c.write16(at + 14, f.bits);
    c.write16(at + 16, 0);
}

// A buffer's sound in the process's memory, shared by its duplicates.
struct Block {
    Win32Process &process;
    uint32_t address = 0;
    uint32_t size = 0;
    Block(Win32Process &p, uint32_t bytes) : process(p), address(p.memory().alloc(bytes)), size(bytes) {}
    Block(const Block &) = delete;
    Block &operator=(const Block &) = delete;
    ~Block() {
        if (address)
            process.memory().free(address);
    }
};

class Buffer;

} // namespace

struct DirectSound::State {
    Win32Process &process;
    Cpu &cpu;
    // Held by the audio thread while it mixes, and by the program's
    // thread while it changes a buffer.
    std::mutex mutex;
    std::set<Buffer *> buffers; // the secondary ones alive
    uint32_t cooperativeLevel = 0;
    uint32_t speakerConfig = 0;
    ComInterface sound{"IDirectSound", kIidDirectSound, directSound()};
    ComInterface buffer{"IDirectSoundBuffer", kIidDirectSoundBuffer, soundBuffer()};
    std::vector<float> mixed; // the mix before it's clipped
    // PlaySound's wave, one at a time, and the mix's place in it in frames.
    std::shared_ptr<const audio::Sound> wave;
    double wavePos = 0;
    bool waveLoops = false;
    // Last, so its objects go while the rest is still here.
    ComHost com;
    explicit State(Win32Process &p) : process(p), cpu(p.cpu()), com(p) {}
    uint32_t now() const { return process.milliseconds ? process.milliseconds() : 0; }
    void mix(int16_t *out, int frames, int rate);
};

namespace {

class Buffer : public ComObject {
public:
    DirectSound::State *s = nullptr;
    bool primary = false;
    uint32_t flags = 0;
    Format format;
    std::shared_ptr<Block> block;
    uint32_t frequency = 0; // 0: the format's rate
    int32_t volume = 0;
    int32_t pan = 0;
    bool playing = false;
    bool looping = false;
    // Where it was when last started, moved or stopped, and when.
    uint32_t startPos = 0;
    uint32_t startMs = 0;
    // The mix's own place in the sound, in frames, kept to the clock's.
    double heard = 0;
    bool heardSet = false;

    ~Buffer() override {
        if (primary)
            return;
        std::lock_guard<std::mutex> lock(s->mutex);
        s->buffers.erase(this);
    }

    const ComInterface *query(const std::string &iid) override { return iid == s->buffer.iid ? &s->buffer : nullptr; }

    uint32_t size() const { return block ? block->size : 0; }
    uint32_t rate() const { return frequency ? frequency : format.rate; }

    // The play position in bytes, a whole frame, by the process's clock.
    // A buffer played once stops at its end, and is back at its start.
    uint32_t position() {
        if (!playing || !size())
            return startPos;
        const uint32_t align = std::max<uint32_t>(format.blockAlign(), 1);
        const uint64_t frames = uint64_t(s->now() - startMs) * rate() / 1000;
        const uint64_t at = startPos + frames * align;
        if (looping)
            return uint32_t(at % size());
        if (at >= size()) {
            playing = false;
            startPos = 0;
            return 0;
        }
        return uint32_t(at);
    }
    // Whether it's playing now: one played once may have ended by the clock.
    bool isPlaying() {
        position();
        return playing;
    }
    // Starts the clock again from where it is, for a change of rate or place.
    void anchor() {
        startPos = position();
        startMs = s->now();
        heardSet = false;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        std::lock_guard<std::mutex> lock(s->mutex);
        // GetCaps(DSBCAPS: size, flags, buffer bytes, transfer rate, CPU overhead).
        if (m == "GetCaps") {
            if (!c.valid(c.arg(1), 20) || c.read32(c.arg(1)) < 20)
                return kDsErrInvalidParam;
            c.write32(c.arg(1) + 4, flags | kLocSoftware);
            c.write32(c.arg(1) + 8, size());
            c.write32(c.arg(1) + 12, 0);
            c.write32(c.arg(1) + 16, 0);
            return kDsOk;
        }
        // GetCurrentPosition(*play, *write): the program may write from
        // the write cursor on, a little ahead of what's being heard.
        if (m == "GetCurrentPosition") {
            const uint32_t play = position();
            uint32_t write = play;
            if (playing && size()) {
                const uint32_t align = std::max<uint32_t>(format.blockAlign(), 1);
                write = (play + rate() * align / 100 / align * align) % size(); // 10 ms on
            }
            if (c.arg(1) && c.valid(c.arg(1), 4))
                c.write32(c.arg(1), play);
            if (c.arg(2) && c.valid(c.arg(2), 4))
                c.write32(c.arg(2), write);
            return kDsOk;
        }
        // GetFormat(format, room, *written).
        if (m == "GetFormat") {
            if (c.arg(1) && c.arg(2) >= 18 && c.valid(c.arg(1), 18))
                writeFormat(c, c.arg(1), format);
            else if (c.arg(1))
                return kDsErrInvalidParam;
            if (c.arg(3) && c.valid(c.arg(3), 4))
                c.write32(c.arg(3), 18);
            return kDsOk;
        }
        if (m == "GetVolume" || m == "GetPan" || m == "GetFrequency" || m == "GetStatus") {
            if (!c.valid(c.arg(1), 4))
                return kDsErrInvalidParam;
            uint32_t v = 0;
            if (m == "GetVolume")
                v = uint32_t(volume);
            else if (m == "GetPan")
                v = uint32_t(pan);
            else if (m == "GetFrequency")
                v = rate();
            else if (isPlaying())
                v = kStatusPlaying | (looping ? kStatusLooping : 0);
            c.write32(c.arg(1), v);
            return kDsOk;
        }
        if (m == "Initialize")
            return kDsErrAlreadyInitialized;
        // Lock(offset, bytes, *part1, *bytes1, *part2, *bytes2, flags): the
        // memory itself, in one part or two where it wraps round.
        if (m == "Lock") {
            if (primary)
                return s->cooperativeLevel < kWritePrimary ? kDsErrPrioLevelNeeded : kDsErrInvalidCall;
            uint32_t offset = c.arg(1);
            uint32_t bytes = c.arg(2);
            if (c.arg(7) & kLockFromWriteCursor) {
                const uint32_t play = position();
                const uint32_t align = std::max<uint32_t>(format.blockAlign(), 1);
                offset = playing ? (play + rate() * align / 100 / align * align) % size() : play;
            }
            if (c.arg(7) & kLockEntireBuffer)
                bytes = size();
            if (offset >= size() || bytes == 0 || bytes > size() || !c.valid(c.arg(3), 4) || !c.valid(c.arg(4), 4))
                return kDsErrInvalidParam;
            const uint32_t first = std::min(bytes, size() - offset);
            c.write32(c.arg(3), block->address + offset);
            c.write32(c.arg(4), first);
            if (c.arg(5) && c.valid(c.arg(5), 4))
                c.write32(c.arg(5), first < bytes ? block->address : 0);
            if (c.arg(6) && c.valid(c.arg(6), 4))
                c.write32(c.arg(6), c.arg(5) ? bytes - first : 0);
            return kDsOk;
        }
        if (m == "Unlock" || m == "Restore")
            return kDsOk;
        // Play(reserved, priority, flags).
        if (m == "Play") {
            if (primary) {
                playing = true;
                return kDsOk;
            }
            if (!playing) {
                startMs = s->now();
                heardSet = false;
            }
            playing = size() > 0;
            looping = c.arg(3) & kPlayLooping;
            return kDsOk;
        }
        if (m == "Stop") {
            if (!primary)
                startPos = position();
            playing = false;
            return kDsOk;
        }
        if (m == "SetCurrentPosition") {
            if (primary || c.arg(1) >= size())
                return primary ? kDsErrInvalidCall : kDsErrInvalidParam;
            startPos = c.arg(1) - c.arg(1) % std::max<uint32_t>(format.blockAlign(), 1);
            startMs = s->now();
            heardSet = false;
            return kDsOk;
        }
        // SetFormat: the primary buffer's, which only says how the
        // program would like the output mixed; Reluva mixes as it does.
        if (m == "SetFormat") {
            if (!primary)
                return kDsErrInvalidCall;
            if (s->cooperativeLevel < 2) // DSSCL_PRIORITY
                return kDsErrPrioLevelNeeded;
            const auto f = readFormat(c, c.arg(1));
            if (!f)
                return kDsErrBadFormat;
            format = *f;
            return kDsOk;
        }
        if (m == "SetVolume" || m == "SetPan") {
            const int32_t v = int32_t(c.arg(1));
            if (!(flags & (m == "SetVolume" ? kCtrlVolume : kCtrlPan)))
                return kDsErrControlUnavail;
            if (v < -10000 || v > (m == "SetVolume" ? 0 : 10000))
                return kDsErrInvalidParam;
            (m == "SetVolume" ? volume : pan) = v;
            return kDsOk;
        }
        if (m == "SetFrequency") {
            if (!(flags & kCtrlFrequency) || primary)
                return kDsErrControlUnavail;
            const uint32_t f = c.arg(1);
            if (f && (f < kMinRate || f > kMaxRate))
                return kDsErrInvalidParam;
            anchor();
            frequency = f;
            return kDsOk;
        }
        return std::nullopt;
    }
};

class Sound : public ComObject {
public:
    DirectSound::State *s = nullptr;
    std::weak_ptr<Buffer> primary; // one, however often it's asked for

    const ComInterface *query(const std::string &iid) override { return iid == s->sound.iid ? &s->sound : nullptr; }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // CreateSoundBuffer(DSBUFFERDESC, *buffer, outer). The description
        // is 20 bytes (DirectX 6 and before) or 36 (7's, with a 3D algorithm).
        if (m == "CreateSoundBuffer") {
            const uint32_t desc = c.arg(1);
            if (c.arg(3))
                return kDsErrNoAggregation;
            if (!c.valid(desc, 20) || (c.read32(desc) != 20 && c.read32(desc) != 36) || !c.valid(c.arg(2), 4))
                return kDsErrInvalidParam;
            const uint32_t flags = c.read32(desc + 4);
            if (flags & kPrimary) {
                if (c.read32(desc + 8) || c.read32(desc + 16))
                    return kDsErrInvalidParam;
                std::shared_ptr<Buffer> b = primary.lock();
                if (b) {
                    ++b->refs;
                } else {
                    b = std::make_shared<Buffer>();
                    b->s = s;
                    b->primary = true;
                    b->flags = flags;
                    primary = b;
                }
                c.write32(c.arg(2), s->com.pointer(b, s->buffer));
                return kDsOk;
            }
            const uint32_t bytes = c.read32(desc + 8);
            if (bytes < kMinSize || bytes > kMaxSize)
                return kDsErrInvalidParam;
            const auto format = readFormat(c, c.read32(desc + 16));
            if (!format)
                return kDsErrBadFormat;
            auto b = std::make_shared<Buffer>();
            b->s = s;
            b->flags = flags;
            b->format = *format;
            b->block = std::make_shared<Block>(s->process, bytes);
            // Silence to begin with: the middle of 8-bit samples, 0 of 16-bit (*inferred*).
            for (uint32_t i = 0; i < bytes; ++i)
                c.write8(b->block->address + i, format->bits == 8 ? 0x80 : 0);
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                s->buffers.insert(b.get());
            }
            c.write32(c.arg(2), s->com.pointer(b, s->buffer));
            return kDsOk;
        }
        // GetCaps(DSCAPS, 96 bytes): one device mixing in software, any
        // rate DirectSound allows, every PCM format, no hardware buffers.
        if (m == "GetCaps") {
            const uint32_t caps = c.arg(1);
            if (!c.valid(caps, 96) || c.read32(caps) != 96)
                return kDsErrInvalidParam;
            for (uint32_t i = 4; i < 96; i += 4)
                c.write32(caps + i, 0);
            // DSCAPS_PRIMARY{MONO,STEREO,8BIT,16BIT}, CONTINUOUSRATE and SECONDARY{MONO,STEREO,8BIT,16BIT}.
            c.write32(caps + 4, 0x0f1f);
            c.write32(caps + 8, kMinRate);
            c.write32(caps + 12, kMaxRate);
            c.write32(caps + 16, 1); // primary buffers
            return kDsOk;
        }
        // DuplicateSoundBuffer(original, *copy): the same sound, with a
        // place, volume and pan of its own.
        if (m == "DuplicateSoundBuffer") {
            const auto original = s->com.shared<Buffer>(c.arg(1));
            if (!original || original->primary || !c.valid(c.arg(2), 4))
                return kDsErrInvalidParam;
            auto b = std::make_shared<Buffer>();
            b->s = s;
            b->flags = original->flags;
            b->format = original->format;
            b->block = original->block;
            b->frequency = original->frequency;
            b->volume = original->volume;
            b->pan = original->pan;
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                s->buffers.insert(b.get());
            }
            c.write32(c.arg(2), s->com.pointer(b, s->buffer));
            return kDsOk;
        }
        if (m == "SetCooperativeLevel") {
            s->cooperativeLevel = c.arg(2);
            return kDsOk;
        }
        if (m == "Compact")
            return kDsOk;
        if (m == "GetSpeakerConfig") {
            if (!c.valid(c.arg(1), 4))
                return kDsErrInvalidParam;
            c.write32(c.arg(1), s->speakerConfig ? s->speakerConfig : 4); // DSSPEAKER_STEREO
            return kDsOk;
        }
        if (m == "SetSpeakerConfig") {
            s->speakerConfig = c.arg(1);
            return kDsOk;
        }
        if (m == "Initialize")
            return kDsErrAlreadyInitialized;
        return std::nullopt;
    }
};

class Output : public audio::OutputStream {
public:
    explicit Output(std::weak_ptr<DirectSound::State> s) : _s(std::move(s)) {}
    void read(int16_t *out, int frames, int rate) override {
        if (auto s = _s.lock())
            s->mix(out, frames, rate);
        else
            std::fill(out, out + 2 * std::max(frames, 0), int16_t(0));
    }

private:
    std::weak_ptr<DirectSound::State> _s;
};

} // namespace

// Each buffer playing, from where the mix last left it; where that has
// drifted from the clock's play position by more than a tenth of a second
// (the program moved it, or the mix fell behind), from the clock's.
void DirectSound::State::mix(int16_t *out, int frames, int rate) {
    if (frames <= 0)
        return;
    mixed.assign(size_t(frames) * 2, 0.0f);
    std::lock_guard<std::mutex> lock(mutex);
    const uint8_t *memory = cpu.memory().data();
    for (Buffer *b : buffers) {
        const uint32_t at = b->position();
        if (!b->playing || rate <= 0 || !cpu.valid(b->block->address, b->size()))
            continue;
        const Format &f = b->format;
        const uint32_t align = f.blockAlign();
        const uint32_t count = b->size() / align;
        if (!count)
            continue;
        const double clock = double(at / align);
        const double drift = std::fmod(clock - b->heard + 1.5 * count, double(count)) - 0.5 * count;
        if (!b->heardSet || std::fabs(drift) > b->rate() / 10.0) {
            b->heard = clock;
            b->heardSet = true;
        }
        const double g = gain(b->volume);
        const double left = g * (b->pan > 0 ? gain(-b->pan) : 1.0);
        const double right = g * (b->pan < 0 ? gain(b->pan) : 1.0);
        const double step = double(b->rate()) / double(rate);
        const uint8_t *data = memory + b->block->address;
        auto sample = [&](uint32_t frame, int channel) -> float {
            const uint8_t *p = data + frame * align + uint32_t(channel * f.bits / 8);
            return f.bits == 8 ? float((int(p[0]) - 128) << 8) : float(int16_t(p[0] | p[1] << 8));
        };
        for (int i = 0; i < frames; ++i) {
            if (b->heard >= double(count)) {
                if (!b->looping)
                    break;
                b->heard = std::fmod(b->heard, double(count));
            }
            const uint32_t frame = uint32_t(b->heard);
            const float l = sample(frame, 0);
            const float r = f.channels > 1 ? sample(frame, 1) : l;
            mixed[size_t(i) * 2] += float(l * left);
            mixed[size_t(i) * 2 + 1] += float(r * right);
            b->heard += step;
        }
    }
    if (wave && wave->rate > 0 && wave->frames()) {
        const double step = double(wave->rate) / double(rate);
        const size_t count = wave->frames();
        for (int i = 0; i < frames; ++i) {
            if (wavePos >= double(count)) {
                if (!waveLoops) {
                    wave.reset();
                    break;
                }
                wavePos = std::fmod(wavePos, double(count));
            }
            const size_t at = size_t(wavePos) * size_t(wave->channels);
            const float l = wave->samples[at];
            const float r = wave->channels > 1 ? wave->samples[at + 1] : l;
            mixed[size_t(i) * 2] += l;
            mixed[size_t(i) * 2 + 1] += r;
            wavePos += step;
        }
    }
    for (size_t i = 0; i < mixed.size(); ++i)
        out[i] = int16_t(std::lround(std::clamp(mixed[i], -32768.0f, 32767.0f)));
}

DirectSound::~DirectSound() = default;

std::shared_ptr<audio::OutputStream> DirectSound::output() {
    return std::make_shared<Output>(_s);
}

int DirectSound::buffers() const {
    std::lock_guard<std::mutex> lock(_s->mutex);
    return int(_s->buffers.size());
}

int DirectSound::playing() const {
    std::lock_guard<std::mutex> lock(_s->mutex);
    int n = 0;
    for (Buffer *b : _s->buffers)
        if (b->isPlaying())
            ++n;
    return n;
}

DirectSound::DirectSound(Win32Process &process) : _s(std::make_shared<State>(process)) {
    State *s = _s.get();
    s->com.define(s->sound);
    s->com.define(s->buffer);
    // The exports, by ordinal as programs import them: 1 DirectSoundCreate,
    // 2 DirectSoundEnumerateA, 3 DirectSoundEnumerateW.
    const char *dll = "DSOUND.dll";
    for (const char *name : {"#1", "DirectSoundCreate"})
        process.provideStdcall(dll, name, 3, [s](Cpu &c) -> uint32_t {
            // DirectSoundCreate(device, *sound, outer): the default device
            // (none, or GUID_NULL) or Reluva's.
            if (c.arg(2))
                return kDsErrNoAggregation;
            if (c.arg(0)) {
                const std::string g = ComHost::guid(c, c.arg(0));
                if (g != kDeviceGuid && g != "{00000000-0000-0000-0000-000000000000}")
                    return kDsErrNoDriver;
            }
            if (!c.valid(c.arg(1), 4))
                return kDsErrInvalidParam;
            auto o = std::make_shared<Sound>();
            o->s = s;
            c.write32(c.arg(1), s->com.pointer(o, s->sound));
            return kDsOk;
        });
    // DirectSoundEnumerate(callback, context): callback(guid, description,
    // module, context) for the primary driver (no GUID), then each device,
    // until it returns FALSE.
    auto enumerate = [s](Cpu &c, bool wide) -> uint32_t {
        Win32Process &p = s->process;
        const uint32_t guid = p.memory().alloc(16);
        ComHost::writeGuid(c, guid, kDeviceGuid);
        auto text = [&](const std::string &t) {
            const uint32_t at = p.memory().alloc(uint32_t(t.size() + 1) * 2);
            for (size_t i = 0; i <= t.size(); ++i) {
                const uint8_t ch = i < t.size() ? uint8_t(t[i]) : 0;
                if (wide)
                    c.write16(at + uint32_t(i) * 2, ch);
                else
                    c.write8(at + uint32_t(i), ch);
            }
            return at;
        };
        const uint32_t primaryName = text("Primary Sound Driver");
        const uint32_t name = text("Reluva");
        const uint32_t none = text("");
        const uint32_t module = text("reluva");
        uint32_t more = 0;
        if (!c.call(c.arg(0), {0, primaryName, none, c.arg(1)}, more) && more)
            c.call(c.arg(0), {guid, name, module, c.arg(1)}, more);
        for (uint32_t a : {guid, primaryName, name, none, module})
            p.memory().free(a);
        return kDsOk;
    };
    // WINMM's PlaySoundA(sound, module, flags): a WAV file (or, with
    // SND_MEMORY, a WAV file's image in memory) played once or, with
    // SND_LOOP, until another or none (a null sound) replaces it.
    process.provideStdcall("WINMM.dll", "PlaySoundA", 3, [s](Cpu &c) -> uint32_t {
        constexpr uint32_t kAsync = 0x1;
        constexpr uint32_t kMemory = 0x4;
        constexpr uint32_t kLoop = 0x8;
        constexpr uint32_t kNoStop = 0x10;
        constexpr uint32_t kAlias = 0x10000;
        constexpr uint32_t kResource = 0x40004;
        const uint32_t name = c.arg(0);
        const uint32_t flags = c.arg(2);
        {
            std::lock_guard<std::mutex> lock(s->mutex);
            if ((flags & kNoStop) && s->wave)
                return 0;
            s->wave.reset();
        }
        if (!name)
            return 1;
        std::vector<uint8_t> bytes;
        if ((flags & kResource) == kResource || (flags & kAlias)) {
            // Not stood in for: system sounds by alias, and WAV resources.
            if (s->process.output)
                s->process.output("PlaySoundA: aliases and resources aren't stood in for\n");
            return 0;
        }
        if (flags & kMemory) {
            // A RIFF file: its size is in its header.
            if (!c.valid(name, 8))
                return 0;
            const uint32_t size = c.read32(name + 4) + 8;
            if (!c.valid(name, size))
                return 0;
            bytes.assign(c.memory().data() + name, c.memory().data() + name + size);
        } else {
            const auto host =
                s->process.mapPath ? s->process.mapPath(s->process.fullPath(c.string(name)), false) : std::nullopt;
            std::ifstream in(host ? *host : std::filesystem::path(), std::ios::binary);
            if (!host || !in)
                return 0;
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        auto sound = std::make_shared<audio::Sound>();
        if (audio::decodeWav(bytes, *sound) || !sound->frames())
            return 0;
        const uint64_t ms = uint64_t(sound->frames()) * 1000 / uint64_t(std::max(sound->rate, 1));
        {
            std::lock_guard<std::mutex> lock(s->mutex);
            s->wave = sound;
            s->wavePos = 0;
            s->waveLoops = (flags & kLoop) && (flags & kAsync);
        }
        // Played synchronously: the call returns when it has played.
        if (!(flags & kAsync) && s->process.hostThreads())
            s->process.wait([] { return false; }, uint32_t(ms));
        return 1;
    });

    for (const char *name : {"#2", "DirectSoundEnumerateA"})
        process.provideStdcall(dll, name, 2, [enumerate](Cpu &c) { return enumerate(c, false); });
    for (const char *name : {"#3", "DirectSoundEnumerateW"})
        process.provideStdcall(dll, name, 2, [enumerate](Cpu &c) { return enumerate(c, true); });
}

} // namespace reluva::x86
