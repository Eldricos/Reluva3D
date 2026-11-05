// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Small self-contained tests for common/. No framework, to keep deps at zero.

#include "common/audio/decode.h"
#include "common/audio/mixer.h"
#include "common/audio/mp3.h"
#include "common/audio/stream.h"
#include "common/audio/wav.h"
#include "tests/common/data/smacker_videos.h"
#include "tests/common/data/tone440_mp3.h"
#include "tests/common/data/x86_testdll.h"
#include "common/archive.h"
#include "common/bmp.h"
#include "common/bytes.h"
#include "common/config.h"
#include "common/cursor.h"
#include "common/dcl.h"
#include "common/detection.h"
#include "common/draw.h"
#include "common/font.h"
#include "common/fs.h"
#include "common/headless_system.h"
#include "common/gfx3d.h"
#include "common/image.h"
#include "common/md4.h"
#include "common/md5.h"
#include "common/net.h"
#include "common/palette.h"
#include "common/serializer.h"
#include "common/settings.h"
#include "common/stream.h"
#include "common/text.h"
#include "common/video/avi.h"
#include "common/video/cinepak.h"
#include "common/video/smacker.h"
#include "common/x86/cpu.h"
#include "common/x86/disasm.h"
#include "common/x86/win32.h"
#include "common/zip.h"
#include "common/inflate.h"
#include "common/wise.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <memory>
#include <fstream>
#include <string>

using namespace reluva;

static std::string md5Of(const std::string& s) {
    Md5 m;
    m.update(s.data(), s.size());
    return m.hexDigest();
}

static void testMd5() {
    CHECK(md5Of("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5Of("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5Of("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6");
    // Crosses the 56-byte padding boundary.
    CHECK(md5Of(std::string(64, 'a')) == "014842d480b571495a4a0363793f7367");
}

static void testFsAndDetection(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "Data");
    {
        std::ofstream(tmp / "Data" / "GAME.DAT", std::ios::binary) << "abc";
    }

    auto found = fs::findFile(tmp, "data/game.dat");
    CHECK(found.has_value());
    CHECK(!fs::findFile(tmp, "data/missing.dat"));
    CHECK(fs::md5File(*found, kDetectionHashBytes) == "900150983cd24fb0d6963f7d28e17f72");

    std::vector<GameVariant> table = {
        {"good", "Good", "en", "windows", {{"DATA/game.dat", "900150983cd24fb0d6963f7d28e17f72", 3}}},
        {"badsize", "Bad size", "en", "windows", {{"data/game.dat", "", 4}}},
        {"badmd5", "Bad md5", "en", "windows", {{"data/game.dat", "00000000000000000000000000000000", -1}}},
    };
    auto results = detectFromTable("eng", table, tmp);
    CHECK(results.size() == 1);
    CHECK(!results.empty() && results[0].gameId == "good");

    std::filesystem::remove_all(tmp);
}

static void testConfig(const std::filesystem::path& tmp) {
    std::filesystem::create_directories(tmp);
    auto path = tmp / "test.ini";
    {
        Config c;
        c.load(path);
        c.set(Config::kGlobalSection, "last_target", "foo");
        c.set("foo", "path", "/games/foo bar");
        CHECK(!c.save());
    }
    Config c;
    c.load(path);
    CHECK(c.get("foo", "path") == "/games/foo bar");
    CHECK(c.targets().size() == 1);
    CHECK(c.get("foo", "missing", "dflt") == "dflt");

    // Settings at three levels: every game's, an engine's games', a game's
    // own; the most specific wins, and an engine's section is no game.
    c.set("foo", "engine", "eng");
    c.set("bar", "engine", "other");
    c.set(Config::kGlobalSection, settings::kVsync, "0");
    c.set(Config::kGlobalSection, settings::kSmooth, "1");
    c.set(Config::engineSection("eng"), settings::kSmooth, "0");
    c.set(Config::engineSection("eng"), settings::kWindowScale, "auto");
    c.set("foo", settings::kWindowScale, "3");
    CHECK(c.targets().size() == 2 && Config::isEngineSection(Config::engineSection("eng")));
    auto foo = c.settingsFor("foo"), bar = c.settingsFor("bar");
    CHECK(foo[settings::kVsync] == "0" && foo[settings::kSmooth] == "0" && foo[settings::kWindowScale] == "3");
    CHECK(bar[settings::kSmooth] == "1" && bar.count(settings::kWindowScale) == 0);
    c.unset("foo", settings::kWindowScale);
    CHECK(!c.has("foo", settings::kWindowScale) && c.settingsFor("foo")[settings::kWindowScale] == "auto");

    // As the frontend and engines read them: unset is Reluva's default.
    CHECK(settings::display({}) == DisplayOptions{});
    const DisplayOptions d = settings::display(c.settingsFor("foo"));
    CHECK(!d.fullscreen && d.windowScale == DisplayOptions::kAutoScale && !d.integerScaling && !d.smooth && !d.vsync);
    CHECK(settings::display({{settings::kWindowScale, "2"}, {settings::kIntegerScaling, "1"}}).windowScale == 2);
    CHECK(settings::volume({}, settings::kMasterVolume) == audio::kMaxVolume);
    CHECK(settings::volume({{settings::kMasterVolume, "200"}}, settings::kMasterVolume) == audio::kMaxBoostVolume);
    CHECK(settings::volume({{settings::kMasterVolume, "50"}}, settings::kMasterVolume) == audio::kMaxVolume / 2);
    CHECK(settings::volume({{settings::kMasterVolume, "x"}}, settings::kMasterVolume, 30) == audio::kMaxVolume * 30 / 100);
    std::filesystem::remove_all(tmp);
}

static void testUtf16() {
    const uint16_t hello[] = {'H', 'i', ' ', 0x00e9, 0x20ac, 0xd83d, 0xde80}; // e-acute, euro, rocket
    CHECK(text::utf16leToUtf8(hello, 7) == "Hi \xc3\xa9\xe2\x82\xac\xf0\x9f\x9a\x80");
    const uint16_t lone[] = {0xd800, 'x'};
    CHECK(text::utf16leToUtf8(lone, 2) == "\xef\xbf\xbdx");
    CHECK(text::utf16leToUtf8(nullptr, 0).empty());
}

static void testFont() {
    using namespace reluva;
    CHECK(font::length("Caf\xc3\xa9 \xe2\x80\x99") == 6); // é and ’ are one character each
    Surface s;
    s.create(20, 12, PixelFormat::RGBA32);
    const int end = font::drawText(s, 1, 1, "I|", {255, 0, 0});
    CHECK(end == 1 + 2 * font::kAdvance);
    // 'I' has its middle column set from row 1 to 5 and a bar on row 0.
    auto red = [&](int x, int y) { return s.row(y)[x * 4] == 255 && s.row(y)[x * 4 + 1] == 0; };
    CHECK(red(3, 1) && red(2, 1) && red(4, 1) && !red(1, 2) && red(3, 4) && !red(0, 0));
    CHECK(red(9, 7) && !red(9, 8)); // '|' runs rows 0-6
    font::drawText(s, 15, 10, "W", {255, 0, 0}, 2); // clipped, not out of bounds
}

static void testWav() {
    using namespace reluva::audio;
    // One 8-byte IMA ADPCM block: header (sample 100, index 0), then nibbles
    // 4, 0, 0, ... (low nibble first); the fact chunk keeps 5 frames.
    std::vector<uint8_t> b;
    auto put32 = [](std::vector<uint8_t>& v, uint32_t x) {
        for (int i = 0; i < 4; ++i)
            v.push_back(uint8_t(x >> (8 * i)));
    };
    auto put16 = [&](uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); };
    auto tag = [&](const char* t) { b.insert(b.end(), t, t + 4); };
    tag("RIFF"); put32(b, 0); tag("WAVE");
    tag("fmt "); put32(b, 20);
    put16(0x11); put16(1); put32(b, 22050); put32(b, 22050); put16(8); put16(4); put16(2); put16(9);
    tag("fact"); put32(b, 4); put32(b, 5);
    tag("data"); put32(b, 8);
    for (uint8_t v : {100, 0, 0, 0, 0x04, 0, 0, 0})
        b.push_back(v);
    Sound s;
    CHECK(!decodeWav(b, s));
    CHECK(s.rate == 22050 && s.channels == 1 && s.format == 0x11);
    CHECK((s.samples == std::vector<int16_t>{100, 107, 108, 109, 109}));
    // Back through 16-bit PCM unchanged.
    Sound pcm;
    CHECK(!decodeWav(encodePcmWav(s), pcm));
    CHECK(pcm.format == 1 && pcm.rate == 22050 && pcm.samples == s.samples);
    b.resize(20);
    CHECK(bool(decodeWav(b, s))); // cut inside the fmt chunk
}

static void testMp3() {
    using namespace reluva::audio;
    const std::vector<uint8_t> tone(std::begin(kTone440Mp3), std::end(kTone440Mp3));
    Sound s;
    CHECK(!decodeMp3(tone, s));
    CHECK(s.format == kFormatMp3 && s.rate == 22050 && s.channels == 1);
    // About 0.2 s (whole frames of 576 samples, plus the encoder's delay).
    CHECK(s.frames() >= 4410 && s.frames() <= 4410 + 3 * 576);
    // 440 Hz: about 2 x 440 x 0.1 = 88 zero crossings in the middle 0.1 s.
    int crossings = 0;
    for (size_t i = 1102; i < 3307; ++i)
        crossings += (s.samples[i - 1] < 0) != (s.samples[i] < 0);
    CHECK(crossings >= 84 && crossings <= 92);
    // The same after an ID3v2 tag (header, 5 bytes of padding), and through decodeSound.
    std::vector<uint8_t> tagged = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0};
    tagged.insert(tagged.end(), tone.begin(), tone.end());
    Sound t;
    CHECK(!decodeSound(tagged, t) && t.samples == s.samples);
    CHECK(bool(decodeMp3(std::vector<uint8_t>(100, 0), t))); // no frames
}

