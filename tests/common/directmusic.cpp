// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectMusic as Windows plays it: RIFF files, a DLS collection's
// instruments and their synthesizer, and a segment's style played through
// a performance.
//
//   reluva-tests common.unit.directmusic

#include "audio/directmusic.h"
#include "audio/dls.h"
#include "audio/synth.h"
#include "common/riff.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace reluva;

namespace {

// A RIFF chunk: id, little-endian size, bytes, padded to even.
std::vector<uint8_t> riffChunk(const std::string &id, const std::vector<uint8_t> &body) {
    std::vector<uint8_t> out(id.begin(), id.end());
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(body.size() >> (8 * i)));
    out.insert(out.end(), body.begin(), body.end());
    if (body.size() & 1)
        out.push_back(0);
    return out;
}
std::vector<uint8_t> riffList(const std::string &id, const std::string &type,
                              const std::vector<std::vector<uint8_t>> &children) {
    std::vector<uint8_t> body(type.begin(), type.end());
    for (const auto &c : children)
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
    for (const auto &p : parts)
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
    // (The chunks point into the bytes, which are kept while they're read.)
    RiffChunk top;
    const std::vector<uint8_t> riff =
        riffList("RIFF", "TEST", {riffChunk("abc ", {1, 2, 3}), riffList("LIST", "kids", {riffChunk("x   ", {7})})});
    CHECK(!readRiff(riff, top));
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
    const DlsRegion &r = c.instruments[0].regions.at(0);
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

std::vector<uint8_t> utf16(const std::string &text) {
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
    std::map<std::string, const std::vector<uint8_t> *> files = {
        {"test.sgt", &segment}, {"test.sty", &style}, {"test.dls", &dls}};
    DmPerformance perf(22050, [&](const std::string &name, std::vector<uint8_t> &out) {
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
        perf.render(out.data() + 2 * at, int(std::min<size_t>(256, out.size() / 2 - at)));
    auto level = [&](float from, float to) {
        double sum = 0;
        const size_t a = size_t(from * 22050);
        const size_t b = size_t(to * 22050);
        for (size_t i = a; i < b; ++i)
            sum += double(out[2 * i]) * out[2 * i];
        return std::sqrt(sum / double(b - a));
    };
    for (float beat = 0; beat < 3.9f; beat += 0.5f)
        CHECK(level(beat, beat + 0.05f) > 0.01 && level(beat + 0.25f, beat + 0.45f) < 0.001);
    // Four measures, then it's over.
    CHECK(level(8.3f, 9) < 0.0001 && !perf.playing());
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.directmusic",
                                        {
                                            {"dls", [](const std::filesystem::path &) { testDls(); }},
                                            {"performance", [](const std::filesystem::path &) { testDirectMusic(); }},
                                        });
