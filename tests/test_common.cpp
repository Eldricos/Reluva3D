// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Small self-contained tests for common/. No framework, to keep deps at zero.

#include "common/audio/decode.h"
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
#include "common/video/avi.h"
#include "common/video/indeo5.h"
#include "common/x86/a3d.h"
#include "common/x86/com.h"
#include "common/x86/cpu.h"
#include "common/x86/directx/dinput.h"
#include "common/x86/directx/directx.h"
#include "common/x86/msvcp60.h"
#include "common/x86/msvcrt.h"
#include "common/x86/win32.h"
#include "common/zip.h"
#include "zip_writer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <memory>
#include <fstream>
#include <random>
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
}

// A stream: it waits for what's pushed, plays it as it comes, and ends.
static void testMixerStream() {
    using namespace reluva::audio;
    std::vector<int16_t> out(40);
    Mixer m(22050);
    auto stream = std::make_shared<SoundStream>(22050, 1);
    const SoundHandle a = m.playStream(stream, SoundType::Effects, kMaxVolume, 0, 0);
    CHECK(a != kNoSound && m.isPlaying(a));
    m.mix(out.data(), 4);
    CHECK(out[0] == 0 && m.isPlaying(a)); // nothing yet: it waits
    const std::vector<int16_t> ten(10, 500);
    stream->push(ten.data(), ten.size());
    m.mix(out.data(), 12);
    // All but the last frame, which waits to be blended with what comes next.
    CHECK(out[0] == 500 && out[17] == 500 && out[18] == 0 && m.isPlaying(a));
    stream->push(ten.data(), ten.size());
    stream->end();
    m.mix(out.data(), 20);
    CHECK(out[0] == 500 && out[21] == 500 && out[22] == 0 && !m.isPlaying(a));
    CHECK(m.playStream(nullptr, SoundType::Effects) == kNoSound);
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

// Runs `code` with the registers and flags as they are, giving whether it
// failed.
static bool runX86Keeping(x86::Cpu& cpu, const std::vector<uint8_t>& code, uint32_t& result) {
    std::copy(code.begin(), code.end(), cpu.memory().begin() + 0x2000);
    return bool(cpu.call(0x2000, {}, result));
}

static void testX86Instructions(bool cached) {
    x86::Cpu cpu(16u << 20);
    cpu.cacheOn = cached;
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
    // Code that changes is run as it now is: mov eax, imm; ret, run, then
    // its immediate rewritten.
    CHECK(runX86(cpu, {0xb8, 1, 0, 0, 0, 0xc3}) == 1);
    cpu.write32(0x2001, 2);
    CHECK(runX86(cpu, {0xb8, 2, 0, 0, 0, 0xc3}) == 2);
    // ...and by the program itself, after it's been run: mov byte [0x2011], n;
    // jmp 0x2010 (mov eax, 5; ret), with n 7 and then 9.
    for (uint8_t n : {7, 9}) {
        std::vector<uint8_t> code = {0xc6, 0x05, 0x11, 0x20, 0, 0, n, 0xe9, 4, 0, 0, 0};
        code.resize(16, 0x90);
        code.insert(code.end(), {0xb8, 5, 0, 0, 0, 0xc3});
        CHECK(runX86(cpu, code) == n);
    }
}

// The decoded-instruction cache against running from the bytes: random
// runs of the instructions it has handlers for (and some it hasn't), with
// operands in registers and memory, give the same registers, flags, x87
// state and memory both ways.
static void testX86CacheAgreement() {
    std::mt19937 random(1234);
    auto pick = [&](uint32_t n) { return uint32_t(random() % n); };
    const uint32_t kData = 0x10000, kFs = 0x100;
    // Registers written as 16 or 32 bits: not ESP or EBP (the stack and the data's base).
    auto reg32 = [&] {
        static const uint8_t kRegs[6] = {0, 1, 2, 3, 6, 7};
        return kRegs[pick(6)];
    };
    auto instruction = [&](std::vector<uint8_t>& out) {
        std::vector<uint8_t> b;
        const bool o16 = pick(6) == 0;
        // A ModRM operand: a register, [ebp+d8], [ebp+d32], [d32] (through a
        // SIB), sometimes with FS.
        auto modrm = [&](int reg, bool wordReg, bool memoryOnly = false) {
            const int kind = memoryOnly ? 1 + int(pick(3)) : int(pick(4));
            if (kind == 0) {
                b.push_back(uint8_t(0xc0 | reg << 3 | (wordReg ? reg32() : pick(8))));
            } else if (kind == 1) {
                b.push_back(uint8_t(0x45 | reg << 3));
                b.push_back(uint8_t(pick(0x78)));
            } else if (kind == 2) {
                b.push_back(uint8_t(0x85 | reg << 3));
                const uint32_t d = pick(0x78);
                b.insert(b.end(), {uint8_t(d), 0, 0, 0});
            } else {
                b.push_back(uint8_t(0x04 | reg << 3));
                b.push_back(0x25);
                const uint32_t d = kData + pick(0x78);
                b.insert(b.end(), {uint8_t(d), uint8_t(d >> 8), uint8_t(d >> 16), 0});
            }
            if (kind != 0 && pick(4) == 0)
                b.insert(b.begin(), 0x64);
        };
        auto imm = [&](int bytes) {
            for (int i = 0; i < bytes; ++i)
                b.push_back(uint8_t(random()));
        };
        const int wordSize = o16 ? 2 : 4;
        switch (pick(24)) {
        case 0:
        case 1:
        case 2: { // ALU r/m, r and r, r/m
            const uint8_t op = uint8_t(pick(8) << 3 | pick(4));
            b.push_back(op);
            modrm((op & 2) ? ((op & 1) ? reg32() : int(pick(8))) : int(pick(8)), op & 1);
            break;
        }
        case 3: // ALU AL/EAX, imm
            b.push_back(uint8_t(pick(8) << 3 | 4 | pick(2)));
            imm((b.back() & 1) ? wordSize : 1);
            break;
        case 4: { // ALU r/m, imm
            const uint8_t op = std::array<uint8_t, 3>{0x80, 0x81, 0x83}[pick(3)];
            b.push_back(op);
            modrm(int(pick(8)), op != 0x80);
            imm(op == 0x81 ? wordSize : 1);
            break;
        }
        case 5: b.push_back(uint8_t(0x40 | pick(2) << 3 | reg32())); break; // INC, DEC
        case 6: // TEST
            b.push_back(uint8_t(0x84 | pick(2)));
            modrm(int(pick(8)), b.back() & 1);
            break;
        case 7:
        case 8: { // MOV
            const uint8_t op = uint8_t(0x88 | pick(4));
            b.push_back(op);
            modrm((op & 2) && (op & 1) ? reg32() : int(pick(8)), op & 1);
            break;
        }
        case 9: // MOV r, imm
            b.push_back(pick(2) ? uint8_t(0xb0 | pick(8)) : uint8_t(0xb8 | reg32()));
            imm(b.back() >= 0xb8 ? wordSize : 1);
            break;
        case 10: { // MOV r/m, imm
            const uint8_t op = uint8_t(0xc6 | pick(2));
            b.push_back(op);
            modrm(0, op & 1);
            imm((op & 1) ? wordSize : 1);
            break;
        }
        case 11: // LEA
            b.push_back(0x8d);
            modrm(reg32(), true, true);
            break;
        case 12: { // shifts and rotates
            const uint8_t op = std::array<uint8_t, 6>{0xc0, 0xc1, 0xd0, 0xd1, 0xd2, 0xd3}[pick(6)];
            b.push_back(op);
            modrm(int(pick(8)), op & 1);
            if (op <= 0xc1)
                imm(1);
            break;
        }
        case 13: // SETcc, MOVZX/MOVSX, IMUL
            switch (pick(3)) {
            case 0: b.insert(b.end(), {0x0f, uint8_t(0x90 | pick(16))}); modrm(int(pick(8)), false); break;
            case 1: {
                const uint8_t op = std::array<uint8_t, 4>{0xb6, 0xb7, 0xbe, 0xbf}[pick(4)];
                b.insert(b.end(), {0x0f, op});
                modrm(reg32(), op & 1);
                break;
            }
            default: b.insert(b.end(), {0x0f, 0xaf}); modrm(reg32(), true); break;
            }
            break;
        case 14: { // IMUL r, r/m, imm
            const uint8_t op = pick(2) ? 0x69 : 0x6b;
            b.push_back(op);
            modrm(reg32(), true);
            imm(op == 0x69 ? wordSize : 1);
            break;
        }
        case 15: { // TEST, NOT, NEG, MUL, IMUL r/m (all but TEST run from their bytes)
            const uint8_t op = uint8_t(0xf6 | pick(2));
            const int r = int(pick(6));
            b.push_back(op);
            modrm(r, op & 1);
            if (r <= 1)
                imm((op & 1) ? wordSize : 1);
            break;
        }
        case 16: // INC, DEC r/m
            b.push_back(uint8_t(0xfe | pick(2)));
            modrm(int(pick(2)), b.back() & 1);
            break;
        case 17: { // MOV with an absolute address
            const uint32_t d = kData + pick(0x78);
            b.push_back(uint8_t(0xa0 | pick(4)));
            b.insert(b.end(), {uint8_t(d), uint8_t(d >> 8), uint8_t(d >> 16), 0});
            break;
        }
        case 18: b.push_back(pick(2) ? 0x90 : 0x99); break;
        case 19:
        case 20: { // x87 on memory
            // (FISTP, FLDCW and FNSTCW run from their bytes.)
            static const uint8_t kOps[][2] = {{0xd8, 8}, {0xdc, 8}, {0xd9, 0}, {0xd9, 2}, {0xd9, 3}, {0xdd, 0},
                                              {0xdd, 2}, {0xdd, 3}, {0xdb, 0}, {0xdb, 3}, {0xd9, 5}, {0xd9, 7}};
            const auto& o = kOps[pick(12)];
            b.push_back(o[0]);
            modrm(o[1] == 8 ? int(pick(8)) : o[1], false, true);
            break;
        }
        case 21: { // x87 on registers
            static const uint8_t kOps[][2] = {{0xd8, 0xc0}, {0xd8, 0xc8}, {0xd8, 0xe0}, {0xd8, 0xf0}, {0xd8, 0xd0},
                                              {0xdc, 0xc0}, {0xdc, 0xe8}, {0xde, 0xc0}, {0xde, 0xf8}, {0xd9, 0xc0},
                                              {0xd9, 0xc8}, {0xdd, 0xd0}, {0xdd, 0xd8}, {0xdc, 0xd0}};
            const auto& o = kOps[pick(14)];
            b.insert(b.end(), {o[0], uint8_t(o[1] | pick(8))});
            break;
        }
        case 22: { // x87 without operands
            // (FCOMPP and FSQRT run from their bytes.)
            static const uint8_t kOps[][2] = {{0xd9, 0xe0}, {0xd9, 0xe1}, {0xd9, 0xe8}, {0xd9, 0xee},
                                              {0xdf, 0xe0}, {0xde, 0xd9}, {0xd9, 0xfa}};
            const auto& o = kOps[pick(7)];
            b.insert(b.end(), {o[0], o[1]});
            break;
        }
        default: break; // a jump, below
        }
        if (b.empty())
            return false;
        // The operand-size prefix, on what it means something for.
        if (o16) {
            const size_t at = b[0] == 0x64 ? 1 : 0;
            const uint8_t op = b[at];
            if (!(op <= 0x4f || (op >= 0x80 && op <= 0x8b) || op == 0xa1 || op == 0xa3 || (op >= 0xb8 && op <= 0xbf) ||
                  op == 0xc1 || op == 0xc7 || op == 0xd1 || op == 0xd3 || op == 0xf7 || op == 0xff || op == 0x69 ||
                  op == 0x6b || (op == 0x0f && (b[at + 1] == 0xaf || b[at + 1] == 0xb6))))
                return true; // left out: its immediate was made for 16 bits
            b.insert(b.begin(), 0x66);
        }
        out.insert(out.end(), b.begin(), b.end());
        return true;
    };
    for (int run = 0; run < 3000; ++run) {
        std::vector<uint8_t> code;
        for (int n = 0; n < 40; ++n) {
            std::vector<uint8_t> one;
            if (!instruction(one)) {
                // A jump (Jcc or JMP, short or near) over the next instruction.
                std::vector<uint8_t> next;
                while (next.empty())
                    instruction(next);
                const uint8_t length = uint8_t(next.size());
                switch (pick(4)) {
                case 0: one = {uint8_t(0x70 | pick(16)), length}; break;
                case 1: one = {0x0f, uint8_t(0x80 | pick(16)), length, 0, 0, 0}; break;
                case 2: one = {0xeb, length}; break;
                default: one = {0xe9, length, 0, 0, 0}; break;
                }
                one.insert(one.end(), next.begin(), next.end());
            }
            code.insert(code.end(), one.begin(), one.end());
        }
        code.push_back(0xc3);
        std::vector<uint8_t> data(0x200);
        for (uint8_t& d : data)
            d = uint8_t(random());
        std::array<uint32_t, 8> regs;
        for (uint32_t& r : regs)
            r = uint32_t(random());
        const uint32_t flags = 0x202 | (uint32_t(random()) & 0x8d5);
        std::array<double, 8> fpu;
        for (double& f : fpu)
            f = double(int32_t(random())) / 1024.0;
        std::array<uint32_t, 8> after[2];
        uint32_t flagsAfter[2], result[2];
        std::array<double, 8> fpuAfter[2];
        std::vector<uint8_t> dataAfter[2];
        bool ok[2];
        static x86::Cpu cpus[2] = {x86::Cpu(1u << 20), x86::Cpu(1u << 20)};
        for (int k = 0; k < 2; ++k) {
            x86::Cpu& cpu = cpus[k];
            cpu.cacheOn = k == 0;
            cpu.setInstructionLimit(10000);
            cpu.reg = regs;
            cpu.reg[x86::Cpu::EBP] = kData;
            cpu.setStack(0x80000);
            cpu.eflags = flags;
            cpu.fsBase = kFs;
            cpu.fpu.regs = fpu;
            cpu.fpu.top = 0;
            cpu.fpu.tags = 0;
            cpu.fpu.status = 0;
            std::copy(data.begin(), data.end(), cpu.memory().begin() + kData);
            std::copy(data.begin(), data.end(), cpu.memory().begin() + kData + kFs);
            ok[k] = !runX86Keeping(cpu, code, result[k]);
            after[k] = cpu.reg;
            flagsAfter[k] = cpu.eflags;
            fpuAfter[k] = cpu.fpu.regs;
            dataAfter[k].assign(cpu.memory().begin() + kData, cpu.memory().begin() + kData + 0x200 + kFs);
        }
        const bool same = ok[0] == ok[1] && after[0] == after[1] && flagsAfter[0] == flagsAfter[1] &&
                          std::memcmp(fpuAfter[0].data(), fpuAfter[1].data(), sizeof fpuAfter[0]) == 0 &&
                          dataAfter[0] == dataAfter[1] && cpus[0].fpu.top == cpus[1].fpu.top &&
                          cpus[0].fpu.status == cpus[1].fpu.status;
        if (!same) {
            std::fprintf(stderr, "x86 cache disagrees on run %d:", run);
            for (uint8_t c : code)
                std::fprintf(stderr, " %02x", c);
            std::fprintf(stderr, "\n");
            ++failures;
            break;
        }
    }
}

static void testX86Dll(bool cached) {
    x86::Cpu cpu(64u << 20);
    cpu.cacheOn = cached;
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

// The program's threads, run in turns: one counts and sleeps 50 ms at a
// time, getting its turn when the main thread sleeps and it's due; one
// returns, which ends it.
static void testThreads() {
    x86::Cpu cpu(64u << 20);
    x86::Win32Process process(cpu);
    uint32_t now = 1000;
    process.milliseconds = [&] { return now; };
    const uint32_t slot = process.memory().alloc(4), counter = process.memory().alloc(4);
    cpu.write32(slot, process.provided("KERNEL32.dll", "Sleep"));
    auto code = [&](std::vector<uint8_t> bytes) {
        const uint32_t at = process.memory().alloc(uint32_t(bytes.size()));
        std::copy(bytes.begin(), bytes.end(), cpu.memory().begin() + at);
        return at;
    };
    auto le = [](uint32_t v) { return std::vector<uint8_t>{uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; };
    std::vector<uint8_t> counting = {0xff, 0x05}; // inc dword [counter]
    for (uint8_t b : le(counter))
        counting.push_back(b);
    counting.insert(counting.end(), {0x6a, 50, 0xff, 0x15}); // push 50; call [Sleep]
    for (uint8_t b : le(slot))
        counting.push_back(b);
    counting.insert(counting.end(), {0xeb, 0xf0}); // jmp back to the start
    std::vector<uint8_t> sleeping = {0x6a, 0, 0xff, 0x15}; // the main thread: Sleep(0); return
    for (uint8_t b : le(slot))
        sleeping.push_back(b);
    sleeping.push_back(0xc3);
    const uint32_t thread = code(counting), main = code(sleeping), returning = code({0xc3});
    process.startThread(thread, 7);
    process.startThread(returning, 0);
    CHECK(process.threadsStarted() == 2 && process.threadsRunning() == 2);
    uint32_t r = 0;
    CHECK(!cpu.call(main, {}, r) && cpu.read32(counter) == 1 && process.threadsRunning() == 1);
    CHECK(!cpu.call(main, {}, r) && cpu.read32(counter) == 1); // not due yet
    now += 50;
    CHECK(!cpu.call(main, {}, r) && cpu.read32(counter) == 2);
    now += 120;
    CHECK(!cpu.call(main, {}, r) && cpu.read32(counter) == 3 && !process.inThread());
}

// MSVCRT's stand-ins and KERNEL32's files, called as the program would.
static void testMsvcrtAndFiles(const std::filesystem::path& tmp) {
    std::filesystem::create_directories(tmp / "Game");
    {
        std::ofstream(tmp / "Game" / "Data.txt", std::ios::binary) << "hello world";
        std::ofstream(tmp / "Game" / "Other.bin", std::ios::binary) << "x";
        std::ofstream(tmp / "Game" / "Lines.dat", std::ios::binary) << "ab\r\ncd\r\n";
    }
    x86::Cpu cpu(64u << 20);
    x86::Win32Process process(cpu);
    process.setProgram("Prog.exe", "C:\\Game\\");
    process.mapPath = [&](const std::string& path, bool forWriting) -> std::optional<std::filesystem::path> {
        if (path.rfind("C:\\Game\\", 0) != 0)
            return std::nullopt;
        if (forWriting)
            return path == "C:\\Game\\Out.dat" ? std::optional(tmp / "Game" / "Out.dat") : std::nullopt;
        if (path.size() == 8)
            return tmp / "Game";
        return fs::findFile(tmp / "Game", path.substr(8)).value_or(tmp / "Game" / "missing");
    };
    process.cdLabel = "LEGOATEN";
    x86::addMsvcrt(process);
    auto fn = [&](const char* dll, const char* name) { return process.provided(dll, name); };
    auto text = [&](const std::string& s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    uint32_t r = 0;
    CHECK(!cpu.call(fn("MSVCRT.dll", "strlen"), {text("abcdef")}, r) && r == 6);
    CHECK(!cpu.call(fn("MSVCRT.dll", "_stricmp"), {text("Alpha"), text("aLPHA")}, r) && r == 0);
    CHECK(!cpu.call(fn("MSVCRT.dll", "strcmp"), {text("a"), text("b")}, r) && int32_t(r) < 0);
    // Visual C++'s rand: from seed 1, 41, 18467, 6334.
    CHECK(!cpu.call(fn("MSVCRT.dll", "srand"), {1}, r));
    for (uint32_t expected : {41u, 18467u, 6334u})
        CHECK(!cpu.call(fn("MSVCRT.dll", "rand"), {}, r) && r == expected);
    // sscanf and sprintf.
    const uint32_t vars = process.memory().alloc(64);
    cpu.write32(vars + 48, vars);
    cpu.write32(vars + 52, vars + 4);
    cpu.write32(vars + 56, vars + 8);
    CHECK(x86::scanFormat(cpu, "12 3.5 word", "%d %f %s", vars + 48) == 3);
    float f = 0;
    const uint32_t fbits = cpu.read32(vars + 4);
    std::memcpy(&f, &fbits, 4);
    CHECK(cpu.read32(vars) == 12 && f == 3.5f && cpu.string(vars + 8) == "word");
    CHECK(!cpu.call(fn("MSVCRT.dll", "sprintf"), {vars + 16, text("%s-%03d"), text("id"), 7}, r) && r == 6 &&
          cpu.string(vars + 16) == "id-007");
    // The FPU-stack ones: _ftol cuts towards zero, _CIpow takes x below y.
    cpu.fpuPush(-2.7);
    CHECK(!cpu.call(fn("MSVCRT.dll", "_ftol"), {}, r) && r == uint32_t(-2) && cpu.reg[x86::Cpu::EDX] == 0xffffffffu);
    cpu.fpuPush(2);
    cpu.fpuPush(10);
    CHECK(!cpu.call(fn("MSVCRT.dll", "_CIpow"), {}, r) && cpu.fpuPop() == 1024);
    // _EH_prolog: a frame of the previous FS:[0], the handler (EAX) and -1,
    // linked in, with EBP pointing at the caller's EBP kept above it.
    const uint32_t chain = cpu.read32(cpu.fsBase);
    cpu.reg[x86::Cpu::EAX] = 0x1234;
    cpu.reg[x86::Cpu::EBP] = 0x5555;
    const uint32_t espBefore = cpu.reg[x86::Cpu::ESP];
    CHECK(!cpu.call(fn("MSVCRT.dll", "_EH_prolog"), {}, r));
    const uint32_t frame = cpu.read32(cpu.fsBase);
    CHECK(frame == espBefore - 4 - 12 && cpu.read32(frame) == chain && cpu.read32(frame + 4) == 0x1234 &&
          cpu.read32(frame + 8) == 0xffffffffu && cpu.read32(frame + 12) == 0x5555 && cpu.reg[x86::Cpu::EBP] == frame + 12);
    cpu.write32(cpu.fsBase, chain);

    // Files: fopen relative to the program's folder, fread, fclose.
    CHECK(!cpu.call(fn("MSVCRT.dll", "fopen"), {text("data.txt"), text("r")}, r) && r != 0);
    const uint32_t file = r;
    CHECK(!cpu.call(fn("MSVCRT.dll", "fread"), {vars, 1, 32, file}, r) && r == 11);
    CHECK(std::string(reinterpret_cast<const char*>(cpu.memory().data() + vars), 11) == "hello world");
    CHECK(!cpu.call(fn("MSVCRT.dll", "fclose"), {file}, r) && r == 0);
    CHECK(!cpu.call(fn("MSVCRT.dll", "fopen"), {text("nothere.txt"), text("rb")}, r) && r == 0);
    // Text mode: CR-LF read as LF (fgets, fgetc, fread) and written from LF;
    // binary mode keeps both.
    CHECK(!cpu.call(fn("MSVCRT.dll", "fopen"), {text("Lines.dat"), text("r")}, r) && r != 0);
    const uint32_t lines = r;
    CHECK(!cpu.call(fn("MSVCRT.dll", "fgets"), {vars, 32, lines}, r) && r == vars && cpu.string(vars) == "ab\n");
    CHECK(!cpu.call(fn("MSVCRT.dll", "fgetc"), {lines}, r) && r == 'c');
    CHECK(!cpu.call(fn("MSVCRT.dll", "fread"), {vars, 1, 32, lines}, r) && r == 2 && cpu.string(vars, 2) == "d\n");
    CHECK(!cpu.call(fn("MSVCRT.dll", "fclose"), {lines}, r) && r == 0);
    CHECK(!cpu.call(fn("MSVCRT.dll", "fopen"), {text("Lines.dat"), text("rb")}, r) && r != 0);
    const uint32_t raw = r;
    CHECK(!cpu.call(fn("MSVCRT.dll", "fread"), {vars, 1, 32, raw}, r) && r == 8);
    CHECK(!cpu.call(fn("MSVCRT.dll", "fclose"), {raw}, r) && r == 0);
    CHECK(!cpu.call(fn("MSVCRT.dll", "fopen"), {text("Out.dat"), text("w")}, r) && r != 0);
    const uint32_t written = r;
    CHECK(!cpu.call(fn("MSVCRT.dll", "fwrite"), {text("x\ny\n"), 1, 4, written}, r) && r == 4);
    CHECK(!cpu.call(fn("MSVCRT.dll", "fclose"), {written}, r) && r == 0);
    {
        std::ifstream in(tmp / "Game" / "Out.dat", std::ios::binary);
        CHECK(std::string(std::istreambuf_iterator<char>(in), {}) == "x\r\ny\r\n");
    }
    // CreateFileA, a mapped view, a search, the CD's label.
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateFileA"), {text("C:\\Game\\DATA.TXT"), 0x80000000u, 1, 0, 3, 0, 0}, r) &&
          r != 0xffffffffu);
    const uint32_t handle = r;
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateFileMappingA"), {handle, 0, 2, 0, 0, 0}, r) && r != 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "MapViewOfFile"), {r, 4, 0, 6, 0}, r) && cpu.string(r, 5) == "world");
    const uint32_t find = process.memory().alloc(320);
    CHECK(!cpu.call(fn("KERNEL32.dll", "FindFirstFileA"), {text("C:\\Game\\*.txt"), find}, r) && r != 0xffffffffu &&
          cpu.string(find + 44) == "Data.txt" && cpu.read32(find + 32) == 11);
    CHECK(!cpu.call(fn("KERNEL32.dll", "FindNextFileA"), {r, find}, r) && r == 0);
    const uint32_t label = process.memory().alloc(32);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetVolumeInformationA"), {text("D:\\"), label, 32, 0, 0, 0, 0, 0}, r) && r == 1 &&
          cpu.string(label) == "LEGOATEN");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetDriveTypeA"), {text("D:\\")}, r) && r == 5);

    // MulDiv rounds half away from zero; date and time text from a
    // SYSTEMTIME (2001-02-03, a Saturday, 14:05:09).
    CHECK(!cpu.call(fn("KERNEL32.dll", "MulDiv"), {10, 3, 4}, r) && r == 8);
    CHECK(!cpu.call(fn("KERNEL32.dll", "MulDiv"), {uint32_t(-10), 3, 4}, r) && r == uint32_t(-8));
    CHECK(!cpu.call(fn("KERNEL32.dll", "MulDiv"), {1, 2, 0}, r) && r == 0xffffffffu);
    const uint32_t st = process.memory().alloc(16);
    const uint16_t when[8] = {2001, 2, 6, 3, 14, 5, 9, 0};
    for (int i = 0; i < 8; ++i)
        cpu.write16(st + 2 * i, when[i]);
    const uint32_t out = process.memory().alloc(64);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetDateFormatA"), {0x409, 0, st, 0, out, 64}, r) && r == 9 &&
          cpu.string(out) == "2/3/2001");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetDateFormatA"), {0x409, 2, st, 0, out, 64}, r) &&
          cpu.string(out) == "Saturday, February 03, 2001");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetTimeFormatA"), {0x409, 0, st, 0, out, 64}, r) &&
          cpu.string(out) == "2:05:09 PM");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetTimeFormatA"), {0x409, 0, st, text("HH'h'mm"), out, 0}, r) && r == 6);
    CHECK(!cpu.call(fn("KERNEL32.dll", "QueryPerformanceFrequency"), {out}, r) && r == 1 && cpu.read32(out) == 1000000);
}