static void testMixer() {
    using namespace reluva::audio;
    auto make = [](int rate, int channels, std::vector<int16_t> samples) {
        auto s = std::make_shared<Sound>();
        s->rate = rate, s->channels = channels, s->samples = std::move(samples);
        return std::shared_ptr<const Sound>(s);
    };
    std::vector<int16_t> out(40);
    Mixer m(22050);
    // A sound at the output rate plays sample for sample, then stops and frees its channel.
    const SoundHandle a = m.play(make(22050, 1, std::vector<int16_t>(10, 1000)), SoundType::Effects);
    CHECK(a != kNoSound && m.isPlaying(a));
    m.mix(out.data(), 12);
    CHECK(out[0] == 1000 && out[1] == 1000 && out[18] == 1000 && out[19] == 1000 && out[20] == 0 && out[23] == 0);
    CHECK(!m.isPlaying(a));
    m.mix(out.data(), 4);
    CHECK(out[0] == 0 && out[7] == 0); // silence with nothing playing
    // Two sounds add up, and clamp at 16 bits.
    m.play(make(22050, 1, {30000, 1000}), SoundType::Effects);
    m.play(make(22050, 1, {30000, -3000}), SoundType::Music);
    m.mix(out.data(), 2);
    CHECK(out[0] == 32767 && out[2] == -2000 && out[3] == -2000);
    // Volumes: the sound's and its type's multiply.
    m.setTypeVolume(SoundType::Music, kMaxVolume / 2);
    m.play(make(22050, 1, {1000}), SoundType::Music, kMaxVolume / 2);
    m.mix(out.data(), 1);
    CHECK(out[0] == 250 && out[1] == 250 && m.typeVolume(SoundType::Music) == 128);
    m.setTypeVolume(SoundType::Music, kMaxVolume);
    // Pan: full right silences the left.
    m.play(make(22050, 1, {1000}), SoundType::Speech, kMaxVolume, kMaxPan);
    m.mix(out.data(), 1);
    CHECK(out[0] == 0 && out[1] == 1000);
    // Resampling: an 11025 Hz ramp at 22050 Hz gets a frame in between each.
    const SoundHandle r = m.play(make(11025, 1, {0, 100, 200, 300}), SoundType::Effects);
    m.mix(out.data(), 4);
    CHECK(out[0] == 0 && out[2] == 50 && out[4] == 100 && out[6] == 150 && m.position(r) == 2.0);
    m.stopAll();
    // Stereo keeps its channels; a loop repeats until stopped.
    const SoundHandle l = m.play(make(22050, 2, {10, -10, 20, -20}), SoundType::Music, kMaxVolume, 0, true);
    m.mix(out.data(), 5);
    CHECK(out[0] == 10 && out[1] == -10 && out[2] == 20 && out[3] == -20 && out[4] == 10 && out[8] == 10);
    CHECK(m.isPlaying(l) && m.isTypePlaying(SoundType::Music) && !m.isTypePlaying(SoundType::Speech));
    m.stopType(SoundType::Music);
    m.mix(out.data(), 2);
    CHECK(!m.isPlaying(l) && out[0] == 0);
    // Nothing to play.
    CHECK(m.play(std::shared_ptr<const Sound>(), SoundType::Effects) == kNoSound && m.play(make(22050, 1, {}), SoundType::Effects) == kNoSound);
}

// Raw deflate streams made with zlib (window bits -15): a stored block, one
// with the fixed codes, one with its own.
static const char kStored[] = "\x01\x0d\x00\xf2\xff\x73\x74\x6f\x72\x65\x64\x20\x62\x6c\x6f\x63\x6b\x21";
static const char kFixed[] =
    "\x0b\x2e\x49\x2c\x52\x08\x29\x4a\xcd\xb6\x52\x08\xcb\xaf\x4c\x4c\x4f\x2d\x52\xd0\x55\x70\xcd\xc9\x2c\x49\x55\x70"
    "\xcb\x2f\x4a\x4e\xd5\x53\x08\xa6\x82\x0a\x06\x46\x26\x66\x16\x56\x36\x76\x0e\x4e\x2e\x6e\x1e\x5e\x3e\x7e\x01\x41"
    "\x21\x61\x11\x51\x31\x71\x09\x49\x29\x69\x19\x59\x39\x79\x05\x45\x25\x65\x15\x55\x35\x75\x0d\x4d\x2d\x6d\x1d\x5d"
    "\x3d\x7d\x03\x43\x23\x63\x13\x53\x33\x73\x0b\x4b\x2b\x6b\x1b\x5b\x3b\x7b\x00";
static const char kDynamic[] =
    "\xdd\x90\xc9\x01\x80\x20\x10\xc4\x5a\x99\x0a\x6c\xc0\x36\x68\x00\x94\x4b\x85\x55\x4e\xb1\x7a\xb7\x09\x3f\xbe\x93"
    "\x7c\x22\x9c\xc6\x55\xfd\xb2\x43\x25\xea\x11\x86\x6e\x6c\x35\x9c\x19\xd4\x74\x42\x61\x7c\xc8\x67\x60\x25\x3b\x43"
    "\xfc\x5d\x3e\x25\x7b\x61\x40\xb1\xd4\x7d\x71\x30\xbe\x69\x46\x8f\x8e\x38\xfc\x55\x29\x71\x6b\xf3\xf4\xff\x13\xdf"
    "\x6c\x7b\x01";

static void testInflateAndWise(const std::filesystem::path& tmp) {
    struct Case {
        const char* data;
        size_t size, outSize;
        uint32_t crc;
    };
    const Case cases[] = {{kStored, sizeof kStored - 1, 13, 0x69348c2d},
                          {kFixed, sizeof kFixed - 1, 166, 0x95b7009b},
                          {kDynamic, sizeof kDynamic - 1, 622, 0x2396af23}};
    for (const Case& c : cases) {
        // With bytes after it, as in a Wise installer: only the stream's taken.
        std::vector<uint8_t> in(c.data, c.data + c.size);
        in.insert(in.end(), {0xaa, 0xbb, 0xcc, 0xdd});
        std::vector<uint8_t> out;
        size_t used = 0;
        CHECK(!inflateStream(in.data(), in.size(), out, used));
        CHECK(used == c.size && out.size() == c.outSize && crc32(out.data(), out.size()) == c.crc);
        CHECK(inflateStream(in.data(), c.size - 2, out, used)); // cut short
    }
    std::vector<uint8_t> out;
    size_t used = 0;
    const uint8_t bad[] = {0x07, 0x00}; // block type 3
    CHECK(inflateStream(bad, sizeof bad, out, used));

    // A Wise installer: a Windows program naming WiseMain, a header, then
    // files, each a deflate stream and its CRC-32.
    std::vector<uint8_t> exe(0x200, 0);
    exe[0] = 'M';
    exe[1] = 'Z';
    exe[0x3c] = 0x40;
    std::memcpy(&exe[0x40], "PE\0\0", 4);
    exe[0x46] = 1;    // one section
    exe[0x54] = 0;    // no optional header: the section table at 0x58
    exe[0x58 + 16] = 0x00, exe[0x58 + 17] = 0x01; // its 0x100 bytes
    exe[0x58 + 20] = 0x00, exe[0x58 + 21] = 0x01; // at 0x100
    std::memcpy(&exe[0x120], "WiseMain", 8);
    exe.resize(0x200);
    const char header[] = "MS Sans Serif\0A patch Installation\0";
    exe.insert(exe.end(), header, header + sizeof header - 1);
    for (const Case& c : cases) {
        exe.insert(exe.end(), c.data, c.data + c.size);
        for (int i = 0; i < 4; ++i)
            exe.push_back(uint8_t(c.crc >> (8 * i)));
    }
    exe.insert(exe.end(), {1, 2, 3}); // trailing bytes that aren't a file
    test::writeFile(tmp / "patch.exe", exe);
    std::vector<WiseStream> streams;
    CHECK(!readWiseInstaller(tmp / "patch.exe", streams));
    CHECK(streams.size() == 3 && streams[1].size == 166 && streams[1].crc == 0x95b7009b &&
          streams[0].offset == 0x200 + sizeof header - 1);
    CHECK(streams.size() == 3 && !extractWiseStream(tmp / "patch.exe", streams[2], out) && out.size() == 622);
    exe[0x120] = 'X'; // not Wise's
    test::writeFile(tmp / "other.exe", exe);
    CHECK(readWiseInstaller(tmp / "other.exe", streams));
}

static void testZip(const std::filesystem::path& tmp) {
    using test::toBytes;
    CHECK(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926);

    // "textures/borg/borgfloor1\n" 12 times, raw-deflated by zlib.
    std::string text;
    for (int i = 0; i < 12; ++i)
        text += "textures/borg/borgfloor1\n";
    const std::vector<uint8_t> deflated = {0x2b, 0x49, 0xad, 0x28, 0x29, 0x2d, 0x4a, 0x2d, 0xd6, 0x4f,
                                           0xca, 0x2f, 0x4a, 0x07, 0x13, 0x69, 0x39, 0xf9, 0xf9, 0x45,
                                           0x86, 0x5c, 0x25, 0xa3, 0x12, 0xe8, 0x12, 0x00};
    std::filesystem::remove_all(tmp);
    const auto path = tmp / "pak0.pk3";
    test::writeFile(path, test::buildZip({{"Scripts/", {}, {}},
                                          {"Scripts/Borg.shader", toBytes("hello"), {}},
                                          {"maps/list.txt", toBytes(text), deflated},
                                          {"empty.cfg", {}, {}}},
                                         "a comment"));
    ZipArchive zip;
    CHECK(!zip.open(path));
    CHECK(zip.entries().size() == 3); // the folder entry is left out
    CHECK(zip.has("scripts\\BORG.shader") && !zip.has("scripts/"));
    std::vector<uint8_t> data;
    CHECK(!zip.read("scripts/borg.shader", data) && data == toBytes("hello"));
    CHECK(!zip.read("MAPS/LIST.TXT", data) && data == toBytes(text));
    CHECK(!zip.read("empty.cfg", data) && data.empty());
    CHECK(zip.read("missing.txt", data));

    // A wrong CRC, and something that isn't a zip.
    auto damaged = test::buildZip({{"a.txt", toBytes("abc"), {}}});
    damaged[30 + 5] = 'X'; // the file's data, after the 30-byte header and name
    test::writeFile(path, damaged);
    CHECK(!zip.open(path) && zip.read("a.txt", data));
    test::writeFile(path, toBytes("definitely not a zip archive"));
    CHECK(zip.open(path));
    std::filesystem::remove_all(tmp);
}

