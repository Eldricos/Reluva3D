// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WINMM's MCI, as Microsoft documents it, for CD audio: a program opens
// the CD-ROM drive's "cdaudio" device and reads the disc's table of
// contents (Win32Process::disc) from it. Playing is accepted, and not
// heard. Other MCI devices aren't there.

#include "emulation/x86/win32.h"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>

namespace reluva::x86 {

namespace {

enum : uint32_t {
    kMciOpen = 0x803,
    kMciClose = 0x804,
    kMciPlay = 0x806,
    kMciSeek = 0x807,
    kMciStop = 0x808,
    kMciPause = 0x809,
    kMciSet = 0x80d,
    kMciStatus = 0x814,
    kMciResume = 0x855,
};
enum : uint32_t {
    kNotify = 0x1,
    kFrom = 0x4,
    kTo = 0x8,
    kTrack = 0x10,
    kOpenElement = 0x200,
    kOpenTypeId = 0x1000,
    kOpenType = 0x2000,
    kStatusItem = 0x100,
    kStatusStart = 0x200,
    kSetTimeFormat = 0x400,
};
enum : uint32_t {
    kFormatMilliseconds = 0,
    kFormatMsf = 2,
    kFormatFrames = 3,
    kFormatTmsf = 10,
};
enum : uint32_t {
    kStatusLength = 1,
    kStatusPosition = 2,
    kStatusNumberOfTracks = 3,
    kStatusMode = 4,
    kStatusMediaPresent = 5,
    kStatusTimeFormat = 6,
    kStatusReady = 7,
    kStatusCurrentTrack = 8,
    kCdaStatusTypeTrack = 0x4001,
};
// MCIERR_BASE (256) and on.
enum : uint32_t {
    kErrInvalidDeviceId = 257,
    kErrUnrecognizedCommand = 261,
    kErrInvalidDeviceName = 263,
    kErrUnsupportedFunction = 274,
    kErrDeviceNotReady = 276,
    kErrOutOfRange = 282,
    kErrBadTimeFormat = 293,
    kErrMissingParameter = 317,
};
constexpr uint32_t kDevTypeCdAudio = 516;
constexpr uint32_t kModeStop = 525;
constexpr uint32_t kModePlay = 526;
constexpr uint32_t kModePause = 529;
constexpr uint32_t kModeOpen = 530;
constexpr uint32_t kCdaTrackAudio = 1088;
constexpr uint32_t kCdaTrackOther = 1089;
constexpr uint32_t kMmMciNotify = 0x3b9;
constexpr uint32_t kFramesPerSecond = 75;

// Frames as MCI gives them in a time format: MSF packs minutes, seconds
// and frames from the low byte up.
uint32_t inFormat(uint32_t frames, uint32_t format) {
    switch (format) {
    case kFormatMilliseconds:
        return uint32_t(uint64_t(frames) * 1000 / kFramesPerSecond);
    case kFormatFrames:
        return frames;
    default: {
        const uint32_t minutes = frames / (kFramesPerSecond * 60);
        const uint32_t seconds = frames / kFramesPerSecond % 60;
        return minutes | seconds << 8 | (frames % kFramesPerSecond) << 16;
    }
    }
}

// A time in a format as frames: TMSF's is a track and the time into it.
uint32_t fromFormat(uint32_t value, uint32_t format, const Win32Process::DiscContents &disc) {
    const uint32_t msf =
        (value & 0xff) * 60 * kFramesPerSecond + (value >> 8 & 0xff) * kFramesPerSecond + (value >> 16 & 0xff);
    switch (format) {
    case kFormatMilliseconds:
        return uint32_t(uint64_t(value) * kFramesPerSecond / 1000);
    case kFormatFrames:
        return value;
    case kFormatMsf:
        return msf;
    default: {
        const uint32_t track = value & 0xff;
        const uint32_t start = track >= 1 && track <= disc.tracks.size() ? disc.tracks[track - 1].start : 0;
        const uint32_t into =
            (value >> 8 & 0xff) * 60 * kFramesPerSecond + (value >> 16 & 0xff) * kFramesPerSecond + (value >> 24);
        return start + into;
    }
    }
}

std::string lowerCase(std::string s) {
    for (char &ch : s)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

} // namespace

Win32Process::DiscContents Win32Process::DiscContents::fromCueTracks(const std::vector<CueTrack> &tracks) {
    DiscContents disc;
    uint64_t at = 150; // the disc's first track's data starts two seconds in
    for (const CueTrack &t : tracks) {
        at += t.pregap + t.lead;
        disc.tracks.push_back({uint32_t(at), t.audio});
        at += t.size / uint64_t(t.sectorSize);
    }
    disc.end = uint32_t(at);
    return disc;
}

// A media descriptor: "MEDIA DESCRIPTOR", the number of sessions (+0x14,
// 16 bits) and where their blocks are (+0x50). A session's block (24
// bytes) has its number of entries (+0xa) and where they are (+0x14); an
// entry (80 bytes) is the disc's table of contents' own: its control and
// ADR (+2, the ADR in the high half; control bit 4: data), its point
// (+4: a track's number, or 0xa2 for the lead-out) and where that is
// (+9 to +0xb: minutes, seconds, frames). Worked out from Star Trek:
// Armada's disc's, where each track's start sector (+0x24), its place in
// the image (+0x28, 2,352 bytes a sector), the lengths after the entries
// and the session's end (+4) all agree with it.
std::optional<Win32Process::DiscContents>
Win32Process::DiscContents::fromMediaDescriptor(const std::vector<uint8_t> &file, std::string &error) {
    auto u16 = [&](size_t at) { return at + 2 <= file.size() ? uint32_t(file[at] | file[at + 1] << 8) : 0u; };
    auto u32 = [&](size_t at) { return u16(at) | u16(at + 2) << 16; };
    if (file.size() < 0x58 || std::string(file.begin(), file.begin() + 16) != "MEDIA DESCRIPTOR") {
        error = "not a media descriptor";
        return std::nullopt;
    }
    DiscContents disc;
    const uint32_t sessions = u16(0x14);
    const uint32_t sessionsAt = u32(0x50);
    for (uint32_t s = 0; s < sessions; ++s) {
        const size_t session = sessionsAt + 24 * size_t(s);
        const uint32_t entries = file.size() > session + 0xa ? file[session + 0xa] : 0;
        const size_t entriesAt = u32(session + 0x14);
        for (uint32_t e = 0; e < entries; ++e) {
            const size_t at = entriesAt + 80 * size_t(e);
            if (at + 80 > file.size()) {
                error = "an entry past the file's end";
                return std::nullopt;
            }
            const uint32_t point = file[at + 4];
            const uint32_t frame = (uint32_t(file[at + 9]) * 60 + file[at + 10]) * kFramesPerSecond + file[at + 11];
            if (point >= 1 && point <= 99)
                disc.tracks.push_back({frame, !(file[at + 2] & 0x04)});
            else if (point == 0xa2)
                disc.end = frame;
        }
    }
    if (disc.tracks.empty() || !disc.end) {
        error = "no tracks, or no lead-out";
        return std::nullopt;
    }
    return disc;
}

void Win32Process::addMciStandIns() {
    const char *w = "WINMM.dll";

    // mciSendCommandA(device, message, flags, parameters): the parameters'
    // first word is the window to notify (MCI_NOTIFY), with MM_MCINOTIFY
    // once the command is done (MCI_NOTIFY_SUCCESSFUL).
    stdcall(w, "mciSendCommandA", 4, [this](Cpu &c) -> uint32_t {
        const uint32_t device = c.arg(0);
        const uint32_t message = c.arg(1);
        const uint32_t flags = c.arg(2);
        const uint32_t p = c.arg(3);
        auto done = [&](uint32_t id) {
            if ((flags & kNotify) && p && c.valid(p, 4) && c.read32(p))
                postMessage(c.read32(p), kMmMciNotify, 1, id);
            return 0u;
        };
        if (message == kMciOpen) {
            // MCI_OPEN_PARMS: the device's ID (+4) set; its type (+8), by
            // name or (MCI_OPEN_TYPE_ID) number; its element (+12), a drive.
            if (!c.valid(p, 20))
                return kErrMissingParameter;
            bool cdAudio = false;
            if (flags & kOpenType)
                cdAudio = (flags & kOpenTypeId) ? (c.read32(p + 8) & 0xffff) == kDevTypeCdAudio
                                                : lowerCase(c.string(c.read32(p + 8))) == "cdaudio";
            if (!cdAudio)
                return kErrInvalidDeviceName;
            if (flags & kOpenElement) {
                const std::string drive = c.string(c.read32(p + 12));
                if (drive.empty() || std::toupper(static_cast<unsigned char>(drive[0])) !=
                                         std::toupper(static_cast<unsigned char>(cdDrive[0])))
                    return kErrInvalidDeviceName;
            }
            const uint32_t id = _nextMciDevice++;
            _mciDevices[id].at = disc ? disc->tracks[0].start : 0;
            c.write32(p + 4, id);
            return done(id);
        }
        auto it = _mciDevices.find(device);
        if (it == _mciDevices.end())
            return kErrInvalidDeviceId;
        MciDevice &dev = it->second;
        uint32_t &format = dev.format;
        // Where it is now: playing moves on with the clock, and stops at
        // `to`.
        auto position = [&]() -> uint32_t {
            if (!dev.playing)
                return dev.at;
            const uint64_t moved = (now() - dev.started) * kFramesPerSecond / 1000;
            if (dev.at + moved >= dev.to) {
                dev.playing = false;
                dev.at = dev.to;
                return dev.at;
            }
            return dev.at + uint32_t(moved);
        };
        switch (message) {
        case kMciClose:
            _mciDevices.erase(it);
            return done(device);
        case kMciSet:
            if (flags & kSetTimeFormat) {
                const uint32_t asked = c.valid(p, 8) ? c.read32(p + 4) : ~0u;
                if (asked != kFormatMilliseconds && asked != kFormatMsf && asked != kFormatFrames &&
                    asked != kFormatTmsf)
                    return kErrBadTimeFormat;
                format = asked;
            }
            return done(device);
        case kMciPlay:
        case kMciSeek:
        case kMciStop:
        case kMciPause:
        case kMciResume: {
            if (!disc)
                return kErrDeviceNotReady;
            // MCI_PLAY_PARMS and MCI_SEEK_PARMS: from (+4), to (+8).
            const bool wide = message == kMciPlay || message == kMciSeek;
            const uint32_t from =
                wide && (flags & kFrom) && c.valid(p, 12) ? fromFormat(c.read32(p + 4), format, *disc) : position();
            if (wide && (from < disc->tracks[0].start || from > disc->end))
                return kErrOutOfRange;
            dev.at = message == kMciStop || message == kMciPause ? position() : from;
            dev.playing = false;
            dev.paused = message == kMciPause;
            if (message == kMciPlay || message == kMciResume) {
                if (message == kMciPlay)
                    dev.to = (flags & kTo) && c.valid(p, 12) ? fromFormat(c.read32(p + 8), format, *disc) : disc->end;
                dev.playing = dev.at < dev.to;
                dev.started = now();
            }
            return done(device);
        }
        case kMciStatus:
            break;
        default:
            return kErrUnrecognizedCommand;
        }
        // MCI_STATUS_PARMS: what's asked (+8), of which track (+12, with
        // MCI_TRACK); the answer at +4.
        if (!(flags & kStatusItem) || !c.valid(p, 16))
            return kErrMissingParameter;
        const uint32_t item = c.read32(p + 8);
        uint32_t answer = 0;
        if (item == kStatusMediaPresent || item == kStatusReady) {
            answer = disc ? 1 : 0;
        } else if (item == kStatusMode) {
            position();
            answer = !disc ? kModeOpen : dev.playing ? kModePlay : dev.paused ? kModePause : kModeStop;
        } else if (item == kStatusTimeFormat) {
            answer = format;
        } else if (!disc) {
            return kErrDeviceNotReady;
        } else {
            const auto &tracks = disc->tracks;
            const uint32_t n = c.read32(p + 12);
            const bool ofTrack = flags & kTrack;
            if (ofTrack && (n < 1 || n > tracks.size()))
                return kErrOutOfRange;
            const auto &t = tracks[ofTrack ? n - 1 : 0];
            const uint32_t trackEnd = ofTrack && n < tracks.size() ? tracks[n].start : disc->end;
            switch (item) {
            case kStatusNumberOfTracks:
                answer = uint32_t(tracks.size());
                break;
            case kStatusCurrentTrack:
                answer = 1;
                break;
            case kStatusLength: // a length in TMSF is in MSF
                answer = inFormat(ofTrack ? trackEnd - t.start : disc->end - tracks[0].start,
                                  format == kFormatTmsf ? kFormatMsf : format);
                break;
            case kStatusPosition: {
                // A track's start, the disc's (MCI_STATUS_START), or where
                // it is; in TMSF, the track and the time into it.
                const uint32_t at = ofTrack ? t.start : (flags & kStatusStart) ? tracks[0].start : position();
                if (format != kFormatTmsf) {
                    answer = inFormat(at, format);
                    break;
                }
                uint32_t track = 1;
                while (track < tracks.size() && tracks[track].start <= at)
                    ++track;
                answer = track | inFormat(at - tracks[track - 1].start, kFormatMsf) << 8;
                break;
            }
            case kCdaStatusTypeTrack:
                answer = t.audio ? kCdaTrackAudio : kCdaTrackOther;
                break;
            default:
                return kErrUnsupportedFunction;
            }
        }
        c.write32(p + 4, answer);
        return done(device);
    });
    // mciGetErrorStringA(error, text, size): the error's number, as text.
    stdcall(w, "mciGetErrorStringA", 3, [](Cpu &c) -> uint32_t {
        const std::string text = "MCI error " + std::to_string(c.arg(0));
        if (!c.arg(2) || !c.valid(c.arg(1), c.arg(2)))
            return 0u;
        c.writeString(c.arg(1), text, c.arg(2));
        return 1u;
    });
}

} // namespace reluva::x86
