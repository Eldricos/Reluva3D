// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// MCI's cdaudio device and the auxiliary CD volume, as Microsoft's WINMM
// documentation describes them (mmsystem.h's commands, flags, structures
// and errors).

#include "emulation/x86/cdaudio.h"

#include "audio/decode.h"
#include "audio/mixer.h"
#include "audio/sound.h"
#include "audio/stream.h"
#include "common/cue.h"
#include "common/stream.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

namespace reluva::x86 {

namespace {

// Commands.
constexpr uint32_t kOpen = 0x803;
constexpr uint32_t kClose = 0x804;
constexpr uint32_t kPlay = 0x806;
constexpr uint32_t kSeek = 0x807;
constexpr uint32_t kStop = 0x808;
constexpr uint32_t kPause = 0x809;
constexpr uint32_t kSet = 0x80d;
constexpr uint32_t kStatus = 0x814;
constexpr uint32_t kResume = 0x855;
// Flags.
constexpr uint32_t kFrom = 0x4;
constexpr uint32_t kTo = 0x8;
constexpr uint32_t kTrack = 0x10;
constexpr uint32_t kOpenTypeId = 0x1000;
constexpr uint32_t kOpenType = 0x2000;
constexpr uint32_t kSetTimeFormat = 0x400;
constexpr uint32_t kStatusItem = 0x100;
constexpr uint32_t kStatusStart = 0x200;
constexpr uint32_t kSeekToStart = 0x100;
constexpr uint32_t kSeekToEnd = 0x200;
// MCI_STATUS's items.
constexpr uint32_t kItemLength = 1;
constexpr uint32_t kItemPosition = 2;
constexpr uint32_t kItemTracks = 3;
constexpr uint32_t kItemMode = 4;
constexpr uint32_t kItemMediaPresent = 5;
constexpr uint32_t kItemTimeFormat = 6;
constexpr uint32_t kItemReady = 7;
constexpr uint32_t kItemCurrentTrack = 8;
constexpr uint32_t kItemTrackType = 0x4001; // MCI_CDA_STATUS_TYPE_TRACK
// Modes, and track types.
constexpr uint32_t kModeNotReady = 524;
constexpr uint32_t kModeStop = 525;
constexpr uint32_t kModePlay = 526;
constexpr uint32_t kModePause = 529;
constexpr uint32_t kTrackAudio = 0x440;
constexpr uint32_t kTrackOther = 0x441;
// Time formats.
constexpr uint32_t kMilliseconds = 0;
constexpr uint32_t kMsf = 2;
constexpr uint32_t kTmsf = 10;
constexpr uint32_t kDeviceTypeCdAudio = 516; // MCI_DEVTYPE_CD_AUDIO
// Errors.
constexpr uint32_t kErrInvalidDeviceId = 257;
constexpr uint32_t kErrUnrecognizedCommand = 261;
constexpr uint32_t kErrInvalidDeviceName = 263;
constexpr uint32_t kErrDeviceNotReady = 276;
constexpr uint32_t kErrOutOfRange = 282;
constexpr uint32_t kErrBadTimeFormat = 293;
constexpr uint32_t kErrBadDeviceId = 2; // MMSYSERR_BADDEVICEID, the aux functions'

constexpr uint32_t kDevice = 1; // the one device's ID
constexpr int kRate = 44100;

// Plays the parts of a run of tracks, one after another: from `from`
// frames into the first, `count` frames in all.
class Span : public audio::Stream {
public:
    Span(std::vector<std::shared_ptr<const audio::Sound>> sounds, size_t from, size_t count)
        : _sounds(std::move(sounds)), _frame(from), _left(count) {}
    int read(int16_t *out, int samples) override {
        int done = 0;
        while (done + 1 < samples && _left > 0 && _index < _sounds.size()) {
            const audio::Sound &s = *_sounds[_index];
            const size_t channels = size_t(std::max(s.channels, 1));
            const size_t frames = s.samples.size() / channels;
            if (_frame >= frames) {
                ++_index;
                _frame = 0;
                continue;
            }
            const int16_t left = s.samples[_frame * channels];
            out[done++] = left;
            out[done++] = channels > 1 ? s.samples[_frame * channels + 1] : left;
            ++_frame;
            --_left;
        }
        return done;
    }
    bool stereo() const override { return true; }
    int rate() const override { return kRate; }
    bool ended() const override { return _left == 0 || _index >= _sounds.size(); }

private:
    std::vector<std::shared_ptr<const audio::Sound>> _sounds;
    size_t _index = 0;
    size_t _frame;
    size_t _left;
};

// A WAV file's length in milliseconds from its header; 0 if it isn't one.
uint32_t wavLengthMs(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary);
    char riff[12];
    if (!f.read(riff, 12) || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0)
        return 0;
    uint32_t bytesPerSecond = 0;
    char chunk[8];
    while (f.read(chunk, 8)) {
        uint32_t size = 0;
        std::memcpy(&size, chunk + 4, 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            char fmt[16];
            if (size < 16 || !f.read(fmt, 16))
                return 0;
            std::memcpy(&bytesPerSecond, fmt + 8, 4);
            f.seekg(std::streamoff(size - 16 + (size & 1)), std::ios::cur);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            return bytesPerSecond ? uint32_t(uint64_t(size) * 1000 / bytesPerSecond) : 0;
        } else {
            f.seekg(std::streamoff(size + (size & 1)), std::ios::cur);
        }
    }
    return 0;
}

} // namespace

