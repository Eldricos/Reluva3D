// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The DirectSound stand-in (src/emulation/x86/dsound.cpp), called as a
// program would: the devices listed, the primary buffer set up, a
// secondary buffer written, played by a clock the test sets and mixed,
// and PlaySoundA's wave.
//
//   reluva-tests common.unit.dsound

#include "audio/mixer.h"
#include "audio/sound.h"
#include "audio/wav.h"
#include "emulation/x86/com.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/dsound.h"
#include "emulation/x86/win32.h"
#include "tests/support/test.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace reluva;

namespace {

struct Fixture {
    x86::Cpu cpu{16u << 20};
    x86::Win32Process process{cpu};
    x86::DirectSound sound{process};
    uint32_t clock = 1000;
    uint32_t mem = 0;
    Fixture() {
        mem = process.memory().alloc(4096);
        process.milliseconds = [this] { return clock; };
    }
    uint32_t exported(const char *dll, const char *name, const std::vector<uint32_t> &args) {
        uint32_t r = 0;
        CHECK(!cpu.call(process.standIn(dll, name), args, r));
        return r;
    }
    // Method `index` (from 0, IUnknown's included) of the interface `self` points at.
    uint32_t method(uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        uint32_t r = 0;
        CHECK(!cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r));
        return r;
    }
    // Writes a DSBUFFERDESC (DirectX 6's, 20 bytes) and its WAVEFORMATEX at `at`.
    uint32_t desc(uint32_t at, uint32_t flags, uint32_t bytes, uint32_t rate, uint16_t channels, uint16_t bits) {
        const uint32_t format = rate ? at + 32 : 0;
        cpu.write32(at, 20);
        cpu.write32(at + 4, flags);
        cpu.write32(at + 8, bytes);
        cpu.write32(at + 12, 0);
        cpu.write32(at + 16, format);
        if (format) {
            const uint16_t align = uint16_t(channels * bits / 8);
            cpu.write16(format, 1);
            cpu.write16(format + 2, channels);
            cpu.write32(format + 4, rate);
            cpu.write32(format + 8, rate * align);
            cpu.write16(format + 12, align);
            cpu.write16(format + 14, bits);
            cpu.write16(format + 16, 0);
        }
        return at;
    }
    // The output's next `frames` frames at 1000 a second, the left channel's.
    std::vector<int16_t> mix(const std::shared_ptr<audio::OutputStream> &out, int frames) {
        std::vector<int16_t> both(size_t(frames) * 2);
        out->read(both.data(), frames, 1000);
        std::vector<int16_t> left;
        for (size_t i = 0; i < both.size(); i += 2)
            left.push_back(both[i]);
        return left;
    }
};

// IDirectSound's methods by index.
constexpr int kRelease = 2;
constexpr int kCreateSoundBuffer = 3;
constexpr int kGetCaps = 4;
constexpr int kDuplicateSoundBuffer = 5;
constexpr int kSetCooperativeLevel = 6;
// IDirectSoundBuffer's.
constexpr int kGetCurrentPosition = 4;
constexpr int kGetStatus = 9;
constexpr int kLock = 11;
constexpr int kPlay = 12;
constexpr int kSetFormat = 14;
constexpr int kSetVolume = 15;
constexpr int kSetPan = 16;
constexpr int kStop = 18;
constexpr int kUnlock = 19;
// DSBCAPS_CTRLFREQUENCY, _CTRLPAN and _CTRLVOLUME.
constexpr uint32_t kControls = 0x20 | 0x40 | 0x80;

