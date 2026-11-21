// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectSound: docs/common/x86/directsound.md. Interface layouts, IIDs,
// structures and constants are the DirectX 5 SDK documentation's
// (dsound.h).

#include "emulation/x86/directx/dsound.h"

#include "audio/mixer.h"
#include "emulation/x86/com.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>

namespace reluva::x86 {

namespace {

const char *const kIidDirectSound = "{279afa83-4981-11ce-a521-0020af0be560}";
const char *const kIidBuffer = "{279afa85-4981-11ce-a521-0020af0be560}";
const char *const kIidListener = "{279afa84-4981-11ce-a521-0020af0be560}";
const char *const kIidBuffer3D = "{279afa86-4981-11ce-a521-0020af0be560}";

// DSBUFFERDESC's flags.
constexpr uint32_t kPrimary = 0x1;
constexpr uint32_t kCtrl3D = 0x10;
// Play's, GetStatus's and Lock's.
constexpr uint32_t kPlayLooping = 0x1;
constexpr uint32_t kStatusPlaying = 0x1;
constexpr uint32_t kStatusLooping = 0x4;
constexpr uint32_t kLockFromWriteCursor = 0x1;
constexpr uint32_t kLockEntireBuffer = 0x2;
// 3D buffers' modes.
constexpr uint32_t kModeHeadRelative = 1;
constexpr uint32_t kModeDisable = 2;

constexpr uint32_t kErrAlreadyInitialized = 0x88780082u;
constexpr uint32_t kErrBadFormat = 0x88780064u;
constexpr uint32_t kErrControlUnavailable = 0x8878001eu;
constexpr uint32_t kSpeakerStereo = 4;

// The primary buffer's size, which programs only ask (it's mixed, not
// written): about 100 ms of its format at most.
constexpr uint32_t kPrimarySize = 16384;
// How far ahead of the play position the write position is, as a sound
// card's is: what's there is already on its way out.
constexpr uint32_t kWriteAheadMs = 15;

const std::vector<ComInterface::Method> kUnknown = {{"QueryInterface", 2}, {"AddRef", 0}, {"Release", 0}};
std::vector<ComInterface::Method> with(std::vector<ComInterface::Method> m) {
    m.insert(m.begin(), kUnknown.begin(), kUnknown.end());
    return m;
}

ComInterface makeDirectSound() {
    return {"IDirectSound", kIidDirectSound,
            with({{"CreateSoundBuffer", 3},
                  {"GetCaps", 1},
                  {"DuplicateSoundBuffer", 2},
                  {"SetCooperativeLevel", 2},
                  {"Compact", 0},
                  {"GetSpeakerConfig", 1},
                  {"SetSpeakerConfig", 1},
                  {"Initialize", 1}}),
            0};
}

ComInterface makeBuffer() {
    return {"IDirectSoundBuffer", kIidBuffer,
            with({{"GetCaps", 1},
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
                  {"Restore", 0}}),
            0};
}

ComInterface makeListener() {
    return {"IDirectSound3DListener", kIidListener,
            with({{"GetAllParameters", 1},
                  {"GetDistanceFactor", 1},
                  {"GetDopplerFactor", 1},
                  {"GetOrientation", 2},
                  {"GetPosition", 1},
                  {"GetRolloffFactor", 1},
                  {"GetVelocity", 1},
                  {"SetAllParameters", 2},
                  {"SetDistanceFactor", 2},
                  {"SetDopplerFactor", 2},
                  {"SetOrientation", 7},
                  {"SetPosition", 4},
                  {"SetRolloffFactor", 2},
                  {"SetVelocity", 4},
                  {"CommitDeferredSettings", 0}}),
            0};
}

ComInterface makeBuffer3D() {
    return {"IDirectSound3DBuffer", kIidBuffer3D,
            with({{"GetAllParameters", 1},
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
                  {"SetVelocity", 4}}),
            0};
}

float asFloat(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

uint32_t bits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

// A buffer's sound: where it is in the process's memory, shared by a
// buffer and its duplicates, and freed with the last of them.
struct Wave {
    Win32Process &process;
    uint32_t address = 0;
    uint32_t size = 0;
    Wave(Win32Process &p, uint32_t bytes, uint8_t silence) : process(p), size(bytes) {
        address = bytes ? p.memory().alloc(bytes) : 0;
        if (address)
            std::memset(p.cpu().memory().data() + address, silence, bytes);
    }
    ~Wave() {
        if (address)
            process.memory().free(address);
    }
};

// WAVEFORMATEX's fields as far as PCM needs them.
struct Format {
    uint16_t tag = 1;
    uint16_t channels = 2;
    uint32_t rate = 22050;
    uint32_t bytesPerSecond = 88200;
    uint16_t blockAlign = 4;
    uint16_t bits = 16;

    bool read(Cpu &c, uint32_t p) {
        if (!c.valid(p, 16))
            return false;
        tag = uint16_t(c.read16(p));
        channels = uint16_t(c.read16(p + 2));
        rate = c.read32(p + 4);
        bytesPerSecond = c.read32(p + 8);
        blockAlign = uint16_t(c.read16(p + 12));
        bits = uint16_t(c.read16(p + 14));
        return channels > 0 && rate > 0 && blockAlign > 0;
    }
    // WAVEFORMATEX, its cbSize 0: up to `room` bytes of its 18.
    uint32_t write(Cpu &c, uint32_t p, uint32_t room) const {
        uint8_t b[18] = {};
        std::memcpy(b, &tag, 2);
        std::memcpy(b + 2, &channels, 2);
        std::memcpy(b + 4, &rate, 4);
        std::memcpy(b + 8, &bytesPerSecond, 4);
        std::memcpy(b + 12, &blockAlign, 2);
        std::memcpy(b + 14, &bits, 2);
        const uint32_t n = std::min<uint32_t>(room, sizeof b);
        if (c.valid(p, n))
            std::memcpy(c.memory().data() + p, b, n);
        return n;
    }
    bool playable() const { return tag == 1 && (bits == 8 || bits == 16) && channels >= 1 && channels <= 2; }
};

} // namespace

struct DirectSound::State {
    Win32Process &process;
    Cpu &cpu;
    ComHost com;
    ComInterface directSound = makeDirectSound();
    ComInterface buffer = makeBuffer();
    ComInterface listener = makeListener();
    ComInterface buffer3D = makeBuffer3D();
    audio::Mixer *mixer = nullptr;
    // The listener, for 3D buffers' volumes and pans.
    float listenerPos[3]{};
    float listenerFront[3]{0, 0, 1};
    float listenerTop[3]{0, 1, 0};
    float rolloff = 1;
    float distanceFactor = 1;
    float doppler = 1;
    size_t made = 0;
    std::vector<std::weak_ptr<ComObject>> buffers;
    explicit State(Win32Process &p) : process(p), cpu(p.cpu()), com(p) {}
    uint32_t now() const { return process.milliseconds(); }
    void update();
    void heardAll();
};

namespace {

// A sound buffer, the primary one included: its sound in the process's
// memory, and its playing. The primary buffer is played by nothing: the
// mixer mixes the others itself.
class Buffer : public ComObject {
public:
    DirectSound::State *s = nullptr;
    bool primary = false;
    uint32_t flags = 0;
    Format format;
    std::shared_ptr<Wave> wave;
    bool playing = false;
    bool looped = false;
    // where it was when last started, or stopped
    uint32_t startMs = 0;
    uint32_t startPos = 0;
    uint32_t stoppedPos = 0;
    uint32_t fed = 0;       // how far it's been handed to the mixer
    int32_t volume = 0;     // hundredths of a decibel, 0 to -10,000
    int32_t pan = 0;        // -10,000 (left) to 10,000 (right)
    uint32_t frequency = 0; // 0 for the format's own
    // 3D
    uint32_t mode = 0;
    float position[3]{};
    float minDistance = 1;
    float maxDistance = 1e9f;
    std::shared_ptr<audio::SoundStream> stream;
    audio::SoundHandle sound = audio::kNoSound;

