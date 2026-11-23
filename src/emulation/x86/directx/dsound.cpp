// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// IDirectSound, IDirectSoundBuffer, IDirectSound3DBuffer and
// IDirectSound3DListener, as the DirectX 5 SDK's dsound.h declares them and
// its documentation describes them.

#include "emulation/x86/directx/dsound.h"

#include "audio/mixer.h"
#include "emulation/x86/com.h"
#include "emulation/x86/directx/sound3d.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <string_view>
#include <vector>

namespace reluva::x86 {

namespace {

const char *const kIidDirectSound = "{279afa83-4981-11ce-a521-0020af0be560}";
const char *const kIidBuffer = "{279afa85-4981-11ce-a521-0020af0be560}";
const char *const kIidListener = "{279afa84-4981-11ce-a521-0020af0be560}";
const char *const kIid3DBuffer = "{279afa86-4981-11ce-a521-0020af0be560}";

constexpr uint32_t kOk = 0;
constexpr uint32_t kErrInvalidParam = 0x80070057u;
constexpr uint32_t kErrInvalidCall = 0x88780032u;
constexpr uint32_t kErrBadFormat = 0x88780064u;
constexpr uint32_t kErrControlUnavailable = 0x8878001eu;
constexpr uint32_t kErrPriorityNeeded = 0x88780046u;

// DSBUFFERDESC's flags.
constexpr uint32_t kPrimary = 0x1;
constexpr uint32_t kCtrl3d = 0x10;
constexpr uint32_t kCtrlFrequency = 0x20;
constexpr uint32_t kCtrlPan = 0x40;
constexpr uint32_t kCtrlVolume = 0x80;
// What the stand-in accepts of a buffer's flags, as hardware the card
// says it hasn't (DSBCAPS_LOCHARDWARE) or software.
constexpr uint32_t kLocHardware = 0x4;
constexpr uint32_t kLocSoftware = 0x8;
// The 3D methods' last argument: applied now, or at CommitDeferredSettings.
constexpr uint32_t kDeferred = 1;
// How far the write cursor runs ahead of the play cursor, as a card's
// does: what's being played can't be written.
constexpr uint32_t kWriteAheadMs = 15;

const std::vector<ComInterface::Method> kDirectSoundMethods = {
    {"QueryInterface", 2},      {"AddRef", 0},     {"Release", 0},
    {"CreateSoundBuffer", 3},   {"GetCaps", 1},    {"DuplicateSoundBuffer", 2},
    {"SetCooperativeLevel", 2}, {"Compact", 0},    {"GetSpeakerConfig", 1},
    {"SetSpeakerConfig", 1},    {"Initialize", 1},
};
const std::vector<ComInterface::Method> kBufferMethods = {
    {"QueryInterface", 2},
    {"AddRef", 0},
    {"Release", 0},
    {"GetCaps", 1},
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
    {"Restore", 0},
};
const std::vector<ComInterface::Method> k3DBufferMethods = {
    {"QueryInterface", 2},
    {"AddRef", 0},
    {"Release", 0},
    {"GetAllParameters", 1},
    {"GetConeAngles", 2},
    {"GetConeOrientation", 1},
    {"GetConeOutsideVolume", 1},
    {"GetMaxDistance", 1},
    {"GetMinDistance", 1},
    {"GetMode", 1},
    {"GetPosition", 1},
    {"GetVelocity", 1},
    {"SetAllParameters", 2},
    {"SetConeAngles", 3},
    {"SetConeOrientation", 4},
    {"SetConeOutsideVolume", 2},
    {"SetMaxDistance", 2},
    {"SetMinDistance", 2},
    {"SetMode", 2},
    {"SetPosition", 4},
    {"SetVelocity", 4},
};
const std::vector<ComInterface::Method> kListenerMethods = {
    {"QueryInterface", 2},    {"AddRef", 0},
    {"Release", 0},           {"GetAllParameters", 1},
    {"GetDistanceFactor", 1}, {"GetDopplerFactor", 1},
    {"GetOrientation", 2},    {"GetPosition", 1},
    {"GetRolloffFactor", 1},  {"GetVelocity", 1},
    {"SetAllParameters", 2},  {"SetDistanceFactor", 2},
    {"SetDopplerFactor", 2},  {"SetOrientation", 7},
    {"SetPosition", 4},       {"SetRolloffFactor", 2},
    {"SetVelocity", 4},       {"CommitDeferredSettings", 0},
};

float asFloat(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
uint32_t asBits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
// Hundredths of a decibel (0 down to -10000) as a gain.
double decibels(int32_t hundredths) {
    return hundredths <= -10000 ? 0.0 : std::pow(10.0, double(hundredths) / 2000.0);
}

// A PCM format, as WAVEFORMATEX gives it.
struct WaveFormat {
    uint16_t tag = 1;
    uint16_t channels = 2;
    uint32_t rate = 22050;
    uint16_t bits = 16;
    uint16_t blockAlign() const { return uint16_t(channels * (bits / 8)); }
    bool usable() const { return tag == 1 && (channels == 1 || channels == 2) && (bits == 8 || bits == 16); }
    static WaveFormat read(Cpu &c, uint32_t p) {
        WaveFormat f;
        f.tag = c.read16(p);
        f.channels = c.read16(p + 2);
        f.rate = c.read32(p + 4);
        f.bits = c.read16(p + 14);
        return f;
    }
    void write(Cpu &c, uint32_t p) const {
        c.write16(p, tag);
        c.write16(p + 2, channels);
        c.write32(p + 4, rate);
        c.write32(p + 8, rate * blockAlign());
        c.write16(p + 12, blockAlign());
        c.write16(p + 14, bits);
    }
};

class Buffer;

} // namespace

struct DirectSound::State {
    Win32Process &process;
    Cpu &cpu;
    std::unique_ptr<ComHost> com;
    ComInterface directSound{"IDirectSound", kIidDirectSound, kDirectSoundMethods};
    ComInterface buffer{"IDirectSoundBuffer", kIidBuffer, kBufferMethods};
    ComInterface buffer3d{"IDirectSound3DBuffer", kIid3DBuffer, k3DBufferMethods};
    ComInterface listener{"IDirectSound3DListener", kIidListener, kListenerMethods};
    // What the mixer's thread reads, under `mutex`: the buffers, the
    // listener, the primary buffer's volume; nothing once `open` is false.
    std::mutex mutex;
    bool open = true;
    std::set<Buffer *> buffers;
    Ds3dListener listenerNow;
    Ds3dListener listenerDeferred;
    bool listenerPending = false; // listenerDeferred holds settings to commit
    int32_t masterVolume = 0;
    size_t made = 0;
    std::vector<int32_t> mix;
    explicit State(Win32Process &p) : process(p), cpu(p.cpu()), com(std::make_unique<ComHost>(p)) {}
    uint64_t now() const { return process.milliseconds ? process.milliseconds() : 0; }
    void read(int16_t *out, int frames, int rate);
};

namespace {

// The process memory a buffer's sound is in, shared by its duplicates.
struct Memory {
    Win32Process &process;
    uint32_t address;
    uint32_t size;
    Memory(Win32Process &p, uint32_t a, uint32_t n) : process(p), address(a), size(n) {}
    Memory(const Memory &) = delete;
    Memory &operator=(const Memory &) = delete;
    ~Memory() {
        if (address)
            process.memory().free(address);
    }
};

// A sound buffer: its memory, format and controls, its playing, and its
// place in 3D; also the primary buffer, which holds the listener.
class Buffer : public ComObject {
public:
    DirectSound::State *s = nullptr;
    std::shared_ptr<Memory> memory;
    WaveFormat format;
    uint32_t flags = 0;
    bool primary = false;
    // Its playing, by the process's clock: from where, since when, at what
    // frequency (frames a second).
    bool playing = false;
    bool looping = false;
    uint64_t startMs = 0;
    uint32_t startPosition = 0;
    uint32_t frequency = 0;
    int32_t volume = 0; // hundredths of a decibel
    int32_t pan = 0;    // -10000 left to 10000 right
    Ds3dSource place;
    Ds3dSource deferredPlace;
    // The mixer's own place in it (frames), kept to the play position.
    double outCursor = 0;
    bool outSynced = false;
    double rearLow[2]{};

