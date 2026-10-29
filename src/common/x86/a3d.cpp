// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Interface layouts and constants are Aureal's A3D 3.0 SDK documentation's
// (ia3dapi.h, freely distributed by Aureal). Every method a program
// calls was checked against its call sites: the slot it calls and the words
// it pushes (docs/engines/alphateam.md).

#include "common/x86/a3d.h"

#include "common/audio/mixer.h"
#include "common/x86/com.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace reluva::x86 {

namespace {

const char* const kClsidA3dApi = "{92fa2c24-253c-11d2-90fb-006008a1f441}";
const char* const kIidA3d = "{9ae07221-8ac4-11d3-b6aa-00600879f3ee}";      // the A3D object (version 4)
const char* const kIidGeom = "{4730c6c2-b797-11d3-a33d-00104b67d10e}";     // its geometry
// The listener's is the one NiA3dSound asks the A3D object for
// (NiA3dListener::Startup). The source's and the material's are never asked
// for (NewSource and NewMaterial hand them out); they follow on from it.
const char* const kIidListener = "{c398e563-d90b-11d1-90fb-006008a1f441}"; // its listener

constexpr uint32_t kClassNotRegistered = 0x80040154u;
// Play modes; GetStatus's bits.
constexpr uint32_t kLooped = 1;
constexpr uint32_t kStatusPlaying = 0x1, kStatusLooping = 0x4;
// Lock's flags.
constexpr uint32_t kFromWriteCursor = 0x1, kEntireBuffer = 0x2;

const std::vector<ComInterface::Method> kUnknown = {{"QueryInterface", 2}, {"AddRef", 0}, {"Release", 0}};
std::vector<ComInterface::Method> with(std::vector<ComInterface::Method> m) {
    m.insert(m.begin(), kUnknown.begin(), kUnknown.end());
    return m;
}

ComInterface makeA3d() {
    return {"IA3d4", kIidA3d,
            with({{"SetOutputMode", 3},
                  {"GetOutputMode", 3},
                  {"SetResourceManagerMode", 1},
                  {"GetResourceManagerMode", 1},
                  {"SetHFAbsorbFactor", 1},
                  {"GetHFAbsorbFactor", 1},
                  {"RegisterVersion", 1},
                  {"GetSoftwareCaps", 1},
                  {"GetHardwareCaps", 1},
                  {"Clear", 0},
                  {"Flush", 0},
                  {"Compat", 2},
                  {"Init", 3},
                  {"IsFeatureAvailable", 1},
                  {"NewSource", 2},
                  {"DuplicateSource", 2},
                  {"SetCooperativeLevel", 2},
                  {"GetCooperativeLevel", 1},
                  {"SetMaxReflectionDelayTime", 1},
                  {"GetMaxReflectionDelayTime", 1},
                  {"SetCoordinateSystem", 1},
                  {"GetCoordinateSystem", 1},
                  {"SetOutputGain", 1},
                  {"GetOutputGain", 1},
                  {"SetNumFallbackSources", 1},
                  {"GetNumFallbackSources", 1},
                  {"SetRMPriorityBias", 1},
                  {"GetRMPriorityBias", 1},
                  {"DisableViewer", 0},
                  {"SetUnitsPerMeter", 1},
                  {"GetUnitsPerMeter", 1},
                  {"SetDopplerScale", 1},
                  {"GetDopplerScale", 1},
                  {"SetDistanceModelScale", 1},
                  {"GetDistanceModelScale", 1},
                  {"SetEq", 1},
                  {"GetEq", 1},
                  {"Shutdown", 0},
                  {"RegisterApp", 1}}),
            0};
}

ComInterface makeListener() {
    return {"IA3dListener", kIidListener,
            with({{"SetPosition3f", 3},
                  {"GetPosition3f", 3},
                  {"SetPosition3fv", 1},
                  {"GetPosition3fv", 1},
                  {"SetOrientationAngles3f", 3},
                  {"GetOrientationAngles3f", 3},
                  {"SetOrientationAngles3fv", 1},
                  {"GetOrientationAngles3fv", 1},
                  {"SetOrientation6f", 6},
                  {"GetOrientation6f", 6},
                  {"SetOrientation6fv", 1},
                  {"GetOrientation6fv", 1},
                  {"SetVelocity3f", 3},
                  {"GetVelocity3f", 3},
                  {"SetVelocity3fv", 1},
                  {"GetVelocity3fv", 1}}),
            0};
}

// Geometry (occlusions and reflections), drawn in an OpenGL-like way. It's
// heard as nothing: the program's surfaces and materials are kept no record of.
ComInterface makeGeom() {
    return {"IA3dGeom", kIidGeom,
            with({{"Enable", 1},          {"Disable", 1},          {"IsEnabled", 1},
                  {"SetOcclusionMode", 1}, {"GetOcclusionMode", 1}, {"SetReflectionMode", 1},
                  {"GetReflectionMode", 1}, {"SetReflectionGainScale", 1}, {"GetReflectionGainScale", 1},
                  {"SetReflectionDelayScale", 1}, {"GetReflectionDelayScale", 1}, {"PushMatrix", 0},
                  {"PopMatrix", 0},        {"LoadIdentity", 0},     {"LoadMatrix", 1},
                  {"GetMatrix", 1},        {"MultMatrix", 1},       {"Translate3f", 3},
                  {"Translate3fv", 1},     {"Rotate3f", 4},         {"Rotate3fv", 2},
                  {"Scale3f", 3},          {"Scale3fv", 1},         {"Begin", 1},
                  {"End", 0},              {"Vertex3f", 3},         {"Vertex3fv", 1},
                  {"Normal3f", 3},         {"Normal3fv", 1},        {"Tag", 1},
                  {"SetOpeningFactorf", 1}, {"SetOpeningFactorfv", 1}, {"NewMaterial", 1},
                  {"BindMaterial", 1},     {"NewList", 1},          {"NewEnvironment", 1},
                  {"BindListener", 0},     {"BindSource", 1},       {"BindEnvironment", 1},
                  {"Method42", 1},         {"Method43", 1},         {"Method44", 1},
                  {"Method45", 1},         {"Method46", 1},         {"Method47", 1},
                  {"Method48", 1},         {"Method49", 1}}),
            0};
}

ComInterface makeMaterial() {
    return {"IA3dMaterial", "{c398e565-d90b-11d1-90fb-006008a1f441}",
            with({{"Load", 1},
                  {"Save", 1},
                  {"UnSerialize", 2},
                  {"Serialize", 2},
                  {"Duplicate", 1},
                  {"SetNameID", 1},
                  {"GetNameID", 1},
                  {"SelectPreset", 1},
                  {"GetClosestPreset", 1},
                  {"SetReflectance", 2},
                  {"GetReflectance", 2},
                  {"SetTransmittance", 2},
                  {"GetTransmittance", 2}}),
            0};
}

ComInterface makeSource() {
    return {"IA3dSource", "{c398e564-d90b-11d1-90fb-006008a1f441}",
            with({{"LoadWaveFile", 1},
                  {"LoadWaveData", 2},
                  {"AllocateWaveData", 1},
                  {"FreeWaveData", 0},
                  {"SetWaveFormat", 1},
                  {"GetWaveFormat", 1},
                  {"GetWaveSize", 0},
                  {"GetType", 1},
                  {"Lock", 7},
                  {"Unlock", 4},
                  {"Play", 1},
                  {"Stop", 0},
                  {"Rewind", 0},
                  {"SetWaveTime", 1},
                  {"GetWaveTime", 1},
                  {"SetWavePosition", 1},
                  {"GetWavePosition", 1},
                  {"SetPosition3f", 3},
                  {"GetPosition3f", 3},
                  {"SetPosition3fv", 1},
                  {"GetPosition3fv", 1},
                  {"SetOrientationAngles3f", 3},
                  {"GetOrientationAngles3f", 3},
                  {"SetOrientationAngles3fv", 1},
                  {"GetOrientationAngles3fv", 1},
                  {"SetOrientation6f", 6},
                  {"GetOrientation6f", 6},
                  {"SetOrientation6fv", 1},
                  {"GetOrientation6fv", 1},
                  {"SetVelocity3f", 3},
                  {"GetVelocity3f", 3},
                  {"SetVelocity3fv", 1},
                  {"GetVelocity3fv", 1},
                  {"SetCone", 3},
                  {"GetCone", 3},
                  {"SetMinMaxDistance", 3},
                  {"GetMinMaxDistance", 3},
                  {"SetGain", 1},
                  {"GetGain", 1},
                  {"SetPitch", 1},
                  {"GetPitch", 1},
                  {"SetDopplerScale", 1},
                  {"GetDopplerScale", 1},
                  {"SetDistanceModelScale", 1},
                  {"GetDistanceModelScale", 1},
                  {"SetEq", 1},
                  {"GetEq", 1},
                  {"SetPriority", 1},
                  {"GetPriority", 1},
                  {"SetRenderMode", 1},
                  {"GetRenderMode", 1},
                  {"GetAudibility", 1},
                  {"GetOcclusionFactor", 1},
                  {"GetStatus", 1},
                  {"SetPanValues", 2},
                  {"GetPanValues", 2},
                  {"SetWaveEvent", 2},
                  {"ClearWaveEvents", 0},
                  {"SetTransformMode", 1},
                  {"GetTransformMode", 1},
                  {"SetReflectionDelayScale", 1},
                  {"GetReflectionDelayScale", 1},
                  {"SetReflectionGainScale", 1},
                  {"GetReflectionGainScale", 1}}),
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

} // namespace

struct A3d::State {
    Win32Process& process;
    Cpu& cpu;
    ComHost com;
    ComInterface a3d = makeA3d(), listener = makeListener(), geom = makeGeom(), material = makeMaterial(),
                 source = makeSource();
    audio::Mixer* mixer = nullptr;
    // The listener, for the sources' gains and pans.
    float listenerPos[3]{}, listenerFront[3]{0, 0, -1}, listenerTop[3]{0, 1, 0};
    float outputGain = 1, unitsPerMeter = 1, distanceModelScale = 1;
    bool leftHanded = false; // SetCoordinateSystem: A3D_RIGHT_HANDED_CS 0, A3D_LEFT_HANDED_CS 1
    size_t made = 0;
    std::vector<std::weak_ptr<ComObject>> sources;
    explicit State(Win32Process& p) : process(p), cpu(p.cpu()), com(p) {}
    uint32_t now() { return process.milliseconds ? process.milliseconds() : 0; }
    void update();
};

namespace {

// A sound source: its wave data in the process's memory, and its playing.
class Source : public ComObject {
public:
    A3d::State* s = nullptr;
    uint32_t type = 0;
    // WAVEFORMATEX.
    uint16_t formatTag = 1, channels = 1, blockAlign = 2, bitsPerSample = 16;
    uint32_t rate = 22050, bytesPerSecond = 44100;
    uint32_t data = 0, size = 0; // in the process's memory
    bool playing = false, looped = false;
    bool formatSet = false; // SetWaveFormat called, or the format copied (DuplicateSource)
    uint32_t startMs = 0, startPos = 0, stoppedPos = 0; // where it was when last started, or stopped
    uint32_t fed = 0;                                   // how far it's been handed to the mixer
    float gain = 1, pitch = 1, position[3]{}, minDistance = 1, maxDistance = 1e9f, priority = 0.5f;
    bool muteBeyondMax = false; // past its maximum distance: silent, or kept at the level there
    float distanceScale = 1;    // how fast it fades with distance: 1 as in the world, 2 twice as fast
    std::vector<std::pair<uint32_t, uint32_t>> events; // wave events: offset, event handle
    std::shared_ptr<audio::SoundStream> stream;
    audio::SoundHandle sound = audio::kNoSound;

    ~Source() override {
        silence();
        if (data)
            s->process.memory().free(data);
    }

    const ComInterface* query(const std::string& iid) override { return iid == s->source.iid ? &s->source : nullptr; }

    // The play position (bytes, a whole block), as of the process's clock.
    uint32_t cursor() {
        if (!playing || !size)
            return stoppedPos;
        const double elapsed = double(s->now() - startMs) / 1000.0;
        uint64_t moved = uint64_t(elapsed * double(bytesPerSecond) * double(pitch));
        moved -= moved % std::max<uint32_t>(blockAlign, 1);
        const uint64_t at = uint64_t(startPos) + moved;
        if (looped)
            return uint32_t(at % size);
        if (at >= size) { // played to its end
            feedTo(size);
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
    // Moves the play position along: what it passes goes to the mixer, and
    // its wave events are passed.
    void advance() {
        if (!playing)
            return;
        const uint32_t to = cursor();
        if (playing)
            feedTo(to);
    }
    // Hands the mixer what's between `fed` and `to` (wrapping round).
    void feedTo(uint32_t to) {
        if (to == fed)
            return;
        auto span = [&](uint32_t from, uint32_t end) {
            for (const auto& [offset, event] : events)
                if (offset >= from && (offset < end || (end == size && offset >= size)))
                    s->process.setEvent(event); // one at or past the end is passed at the end
            if (stream && end > from)
                push(from, end);
        };
        if (to > fed) {
            span(fed, to);
        } else {
            span(fed, size);
            span(0, to);
        }
        fed = to;
    }
    void push(uint32_t from, uint32_t end) {
        if (formatTag != 1 || !s->cpu.valid(data + from, end - from))
            return;
        const uint8_t* p = s->cpu.memory().data() + data + from;
        std::vector<int16_t> out;
        if (bitsPerSample == 8) {
            out.resize(end - from);
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = int16_t((int(p[i]) - 128) << 8);
        } else {
            out.resize((end - from) / 2);
            std::memcpy(out.data(), p, out.size() * 2);
        }
        out.resize(out.size() - out.size() % channels);
        stream->push(out.data(), out.size());
    }
    void start(bool loop) {
        silence();
        looped = loop;
        startPos = stoppedPos;
        fed = startPos;
        startMs = s->now();
        playing = size > 0;
        if (playing && s->mixer && formatTag == 1 && (bitsPerSample == 8 || bitsPerSample == 16) && channels >= 1 &&
            channels <= 2) {
            stream = std::make_shared<audio::SoundStream>(int(rate), int(channels));
            sound = s->mixer->playStream(stream, audio::SoundType::Effects, volume(), pan(), 200);
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
    // The gain heard, 0..256: the source's own, the output's, and its
    // distance from the listener. Beyond its minimum distance it fades as
    // min / (min + scale * (distance - min)), the distance going no further
    // than its maximum, the scale being its own and the A3D object's
    // distance model scales (1: inverse distance).
    int volume() const {
        double g = double(gain) * double(s->outputGain);
        if (!(type & 0x1)) { // 3D unless A3DSOURCE_INITIAL_RENDERMODE_NATIVE (bit 0)
            double d = 0;
            for (int i = 0; i < 3; ++i)
                d += double(position[i] - s->listenerPos[i]) * double(position[i] - s->listenerPos[i]);
            d = std::sqrt(d);
            if (muteBeyondMax && d > maxDistance)
                return 0;
            d = std::min<double>(d, std::max<double>(maxDistance, minDistance));
            const double scale = std::max(0.0, double(distanceScale) * double(s->distanceModelScale));
            if (d > minDistance && minDistance > 0)
                g *= double(minDistance) / (double(minDistance) + scale * (d - double(minDistance)));
        }
        return int(std::clamp(g, 0.0, 1.0) * audio::kMaxVolume);
    }
    // Left -127 .. right 127, from where it is across the listener.
    int pan() const {
        if (type & 0x1)
            return 0;
        const float* f = s->listenerFront;
        const float* u = s->listenerTop;
        // Front x top points right in a right-handed system, left in a
        // left-handed one.
        const float side = s->leftHanded ? -1.0f : 1.0f;
        const float right[3] = {side * (f[1] * u[2] - f[2] * u[1]), side * (f[2] * u[0] - f[0] * u[2]),
                                side * (f[0] * u[1] - f[1] * u[0])};
        double dx = 0, d = 0;
        for (int i = 0; i < 3; ++i) {
            const double r = double(position[i] - s->listenerPos[i]);
            dx += r * double(right[i]);
            d += r * r;
        }
        if (d <= 0)
            return 0;
        return int(std::clamp(dx / std::sqrt(d), -1.0, 1.0) * audio::kMaxPan * 0.8);
    }
    void heard() {
        if (sound != audio::kNoSound && s->mixer) {
            s->mixer->setVolume(sound, volume());
            s->mixer->setPan(sound, pan());
        }
    }

    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        s->update();
        auto out = [&](int arg, uint32_t v) {
            if (c.valid(c.arg(arg), 4))
                c.write32(c.arg(arg), v);
        };
        auto outF = [&](int arg, float v) { out(arg, bits(v)); };
        if (m == "AllocateWaveData") { // (bytes)
            if (data)
                s->process.memory().free(data);
            size = c.arg(1);
            data = size ? s->process.memory().alloc(size) : 0;
            if (data)
                std::memset(c.memory().data() + data, bitsPerSample == 8 ? 0x80 : 0, size);
            playing = false;
            stoppedPos = fed = 0;
            return data || !size ? kSOk : kEOutOfMemory;
        }
        if (m == "FreeWaveData") {
            stop();
            if (data)
                s->process.memory().free(data);
            data = size = 0;
            return kSOk;
        }
        if (m == "LoadWaveData") { // (data, bytes): a copy of it
            const uint32_t from = c.arg(1), bytes = c.arg(2);
            if (data)
                s->process.memory().free(data);
            size = bytes;
            data = size ? s->process.memory().alloc(size) : 0;
            if (data && c.valid(from, size))
                std::memcpy(c.memory().data() + data, c.memory().data() + from, size);
            stoppedPos = fed = 0;
            return kSOk;
        }
        if (m == "SetWaveFormat" || m == "GetWaveFormat") { // WAVEFORMATEX*
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 16))
                return kEInvalidArg;
            if (m == "GetWaveFormat") {
                // A source with no wave yet has no format: NiA3dSound asks
                // this to tell whether a source still needs its file loaded.
                if (!formatSet && !size)
                    return kEFail;
                c.write16(p, formatTag);
                c.write16(p + 2, channels);
                c.write32(p + 4, rate);
                c.write32(p + 8, bytesPerSecond);
                c.write16(p + 12, blockAlign);
                c.write16(p + 14, bitsPerSample);
                return kSOk;
            }
            formatSet = true;
            formatTag = c.read16(p);
            channels = std::max<uint16_t>(c.read16(p + 2), 1);
            rate = c.read32(p + 4);
            bytesPerSecond = c.read32(p + 8);
            blockAlign = std::max<uint16_t>(c.read16(p + 12), 1);
            bitsPerSample = c.read16(p + 14);
            if (!bytesPerSecond)
                bytesPerSecond = rate * blockAlign;
            return kSOk;
        }
        if (m == "GetWaveSize")
            return size;
        if (m == "GetType") {
            out(1, type);
            return kSOk;
        }
        // Lock(offset, bytes, &p1, &n1, &p2, &n2, flags): the part asked
        // for, in two pieces when it wraps round the end.
        if (m == "Lock") {
            uint32_t offset = c.arg(1), bytes = c.arg(2);
            const uint32_t flags = c.arg(7);
            if (flags & kFromWriteCursor)
                offset = cursor();
            if (flags & kEntireBuffer)
                bytes = size;
            if (!size || offset >= size || bytes > size)
                return kEInvalidArg;
            const uint32_t first = std::min(bytes, size - offset);
            out(3, data + offset);
            out(4, first);
            out(5, first < bytes ? data : 0);
            out(6, bytes - first);
            return kSOk;
        }
        if (m == "Unlock")
            return kSOk;
        if (m == "Play") { // (mode): A3D_SINGLE 0, A3D_LOOPED 1
            if (playing)
                stop();
            start(c.arg(1) == kLooped);
            return kSOk;
        }
        if (m == "Stop") {
            stop();
            return kSOk;
        }
        if (m == "Rewind") {
            const bool was = playing;
            stop();
            stoppedPos = 0;
            if (was)
                start(looped);
            return kSOk;
        }
        if (m == "SetWavePosition" || m == "SetWaveTime") {
            uint32_t at = m == "SetWavePosition"
                              ? c.arg(1)
                              : uint32_t(double(asFloat(c.arg(1))) * double(bytesPerSecond));
            at -= at % blockAlign;
            at = size ? std::min(at, size - 1 - (size - 1) % blockAlign) : 0;
            const bool was = playing;
            stop();
            stoppedPos = at;
            if (was)
                start(looped);
            return kSOk;
        }
        if (m == "GetWavePosition") {
            out(1, cursor());
            return kSOk;
        }
        if (m == "GetWaveTime") {
            outF(1, bytesPerSecond ? float(double(cursor()) / double(bytesPerSecond)) : 0.0f);
            return kSOk;
        }
        if (m == "GetStatus") {
            const uint32_t at = cursor();
            (void)at;
            out(1, playing ? kStatusPlaying | (looped ? kStatusLooping : 0) : 0);
            return kSOk;
        }
        if (m == "SetWaveEvent") { // (offset, event): set as play passes it
            events.push_back({c.arg(1), c.arg(2)});
            s->process.trackEvent(c.arg(2));
            return kSOk;
        }
        if (m == "ClearWaveEvents") {
            events.clear();
            return kSOk;
        }
        if (m == "SetPosition3f" || m == "SetPosition3fv") {
            const bool v = m == "SetPosition3fv";
            for (int i = 0; i < 3; ++i)
                position[i] = asFloat(v ? (c.valid(c.arg(1), 12) ? c.read32(c.arg(1) + 4 * uint32_t(i)) : 0)
                                        : c.arg(1 + i));
            heard();
            return kSOk;
        }
        if (m == "GetPosition3f") {
            for (int i = 0; i < 3; ++i)
                outF(1 + i, position[i]);
            return kSOk;
        }
        if (m == "SetGain" || m == "SetPitch" || m == "SetPriority" || m == "SetDistanceModelScale") {
            const float v = asFloat(c.arg(1));
            (m == "SetGain" ? gain : m == "SetPitch" ? pitch : m == "SetPriority" ? priority : distanceScale) = v;
            if (m == "SetPitch" && playing) { // its rate changes from here
                const uint32_t at = cursor();
                stoppedPos = at;
                startPos = at;
                startMs = s->now();
            }
            heard();
            return kSOk;
        }
        if (m == "GetDistanceModelScale") {
            outF(1, distanceScale);
            return kSOk;
        }
        if (m == "GetGain" || m == "GetPitch" || m == "GetPriority") {
            outF(1, m == "GetGain" ? gain : m == "GetPitch" ? pitch : priority);
            return kSOk;
        }
        if (m == "SetMinMaxDistance") { // (min, max, mode): A3D_AUDIBLE 0, A3D_MUTE 1
            minDistance = asFloat(c.arg(1));
            maxDistance = asFloat(c.arg(2));
            muteBeyondMax = c.arg(3) == 1;
            heard();
            return kSOk;
        }
        if (m == "GetMinMaxDistance") {
            outF(1, minDistance);
            outF(2, maxDistance);
            out(3, muteBeyondMax ? 1 : 0);
            return kSOk;
        }
        if (m == "SetRenderMode") {
            type = (type & ~0x1u) | (c.arg(1) & 0x1);
            heard();
            return kSOk;
        }
        if (m == "GetRenderMode") {
            out(1, type & 0x1);
            return kSOk;
        }
        if (m == "GetAudibility") {
            outF(1, playing ? float(volume()) / audio::kMaxVolume : 0.0f);
            return kSOk;
        }
        if (m == "GetOcclusionFactor") {
            outF(1, 0.0f);
            return kSOk;
        }
        // The rest is kept as nothing: orientation, velocity, cones,
        // Doppler, EQ, reflections. A Get gives zeros.
        if (m.substr(0, 3) == "Set")
            return kSOk;
        if (m.substr(0, 3) == "Get") {
            for (int i = 1; i <= iface.methods[size_t(index)].args; ++i)
                out(i, 0);
            return kSOk;
        }
        return std::nullopt;
    }
};

// A geometry material: nothing heard of it.
class Material : public ComObject {
public:
    A3d::State* s = nullptr;
    const ComInterface* query(const std::string& iid) override {
        return iid == s->material.iid ? &s->material : nullptr;
    }
    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (m.substr(0, 3) == "Get") {
            for (int i = 1; i <= iface.methods[size_t(index)].args; ++i)
                if (c.valid(c.arg(i), 4))
                    c.write32(c.arg(i), 0);
            return kSOk;
        }
        if (m.substr(0, 3) == "Set" || m == "SelectPreset")
            return kSOk;
        return std::nullopt;
    }
};

// The A3D object: also its listener and its geometry.
class Api : public ComObject {
public:
    A3d::State* s = nullptr;
    const ComInterface* query(const std::string& iid) override {
        for (ComInterface* f : {&s->a3d, &s->listener, &s->geom})
            if (iid == f->iid)
                return f;
        return nullptr;
    }
    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        s->update();
        auto out = [&](int arg, uint32_t v) {
            if (c.valid(c.arg(arg), 4))
                c.write32(c.arg(arg), v);
        };
        if (m == "SetCoordinateSystem") {
            s->leftHanded = c.arg(1) == 1;
            return kSOk;
        }
        if (&iface == &s->listener) {
            float* target = m == "SetPosition3f" ? s->listenerPos : nullptr;
            if (target) {
                for (int i = 0; i < 3; ++i)
                    target[i] = asFloat(c.arg(1 + i));
                return kSOk;
            }
            if (m == "SetOrientation6f") { // front, then top
                for (int i = 0; i < 3; ++i) {
                    s->listenerFront[i] = asFloat(c.arg(1 + i));
                    s->listenerTop[i] = asFloat(c.arg(4 + i));
                }
                return kSOk;
            }
            if (m == "GetPosition3f") {
                for (int i = 0; i < 3; ++i)
                    out(1 + i, bits(s->listenerPos[i]));
                return kSOk;
            }
            if (m.substr(0, 3) == "Set")
                return kSOk;
            if (m.substr(0, 3) == "Get") {
                for (int i = 1; i <= iface.methods[size_t(index)].args; ++i)
                    out(i, 0);
                return kSOk;
            }
            return std::nullopt;
        }
        if (&iface == &s->geom) {
            if (m == "NewMaterial") { // (out)
                auto material = std::make_shared<Material>();
                material->s = s;
                out(1, s->com.pointer(material, s->material));
                return kSOk;
            }
            if (m == "IsEnabled")
                return 0u; // FALSE
            if (m.substr(0, 3) == "Get") {
                for (int i = 1; i <= iface.methods[size_t(index)].args; ++i)
                    out(i, 0);
                return kSOk;
            }
            // Drawing, binding, enabling: nothing heard.
            return kSOk;
        }
        // IA3d4.
        if (m == "Init" || m == "SetCooperativeLevel" || m == "SetCoordinateSystem" || m == "Clear" ||
            m == "RegisterVersion" || m == "RegisterApp" || m == "SetOutputMode" || m == "SetResourceManagerMode" ||
            m == "Compat" || m == "Shutdown" || m == "DisableViewer")
            return kSOk;
        if (m == "Flush")
            return kSOk; // the sources are brought up to date on every call
        if (m == "IsFeatureAvailable")
            return 0u; // FALSE: no reflections, occlusions or other features
        // NewSource(type, out); DuplicateSource(source, out)
        if (m == "NewSource" || m == "DuplicateSource") {
            auto source = std::make_shared<Source>();
            source->s = s;
            if (m == "NewSource") {
                source->type = c.arg(1);
            } else if (auto from = s->com.shared<Source>(c.arg(1))) {
                source->type = from->type;
                source->formatTag = from->formatTag, source->channels = from->channels;
                source->rate = from->rate, source->bytesPerSecond = from->bytesPerSecond;
                source->blockAlign = from->blockAlign, source->bitsPerSample = from->bitsPerSample;
                source->size = from->size;
                source->formatSet = from->formatSet;
                source->data = from->size ? s->process.memory().alloc(from->size) : 0;
                if (source->data)
                    std::memcpy(c.memory().data() + source->data, c.memory().data() + from->data, from->size);
                source->gain = from->gain, source->pitch = from->pitch;
            }
            s->sources.push_back(source);
            ++s->made;
            out(2, s->com.pointer(source, s->source));
            return kSOk;
        }
        float* value = m == "SetOutputGain" || m == "GetOutputGain"                 ? &s->outputGain
                       : m == "SetUnitsPerMeter" || m == "GetUnitsPerMeter"         ? &s->unitsPerMeter
                       : m == "SetDistanceModelScale" || m == "GetDistanceModelScale" ? &s->distanceModelScale
                                                                                      : nullptr;
        if (value && m.substr(0, 3) == "Set") {
            *value = asFloat(c.arg(1));
            return kSOk;
        }
        if (value) {
            out(1, bits(*value));
            return kSOk;
        }
        if (m.substr(0, 3) == "Set")
            return kSOk;
        if (m.substr(0, 3) == "Get") {
            for (int i = 1; i <= iface.methods[size_t(index)].args; ++i)
                out(i, 0);
            return kSOk;
        }
        return std::nullopt;
    }
};

} // namespace

// Every source brought up to date with the clock (on each call into A3D).
void A3d::State::update() {
    sources.erase(std::remove_if(sources.begin(), sources.end(), [](const auto& w) { return w.expired(); }),
                  sources.end());
    for (const auto& w : sources)
        if (auto source = w.lock())
            static_cast<Source*>(source.get())->advance();
}

A3d::~A3d() = default;

A3d::A3d(Win32Process& process) : s_(std::make_unique<State>(process)) {
    State* s = s_.get();
    for (ComInterface* f : {&s->a3d, &s->listener, &s->geom, &s->material, &s->source})
        s->com.define(*f);
    auto earlier = process.createComObject;
    process.createComObject = [s, this, earlier](const std::string& clsid, const std::string& iid, uint32_t out) {
        if (clsid != kClsidA3dApi)
            return earlier ? earlier(clsid, iid, out) : kClassNotRegistered;
        s->mixer = mixer;
        auto api = std::make_shared<Api>();
        api->s = s;
        const ComInterface* f = api->query(iid);
        if (!f)
            return kENoInterface;
        if (s->cpu.valid(out, 4))
            s->cpu.write32(out, s->com.pointer(api, *f));
        return kSOk;
    };
}

size_t A3d::sourcesMade() const { return s_->made; }

size_t A3d::sourcesPlaying() const {
    size_t n = 0;
    for (const auto& w : s_->sources)
        if (auto source = w.lock())
            n += static_cast<Source*>(source.get())->playing;
    return n;
}

} // namespace reluva::x86
