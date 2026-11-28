// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectSound, as the DirectX 7 documentation describes it. Interface
// method orders, GUIDs and structure layouts are the SDK's dsound.h.

#include "emulation/x86/directx/dsound.h"

#include "emulation/x86/com.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <string_view>

namespace reluva::x86 {

namespace {

const char *const kIidDirectSound = "{279afa83-4981-11ce-a521-0020af0be560}";
const char *const kIidBuffer = "{279afa85-4981-11ce-a521-0020af0be560}";
const char *const kIidListener = "{279afa84-4981-11ce-a521-0020af0be560}";
const char *const kIidBuffer3d = "{279afa86-4981-11ce-a521-0020af0be560}";
const char *const kIidNotify = "{b0210783-89cd-11d0-af08-00a0c925cd16}";

constexpr uint32_t kOk = 0;
constexpr uint32_t kErrInvalidParam = 0x80070057u;
constexpr uint32_t kErrBadFormat = 0x88780064u;
// DSBUFFERDESC's flags.
constexpr uint32_t kPrimaryBuffer = 0x1;
constexpr uint32_t kCtrl3d = 0x10;
constexpr uint32_t kCtrlPositionNotify = 0x100;
// A notification's offset for when the buffer stops (DSBPN_OFFSETSTOP).
constexpr uint32_t kOffsetStop = 0xffffffffu;
constexpr uint32_t kLocHardware = 0x40000;
constexpr uint32_t kLocSoftware = 0x80000;
// The primary buffer's size, in seconds of its format: programs can't
// write to it, so any will do.
constexpr uint32_t kPrimarySeconds = 1;

const std::vector<ComInterface::Method> kUnknown = {{"QueryInterface", 2}, {"AddRef", 0}, {"Release", 0}};
std::vector<ComInterface::Method> with(std::vector<ComInterface::Method> m) {
    m.insert(m.begin(), kUnknown.begin(), kUnknown.end());
    return m;
}

const std::vector<ComInterface::Method> kDirectSoundMethods = with({
    {"CreateSoundBuffer", 3},
    {"GetCaps", 1},
    {"DuplicateSoundBuffer", 2},
    {"SetCooperativeLevel", 2},
    {"Compact", 0},
    {"GetSpeakerConfig", 1},
    {"SetSpeakerConfig", 1},
    {"Initialize", 1},
});
const std::vector<ComInterface::Method> kNotifyMethods = with({{"SetNotificationPositions", 2}});
const std::vector<ComInterface::Method> kBufferMethods = with({
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
});
// Vectors are three floats, passed by value as three words.
const std::vector<ComInterface::Method> kListenerMethods = with({
    {"GetAllParameters", 1},
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
    {"CommitDeferredSettings", 0},
});
const std::vector<ComInterface::Method> kBuffer3dMethods = with({
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
});

} // namespace

struct DirectSound::State {
    Win32Process &process;
    ComHost com;
    ComInterface directSound{"IDirectSound", kIidDirectSound, kDirectSoundMethods};
    ComInterface buffer{"IDirectSoundBuffer", kIidBuffer, kBufferMethods};
    ComInterface listener{"IDirectSound3DListener", kIidListener, kListenerMethods};
    ComInterface buffer3d{"IDirectSound3DBuffer", kIidBuffer3d, kBuffer3dMethods};
    ComInterface notify{"IDirectSoundNotify", kIidNotify, kNotifyMethods};
    explicit State(Win32Process &p) : process(p), com(p) {}
};

namespace {

// A buffer's memory, shared by the buffers DuplicateSoundBuffer makes.
struct Memory {
    Win32Process &process;
    uint32_t address = 0;
    uint32_t size = 0;
    Memory(Win32Process &p, uint32_t bytes)
        : process(p), address(p.memory().alloc(std::max(bytes, 16u))), size(bytes) {}
    Memory(const Memory &) = delete;
    Memory &operator=(const Memory &) = delete;
    ~Memory() {
        if (address)
            process.memory().free(address);
    }
};

// A WAVEFORMATEX's PCM fields.
struct Format {
    uint32_t channels = 2;
    uint32_t rate = 22050;
    uint32_t bits = 16;
    uint32_t blockAlign = 4;
};

bool readFormat(Cpu &c, uint32_t p, Format &f) {
    if (!c.valid(p, 16))
        return false;
    Format r;
    r.channels = c.read16(p + 2);
    r.rate = c.read32(p + 4);
    r.blockAlign = c.read16(p + 12);
    r.bits = c.read16(p + 14);
    if (!r.channels || !r.rate || !r.blockAlign)
        return false;
    f = r;
    return true;
}

// Where each of the 3D interfaces' values is in DS3DBUFFER or
// DS3DLISTENER (in words), and how many words it is; a getter given two
// pointers (GetOrientation, GetConeAngles) fills each with as many.
struct Param3d {
    const char *name;
    uint32_t at;
    uint32_t words;
    uint32_t pointers = 1;
};
const Param3d kListenerParams[] = {
    {"Position", 1, 3},        {"Velocity", 4, 3},       {"Orientation", 7, 3, 2},
    {"DistanceFactor", 13, 1}, {"RolloffFactor", 14, 1}, {"DopplerFactor", 15, 1},
};
const Param3d kBuffer3dParams[] = {
    {"Position", 1, 3},           {"Velocity", 4, 3},     {"ConeAngles", 7, 1, 2}, {"ConeOrientation", 9, 3},
    {"ConeOutsideVolume", 12, 1}, {"MinDistance", 13, 1}, {"MaxDistance", 14, 1},  {"Mode", 15, 1},
};
constexpr uint32_t kOne = 0x3f800000u; // 1.0f

// A buffer; with DSBCAPS_CTRL3D, also its 3D interface (or, the primary
// one's, the listener's), which only keeps what it's set to.
class Buffer : public ComObject {
public:
    DirectSound::State *s = nullptr;
    std::shared_ptr<Memory> memory;
    uint32_t flags = 0;
    Format format;
    bool primary = false;
    int32_t volume = 0;
    int32_t pan = 0;
    bool playing = false;
    bool looping = false;
    // Where it was at `since` (in now()), playing on from there.
    uint32_t start = 0;
    uint64_t since = 0;
    // DS3DBUFFER or DS3DLISTENER, as last set: by default where the
    // documentation says, a buffer at the origin, heard from 1 unit, its
    // cones whole; the listener there too, facing +z, +y up.
    uint32_t params3d[16]{};

