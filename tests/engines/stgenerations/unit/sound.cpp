// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tests/engines/stgenerations/support.h"

#include <cstring>
#include <vector>

#include "engines/stgenerations/audio.h"
#include "common/bytes.h"
#include "common/stream.h"
#include "engines/stgenerations/game/sound.h"

using namespace reluva;
using namespace reluva::stgenerations;

namespace {

// A 56-byte sound table entry.
std::vector<uint8_t> soundEntry(uint32_t key, uint32_t flags, int positionMode = 0, double x = 0, double radius = 0,
                                int32_t repeats = 0) {
    std::vector<uint8_t> r(56);
    writeLE32(&r[0x00], key);
    writeLE32(&r[0x04], 64);
    writeLE32(&r[0x08], 1);
    writeLE32(&r[0x0C], positionMode);
    writeLE32(&r[0x14], (uint32_t)(int32_t)(x * 65536));
    writeLE32(&r[0x20], (uint32_t)(int32_t)(radius * 65536));
    writeLE32(&r[0x28], (uint32_t)repeats);
    writeLE32(&r[0x34], flags);
    return r;
}

// A script with no objects and this sound table: 0 Riker speaking, 1 a
// sound at (10, 0, 0) heard within 20 units, 2 a COMMON sound, 3 music;
// and cue 5, which plays sound 0, waits 500 ms and plays it again.
std::vector<uint8_t> makeSoundScript() {
    std::vector<std::vector<uint8_t>> sections(6);
    for (const std::vector<uint8_t> &e :
         {soundEntry(0x4006B, MissionSound::kFlagSubtitle), soundEntry(0x00003, 0, 1, 10, 20), soundEntry(0x1000032, 0),
          soundEntry(0x121D4D, MissionSound::kFlagStream)})
        sections[3].insert(sections[3].end(), e.begin(), e.end());
    std::vector<uint8_t> end(56, 0xFF);
    sections[3].insert(sections[3].end(), end.begin(), end.end());

    const uint32_t cue[] = {0x12345678, 5, 0x20, 0, 0, 0, (uint32_t)-500, 0, 0, 0, 0x87654321, 0xFFFFFFFF};
    for (uint32_t word : cue) {
        uint8_t b[4];
        writeLE32(b, word);
        sections[2].insert(sections[2].end(), b, b + 4);
    }

    std::vector<uint8_t> file(40);
    writeBE32(&file[0], tagBE('S', 'G', 'E', 'N'));
    writeLE16(&file[4], 22);
    writeLE16(&file[6], 2);
    writeLE32(&file[12], 2);
    for (int i = 0; i < 6; i++)
        writeLE32(&file[16 + i * 4], (uint32_t)sections[i].size());
    file.push_back(0x00);
    file.push_back(0x10); // the program: end marker 0x1000
    for (const std::vector<uint8_t> &s : sections)
        file.insert(file.end(), s.begin(), s.end());
    writeLE32(&file[8], (uint32_t)file.size() - 12);
    return file;
}

// 100 ms of 22 kHz mono silence.
std::vector<uint8_t> makeWAV() {
    const uint32_t samples = 2205;
    std::vector<uint8_t> w(44 + samples * 2);
    memcpy(&w[0], "RIFF", 4);
    writeLE32(&w[4], (uint32_t)w.size() - 8);
    memcpy(&w[8], "WAVEfmt ", 8);
    writeLE32(&w[16], 16);
    writeLE16(&w[20], 1);
    writeLE16(&w[22], 1);
    writeLE32(&w[24], 22050);
    writeLE32(&w[28], 44100);
    writeLE16(&w[32], 2);
    writeLE16(&w[34], 16);
    memcpy(&w[36], "data", 4);
    writeLE32(&w[40], samples * 2);
    return w;
}

} // namespace