void testDevices() {
    Fixture f;
    // DirectSoundEnumerateA (ordinal 2): the primary driver, without a GUID, then Reluva's device.
    std::vector<std::string> seen;
    f.process.provideStdcall("TEST", "Device", 4, [&](x86::Cpu &c) {
        seen.push_back((c.arg(0) ? x86::ComHost::guid(c, c.arg(0)) : "none") + " " + c.string(c.arg(1)));
        return 1u;
    });
    CHECK(f.exported("DSOUND.dll", "#2", {f.process.standIn("TEST", "Device"), 0}) == 0);
    CHECK(seen.size() == 2 && seen[0] == "none Primary Sound Driver");
    CHECK(seen.size() == 2 && seen[1] == "{8c8b2a40-4e3b-4b2f-9a35-52454c555641} Reluva");
    // DirectSoundCreate (ordinal 1): a device nobody has is no driver.
    x86::ComHost::writeGuid(f.cpu, f.mem + 16, "{11111111-2222-3333-4444-555555555555}");
    CHECK(f.exported("DSOUND.dll", "#1", {f.mem + 16, f.mem, 0}) == 0x88780078u);
    CHECK(f.exported("DSOUND.dll", "#1", {0, f.mem, 0}) == 0);
    const uint32_t ds = f.cpu.read32(f.mem);
    // GetCaps: software mixing at any rate DirectSound allows.
    f.cpu.write32(f.mem + 256, 96);
    CHECK(f.method(ds, kGetCaps, {f.mem + 256}) == 0);
    CHECK(f.cpu.read32(f.mem + 260) == 0x0f1f && f.cpu.read32(f.mem + 264) == 100 &&
          f.cpu.read32(f.mem + 268) == 100000);
    // The primary buffer, as LEGO Loco sets it up: its format wants DSSCL_PRIORITY.
    const uint32_t primaryDesc = f.desc(f.mem + 512, 1, 0, 0, 0, 0);
    CHECK(f.method(ds, kCreateSoundBuffer, {primaryDesc, f.mem + 4, 0}) == 0);
    const uint32_t primary = f.cpu.read32(f.mem + 4);
    f.desc(f.mem + 600, 0, 0, 22050, 2, 16);
    CHECK(f.method(primary, kSetFormat, {f.mem + 632}) == 0x88780046u); // DSERR_PRIOLEVELNEEDED
    CHECK(f.method(ds, kSetCooperativeLevel, {0x10010, 2}) == 0);
    CHECK(f.method(primary, kSetFormat, {f.mem + 632}) == 0);
    // Only DSSCL_WRITEPRIMARY lets a program write it.
    CHECK(f.method(primary, kLock, {0, 4, f.mem + 8, f.mem + 12, 0, 0, 0}) == 0x88780046u);
    CHECK(f.sound.buffers() == 0);
}