CdAudio::Disc CdAudio::discIn(const std::filesystem::path &folder) {
    Disc d;
    std::error_code ec;
    if (folder.empty() || !std::filesystem::is_directory(folder, ec))
        return d;
    // A cue sheet's tracks, read from its image.
    for (const auto &e : std::filesystem::directory_iterator(folder, ec)) {
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        std::vector<CueTrack> tracks;
        if (ext != ".cue" || readCueSheet(e.path(), tracks) || tracks.empty())
            continue;
        auto shared = std::make_shared<std::vector<CueTrack>>(std::move(tracks));
        d.tracks = shared->back().number;
        auto find = [shared](int n) -> const CueTrack * {
            for (const CueTrack &t : *shared)
                if (t.number == n && t.audio)
                    return &t;
            return nullptr;
        };
        d.lengthMs = [find](int n) {
            const CueTrack *t = find(n);
            return t ? uint32_t(t->size / 4 * 1000 / kRate) : 0u;
        };
        d.load = [find](int n) -> std::shared_ptr<const audio::Sound> {
            const CueTrack *t = find(n);
            if (!t)
                return nullptr;
            std::ifstream f(t->file, std::ios::binary);
            std::vector<uint8_t> bytes(size_t(t->size));
            if (!f.seekg(std::streamoff(t->offset)) ||
                !f.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())))
                return nullptr;
            auto s = std::make_shared<audio::Sound>();
            s->rate = kRate;
            s->channels = 2;
            s->samples.resize(bytes.size() / 2);
            for (size_t i = 0; i < s->samples.size(); ++i)
                s->samples[i] =
                    int16_t(t->bigEndian ? bytes[2 * i] << 8 | bytes[2 * i + 1] : bytes[2 * i] | bytes[2 * i + 1] << 8);
            return s;
        };
        return d;
    }
    // Tracks ripped from it, trackNN.wav or .mp3; the last one found ends
    // the disc.
    auto file = [folder](int n) -> std::filesystem::path {
        for (const char *ext : {".wav", ".mp3"}) {
            char name[16];
            std::snprintf(name, sizeof name, "track%02d%s", n, ext);
            std::error_code e;
            for (const auto &entry : std::filesystem::directory_iterator(folder, e)) {
                std::string have = entry.path().filename().string();
                std::transform(have.begin(), have.end(), have.begin(),
                               [](unsigned char c) { return char(std::tolower(c)); });
                if (have == name)
                    return entry.path();
            }
        }
        return {};
    };
    for (int n = 2; n <= 99; ++n)
        if (!file(n).empty())
            d.tracks = n;
    if (!d.tracks)
        return d;
    auto mp3Lengths = std::make_shared<std::map<int, uint32_t>>();
    d.load = [file](int n) -> std::shared_ptr<const audio::Sound> {
        const std::filesystem::path p = file(n);
        std::vector<uint8_t> bytes;
        auto s = std::make_shared<audio::Sound>();
        if (p.empty() || !readFile(p, bytes) || audio::decodeSound(bytes, *s))
            return nullptr;
        return s;
    };
    d.lengthMs = [file, load = d.load, mp3Lengths](int n) -> uint32_t {
        const std::filesystem::path p = file(n);
        if (p.empty())
            return 0;
        if (const uint32_t ms = wavLengthMs(p))
            return ms;
        // An MP3's length is known once decoded.
        if (auto it = mp3Lengths->find(n); it != mp3Lengths->end())
            return it->second;
        const auto s = load(n);
        const uint32_t ms =
            s && s->rate > 0 ? uint32_t(s->samples.size() / size_t(std::max(s->channels, 1)) * 1000 / size_t(s->rate))
                             : 0;
        (*mp3Lengths)[n] = ms;
        return ms;
    };
    return d;
}