static void testSearchSet(const std::filesystem::path& tmp) {
    using test::toBytes;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "low.zip", test::buildZip({{"a.txt", toBytes("low"), {}}, {"b.txt", toBytes("b"), {}}}));
    test::writeFile(tmp / "high.zip", test::buildZip({{"A.TXT", toBytes("high"), {}}}));
    test::writeFile(tmp / "dir" / "Sub" / "C.txt", toBytes("loose"));

    SearchSet set;
    auto open = [](const std::filesystem::path& p) {
        auto z = std::make_unique<ZipArchive>();
        CHECK(!z->open(p));
        return z;
    };
    set.add(open(tmp / "low.zip"), 1);
    set.add(open(tmp / "high.zip"), 2);
    auto dir = std::make_unique<DirectoryArchive>();
    CHECK(!dir->open(tmp / "dir"));
    set.add(std::move(dir), 0);

    std::vector<uint8_t> data;
    CHECK(!set.read("a.txt", data) && data == toBytes("high"));
    CHECK(!set.read("b.txt", data) && data == toBytes("b"));
    CHECK(!set.read("sub/c.txt", data) && data == toBytes("loose"));
    CHECK(set.read("d.txt", data));
    const auto names = set.list();
    CHECK(names.size() == 3 && names[0] == "A.TXT" && names[2] == "Sub/C.txt");
    CHECK(set.archives().size() == 3 && set.archives()[0] == set.find("a.txt"));
    CHECK(normalizeArchiveName("\\\\Textures\\\\Base//Floor.TGA") == "textures/base/floor.tga");
    std::filesystem::remove_all(tmp);
}

static void testMd4() {
    // RFC 1320's test values.
    auto hex = [](const std::string& text) {
        const auto d = md4(text.data(), text.size());
        std::string out;
        char buf[3];
        for (uint8_t b : d) {
            std::snprintf(buf, sizeof buf, "%02x", b);
            out += buf;
        }
        return out;
    };
    CHECK(hex("") == "31d6cfe0d16ae931b73c59d7e0c089c0");
    CHECK(hex("abc") == "a448017aaf21d8525fc10ae87aa6729d");
    CHECK(hex("message digest") == "d9130a8164549fe818874806e1c7014b");
    CHECK(hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
          "e33b4ddc9c38f2199c3e7b164fcc0536");
}

static void testNet() {
    net::Address a;
    CHECK(net::parseAddress("192.168.1.20:27961", 27960, a) && a.ip == 0xc0a80114u && a.port == 27961);
    CHECK(net::parseAddress("10.0.0.1", 27960, a) && a.port == 27960 && a.isLan());
    CHECK(a.toString() == "10.0.0.1:27960");
    CHECK(!net::parseAddress("256.0.0.1", 1, a) && !net::parseAddress("1.2.3", 1, a) &&
          !net::parseAddress("example.com", 1, a));
    const net::Address google{0x08080808u, 1};
    CHECK(!google.isLan());

    // Two sockets in memory: one to the other, and a broadcast.
    auto network = net::LoopbackNetwork::create();
    auto server = network->open(27960), other = network->open(27961), client = network->open(0);
    CHECK(server && client && other && client->port() == 27962 && !network->open(27960));
    CHECK(client->send({0x7f000001u, 27960}, {1, 2, 3}));
    net::Address from;
    std::vector<uint8_t> data;
    CHECK(server->receive(from, data) && data == std::vector<uint8_t>({1, 2, 3}) && from.port == 27962);
    CHECK(!server->receive(from, data));
    client->send(net::Address::broadcast(27960), {4});
    CHECK(server->receive(from, data) && data.size() == 1 && !other->receive(from, data));
}

static void testImage() {
    // 2x2 uncompressed 24-bit TGA, stored bottom row first (descriptor 0).
    std::vector<uint8_t> tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0};
    for (uint8_t v : {0, 0, 255, 0, 255, 0,      // bottom: red, green (stored B, G, R)
                      255, 0, 0, 255, 255, 255}) // top: blue, white
        tga.push_back(v);
    image::Image img;
    CHECK(!image::decode(tga, img));
    CHECK(img.w == 2 && img.h == 2);
    auto is = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        const uint8_t* p = img.pixel(x, y);
        return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
    };
    CHECK(is(0, 0, 0, 0, 255, 255) && is(1, 0, 255, 255, 255, 255));
    CHECK(is(0, 1, 255, 0, 0, 255) && is(1, 1, 0, 255, 0, 255));

    // 3x1 RLE 32-bit TGA, top row first (descriptor 0x28): a run of 3.
    const std::vector<uint8_t> rle = {0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 1, 0, 32, 0x28,
                                      0x82, 10, 20, 30, 128};
    CHECK(!image::decode(rle, img));
    CHECK(img.w == 3 && img.h == 1 && is(0, 0, 30, 20, 10, 128) && is(2, 0, 30, 20, 10, 128));
    CHECK(image::decode({1, 2, 3}, img));

    // Encoding a TGA and reading it back (alpha dropped).
    image::Image two;
    two.w = 2;
    two.h = 2;
    two.rgba = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    image::Image back;
    CHECK(!image::decode(image::encodeTga(two), back));
    CHECK(back.w == 2 && back.h == 2 && back.rgba == std::vector<uint8_t>({1, 2, 3, 255, 5, 6, 7, 255, 9, 10, 11,
                                                                           255, 13, 14, 15, 255}));

    // Blending half-transparent red over black.
    Surface s;
    s.create(2, 1, PixelFormat::RGBA32);
    img.w = 1;
    img.h = 1;
    img.rgba = {255, 0, 0, 128};
    image::blit(s, img, 1, 0);
    image::blit(s, img, 5, 5); // clipped away
    CHECK(s.row(0)[0] == 0 && s.row(0)[4] == 128 && s.row(0)[7] == 255);

    // PNG written (stored, uncompressed) and read back the same, alpha kept;
    // more than one deflate block.
    image::Image png;
    png.w = 200;
    png.h = 90;
    for (int y = 0; y < png.h; ++y)
        for (int x = 0; x < png.w; ++x)
            png.rgba.insert(png.rgba.end(), {uint8_t(x), uint8_t(y), uint8_t(x ^ y), uint8_t(x * 3 + y)});
    image::Image pngBack;
    CHECK(!image::decode(image::encodePng(png), pngBack) && pngBack.w == 200 && pngBack.h == 90 &&
          pngBack.rgba == png.rgba);
}

static void testGfxMath() {
    using namespace reluva::gfx;
    auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    auto same = [&](const Vec3& a, const Vec3& b) { return approx(a[0], b[0]) && approx(a[1], b[1]) && approx(a[2], b[2]); };
    // Depth runs 0 at the near plane to 1 at the far one; +Y stays up.
    const Mat4 proj = perspective(3.14159265f / 2, 1, 4, 100);
    CHECK(same(transformPoint(proj, {0, 0, -4}), {0, 0, 0}));
    CHECK(same(transformPoint(proj, {0, 0, -100}), {0, 0, 1}));
    CHECK(same(transformPoint(proj, {4, 4, -4}), {1, 1, 0}));
    // Looking along +X from (10, 0, 0) with Z up: +X is ahead (-Z in view
    // space), +Y is to the left, +Z up.
    const Mat4 view = lookAlong({10, 0, 0}, {1, 0, 0}, {0, 0, 1});
    CHECK(same(transformPoint(view, {15, 0, 0}), {0, 0, -5}));
    CHECK(same(transformPoint(view, {10, 2, 0}), {-2, 0, 0}));
    CHECK(same(transformPoint(view, {10, 0, 3}), {0, 3, 0}));
    CHECK(multiply(identity(), view) == view && multiply(view, identity()) == view);
    const Mat4 both = multiply(proj, view);
    CHECK(same(transformPoint(both, {14, 0, 0}), transformPoint(proj, transformPoint(view, {14, 0, 0}))));
}

// Runs `code` (hand-encoded, ending in RET) as a function of no arguments.
static uint32_t runX86(x86::Cpu& cpu, std::vector<uint8_t> code, bool* ok = nullptr) {
    const uint32_t at = 0x2000;
    std::copy(code.begin(), code.end(), cpu.memory().begin() + at);
    uint32_t result = 0;
    const Error e = cpu.call(at, {}, result);
    if (ok)
        *ok = !e;
    else if (e)
        std::fprintf(stderr, "x86: %s\n", e.message.c_str());
    return result;
}