    explicit Buffer(DirectSound::State *state) : s(state) {}
    // Heard from here on: once its memory and format are set.
    void attach() {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->buffers.insert(this);
        ++s->made;
    }
    ~Buffer() override {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->buffers.erase(this);
    }

    uint32_t size() const { return memory ? memory->size : 0; }
    uint32_t block() const { return std::max<uint32_t>(format.blockAlign(), 1); }

    // The play position (bytes, a whole block) by the clock; a buffer
    // played once stops at its end. Under the mutex.
    uint32_t position(uint64_t nowMs) {
        if (!playing || !size())
            return startPosition;
        const uint64_t played = (nowMs - startMs) * frequency / 1000 * block();
        const uint64_t at = startPosition + played;
        if (!looping && at >= size()) {
            playing = false;
            startPosition = 0;
            return 0;
        }
        return uint32_t(at % size()) / block() * block();
    }
    // Starts the clock again from where it is, for a change of frequency.
    void rebase() {
        const uint64_t now = s->now();
        startPosition = position(now);
        startMs = now;
    }

    const ComInterface *query(const std::string &iid) override {
        if (iid == kIidBuffer)
            return &s->buffer;
        if (iid == kIid3DBuffer && !primary && (flags & kCtrl3d))
            return &s->buffer3d;
        if (iid == kIidListener && primary)
            return &s->listener;
        return nullptr;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // The 3D methods' last argument says when a setting applies.
        const bool deferred = m.rfind("Set", 0) == 0 && (c.arg(iface.methods[size_t(index)].args) & kDeferred);
        std::lock_guard<std::mutex> lock(s->mutex);
        if (&iface == &s->buffer3d)
            return invoke3d(m, c, deferred);
        if (&iface == &s->listener)
            return invokeListener(m, c, deferred);
        return invokeBuffer(m, c);
    }