// Reluva's MSVCP60.dll, called as the games call it: strings in Visual C++
// 6's layout, a file stream reading lines, a growing string stream buffer
// and cerr.
static void testMsvcp60(const std::filesystem::path& tmp) {
    std::filesystem::create_directories(tmp / "Game");
    std::ofstream(tmp / "Game" / "Lines.txt", std::ios::binary) << "first line\nsecond\n";
    x86::Cpu cpu(64u << 20);
    x86::Win32Process process(cpu);
    process.setProgram("Prog.exe", "C:\\Game\\");
    process.mapPath = [&](const std::string& path, bool forWriting) -> std::optional<std::filesystem::path> {
        if (forWriting || path.rfind("C:\\Game\\", 0) != 0)
            return std::nullopt;
        return fs::findFile(tmp / "Game", path.substr(8)).value_or(tmp / "Game" / "missing");
    };
    std::string output;
    process.output = [&](const std::string& text) { output += text; };
    x86::addMsvcrt(process);
    const x86::Win32Process::Module* lib = nullptr;
    CHECK(!x86::loadMsvcp60(process, lib));
    if (!lib)
        return;
    CHECK(!process.attach(*lib));
    CHECK(process.missingImports().empty());
    auto fn = [&](const char* name) {
        const uint32_t a = lib->exportAddress(name);
        CHECK(a != 0);
        return a;
    };
    auto text = [&](const std::string& s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    uint32_t r = 0;
    auto method = [&](uint32_t self, const char* name, std::vector<uint32_t> args) {
        cpu.reg[x86::Cpu::ECX] = self;
        const bool ok = !cpu.call(fn(name), args, r);
        CHECK(ok);
        return r;
    };
    // string: the characters at +4, length +8, capacity +12, a 0 count
    // before them.
    const char* kCtorChars = "??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBDABV?$allocator@D@1@@Z";
    const char* kAppend = "?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z";
    const char* kFind = "?find@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z";
    const char* kSubstr = "?substr@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBE?AV12@II@Z";
    const char* kErase = "?erase@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@II@Z";
    const char* kFreeze = "?_Freeze@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXXZ";
    const char* kDtor = "??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ";
    const char* kEq = "??8std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBD@Z";
    const char* kRfind = "?rfind@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z";
    const uint32_t s1 = process.memory().alloc(16), s2 = process.memory().alloc(16);
    method(s1, kCtorChars, {text("hello world"), 0});
    auto chars = [&](uint32_t s) { return cpu.read32(s + 4) ? cpu.string(cpu.read32(s + 4)) : std::string(); };
    CHECK(chars(s1) == "hello world" && cpu.read32(s1 + 8) == 11 && cpu.read32(s1 + 12) >= 11 &&
          cpu.read8(cpu.read32(s1 + 4) - 1) == 0);
    method(s1, kAppend, {text(", again and again"), 17});
    CHECK(chars(s1) == "hello world, again and again" && cpu.read32(s1 + 8) == 28);
    CHECK(method(s1, kFind, {text("again"), 0, 5}) == 13 && method(s1, kRfind, {text("again"), 0xffffffffu, 5}) == 23);
    CHECK(method(s1, kFind, {text("absent"), 0, 6}) == 0xffffffffu);
    cpu.reg[x86::Cpu::ECX] = s1;
    CHECK(method(s1, kSubstr, {s2, 6, 5}) == s2 && chars(s2) == "world");
    CHECK(!cpu.call(fn(kEq), {s2, text("world")}, r) && (r & 0xff) == 1);
    method(s1, kErase, {5, 0xffffffffu});
    CHECK(chars(s1) == "hello" && cpu.read32(s1 + 8) == 5);
    method(s1, kFreeze, {});
    CHECK(cpu.read8(cpu.read32(s1 + 4) - 1) == 255);
    method(s1, kDtor, {});
    method(s2, kDtor, {});
    CHECK(cpu.read32(s1 + 4) == 0 && cpu.read32(s2 + 8) == 0);

    // ifstream: the ios at +0x5c, its state at +4; lines, then bytes.
    const uint32_t in = process.memory().alloc(0x5c + 0x34);
    method(in, "??0?$basic_ifstream@DU?$char_traits@D@std@@@std@@QAE@PBDH@Z", {text("C:\\Game\\Lines.txt"), 1, 1});
    const uint32_t ios = in + cpu.read32(cpu.read32(in) + 4);
    CHECK(ios == in + 0x5c && cpu.read32(ios + 4) == 0 && cpu.read32(in + 8 + 0x50) != 0);
    CHECK(cpu.read32(ios) == lib->exportAddress("??_7?$basic_ifstream@DU?$char_traits@D@std@@@std@@6B@"));
    const uint32_t buffer = process.memory().alloc(64);
    method(in, "?getline@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADHD@Z", {buffer, 64, '\n'});
    CHECK(cpu.string(buffer) == "first line" && cpu.read32(in + 4) == 11 && cpu.read32(ios + 4) == 0);
    method(in, "?read@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADH@Z", {buffer, 3});
    CHECK(cpu.string(buffer, 3) == "sec" && cpu.read32(in + 4) == 3);
    CHECK(method(in, "?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEHXZ", {}) == 'o');
    method(in, "?read@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADH@Z", {buffer, 10});
    CHECK(cpu.read32(in + 4) == 3 && cpu.read32(ios + 4) == 3); // "nd\n", then eof and fail
    method(in, "??_D?$basic_ifstream@DU?$char_traits@D@std@@@std@@QAEXXZ", {});
    const uint32_t missing = process.memory().alloc(0x5c + 0x34);
    method(missing, "??0?$basic_ifstream@DU?$char_traits@D@std@@@std@@QAE@PBDH@Z", {text("C:\\Game\\No.txt"), 1, 1});
    CHECK(cpu.read32(missing + 0x5c + 4) == 2);

    // A strstreambuf as the games build one: the base constructor, their
    // own vftable of the library's functions, then _Init (here growing as
    // it's written).
    const uint32_t sb = process.memory().alloc(0x50);
    method(sb, "??0?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAE@XZ", {});
    const uint32_t vft = process.memory().alloc(13 * 4), base = cpu.read32(sb);
    for (int i = 0; i < 13; ++i)
        cpu.write32(vft + 4 * i, cpu.read32(base + 4 * i));
    cpu.write32(vft + 4 * 1, fn("?overflow@strstreambuf@std@@MAEHH@Z"));
    cpu.write32(vft + 4 * 2, fn("?pbackfail@strstreambuf@std@@MAEHH@Z"));
    cpu.write32(vft + 4 * 4, fn("?underflow@strstreambuf@std@@MAEHXZ"));
    cpu.write32(vft + 4 * 8, fn("?seekoff@strstreambuf@std@@MAE?AV?$fpos@H@2@JW4seekdir@ios_base@2@H@Z"));
    cpu.write32(vft + 4 * 9, fn("?seekpos@strstreambuf@std@@MAE?AV?$fpos@H@2@V32@H@Z"));
    cpu.write32(sb, vft);
    method(sb, "?_Init@strstreambuf@std@@IAEXHPAD0H@Z", {0, 0, 0, 0});
    const std::string long_ = "0123456789abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    CHECK(method(sb, "?xsputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHPBDH@Z",
                 {text(long_), uint32_t(long_.size())}) == long_.size());
    const uint32_t back = process.memory().alloc(128);
    CHECK(method(sb, "?xsgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHPADH@Z", {back, 128}) == long_.size() &&
          cpu.string(back, uint32_t(long_.size())) == long_);
    const uint32_t pos = process.memory().alloc(24);
    method(sb, "?seekoff@strstreambuf@std@@MAE?AV?$fpos@H@2@JW4seekdir@ios_base@2@H@Z", {pos, 10, 0, 1});
    CHECK(cpu.read32(pos) == 10);
    CHECK(method(sb, "?xsgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHPADH@Z", {back, 3}) == 3 &&
          cpu.string(back, 3) == "abc");
    method(sb, "??1strstreambuf@std@@UAE@XZ", {});

    // cerr << "n = " << 42 << " x = " << 1.5f << endl
    const uint32_t cerr = fn("?cerr@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A");
    const uint32_t putChars = fn("??6std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@PBD@Z");
    CHECK(!cpu.call(putChars, {cerr, text("n = ")}, r) && r == cerr);
    method(cerr, "??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@H@Z", {42});
    CHECK(!cpu.call(putChars, {cerr, text(" x = ")}, r));
    const float x = 1.5f;
    uint32_t xbits;
    std::memcpy(&xbits, &x, 4);
    method(cerr, "??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@M@Z", {xbits});
    CHECK(!cpu.call(fn("?endl@std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@1@AAV21@@Z"), {cerr}, r));
    CHECK(output == "n = 42 x = 1.5\n");
}