static void testX86Instructions() {
    x86::Cpu cpu(16u << 20);
    cpu.setStack(0x100000);
    // mov eax, 5; add eax, 0xfffffffb; setz al; movzx eax, al; ret
    CHECK(runX86(cpu, {0xb8, 5, 0, 0, 0, 0x83, 0xc0, 0xfb, 0x0f, 0x94, 0xc0, 0x0f, 0xb6, 0xc0, 0xc3}) == 1);
    // mov ecx, 10; xor eax, eax; loop: add eax, ecx; dec ecx; jnz loop; ret  (55)
    CHECK(runX86(cpu, {0xb9, 10, 0, 0, 0, 0x31, 0xc0, 0x01, 0xc8, 0x49, 0x75, 0xfb, 0xc3}) == 55);
    // mov eax, -7; cdq; mov ecx, 2; idiv ecx; imul eax, edx, 100; ret  (edx = -1, eax = -100)
    CHECK(runX86(cpu, {0xb8, 0xf9, 0xff, 0xff, 0xff, 0x99, 0xb9, 2, 0, 0, 0, 0xf7, 0xf9, 0x6b, 0xc2, 100, 0xc3}) ==
          uint32_t(-100));
    // mov eax, 0x80000000; sar eax, 31; shr eax, 28; ret  (0xf)
    CHECK(runX86(cpu, {0xb8, 0, 0, 0, 0x80, 0xc1, 0xf8, 31, 0xc1, 0xe8, 28, 0xc3}) == 0xf);
    // mov eax, 0x12345678; bswap eax; ret
    CHECK(runX86(cpu, {0xb8, 0x78, 0x56, 0x34, 0x12, 0x0f, 0xc8, 0xc3}) == 0x78563412);
    // rep stosb then repe cmpsb: mov edi, 0x3000; mov ecx, 8; mov al, 0x41; rep stosb;
    // mov esi, 0x3000; mov edi, 0x3004; mov ecx, 4; repe cmpsb; mov eax, ecx; ret  (0: all equal)
    CHECK(runX86(cpu, {0xbf, 0, 0x30, 0, 0, 0xb9, 8, 0, 0, 0, 0xb0, 0x41, 0xf3, 0xaa, 0xbe, 0, 0x30, 0, 0, 0xbf, 4, 0x30,
                       0, 0, 0xb9, 4, 0, 0, 0, 0xf3, 0xa6, 0x89, 0xc8, 0xc3}) == 0);
    CHECK(cpu.read32(0x3000) == 0x41414141);
    // x87: fld1; fld1; faddp; fsqrt; fstp dword [0x3000]; mov eax, [0x3000]; ret  (sqrt 2 as a float)
    runX86(cpu, {0xd9, 0xe8, 0xd9, 0xe8, 0xde, 0xc1, 0xd9, 0xfa, 0xd9, 0x1d, 0, 0x30, 0, 0, 0xa1, 0, 0x30, 0, 0, 0xc3});
    float f;
    const uint32_t bitsOfF = cpu.read32(0x3000);
    std::memcpy(&f, &bitsOfF, 4);
    CHECK(f == std::sqrt(2.0f));
    // fs: mov eax, fs:[4]; ret
    cpu.fsBase = 0x4000;
    cpu.write32(0x4004, 0xabcd);
    CHECK(runX86(cpu, {0x64, 0xa1, 4, 0, 0, 0, 0xc3}) == 0xabcd);
    // Faults: a null read, and division by zero, fail the call.
    bool ok = true;
    runX86(cpu, {0xa1, 0, 0, 0, 0, 0xc3}, &ok);
    CHECK(!ok);
    runX86(cpu, {0x31, 0xc9, 0xf7, 0xf1, 0xc3}, &ok);
    CHECK(!ok);
    // A native function, stdcall with one argument: push 20; call [native]; ret
    const uint32_t native = cpu.addNative("double", [](x86::Cpu& c) {
        c.reg[x86::Cpu::EAX] = c.arg(0) * 2;
        return 4u;
    });
    cpu.write32(0x3100, native);
    CHECK(runX86(cpu, {0x6a, 20, 0xff, 0x15, 0, 0x31, 0, 0, 0xc3}) == 40);
}

static void testX86Dll() {
    x86::Cpu cpu(64u << 20);
    x86::Win32Process process(cpu);
    const x86::Win32Process::Module* m = nullptr;
    const std::vector<uint8_t> file(std::begin(kX86TestDll), std::end(kX86TestDll));
    CHECK(!process.load("testdll.dll", file, m));
    if (!m)
        return;
    CHECK(process.missingImports().empty());
    CHECK(!process.attach(*m));
    uint32_t r = 0;
    CHECK(!cpu.call(m->exportAddress("testdll_attached"), {}, r) && r == 0x0a280105);
    unsigned heapSum = 0;
    for (unsigned i = 0; i < 100; ++i)
        heapSum += uint8_t(i * 7);
    CHECK(!cpu.call(m->exportAddress("testdll_heap"), {100}, r) && r == heapSum);
    const uint32_t run = m->exportAddress("testdll_run");
    CHECK(run != 0);
    const int inputs = int(sizeof kX86TestInputs / sizeof kX86TestInputs[0]);
    for (int t = 0; t < int(sizeof kX86TestExpected / sizeof kX86TestExpected[0]); ++t)
        for (int i = 0; i < inputs; ++i) {
            const Error e = cpu.call(run, {uint32_t(t), kX86TestInputs[i][0], kX86TestInputs[i][1]}, r);
            if (e || r != kX86TestExpected[t][i]) {
                std::fprintf(stderr, "x86 test %d, input %d: %s (0x%08x, expected 0x%08x)\n", t, i,
                             e ? e.message.c_str() : "wrong result", r, kX86TestExpected[t][i]);
                ++test::failures();
            }
        }
    // Loaded again it goes elsewhere, relocated: its table of function
    // pointers (test 13) must still work.
    const x86::Win32Process::Module* again = nullptr;
    CHECK(!process.load("testdll2.dll", file, again));
    if (again) {
        CHECK(again->base != m->base);
        for (int i = 0; i < inputs; ++i)
            CHECK(!cpu.call(again->exportAddress("testdll_run"), {13, kX86TestInputs[i][0], kX86TestInputs[i][1]}, r) &&
                  r == kX86TestExpected[13][i]);
    }
}

static void testStreams(const std::filesystem::path& tmp) {
    MemoryReadStream s(std::vector<uint8_t>{0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 'h', 'i', 0, 9});
    CHECK(s.readUint16LE() == 0x1234);
    CHECK(s.readUint32LE() == 0x12345678u);
    CHECK(s.readCString() == "hi");
    CHECK(s.readByte() == 9);
    CHECK(!s.eos() && !s.err());
    s.readByte();
    CHECK(s.eos() && s.err());
    CHECK(s.seek(2) && s.readAll().size() == 8);
    // A part of a shared buffer.
    auto buffer = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{1, 2, 3, 4, 5});
    MemoryReadStream part(buffer, 3, 10);
    CHECK(part.size() == 2 && part.readByte() == 4 && part.seek(-1, SEEK_END) && part.readByte() == 5);

    MemoryWriteStream w;
    w.writeUint16LE(0x0102);
    w.writeUint32LE(0x03040506);
    w.writeFixedString("abc", 5);
    w.writeFixedString("toolong", 2);
    CHECK(w.size() == 13 && w.data()[0] == 2 && w.data()[2] == 6 && w.data()[6] == 'a' && w.data()[9] == 0 &&
          w.data()[11] == 't' && w.data()[12] == 'o');
    std::filesystem::create_directories(tmp);
    std::vector<uint8_t> back;
    CHECK(writeFile(tmp / "out.bin", w.data()) && readFile(tmp / "out.bin", back) && back == w.data());
    CHECK(!readFile(tmp / "missing.bin", back));

    // Big-endian and signed values, fixed-length strings, lines.
    MemoryReadStream b(std::vector<uint8_t>{0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0xFE, 0xFF, 0xFF, 0xFF, 'a', 'b', 0,
                                            'z', 'o', 'n', 'e', '\r', '\n', 't', 'w', 'o', '\r', '3', '\n', '4'});
    CHECK(b.readUint16BE() == 0x1234 && b.readUint32BE() == 0x12345678u && b.readSint32LE() == -2);
    CHECK(b.readFixedString(4) == "ab");
    CHECK(b.readLine() == "one" && b.readLine() == "two" && b.readLine() == "3" && b.readLine() == "4" && b.eos());
    CHECK(b.readLine().empty());
    MemoryWriteStream wbe;
    wbe.writeUint32BE(0x01020304);
    wbe.writeUint16BE(0x0506);
    wbe.writeString("ok");
    CHECK((wbe.data() == std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 'o', 'k'}));
    MemoryReadStream shortBE(std::vector<uint8_t>{1});
    CHECK(shortBE.readUint32BE() == 0x01000000u && shortBE.err());
}

static void testTextHelpers() {
    CHECK(text::toUpper("Cc.All") == "CC.ALL" && text::equalsIgnoreCase("Cc.All", "CC.ALL") &&
          !text::equalsIgnoreCase("CC.AL", "CC.ALL"));
    CHECK((text::split("a,b,,c", ',') == std::vector<std::string>{"a", "b", "", "c"}));
    CHECK(text::trim("  x y \t\n") == "x y" && text::trim("   ").empty());
    CHECK(text::format("%d-%s-%04x", 7, "z", 0xbeef) == "7-z-beef");
    CHECK(fs::normalizeDosPath("ds9\\art\\b\\r\\brdg0000.cmp") == "DS9/ART/B/R/BRDG0000.CMP");
    CHECK(fs::normalizeDosPath("/SOUND//tick.raw/") == "SOUND/TICK.RAW");
    CHECK(fs::normalizeDosPath("DEEP.EXE;1") == "DEEP.EXE");
    CHECK(text::toLower("Cc.ALL") == "cc.all" && text::hasSuffixIgnoreCase("COMMON.Dat", ".dat") &&
          !text::hasSuffixIgnoreCase("at", ".dat"));
    CHECK(text::matchGlob("MUSIC.1", "music.?") && !text::matchGlob("MUSIC.12", "MUSIC.?"));
    CHECK(text::matchGlob("view.ovr", "*.OVR") && text::matchGlob("a.b.c", "*.*.*") && text::matchGlob("", "*"));
    CHECK(!text::matchGlob("abc", "a*d") && text::matchGlob("abcbd", "a*b?") && !text::matchGlob("x", ""));
    // Bytes in a buffer, and the four-character codes they hold.
    const uint8_t bytes[8] = {'R', 'I', 'F', 'F', 0x10, 0x20, 0x30, 0x40};
    CHECK(readLE32(bytes) == tagLE('R', 'I', 'F', 'F') && readBE32(bytes) == tagBE('R', 'I', 'F', 'F'));
    CHECK(readLE64(bytes) == (uint64_t(0x40302010) << 32 | tagLE('R', 'I', 'F', 'F')));
    uint8_t out[4] = {};
    writeLE32(out, 0x04030201);
    writeLE16(out, 0x0605);
    CHECK(out[0] == 5 && out[1] == 6 && out[2] == 3 && out[3] == 4);
    // Rectangles.
    Rect r(0, 0, 10, 10);
    CHECK(r.contains(Point(9, 9)) && !r.contains(Point(10, 0)) && r.intersects(Rect(9, 9, 20, 20)) &&
          !r.intersects(Rect(10, 0, 20, 5)));
    r.clip(Rect(5, -5, 20, 8));
    CHECK(r == Rect(5, 0, 10, 8) && !r.isEmpty());
    r.translate(1, 2);
    CHECK(r == Rect(6, 2, 11, 10));
    r.clip(Rect(50, 50, 60, 60));
    CHECK(r == Rect() && r.isEmpty());
}