    std::optional<uint32_t> invokeBuffer(std::string_view m, Cpu &c) {
        auto out = [&](int arg, uint32_t v) {
            if (c.arg(arg) && c.valid(c.arg(arg), 4))
                c.write32(c.arg(arg), v);
        };
        if (m == "GetCaps") { // DSBCAPS: size, flags, bytes, transfer rate, overhead
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 20))
                return kErrInvalidParam;
            c.write32(p + 4, (flags & ~kLocHardware) | kLocSoftware);
            c.write32(p + 8, size());
            c.write32(p + 12, 0);
            c.write32(p + 16, 0);
            return kOk;
        }
        if (m == "GetCurrentPosition") { // (play out, write out)
            const uint32_t at = position(s->now());
            const uint32_t ahead = playing ? format.rate * block() * kWriteAheadMs / 1000 / block() * block() : 0;
            out(1, at);
            out(2, size() ? (at + ahead) % size() : 0);
            return kOk;
        }
        if (m == "SetCurrentPosition") {
            if (primary || c.arg(1) >= std::max(size(), 1u))
                return kErrInvalidParam;
            startPosition = c.arg(1) / block() * block();
            startMs = s->now();
            outSynced = false;
            return kOk;
        }
        if (m == "GetFormat") { // (WAVEFORMATEX*, size, written out)
            const uint32_t p = c.arg(1);
            if (!p) {
                out(3, 18);
                return kOk;
            }
            if (c.arg(2) < 16 || !c.valid(p, std::min(c.arg(2), 18u)))
                return kErrInvalidParam;
            format.write(c, p);
            if (c.arg(2) >= 18)
                c.write16(p + 16, 0);
            out(3, std::min(c.arg(2), 18u));
            return kOk;
        }
        if (m == "SetFormat") { // the primary buffer's only
            if (!primary)
                return kErrInvalidCall;
            if (!c.valid(c.arg(1), 16))
                return kErrInvalidParam;
            const WaveFormat f = WaveFormat::read(c, c.arg(1));
            if (!f.usable())
                return kErrBadFormat;
            format = f;
            return kOk;
        }
        if (m == "GetVolume" || m == "GetPan" || m == "GetFrequency") {
            out(1, m == "GetVolume" ? uint32_t(volume) : m == "GetPan" ? uint32_t(pan) : frequency);
            return kOk;
        }
        if (m == "SetVolume") {
            if (!(flags & kCtrlVolume) && !primary)
                return kErrControlUnavailable;
            volume = std::clamp(int32_t(c.arg(1)), -10000, 0);
            if (primary)
                s->masterVolume = volume;
            return kOk;
        }
        if (m == "SetPan") {
            if (!(flags & kCtrlPan) && !primary)
                return kErrControlUnavailable;
            pan = std::clamp(int32_t(c.arg(1)), -10000, 10000);
            return kOk;
        }
        if (m == "SetFrequency") { // 0: the format's own
            if (!(flags & kCtrlFrequency) || primary)
                return kErrControlUnavailable;
            if (c.arg(1) && (c.arg(1) < 100 || c.arg(1) > 100000))
                return kErrInvalidParam;
            rebase();
            frequency = c.arg(1) ? c.arg(1) : format.rate;
            return kOk;
        }
        if (m == "GetStatus") { // DSBSTATUS_PLAYING 1, _LOOPING 4
            position(s->now());
            out(1, (playing ? 1u : 0u) | (playing && looping ? 4u : 0u));
            return kOk;
        }
        if (m == "Play") { // (reserved, priority, flags): DSBPLAY_LOOPING 1
            if (primary) {
                playing = true;
                looping = true;
                return kOk;
            }
            const uint64_t now = s->now();
            if (!playing) {
                startMs = now;
                outSynced = false;
            } else {
                rebase();
            }
            playing = size() > 0;
            looping = c.arg(3) & 1;
            return kOk;
        }
        if (m == "Stop") {
            rebase();
            playing = false;
            return kOk;
        }
        // Lock(offset, bytes, &p1, &n1, &p2, &n2, flags): the part asked for,
        // in two pieces when it wraps round the end. DSBLOCK_FROMWRITECURSOR
        // 1, DSBLOCK_ENTIREBUFFER 2.
        if (m == "Lock") {
            if (primary)
                return kErrPriorityNeeded; // its mix isn't the program's to write
            uint32_t offset = c.arg(1);
            uint32_t bytes = c.arg(2);
            if (c.arg(7) & 1) {
                const uint32_t at = position(s->now());
                const uint32_t ahead = playing ? format.rate * block() * kWriteAheadMs / 1000 / block() * block() : 0;
                offset = (at + ahead) % std::max(size(), 1u);
            }
            if (c.arg(7) & 2)
                bytes = size();
            if (!size() || offset >= size() || bytes > size() || !bytes)
                return kErrInvalidParam;
            const uint32_t first = std::min(bytes, size() - offset);
            out(3, memory->address + offset);
            out(4, first);
            out(5, first < bytes ? memory->address : 0);
            out(6, bytes - first);
            return kOk;
        }
        if (m == "Unlock" || m == "Restore" || m == "Initialize")
            return kOk;
        return std::nullopt;
    }

