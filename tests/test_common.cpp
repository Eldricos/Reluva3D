// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Small self-contained tests for common/. No framework, to keep deps at zero.

#include "common/audio/decode.h"
#include "common/audio/ea_audio.h"
#include "common/audio/mixer.h"
#include "common/audio/mp3.h"
#include "common/audio/wav.h"
#include "data/tone440_mp3.h"
#include "data/x86_testdll.h"
#include "common/archive.h"
#include "common/config.h"
#include "common/detection.h"
#include "common/font.h"
#include "common/fs.h"
#include "common/gfx3d.h"
#include "common/image.h"
#include "common/md4.h"
#include "common/md5.h"
#include "common/net.h"
#include "common/text.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"
#include "common/zip.h"
#include "zip_writer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <memory>
#include <fstream>
#include <string>

using namespace reluva;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

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
    CHECK(m.play(nullptr, SoundType::Effects) == kNoSound && m.play(make(22050, 1, {}), SoundType::Effects) == kNoSound);
    // A stream is asked for frames at the output rate, and scaled by its volumes.
    struct Ramp : Stream {
        int16_t next = 0, rate = 0;
        void read(int16_t* o, int frames, int r) override {
            rate = int16_t(r);
            for (int i = 0; i < frames; ++i, next += 100)
                o[2 * i] = o[2 * i + 1] = next;
        }
    };
    auto ramp = std::make_shared<Ramp>();
    const SoundHandle st = m.playStream(ramp, SoundType::Effects, kMaxVolume / 2);
    m.mix(out.data(), 3);
    CHECK(ramp->rate == 22050 && out[0] == 0 && out[2] == 50 && out[5] == 100 && m.isPlaying(st));
    m.stop(st);
    m.mix(out.data(), 1);
    CHECK(!m.isPlaying(st) && out[0] == 0 && m.playStream(nullptr, SoundType::Effects) == kNoSound);
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
    // FILD/FISTP of 64-bit integers copy them exactly (a memcpy trick):
    // fild qword [0x3000]; fild qword [0x3010]; fxch st1; fistp qword [0x3018];
    // fistp qword [0x3020]; ret. Both values need more than a double's 53 bits.
    cpu.write32(0x3000, 0x6009fb10);
    cpu.write32(0x3004, 0x4853e428);
    cpu.write32(0x3010, 0x89abcdef);
    cpu.write32(0x3014, 0xf1234567);
    runX86(cpu, {0xdf, 0x2d, 0, 0x30, 0, 0, 0xdf, 0x2d, 0x10, 0x30, 0, 0, 0xd9, 0xc9, 0xdf, 0x3d, 0x18, 0x30, 0, 0,
                 0xdf, 0x3d, 0x20, 0x30, 0, 0, 0xc3});
    CHECK(cpu.read32(0x3018) == 0x6009fb10 && cpu.read32(0x301c) == 0x4853e428);
    CHECK(cpu.read32(0x3020) == 0x89abcdef && cpu.read32(0x3024) == 0xf1234567);
    // Decoded operands are cached by address, but code written over runs as
    // written: mov eax, [0x3000]; ret, then its displacement changed to
    // 0x3004 from outside; then by the program itself, under an entry
    // cached the run before: mov byte [0x2009], n; mov eax, [0x3000]; ret.
    cpu.write32(0x3000, 111);
    cpu.write32(0x3004, 222);
    cpu.write32(0x3008, 333);
    CHECK(runX86(cpu, {0x8b, 0x05, 0, 0x30, 0, 0, 0xc3}) == 111);
    CHECK(runX86(cpu, {0x8b, 0x05, 0x04, 0x30, 0, 0, 0xc3}) == 222);
    CHECK(runX86(cpu, {0xc6, 0x05, 0x09, 0x20, 0, 0, 0x00, 0x8b, 0x05, 0, 0x30, 0, 0, 0xc3}) == 111);
    CHECK(runX86(cpu, {0xc6, 0x05, 0x09, 0x20, 0, 0, 0x08, 0x8b, 0x05, 0, 0x30, 0, 0, 0xc3}) == 333);
    // Segment registers in a flat model: push es; pop es; mov eax, 0; mov ax, ds; ret
    CHECK(runX86(cpu, {0x06, 0x07, 0xb8, 0, 0, 0, 0, 0x66, 0x8c, 0xd8, 0xc3}) == 0x23);
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