    void reset3d() {
        params3d[0] = 64;
        if (primary) {
            params3d[9] = params3d[11] = kOne;                 // front z, top y
            params3d[13] = params3d[14] = params3d[15] = kOne; // the factors
        } else {
            params3d[7] = params3d[8] = 360; // the cone angles
            params3d[11] = kOne;             // the cone along z
            params3d[13] = kOne;             // the minimum distance
            params3d[14] = 0x4e6e6b28u;      // the maximum, 1e9 (DS3D_DEFAULTMAXDISTANCE)
        }
    }

    uint32_t size() const { return memory ? memory->size : 0; }

    // The play cursor now, stopping a sound that has played to its end.
    uint32_t position() {
        const uint32_t bytes = size();
        if (!playing || !bytes)
            return start;
        const uint64_t now = s->process.now();
        const uint64_t frames = (now - since) * format.rate / 1000;
        const uint64_t at = start + frames * format.blockAlign;
        if (!looping && at >= bytes) {
            playing = false;
            start = 0;
            since = now;
            return 0;
        }
        return uint32_t(at % bytes);
    }
    // Where the cursor has got to since it was last put somewhere, in bytes,
    // not wrapped round.
    int64_t played() const {
        if (!playing)
            return int64_t(start);
        const uint64_t frames = (s->process.now() - since) * format.rate / 1000;
        return int64_t(start) + int64_t(frames * format.blockAlign);
    }
    void moveTo(uint32_t at) {
        start = size() ? at % size() : 0;
        since = s->process.now();
    }