    // DS3DBUFFER: size, position, velocity, inside and outside cone angles,
    // cone orientation, outside volume, minimum and maximum distance, mode.
    std::optional<uint32_t> invoke3d(std::string_view m, Cpu &c, bool deferred) {
        Ds3dSource &to = deferred ? deferredPlace : place;
        if (deferred && !pending)
            deferredPlace = place;
        pending |= deferred;
        auto vec = [&](int arg, float *v) {
            for (int i = 0; i < 3; ++i)
                v[i] = asFloat(c.arg(arg + i));
        };
        auto putVec = [&](uint32_t p, const float *v) {
            if (c.valid(p, 12))
                for (uint32_t i = 0; i < 3; ++i)
                    c.write32(p + 4 * i, asBits(v[i]));
        };
        auto out = [&](uint32_t p, uint32_t v) {
            if (c.valid(p, 4))
                c.write32(p, v);
        };
        if (m == "SetPosition") {
            vec(1, to.position);
            return kOk;
        }
        if (m == "SetVelocity") {
            vec(1, to.velocity);
            return kOk;
        }
        if (m == "SetConeOrientation") {
            vec(1, to.coneOrientation);
            return kOk;
        }
        if (m == "SetConeAngles") {
            to.insideCone = c.arg(1);
            to.outsideCone = c.arg(2);
            return kOk;
        }
        if (m == "SetConeOutsideVolume") {
            to.coneOutsideVolume = int32_t(c.arg(1));
            return kOk;
        }
        if (m == "SetMinDistance") {
            to.minDistance = asFloat(c.arg(1));
            return kOk;
        }
        if (m == "SetMaxDistance") {
            to.maxDistance = asFloat(c.arg(1));
            return kOk;
        }
        if (m == "SetMode") {
            to.mode = c.arg(1);
            return kOk;
        }
        if (m == "SetAllParameters" || m == "GetAllParameters") {
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 64))
                return kErrInvalidParam;
            if (m == "GetAllParameters") {
                c.write32(p, 64);
                putVec(p + 4, place.position);
                putVec(p + 16, place.velocity);
                c.write32(p + 28, place.insideCone);
                c.write32(p + 32, place.outsideCone);
                putVec(p + 36, place.coneOrientation);
                c.write32(p + 48, uint32_t(place.coneOutsideVolume));
                c.write32(p + 52, asBits(place.minDistance));
                c.write32(p + 56, asBits(place.maxDistance));
                c.write32(p + 60, place.mode);
                return kOk;
            }
            for (int i = 0; i < 3; ++i) {
                to.position[i] = asFloat(c.read32(p + 4 + 4 * uint32_t(i)));
                to.velocity[i] = asFloat(c.read32(p + 16 + 4 * uint32_t(i)));
                to.coneOrientation[i] = asFloat(c.read32(p + 36 + 4 * uint32_t(i)));
            }
            to.insideCone = c.read32(p + 28);
            to.outsideCone = c.read32(p + 32);
            to.coneOutsideVolume = int32_t(c.read32(p + 48));
            to.minDistance = asFloat(c.read32(p + 52));
            to.maxDistance = asFloat(c.read32(p + 56));
            to.mode = c.read32(p + 60);
            return kOk;
        }
        if (m == "GetPosition") {
            putVec(c.arg(1), place.position);
            return kOk;
        }
        if (m == "GetVelocity") {
            putVec(c.arg(1), place.velocity);
            return kOk;
        }
        if (m == "GetConeOrientation") {
            putVec(c.arg(1), place.coneOrientation);
            return kOk;
        }
        if (m == "GetConeAngles") {
            out(c.arg(1), place.insideCone);
            out(c.arg(2), place.outsideCone);
            return kOk;
        }
        if (m == "GetConeOutsideVolume") {
            out(c.arg(1), uint32_t(place.coneOutsideVolume));
            return kOk;
        }
        if (m == "GetMinDistance" || m == "GetMaxDistance") {
            out(c.arg(1), asBits(m == "GetMinDistance" ? place.minDistance : place.maxDistance));
            return kOk;
        }
        if (m == "GetMode") {
            out(c.arg(1), place.mode);
            return kOk;
        }
        return std::nullopt;
    }

    // DS3DLISTENER: size, position, velocity, front, top, distance, rolloff
    // and Doppler factors.
    std::optional<uint32_t> invokeListener(std::string_view m, Cpu &c, bool deferred) const {
        Ds3dListener &to = deferred ? s->listenerDeferred : s->listenerNow;
        if (deferred && !s->listenerPending)
            s->listenerDeferred = s->listenerNow;
        s->listenerPending |= deferred;
        const Ds3dListener &l = s->listenerNow;
        auto vec = [&](int arg, float *v) {
            for (int i = 0; i < 3; ++i)
                v[i] = asFloat(c.arg(arg + i));
        };
        auto putVec = [&](uint32_t p, const float *v) {
            if (c.valid(p, 12))
                for (uint32_t i = 0; i < 3; ++i)
                    c.write32(p + 4 * i, asBits(v[i]));
        };
        auto outF = [&](uint32_t p, float v) {
            if (c.valid(p, 4))
                c.write32(p, asBits(v));
        };
        if (m == "SetPosition") {
            vec(1, to.position);
            return kOk;
        }
        if (m == "SetVelocity") {
            vec(1, to.velocity);
            return kOk;
        }
        if (m == "SetOrientation") {
            vec(1, to.front);
            vec(4, to.top);
            return kOk;
        }
        if (m == "SetDistanceFactor") {
            to.distanceFactor = asFloat(c.arg(1));
            return kOk;
        }
        if (m == "SetRolloffFactor") {
            to.rolloffFactor = asFloat(c.arg(1));
            return kOk;
        }
        if (m == "SetDopplerFactor") {
            to.dopplerFactor = asFloat(c.arg(1));
            return kOk;
        }
        if (m == "SetAllParameters" || m == "GetAllParameters") {
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 64))
                return kErrInvalidParam;
            if (m == "GetAllParameters") {
                c.write32(p, 64);
                putVec(p + 4, l.position);
                putVec(p + 16, l.velocity);
                putVec(p + 28, l.front);
                putVec(p + 40, l.top);
                c.write32(p + 52, asBits(l.distanceFactor));
                c.write32(p + 56, asBits(l.rolloffFactor));
                c.write32(p + 60, asBits(l.dopplerFactor));
                return kOk;
            }
            for (int i = 0; i < 3; ++i) {
                to.position[i] = asFloat(c.read32(p + 4 + 4 * uint32_t(i)));
                to.velocity[i] = asFloat(c.read32(p + 16 + 4 * uint32_t(i)));
                to.front[i] = asFloat(c.read32(p + 28 + 4 * uint32_t(i)));
                to.top[i] = asFloat(c.read32(p + 40 + 4 * uint32_t(i)));
            }
            to.distanceFactor = asFloat(c.read32(p + 52));
            to.rolloffFactor = asFloat(c.read32(p + 56));
            to.dopplerFactor = asFloat(c.read32(p + 60));
            return kOk;
        }
        if (m == "GetPosition") {
            putVec(c.arg(1), l.position);
            return kOk;
        }
        if (m == "GetVelocity") {
            putVec(c.arg(1), l.velocity);
            return kOk;
        }
        if (m == "GetOrientation") {
            putVec(c.arg(1), l.front);
            putVec(c.arg(2), l.top);
            return kOk;
        }
        if (m == "GetDistanceFactor" || m == "GetRolloffFactor" || m == "GetDopplerFactor") {
            outF(c.arg(1), m == "GetDistanceFactor"  ? l.distanceFactor
                           : m == "GetRolloffFactor" ? l.rolloffFactor
                                                     : l.dopplerFactor);
            return kOk;
        }
        // Every setting made with DS3D_DEFERRED, the listener's and the
        // buffers', applied at once.
        if (m == "CommitDeferredSettings") {
            if (s->listenerPending)
                s->listenerNow = s->listenerDeferred;
            s->listenerPending = false;
            for (Buffer *b : s->buffers)
                if (b->pending) {
                    b->place = b->deferredPlace;
                    b->pending = false;
                }
            return kOk;
        }
        return std::nullopt;
    }

    bool pending = false; // deferredPlace holds settings to commit
};

