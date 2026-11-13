// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The mixer's additions for sound made at the output's rate: sounds
// queued one after another (SoundQueue), a sound's loop and rate, and an
// OutputStream.
//
//   reluva-tests common.unit.mixer

#include "audio/mixer.h"
#include "audio/sound.h"
#include "tests/support/test.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

using namespace reluva;

namespace {

void testSoundQueue() {
    using namespace reluva::audio;
    auto mono = [](int rate, std::vector<int16_t> samples) {
        auto s = std::make_shared<Sound>();
        s->rate = rate;
        s->channels = 1;
        s->samples = std::move(samples);
        return s;
    };
    SoundQueue q;
    q.push(mono(22050, {100, 200, 300}), 7);
    q.push(mono(22050, {400, 500}), 8);
    CHECK(q.playing() == 7 && q.queued() == 2);
    int16_t out[12];
    q.read(out, 6, 22050);
    const int16_t expected[] = {100, 200, 300, 400, 500, 0};
    for (int i = 0; i < 6; ++i)
        CHECK(out[2 * i] == expected[i] && out[2 * i + 1] == expected[i]);
    CHECK(q.playing() == -1 && q.queued() == 0);

    // At half the output rate: each frame twice, halfway between them.
    SoundQueue half;
    half.push(mono(11025, {0, 100}), 1);
    half.push(mono(11025, {200}), 2);
    half.read(out, 3, 22050);
    CHECK(out[0] == 0 && out[2] == 50 && out[4] == 100 && half.playing() == 1);
    half.read(out, 3, 22050);
    CHECK(out[0] == 150 && out[2] == 200 && out[4] == 200 && half.playing() == 2);
}

std::shared_ptr<audio::Sound> mono(int rate, std::vector<int16_t> samples) {
    auto s = std::make_shared<audio::Sound>();
    s->rate = rate;
    s->channels = 1;
    s->samples = std::move(samples);
    return s;
}

// A loop: frames 1 and 2 over and over, after the first; then a sound at
// twice its rate.
void testLoopAndRate() {
    using namespace reluva::audio;
    Mixer m(22050);
    std::vector<int16_t> out(40);
    auto looped = mono(22050, {10, 20, 30, 40});
    looped->loopStart = 1, looped->loopEnd = 3;
    const SoundHandle l = m.play(looped, SoundType::Effects, kMaxVolume, 0, true);
    m.mix(out.data(), 6);
    CHECK(out[0] == 10 && out[2] == 20 && out[4] == 30 && out[6] == 20 && out[8] == 30 && out[10] == 20);
    m.stop(l);
    // Twice the rate: every other frame.
    const SoundHandle r = m.play(mono(22050, {0, 10, 20, 30, 40, 50}), SoundType::Effects);
    m.setRate(r, 2.0);
    m.mix(out.data(), 4);
    CHECK(out[0] == 0 && out[2] == 20 && out[4] == 40 && out[6] == 0 && !m.isPlaying(r));
}

// An OutputStream is asked for frames at the output rate, and scaled by its volumes.
void testOutputStream() {
    using namespace reluva::audio;
    Mixer m(22050);
    std::vector<int16_t> out(40);
    struct Ramp : OutputStream {
        int16_t next = 0, rate = 0;
        void read(int16_t* o, int frames, int r) override {
            rate = int16_t(r);
            for (int i = 0; i < frames; ++i, next += 100)
                o[2 * i] = o[2 * i + 1] = next;
        }
    };
    auto ramp = std::make_shared<Ramp>();
    const SoundHandle st = m.playOutput(ramp, SoundType::Effects, kMaxVolume / 2);
    m.mix(out.data(), 3);
    CHECK(ramp->rate == 22050 && out[0] == 0 && out[2] == 50 && out[5] == 100 && m.isPlaying(st));
    m.stop(st);
    m.mix(out.data(), 1);
    CHECK(!m.isPlaying(st) && out[0] == 0 && m.playOutput(nullptr, SoundType::Effects) == kNoSound);
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.mixer", {
    {"sound queue", [](const std::filesystem::path&) { testSoundQueue(); }},
    {"loop and rate", [](const std::filesystem::path&) { testLoopAndRate(); }},
    {"output stream", [](const std::filesystem::path&) { testOutputStream(); }},
});