static void testDcl() {
    // The example from Mark Adler's blast.c: "AIAIAIAIAIAIA".
    const uint8_t packed[] = {0x00, 0x04, 0x82, 0x24, 0x25, 0x8f, 0x80, 0x7f};
    std::vector<uint8_t> out;
    CHECK(decompressDCL(packed, sizeof(packed), out));
    CHECK(std::string(out.begin(), out.end()) == "AIAIAIAIAIAIA");
    const uint8_t bad[] = {0x02, 0x09, 0xff};
    CHECK(!decompressDCL(bad, sizeof(bad), out));
}

static void testDrawAndPalette() {
    Surface dst, src;
    draw::create(dst, 4, 4);
    draw::create(src, 3, 3, 7);
    *draw::at(src, 1, 1) = 0;
    draw::blit(dst, src, 2, 2, 0);
    CHECK(*draw::at(dst, 2, 2) == 7 && *draw::at(dst, 3, 3) == 0 && *draw::at(dst, 1, 1) == 0);
    draw::blit(dst, src, -2, -2);
    CHECK(*draw::at(dst, 0, 0) == 7 && *draw::at(dst, 1, 0) == 0);
    draw::fill(dst, 1);
    draw::frameRect(dst, 0, 0, 4, 4, 9);
    CHECK(*draw::at(dst, 0, 0) == 9 && *draw::at(dst, 3, 2) == 9 && *draw::at(dst, 1, 1) == 1);
    draw::fillRect(dst, 2, 2, 10, 10, 5);
    CHECK(*draw::at(dst, 3, 3) == 5 && *draw::at(dst, 1, 3) == 9 && !draw::empty(dst) && draw::empty(Surface()));

    uint8_t vga[768] = {};
    vga[3] = 63;
    vga[4] = 32;
    Palette pal;
    pal.loadVga(vga);
    CHECK(pal.data()[3] == 255 && pal.data()[4] == 130 && pal.findBestColor(250, 0, 0) == 1);
    CHECK(pal.colors()[1].r == 255 && pal.colors()[1].g == 130 && pal.colors()[0].r == 0);
}

static void testAudioStreams() {
    using namespace reluva::audio;
    int16_t buf[5];
    RawStream u8(std::vector<uint8_t>{0x80, 0xff}, RawStream::Format::U8, 22050);
    CHECK(u8.read(buf, 4) == 2 && buf[0] == 0 && buf[1] == 127 << 8 && u8.ended());
    RawStream s16(std::vector<uint8_t>{0x00, 0x80}, RawStream::Format::S16LE, 22050);
    CHECK(s16.read(buf, 4) == 1 && buf[0] == -32768);
    RawStream be(std::vector<uint8_t>{0x12, 0x34}, RawStream::Format::S16BE, 22050);
    CHECK(be.read(buf, 4) == 1 && buf[0] == 0x1234);
    RawStream s8(std::vector<uint8_t>{0xff}, RawStream::Format::S8, 22050, false, 3);
    CHECK(s8.read(buf, 5) == 3 && buf[2] == -256 && s8.ended());
    RawStream forever(std::vector<uint8_t>{0x90}, RawStream::Format::U8, 22050, false, 0);
    CHECK(forever.read(buf, 5) == 5 && !forever.ended());

    QueuingStream q(22050, false);
    q.queue({1, 2, 3});
    CHECK(q.queued() == 3 && q.read(buf, 5) == 5); // padded with silence until finished
    CHECK(buf[2] == 3 && buf[3] == 0);
    q.queue({4});
    q.finish();
    CHECK(!q.ended() && q.read(buf, 5) == 1 && buf[0] == 4 && q.ended());
    RawStream whole(std::vector<uint8_t>(5000, 0x80), RawStream::Format::U8, 22050);
    CHECK(readAll(whole).size() == 5000);
}

static void testMixerStreams() {
    using namespace reluva::audio;
    std::vector<int16_t> out(2 * 64);
    Mixer m(22050);
    // A stream plays to its end, then frees its channel.
    const SoundHandle a = m.play(std::make_shared<RawStream>(std::vector<uint8_t>(100, 0xc0), RawStream::Format::U8,
                                                              22050),
                                 SoundType::Effects);
    CHECK(a != kNoSound && m.isPlaying(a));
    m.mix(out.data(), 64);
    CHECK(out[0] == 64 << 8 && out[0] == out[1] && m.position(a) == 64.0);
    m.mix(out.data(), 64);
    CHECK(!m.isPlaying(a) && out[2 * 63] == 0);
    // Type and master volumes, and twice as loud.
    m.setTypeVolume(SoundType::Speech, kMaxVolume / 2);
    m.play(std::make_shared<RawStream>(std::vector<uint8_t>(100, 0xc0), RawStream::Format::U8, 22050),
           SoundType::Speech);
    m.mix(out.data(), 8);
    CHECK(out[0] == (64 << 8) / 2);
    m.stopAll();
    m.setMasterVolume(kMaxBoostVolume);
    m.play(std::make_shared<RawStream>(std::vector<uint8_t>(100, 0xa0), RawStream::Format::U8, 22050),
           SoundType::Music);
    m.mix(out.data(), 8);
    CHECK(out[0] == (32 << 8) * 2 && m.masterVolume() == kMaxBoostVolume);
    m.stopAll();
    m.setMasterVolume(kMaxVolume);
    m.setTypeVolume(SoundType::Effects, kMaxBoostVolume + 100);
    CHECK(m.typeVolume(SoundType::Effects) == kMaxBoostVolume);
    // A decoded sound follows the master volume too.
    auto sound = std::make_shared<Sound>();
    sound->rate = 22050, sound->channels = 1, sound->samples = {1000, 1000};
    m.setMasterVolume(kMaxVolume / 2);
    m.play(std::shared_ptr<const Sound>(sound), SoundType::Music);
    m.mix(out.data(), 1);
    CHECK(out[0] == 500);
    m.stopAll();
    m.setMasterVolume(kMaxVolume);
    // A queue fed while it plays, kept by the engine too: half the rate,
    // so each sample is heard twice (interpolated).
    auto q = std::make_shared<QueuingStream>(11025, false);
    q->queue({0, 1000, 2000});
    const SoundHandle h = m.play(q, SoundType::Music);
    m.mix(out.data(), 4);
    CHECK(out[0] == 0 && out[2] == 500 && out[4] == 1000 && out[6] == 1500);
    q->finish();
    m.mix(out.data(), 8);
    CHECK(!m.isPlaying(h));
}

static void checkSmacker(const uint8_t* smk, size_t smkSize, const uint8_t* rgb, const uint8_t* pcm, size_t pcmSize,
                         bool stereo) {
    smacker::Decoder decoder;
    CHECK(decoder.load(std::make_unique<MemoryReadStream>(std::vector<uint8_t>(smk, smk + smkSize))));
    CHECK(decoder.width() == 32 && decoder.height() == 16 && decoder.frameCount() == 4);
    CHECK(decoder.frameMicros() == 100000u && decoder.hasAudio() && decoder.audioRate() == 22050);
    CHECK(decoder.audioStereo() == stereo);
    const size_t frameBytes = 32 * 16 * 3;
    int mismatched = 0;
    std::vector<int16_t> sound;
    for (int f = 0; f < 4; f++) {
        CHECK(decoder.decodeNextFrame());
        const Surface& s = decoder.surface();
        const uint8_t* pal = decoder.palette();
        for (int i = 0; i < 32 * 16; i++) {
            const uint8_t* c = pal + s.pixels[size_t(i)] * 3;
            const uint8_t* e = rgb + size_t(f) * frameBytes + size_t(i) * 3;
            if (c[0] != e[0] || c[1] != e[1] || c[2] != e[2])
                mismatched++;
        }
        std::vector<int16_t> a = decoder.takeAudio();
        sound.insert(sound.end(), a.begin(), a.end());
    }
    CHECK(!decoder.decodeNextFrame());
    CHECK(mismatched == 0);
    CHECK(sound.size() == pcmSize / 2);
    int soundMismatched = 0;
    for (size_t i = 0; i < sound.size() && i * 2 + 1 < pcmSize; i++)
        if (sound[i] != int16_t(pcm[i * 2] | (pcm[i * 2 + 1] << 8)))
            soundMismatched++;
    CHECK(soundMismatched == 0);
}

static void testSmacker() {
    // Compared with FFmpeg's decoding of the same files (smacker_videos.h).
    checkSmacker(kSmackerTest2, sizeof kSmackerTest2, kSmackerTest2Rgb, kSmackerTest2Pcm, sizeof kSmackerTest2Pcm,
                 false);
    checkSmacker(kSmackerTest4, sizeof kSmackerTest4, kSmackerTest4Rgb, kSmackerTest4Pcm, sizeof kSmackerTest4Pcm,
                 true);
    smacker::Decoder bad;
    CHECK(!bad.load(std::make_unique<MemoryReadStream>(std::vector<uint8_t>(200, 0x55))));
}