// USER32's windows and messages, the registry and COM, called as a
// program calls them; the window procedure is a native function recording
// the messages it gets.
static void testWindowsAndRegistry() {
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    auto fn = [&](const char* dll, const char* name) { return process.provided(dll, name); };
    auto text = [&](const std::string& s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    std::vector<uint32_t> seen;
    const uint32_t proc = cpu.addNative("test!WndProc", [&](x86::Cpu& c) {
        seen.push_back(c.arg(1));
        c.reg[x86::Cpu::EAX] = c.arg(1) == 0x81 ? 1 : 0; // WM_NCCREATE goes on
        return 16u;
    });
    uint32_t r = 0;
    const uint32_t wc = process.memory().alloc(40);
    cpu.write32(wc + 4, proc);
    cpu.write32(wc + 36, text("TestClass"));
    CHECK(!cpu.call(fn("USER32.dll", "RegisterClassA"), {wc}, r) && r != 0);
    CHECK(!cpu.call(fn("USER32.dll", "CreateWindowExA"),
                    {0, text("testclass"), text("Title"), 0x00cf0000, 10, 20, 648, 507, 0, 0, 0, 0}, r) && r != 0);
    const uint32_t hwnd = r;
    CHECK((seen == std::vector<uint32_t>{0x81, 0x1}));
    CHECK(!cpu.call(fn("USER32.dll", "FindWindowA"), {text("TESTCLASS"), 0}, r) && r == hwnd);
    CHECK(!cpu.call(fn("USER32.dll", "FindWindowA"), {text("Other"), 0}, r) && r == 0);
    seen.clear();
    CHECK(!cpu.call(fn("USER32.dll", "ShowWindow"), {hwnd, 1}, r) && r == 0);
    CHECK((seen == std::vector<uint32_t>{0x18, 0x1c, 0x6, 0x7}));
    // A thick frame (4) and a caption (19) around the client area.
    const uint32_t rc = process.memory().alloc(16);
    for (uint32_t v : {0u, 4u, 8u, 12u})
        cpu.write32(rc + v, v == 8 ? 640 : v == 12 ? 480 : 0);
    CHECK(!cpu.call(fn("USER32.dll", "AdjustWindowRect"), {rc, 0x00cf0000, 0}, r) && int32_t(cpu.read32(rc)) == -4 &&
          int32_t(cpu.read32(rc + 4)) == -23 && cpu.read32(rc + 8) == 644 && cpu.read32(rc + 12) == 484);
    CHECK(!cpu.call(fn("USER32.dll", "GetClientRect"), {hwnd, rc}, r) && cpu.read32(rc + 8) == 640 &&
          cpu.read32(rc + 12) == 480);
    // Posted messages come back from PeekMessageA and go to the procedure.
    const uint32_t msg = process.memory().alloc(28);
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 0);
    process.postMessage(hwnd, 0x100, 'Q', 0); // WM_KEYDOWN
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 1 && cpu.read32(msg) == hwnd &&
          cpu.read32(msg + 4) == 0x100);
    CHECK(!cpu.call(fn("USER32.dll", "TranslateMessage"), {msg}, r) && r == 1);
    seen.clear();
    CHECK(!cpu.call(fn("USER32.dll", "DispatchMessageA"), {msg}, r));
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 1 && cpu.read32(msg + 4) == 0x102 &&
          cpu.read32(msg + 8) == 'q');
    CHECK((seen == std::vector<uint32_t>{0x100}));
    CHECK(!cpu.call(fn("USER32.dll", "PostQuitMessage"), {3}, r));
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, hwnd, 0, 0, 1}, r) && r == 1 && cpu.read32(msg + 4) == 0x12 &&
          cpu.read32(msg + 8) == 3);
    // Closing through DefWindowProcA destroys the window.
    seen.clear();
    CHECK(!cpu.call(fn("USER32.dll", "DefWindowProcA"), {hwnd, 0x10, 0, 0}, r));
    CHECK((seen == std::vector<uint32_t>{0x2, 0x82}) && process.window(hwnd) == nullptr);

    // The registry: a key made, a value set and read back; Windows' own
    // locale list enumerated.
    const uint32_t key = process.memory().alloc(4), disposition = process.memory().alloc(4);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegCreateKeyExA"),
                    {0x80000002u, text("Software\\Test\\Game"), 0, 0, 0, 0, 0, key, disposition}, r) && r == 0 &&
          cpu.read32(disposition) == 1);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegSetValueExA"), {cpu.read32(key), text("Path"), 0, 1, text("D:\\"), 4}, r) &&
          r == 0);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegCloseKey"), {cpu.read32(key)}, r) && r == 0);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegOpenKeyExA"), {0x80000002u, text("SOFTWARE\\test"), 0, 1, key}, r) && r == 0);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegOpenKeyExA"), {0x80000002u, text("Software\\Absent"), 0, 1, key}, r) &&
          r == 2);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegOpenKeyExA"), {0x80000002u, text("Software\\Test\\Game"), 0, 1, key}, r) &&
          r == 0);
    const uint32_t type = process.memory().alloc(4), data = process.memory().alloc(16), size = process.memory().alloc(4);
    cpu.write32(size, 2);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegQueryValueExA"), {cpu.read32(key), text("path"), 0, type, data, size}, r) &&
          r == 234 && cpu.read32(size) == 4);
    cpu.write32(size, 16);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegQueryValueExA"), {cpu.read32(key), text("path"), 0, type, data, size}, r) &&
          r == 0 && cpu.read32(type) == 1 && cpu.string(data) == "D:\\");
    CHECK(process.registryValue("HKEY_LOCAL_MACHINE\\Software\\Test\\Game", "Path").has_value());
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegOpenKeyExA"),
                    {0x80000002u, text("System\\CurrentControlSet\\Control\\Nls\\Locale"), 0, 1, key}, r) && r == 0);
    const uint32_t name = process.memory().alloc(32), nameSize = process.memory().alloc(4);
    cpu.write32(nameSize, 32);
    cpu.write32(size, 16);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegEnumValueA"), {cpu.read32(key), 1, name, nameSize, 0, type, data, size}, r) &&
          r == 0 && cpu.string(name) == "00000409" && cpu.read32(nameSize) == 8 && cpu.string(data) == "1");
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegEnumValueA"), {cpu.read32(key), 2, name, nameSize, 0, type, data, size}, r) &&
          r == 259);

    // COM: no class without the engine's; with it, the class asked for.
    const uint32_t guids = process.memory().alloc(32), out = process.memory().alloc(4);
    const uint8_t clsid[16] = {0xe0, 0xee, 0xf1, 0xd8, 0x34, 0xf6, 0xcf, 0x11, 0x87, 0, 0, 0xa0, 0x24, 0x5d, 0x91, 0x8b};
    for (uint32_t i = 0; i < 16; ++i)
        cpu.write8(guids + i, clsid[i]);
    CHECK(!cpu.call(fn("ole32.dll", "CoCreateInstance"), {guids, 0, 1, guids + 16, out}, r) && r == 0x80040154u);
    std::string asked;
    process.createComObject = [&](const std::string& c, const std::string&, uint32_t) {
        asked = c;
        return 0u;
    };
    CHECK(!cpu.call(fn("ole32.dll", "CoCreateInstance"), {guids, 0, 1, guids + 16, out}, r) && r == 0 &&
          asked == "{d8f1eee0-f634-11cf-8700-00a0245d918b}");
}