    ~Buffer() override { silence(); }

    uint32_t size() const { return wave ? wave->size : 0; }
    uint32_t rate() const { return frequency ? frequency : format.rate; }

    const ComInterface *query(const std::string &iid) override {
        if (iid == s->buffer.iid)
            return &s->buffer;
        if (iid == s->buffer3D.iid && !primary && (flags & kCtrl3D))
            return &s->buffer3D;
        if (iid == s->listener.iid && primary)
            return &s->listener;
        return nullptr;
    }

    // The play position (bytes, a whole block), as of the process's clock.
    uint32_t cursor() {
        if (!playing || !size())
            return stoppedPos;
        const double elapsed = double(s->now() - startMs) / 1000.0;
        uint64_t moved = uint64_t(elapsed * double(rate()) * double(format.blockAlign));
        moved -= moved % std::max<uint32_t>(format.blockAlign, 1);
        const uint64_t at = uint64_t(startPos) + moved;
        if (looped)
            return uint32_t(at % size());
        if (at >= size()) { // played to its end
            feedTo(size());
            playing = false;
            stoppedPos = 0;
            if (stream)
                stream->end();
            stream.reset();
            sound = audio::kNoSound;
            return 0;
        }
        return uint32_t(at);
    }
    uint32_t writeCursor() {
        const uint32_t play = cursor();
        if (!playing || !size())
            return play;
        uint32_t ahead = uint32_t(uint64_t(rate()) * format.blockAlign * kWriteAheadMs / 1000);
        ahead -= ahead % std::max<uint32_t>(format.blockAlign, 1);
        return (play + ahead) % size();
    }
    // Moves the play position along, what it passes going to the mixer.
    void advance() {
        if (!playing)
            return;
        const uint32_t to = cursor();
        if (playing)
            feedTo(to);
    }
    void feedTo(uint32_t to) {
        if (to == fed)
            return;
        if (to > fed) {
            push(fed, to);
        } else {
            push(fed, size());
            push(0, to);
        }
        fed = to;
    }
    void push(uint32_t from, uint32_t end) {
        if (!stream || end <= from || !format.playable() || !s->cpu.valid(wave->address + from, end - from))
            return;
        const uint8_t *p = s->cpu.memory().data() + wave->address + from;
        std::vector<int16_t> out;
        if (format.bits == 8) {
            out.resize(end - from);
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = int16_t((int(p[i]) - 128) << 8);
        } else {
            out.resize((end - from) / 2);
            std::memcpy(out.data(), p, out.size() * 2);
        }
        out.resize(out.size() - out.size() % format.channels);
        stream->push(out.data(), out.size());
    }
    void start(bool loop) {
        silence();
        looped = loop;
        startPos = stoppedPos;
        fed = startPos;
        startMs = s->now();
        playing = size() > 0;
        if (playing && !primary && s->mixer && format.playable()) {
            stream = std::make_shared<audio::SoundStream>(int(rate()), int(format.channels));
            sound = s->mixer->playStream(stream, audio::SoundType::kEffects, heardVolume(), heardPan(), 100);
        }
    }
    void stop() {
        stoppedPos = cursor();
        playing = false;
        silence();
    }
    void silence() {
        if (sound != audio::kNoSound && s->mixer)
            s->mixer->stop(sound);
        sound = audio::kNoSound;
        stream.reset();
    }
    bool is3D() const { return !primary && (flags & kCtrl3D) && mode != kModeDisable; }
    // Where it is from the listener, in the listener's terms when it's
    // placed relative to the world.
    void offset(double out[3]) const {
        for (int i = 0; i < 3; ++i)
            out[i] = double(position[i]) - (mode == kModeHeadRelative ? 0.0 : double(s->listenerPos[i]));
    }
    // 0..kMaxVolume: its volume in decibels and, in 3D, its distance, which
    // past its minimum distance divides it by 1 + rolloff * (d / min - 1),
    // no further than its maximum distance.
    int heardVolume() const {
        double g = std::pow(10.0, double(std::clamp(volume, -10000, 0)) / 2000.0);
        if (is3D()) {
            double o[3];
            offset(o);
            double d = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]) * double(s->distanceFactor);
            d = std::min<double>(d, std::max(maxDistance, minDistance));
            if (d > minDistance && minDistance > 0)
                g /= 1.0 + double(s->rolloff) * (d / double(minDistance) - 1.0);
        }
        return int(std::clamp(g, 0.0, 1.0) * audio::kMaxVolume);
    }
    // -kMaxPan..kMaxPan: its pan (the other side's attenuation, in
    // decibels) or, in 3D, where it is across the listener.
    int heardPan() const {
        if (is3D()) {
            const float *f = mode == kModeHeadRelative ? nullptr : s->listenerFront;
            const float *u = mode == kModeHeadRelative ? nullptr : s->listenerTop;
            const float front[3] = {0, 0, 1};
            const float top[3] = {0, 1, 0};
            if (!f) {
                f = front;
                u = top;
            }
            // Left-handed: top x front points right.
            const double right[3] = {double(u[1] * f[2] - u[2] * f[1]), double(u[2] * f[0] - u[0] * f[2]),
                                     double(u[0] * f[1] - u[1] * f[0])};
            double o[3];
            offset(o);
            const double d = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
            if (d <= 0)
                return 0;
            const double across = (o[0] * right[0] + o[1] * right[1] + o[2] * right[2]) / d;
            return int(std::clamp(across, -1.0, 1.0) * audio::kMaxPan * 0.8);
        }
        const double other = std::pow(10.0, -double(std::min(std::abs(pan), 10000)) / 2000.0);
        const int p = int((1.0 - other) * audio::kMaxPan);
        return pan < 0 ? -p : p;
    }
    // NOLINTNEXTLINE(readability-make-member-function-const): sets its sound's volume and pan
    void heard() {
        if (sound != audio::kNoSound && s->mixer) {
            s->mixer->setVolume(sound, heardVolume());
            s->mixer->setPan(sound, heardPan());
        }
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        s->update();
        if (&iface == &s->listener)
            return listenerMethod(iface.methods[size_t(index)].name, c);
        if (&iface == &s->buffer3D)
            return buffer3DMethod(iface.methods[size_t(index)].name, c);
        return bufferMethod(iface.methods[size_t(index)].name, c);
    }