static void testX86Disassembler() {
    // Hand-checked against the Intel manuals' opcode maps; the text as
    // capstone writes it. Each at 0x10000.
    struct Case {
        std::vector<uint8_t> bytes;
        const char* text;
    };
    const Case cases[] = {
        {{0x55}, "push ebp"},
        {{0x89, 0xe5}, "mov ebp, esp"},
        {{0x83, 0xec, 0x10}, "sub esp, 0x10"},
        {{0x8b, 0x45, 0x08}, "mov eax, dword ptr [ebp + 8]"},
        {{0x89, 0x45, 0xf8}, "mov dword ptr [ebp - 8], eax"},
        {{0xe8, 0x00, 0x01, 0x00, 0x00}, "call 0x10105"},
        {{0x74, 0x05}, "je 0x10007"},
        {{0x0f, 0x85, 0x10, 0x00, 0x00, 0x00}, "jne 0x10016"},
        {{0xeb, 0xfe}, "jmp 0x10000"},
        {{0x8d, 0x04, 0x8d, 0x10, 0x00, 0x00, 0x00}, "lea eax, [ecx*4 + 0x10]"},
        {{0x8d, 0x44, 0x24, 0x04}, "lea eax, [esp + 4]"},
        {{0x8b, 0x04, 0x85, 0x00, 0x00, 0x05, 0x00}, "mov eax, dword ptr [eax*4 + 0x50000]"},
        {{0x8b, 0x4c, 0x9e, 0xfc}, "mov ecx, dword ptr [esi + ebx*4 - 4]"},
        {{0x0f, 0xb6, 0xc0}, "movzx eax, al"},
        {{0x0f, 0xbf, 0x46, 0x02}, "movsx eax, word ptr [esi + 2]"},
        {{0xd9, 0x45, 0xfc}, "fld dword ptr [ebp - 4]"},
        {{0xdd, 0x1d, 0x00, 0x00, 0x05, 0x00}, "fstp qword ptr [0x50000]"},
        {{0xdb, 0x2d, 0x00, 0x00, 0x05, 0x00}, "fld xword ptr [0x50000]"},
        {{0xde, 0xc9}, "fmulp st(1), st(0)"},
        {{0xd8, 0xc1}, "fadd st(0), st(1)"},
        {{0xdf, 0xe0}, "fnstsw ax"},
        {{0xd9, 0xe8}, "fld1"},
        {{0xd9, 0xc9}, "fxch st(1)"},
        {{0xf3, 0xa5}, "rep movsd dword ptr es:[edi], dword ptr [esi]"},
        {{0xf2, 0xae}, "repne scasb al, byte ptr es:[edi]"},
        {{0xaa}, "stosb byte ptr es:[edi], al"},
        {{0xc3}, "ret"},
        {{0xc2, 0x08, 0x00}, "ret 8"},
        {{0x6a, 0xff}, "push -1"},
        {{0x68, 0x00, 0x00, 0x05, 0x00}, "push 0x50000"},
        {{0x83, 0xe4, 0xf0}, "and esp, 0xfffffff0"},
        {{0x83, 0xc4, 0xf8}, "add esp, -8"},
        {{0xc7, 0x05, 0x10, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00}, "mov dword ptr [0x50010], 1"},
        {{0x66, 0x89, 0x03}, "mov word ptr [ebx], ax"},
        {{0x66, 0xc7, 0x45, 0xfe, 0x34, 0x12}, "mov word ptr [ebp - 2], 0x1234"},
        {{0xa1, 0x00, 0x00, 0x05, 0x00}, "mov eax, dword ptr [0x50000]"},
        {{0x64, 0x8b, 0x00}, "mov eax, dword ptr fs:[eax]"},
        {{0xff, 0x15, 0x00, 0x10, 0x05, 0x00}, "call dword ptr [0x51000]"},
        {{0xff, 0xe0}, "jmp eax"},
        {{0x0f, 0xaf, 0xc1}, "imul eax, ecx"},
        {{0x6b, 0xc0, 0x0c}, "imul eax, eax, 0xc"},
        {{0xc1, 0xe0, 0x04}, "shl eax, 4"},
        {{0xd1, 0xf8}, "sar eax, 1"},
        {{0x0f, 0xa4, 0xc2, 0x05}, "shld edx, eax, 5"},
        {{0x0f, 0xbc, 0xc8}, "bsf ecx, eax"},
        {{0x0f, 0x94, 0xc0}, "sete al"},
        {{0x0f, 0x44, 0xc1}, "cmove eax, ecx"},
        {{0x0f, 0xba, 0xe0, 0x03}, "bt eax, 3"},
        {{0xf0, 0x0f, 0xc1, 0x01}, "lock xadd dword ptr [ecx], eax"},
        {{0x3c, 0x20}, "cmp al, 0x20"},
        {{0xf7, 0xd8}, "neg eax"},
        {{0x99}, "cdq"},
        {{0x66, 0x98}, "cbw"},
        {{0x9b}, "wait"},
    };
    for (const Case& c : cases) {
        const x86::Instruction i = x86::disassemble(c.bytes.data(), c.bytes.size(), 0x10000);
        const std::string text = i.operands.empty() ? i.mnemonic : i.mnemonic + " " + i.operands;
        if (!i.valid || i.size != int(c.bytes.size()) || text != c.text) {
            std::printf("  disassembled \"%s\" (%d bytes), expected \"%s\" (%zu)\n", text.c_str(), i.size, c.text,
                        c.bytes.size());
            CHECK(false);
        }
    }
    // Direct jumps say where they go.
    const uint8_t call[] = {0xe8, 0xfb, 0xff, 0xff, 0xff};
    const x86::Instruction c = x86::disassemble(call, sizeof call, 0x10000);
    CHECK(c.hasTarget && c.target == 0x10000 && !x86::disassemble(cases[0].bytes.data(), 1, 0).hasTarget);
    // Not instructions: one byte, to show as data; cut short too.
    const uint8_t bad[] = {0x0f, 0xff, 0x8f, 0xc8, 0xe8, 0x00};
    const x86::Instruction b0 = x86::disassemble(bad, 2, 0), b1 = x86::disassemble(bad + 2, 2, 0),
                           b2 = x86::disassemble(bad + 4, 2, 0);
    CHECK(!b0.valid && b0.size == 1 && b0.mnemonic == "db" && b0.operands == "0xf");
    CHECK(!b1.valid && b1.size == 1 && !b2.valid && b2.size == 1);
}

static const reluva::test::Suite hashesSuite("common.unit.hashes", {
    {"md5", [](const std::filesystem::path&) { testMd5(); }},
    {"md4", [](const std::filesystem::path&) { testMd4(); }},
});

// BMPs keeping their palette indices (common/bmp.h): 8-bit stored bottom
// up, RLE8, 4-bit, 24-bit, and a DIB without the file header.
static void testBmp() {
    using namespace reluva;
    using Bytes = std::vector<uint8_t>;
    auto le = [](Bytes& b, uint32_t v, int n) {
        for (int i = 0; i < n; ++i)
            b.push_back(uint8_t(v >> (8 * i)));
    };
    // BITMAPINFOHEADER, a palette of `colors` (entry i: R i*10, G 1, B 2),
    // then `pixels`; with a BITMAPFILEHEADER first if `file`.
    auto make = [&](int w, int h, int bits, uint32_t compression, int colors, const Bytes& pixels, bool file) {
        Bytes info;
        le(info, 40, 4);
        le(info, uint32_t(w), 4);
        le(info, uint32_t(h), 4);
        le(info, 1, 2);
        le(info, uint32_t(bits), 2);
        le(info, compression, 4);
        le(info, uint32_t(pixels.size()), 4);
        le(info, 0, 4);
        le(info, 0, 4);
        le(info, uint32_t(colors), 4);
        le(info, 0, 4);
        for (int i = 0; i < colors; ++i)
            info.insert(info.end(), {2, 1, uint8_t(i * 10), 0});
        Bytes out;
        if (file) {
            out = {'B', 'M'};
            le(out, uint32_t(14 + info.size() + pixels.size()), 4);
            le(out, 0, 4);
            le(out, uint32_t(14 + info.size()), 4);
        }
        out.insert(out.end(), info.begin(), info.end());
        out.insert(out.end(), pixels.begin(), pixels.end());
        return out;
    };
    image::Bitmap bmp;
    // 3 x 2, 8-bit, bottom up: rows padded to 4 bytes, the last row first.
    CHECK(!image::decodeBmp(make(3, 2, 8, 0, 4, {3, 2, 1, 0, 0, 1, 2, 0}, true), bmp));
    CHECK(bmp.indexed() && bmp.surface.w == 3 && bmp.surface.h == 2 && bmp.paletteColors == 4);
    CHECK(bmp.surface.row(0)[0] == 0 && bmp.surface.row(0)[2] == 2 && bmp.surface.row(1)[0] == 3);
    CHECK(bmp.palette.data()[3 * 3] == 30 && bmp.palette.data()[3 * 3 + 1] == 1 && bmp.palette.data()[3 * 3 + 2] == 2);
    CHECK(bmp.palette.data()[4 * 3] == 0); // past the file's colours: black
    // RLE8 top down (negative height): a run, single pixels, end of line, a delta.
    const Bytes rle = {2, 7, 0, 3, 1, 2, 3, 0 /* padding */, 0, 0, 0, 2, 2, 0, 1, 9, 0, 1};
    CHECK(!image::decodeBmp(make(5, -2, 8, 1, 10, rle, true), bmp));
    CHECK(bmp.surface.row(0)[0] == 7 && bmp.surface.row(0)[1] == 7 && bmp.surface.row(0)[2] == 1 &&
          bmp.surface.row(0)[4] == 3 && bmp.surface.row(1)[0] == 0 && bmp.surface.row(1)[2] == 9);
    // 4-bit: two pixels a byte, the high nibble first.
    CHECK(!image::decodeBmp(make(3, 1, 4, 0, 16, {0x12, 0x30, 0, 0}, true), bmp));
    CHECK(bmp.surface.row(0)[0] == 1 && bmp.surface.row(0)[1] == 2 && bmp.surface.row(0)[2] == 3 && bmp.paletteColors == 16);
    // 24-bit: B, G, R as RGBA32.
    CHECK(!image::decodeBmp(make(1, 1, 24, 0, 0, {30, 20, 10, 0}, true), bmp));
    CHECK(!bmp.indexed() && bmp.surface.row(0)[0] == 10 && bmp.surface.row(0)[2] == 30 && bmp.surface.row(0)[3] == 255);
    // A DIB: the pixels right after the palette.
    const Bytes dib = make(2, 1, 8, 0, 2, {1, 0, 0, 0}, false);
    CHECK(!image::decodeDib(dib.data(), dib.size(), bmp) && bmp.surface.row(0)[0] == 1 && bmp.palette.data()[3] == 10);
    // Not BMPs, or cut short.
    CHECK(image::decodeBmp(Bytes{'X', 'Y'}, bmp) && bmp.surface.w == 0);
    Bytes cut = make(4, 4, 8, 0, 2, {1, 2, 3, 4}, true);
    CHECK(image::decodeBmp(cut, bmp));
    CHECK(image::decodeBmp(make(1, 1, 2, 0, 4, {0, 0, 0, 0}, true), bmp)); // 2 bits per pixel isn't a BMP depth
}