// DirectDraw and Direct3D 7 called through their vtables as a program
// calls them: a 64x48 fullscreen mode with a back buffer and Z buffer, a
// clear, a Gouraud triangle, a textured quad, the depth test, a flip.
static void testDirectX() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    x86::DirectX dx(process);
    dx.modes.push_back({64, 48, 16});
    uint32_t r = 0;
    auto mem = [&](uint32_t size) { return process.memory().alloc(size); };
    auto guid = [&](const char* g) {
        const uint32_t a = mem(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    };
    // Method `index` of the interface at `self`.
    auto call = [&](uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        const bool ok = !cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r);
        CHECK(ok);
        return r;
    };
    auto bits = [](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    const uint32_t out = mem(4);
    CHECK(!cpu.call(process.provided("DDRAW.dll", "DirectDrawCreateEx"),
                    {0, out, guid("{15e65ec0-3b9c-11d2-b92f-00609797ea5b}"), 0}, r) && r == 0);
    const uint32_t dd = cpu.read32(out);
    CHECK(call(dd, 20, {0, 0x11}) == 0);                // SetCooperativeLevel: exclusive, fullscreen
    CHECK(call(dd, 21, {64, 48, 16, 0, 0}) == 0);       // SetDisplayMode
    CHECK(call(dd, 21, {65, 48, 16, 0, 0}) != 0);       // a mode not offered
    // A flipping primary with one back buffer.
    const uint32_t desc = mem(124);
    auto clearDesc = [&] {
        std::memset(cpu.memory().data() + desc, 0, 124);
        cpu.write32(desc, 124);
    };
    clearDesc();
    cpu.write32(desc + 4, 0x1 | 0x20);                       // CAPS, BACKBUFFERCOUNT
    cpu.write32(desc + 20, 1);
    cpu.write32(desc + 104, 0x200 | 0x10 | 0x8 | 0x2000);    // PRIMARY, FLIP, COMPLEX, 3DDEVICE
    CHECK(call(dd, 6, {desc, out, 0}) == 0);                 // CreateSurface
    const uint32_t primary = cpu.read32(out);
    const uint32_t caps = mem(16);
    cpu.write32(caps, 0x4); // BACKBUFFER
    CHECK(call(primary, 12, {caps, out}) == 0);              // GetAttachedSurface
    const uint32_t back = cpu.read32(out);
    // A 16-bit Z buffer, attached.
    clearDesc();
    cpu.write32(desc + 4, 0x1 | 0x2 | 0x4 | 0x40);           // CAPS, HEIGHT, WIDTH, ZBUFFERBITDEPTH
    cpu.write32(desc + 8, 48);
    cpu.write32(desc + 12, 64);
    cpu.write32(desc + 104, 0x20000);                        // ZBUFFER
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    CHECK(call(back, 3, {cpu.read32(out)}) == 0);            // AddAttachedSurface
    // Direct3D, from the DirectDraw object, and a device on the back buffer.
    CHECK(call(dd, 0, {guid("{f5049e77-4861-11d2-a407-00a0c90629a8}"), out}) == 0);
    const uint32_t d3d = cpu.read32(out);
    cpu.fpu.control = 0x027f; // 53 bits, as Windows starts a thread
    CHECK(call(d3d, 4, {guid("{84e63de0-46aa-11cf-816f-0000c020156e}"), back, out}) == 0); // CreateDevice
    const uint32_t device = cpu.read32(out);
    CHECK(cpu.fpu.control == 0x007f); // Direct3D leaves it in single precision

    // Clear to blue, then a red-green-blue triangle (screen coordinates).
    CHECK(call(device, 10, {0, 0, 3, 0xff0000ffu, bits(1.0f), 0}) == 0); // Clear target and Z
    CHECK(call(device, 20, {22, 1}) == 0);                               // CULLMODE: none
    const uint32_t verts = mem(4 * 28);
    auto vertex = [&](uint32_t i, float x, float y, float z, uint32_t color, float u = 0, float v = 0) {
        const uint32_t p = verts + i * 28; // XYZRHW, DIFFUSE, TEX1
        cpu.write32(p, bits(x));
        cpu.write32(p + 4, bits(y));
        cpu.write32(p + 8, bits(z));
        cpu.write32(p + 12, bits(1.0f));
        cpu.write32(p + 16, color);
        cpu.write32(p + 20, bits(u));
        cpu.write32(p + 24, bits(v));
    };
    const uint32_t fvf = 0x4 | 0x40 | 0x100;
    vertex(0, 0, 0, 0.5f, 0xffff0000u);
    vertex(1, 32, 0, 0.5f, 0xffff0000u);
    vertex(2, 0, 32, 0.5f, 0xffff0000u);
    CHECK(call(device, 37, {0, 1, 2}) == 0); // stage 0 COLOROP: SELECTARG1
    CHECK(call(device, 37, {0, 2, 0}) == 0); // COLORARG1: diffuse
    CHECK(call(device, 25, {4, fvf, verts, 3, 0}) == 0); // DrawPrimitive: a triangle list
    // A farther green triangle over it is hidden by the depth test.
    vertex(0, 0, 0, 0.9f, 0xff00ff00u);
    vertex(1, 32, 0, 0.9f, 0xff00ff00u);
    vertex(2, 0, 32, 0.9f, 0xff00ff00u);
    CHECK(call(device, 25, {4, fvf, verts, 3, 0}) == 0);
    // A textured quad (2x2 texels, point sampled) at the bottom right.
    clearDesc();
    cpu.write32(desc + 4, 0x1 | 0x2 | 0x4 | 0x1000);
    cpu.write32(desc + 8, 2);
    cpu.write32(desc + 12, 2);
    cpu.write32(desc + 72, 32);
    cpu.write32(desc + 76, 0x40);    // RGB
    cpu.write32(desc + 84, 16);
    cpu.write32(desc + 88, 0xf800);
    cpu.write32(desc + 92, 0x07e0);
    cpu.write32(desc + 96, 0x001f);
    cpu.write32(desc + 104, 0x1000); // TEXTURE
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    const uint32_t texture = cpu.read32(out);
    CHECK(call(texture, 25, {0, desc, 1, 0}) == 0); // Lock
    const uint32_t texels = cpu.read32(desc + 36), pitch = cpu.read32(desc + 16);
    cpu.write16(texels, 0xffff);              // white
    cpu.write16(texels + 2, 0xf800);          // red
    cpu.write16(texels + pitch, 0x07e0);      // green
    cpu.write16(texels + pitch + 2, 0x0000);  // black
    CHECK(call(texture, 32, {0}) == 0);       // Unlock
    CHECK(call(device, 35, {0, texture}) == 0);  // SetTexture
    CHECK(call(device, 37, {0, 1, 4}) == 0);     // COLOROP: MODULATE
    CHECK(call(device, 37, {0, 2, 2}) == 0);     // COLORARG1: texture
    CHECK(call(device, 37, {0, 3, 0}) == 0);     // COLORARG2: diffuse
    vertex(0, 40, 24, 0.5f, 0xffffffffu, 0, 0);
    vertex(1, 64, 24, 0.5f, 0xffffffffu, 1, 0);
    vertex(2, 40, 48, 0.5f, 0xffffffffu, 0, 1);
    vertex(3, 64, 48, 0.5f, 0xffffffffu, 1, 1);
    CHECK(call(device, 25, {5, fvf, verts, 4, 0}) == 0); // a strip
    CHECK(call(primary, 11, {0, 1}) == 0); // Flip

    const x86::DirectX::Frame& f = dx.frame();
    CHECK(f.width == 64 && f.height == 48 && f.number == 1);
    auto at = [&](int x, int y) { return f.pixels[size_t(y * f.width + x)] & 0xffffff; };
    CHECK(at(5, 5) == 0xff0000);  // the near triangle
    CHECK(at(40, 5) == 0x0000ff); // the clear colour
    CHECK(at(31, 31) == 0x0000ff); // past the hypotenuse
    CHECK(at(45, 28) == 0xffffff && at(58, 28) == 0xff0000 && at(45, 44) == 0x00ff00 && at(58, 44) == 0x000000);
    // The display's gamma ramp (IDirectDrawGammaControl, from the primary):
    // red turned upside down, shown from then on.
    CHECK(call(primary, 0, {guid("{69c11c3e-b46b-11d1-ad7a-00c04fc29b4e}"), out}) == 0);
    const uint32_t gamma = cpu.read32(out);
    const uint32_t ramp = mem(1536);
    for (uint32_t i = 0; i < 768; ++i)
        cpu.write16(ramp + 2 * i, uint16_t((i < 256 ? 255 - i : i % 256) * 257));
    CHECK(call(gamma, 4, {0, ramp}) == 0); // SetGammaRamp
    CHECK(call(primary, 11, {0, 1}) == 0 && call(primary, 11, {0, 1}) == 0); // the drawing shown again
    CHECK(at(5, 5) == 0x000000 && at(40, 5) == 0xff00ff);
    const uint32_t read = mem(1536);
    CHECK(call(gamma, 3, {0, read}) == 0 && cpu.read16(read) == 0xffff && cpu.read16(read + 512) == 0); // GetGammaRamp
    for (uint32_t i = 0; i < 768; ++i)
        cpu.write16(ramp + 2 * i, uint16_t((i % 256) * 257));
    CHECK(call(gamma, 4, {0, ramp}) == 0);
    call(gamma, 2, {}); // Release: the primary keeps its other references
    // Shown a flip late: a flip shows the frame before, its drawing done;
    // the frame just flipped shows at the next.
    dx.presentLate = true;
    const uint64_t shown = f.number;
    CHECK(call(device, 10, {0, 0, 1, 0xff00ff00u, bits(1.0f), 0}) == 0); // Clear to green
    CHECK(call(primary, 11, {0, 1}) == 0);
    CHECK(f.number == shown); // the first late flip has nothing before it to show
    CHECK(call(device, 10, {0, 0, 1, 0xffffff00u, bits(1.0f), 0}) == 0); // yellow
    CHECK(call(primary, 11, {0, 1}) == 0);
    CHECK(f.number == shown + 1 && at(40, 5) == 0x00ff00); // the green one
    CHECK(call(primary, 11, {0, 1}) == 0);
    CHECK(f.number == shown + 2 && at(40, 5) == 0xffff00);
    dx.presentLate = false;
    // Drawing by rows on several threads draws the same as on one:
    // overlapping triangles, depth tested, bilinear, blended.
    CHECK(call(device, 37, {0, 16, 2}) == 0); // MAGFILTER: linear
    CHECK(call(device, 20, {27, 1}) == 0);    // ALPHABLENDENABLE
    CHECK(call(device, 20, {19, 5}) == 0);    // SRCBLEND: SRCALPHA
    CHECK(call(device, 20, {20, 6}) == 0);    // DESTBLEND: INVSRCALPHA
    auto scene = [&](x86::DirectX::DrawThreads threads) {
        dx.drawThreads = threads;
        CHECK(call(device, 10, {0, 0, 3, 0xff202020u, bits(1.0f), 0}) == 0);
        uint32_t seed = 7;
        auto next = [&] { return (seed = seed * 1103515245u + 12345u) >> 8; };
        for (int t = 0; t < 30; ++t) {
            for (uint32_t i = 0; i < 3; ++i)
                vertex(i, float(next() % 900) / 10 - 13, float(next() % 700) / 10 - 11, float(next() % 1000) / 1000,
                       next() | 0x40000000u, float(next() % 300) / 100, float(next() % 300) / 100);
            CHECK(call(device, 25, {4, fvf, verts, 3, 0}) == 0);
        }
        CHECK(call(back, 25, {0, desc, 1, 0}) == 0); // Lock
        const uint32_t pixels = cpu.read32(desc + 36), rowBytes = cpu.read32(desc + 16);
        std::vector<uint8_t> image(cpu.memory().begin() + pixels, cpu.memory().begin() + pixels + rowBytes * 48);
        CHECK(call(back, 32, {0}) == 0);
        return image;
    };
    const std::vector<uint8_t> one = scene(x86::DirectX::DrawThreads::One);
    CHECK(scene(x86::DirectX::DrawThreads::Always) == one);
    CHECK(std::adjacent_find(one.begin(), one.end(), std::not_equal_to<>()) != one.end()); // something was drawn
    // Release the device and the DirectDraw object's.
    CHECK(call(device, 2, {}) == 0);
}