    static void out(Cpu &c, uint32_t p, uint32_t v) {
        if (p && c.valid(p, 4))
            c.write32(p, v);
    }
    static void outVector(Cpu &c, uint32_t p, const float v[3]) {
        for (int i = 0; i < 3; ++i)
            out(c, p + 4 * uint32_t(i), bits(v[i]));
    }
    static void inVector(Cpu &c, uint32_t p, float v[3]) {
        if (c.valid(p, 12))
            for (int i = 0; i < 3; ++i)
                v[i] = asFloat(c.read32(p + 4 * uint32_t(i)));
    }

    std::optional<uint32_t> bufferMethod(std::string_view m, Cpu &c) {
        if (m == "GetCaps") { // DSBCAPS*
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 20))
                return kEInvalidArg;
            c.write32(p, 20);
            c.write32(p + 4, flags);
            c.write32(p + 8, size());
            c.write32(p + 12, 0);
            c.write32(p + 16, 0);
            return kSOk;
        }
        if (m == "GetCurrentPosition") { // (play*, write*)
            out(c, c.arg(1), cursor());
            out(c, c.arg(2), writeCursor());
            return kSOk;
        }
        if (m == "GetFormat") { // (format*, room, written*)
            const uint32_t room = c.arg(2);
            const uint32_t n = c.arg(1) ? format.write(c, c.arg(1), room) : 18;
            out(c, c.arg(3), n);
            return kSOk;
        }
        if (m == "GetVolume" || m == "GetPan" || m == "GetFrequency") {
            out(c, c.arg(1), uint32_t(m == "GetVolume" ? volume : m == "GetPan" ? pan : int32_t(rate())));
            return kSOk;
        }
        if (m == "GetStatus") {
            advance();
            out(c, c.arg(1), playing ? kStatusPlaying | (looped ? kStatusLooping : 0) : 0);
            return kSOk;
        }
        if (m == "Initialize")
            return kErrAlreadyInitialized;
        if (m == "Lock") { // (offset, bytes, ptr1*, bytes1*, ptr2*, bytes2*, flags)
            if (!size())
                return kEInvalidArg;
            uint32_t offset = c.arg(1);
            uint32_t bytes = c.arg(2);
            const uint32_t how = c.arg(7);
            if (how & kLockEntireBuffer) {
                offset = 0;
                bytes = size();
            } else if (how & kLockFromWriteCursor) {
                offset = writeCursor();
            }
            if (offset >= size() || bytes == 0)
                return kEInvalidArg;
            bytes = std::min(bytes, size());
            const uint32_t first = std::min(bytes, size() - offset);
            out(c, c.arg(3), wave->address + offset);
            out(c, c.arg(4), first);
            out(c, c.arg(5), bytes > first ? wave->address : 0);
            out(c, c.arg(6), bytes - first);
            return kSOk;
        }
        if (m == "Unlock" || m == "Restore")
            return kSOk;
        if (m == "Play") { // (reserved, priority, flags)
            const bool loop = (c.arg(3) & kPlayLooping) || primary;
            if (playing && looped == loop)
                return kSOk;
            if (playing)
                stoppedPos = cursor();
            start(loop);
            return kSOk;
        }
        if (m == "SetCurrentPosition") {
            const uint32_t at = size() ? c.arg(1) % size() : 0;
            stoppedPos = at - at % std::max<uint32_t>(format.blockAlign, 1);
            if (playing)
                start(looped);
            return kSOk;
        }
        if (m == "SetFormat") { // the primary buffer's
            if (!primary)
                return kEInvalidArg;
            Format f;
            if (!f.read(c, c.arg(1)))
                return kErrBadFormat;
            format = f;
            return kSOk;
        }
        if (m == "SetVolume") {
            volume = int32_t(c.arg(1));
            heard();
            return kSOk;
        }
        if (m == "SetPan") {
            pan = int32_t(c.arg(1));
            heard();
            return kSOk;
        }
        if (m == "SetFrequency") {
            if (primary)
                return kErrControlUnavailable;
            frequency = c.arg(1);
            if (playing) { // its stream plays at the new rate from here
                stoppedPos = cursor();
                start(looped);
            }
            return kSOk;
        }
        if (m == "Stop") {
            stop();
            return kSOk;
        }
        return std::nullopt;
    }

    std::optional<uint32_t> buffer3DMethod(std::string_view m, Cpu &c) {
        if (m == "SetPosition") { // (x, y, z, apply)
            for (int i = 0; i < 3; ++i)
                position[i] = asFloat(c.arg(1 + i));
        } else if (m == "GetPosition") {
            outVector(c, c.arg(1), position);
        } else if (m == "SetMinDistance") {
            minDistance = asFloat(c.arg(1));
        } else if (m == "GetMinDistance") {
            out(c, c.arg(1), bits(minDistance));
        } else if (m == "SetMaxDistance") {
            maxDistance = asFloat(c.arg(1));
        } else if (m == "GetMaxDistance") {
            out(c, c.arg(1), bits(maxDistance));
        } else if (m == "SetMode") {
            mode = c.arg(1);
        } else if (m == "GetMode") {
            out(c, c.arg(1), mode);
        } else if (m == "SetAllParameters" || m == "GetAllParameters") { // DS3DBUFFER*
            // dwSize, vPosition, vVelocity, cone angles (2), cone
            // orientation, cone outside volume, min, max, mode: 64 bytes.
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 64))
                return kEInvalidArg;
            if (m == "SetAllParameters") {
                inVector(c, p + 4, position);
                minDistance = asFloat(c.read32(p + 52));
                maxDistance = asFloat(c.read32(p + 56));
                mode = c.read32(p + 60);
            } else {
                std::memset(c.memory().data() + p + 4, 0, 60);
                outVector(c, p + 4, position);
                c.write32(p + 32, 360);
                c.write32(p + 36, 360);
                c.write32(p + 48, 0);
                c.write32(p + 52, bits(minDistance));
                c.write32(p + 56, bits(maxDistance));
                c.write32(p + 60, mode);
            }
        } else if (m.rfind("Set", 0) == 0) {
            // Velocity (no Doppler shift), cones: kept by nothing.
        } else if (m == "GetVelocity" || m == "GetConeOrientation") {
            const float zero[3] = {0, 0, m == "GetConeOrientation" ? 1.0f : 0.0f};
            outVector(c, c.arg(1), zero);
        } else if (m == "GetConeAngles") {
            out(c, c.arg(1), 360);
            out(c, c.arg(2), 360);
        } else if (m == "GetConeOutsideVolume") {
            out(c, c.arg(1), 0);
        } else {
            return std::nullopt;
        }
        heard();
        return kSOk;
    }

    std::optional<uint32_t> listenerMethod(std::string_view m, Cpu &c) const {
        DirectSound::State &l = *s;
        if (m == "SetPosition") {
            for (int i = 0; i < 3; ++i)
                l.listenerPos[i] = asFloat(c.arg(1 + i));
        } else if (m == "GetPosition") {
            outVector(c, c.arg(1), l.listenerPos);
        } else if (m == "SetOrientation") { // (front xyz, top xyz, apply)
            for (int i = 0; i < 3; ++i) {
                l.listenerFront[i] = asFloat(c.arg(1 + i));
                l.listenerTop[i] = asFloat(c.arg(4 + i));
            }
        } else if (m == "GetOrientation") {
            outVector(c, c.arg(1), l.listenerFront);
            outVector(c, c.arg(2), l.listenerTop);
        } else if (m == "SetRolloffFactor") {
            l.rolloff = asFloat(c.arg(1));
        } else if (m == "GetRolloffFactor") {
            out(c, c.arg(1), bits(l.rolloff));
        } else if (m == "SetDistanceFactor") {
            l.distanceFactor = asFloat(c.arg(1));
        } else if (m == "GetDistanceFactor") {
            out(c, c.arg(1), bits(l.distanceFactor));
        } else if (m == "SetDopplerFactor") {
            l.doppler = asFloat(c.arg(1));
        } else if (m == "GetDopplerFactor") {
            out(c, c.arg(1), bits(l.doppler));
        } else if (m == "SetVelocity" || m == "CommitDeferredSettings") {
            // No Doppler shift; settings deferred are applied at once.
        } else if (m == "GetVelocity") {
            const float zero[3] = {};
            outVector(c, c.arg(1), zero);
        } else if (m == "SetAllParameters" || m == "GetAllParameters") { // DS3DLISTENER*
            // dwSize, vPosition, vVelocity, vOrientFront, vOrientTop,
            // distance, rolloff, Doppler factors: 64 bytes.
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 64))
                return kEInvalidArg;
            if (m == "SetAllParameters") {
                inVector(c, p + 4, l.listenerPos);
                inVector(c, p + 28, l.listenerFront);
                inVector(c, p + 40, l.listenerTop);
                l.distanceFactor = asFloat(c.read32(p + 52));
                l.rolloff = asFloat(c.read32(p + 56));
                l.doppler = asFloat(c.read32(p + 60));
            } else {
                std::memset(c.memory().data() + p + 4, 0, 60);
                outVector(c, p + 4, l.listenerPos);
                outVector(c, p + 28, l.listenerFront);
                outVector(c, p + 40, l.listenerTop);
                c.write32(p + 52, bits(l.distanceFactor));
                c.write32(p + 56, bits(l.rolloff));
                c.write32(p + 60, bits(l.doppler));
            }
        } else {
            return std::nullopt;
        }
        l.heardAll();
        return kSOk;
    }
};