    const ComInterface *query(const std::string &iid) override {
        if (iid == kIidBuffer)
            return &s->buffer;
        if ((flags & kCtrl3d) && iid == (primary ? kIidListener : kIidBuffer3d))
            return primary ? &s->listener : &s->buffer3d;
        if (!primary && (flags & kCtrlPositionNotify) && iid == kIidNotify)
            return &s->notify;
        return nullptr;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (&iface == &s->buffer)
            return invokeBuffer(m, c);
        if (&iface == &s->notify)
            return invokeNotify(m, c);
        // The 3D interfaces: values kept as set, and given back. Settings
        // are made at once, deferred (DS3D_DEFERRED) or not.
        if (m == "SetAllParameters" || m == "GetAllParameters") {
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 4) || c.read32(p) < sizeof params3d || !c.valid(p, sizeof params3d))
                return kErrInvalidParam;
            for (uint32_t i = 1; i < 16; ++i)
                if (m[0] == 'S')
                    params3d[i] = c.read32(p + 4 * i);
                else
                    c.write32(p + 4 * i, params3d[i]);
            return kOk;
        }
        if (m == "CommitDeferredSettings")
            return kOk;
        const bool set = m.starts_with("Set");
        const std::string_view what = m.substr(3);
        for (const Param3d &param :
             primary ? std::span<const Param3d>(kListenerParams) : std::span<const Param3d>(kBuffer3dParams)) {
            if (what != param.name)
                continue;
            for (uint32_t i = 0; i < param.words * param.pointers; ++i) {
                if (set) {
                    params3d[param.at + i] = c.arg(1 + i);
                    continue;
                }
                const uint32_t to = c.arg(1 + i / param.words);
                if (!c.valid(to, 4 * param.words))
                    return kErrInvalidParam;
                c.write32(to + 4 * (i % param.words), params3d[param.at + i]);
            }
            return kOk;
        }
        return std::nullopt;
    }

