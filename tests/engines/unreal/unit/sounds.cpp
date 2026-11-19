// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sounds: WAV, ADPCM and MP3 in packages, bad ones, and music streamed.
//
//   reluva-tests unreal.unit.sounds

#include "tests/engines/unreal/support.h"

using namespace reluva;
using namespace reluva::unreal;
using namespace reluva::unreal::test_data;

namespace {

void testSounds() {
    audio::Sound tone;
    tone.rate = 11025;
    tone.channels = 1;
    for (int i = 0; i < 100; ++i)
        tone.samples.push_back(int16_t(i * 100));
    const std::vector<uint8_t> wav = audio::encodePcmWav(tone);
    PackageWriter w;
    const int32_t cls = w.addImport("Engine", "Class", 0, "Sound");
    Bytes o;
    endProps(o, w);
    o.index(w.name("WAV"));
    o.i32(0);
    o.index(int32_t(wav.size()));
    o.bytes(wav);
    w.addExport(cls, 0, "Beep", o.b);
    Package p;
    CHECK(!p.openMemory(w.build(), "Sfx"));
    EmbeddedFile f;
    CHECK(!readEmbeddedFile(p, 0, f));
    CHECK(f.format == "WAV" && f.data == wav);
    audio::Sound back;
    CHECK(!audio::decodeSound(f.data, back) && back.samples == tone.samples);
}

void testSoundErrors() {
    EmbeddedFile f;
    f.format = "OGG";
    f.data = {'O', 'g', 'g', 'S', 0, 2};
    f.data.resize(20);
    audio::Sound s;
    const Error err = decodeEmbeddedSound(f, s);
    CHECK(!f.isPlaceholder());
    CHECK(err && err.message.find("format OGG, 20 bytes starting 4f 67 67 53 00 02 00 00 (\"OggS\")") == 0);
    // The Fallen's empty sounds.
    f.format = "wav";
    f.data = {'X', ' ', '\r', '\n'};
    CHECK(f.isPlaceholder() && decodeEmbeddedSound(f, s).message.find("placeholder") != std::string::npos);
}

void testStreamedSounds() {
    // A placeholder Sound, and the MP3 of its name in the mp3 folder.
    const auto tmp = std::filesystem::temp_directory_path() / "reluva_unreal_mp3";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "System");
    std::filesystem::create_directories(tmp / "Sounds");
    std::filesystem::create_directories(tmp / "mp3");
    std::ofstream(tmp / "System" / "Default.ini") << "[Core.System]\nPaths=..\\Sounds\\*.uax\n";
    std::ofstream(tmp / "mp3" / "orb_thme.mp3", std::ios::binary)
        .write(reinterpret_cast<const char *>(kTone440Mp3), sizeof kTone440Mp3);
    PackageWriter w;
    const int32_t cls = w.addImport("Engine", "Class", 0, "Sound");
    for (const char *name : {"Orb_Thme", "Missing"}) {
        Bytes o;
        endProps(o, w);
        o.index(w.name("wav"));
        o.i32(0);
        o.index(4);
        o.u8('X'), o.u8(' '), o.u8('\r'), o.u8('\n');
        w.addExport(cls, 0, name, o.b);
    }
    {
        const std::vector<uint8_t> pkg = w.build();
        std::ofstream(tmp / "Sounds" / "Trks.uax", std::ios::binary)
            .write(reinterpret_cast<const char *>(pkg.data()), std::streamsize(pkg.size()));
    }
    PackageSet set;
    CHECK(!set.open(tmp, ""));
    const Package *p = set.get("Trks");
    CHECK(p != nullptr);
    if (p) {
        audio::Sound s;
        std::string from;
        CHECK(!loadSound(set, *p, 0, s, &from));
        CHECK(s.format == audio::kFormatMp3 && s.rate == 22050 && s.frames() > 0);
        CHECK(std::filesystem::path(from).filename() == "orb_thme.mp3");
        const Error err = loadSound(set, *p, 1, s, &from);
        CHECK(err && err.message.find("no mp3/Missing.mp3") != std::string::npos);
    }
    std::filesystem::remove_all(tmp);
}

} // namespace

static const reluva::test::Suite kSuite("unreal.unit.sounds", {
    {"sounds", [](const std::filesystem::path &) { testSounds(); }},
    {"sound errors", [](const std::filesystem::path &) { testSoundErrors(); }},
    {"streamed sounds", [](const std::filesystem::path &) { testStreamedSounds(); }},
});