// The serializer writes what it reads: little-endian values of the size
// asked for, signed ones sign-extended, strings and vectors with their
// lengths; a short read fails.
static void testSerializer() {
    using namespace reluva;
    int32_t a = -2;
    uint16_t b = 0xBEEF;
    int16_t c = -300;
    bool d = true;
    double e = 1.5;
    std::string f = "Picard";
    std::vector<int> g = {1, -1, 70000};
    int64_t wide = -5;
    MemoryWriteStream out;
    {
        Serializer s(nullptr, &out);
        CHECK(s.isSaving() && !s.isLoading());
        s.syncAsSint32(a);
        s.syncAsUint16(b);
        s.syncAsSint16(c);
        s.syncAsBool(d);
        s.syncAsDouble(e);
        s.syncString(f);
        s.syncVector(g, [](Serializer& t, int& v) { t.syncAsInt(v, 3); });
        s.syncAsInt(wide, 2);
        s.syncCount(7);
        CHECK(!s.err());
    }
    const std::vector<uint8_t> expectStart = {0xFE, 0xFF, 0xFF, 0xFF, 0xEF, 0xBE, 0xD4, 0xFE, 1};
    CHECK(out.size() == 4 + 2 + 2 + 1 + 8 + 4 + 6 + 4 + 9 + 2 + 4);
    CHECK(std::equal(expectStart.begin(), expectStart.end(), out.data().begin()));
    // The vector's third element in three bytes: 70000 = 0x011170.
    CHECK(out.data()[4 + 2 + 2 + 1 + 8 + 4 + 6 + 4 + 6] == 0x70 && out.data()[4 + 2 + 2 + 1 + 8 + 4 + 6 + 4 + 8] == 0x01);

    int32_t a2 = 0;
    uint16_t b2 = 0;
    int16_t c2 = 0;
    bool d2 = false;
    double e2 = 0;
    std::string f2;
    std::vector<int> g2;
    int64_t wide2 = 0;
    MemoryReadStream in(out.data());
    Serializer s(&in, nullptr);
    s.syncAsSint32(a2);
    s.syncAsUint16(b2);
    s.syncAsSint16(c2);
    s.syncAsBool(d2);
    s.syncAsDouble(e2);
    s.syncString(f2);
    s.syncVector(g2, [](Serializer& t, int& v) { t.syncAsInt(v, 3); });
    s.syncAsInt(wide2, 2);
    s.syncCount(7);
    CHECK(!s.err() && a2 == a && b2 == b && c2 == c && d2 && e2 == e && f2 == f && g2 == g && wide2 == -5);
    // Reading on past the end fails; so does a count that differs.
    uint32_t more = 0;
    s.syncAsUint32(more);
    CHECK(s.err());
    MemoryWriteStream counted;
    Serializer w(nullptr, &counted);
    w.syncCount(3);
    MemoryReadStream back(counted.data());
    Serializer r(&back, nullptr);
    r.syncCount(4);
    CHECK(r.err());
}

static const reluva::test::Suite filesSuite("common.unit.files", {
    {"filesystem and detection", [](const std::filesystem::path& tmp) { testFsAndDetection(tmp / "fs"); }},
    {"config", [](const std::filesystem::path& tmp) { testConfig(tmp / "cfg"); }},
    {"search set", [](const std::filesystem::path& tmp) { testSearchSet(tmp / "search"); }},
    {"streams", [](const std::filesystem::path& tmp) { testStreams(tmp / "streams"); }},
    {"serializer", [](const std::filesystem::path&) { testSerializer(); }},
});

static const reluva::test::Suite archivesSuite("common.unit.archives", {
    {"zip", [](const std::filesystem::path& tmp) { testZip(tmp / "zip"); }},
    {"inflate and wise", [](const std::filesystem::path& tmp) { testInflateAndWise(tmp / "wise"); }},
    {"dcl", [](const std::filesystem::path&) { testDcl(); }},
});

static const reluva::test::Suite textSuite("common.unit.text", {
    {"utf16", [](const std::filesystem::path&) { testUtf16(); }},
    {"font", [](const std::filesystem::path&) { testFont(); }},
    {"strings", [](const std::filesystem::path&) { testTextHelpers(); }},
});

// A paused sound keeps its place and plays on from there; it still counts
// as playing, and can be stopped while paused.
static void testMixerPause() {
    using namespace reluva::audio;
    auto s = std::make_shared<Sound>();
    s->rate = 22050, s->channels = 1, s->samples = {1, 2, 3, 4};
    std::vector<int16_t> out(8);
    Mixer m(22050);
    const SoundHandle h = m.play(std::shared_ptr<const Sound>(s), SoundType::Effects);
    m.mix(out.data(), 1);
    CHECK(out[0] == 1 && !m.isPaused(h));
    m.setPaused(h, true);
    m.mix(out.data(), 2);
    CHECK(out[0] == 0 && out[3] == 0 && m.isPlaying(h) && m.isPaused(h) && m.position(h) == 1.0);
    m.setPaused(h, false);
    m.mix(out.data(), 2);
    CHECK(out[0] == 2 && out[2] == 3 && !m.isPaused(h));
    m.setPaused(h, true);
    m.stop(h);
    CHECK(!m.isPlaying(h) && !m.isPaused(h));
    m.setPaused(kNoSound, true); // nothing to pause
    // Everything paused: silence, and the sound plays on afterwards.
    const SoundHandle g = m.play(std::shared_ptr<const Sound>(s), SoundType::Music);
    m.setAllPaused(true);
    m.mix(out.data(), 2);
    CHECK(out[0] == 0 && m.allPaused() && m.position(g) == 0.0);
    m.setAllPaused(false);
    m.mix(out.data(), 1);
    CHECK(out[0] == 1 && !m.allPaused());
}

static const reluva::test::Suite audioSuite("common.unit.audio", {
    {"wav", [](const std::filesystem::path&) { testWav(); }},
    {"mp3", [](const std::filesystem::path&) { testMp3(); }},
    {"mixer", [](const std::filesystem::path&) { testMixer(); }},
    {"streams", [](const std::filesystem::path&) { testAudioStreams(); }},
    {"mixer streams", [](const std::filesystem::path&) { testMixerStreams(); }},
    {"mixer pause", [](const std::filesystem::path&) { testMixerPause(); }},
});

// Cursors (common/cursor.h): made from palette indices or a surface,
// scaled with their hotspot, and drawn by a backend that has to.
static void testCursors() {
    using namespace reluva;
    uint8_t palette[768] = {};
    palette[3] = 255, palette[4] = 0, palette[5] = 0;       // 1: red
    palette[6] = 0, palette[7] = 0, palette[8] = 255;       // 2: blue
    // 3 x 2, index 0 see-through, the hotspot at (1, 1); rows 4 bytes apart.
    const uint8_t pixels[8] = {0, 1, 2, 9, 1, 0, 1, 9};
    Cursor c = cursor::fromIndexed(pixels, 3, 2, 4, palette, 0, 1, 1);
    CHECK(c.w == 3 && c.h == 2 && c.hotX == 1 && c.hotY == 1 && !c.empty());
    CHECK(c.pixel(0, 0)[3] == 0 && c.pixel(1, 0)[0] == 255 && c.pixel(1, 0)[3] == 255);
    CHECK(c.pixel(2, 0)[2] == 255 && c.pixel(2, 0)[0] == 0 && c.pixel(1, 1)[3] == 0);
    CHECK(cursor::fromIndexed(nullptr, 3, 2, 4, palette, 0, 0, 0).empty() && Cursor{}.empty());

    // From a CLUT8 surface: the same.
    Surface s;
    s.create(3, 2, PixelFormat::CLUT8);
    std::copy_n(pixels, 3, s.row(0));
    std::copy_n(pixels + 4, 3, s.row(1));
    Color colors[256] = {};
    colors[1] = {255, 0, 0};
    colors[2] = {0, 0, 255};
    CHECK(cursor::fromSurface(s, colors, 0, 1, 1) == c);
    // From RGBA32: as it is.
    Surface rgba;
    rgba.create(2, 1, PixelFormat::RGBA32);
    rgba.row(0)[0] = 9, rgba.row(0)[3] = 128;
    const Cursor r = cursor::fromSurface(rgba, nullptr, -1, 0, 0);
    CHECK(r.w == 2 && r.pixel(0, 0)[0] == 9 && r.pixel(0, 0)[3] == 128 && r.pixel(1, 0)[3] == 0);

    // Twice the size, sharp: each pixel a 2 x 2 block, the hotspot with it.
    const Cursor big = cursor::scaled(c, 2);
    CHECK(big.w == 6 && big.h == 4 && big.hotX == 2 && big.hotY == 2);
    CHECK(big.pixel(2, 0)[0] == 255 && big.pixel(3, 1)[0] == 255 && big.pixel(0, 0)[3] == 0 && big.pixel(5, 1)[2] == 255);
    CHECK(cursor::scaled(c, 1) == c);
    // Smoothed: a see-through neighbour fades the alpha, not the colour.
    const Cursor soft = cursor::scaled(c, 1.5f, true);
    CHECK(soft.w == 5 && soft.h == 3);
    bool faded = false;
    for (int y = 0; y < soft.h; ++y)
        for (int x = 0; x < soft.w; ++x) {
            const uint8_t* p = soft.pixel(x, y);
            if (p[3] > 0 && p[3] < 255 && p[1] == 0)
                faded = true;
        }
    CHECK(faded);

    // Drawn with the hotspot at (0, 0), so its top left is off the surface:
    // clipped, see-through pixels left as they were.
    Surface target;
    target.create(4, 4, PixelFormat::RGBA32);
    for (auto& b : target.pixels)
        b = 50;
    cursor::draw(target, c, 0, 0);
    CHECK(target.row(0)[0] == 50 && target.row(0)[4] == 255 && target.row(0)[5] == 0 && target.row(0)[8] == 50);
    cursor::draw(target, c, 3, 3); // its red pixel at (3, 2), its blue at (4, 2): off the surface
    CHECK(target.row(2)[12] == 255 && target.row(2)[13] == 0);

    // The headless System keeps what it was given.
    HeadlessSystem sys;
    System& base = sys;
    base.setMouseCursor(&c);
    CHECK(sys.cursor() == c && sys.cursorChanges() == 1);
    base.setMouseCursorVisible(false);
    base.setMouseCursor(nullptr);
    CHECK(sys.cursor().empty() && !sys.cursorVisible() && sys.cursorChanges() == 2);
}

