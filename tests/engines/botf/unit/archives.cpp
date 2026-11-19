// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Birth of the Federation's unit tests: alt.res and its LZSS, the saved
// games, the .snd archives, and finding the game's files.

#include "audio/sound.h"
#include "audio/wav.h"
#include "engines/botf/formats/alt_res.h"
#include "engines/botf/formats/save_game.h"
#include "engines/botf/formats/snd_archive.h"
#include "engines/botf/game_files.h"
#include "image/image.h"

#include "tests/engines/botf/unit/helpers.h"
#include "tests/support/test.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::botf;
using namespace reluva::botf::test;

namespace {

// A small LZSS encoder in alt.res's format, writing the window from
// position 1 as its tool does: runs of a byte copied from 1 byte back,
// repeats of the last 2 bytes (a pixel) from 2 back, and literals for the
// rest. A decoder starting its window anywhere else reads the wrong bytes.
Bytes compress(const Bytes &raw) {
    std::vector<Bytes> items;
    size_t r = 1;
    for (size_t i = 0; i < raw.size();) {
        size_t len = 0;
        size_t back = 0;
        for (size_t d : {size_t(1), size_t(2)}) {
            size_t n = 0;
            if (i >= d)
                while (i + n < raw.size() && n < 17 && raw[i + n] == raw[i - d + n % d])
                    ++n;
            if (n > len)
                len = n, back = d;
        }
        if (len >= 2) {
            const size_t from = (r + 4096 - back) % 4096;
            items.push_back({uint8_t((len - 2) << 4 | from >> 8), uint8_t(from & 0xff)});
            i += len;
            r = (r + len) % 4096;
        } else {
            items.push_back({raw[i]});
            ++i;
            r = (r + 1) % 4096;
        }
    }
    Bytes out;
    for (size_t k = 0; k < items.size(); k += 8) {
        uint8_t flags = 0;
        for (size_t j = 0; j < 8 && k + j < items.size(); ++j)
            if (items[k + j].size() == 1)
                flags |= uint8_t(1 << j);
        out.push_back(flags);
        for (size_t j = 0; j < 8 && k + j < items.size(); ++j)
            out.insert(out.end(), items[k + j].begin(), items[k + j].end());
    }
    return out;
}

// A small 16-bit TGA: runs of colours that change every few pixels, and
// some black, so copies read from the wrong place give the wrong bytes.
Bytes makeTga(int w, int h, uint8_t seed) {
    Bytes t = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    put16(t, uint32_t(w));
    put16(t, uint32_t(h));
    t.push_back(16);
    t.push_back(1);
    for (int i = 0; i < w * h; ++i) {
        const int run = i / (3 + (i / 50) % 9);
        put16(t, run % 5 == 0 ? 0 : uint32_t((run * 2654435761u + seed) >> 17) & 0x7fff);
    }
    return t;
}

void testLzss() {
    // The start of the retail alt.res's first file: a flag byte, literals
    // and two references that fill the TGA header's zeros; the decoded
    // header (800x600, 16-bit) is how the format was worked out.
    const Bytes start = {0xe7, 0x00, 0x00, 0x02, 0x00, 0x01, 0x50, 0x05, 0x20,
                         0x03, 0x58, 0x07, 0x02, 0x10, 0x01, 0x70, 0x04};
    Bytes out;
    size_t used = 0;
    CHECK(!decompressLzss(start.data(), start.size(), 18, out, &used));
    const Bytes header = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0x03, 0x58, 0x02, 0x10, 0x01};
    CHECK(out == header);
    CHECK(used == 15);

    // Round trip through the encoder above.
    const Bytes raw = makeTga(40, 30, 3);
    const Bytes packed = compress(raw);
    CHECK(packed.size() < raw.size());
    CHECK(!decompressLzss(packed.data(), packed.size(), raw.size(), out, &used));
    CHECK(out == raw);
    CHECK(used == packed.size());

    // Too little input fails.
    CHECK(decompressLzss(packed.data(), packed.size() / 2, raw.size(), out));

    // A reference to the window before anything is written there copies
    // what it was filled with: zeros, or a saved game's spaces.
    const Bytes early = {0xfe, 0x10, 0x80};
    CHECK(!decompressLzss(early.data(), early.size(), 3, out) && out == Bytes(3, 0));
    CHECK(!decompressLzss(early.data(), early.size(), 3, out, nullptr, 0x20) && out == Bytes(3, 0x20));
}

void testSaveSummary(const std::filesystem::path &tmp) {
    writeFile(tmp / "game0.sav", makeSave("Tutorial #1", "Irregular galaxy \xe9", 41));
    SaveSummary save;
    CHECK(!readSaveSummary(tmp / "game0.sav", save));
    CHECK(save.name == "Tutorial #1");
    CHECK(save.description == "Irregular galaxy \xc3\xa9"); // Windows-1252 to UTF-8
    CHECK(save.turn == 41 && !save.multiplayer);
    // Without gameInfo, or not an archive, it isn't read.
    writeFile(tmp / "game1.sav", makeLzssArchive("x", {{"version", Bytes(8, 1)}}));
    CHECK(bool(readSaveSummary(tmp / "game1.sav", save)));
    writeFile(tmp / "game2.sav", Bytes(10, 0));
    CHECK(bool(readSaveSummary(tmp / "game2.sav", save)));
}