// The DirectSound object: it makes buffers.
class Api : public ComObject {
public:
    DirectSound::State *s = nullptr;

    const ComInterface *query(const std::string &iid) override {
        return iid == s->directSound.iid ? &s->directSound : nullptr;
    }

    std::shared_ptr<Buffer> makeBuffer() const {
        auto b = std::make_shared<Buffer>();
        b->s = s;
        s->buffers.push_back(b);
        s->made++;
        return b;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        s->update();
        if (m == "CreateSoundBuffer") { // (DSBUFFERDESC*, buffer**, outer)
            const uint32_t desc = c.arg(1);
            if (!c.valid(desc, 20) || !c.valid(c.arg(2), 4))
                return kEInvalidArg;
            const uint32_t flags = c.read32(desc + 4);
            const bool primary = (flags & kPrimary) != 0;
            uint32_t bytes = c.read32(desc + 8);
            Format format;
            if (primary) {
                bytes = kPrimarySize;
            } else {
                // DSBSIZE_MIN and DSBSIZE_MAX.
                if (bytes < 4 || bytes > 0x0fffffff || !format.read(c, c.read32(desc + 16)))
                    return kEInvalidArg;
            }
            auto b = makeBuffer();
            b->flags = flags;
            b->primary = primary;
            b->format = format;
            b->wave = std::make_shared<Wave>(s->process, bytes, b->format.bits == 8 ? 0x80 : 0);
            if (!b->wave->address)
                return kEOutOfMemory;
            c.write32(c.arg(2), s->com.pointer(b, s->buffer));
            return kSOk;
        }
        if (m == "DuplicateSoundBuffer") { // (original, duplicate**): the same sound, played apart
            auto original = s->com.shared<Buffer>(c.arg(1));
            if (!original || original->primary || !c.valid(c.arg(2), 4))
                return kEInvalidArg;
            auto b = makeBuffer();
            b->flags = original->flags;
            b->format = original->format;
            b->wave = original->wave;
            b->volume = original->volume;
            b->pan = original->pan;
            b->frequency = original->frequency;
            c.write32(c.arg(2), s->com.pointer(b, s->buffer));
            return kSOk;
        }
        if (m == "GetCaps") { // DSCAPS*: mixed in software, any rate
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 96))
                return kEInvalidArg;
            std::memset(c.memory().data() + p, 0, 96);
            c.write32(p, 96);
            // Primary mono, stereo, 8- and 16-bit; any rate; secondary the same.
            c.write32(p + 4, 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x100 | 0x200 | 0x400 | 0x800);
            c.write32(p + 8, 100);
            c.write32(p + 12, 100000);
            c.write32(p + 16, 1);
            return kSOk;
        }
        if (m == "SetCooperativeLevel" || m == "Compact" || m == "SetSpeakerConfig")
            return kSOk;
        if (m == "GetSpeakerConfig") {
            if (c.valid(c.arg(1), 4))
                c.write32(c.arg(1), kSpeakerStereo);
            return kSOk;
        }
        if (m == "Initialize")
            return kErrAlreadyInitialized;
        return std::nullopt;
    }
};

} // namespace