static const reluva::test::Suite imageSuite("common.unit.image", {
    {"image", [](const std::filesystem::path&) { testImage(); }},
    {"3d math", [](const std::filesystem::path&) { testGfxMath(); }},
    {"8-bit drawing and palettes", [](const std::filesystem::path&) { testDrawAndPalette(); }},
    {"cursors", [](const std::filesystem::path&) { testCursors(); }},
    {"bmp with its palette", [](const std::filesystem::path&) { testBmp(); }},
});

// Cinepak frames and an AVI holding them, built here: an 8 x 4 picture
// of two blocks, the first V1 (one 2x2 cell per quarter), the second V4.
namespace cinepak_test {
using Bytes = std::vector<uint8_t>;
void be(Bytes& b, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i)
        b.push_back(uint8_t(v >> (8 * i)));
}
void le(Bytes& b, uint32_t v, int n) {
    for (int i = 0; i < n; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}
Bytes chunk(int id, const Bytes& body) {
    Bytes b{uint8_t(id)};
    be(b, uint32_t(body.size()) + 4, 3);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}
// One strip holding `chunks`, in a frame of its own.
Bytes frame(const Bytes& chunks, bool intra) {
    Bytes strip{uint8_t(intra ? 0x10 : 0x11)};
    be(strip, 12 + uint32_t(chunks.size()), 3);
    be(strip, 0, 2); // y1
    be(strip, 0, 2); // x1
    be(strip, 4, 2); // y2
    be(strip, 8, 2); // x2
    strip.insert(strip.end(), chunks.begin(), chunks.end());
    Bytes f{0};
    be(f, 10 + uint32_t(strip.size()), 3);
    be(f, 8, 2);
    be(f, 4, 2);
    be(f, 1, 2);
    f.insert(f.end(), strip.begin(), strip.end());
    return f;
}
Bytes firstFrame() {
    const Bytes v1 = {10, 20, 30, 40, 0, 0, 100, 100, 100, 100, 10, uint8_t(-5)};
    const Bytes v4 = {200, 201, 202, 203, 0, 0};
    Bytes image;
    be(image, 0x40000000, 4); // block 0 V1, block 1 V4
    image.insert(image.end(), {0, 0, 0, 0, 0});
    Bytes chunks;
    for (const Bytes& c : {chunk(0x22, v1), chunk(0x20, v4), chunk(0x30, image)})
        chunks.insert(chunks.end(), c.begin(), c.end());
    return frame(chunks, true);
}
Bytes secondFrame() {
    Bytes image;
    be(image, 0x80000000, 4); // block 0 coded as V1 (entry 1), block 1 skipped
    image.push_back(1);
    return frame(chunk(0x31, image), false);
}
bool is(const reluva::Surface& s, int x, int y, int r, int g, int b) {
    const uint8_t* p = s.row(y) + x * 4;
    return p[0] == r && p[1] == g && p[2] == b && p[3] == 255;
}
bool grey(const reluva::Surface& s, int x, int y, int v) { return is(s, x, y, v, v, v); }
} // namespace cinepak_test

static void testCinepak() {
    using namespace cinepak_test;
    reluva::cinepak::Decoder decoder(8, 4);
    const Bytes f = firstFrame();
    CHECK(decoder.decode(f.data(), uint32_t(f.size())));
    const reluva::Surface& p = decoder.surface();
    CHECK(p.w == 8 && p.h == 4 && p.format == reluva::PixelFormat::RGBA32);
    // V1: each luma sample covers 2 x 2 pixels.
    CHECK(grey(p, 0, 0, 10) && grey(p, 1, 1, 10) && grey(p, 2, 0, 20) && grey(p, 0, 2, 30) && grey(p, 3, 3, 40));
    // V4: four 2 x 2 cells, each pixel its own sample.
    CHECK(grey(p, 4, 0, 200) && grey(p, 5, 0, 201) && grey(p, 4, 1, 202) && grey(p, 5, 1, 203) && grey(p, 6, 0, 200));
    // An inter frame redraws the coded block only, from the kept codebook:
    // Y 100, U 10, V -5 is R 90, G 100, B 120.
    const Bytes g = secondFrame();
    CHECK(decoder.decode(g.data(), uint32_t(g.size())));
    CHECK(is(p, 0, 0, 90, 100, 120) && grey(p, 5, 0, 201));
    // A dropped frame keeps the picture; a stub isn't a frame.
    CHECK(decoder.decode(nullptr, 0) && is(p, 0, 0, 90, 100, 120));
    const uint8_t stub[4] = {};
    CHECK(!decoder.decode(stub, 4));
}

static void testAvi() {
    using namespace cinepak_test;
    const Bytes video1 = firstFrame(), video2 = secondFrame();
    auto tag = [](Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); };
    auto riff = [&](Bytes& b, const char* t, const Bytes& body) {
        tag(b, t);
        le(b, uint32_t(body.size()), 4);
        b.insert(b.end(), body.begin(), body.end());
        if (body.size() & 1)
            b.push_back(0);
    };
    auto list = [&](const char* type, const Bytes& body) {
        Bytes b;
        tag(b, type);
        b.insert(b.end(), body.begin(), body.end());
        return b;
    };
    Bytes avih, vstrh, vstrf, astrh, astrf, hdrl, vstrl, astrl, movi, avi;
    le(avih, 50000, 4);
    avih.resize(56);
    tag(vstrh, "vids");
    tag(vstrh, "cvid");
    vstrh.resize(56);
    le(vstrf, 40, 4);
    le(vstrf, 8, 4);
    le(vstrf, 4, 4);
    vstrf.resize(40);
    tag(astrh, "auds");
    astrh.resize(56);
    le(astrf, 1, 2);
    le(astrf, 2, 2);
    le(astrf, 22050, 4);
    le(astrf, 88200, 4);
    le(astrf, 4, 2);
    le(astrf, 16, 2);
    riff(vstrl, "strh", vstrh);
    riff(vstrl, "strf", vstrf);
    riff(astrl, "strh", astrh);
    riff(astrl, "strf", astrf);
    riff(hdrl, "avih", avih);
    riff(hdrl, "LIST", list("strl", vstrl));
    riff(hdrl, "LIST", list("strl", astrl));
    riff(movi, "00db", video1);
    riff(movi, "01wb", Bytes{1, 2, 3, 4});
    riff(movi, "00id", video2); // as Star Trek: Generations names its inter frames
    riff(movi, "01wb", Bytes{5, 6, 7, 8});
    riff(avi, "LIST", list("hdrl", hdrl));
    riff(avi, "LIST", list("movi", movi));
    Bytes file;
    riff(file, "RIFF", list("AVI ", avi));

    reluva::avi::Decoder movie;
    CHECK(movie.load(std::make_unique<reluva::MemoryReadStream>(file)));
    CHECK(movie.width() == 8 && movie.height() == 4 && movie.frameCount() == 2 && movie.frameMicros() == 50000);
    CHECK(movie.videoCodec() == reluva::avi::fourcc("cvid") && movie.canDecode());
    CHECK(movie.hasAudio() && movie.audioRate() == 22050 && movie.audioChannels() == 2 && movie.audioBits() == 16);
    CHECK(movie.audio().size() == 8 && movie.audio()[4] == 5);
    CHECK(movie.decodeNextFrame() && grey(movie.surface(), 0, 0, 10) && movie.currentFrame() == 0);
    CHECK(movie.decodeNextFrame() && is(movie.surface(), 0, 0, 90, 100, 120) && movie.endOfVideo());
    CHECK(!movie.decodeNextFrame());
    std::vector<uint8_t> raw;
    CHECK(movie.readFrame(1, raw) && raw == video2 && !movie.readFrame(2, raw));
    // Not an AVI, or one in another codec.
    reluva::avi::Decoder other;
    CHECK(!other.load(std::make_unique<reluva::MemoryReadStream>(Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0})));
    Bytes msvc = file;
    const auto at = std::search(msvc.begin(), msvc.end(), std::begin("cvid"), std::begin("cvid") + 4);
    std::copy_n("MSVC", 4, at);
    CHECK(other.load(std::make_unique<reluva::MemoryReadStream>(msvc)) && !other.canDecode() &&
          other.videoCodec() == reluva::avi::fourcc("MSVC") && !other.decodeNextFrame() && other.surface().w == 0);
}

static const reluva::test::Suite videoSuite("common.unit.video", {
    {"smacker", [](const std::filesystem::path&) { testSmacker(); }},
    {"cinepak", [](const std::filesystem::path&) { testCinepak(); }},
    {"avi", [](const std::filesystem::path&) { testAvi(); }},
});

static const reluva::test::Suite netSuite("common.unit.net", {
    {"network", [](const std::filesystem::path&) { testNet(); }},
});

static const reluva::test::Suite x86Suite("common.unit.x86", {
    {"instructions", [](const std::filesystem::path&) { testX86Instructions(); }},
    {"test dll", [](const std::filesystem::path&) { testX86Dll(); }},
    {"disassembler", [](const std::filesystem::path&) { testX86Disassembler(); }},
});