// The IDirectSound object: it makes the buffers.
class Device : public ComObject {
public:
    DirectSound::State *s = nullptr;
    uint32_t level = 0; // SetCooperativeLevel's: DSSCL_NORMAL 1 .. WRITEPRIMARY 4

    const ComInterface *query(const std::string &iid) override {
        return iid == kIidDirectSound ? &s->directSound : nullptr;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (m == "SetCooperativeLevel") {
            level = c.arg(2);
            return kOk;
        }
        if (m == "Initialize" || m == "Compact" || m == "SetSpeakerConfig")
            return kOk;
        if (m == "GetSpeakerConfig") { // DSSPEAKER_STEREO 4
            if (c.valid(c.arg(1), 4))
                c.write32(c.arg(1), 4);
            return kOk;
        }
        // GetCaps(DSCAPS*): a card mixing 8 and 16 bits, mono and stereo, at
        // any rate (DSCAPS_PRIMARYMONO to CONTINUOUSRATE, and the secondary
        // ones), in software: no hardware buffers.
        if (m == "GetCaps") {
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 96))
                return kErrInvalidParam;
            std::memset(c.memory().data() + p + 4, 0, 92);
            c.write32(p + 4, 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x100 | 0x200 | 0x400 | 0x800);
            c.write32(p + 8, 100);
            c.write32(p + 12, 100000);
            c.write32(p + 16, 1); // primary buffers
            return kOk;
        }
        // CreateSoundBuffer(DSBUFFERDESC*, out, outer): size, flags, bytes,
        // reserved, format.
        if (m == "CreateSoundBuffer") {
            const uint32_t desc = c.arg(1);
            const uint32_t out = c.arg(2);
            if (!c.valid(desc, 20) || !c.valid(out, 4))
                return kErrInvalidParam;
            const uint32_t flags = c.read32(desc + 4);
            auto b = std::make_shared<Buffer>(s);
            b->flags = flags;
            if (flags & kPrimary) {
                b->primary = true;
            } else {
                const uint32_t bytes = c.read32(desc + 8);
                const uint32_t fmt = c.read32(desc + 16);
                if (!bytes || bytes > (16u << 20) || !c.valid(fmt, 16))
                    return kErrInvalidParam;
                b->format = WaveFormat::read(c, fmt);
                if (!b->format.usable() || b->format.rate < 100 || b->format.rate > 100000)
                    return kErrBadFormat;
                const uint32_t address = s->process.memory().alloc(bytes);
                if (!address)
                    return kEOutOfMemory;
                std::memset(c.memory().data() + address, b->format.bits == 8 ? 0x80 : 0, bytes);
                b->memory = std::make_shared<Memory>(s->process, address, bytes);
                b->frequency = b->format.rate;
            }
            b->attach();
            c.write32(out, s->com->pointer(b, s->buffer));
            return kOk;
        }
        // DuplicateSoundBuffer(buffer, out): the same sound, its own playing.
        if (m == "DuplicateSoundBuffer") {
            auto from = s->com->shared<Buffer>(c.arg(1));
            if (!from || from->primary || !c.valid(c.arg(2), 4))
                return kErrInvalidParam;
            auto b = std::make_shared<Buffer>(s);
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                b->memory = from->memory;
                b->format = from->format;
                b->flags = from->flags;
                b->frequency = from->format.rate;
                b->volume = from->volume;
                b->pan = from->pan;
                b->place = from->place;
            }
            b->attach();
            c.write32(c.arg(2), s->com->pointer(b, s->buffer));
            return kOk;
        }
        return std::nullopt;
    }
};