// Input as the engine passes it on: window messages (with a double click
// for a class that asks for them), key states, the pointer kept within
// ClipCursor's rectangle; and DirectInput's mouse and keyboard, read as a
// state and as a buffer.
static void testInput() {
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    x86::DirectInput directInput(process);
    uint32_t now = 1000;
    process.milliseconds = [&] { return now; };
    auto fn = [&](const char* dll, const char* name) { return process.provided(dll, name); };
    auto text = [&](const std::string& s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    uint32_t r = 0;
    const uint32_t proc = cpu.addNative("test!InputProc", [](x86::Cpu& c) {
        c.reg[x86::Cpu::EAX] = c.arg(1) == 0x81 ? 1 : 0;
        return 16u;
    });
    const uint32_t wc = process.memory().alloc(40);
    cpu.write32(wc, 0x8); // CS_DBLCLKS
    cpu.write32(wc + 4, proc);
    cpu.write32(wc + 36, text("Game"));
    CHECK(!cpu.call(fn("USER32.dll", "RegisterClassA"), {wc}, r) && r != 0);
    // A popup window (no frame) at 100,50.
    CHECK(!cpu.call(fn("USER32.dll", "CreateWindowExA"), {0, text("Game"), 0, 0x80000000u, 100, 50, 320, 200, 0, 0, 0, 0},
                    r) && r != 0);
    const uint32_t hwnd = r;
    const uint32_t msg = process.memory().alloc(28);
    auto next = [&]() -> std::array<uint32_t, 4> {
        if (cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) || !r)
            return {0, 0, 0, 0};
        return {cpu.read32(msg), cpu.read32(msg + 4), cpu.read32(msg + 8), cpu.read32(msg + 12)};
    };
    process.mouseMoveTo(130, 70);
    CHECK((next() == std::array<uint32_t, 4>{hwnd, 0x200, 0, 30 | 20u << 16}));
    process.mouseButton(0, true);
    CHECK((next() == std::array<uint32_t, 4>{hwnd, 0x201, 1, 30 | 20u << 16}));
    process.mouseButton(0, false);
    CHECK(next()[1] == 0x202);
    now += 200;
    process.mouseButton(0, true); // soon after, in the same place: a double click
    CHECK(next()[1] == 0x203);
    process.mouseButton(0, false);
    now += 1000;
    process.mouseButton(0, true); // too late for one
    CHECK(next()[1] == 0x202 && next()[1] == 0x201);
    process.mouseButton(0, false);
    next();
    process.key(0x41, 0x1e, true); // A
    auto k = next();
    CHECK(k[1] == 0x100 && k[2] == 0x41 && k[3] == (1 | 0x1eu << 16));
    CHECK(!cpu.call(fn("USER32.dll", "GetKeyState"), {0x41}, r) && (r & 0x8000));
    process.key(0x41, 0x1e, false);
    k = next();
    CHECK(k[1] == 0x101 && k[3] == (1 | 0x1eu << 16 | 3u << 30));
    CHECK(!cpu.call(fn("USER32.dll", "GetKeyState"), {0x41}, r) && !(r & 0x8000));
    // ClipCursor keeps the pointer in.
    const uint32_t rc = process.memory().alloc(16);
    for (uint32_t i = 0; i < 4; ++i)
        cpu.write32(rc + 4 * i, i == 0 ? 100 : i == 1 ? 50 : i == 2 ? 420 : 250);
    CHECK(!cpu.call(fn("USER32.dll", "ClipCursor"), {rc}, r) && r == 1);
    process.mouseMoveTo(1000, 0);
    CHECK(process.cursorX == 419 && process.cursorY == 50);
    CHECK(!cpu.call(fn("USER32.dll", "ClipCursor"), {0}, r));
    process.mouseMoveTo(130, 70);
    while (next()[1]) {
    }

    // DirectInput.
    auto guid = [&](const char* g) {
        const uint32_t a = process.memory().alloc(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    };
    auto call = [&](uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        const bool ok = !cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r);
        CHECK(ok);
        return r;
    };
    const uint32_t out = process.memory().alloc(4);
    CHECK(!cpu.call(fn("DINPUT.dll", "DirectInputCreateA"), {0, 0x700, out, 0}, r) && r == 0);
    const uint32_t di = cpu.read32(out);
    CHECK(call(di, 3, {guid("{6f1d2b60-d5a0-11cf-bfc7-444553540000}"), out, 0}) == 0); // CreateDevice: the mouse
    const uint32_t mouse = cpu.read32(out);
    // A mouse format as c_dfDIMouse is: X, Y, Z (LONGs), four buttons (BYTEs).
    const uint32_t objects = process.memory().alloc(7 * 16), format = process.memory().alloc(24);
    const uint32_t axisGuids[3] = {guid("{a36d02e0-c9f3-11cf-bfc7-444553540000}"),
                                   guid("{a36d02e1-c9f3-11cf-bfc7-444553540000}"),
                                   guid("{a36d02e2-c9f3-11cf-bfc7-444553540000}")};
    for (uint32_t i = 0; i < 7; ++i) {
        const uint32_t o = objects + 16 * i;
        cpu.write32(o, i < 3 ? axisGuids[i] : 0);
        cpu.write32(o + 4, i < 3 ? 4 * i : 12 + (i - 3));
        cpu.write32(o + 8, i < 3 ? 0x3 | 0xffff00u : 0xc | 0xffff00u | (i > 4 ? 0x80000000u : 0));
    }
    const uint32_t fmt[6] = {24, 16, 2 /* relative axes */, 16, 7, objects};
    for (uint32_t i = 0; i < 6; ++i)
        cpu.write32(format + 4 * i, fmt[i]);
    CHECK(call(mouse, 11, {format}) == 0);     // SetDataFormat
    const uint32_t state = process.memory().alloc(256);
    CHECK(call(mouse, 9, {16, state}) == 0x8007000cu); // GetDeviceState before Acquire
    const uint32_t prop = process.memory().alloc(20);
    const uint32_t bufferSize[5] = {20, 16, 0, 0, 8};
    for (uint32_t i = 0; i < 5; ++i)
        cpu.write32(prop + 4 * i, bufferSize[i]);
    CHECK(call(mouse, 6, {1, prop}) == 0);      // SetProperty: DIPROP_BUFFERSIZE
    CHECK(call(mouse, 7, {}) == 0);             // Acquire
    process.mouseMoveTo(140, 65);
    process.mouseButton(1, true);
    CHECK(call(mouse, 9, {16, state}) == 0);
    CHECK(int32_t(cpu.read32(state)) == 10 && int32_t(cpu.read32(state + 4)) == -5 && cpu.read32(state + 8) == 0 &&
          cpu.read8(state + 12) == 0 && cpu.read8(state + 13) == 0x80);
    CHECK(call(mouse, 9, {16, state}) == 0 && cpu.read32(state) == 0); // the movement was read
    // The buffer: X moved, Y moved, button 1 pressed.
    const uint32_t data = process.memory().alloc(8 * 16), count = process.memory().alloc(4);
    cpu.write32(count, 8);
    CHECK(call(mouse, 10, {16, data, count, 0}) == 0 && cpu.read32(count) == 3);
    CHECK(cpu.read32(data) == 0 && cpu.read32(data + 4) == 10 && cpu.read32(data + 16) == 4 &&
          int32_t(cpu.read32(data + 20)) == -5 && cpu.read32(data + 32) == 13 && cpu.read32(data + 36) == 0x80);
    process.mouseButton(1, false);

    // The keyboard, with a format of 256 keys by scan code.
    CHECK(call(di, 3, {guid("{6f1d2b61-d5a0-11cf-bfc7-444553540000}"), out, 0}) == 0);
    const uint32_t keyboard = cpu.read32(out);
    const uint32_t keys = process.memory().alloc(256 * 16), keyGuid = guid("{55728220-d33c-11cf-bfc7-444553540000}");
    for (uint32_t i = 0; i < 256; ++i) {
        cpu.write32(keys + 16 * i, keyGuid);
        cpu.write32(keys + 16 * i + 4, i);
        cpu.write32(keys + 16 * i + 8, 0x8000000cu | i << 8);
    }
    const uint32_t keyFmt[6] = {24, 16, 0, 256, 256, keys};
    for (uint32_t i = 0; i < 6; ++i)
        cpu.write32(format + 4 * i, keyFmt[i]);
    CHECK(call(keyboard, 11, {format}) == 0);
    CHECK(call(keyboard, 7, {}) == 0);
    process.key(0x41, 0x1e, true);
    CHECK(call(keyboard, 9, {256, state}) == 0 && cpu.read8(state + 0x1e) == 0x80 && cpu.read8(state + 0x1f) == 0);
    process.key(0x41, 0x1e, false);
    CHECK(call(keyboard, 9, {256, state}) == 0 && cpu.read8(state + 0x1e) == 0);
}