struct CdAudio::State {
    CdAudio &owner;
    Win32Process &process;
    Disc disc;
    bool open = false;
    uint32_t timeFormat = kMilliseconds;
    // Where play is on the disc (milliseconds, the tracks end to end):
    // from `fromMs` since `startMs` on the process's clock, up to `toMs`.
    bool playing = false;
    bool paused = false;
    uint32_t fromMs = 0;
    uint32_t toMs = 0;
    uint64_t startMs = 0;
    uint32_t stoppedMs = 0;        // where it is when not playing
    uint32_t volume = 0xffffffffu; // the aux device's: left low word, right high
    audio::SoundHandle sound = audio::kNoSound;

    State(CdAudio &o, Win32Process &p) : owner(o), process(p) {}
    uint64_t now() const { return process.milliseconds ? process.milliseconds() : 0; }

    uint32_t trackLength(int n) const { return disc.lengthMs && n >= 1 && n <= disc.tracks ? disc.lengthMs(n) : 0; }
    uint32_t trackStart(int n) const {
        uint32_t at = 0;
        for (int t = 1; t < n && t <= disc.tracks; ++t)
            at += trackLength(t);
        return at;
    }
    uint32_t discLength() const { return trackStart(disc.tracks + 1); }
    int trackAt(uint32_t ms) const {
        for (int t = 1; t <= disc.tracks; ++t)
            if (trackLength(t) && ms < trackStart(t) + trackLength(t))
                return t;
        return disc.tracks;
    }
    // Where play is now; past its end, it has stopped there.
    uint32_t position() {
        if (!playing)
            return stoppedMs;
        const uint64_t at = fromMs + (now() - startMs);
        if (at >= toMs) {
            playing = false;
            stoppedMs = toMs;
            silence();
            return toMs;
        }
        return uint32_t(at);
    }

    // A position given in the time format, to milliseconds on the disc;
    // false if it's past the disc's end.
    bool toDisc(uint32_t v, uint32_t &ms) const {
        if (timeFormat == kTmsf) {
            const int track = int(v & 0xff);
            if (track < 1 || track > disc.tracks + 1)
                return false;
            ms = trackStart(track) + ((v >> 8 & 0xff) * 60 + (v >> 16 & 0xff)) * 1000 + (v >> 24 & 0xff) * 1000 / 75;
        } else if (timeFormat == kMsf) {
            ms = ((v & 0xff) * 60 + (v >> 8 & 0xff)) * 1000 + (v >> 16 & 0xff) * 1000 / 75;
        } else {
            ms = v;
        }
        return ms <= discLength();
    }
    // Milliseconds as MSF (minutes, seconds, frames of 1/75 s).
    static uint32_t msf(uint32_t ms) { return (ms / 60000) | (ms / 1000 % 60) << 8 | (ms % 1000 * 75 / 1000) << 16; }
    // A position on the disc in the time format.
    uint32_t fromDisc(uint32_t ms) const {
        if (timeFormat == kTmsf) {
            const int t = trackAt(ms);
            const uint32_t in = ms - std::min(ms, trackStart(t));
            return uint32_t(t) | (in / 60000) << 8 | (in / 1000 % 60) << 16 | (in % 1000 * 75 / 1000) << 24;
        }
        return timeFormat == kMsf ? msf(ms) : ms;
    }
    // A length in the time format: MSF for TMSF too.
    uint32_t length(uint32_t ms) const { return timeFormat == kMilliseconds ? ms : msf(ms); }