class Output : public audio::OutputStream {
public:
    explicit Output(std::shared_ptr<DirectSound::State> s) : _s(std::move(s)) {}
    void read(int16_t *out, int frames, int rate) override { _s->read(out, frames, rate); }

private:
    std::shared_ptr<DirectSound::State> _s;
};

} // namespace

void DirectSound::State::read(int16_t *out, int frames, int rate) {
    std::fill(out, out + size_t(std::max(frames, 0)) * 2, int16_t(0));
    std::lock_guard<std::mutex> lock(mutex);
    if (!open || frames <= 0 || rate <= 0)
        return;
    mix.assign(size_t(frames) * 2, 0);
    const uint64_t at = now();
    const uint8_t *bytes = cpu.memory().data();
    const double master = decibels(masterVolume);
    for (Buffer *b : buffers) {
        if (b->primary || !b->memory)
            continue;
        const uint32_t position = b->position(at);
        if (!b->playing)
            continue;
        const uint32_t block = b->block();
        const uint32_t count = b->size() / block;
        if (!count || !cpu.valid(b->memory->address, b->size()))
            continue;
        // Follow the play position the program sees; within a tenth of a
        // second, keep to the mixer's own steady pace instead.
        const double target = double(position / block);
        const double drift = std::fmod(target - b->outCursor + 1.5 * count, double(count)) - 0.5 * count;
        if (!b->outSynced || std::fabs(drift) > b->frequency / 10.0) {
            b->outCursor = target;
            b->outSynced = true;
        }
        const double gain = master * decibels(b->volume);
        double left = gain * (b->pan > 0 ? decibels(-b->pan) : 1.0);
        double right = gain * (b->pan < 0 ? decibels(b->pan) : 1.0);
        double step = double(b->frequency) / double(rate);
        double muffle = 1; // the rear's low-pass coefficient: 1 passes all
        if (b->flags & kCtrl3d) {
            const Ds3dResult placed = spatialize(listenerNow, b->place);
            left = gain * placed.left;
            right = gain * placed.right;
            step *= placed.doppler;
            if (placed.behind > 0)
                muffle = lowPassCoefficient(rearCutoff(placed.behind), rate);
        }
        const uint8_t *data = bytes + b->memory->address;
        const bool eight = b->format.bits == 8;
        const int channels = b->format.channels;
        auto sample = [&](uint32_t frame, int channel) {
            const uint8_t *p = data + frame * block + uint32_t(channel * (eight ? 1 : 2));
            return eight ? double((int(p[0]) - 128) << 8) : double(int16_t(p[0] | p[1] << 8));
        };
        for (int i = 0; i < frames; ++i) {
            if (b->outCursor >= double(count)) {
                if (!b->looping)
                    break;
                b->outCursor = std::fmod(b->outCursor, double(count));
            }
            const uint32_t f = uint32_t(b->outCursor);
            double l = sample(f, 0);
            double r = channels > 1 ? sample(f, 1) : l;
            if (muffle < 1) {
                l = b->rearLow[0] += muffle * (l - b->rearLow[0]);
                r = b->rearLow[1] += muffle * (r - b->rearLow[1]);
            }
            mix[size_t(i) * 2] += int32_t(l * left);
            mix[size_t(i) * 2 + 1] += int32_t(r * right);
            b->outCursor += step;
        }
    }
    for (size_t i = 0; i < mix.size(); ++i)
        out[i] = int16_t(std::clamp(mix[i], -32768, 32767));
}