static void sound_file_names_and_subtitles() {
    MissionSound::Location where;
    CHECK_EQUAL(MissionSound::getFileName(0x4006B, "WAV", where), std::string("RIKE0107.WAV"));
    CHECK(where == MissionSound::kLocationMission);
    CHECK_EQUAL(MissionSound::getFileName(0x1010035, "TXT", where), std::string("PICA0053.TXT"));
    CHECK(where == MissionSound::kLocationCommon);
    CHECK_EQUAL(MissionSound::getFileName(0x121D4D, "WAV", where), std::string("MUSI7501.WAV"));
    CHECK(where == MissionSound::kLocationMusic);

    CHECK_EQUAL(MissionSound::cleanSubtitle("RIKER: \\T00FF00\\Hello\\\\ there\r\n"),
                std::string("RIKER: Hello\\ there"));
}

static void sound_cues_positions_and_events() {
    std::vector<uint8_t> scriptBytes = makeSoundScript();
    MemoryReadStream scriptStream(scriptBytes);
    ScriptFile script;
    CHECK(script.load(scriptStream));

    audio::Mixer out(22050);
    Mixer mixer(out);
    std::vector<uint8_t> wav = makeWAV();
    std::vector<std::string> opened;
    auto opener = [&](const std::string &name,
                      MissionSound::Location /*where*/) -> std::unique_ptr<SeekableReadStream> {
        opened.push_back(name);
        if (name == "RIKE0107.TXT") {
            static const char text[] = "RIKER: \\T00FF00\\Make it so.";
            return std::unique_ptr<SeekableReadStream>(
                new MemoryReadStream(std::vector<uint8_t>(text, text + sizeof(text) - 1)));
        }
        return std::unique_ptr<SeekableReadStream>(new MemoryReadStream(wav));
    };
    MissionSound sound(script, &mixer, opener, nullptr);
    CHECK_EQUAL(sound.getSoundCount(), 4);
    CHECK_EQUAL(sound.getCueCount(), 1);

    std::vector<int16_t> buffer(22050 * 2);
    auto run = [&](uint32_t ms, std::vector<MissionSound::Finished> &finished) {
        out.mix(buffer.data(), int(22050 * ms / 1000));
        MissionSound::Listener listener = {0, 0, 0, 0};
        sound.update(ms, listener, finished);
    };

    // The cue: the line, its subtitle, a pause, the line again, then event 46.
    std::vector<MissionSound::Finished> finished;
    sound.playCue(5, 0xE001);
    run(10, finished);
    CHECK_EQUAL(opened.size(), (size_t)2);
    CHECK_EQUAL(opened[0], std::string("RIKE0107.WAV"));
    std::string text;
    uint32_t duration = 0;
    CHECK(sound.takeSubtitle(text, duration));
    CHECK_EQUAL(text, std::string("RIKER: Make it so."));
    CHECK_EQUAL(duration, 2000u);
    for (int i = 0; i < 3; i++)
        run(100, finished);
    CHECK_EQUAL(opened.size(), (size_t)2); // the first line is over; the pause is not
    for (int i = 0; i < 3; i++)
        run(100, finished);
    CHECK_EQUAL(opened.size(), (size_t)4); // the second line
    CHECK(finished.empty());
    for (int i = 0; i < 3; i++)
        run(100, finished);
    CHECK_EQUAL(finished.size(), (size_t)1);
    CHECK_EQUAL(finished[0].object, 0xE001);

    // A positional sound plays within its radius and is dropped beyond it.
    opened.clear();
    int handle = sound.play(1);
    CHECK(handle > 0);
    run(10, finished);
    CHECK_EQUAL(opened.size(), (size_t)1);
    opened.clear();
    sound.play(1);
    MissionSound::Listener far = {100, 0, 0, 0};
    finished.clear();
    sound.update(10, far, finished);
    CHECK(opened.empty());

    // Fading out stops it.
    CHECK(sound.fadeOut((uint32_t)handle << 16, 50));
    run(100, finished);
    run(100, finished);
    CHECK(!sound.fadeOut((uint32_t)handle << 16, 50));
}

static const reluva::test::Suite suite(
    "stgenerations.unit.sound",
    {
        {"sound file names and subtitles", [](const std::filesystem::path &) { sound_file_names_and_subtitles(); }},
        {"sound cues positions and events", [](const std::filesystem::path &) { sound_cues_positions_and_events(); }},
    });
