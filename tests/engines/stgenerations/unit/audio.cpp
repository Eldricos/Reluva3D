// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The game's sound calls (engines/stgenerations/audio.h) on Reluva's mixer:
// raw PCM, WAV files played once or looped, handles, volumes and balance,
// pausing. STG-Engine's tests of its own mixer, as the game's calls now
// reach Reluva's (whose own tests are common.unit.audio).

#include "tests/engines/stgenerations/support.h"

#include "common/bytes.h"
#include "common/stream.h"
#include "engines/stgenerations/audio.h"
#include "engines/stgenerations/platform.h"
#include "engines/stgenerations/tools/scripted_system.h"

#include <cstdlib>

using namespace reluva;
using namespace reluva::stgenerations;

static std::vector<uint8_t> makePCM16(int frames, int16_t value) {
    std::vector<uint8_t> data(size_t(frames) * 2);
    for (int i = 0; i < frames; i++)
        writeLE16(&data[size_t(i) * 2], uint16_t(value));
    return data;
}

static std::vector<uint8_t> makeWav(const std::vector<uint8_t>& pcm, bool junk = true) {
    MemoryWriteStream w;
    w.writeUint32BE(tagBE('R', 'I', 'F', 'F'));
    w.writeUint32LE(4 + 8 + 16 + (junk ? 8 + 2 : 0) + 8 + uint32_t(pcm.size()));
    w.writeUint32BE(tagBE('W', 'A', 'V', 'E'));
    w.writeUint32BE(tagBE('f', 'm', 't', ' '));
    w.writeUint32LE(16);
    w.writeUint16LE(1);     // PCM
    w.writeUint16LE(1);     // mono
    w.writeUint32LE(22050); // rate
    w.writeUint32LE(44100); // bytes a second
    w.writeUint16LE(2);     // block align
    w.writeUint16LE(16);    // bits
    if (junk) {
        // An odd-sized chunk, skipped with its pad byte.
        w.writeUint32BE(tagBE('j', 'u', 'n', 'k'));
        w.writeUint32LE(1);
        w.writeByte(0xAA);
        w.writeByte(0);
    }
    w.writeUint32BE(tagBE('d', 'a', 't', 'a'));
    w.writeUint32LE(uint32_t(pcm.size()));
    w.write(pcm.data(), uint32_t(pcm.size()));
    return w.data();
}

static void raw_stream_formats() {
    // 8-bit unsigned: 0x80 is silence, 0xFF near full scale.
    auto s8 = makeRawStream({0x80, 0xFF, 0x00}, 11025, kRawUnsigned);
    int16_t buf[4] = {};
    CHECK_EQUAL(s8->read(buf, 4), 3);
    CHECK_EQUAL(buf[0], 0);
    CHECK_EQUAL(buf[1], 127 * 256);
    CHECK_EQUAL(buf[2], -128 * 256);
    CHECK(s8->ended() && s8->rate() == 11025 && !s8->stereo());

    // 16-bit little-endian stereo.
    auto s16 = makeRawStream({0x34, 0x12, 0xCC, 0xED}, 22050, kRaw16Bits | kRawLittleEndian | kRawStereo);
    CHECK(s16->stereo());
    CHECK_EQUAL(s16->read(buf, 4), 2);
    CHECK_EQUAL(buf[0], 0x1234);
    CHECK_EQUAL(buf[1], int16_t(0xEDCC));
}

static void looping_stream() {
    MemoryReadStream file(makeWav(makePCM16(10, 1)));
    auto looped = makeLooping(makeWavStream(file), 3);
    int16_t buf[64];
    CHECK_EQUAL(looped->read(buf, 64), 30);
    CHECK(looped->ended());
    // 0 loops: until stopped.
    MemoryReadStream again(makeWav(makePCM16(10, 1)));
    auto forever = makeLooping(makeWavStream(again), 0);
    CHECK_EQUAL(forever->read(buf, 64), 64);
    CHECK(!forever->ended());
}

static void wave_parsing() {
    MemoryReadStream r(makeWav(makePCM16(4, 1000)));
    WavInfo info;
    CHECK(readWavInfo(r, info));
    CHECK_EQUAL(info.sampleRate, 22050);
    CHECK_EQUAL(info.channels, 1);
    CHECK_EQUAL(info.bitsPerSample, 16);
    CHECK_EQUAL(info.dataSize, 8u);

    auto stream = makeWavStream(r);
    CHECK(stream != nullptr);
    if (stream) {
        CHECK_EQUAL(stream->lengthMs(), 0u); // 4 frames: under a millisecond
        int16_t buf[8];
        CHECK_EQUAL(stream->read(buf, 8), 4);
        CHECK_EQUAL(buf[3], 1000);
        CHECK_EQUAL(stream->rate(), 22050);
    }

    const uint8_t notWave[] = "RIFF\0\0\0\0AVI ";
    MemoryReadStream bad(std::vector<uint8_t>(notWave, notWave + sizeof(notWave)));
    CHECK(!readWavInfo(bad, info));
    CHECK(makeWavStream(bad) == nullptr);
}