DirectSound::DirectSound(Win32Process &process) : _s(std::make_shared<State>(process)) {
    State *s = _s.get();
    for (ComInterface *f : {&s->directSound, &s->buffer, &s->buffer3d, &s->listener})
        s->com->define(*f);
    // Called for each sound, many times a frame, writing no memory but
    // through Cpu::write32: the instruction cache needn't check its code
    // after each (Cpu::setWritesTracked).
    for (const char *m : {"SetPosition", "SetVelocity", "SetMinDistance", "SetMaxDistance", "SetMode"})
        process.cpu().setWritesTracked(s->buffer3d.name + "::" + m);
    for (const char *m : {"SetVolume", "SetPan", "SetFrequency", "GetStatus", "GetCurrentPosition"})
        process.cpu().setWritesTracked(s->buffer.name + "::" + m);
    const char *dll = "DSOUND.dll";
    // DirectSoundCreate(guid, out, outer): the one card.
    process.provideStdcall(dll, "DirectSoundCreate", 3, [s](Cpu &c) {
        if (!c.valid(c.arg(1), 4))
            return kErrInvalidParam;
        auto device = std::make_shared<Device>();
        device->s = s;
        c.write32(c.arg(1), s->com->pointer(device, s->directSound));
        return kOk;
    });
    // DirectSoundEnumerateA(callback(guid, description, module, context), context)
    const uint32_t names = process.memory().alloc(64);
    process.cpu().writeString(names, "Primary Sound Driver", 32);
    process.cpu().writeString(names + 32, "", 32);
    process.provideStdcall(dll, "DirectSoundEnumerateA", 2, [names](Cpu &c) {
        uint32_t keepGoing = 0;
        c.call(c.arg(0), {0, names, names + 32, c.arg(1)}, keepGoing);
        return kOk;
    });
}

DirectSound::~DirectSound() {
    {
        std::lock_guard<std::mutex> lock(_s->mutex);
        _s->open = false;
    }
    _s->com.reset();
}

std::shared_ptr<audio::OutputStream> DirectSound::output() {
    return std::make_shared<Output>(_s);
}

size_t DirectSound::buffersMade() const {
    std::lock_guard<std::mutex> lock(_s->mutex);
    return _s->made;
}

size_t DirectSound::buffersPlaying() const {
    std::lock_guard<std::mutex> lock(_s->mutex);
    const uint64_t now = _s->now();
    size_t n = 0;
    for (Buffer *b : _s->buffers) {
        b->position(now);
        n += !b->primary && b->playing;
    }
    return n;
}

} // namespace reluva::x86