    // SetNotificationPositions(count, DSBPOSITIONNOTIFY*: offset, event):
    // each event set as the play cursor passes its offset, each time round,
    // or as the buffer stops; as threads wait on it, by the clock.
    std::optional<uint32_t> invokeNotify(std::string_view m, Cpu &c) {
        if (m != "SetNotificationPositions")
            return std::nullopt;
        const uint32_t count = c.arg(1);
        const uint32_t at = c.arg(2);
        if (playing || (count && !c.valid(at, 8 * count)))
            return kErrInvalidParam; // not while it plays, as Windows has it
        auto self = std::weak_ptr<Buffer>(std::static_pointer_cast<Buffer>(shared_from_this()));
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t offset = c.read32(at + 8 * i);
            const uint32_t event = c.read32(at + 8 * i + 4);
            // Counted in bytes played since the cursor was last put
            // somewhere, so a whole time round between looks is seen.
            position();
            uint32_t from = start;
            uint64_t fromSince = since;
            int64_t last = played();
            bool wasPlaying = playing;
            s->process.signalEventEachTime(event, [self, offset, from, fromSince, last, wasPlaying]() mutable {
                auto b = self.lock();
                if (!b || !b->size())
                    return false;
                b->position();
                if (b->start != from || b->since != fromSince) {
                    from = b->start;
                    fromSince = b->since;
                    last = int64_t(b->start);
                }
                const int64_t now = b->played();
                bool passed = false;
                if (offset == kOffsetStop) {
                    passed = wasPlaying && !b->playing;
                } else {
                    const int64_t size = int64_t(b->size());
                    auto laps = [&](int64_t at) {
                        const int64_t d = at - int64_t(offset);
                        return d >= 0 ? d / size : -((-d + size - 1) / size);
                    };
                    passed = laps(now) > laps(last);
                }
                last = now;
                wasPlaying = b->playing;
                return passed;
            });
        }
        return kOk;
    }

    std::optional<uint32_t> invokeBuffer(std::string_view m, Cpu &c) {
        // GetCaps(DSBCAPS*): size, flags, buffer bytes.
        if (m == "GetCaps") {
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 20))
                return kErrInvalidParam;
            c.write32(p + 4, primary ? kPrimaryBuffer | (flags & kCtrl3d) : (flags & ~kLocHardware) | kLocSoftware);
            c.write32(p + 8, size());
            c.write32(p + 12, 0);
            c.write32(p + 16, 0);
            return kOk;
        }
        // GetCurrentPosition(play*, write*): the write cursor 10 ms ahead,
        // where a program may next write (Windows' lead varies by card).
        if (m == "GetCurrentPosition") {
            const uint32_t at = position();
            if (c.arg(1))
                c.write32(c.arg(1), at);
            if (c.arg(2))
                c.write32(c.arg(2), size() ? (at + format.rate / 100 * format.blockAlign) % size() : 0);
            return kOk;
        }
        if (m == "GetFormat") { // (WAVEFORMATEX*, size, written*)
            if (c.arg(1) && c.arg(2) >= 16 && c.valid(c.arg(1), 16)) {
                c.write16(c.arg(1), 1); // WAVE_FORMAT_PCM
                c.write16(c.arg(1) + 2, uint16_t(format.channels));
                c.write32(c.arg(1) + 4, format.rate);
                c.write32(c.arg(1) + 8, format.rate * format.blockAlign);
                c.write16(c.arg(1) + 12, uint16_t(format.blockAlign));
                c.write16(c.arg(1) + 14, uint16_t(format.bits));
            }
            if (c.arg(3))
                c.write32(c.arg(3), 16);
            return kOk;
        }
        if (m == "GetVolume" || m == "GetPan" || m == "GetFrequency") {
            c.write32(c.arg(1), m == "GetVolume" ? uint32_t(volume) : m == "GetPan" ? uint32_t(pan) : format.rate);
            return kOk;
        }
        if (m == "GetStatus") { // DSBSTATUS_PLAYING 1, DSBSTATUS_LOOPING 4
            position();
            c.write32(c.arg(1), playing ? (looping ? 5u : 1u) : 0u);
            return kOk;
        }
        // Lock(offset, bytes, part1*, size1*, part2*, size2*, flags): what's
        // asked for, wrapping round the end as a second part.
        if (m == "Lock") {
            if (primary || !size())
                return kErrInvalidParam;
            uint32_t offset = c.arg(1);
            uint32_t bytes = c.arg(2);
            if (c.arg(7) & 1) // DSBLOCK_FROMWRITECURSOR
                offset = position();
            if (c.arg(7) & 2) // DSBLOCK_ENTIREBUFFER
                bytes = size();
            if (bytes > size() || offset >= size())
                return kErrInvalidParam;
            const uint32_t first = std::min(bytes, size() - offset);
            c.write32(c.arg(3), memory->address + offset);
            c.write32(c.arg(4), first);
            if (c.arg(5))
                c.write32(c.arg(5), bytes > first ? memory->address : 0);
            if (c.arg(6))
                c.write32(c.arg(6), bytes - first);
            return kOk;
        }
        if (m == "Play") { // (reserved, priority, flags: DSBPLAY_LOOPING 1)
            moveTo(position());
            playing = true;
            looping = primary || (c.arg(3) & 1);
            return kOk;
        }
        if (m == "Stop") {
            moveTo(position());
            playing = false;
            return kOk;
        }
        if (m == "SetCurrentPosition") {
            moveTo(c.arg(1));
            return kOk;
        }
        if (m == "SetFormat") {
            if (!primary)
                return 0x88780032u; // DSERR_INVALIDCALL: only the primary's
            return readFormat(c, c.arg(1), format) ? kOk : kErrBadFormat;
        }
        if (m == "SetFrequency") { // 0: the format's own
            moveTo(position());
            if (c.arg(1))
                format.rate = c.arg(1);
            return kOk;
        }
        if (m == "SetVolume" || m == "SetPan") {
            (m == "SetVolume" ? volume : pan) = int32_t(c.arg(1));
            return kOk;
        }
        if (m == "Unlock" || m == "Restore" || m == "Initialize")
            return kOk;
        return std::nullopt;
    }
};

// The device.
class Device : public ComObject {
public:
    DirectSound::State *s = nullptr;
    Format primaryFormat;
    std::weak_ptr<Buffer> primary;

