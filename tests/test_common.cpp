// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Small self-contained tests for common/. No framework, to keep deps at zero.

#include "common/audio/decode.h"
#include "common/audio/directmusic.h"
#include "common/audio/dls.h"
#include "common/audio/mixer.h"
#include "common/audio/mp3.h"
#include "common/audio/synth.h"
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
#include "common/pe.h"
#include "common/riff.h"
#include "common/text.h"
#include "common/winhelp.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"
#include "common/zip.h"
#include "zip_writer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>
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
    // Pitch: twice as high plays every other frame.
    const SoundHandle p = m.play(make(22050, 1, {0, 100, 200, 300, 400}), SoundType::Effects);
    m.setPitch(p, 2);
    m.mix(out.data(), 2);
    CHECK(out[0] == 0 && out[2] == 200 && m.position(p) == 4.0);
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

// A PE32 DLL with nothing but a string table: one .rsrc section at RVA
// 0x1000 (file offset 0x200), type 6, block 34 (ids 528-543), one
// language, two strings.
static std::vector<uint8_t> stringTableDll() {
    std::vector<uint8_t> f(0x400, 0);
    auto put16 = [&](size_t at, uint32_t v) {
        f[at] = uint8_t(v);
        f[at + 1] = uint8_t(v >> 8);
    };
    auto put32 = [&](size_t at, uint32_t v) {
        put16(at, v & 0xffff);
        put16(at + 2, v >> 16);
    };
    f[0] = 'M';
    f[1] = 'Z';
    put32(0x3c, 0x80);
    put32(0x80, 0x00004550);
    put16(0x84, 0x14c);               // i386
    put16(0x86, 1);                   // a section
    put16(0x94, 224);                 // the optional header's size
    put16(0x98, 0x10b);               // PE32
    put32(0x98 + 32, 0x1000);         // section alignment
    put32(0x98 + 36, 0x200);          // file alignment
    put32(0x98 + 56, 0x2000);         // image size
    put32(0x98 + 60, 0x200);          // headers' size
    put32(0x98 + 92, 16);             // data directories
    put32(0x98 + 96 + 2 * 8, 0x1000); // the resource directory
    put32(0x98 + 96 + 2 * 8 + 4, 0x100);
    const size_t sec = 0x98 + 224;
    std::memcpy(&f[sec], ".rsrc", 5);
    put32(sec + 8, 0x200);   // virtual size
    put32(sec + 12, 0x1000); // RVA
    put32(sec + 16, 0x200);  // raw size
    put32(sec + 20, 0x200);  // raw offset
    // The tree: type 6 -> name 34 -> language 1033 -> a data entry.
    const size_t r = 0x200;
    put16(r + 14, 1);
    put32(r + 16, 6);
    put32(r + 20, 0x80000000 | 0x18);
    put16(r + 0x18 + 14, 1);
    put32(r + 0x18 + 16, 34);
    put32(r + 0x18 + 20, 0x80000000 | 0x30);
    put16(r + 0x30 + 14, 1);
    put32(r + 0x30 + 16, 1033);
    put32(r + 0x30 + 20, 0x48); // the data entry
    put32(r + 0x48, 0x1060);    // the strings' RVA
    put32(r + 0x48 + 4, 0x40);
    // Sixteen strings: "" , "Go!", then "Fünf" at 543, the rest empty.
    size_t at = r + 0x60;
    for (int k = 0; k < 16; ++k) {
        const std::u16string s = k == 1 ? u"Go!" : k == 15 ? u"F\u00fcnf" : u"";
        put16(at, uint32_t(s.size()));
        at += 2;
        for (char16_t c : s) {
            put16(at, c);
            at += 2;
        }
    }
    return f;
}

static void testPeStrings() {
    const std::vector<uint8_t> dll = stringTableDll();
    PeImage pe;
    CHECK(!parsePe(dll, pe));
    CHECK(pe.resourceRva == 0x1000 && pe.sections.size() == 1);
    std::map<uint32_t, std::string> strings;
    CHECK(!readPeStrings(dll, pe, strings));
    CHECK(strings.size() == 2 && strings[529] == "Go!" && strings[543] == "F\xc3\xbcnf");
    pe.resourceRva = 0;
    CHECK(readPeStrings(dll, pe, strings));
}