// A3D created as a program creates it, a source made, filled through Lock,
// played once and looped, as the clock moves.
static void testA3d() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    uint32_t clock = 0;
    process.milliseconds = [&] { return clock; };
    x86::A3d a3d(process);
    audio::Mixer mixer(22050);
    a3d.mixer = &mixer;
    uint32_t r = 0;
    auto mem = [&](uint32_t size) { return process.memory().alloc(size); };
    auto guid = [&](const char* g) {
        const uint32_t a = mem(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    };
    auto call = [&](uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        CHECK(!cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r));
        return r;
    };
    const uint32_t out = mem(4), out2 = mem(4), out3 = mem(4), out4 = mem(4);
    CHECK(!cpu.call(process.provided("ole32.dll", "CoCreateInstance"),
                    {guid("{92fa2c24-253c-11d2-90fb-006008a1f441}"), 0, 1, guid("{9ae07221-8ac4-11d3-b6aa-00600879f3ee}"),
                     out},
                    r) && r == 0);
    const uint32_t api = cpu.read32(out);
    CHECK(call(api, 15, {0, 0, 0}) == 0); // Init
    CHECK(call(api, 0, {guid("{4730c6c2-b797-11d3-a33d-00104b67d10e}"), out}) == 0 && cpu.read32(out)); // the geometry
    CHECK(call(api, 0, {guid("{c398e563-d90b-11d1-90fb-006008a1f441}"), out}) == 0);                   // the listener
    CHECK(cpu.read32(out) != 0);
    CHECK(call(api, 17, {0x1, out}) == 0); // NewSource, native (not positioned)
    const uint32_t source = cpu.read32(out);
    CHECK(a3d.sourcesMade() == 1);
    // A second of 1000 Hz mono 8-bit: 1000 bytes.
    const uint32_t format = mem(18);
    std::memset(cpu.memory().data() + format, 0, 18);
    cpu.write16(format, 1);
    cpu.write16(format + 2, 1);
    cpu.write32(format + 4, 1000);
    cpu.write32(format + 8, 1000);
    cpu.write16(format + 12, 1);
    cpu.write16(format + 14, 8);
    CHECK(call(source, 7, {format}) == 0);         // SetWaveFormat
    CHECK(call(source, 5, {1000}) == 0);           // AllocateWaveData
    CHECK(call(source, 9, {}) == 1000);            // GetWaveSize
    CHECK(call(source, 11, {900, 200, out, out2, out3, out4, 0}) == 0); // Lock, wrapping round
    CHECK(cpu.read32(out2) == 100 && cpu.read32(out4) == 100 && cpu.read32(out3) + 900 == cpu.read32(out));
    for (uint32_t i = 0; i < 100; ++i)
        cpu.write8(cpu.read32(out) + i, 0xc0), cpu.write8(cpu.read32(out3) + i, 0xc0);
    CHECK(call(source, 12, {cpu.read32(out), 100, cpu.read32(out3), 100}) == 0); // Unlock
    // Played once: it moves with the clock, and stops at its end.
    CHECK(call(source, 13, {0}) == 0 && a3d.sourcesPlaying() == 1);
    clock = 250;
    CHECK(call(source, 19, {out}) == 0 && cpu.read32(out) == 250); // GetWavePosition
    CHECK(call(source, 56, {out}) == 0 && cpu.read32(out) == 0x1); // GetStatus: playing
    std::vector<int16_t> mixed(2 * 300);
    mixer.mix(mixed.data(), 300); // 250 bytes at 1000 Hz: about 5500 frames, past its prebuffer
    clock = 1500;
    CHECK(call(source, 56, {out}) == 0 && cpu.read32(out) == 0 && a3d.sourcesPlaying() == 0);
    // The first 100 bytes, heard: 0xc0 is +0x40 << 8.
    mixed.assign(2 * 22050, 0);
    mixer.mix(mixed.data(), 22050);
    CHECK(*std::max_element(mixed.begin(), mixed.end()) == 0x4000);
    // A wave event at the middle: not set until play passes it, then set
    // once (auto-reset).
    uint32_t event = 0;
    const auto wait = process.provided("KERNEL32.dll", "WaitForSingleObject");
    CHECK(!cpu.call(process.provided("KERNEL32.dll", "CreateEventA"), {0, 0, 0, 0}, event) && event);
    CHECK(!cpu.call(wait, {event, 0}, r) && r == 0); // untracked: reads as set
    CHECK(call(source, 59, {500, event}) == 0);    // SetWaveEvent
    CHECK(!cpu.call(wait, {event, 0}, r) && r == 0x102);
    // Looped: it wraps round, and stops where it's stopped.
    CHECK(call(source, 13, {1}) == 0);
    clock = 2000;
    CHECK(call(source, 56, {out}) == 0 && !cpu.call(wait, {event, 0}, r) && r == 0x102);
    clock = 2600;
    CHECK(call(source, 56, {out}) == 0 && !cpu.call(wait, {event, 0}, r) && r == 0);
    CHECK(!cpu.call(wait, {event, 0}, r) && r == 0x102);
    CHECK(call(source, 60, {}) == 0); // ClearWaveEvents
    clock = 3750;
    CHECK(call(source, 19, {out}) == 0 && cpu.read32(out) == 250);
    CHECK(call(source, 56, {out}) == 0 && cpu.read32(out) == (0x1 | 0x4));
    CHECK(call(source, 14, {}) == 0);
    clock = 5000;
    CHECK(call(source, 19, {out}) == 0 && cpu.read32(out) == 250 && a3d.sourcesPlaying() == 0);
    CHECK(call(source, 2, {}) == 0); // Release

    // A 3D source: no format until it has a wave (NiA3dSound asks, to know
    // whether to load its file), then heard from where it is. The listener
    // stays where A3D starts it: at the origin, facing -z, y up, so +x is
    // to its right.
    auto bits = [](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    CHECK(call(api, 17, {0x0, out}) == 0); // NewSource, 3D
    const uint32_t positioned = cpu.read32(out);
    CHECK(call(positioned, 8, {mem(18)}) != 0);        // GetWaveFormat: none yet
    CHECK(call(positioned, 7, {format}) == 0);         // SetWaveFormat
    CHECK(call(positioned, 5, {1000}) == 0);           // AllocateWaveData
    CHECK(call(positioned, 11, {0, 1000, out, out2, out3, out4, 0}) == 0);
    std::memset(cpu.memory().data() + cpu.read32(out), 0xc0, 1000);
    CHECK(call(positioned, 12, {cpu.read32(out), 1000, 0, 0}) == 0);
    CHECK(call(positioned, 38, {bits(1), bits(100), 1}) == 0); // SetMinMaxDistance: muted beyond 100
    auto heard = [&](float x) { // the loudest left and right while it plays at (x, 0, 0)
        CHECK(call(positioned, 20, {bits(x), 0, 0}) == 0); // SetPosition3f
        CHECK(call(positioned, 15, {}) == 0);              // Rewind
        CHECK(call(positioned, 13, {0}) == 0);             // Play
        clock += 600;
        CHECK(call(positioned, 56, {out}) == 0);
        std::vector<int16_t> both(2 * 22050);
        mixer.mix(both.data(), 22050);
        int left = 0, right = 0;
        for (size_t i = 0; i < both.size(); i += 2)
            left = std::max(left, std::abs(int(both[i]))), right = std::max(right, std::abs(int(both[i + 1])));
        clock += 600;
        CHECK(call(positioned, 56, {out}) == 0 && cpu.read32(out) == 0); // played to its end
        return std::pair{left, right};
    };
    const auto beyond = heard(200);
    CHECK(beyond.first == 0 && beyond.second == 0);
    const auto onTheRight = heard(10);
    CHECK(onTheRight.second > onTheRight.first && onTheRight.second > 0);
    CHECK(onTheRight.second < 0x4000); // fainter for its distance
    CHECK(call(positioned, 2, {}) == 0);
}