void DirectSound::State::update() {
    buffers.erase(std::remove_if(buffers.begin(), buffers.end(), [](const auto &w) { return w.expired(); }),
                  buffers.end());
    for (const auto &w : buffers)
        if (auto b = w.lock())
            static_cast<Buffer *>(b.get())->advance();
}

void DirectSound::State::heardAll() {
    for (const auto &w : buffers)
        if (auto b = w.lock())
            static_cast<Buffer *>(b.get())->heard();
}

DirectSound::~DirectSound() = default;

DirectSound::DirectSound(Win32Process &process) : _s(std::make_unique<State>(process)) {
    State *s = _s.get();
    for (ComInterface *f : {&s->directSound, &s->buffer, &s->listener, &s->buffer3D})
        s->com.define(*f);
    process.provideStdcall("DSOUND.dll", "DirectSoundCreate", 3, [s, this](Cpu &c) -> uint32_t {
        // (device GUID, the object**, outer): the default device, whichever asked for.
        if (!c.valid(c.arg(1), 4))
            return kEInvalidArg;
        s->mixer = mixer;
        auto api = std::make_shared<Api>();
        api->s = s;
        c.write32(c.arg(1), s->com.pointer(api, s->directSound));
        return kSOk;
    });
}

size_t DirectSound::buffersMade() const {
    return _s->made;
}

size_t DirectSound::buffersPlaying() const {
    size_t n = 0;
    for (const auto &w : _s->buffers)
        if (auto b = w.lock())
            n += static_cast<Buffer *>(b.get())->playing;
    return n;
}

} // namespace reluva::x86