void testBuffer() {
    Fixture f;
    CHECK(f.exported("DSOUND.dll", "DirectSoundCreate", {0, f.mem, 0}) == 0);
    const uint32_t ds = f.cpu.read32(f.mem);
    // A second of 16-bit mono at 1000 frames a second: 2000 bytes.
    const uint32_t d = f.desc(f.mem + 512, kControls, 2000, 1000, 1, 16);
    CHECK(f.method(ds, kCreateSoundBuffer, {d, f.mem + 4, 0}) == 0);
    const uint32_t b = f.cpu.read32(f.mem + 4);
    CHECK(f.sound.buffers() == 1);
    // Locked whole, then across its end: two parts.
    CHECK(f.method(b, kLock, {0, 2000, f.mem + 8, f.mem + 12, f.mem + 16, f.mem + 20, 0}) == 0);
    const uint32_t data = f.cpu.read32(f.mem + 8);
    CHECK(f.cpu.read32(f.mem + 12) == 2000 && f.cpu.read32(f.mem + 16) == 0 && f.cpu.read32(f.mem + 20) == 0);
    for (uint32_t i = 0; i < 2000; i += 2)
        f.cpu.write16(data + i, 1000);
    CHECK(f.method(b, kUnlock, {data, 2000, 0, 0}) == 0);
    CHECK(f.method(b, kLock, {1500, 1000, f.mem + 8, f.mem + 12, f.mem + 16, f.mem + 20, 0}) == 0);
    CHECK(f.cpu.read32(f.mem + 8) == data + 1500 && f.cpu.read32(f.mem + 12) == 500);
    CHECK(f.cpu.read32(f.mem + 16) == data && f.cpu.read32(f.mem + 20) == 500);
    // Played once: its play cursor follows the clock, and it stops at its end.
    CHECK(f.method(b, kPlay, {0, 0, 0}) == 0);
    f.clock += 500;
    CHECK(f.method(b, kGetCurrentPosition, {f.mem + 24, f.mem + 28}) == 0);
    CHECK(f.cpu.read32(f.mem + 24) == 1000 && f.cpu.read32(f.mem + 28) == 1020);
    CHECK(f.method(b, kGetStatus, {f.mem + 32}) == 0 && f.cpu.read32(f.mem + 32) == 1);
    CHECK(f.sound.playing() == 1);
    // What's heard: its samples, then quieter by 20 dB, then panned right
    // (the left is silent).
    auto out = f.sound.output();
    CHECK(f.mix(out, 10)[5] == 1000);
    CHECK(f.method(b, kSetVolume, {uint32_t(-2000)}) == 0);
    CHECK(f.mix(out, 10)[5] == 100);
    CHECK(f.method(b, kSetPan, {10000}) == 0);
    CHECK(f.mix(out, 10)[5] == 0);
    f.clock += 600;
    CHECK(f.method(b, kGetStatus, {f.mem + 32}) == 0 && f.cpu.read32(f.mem + 32) == 0);
    CHECK(f.method(b, kGetCurrentPosition, {f.mem + 24, 0}) == 0 && f.cpu.read32(f.mem + 24) == 0);
    // Looping, it goes round; stopped, it stays.
    CHECK(f.method(b, kPlay, {0, 0, 1}) == 0);
    f.clock += 1250;
    CHECK(f.method(b, kGetStatus, {f.mem + 32}) == 0 && f.cpu.read32(f.mem + 32) == 5);
    CHECK(f.method(b, kStop, {}) == 0);
    f.clock += 100;
    CHECK(f.method(b, kGetCurrentPosition, {f.mem + 24, 0}) == 0 && f.cpu.read32(f.mem + 24) == 500);
    // A duplicate shares the sound; a buffer without the control can't be
    // made quieter.
    CHECK(f.method(ds, kDuplicateSoundBuffer, {b, f.mem + 36}) == 0);
    const uint32_t copy = f.cpu.read32(f.mem + 36);
    CHECK(f.method(copy, kLock, {0, 4, f.mem + 8, f.mem + 12, 0, 0, 0}) == 0 && f.cpu.read32(f.mem + 8) == data);
    CHECK(f.sound.buffers() == 2);
    CHECK(f.method(ds, kCreateSoundBuffer, {f.desc(f.mem + 640, 0, 100, 1000, 1, 8), f.mem + 40, 0}) == 0);
    CHECK(f.method(f.cpu.read32(f.mem + 40), kSetVolume, {0}) == 0x8878001eu); // DSERR_CONTROLUNAVAIL
    // A format DirectSound can't play.
    CHECK(f.method(ds, kCreateSoundBuffer, {f.desc(f.mem + 700, 0, 100, 1000, 3, 8), f.mem + 44, 0}) == 0x88780064u);
    CHECK(f.method(copy, kRelease, {}) == 0 && f.sound.buffers() == 2);
}

void testPlaySound(const std::filesystem::path &tmp) {
    Fixture f;
    audio::Sound tone;
    tone.rate = 1000;
    tone.channels = 1;
    tone.samples.assign(500, 2000);
    const std::vector<uint8_t> wav = audio::encodePcmWav(tone);
    std::ofstream(tmp / "music.wav", std::ios::binary)
        .write(reinterpret_cast<const char *>(wav.data()), std::streamsize(wav.size()));
    f.process.mapPath = [tmp](const std::string &path, bool) -> std::optional<std::filesystem::path> {
        if (path == "C:\\music.wav")
            return tmp / "music.wav";
        return std::nullopt;
    };
    f.cpu.writeString(f.mem, "C:\\music.wav", 32);
    f.cpu.writeString(f.mem + 32, "C:\\none.wav", 32);
    auto out = f.sound.output();
    // SND_ASYNC | SND_LOOP, as LEGO Loco plays its music: it goes round.
    CHECK(f.exported("WINMM.dll", "PlaySoundA", {f.mem, 0, 9}) == 1);
    CHECK(f.mix(out, 400)[399] == 2000 && f.mix(out, 400)[399] == 2000);
    // A null sound stops it; a file not there isn't played.
    CHECK(f.exported("WINMM.dll", "PlaySoundA", {0, 0, 0}) == 1);
    CHECK(f.mix(out, 10)[0] == 0);
    CHECK(f.exported("WINMM.dll", "PlaySoundA", {f.mem + 32, 0, 1}) == 0);
}

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.dsound", {
                                     {"devices", [](const std::filesystem::path &) { testDevices(); }},
                                     {"buffer", [](const std::filesystem::path &) { testBuffer(); }},
                                     {"play-sound", [](const std::filesystem::path &tmp) { testPlaySound(tmp); }},
                                 });