    const ComInterface *query(const std::string &iid) override {
        return iid == kIidDirectSound ? &s->directSound : nullptr;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // CreateSoundBuffer(DSBUFFERDESC*, buffer*, outer): the desc's flags
        // (+4), bytes (+8) and format (+16).
        if (m == "CreateSoundBuffer") {
            const uint32_t desc = c.arg(1);
            if (!c.valid(desc, 20) || !c.valid(c.arg(2), 4))
                return kErrInvalidParam;
            c.write32(c.arg(2), 0);
            auto b = std::make_shared<Buffer>();
            b->s = s;
            b->flags = c.read32(desc + 4);
            b->primary = b->flags & kPrimaryBuffer;
            b->reset3d();
            if (b->primary) {
                // There's one primary buffer, which every request gets.
                if (auto existing = primary.lock()) {
                    ++existing->refs;
                    c.write32(c.arg(2), s->com.pointer(existing, s->buffer));
                    return kOk;
                }
                b->format = primaryFormat;
                b->memory =
                    std::make_shared<Memory>(s->process, kPrimarySeconds * b->format.rate * b->format.blockAlign);
                primary = b;
            } else {
                if (!readFormat(c, c.read32(desc + 16), b->format))
                    return kErrBadFormat;
                const uint32_t bytes = c.read32(desc + 8);
                if (bytes < 4 || bytes > 0x0fffffff)
                    return kErrInvalidParam;
                b->memory = std::make_shared<Memory>(s->process, bytes);
            }
            if (!b->memory->address)
                return kEOutOfMemory;
            c.write32(c.arg(2), s->com.pointer(b, s->buffer));
            return kOk;
        }
        // GetCaps(DSCAPS*): a card of software buffers only, 8 and 16 bits,
        // mono and stereo, at any rate.
        if (m == "GetCaps") {
            const uint32_t p = c.arg(1);
            const uint32_t size = c.valid(p, 4) ? std::min<uint32_t>(c.read32(p), 96) : 0;
            if (size < 12 || !c.valid(p, size))
                return kErrInvalidParam;
            std::memset(c.memory().data() + p + 4, 0, size - 4);
            c.write32(p + 4, 0x1 | 0x2 | 0x4 | 0x8 | 0x10); // primary mono, stereo, 8, 16 bits; any rate
            c.write32(p + 8, 100);
            if (size >= 16)
                c.write32(p + 12, 100000);
            return kOk;
        }
        // DuplicateSoundBuffer(original, duplicate*): a buffer of its own on
        // the original's memory, stopped at the start.
        if (m == "DuplicateSoundBuffer") {
            auto original = s->com.shared<Buffer>(c.arg(1));
            if (!original || original->primary || !c.valid(c.arg(2), 4))
                return kErrInvalidParam;
            auto b = std::make_shared<Buffer>();
            b->s = s;
            b->memory = original->memory;
            b->flags = original->flags;
            b->format = original->format;
            b->reset3d();
            b->volume = original->volume;
            b->pan = original->pan;
            c.write32(c.arg(2), s->com.pointer(b, s->buffer));
            return kOk;
        }
        if (m == "GetSpeakerConfig") {
            c.write32(c.arg(1), 4); // DSSPEAKER_STEREO
            return kOk;
        }
        if (m == "SetCooperativeLevel" || m == "Compact" || m == "SetSpeakerConfig" || m == "Initialize")
            return kOk;
        return std::nullopt;
    }
};

} // namespace

DirectSound::~DirectSound() = default;

DirectSound::DirectSound(Win32Process &process) : _s(std::make_unique<State>(process)) {
    State *s = _s.get();
    for (ComInterface *f : {&s->directSound, &s->buffer, &s->listener, &s->buffer3d, &s->notify})
        s->com.define(*f);
    // DirectSoundCreate(device guid, out, outer): the default device, whatever's asked for.
    auto create = [s](Cpu &c) {
        if (!c.valid(c.arg(1), 4))
            return kErrInvalidParam;
        auto device = std::make_shared<Device>();
        device->s = s;
        c.write32(c.arg(1), s->com.pointer(device, s->directSound));
        return kOk;
    };
    process.provideStdcall("DSOUND.dll", "DirectSoundCreate", 3, create);
    process.provideStdcall("DSOUND.dll", "#1", 3, create);
}

} // namespace reluva::x86