void testAltRes(const std::filesystem::path &tmp) {
    // Two files, stored last first as in the retail alt.res.
    const Bytes a = makeTga(16, 8, 1);
    const Bytes b = makeTga(20, 10, 2);
    const Bytes pa = compress(a);
    const Bytes pb = compress(b);
    Bytes file(24, 0);
    std::memcpy(file.data(), "alt.res", 7);
    const uint32_t offB = 24;
    const uint32_t offA = uint32_t(24 + pb.size());
    file.insert(file.end(), pb.begin(), pb.end());
    file.insert(file.end(), pa.begin(), pa.end());
    set32(file, 16, uint32_t(file.size()));
    set32(file, 20, 2);
    auto entry = [&](const char *name, uint32_t off, uint32_t index, const Bytes &raw, const Bytes &packed) {
        Bytes e(16, 0);
        std::memcpy(e.data(), name, std::strlen(name));
        put32(e, off);
        put32(e, index);
        put32(e, uint32_t(raw.size()));
        put32(e, uint32_t(packed.size()));
        put32(e, 1);
        put32(e, 0x1234);
        file.insert(file.end(), e.begin(), e.end());
    };
    entry("a.tga", offA, 0, a, pa);
    entry("b.tga", offB, 1, b, pb);
    file.resize(file.size() + 100, 0); // padding, as at the retail file's end
    writeFile(tmp / "alt.res", file);

    AltResArchive alt;
    CHECK(!alt.open(tmp / "alt.res"));
    CHECK(alt.entries().size() == 2);
    CHECK(alt.list() == std::vector<std::string>({"a.tga", "b.tga"}));
    CHECK(alt.has("A.TGA") && !alt.has("c.tga"));
    CHECK(alt.entries()[1].offset == offB && alt.entries()[1].check == 0x1234);
    Bytes out;
    CHECK(!alt.read("a.tga", out) && out == a);
    CHECK(!alt.read("B.tga", out) && out == b);
    CHECK(alt.read("c.tga", out));

    // A directory that doesn't fit is refused.
    set32(file, 20, 1000);
    writeFile(tmp / "bad.res", file);
    CHECK(alt.open(tmp / "bad.res"));
}

// One mono IMA ADPCM block of `size` bytes.
Bytes imaBlock(size_t size, int16_t first, uint8_t seed) {
    Bytes b;
    put16(b, uint16_t(first));
    b.push_back(10);
    b.push_back(0);
    for (size_t i = 4; i < size; ++i)
        b.push_back(uint8_t(seed + i * 37));
    return b;
}