static void testWin32Files() {
    // Drives on folders: names matched ignoring case, writes kept apart.
    namespace stdfs = std::filesystem;
    const stdfs::path root = stdfs::temp_directory_path() / "reluva_win32_files";
    stdfs::remove_all(root);
    stdfs::create_directories(root / "game" / "GameData");
    std::ofstream(root / "game" / "GameData" / "Config.TXT") << "x";
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    x86::Win32Process::Drive c;
    c.root = root / "game";
    c.writable = root / "save";
    process.addDrive(c);
    process.setCurrentDirectory("C:\\GameData");
    CHECK(process.fullPath("..\\x\\.\\y") == "C:\\x\\y");
    CHECK(process.fullPath("a/b") == "C:\\GameData\\a\\b");
    CHECK(process.fullPath("\\top") == "C:\\top");
    CHECK(process.hostPath("config.txt", false) == root / "game" / "GameData" / "Config.TXT");
    CHECK(process.hostPath("C:\\GAMEDATA\\CONFIG.TXT", true) == root / "save" / "GameData" / "Config.TXT");
    CHECK(process.hostPath("D:\\x", false).empty());
    stdfs::remove_all(root);
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
                ++failures;
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

// EA-XA frames written by hand (docs/formats/nfs/bnk.md).
static void testEaXa() {
    using namespace reluva::audio;
    // Filter 0, shift 12: each residual as it is (x 2^0); +1, -1, +7, -8.
    uint8_t frame[15] = {0x0c, 0x1f, 0x78};
    int16_t out[28] = {};
    EaXaChannel ch;
    decodeEaXaFrame(frame, ch, out, 28);
    CHECK(out[0] == 1 && out[1] == -1 && out[2] == 7 && out[3] == -8 && out[4] == 0);
    // Shift 0: x 4096; 0x7 -> 28672, then filter 1 (240/256 of the
    // previous) with residual 0.
    uint8_t loud[15] = {0x00, 0x70};
    decodeEaXaFrame(loud, ch, out, 2);
    CHECK(out[0] == 28672 && out[1] == 0);
    uint8_t predict[15] = {0x1c, 0x00};
    decodeEaXaFrame(predict, ch, out, 1);
    CHECK(out[0] == (240 * 0 + 128) >> 8);
    EaXaChannel fresh;
    fresh.previous = 1000;
    decodeEaXaFrame(predict, fresh, out, 1);
    CHECK(out[0] == (240 * 1000 + 128) >> 8);
    // Clamped, and a stride for interleaving.
    EaXaChannel c2;
    c2.previous = 30000;
    int16_t wide[4] = {};
    uint8_t clip1[15] = {0x10, 0x77};
    decodeEaXaFrame(clip1, c2, wide, 2, 2);
    CHECK(wide[0] == 32767 && wide[1] == 0 && wide[2] == 32767);
    // A stream: 30 samples need two frames.
    std::vector<uint8_t> two(30, 0);
    two[0] = 0x0c;
    two[1] = 0x10;
    std::vector<int16_t> samples;
    CHECK(!decodeEaXa(two.data(), two.size(), 30, samples));
    CHECK(samples.size() == 30 && samples[0] == 1);
    CHECK(decodeEaXa(two.data(), 29, 30, samples)); // short
}

// MicroTalk against values decoded from the same stream by Andrew
// D'Addesio's public-domain reference decoder (utkencode's utk.h).
static void testMicroTalk() {
    using namespace reluva::audio;
    std::vector<uint8_t> stream;
    uint32_t x = 12345;
    for (int i = 0; i < 400; ++i) {
        x = (x * 1103515245u + 12345u) & 0x7fffffffu;
        stream.push_back(uint8_t(x >> 16));
    }
    std::vector<int16_t> out;
    CHECK(!decodeMicroTalk(stream.data(), stream.size(), 1296, out));
    CHECK(out.size() == 1296);
    if (out.size() == 1296) {
        CHECK(out[0] == -253 && out[1] == -9 && out[100] == -2462 && out[431] == 1464);
        CHECK(out[432] == 659 && out[700] == -3404 && out[1000] == 6988 && out[1295] == -1254);
        uint32_t hash = 0;
        for (int16_t v : out)
            hash = hash * 31 + uint16_t(v);
        CHECK(hash == 68820045u);
    }
}

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "reluva_tests";
    testMd5();
    testUtf16();
    testFont();
    testWav();
    testMp3();
    testEaXa();
    testMicroTalk();
    testMixer();
    testFsAndDetection(tmp / "fs");
    testConfig(tmp / "cfg");
    testZip(tmp / "zip");
    testSearchSet(tmp / "search");
    testImage();
    testNet();
    testMd4();
    testGfxMath();
    testX86Instructions();
    testX86Dll();
    testWin32Files();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