// A RIFF chunk: id, little-endian size, bytes, padded to even.
std::vector<uint8_t> riffChunk(const std::string& id, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out(id.begin(), id.end());
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(body.size() >> (8 * i)));
    out.insert(out.end(), body.begin(), body.end());
    if (body.size() & 1)
        out.push_back(0);
    return out;
}
std::vector<uint8_t> riffList(const std::string& id, const std::string& type,
                              const std::vector<std::vector<uint8_t>>& children) {
    std::vector<uint8_t> body(type.begin(), type.end());
    for (const auto& c : children)
        body.insert(body.end(), c.begin(), c.end());
    return riffChunk(id, body);
}
std::vector<uint8_t> le(std::initializer_list<uint32_t> words, int width = 4) {
    std::vector<uint8_t> out;
    for (uint32_t w : words)
        for (int i = 0; i < width; ++i)
            out.push_back(uint8_t(w >> (8 * i)));
    return out;
}
std::vector<uint8_t> cat(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

// A DLS collection: one instrument (bank 1, program 5) whose one region
// (keys 40-80) plays a looped 100 Hz square wave recorded at key 57,
// with a release of `release` time cents.
std::vector<uint8_t> buildTestDls(int32_t release = -1200 * 65536) {
    std::vector<uint8_t> samples;
    for (int i = 0; i < 4410; ++i) {
        const int16_t v = (i / 110) % 2 ? 8000 : -8000;
        samples.push_back(uint8_t(v));
        samples.push_back(uint8_t(uint16_t(v) >> 8));
    }
    const std::vector<uint8_t> wsmp = cat({le({20}), le({57, 0}, 2), le({0, 0, 1}), le({16, 0, 0, 4400})});
    const std::vector<uint8_t> wave =
        riffList("LIST", "wave",
                 {riffChunk("fmt ", cat({le({1, 1}, 2), le({22050, 44100}), le({2, 16}, 2)})), riffChunk("wsmp", wsmp),
                  riffChunk("data", samples)});
    const std::vector<uint8_t> region =
        riffList("LIST", "rgn ",
                 {riffChunk("rgnh", le({40, 80, 0, 0, 0, 0}, 2)), riffChunk("wlnk", cat({le({0, 0}, 2), le({1, 0})}))});
    const std::vector<uint8_t> art = riffList(
        "LIST", "lart", {riffChunk("art1", cat({le({8, 1}), le({0, 0, 0x209, 0}, 2), le({uint32_t(release)})}))});
    const std::vector<uint8_t> ins =
        riffList("LIST", "ins ", {riffChunk("insh", le({1, 1, 5})), riffList("LIST", "lrgn", {region}), art});
    return riffList("RIFF", "DLS ",
                    {riffChunk("colh", le({1})), riffList("LIST", "lins", {ins}), riffChunk("ptbl", le({8, 1, 0})),
                     riffList("LIST", "wvpl", {wave})});
}

void testDls() {
    using namespace reluva::audio;
    // RIFF: nested lists and a chunk of odd size.
    RiffChunk top;
    CHECK(!readRiff(
        riffList("RIFF", "TEST", {riffChunk("abc ", {1, 2, 3}), riffList("LIST", "kids", {riffChunk("x   ", {7})})}),
        top));
    CHECK(top.type == "TEST" && top.children.size() == 2 && top.find("abc ")->size == 3 &&
          top.find("abc ")->u8(2) == 3);
    CHECK(top.findList("kids") && top.findList("kids")->find("x   ")->u8(0) == 7);
    CHECK(readRiff({'R', 'I', 'F', 'F', 20, 0, 0, 0, 'T', 'E', 'S', 'T', 'a', 'b', 'c', ' ', 99, 0, 0, 0}, top));

    const std::vector<uint8_t> dls = buildTestDls();
    DlsCollection c;
    CHECK(!readDls(dls, c));
    CHECK(c.instruments.size() == 1 && c.waves.size() == 1 && c.find(1, 5) && !c.find(0, 5));
    if (c.instruments.size() != 1 || c.waves.size() != 1)
        return;
    const DlsRegion& r = c.instruments[0].regions.at(0);
    CHECK(r.keyLow == 40 && r.keyHigh == 80 && r.wave == 0 && c.waves[0].samples.size() == 4410);
    CHECK(c.waves[0].info.unityNote == 57 && c.waves[0].info.looped && c.waves[0].info.loopLength == 4400);
    CHECK(c.instruments[0].articulation.size() == 1 && c.instruments[0].articulation[0].destination == 0x209);
    DlsCollection notDls;
    CHECK(readDls(riffList("RIFF", "WAVE", {}), notDls));

    // Played an octave up (key 69): 200 Hz, its sign changing every 2.5 ms;
    // it keeps going while held, and dies away once let go.
    DlsSynth synth(44100);
    synth.setInstrument(0, &c, &c.instruments[0]);
    synth.noteOn(0, 30, 100); // out of the region
    CHECK(synth.voices() == 0);
    synth.noteOn(0, 69, 127);
    CHECK(synth.voices() == 1);
    std::vector<float> out(2 * 44100, 0.0f);
    synth.render(out.data(), 44100);
    int changes = 0;
    float peak = 0;
    for (size_t i = 22050; i + 1 < 44100; ++i) {
        changes += (out[2 * i] > 0) != (out[2 * i + 2] > 0);
        peak = std::max(peak, std::fabs(out[2 * i]));
    }
    CHECK(changes >= 195 && changes <= 205 && peak > 0.05f);
    synth.noteOff(0, 69);
    std::fill(out.begin(), out.end(), 0.0f);
    synth.render(out.data(), 44100);
    CHECK(synth.voices() == 0 && std::fabs(out[2 * 40000]) < 1e-4f);
}

std::vector<uint8_t> utf16(const std::string& text) {
    std::vector<uint8_t> out;
    for (char c : text) {
        out.push_back(uint8_t(c));
        out.push_back(0);
    }
    out.push_back(0);
    out.push_back(0);
    return out;
}

void testDirectMusic() {
    using namespace reluva::audio;
    // Music values: octave, chord position, scale position, accidental,
    // over C major 7 in C major (no chord track).
    const DmChord c;
    CHECK(dmMusicToMidi(60, 0, c) == 60);      // fixed
    CHECK(dmMusicToMidi(0x4000, 14, c) == 48); // C
    CHECK(dmMusicToMidi(0x4100, 14, c) == 52); // E
    CHECK(dmMusicToMidi(0x4300, 14, c) == 59); // B
    CHECK(dmMusicToMidi(0x4010, 14, c) == 50); // D: a step up the scale
    CHECK(dmMusicToMidi(0x420f, 14, c) == 54); // G flat
    CHECK(dmMusicToMidi(0x4400, 10, c) == -1); // past the chord's top
    DmChord a;
    a.chordRoot = 9;                           // A minor
    a.chordPattern = 0x89;                     // root, minor third, fifth
    CHECK(dmMusicToMidi(0x4100, 14, a) == 60); // its third: C

    // A style: one part of four notes, one on each beat, in one pattern;
    // its band plays them on the test DLS's instrument (bank 1, 5).
    std::vector<uint8_t> prth = {4, 4, 4, 0};
    for (int v = 0; v < 32; ++v) {
        const auto w = le({v == 0 ? 0x3fffffffu : 0x20000000u});
        prth.insert(prth.end(), w.begin(), w.end());
    }
    for (int i = 0; i < 16; ++i)
        prth.push_back(uint8_t(i + 1));
    prth = cat({prth, le({1}, 2), {0, 0, 0, 0}});
    std::vector<uint8_t> notes = le({22});
    for (int beat = 0; beat < 4; ++beat)
        notes = cat({notes, le({uint32_t(beat * 4), 1, 96}), le({0, 60}, 2), {127, 0, 0, 0, 0, 16}});
    std::vector<uint8_t> prfc(prth.begin() + 132, prth.begin() + 148);
    prfc = cat({prfc, le({0}, 2), {0, 0, 0, 1}});
    const std::vector<uint8_t> bins = cat({le({0x105, 0, 0, 0, 0, 0, 0, 0xe3}), {64, 127}, le({0}, 2), le({0})});
    const std::vector<uint8_t> band = riffList(
        "RIFF", "DMBD",
        {riffList(
            "LIST", "lbil",
            {riffList("LIST", "lbin",
                      {riffChunk("bins", bins), riffList("LIST", "DMRF", {riffChunk("file", utf16("test.dls"))})})})});
    double tempo = 120;
    std::vector<uint8_t> tempoBytes(8);
    std::memcpy(tempoBytes.data(), &tempo, 8);
    const std::vector<uint8_t> style =
        riffList("RIFF", "DMST",
                 {riffChunk("styh", cat({{4, 4, 4, 0}, tempoBytes})),
                  riffList("LIST", "part", {riffChunk("prth", prth), riffChunk("note", notes)}),
                  riffList("LIST", "pttn",
                           {riffChunk("ptnh", cat({{4, 4, 4, 0, 1, 100}, le({0, 1}, 2)})),
                            riffList("LIST", "pref", {riffChunk("prfc", prfc)})}),
                  band});
    const std::vector<uint8_t> trkh(32, 0);
    const std::vector<uint8_t> segment = riffList(
        "RIFF", "DMSG",
        {riffChunk("segh", le({0, 4 * 3072, 0, 0, 0, 0})),
         riffList(
             "LIST", "trkl",
             {riffList(
                  "RIFF", "DMTK",
                  {riffChunk("trkh", trkh), riffChunk("cmnd", cat({le({12, 0}), le({0}, 2), {0, 0, 50, 0, 0, 0}}))}),
              riffList("RIFF", "DMTK", {riffChunk("trkh", trkh), riffChunk("tetr", cat({le({16, 0, 0}), tempoBytes}))}),
              riffList("RIFF", "DMTK",
                       {riffChunk("trkh", trkh),
                        riffList("LIST", "sttr",
                                 {riffList("LIST", "strf",
                                           {riffChunk("stmp", le({0})),
                                            riffList("LIST", "DMRF", {riffChunk("file", utf16("test.sty"))})})})})})});
    DmStyle st;
    CHECK(!readDmStyle(style, st));
    CHECK(st.parts.size() == 1 && st.patterns.size() == 1 && st.bands.size() == 1 && st.tempo == 120);
    if (st.parts.size() == 1)
        CHECK(st.parts[0].notes.size() == 4 && st.parts[0].variations() == std::vector<int>{0} &&
              st.parts[0].timeSig.gridTicks(4) == 768);
    DmSegment sg;
    CHECK(!readDmSegment(segment, sg));
    CHECK(sg.length == 4 * 3072 && sg.style.file == "test.sty" && sg.commands.size() == 1 &&
          sg.commands[0].groove == 50 && sg.tempos.size() == 1 && sg.tempos[0].second == 120);

    const std::vector<uint8_t> dls = buildTestDls(-5000 * 65536); // a quick release
    std::map<std::string, const std::vector<uint8_t>*> files = {
        {"test.sgt", &segment}, {"test.sty", &style}, {"test.dls", &dls}};
    DmPerformance perf(22050, [&](const std::string& name, std::vector<uint8_t>& out) {
        const auto it = files.find(name);
        if (it == files.end())
            return false;
        out = *it->second;
        return true;
    });
    CHECK(perf.play("missing.sgt"));
    CHECK(!perf.play("test.sgt"));
    // At 120 beats a minute: a note every half second, silent between.
    std::vector<float> out(size_t(22050 * 9) * 2, 0.0f);
    for (size_t at = 0; at < out.size() / 2; at += 256)
        perf.read(out.data() + 2 * at, int(std::min<size_t>(256, out.size() / 2 - at)));
    auto level = [&](float from, float to) {
        double sum = 0;
        const size_t a = size_t(from * 22050), b = size_t(to * 22050);
        for (size_t i = a; i < b; ++i)
            sum += double(out[2 * i]) * out[2 * i];
        return std::sqrt(sum / double(b - a));
    };
    for (float beat = 0; beat < 3.9f; beat += 0.5f)
        CHECK(level(beat, beat + 0.05f) > 0.01 && level(beat + 0.25f, beat + 0.45f) < 0.001);
    // Four measures, then it's over.
    CHECK(level(8.3f, 9) < 0.0001 && !perf.playing());
}

// A Windows Help file made here: its directory (a B+ tree of one leaf),
// |SYSTEM, and |TOPIC uncompressed with one topic of two records.
static void testWinHelp() {
    // The help compiler's LZ77: 'a' as it is, then three more from one back.
    const uint8_t packed[] = {0x02, 'a', 0x00, 0x00, 'b'};
    const std::vector<uint8_t> unpacked = winhelp::decompressLz77(packed, sizeof packed);
    CHECK(std::string(unpacked.begin(), unpacked.end()) == "aaaab");

    std::vector<uint8_t> f(16, 0);
    auto u8 = [&](uint32_t v) { f.push_back(uint8_t(v)); };
    auto u16 = [&](uint32_t v) { u8(v & 0xFF), u8(v >> 8 & 0xFF); };
    auto u32 = [&](uint32_t v) { u16(v & 0xFFFF), u16(v >> 16); };
    auto bytes = [&](const std::string& s) { f.insert(f.end(), s.begin(), s.end()); };
    auto fileHeader = [&](uint32_t used) { u32(used), u32(used), u8(0); };

    const uint32_t system = uint32_t(f.size());
    fileHeader(12);
    u16(0x036C), u16(33), u16(1), u32(0), u16(0); // version 4 (minor 33), not compressed

    // The topic: its header record (title "Driving"), then its text:
    // "Hello" (a font) " world" (paragraph's end) "" (the end), with a
    // Windows apostrophe.
    const uint32_t topic = uint32_t(f.size());
    const std::string ld2a("Driving\0", 8), ld2b("Hello\0 world\x92s\0\0", 16);
    const std::vector<uint8_t> ld1b = {0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
                                       0x00, 0x00, 0x80, 0x01, 0x00, 0x82, 0xFF};
    const uint32_t first = 12, second = first + 21 + 28 + uint32_t(ld2a.size());
    const uint32_t topicBytes = 12 + (second - 12) + 21 + uint32_t(ld1b.size() + ld2b.size());
    fileHeader(topicBytes);
    u32(0xFFFFFFFF), u32(first), u32(first); // the block's header
    u32(21 + 28 + uint32_t(ld2a.size())), u32(uint32_t(ld2a.size())), u32(0xFFFFFFFF), u32(second), u32(21 + 28);
    u8(0x02);
    u32(0), u32(0xFFFFFFFF), u32(0xFFFFFFFF), u32(0), u32(0xFFFFFFFF), u32(0xFFFFFFFF), u32(0xFFFFFFFF);
    bytes(ld2a);
    u32(21 + uint32_t(ld1b.size() + ld2b.size())), u32(uint32_t(ld2b.size())), u32(first), u32(0xFFFFFFFF),
        u32(21 + uint32_t(ld1b.size()));
    u8(0x20);
    f.insert(f.end(), ld1b.begin(), ld1b.end());
    bytes(ld2b);

    const uint32_t directory = uint32_t(f.size());
    fileHeader(38 + 1024);
    u16(0x293B), u16(0x0402), u16(1024);
    bytes(std::string("z4\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16));
    u16(0), u16(0), u16(0), u16(0xFFFF), u16(1), u16(1), u32(2);
    const size_t page = f.size();
    u16(0), u16(2), u16(0xFFFF), u16(0xFFFF);
    bytes(std::string("|SYSTEM\0", 8)), u32(system);
    bytes(std::string("|TOPIC\0", 7)), u32(topic);
    f.resize(page + 1024, 0);

    const uint32_t header[4] = {0x00035F3F, directory, 0xFFFFFFFF, uint32_t(f.size())};
    for (int i = 0; i < 16; ++i)
        f[size_t(i)] = uint8_t(header[i / 4] >> (i % 4 * 8));

    std::vector<winhelp::Topic> topics;
    CHECK(!winhelp::readHelp(f, topics));
    CHECK(topics.size() == 1);
    if (topics.size() == 1) {
        CHECK(topics[0].title == "Driving");
        CHECK(topics[0].text == "Hello world's");
    }
    f[0] = 0;
    CHECK(winhelp::readHelp(f, topics)); // not a help file
}

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "reluva_tests";
    testMd5();
    testUtf16();
    testFont();
    testWav();
    testMp3();
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
    testPeStrings();
    testDls();
    testDirectMusic();
    testWinHelp();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
