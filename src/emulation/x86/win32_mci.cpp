// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WINMM's MCI, as Microsoft documents it, for CD audio: a program opens
// the CD-ROM drive's "cdaudio" device and reads the disc's table of
// contents (Win32Process::disc) from it. Playing is accepted, and not
// heard. Other MCI devices aren't there.

#include "emulation/x86/win32.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <sstream>
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

std::string lowerCase(std::string s) {
    for (char &ch : s)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

} // namespace

std::optional<Win32Process::DiscContents>
Win32Process::DiscContents::fromCueSheet(const std::string &text,
                                         const std::function<std::optional<uint64_t>(const std::string &)> &fileSize,
                                         std::string &error) {
    // Each file's tracks: their sector size, INDEX 01 within the file, and
    // the gap before them that isn't in it.
    struct CueTrack {
        uint32_t sectorBytes = 2352;
        bool audio = true;
        int64_t index = -1;
        uint32_t pregap = 0;
    };
    struct CueFile {
        uint64_t bytes = 0;
        std::vector<CueTrack> tracks;
    };
    std::vector<CueFile> files;
    auto msf = [](const std::string &s, uint32_t &frames) {
        int m = 0;
        int sec = 0;
        int f = 0;
        if (std::sscanf(s.c_str(), "%d:%d:%d", &m, &sec, &f) != 3 || m < 0 || sec < 0 || sec > 59 || f < 0 || f > 74)
            return false;
        frames = uint32_t((m * 60 + sec) * 75 + f);
        return true;
    };
    std::istringstream lines(text);
    std::string line;
    for (int number = 1; std::getline(lines, line); ++number) {
        std::istringstream words(line);
        std::string keyword;
        words >> keyword;
        keyword = lowerCase(keyword);
        auto fail = [&](const std::string &why) {
            error = "line " + std::to_string(number) + ": " + why;
            return std::nullopt;
        };
        if (keyword == "file") {
            // FILE "name" TYPE: the name may be quoted, with spaces.
            const size_t open = line.find('"');
            const size_t close = open == std::string::npos ? open : line.find('"', open + 1);
            std::string name;
            if (close != std::string::npos) {
                name = line.substr(open + 1, close - open - 1);
            } else {
                words >> name;
            }
            const auto size = fileSize(name);
            if (!size)
                return fail("there's no file " + name);
            files.push_back({*size, {}});
        } else if (keyword == "track") {
            std::string numberText;
            std::string type;
            words >> numberText >> type;
            if (files.empty())
                return fail("a track before any file");
            type = lowerCase(type);
            CueTrack t;
            t.audio = type == "audio";
            if (type == "mode1/2048" || type == "mode2/2048")
                t.sectorBytes = 2048;
            else if (type == "mode2/2336")
                t.sectorBytes = 2336;
            else if (type != "audio" && type != "mode1/2352" && type != "mode2/2352")
                return fail("a track of type " + type + " isn't known");
            files.back().tracks.push_back(t);
        } else if (keyword == "index" || keyword == "pregap") {
            if (files.empty() || files.back().tracks.empty())
                return fail(keyword + " outside a track");
            std::string first;
            std::string second;
            words >> first >> second;
            uint32_t frames = 0;
            if (keyword == "pregap") {
                if (!msf(first, frames))
                    return fail("PREGAP's time isn't mm:ss:ff");
                files.back().tracks.back().pregap = frames;
            } else if (std::atoi(first.c_str()) == 1) {
                if (!msf(second, frames))
                    return fail("INDEX 01's time isn't mm:ss:ff");
                files.back().tracks.back().index = frames;
            }
        }
    }
    DiscContents disc;
    uint64_t at = 150; // the frame the next file starts at
    for (const CueFile &f : files) {
        uint64_t gaps = 0;
        for (size_t i = 0; i < f.tracks.size(); ++i) {
            const CueTrack &t = f.tracks[i];
            if (t.index < 0) {
                error = "a track without INDEX 01";
                return std::nullopt;
            }
            gaps += t.pregap;
            disc.tracks.push_back({uint32_t(at + gaps + uint64_t(t.index)), t.audio});
        }
        // The file's length in sectors: what's before its last track, then
        // that track's sectors to the end.
        uint64_t before = f.tracks.empty() ? 0 : uint64_t(f.tracks[0].index) * f.tracks[0].sectorBytes;
        for (size_t i = 0; i + 1 < f.tracks.size(); ++i)
            before += uint64_t(f.tracks[i + 1].index - f.tracks[i].index) * f.tracks[i].sectorBytes;
        const CueTrack &last = f.tracks.empty() ? CueTrack{} : f.tracks.back();
        const uint64_t lastIndex = f.tracks.empty() ? 0 : uint64_t(last.index);
        if (f.bytes < before) {
            error = "a file shorter than its tracks";
            return std::nullopt;
        }
        at += gaps + lastIndex + (f.bytes - before) / last.sectorBytes;
    }
    if (disc.tracks.empty()) {
        error = "no tracks";
        return std::nullopt;
    }
    disc.end = uint32_t(at);
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
            _mciDevices[id] = kFormatMsf;
            c.write32(p + 4, id);
            return done(id);
        }
        auto it = _mciDevices.find(device);
        if (it == _mciDevices.end())
            return kErrInvalidDeviceId;
        uint32_t &format = it->second;
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
        case kMciResume:
            if (!disc)
                return kErrDeviceNotReady;
            return done(device);
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
            answer = disc ? kModeStop : kModeOpen;
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
            case kStatusPosition: // a track's start in TMSF is the track alone
                answer = format == kFormatTmsf
                             ? (ofTrack ? n : 1u)
                             : inFormat((flags & kStatusStart) || !ofTrack ? tracks[0].start : t.start, format);
                break;
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