static void mixer_volume_and_balance() {
    audio::Mixer out(22050);
    Mixer mixer(out);
    int16_t buf[200];

    SoundHandle handle;
    mixer.playStream(Mixer::kSFXSoundType, &handle,
                     makeRawStream(makePCM16(1000, 8000), 11025, kRaw16Bits | kRawLittleEndian));
    CHECK(mixer.isSoundHandleActive(handle));
    out.mix(buf, 100);
    // Mono is played on both channels at the volume recorded.
    CHECK(std::abs(buf[0] - 8000) < 64);
    CHECK_EQUAL(buf[0], buf[1]);

    // Halving the kind's volume halves the sound; and the sound's own.
    mixer.setVolumeForSoundType(Mixer::kSFXSoundType, 128);
    CHECK_EQUAL(mixer.getVolumeForSoundType(Mixer::kSFXSoundType), 128);
    out.mix(buf, 100);
    CHECK(std::abs(buf[0] - 4000) < 64);
    mixer.setChannelVolume(handle, 255 / 2);
    out.mix(buf, 100);
    CHECK(std::abs(buf[0] - 2000) < 64);

    // Full left balance silences the right channel.
    mixer.setChannelBalance(handle, -127);
    out.mix(buf, 100);
    CHECK(buf[0] != 0);
    CHECK_EQUAL(buf[1], 0);

    // Paused, it waits; then it plays on.
    mixer.pauseHandle(handle, true);
    out.mix(buf, 100);
    CHECK(buf[0] == 0 && mixer.isSoundHandleActive(handle));
    mixer.pauseHandle(handle, false);

    // 1000 frames at 11025 Hz last 2000 output frames at 22050 Hz.
    for (int i = 0; i < 20; i++)
        out.mix(buf, 100);
    CHECK(!mixer.isSoundHandleActive(handle));
    // A handle never played is never active.
    CHECK(!mixer.isSoundHandleActive(SoundHandle()));
}

static void mixer_kinds() {
    audio::Mixer out(22050);
    Mixer mixer(out);
    for (int i = 0; i < 4; i++)
        mixer.playStream(Mixer::kPlainSoundType, nullptr,
                         makeRawStream(makePCM16(100, 30000), 22050, kRaw16Bits | kRawLittleEndian));
    int16_t buf[20];
    out.mix(buf, 10);
    CHECK_EQUAL(buf[0], 32767);

    mixer.stopAll();
    SoundHandle music, speech;
    mixer.playStream(Mixer::kMusicSoundType, &music, makeRawStream(makePCM16(100, 1), 22050, kRaw16Bits | kRawLittleEndian));
    mixer.playStream(Mixer::kSpeechSoundType, &speech,
                     makeRawStream(makePCM16(100, 1), 22050, kRaw16Bits | kRawLittleEndian));
    CHECK(mixer.isSoundTypeActive(Mixer::kMusicSoundType) && mixer.isSoundTypeActive(Mixer::kSpeechSoundType));
    CHECK(!mixer.isSoundTypeActive(Mixer::kSFXSoundType));
    mixer.stopHandle(music);
    CHECK(!mixer.isSoundHandleActive(music) && mixer.isSoundHandleActive(speech));
    // Nothing to play: no sound, no handle.
    SoundHandle none;
    mixer.playStream(Mixer::kSFXSoundType, &none, nullptr);
    CHECK(!mixer.isSoundHandleActive(none));
}

// Without a sound device nothing pulls the mixer: nothing is played, so no
// sound is kept waiting for ever.
static void no_sound_output() {
    audio::Mixer out(22050);
    Mixer mixer(out);
    mixer.setEnabled(false);
    SoundHandle h;
    mixer.playStream(Mixer::kMusicSoundType, &h, makeRawStream(makePCM16(10, 1), 22050, kRaw16Bits | kRawLittleEndian));
    CHECK(!mixer.isSoundHandleActive(h) && !mixer.isSoundTypeActive(Mixer::kMusicSoundType));
}

// The platform's input: the mouse kept in the screen, as the original's
// window gave it; buttons held; text typed only while asked for.
static void platform_input() {
    ScriptedSystem sys;
    Platform platform(sys, nullptr, {});
    platform.initSize(640, 480);
    reluva::Event e;
    e.type = reluva::EventType::MouseMove;
    e.mouseX = -20, e.mouseY = 700;
    sys.schedule(0, e);
    e.type = reluva::EventType::MouseDown;
    e.button = MouseButton::Left;
    e.mouseX = 900, e.mouseY = 10;
    sys.schedule(0, e);
    sys.type(0, "a");
    stgenerations::Event got;
    CHECK(platform.pollEvent(got) && got.type == kEventMousemove && got.mouse == Point(0, 479));
    CHECK(platform.pollEvent(got) && got.type == kEventLbuttondown && got.mouse == Point(639, 10));
    CHECK(platform.getButtonState() == 1);
    CHECK(!platform.pollEvent(got)); // text, while none is asked for
    platform.setTextInput(true);
    sys.type(0, "b");
    CHECK(platform.pollEvent(got) && got.type == kEventKeydown && got.kbd.ascii == 'b' && got.kbd.keycode == 0);
}

static const reluva::test::Suite suite("stgenerations.unit.audio", {
    {"no sound output", [](const std::filesystem::path&) { no_sound_output(); }},
    {"platform input", [](const std::filesystem::path&) { platform_input(); }},
    {"raw stream formats", [](const std::filesystem::path&) { raw_stream_formats(); }},
    {"looping stream", [](const std::filesystem::path&) { looping_stream(); }},
    {"wave parsing", [](const std::filesystem::path&) { wave_parsing(); }},
    {"mixer volume and balance", [](const std::filesystem::path&) { mixer_volume_and_balance(); }},
    {"mixer kinds", [](const std::filesystem::path&) { mixer_kinds(); }},
});