    void silence() {
        if (sound != audio::kNoSound && owner.mixer)
            owner.mixer->stop(sound);
        sound = audio::kNoSound;
    }
    int mixerVolume() const {
        const uint32_t left = volume & 0xffff;
        const uint32_t right = volume >> 16;
        return int((left + right) / 2 * uint32_t(audio::kMaxVolume) / 0xffff);
    }
    // Plays from `from` to `to` on the disc: the tracks between, loaded now.
    void play(uint32_t from, uint32_t to) {
        silence();
        fromMs = from;
        toMs = std::max(from, to);
        startMs = now();
        playing = toMs > fromMs;
        paused = false;
        if (!playing || !owner.mixer || !disc.load)
            return;
        std::vector<std::shared_ptr<const audio::Sound>> sounds;
        const int first = trackAt(from);
        for (int t = first; t <= disc.tracks && trackStart(t) < toMs; ++t)
            if (trackLength(t))
                if (auto s = disc.load(t))
                    sounds.push_back(std::move(s));
        if (sounds.empty())
            return;
        const size_t skip = size_t(from - trackStart(first)) * kRate / 1000;
        const size_t count = size_t(toMs - fromMs) * kRate / 1000;
        sound = owner.mixer->play(std::make_shared<Span>(std::move(sounds), skip, count), audio::SoundType::kMusic,
                                  mixerVolume());
    }
    void stop() {
        stoppedMs = position();
        playing = false;
        silence();
    }

    uint32_t command(Cpu &c, uint32_t device, uint32_t message, uint32_t flags, uint32_t parms);
};

uint32_t CdAudio::State::command(Cpu &c, uint32_t device, uint32_t message, uint32_t flags, uint32_t parms) {
    auto field = [&](uint32_t at) { return c.valid(parms + at, 4) ? c.read32(parms + at) : 0u; };
    if (message == kOpen) {
        // MCI_OPEN_PARMS: callback, device ID out, device type, element, alias.
        if (!c.valid(parms, 12))
            return kErrInvalidDeviceName;
        bool cd = false;
        if (flags & kOpenTypeId) {
            cd = (field(8) & 0xffff) == kDeviceTypeCdAudio;
        } else if (flags & kOpenType) {
            std::string type = field(8) ? c.string(field(8)) : std::string();
            std::transform(type.begin(), type.end(), type.begin(),
                           [](unsigned char ch) { return char(std::tolower(ch)); });
            cd = type == "cdaudio";
        }
        if (!cd)
            return kErrInvalidDeviceName;
        disc = owner.disc ? owner.disc() : Disc();
        open = true;
        timeFormat = kMilliseconds;
        stoppedMs = 0;
        c.write32(parms + 4, kDevice);
        return 0;
    }
    if (device != kDevice || !open)
        return kErrInvalidDeviceId;
    switch (message) {
    case kClose:
        stop();
        open = false;
        return 0;
    case kSet:
        if (flags & kSetTimeFormat) {
            const uint32_t format = field(4);
            if (format != kMilliseconds && format != kMsf && format != kTmsf)
                return kErrBadTimeFormat;
            timeFormat = format;
        }
        return 0;
    case kPlay: { // MCI_PLAY_PARMS: callback, from, to
        if (!disc.tracks)
            return kErrDeviceNotReady;
        uint32_t from = position();
        uint32_t to = discLength();
        if ((flags & kFrom) && !toDisc(field(4), from))
            return kErrOutOfRange;
        if ((flags & kTo) && !toDisc(field(8), to))
            return kErrOutOfRange;
        play(from, to);
        return 0;
    }
    case kSeek: { // MCI_SEEK_PARMS: callback, to
        if (!disc.tracks)
            return kErrDeviceNotReady;
        uint32_t to = 0;
        if (flags & kSeekToEnd)
            to = discLength();
        else if ((flags & kTo) && !(flags & kSeekToStart) && !toDisc(field(4), to))
            return kErrOutOfRange;
        stop();
        stoppedMs = to;
        return 0;
    }
    case kStop:
        stop();
        paused = false;
        return 0;
    case kPause:
        if (playing) {
            stop();
            paused = true;
        }
        return 0;
    case kResume:
        if (paused)
            play(stoppedMs, toMs);
        return 0;
    case kStatus: { // MCI_STATUS_PARMS: callback, result, item, track
        if (!(flags & kStatusItem) || !c.valid(parms, 16))
            return kErrUnrecognizedCommand;
        const int track = int(field(12));
        const bool forTrack = flags & kTrack;
        if (forTrack && (track < 1 || track > disc.tracks))
            return kErrOutOfRange;
        uint32_t result = 0;
        switch (field(8)) {
        case kItemLength:
            result = length(forTrack ? trackLength(track) : discLength());
            break;
        case kItemPosition:
            result = fromDisc(forTrack ? trackStart(track) : flags & kStatusStart ? 0 : position());
            break;
        case kItemTracks:
            result = uint32_t(disc.tracks);
            break;
        case kItemMode:
            position(); // a track played to its end stops
            result = !disc.tracks ? kModeNotReady : playing ? kModePlay : paused ? kModePause : kModeStop;
            break;
        case kItemMediaPresent:
        case kItemReady:
            result = disc.tracks ? 1 : 0;
            break;
        case kItemTimeFormat:
            result = timeFormat;
            break;
        case kItemCurrentTrack:
            result = uint32_t(trackAt(position()));
            break;
        case kItemTrackType:
            result = forTrack && trackLength(track) ? kTrackAudio : kTrackOther;
            break;
        default:
            return kErrUnrecognizedCommand;
        }
        c.write32(parms + 4, result);
        return 0;
    }
    default:
        return kErrUnrecognizedCommand;
    }
}