// Bits written as Indeo 5 reads them: a field's lowest bit first, each
// byte filled from its lowest bit.
struct LeBits {
    std::vector<uint8_t> bytes;
    size_t pos = 0;
    void put(uint32_t v, int n) {
        for (int i = 0; i < n; ++i, ++pos) {
            if (pos / 8 >= bytes.size())
                bytes.push_back(0);
            bytes[pos / 8] |= uint8_t(((v >> i) & 1) << (pos % 8));
        }
    }
    void align() { pos = (pos + 7) & ~size_t(7); }
    void pad() { // up to the next byte, as bytes
        align();
        bytes.resize(pos / 8);
    }
};

// A 16x16 key frame by docs/formats/video/indeo5.md, every block uncoded,
// so a flat grey picture: one luma band of 16x16 macroblocks of 8x8
// blocks, chroma bands of 4x4.
std::vector<uint8_t> indeo5GreyFrame(int type) {
    LeBits b;
    b.put(0x1f, 5);
    b.put(uint32_t(type), 3);
    b.put(0, 8); // frame number
    if (type == 0) {
        b.put(0, 8); // GOP flags
        b.put(0, 2); // one luma band
        b.put(0, 1); // one chroma band
        b.put(15, 4); // size given:
        b.put(16, 13); // height
        b.put(16, 13); // width
        b.put(0, 1); // luma: full-pel, macroblocks twice the blocks, 8x8
        b.put(0, 1);
        b.put(0, 1);
        b.put(0, 1);
        b.put(0, 2);
        b.put(0, 1); // chroma: full-pel, macroblocks of one block, 4x4
        b.put(1, 1);
        b.put(1, 1);
        b.put(0, 1);
        b.put(0, 2);
        b.align();
        b.put(0, 23);
        b.put(0, 1); // no extension
        b.align();
    }
    if (type == 4) {
        b.pad();
        return b.bytes;
    }
    b.put(0, 8); // frame flags
    b.put(0, 3);
    b.align();
    for (int band = 0; band < 3; ++band) {
        b.put(0, 8); // band flags: defaults
        b.put(0, 1); // no checksum
        b.put(0, 5); // quantiser
        b.align();
        b.put(0, 1); // the tile isn't empty
        b.put(1, 1); // its size follows
        b.put(3, 8);
        b.align();
        b.put(0, 1); // the macroblock isn't empty
        if (type != 0)
            b.put(0, 1); // intra
        b.put(0, band ? 1 : 4); // no blocks coded
        b.align();
    }
    b.pad();
    return b.bytes;
}