Bytes wavFile(uint16_t format, int channels, int rate, int bits, int blockAlign, const Bytes &data) {
    Bytes w = {'R', 'I', 'F', 'F'};
    put32(w, uint32_t(36 + data.size()));
    w.insert(w.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(w, 16);
    put16(w, format);
    put16(w, uint32_t(channels));
    put32(w, uint32_t(rate));
    put32(w, uint32_t(rate * channels * 2));
    put16(w, uint32_t(blockAlign));
    put16(w, uint32_t(bits));
    w.insert(w.end(), {'d', 'a', 't', 'a'});
    put32(w, uint32_t(data.size()));
    w.insert(w.end(), data.begin(), data.end());
    return w;
}

void testSnd(const std::filesystem::path &tmp) {
    // sfx.snd: WAV files, then the directory.
    Bytes pcm;
    for (int i = 0; i < 100; ++i)
        put16(pcm, uint32_t(i * 50));
    const Bytes wav = wavFile(1, 1, 22050, 16, 2, pcm);
    Bytes sfx(4, 0);
    sfx.insert(sfx.end(), wav.begin(), wav.end());
    sfx.insert(sfx.end(), wav.begin(), wav.end());
    set32(sfx, 0, uint32_t(sfx.size()));
    for (uint32_t i = 0; i < 2; ++i) {
        put32(sfx, uint32_t(4 + i * wav.size()));
        put32(sfx, uint32_t(wav.size()));
        put32(sfx, 0x0040ca9a);
        put32(sfx, i * 2);
    }
    writeFile(tmp / "sfx.snd", sfx);
    SndArchive snd;
    CHECK(!snd.open(tmp / "sfx.snd", false));
    CHECK(snd.entries().size() == 2 && snd.entries()[1].flags == 2 && !snd.isMusic());
    audio::Sound s;
    CHECK(!snd.decode(1, 11025, s));
    CHECK(s.rate == 22050 && s.frames() == 100 && s.format == 1 && s.samples[3] == 150);
    CHECK(snd.decode(2, 22050, s));

    // english.snd: bare mono ADPCM, its length from the decoded size.
    const Bytes clip = imaBlock(512, 7, 3);
    Bytes speech(4, 0);
    speech.insert(speech.end(), clip.begin(), clip.end());
    set32(speech, 0, uint32_t(speech.size()));
    put32(speech, 4);
    put32(speech, uint32_t(clip.size()));
    put32(speech, 44 + 2 * 1000);
    put32(speech, 0);
    writeFile(tmp / "english.snd", speech);
    CHECK(!snd.open(tmp / "english.snd", false));
    CHECK(!snd.decode(0, 22050, s));
    CHECK(s.format == 0x11 && s.channels == 1 && s.rate == 22050 && s.frames() == 1000 && s.samples[0] == 7);

    // music.snd: 16 lists after the segments; two pieces of stereo blocks.
    Bytes music(128, 0);
    Bytes stereo = imaBlock(1024, 5, 4);
    stereo[4] = 9; // the right channel's header: sample 9, index 0
    stereo[5] = 0;
    stereo[6] = 0;
    stereo[7] = 0;
    const uint32_t segA = uint32_t(music.size());
    music.insert(music.end(), stereo.begin(), stereo.end());
    const uint32_t segB = uint32_t(music.size());
    music.insert(music.end(), stereo.begin(), stereo.end());
    const uint32_t lists = uint32_t(music.size());
    auto segment = [&](uint32_t off, uint32_t frames) {
        put32(music, off);
        put32(music, uint32_t(stereo.size()));
        put32(music, 44 + 4 * frames);
        put32(music, 3);
    };
    segment(segA, 1017); // piece 0: two segments
    segment(segB, 500);
    segment(segB, 1017); // piece 1: one
    set32(music, 0, 2);
    set32(music, 4, lists);
    set32(music, 8, 1);
    set32(music, 12, lists + 32);
    for (int p = 2; p < 16; ++p)
        set32(music, size_t(p) * 8 + 4, uint32_t(music.size())); // empty lists
    writeFile(tmp / "music.snd", music);
    CHECK(!snd.open(tmp / "music.snd", true));
    CHECK(snd.isMusic() && snd.entries().size() == 3 && snd.pieces().size() == 16);
    CHECK(snd.pieces()[0] == std::vector<size_t>({0, 1}) && snd.pieces()[1] == std::vector<size_t>({2}));
    CHECK(snd.pieces()[2].empty() && snd.entries()[2].piece == 1);
    CHECK(!snd.decodePiece(0, 22050, s));
    CHECK(s.channels == 2 && s.frames() == 1517 && s.samples[0] == 5 && s.samples[1] == 9);
    CHECK(s.samples[1017 * 2] == 5); // the second segment starts afresh
    CHECK(snd.decodePiece(16, 22050, s));
}

void testGameFiles(const std::filesystem::path &tmp) {
    const auto ini = parseStbofIni("CDPATH=D:\\\r\nHDPATH=C:\\botf\\\r\nlanguage = German \r\nINSTALL=MINIMUM\r\n"
                                   "DIRECT X NOT 6.0 INSTALLED BY BOTF\r\n");
    CHECK(ini.at("CDPATH") == "D:\\" && ini.at("LANGUAGE") == "German" && ini.at("INSTALL") == "MINIMUM");
    CHECK(ini.size() == 4);

    // A game folder with the CD's language folder copied in, and a CD
    // folder named by Stbof.ini.
    const auto game = tmp / "game";
    const auto cd = tmp / "cd";
    writeFile(game / "STBOF.RES", {1});
    writeFile(game / "German" / "german.snd", {2});
    writeFile(cd / "MUSIC.SND", {3});
    const std::string text = "CDPATH=" + cd.generic_string() + "\r\nLANGUAGE=GERMAN\r\n";
    writeFile(game / "Stbof.ini", Bytes(text.begin(), text.end()));
    GameFiles files(game);
    CHECK(files.language() == "GERMAN" && files.speechFile() == "german.snd");
    CHECK(files.cdDir().has_value());
    CHECK(files.find("stbof.res") == game / "STBOF.RES");
    CHECK(files.find("german.snd") == game / "German" / "german.snd");
    CHECK(files.find("music.snd") == cd / "MUSIC.SND");
    CHECK(!files.find("alt.res"));

    // Without Stbof.ini: English, no CD.
    GameFiles plain(tmp / "cd");
    CHECK(plain.language() == "ENGLISH" && plain.speechFile() == "english.snd" && !plain.cdDir());
    CHECK(plain.find("music.snd").has_value());
}

} // namespace

static const reluva::test::Suite
    kSuite("botf.unit.archives", {
                                     {"lzss", [](const std::filesystem::path &) { testLzss(); }},
                                     {"alt.res", [](const std::filesystem::path &tmp) { testAltRes(tmp); }},
                                     {"saved games", [](const std::filesystem::path &tmp) { testSaveSummary(tmp); }},
                                     {"snd", [](const std::filesystem::path &tmp) { testSnd(tmp); }},
                                     {"game files", [](const std::filesystem::path &tmp) { testGameFiles(tmp); }},
                                 });