CdAudio::CdAudio(Win32Process &process) : _s(std::make_unique<State>(*this, process)) {
    State *s = _s.get();
    const char *dll = "WINMM.dll";
    // mciSendCommandA(device, message, flags, parameters)
    process.provideStdcall(dll, "mciSendCommandA", 4,
                           [s](Cpu &c) { return s->command(c, c.arg(0), c.arg(1), c.arg(2), c.arg(3)); });
    // The CD's volume, as an auxiliary device: AUXCAPSA is a manufacturer,
    // product, version, 32-character name, technology (AUXCAPS_CDAUDIO 1)
    // and support (AUXCAPS_VOLUME 1, AUXCAPS_LRVOLUME 2).
    process.provideStdcall(dll, "auxGetNumDevs", 0, [](Cpu &) { return 1u; });
    process.provideStdcall(dll, "auxGetDevCapsA", 3, [](Cpu &c) {
        if (c.arg(0) != 0)
            return kErrBadDeviceId;
        const uint32_t p = c.arg(1);
        const uint32_t size = std::min(c.arg(2), 48u);
        if (!c.valid(p, size))
            return 11u; // MMSYSERR_INVALPARAM
        uint8_t caps[48]{};
        std::memcpy(caps + 8, "CD Audio", 8);
        caps[40] = 1;
        caps[44] = 0x1 | 0x2;
        std::memcpy(c.memory().data() + p, caps, size);
        return 0u;
    });
    process.provideStdcall(dll, "auxGetVolume", 2, [s](Cpu &c) {
        if (c.arg(0) != 0)
            return kErrBadDeviceId;
        if (c.valid(c.arg(1), 4))
            c.write32(c.arg(1), s->volume);
        return 0u;
    });
    process.provideStdcall(dll, "auxSetVolume", 2, [this, s](Cpu &c) {
        if (c.arg(0) != 0)
            return kErrBadDeviceId;
        s->volume = c.arg(1);
        if (s->sound != audio::kNoSound && mixer)
            mixer->setVolume(s->sound, s->mixerVolume());
        return 0u;
    });
}

CdAudio::~CdAudio() {
    _s->silence();
}

int CdAudio::trackPlaying() const {
    return _s->playing && _s->position() < _s->toMs ? _s->trackAt(_s->position()) : 0;
}

} // namespace reluva::x86