void testVideo() {
    using namespace reluva::video;
    // A key frame: grey; a null frame keeps it.
    Indeo5Decoder dec;
    std::vector<uint8_t> frame = indeo5GreyFrame(0);
    CHECK(!dec.decode(frame.data(), frame.size()));
    CHECK(dec.width() == 16 && dec.height() == 16);
    std::vector<uint8_t> y, cb, cr;
    dec.yuv(y, cb, cr);
    CHECK(y.size() == 256 && cb.size() == 16 && cr.size() == 16);
    CHECK(std::all_of(y.begin(), y.end(), [](uint8_t v) { return v == 128; }));
    CHECK(std::all_of(cb.begin(), cb.end(), [](uint8_t v) { return v == 128; }));
    std::vector<uint8_t> rgba(16 * 16 * 4);
    dec.rgba(rgba.data(), 16 * 4);
    CHECK(rgba[0] == 130 && rgba[1] == 130 && rgba[2] == 130 && rgba[3] == 255);
    frame = indeo5GreyFrame(4);
    CHECK(!dec.decode(frame.data(), frame.size()));
    frame = indeo5GreyFrame(1); // an inter frame of intra macroblocks
    CHECK(!dec.decode(frame.data(), frame.size()));
    CHECK(!dec.decode(nullptr, 0)); // a dropped frame
    // Damage: no start code; an inter frame before any key frame.
    frame[0] ^= 1;
    CHECK(dec.decode(frame.data(), frame.size()));
    Indeo5Decoder fresh;
    frame = indeo5GreyFrame(1);
    CHECK(fresh.decode(frame.data(), frame.size()));

    // An AVI: a video stream (IV50) and a PCM one, their chunks in a
    // "rec " list inside "movi".
    std::vector<uint8_t> a;
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            a.push_back(uint8_t(v >> (8 * i)));
    };
    auto id = [&](const char* s) { a.insert(a.end(), s, s + 4); };
    std::vector<size_t> sizes;
    auto open = [&](const char* list, const char* type) {
        id(list);
        sizes.push_back(a.size());
        u32(0);
        if (type)
            id(type);
    };
    auto close = [&] {
        const size_t at = sizes.back();
        sizes.pop_back();
        const uint32_t n = uint32_t(a.size() - at - 4);
        for (int i = 0; i < 4; ++i)
            a[at + size_t(i)] = uint8_t(n >> (8 * i));
        if (n & 1)
            a.push_back(0);
    };
    open("RIFF", "AVI ");
    open("LIST", "hdrl");
    open("LIST", "strl");
    open("strh", nullptr);
    id("vids");
    id("IV50");
    for (int i = 0; i < 3; ++i)
        u32(0);
    u32(1);  // scale
    u32(15); // rate
    for (int i = 0; i < 9; ++i)
        u32(0);
    close();
    open("strf", nullptr);
    u32(40);
    u32(16);
    u32(16);
    for (int i = 0; i < 7; ++i)
        u32(0);
    close();
    close();
    open("LIST", "strl");
    open("strh", nullptr);
    id("auds");
    for (int i = 0; i < 13; ++i)
        u32(0);
    close();
    open("strf", nullptr);
    for (uint32_t v : {0x00020001u, 22050u, 88200u, 0x00100004u})
        u32(v);
    close();
    close();
    close(); // hdrl
    open("LIST", "movi");
    open("LIST", "rec ");
    const std::vector<uint8_t> key = indeo5GreyFrame(0);
    open("00dc", nullptr);
    a.insert(a.end(), key.begin(), key.end());
    close();
    open("01wb", nullptr);
    for (int i = 0; i < 4; ++i)
        a.insert(a.end(), {uint8_t(i), 0});
    close();
    close();
    open("00dc", nullptr); // a dropped frame
    close();
    close(); // movi
    close(); // RIFF
    AviFile avi;
    CHECK(!avi.parse(a));
    CHECK(avi.width == 16 && avi.height == 16 && avi.framesPerSecond == 15 && avi.videoCodec == fourcc("IV50"));
    CHECK(avi.frames.size() == 2 && avi.frames[0].size == key.size() && avi.frames[1].size == 0);
    CHECK(avi.audioFormat == 1 && avi.audioChannels == 2 && avi.audioRate == 22050 && avi.audioBits == 16);
    const auto sound = avi.sound(a);
    CHECK(sound && sound->frames() == 2 && sound->samples[3] == 3);
    Indeo5Decoder fromAvi;
    CHECK(!fromAvi.decode(a.data() + avi.frames[0].offset, avi.frames[0].size) && fromAvi.width() == 16);
    a[0] = 'X';
    CHECK(avi.parse(a));
}

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "reluva_tests";
    testMd5();
    testUtf16();
    testFont();
    testWav();
    testMp3();
    testMixer();
    testMixerStream();
    testFsAndDetection(tmp / "fs");
    testConfig(tmp / "cfg");
    testZip(tmp / "zip");
    testSearchSet(tmp / "search");
    testImage();
    testNet();
    testMd4();
    testGfxMath();
    testX86Instructions(true);
    testX86Instructions(false);
    testX86Dll(true);
    testX86Dll(false);
    testX86CacheAgreement();
    testThreads();
    testMsvcrtAndFiles(tmp / "msvcrt");
    testMsvcp60(tmp / "msvcp60");
    testWindowsAndRegistry();
    testDirectX();
    testInput();
    testA3d();
    testVideo();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
