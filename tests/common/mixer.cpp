// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The mixer's additions for sound made at the output's rate: sounds
// queued one after another (SoundQueue), a sound's loop and rate, and an
// OutputStream; and its filters and reverb.
//
//   reluva-tests common.unit.mixer

#include "audio/mixer.h"
#include "audio/sound.h"
#include "tests/support/test.h"

#include <cstdint>
#include <cstdlib>
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
    const SoundHandle l = m.play(looped, SoundType::kEffects, kMaxVolume, 0, true);
    m.mix(out.data(), 6);
    CHECK(out[0] == 10 && out[2] == 20 && out[4] == 30 && out[6] == 20 && out[8] == 30 && out[10] == 20);
    m.stop(l);
    // Twice the rate: every other frame.
    const SoundHandle r = m.play(mono(22050, {0, 10, 20, 30, 40, 50}), SoundType::kEffects);
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
        int16_t next = 0;
        int16_t rate = 0;
        void read(int16_t *o, int frames, int r) override {
            rate = int16_t(r);
            for (int i = 0; i < frames; ++i, next += 100)
                o[2 * i] = o[2 * i + 1] = next;
        }
    };
    auto ramp = std::make_shared<Ramp>();
    const SoundHandle st = m.playOutput(ramp, SoundType::kEffects, kMaxVolume / 2);
    m.mix(out.data(), 3);
    CHECK(ramp->rate == 22050 && out[0] == 0 && out[2] == 50 && out[5] == 100 && m.isPlaying(st));
    m.stop(st);
    m.mix(out.data(), 1);
    CHECK(!m.isPlaying(st) && out[0] == 0 && m.playOutput(nullptr, SoundType::kEffects) == kNoSound);
}

// Seeking, muffling, the Butterworth filters and the reverb (with its
// tail), as the Unreal engine uses them for Miles' sample filters.
void testFilters() {
    using namespace reluva::audio;
    auto make = [](int rate, int channels, std::vector<int16_t> samples) {
        auto s = std::make_shared<Sound>();
        s->rate = rate;
        s->channels = channels;
        s->samples = std::move(samples);
        return s;
    };
    Mixer m(22050);
    std::vector<int16_t> out(8);
    // Rate 2 plays every other frame; seek starts it further in.
    const SoundHandle p = m.play(make(22050, 1, {0, 100, 200, 300, 400, 500}), SoundType::kEffects);
    m.setRate(p, 2);
    m.seek(p, 1);
    m.mix(out.data(), 2);
    CHECK(out[0] == 100 && out[2] == 300 && m.position(p) == 5.0);
    m.stopAll();
    // Muffled: a tone at the top of the band (alternating samples) all but
    // goes; a steady level passes once the filter settles.
    std::vector<int16_t> high(2000);
    std::vector<int16_t> steady(2000, 1000);
    for (size_t k = 0; k < high.size(); ++k)
        high[k] = k % 2 ? -1000 : 1000;
    std::vector<int16_t> big(4000);
    const SoundHandle h = m.play(make(22050, 1, high), SoundType::kEffects);
    m.setMuffle(h, 1);
    m.mix(big.data(), 2000);
    CHECK(std::abs(big[3000]) < 200 && std::abs(big[3002]) < 200); // a seventh, at 22 kHz
    const SoundHandle st = m.play(make(22050, 1, steady), SoundType::kEffects);
    m.setMuffle(st, 1);
    m.mix(big.data(), 2000);
    CHECK(std::abs(big[3000] - 1000) < 5);
    m.stopAll();
    // The Butterworth filters: a low-pass at 1 kHz takes the top of the
    // band away, a high-pass at 600 Hz a steady level; the other way
    // round each passes it.
    const SoundHandle lp = m.play(make(22050, 1, high), SoundType::kEffects);
    m.setFilter(lp, Mixer::Filter::kLowPass, 1000);
    m.mix(big.data(), 2000);
    CHECK(std::abs(big[3000]) < 5 && std::abs(big[3002]) < 5);
    const SoundHandle hp = m.play(make(22050, 1, steady), SoundType::kEffects);
    m.setFilter(hp, Mixer::Filter::kHighPass, 600);
    m.mix(big.data(), 2000);
    CHECK(std::abs(big[3000]) < 5);
    const SoundHandle lp2 = m.play(make(22050, 1, steady), SoundType::kEffects);
    m.setFilter(lp2, Mixer::Filter::kLowPass, 1000);
    m.mix(big.data(), 2000);
    CHECK(std::abs(big[3000] - 1000) < 5);
    m.stopAll();
    // The reverb: a click, then its echoes from the first comb (29.7 ms,
    // 655 frames) on; dry before, and the dry part weighed by 1 - mix.
    std::vector<int16_t> click(3000, 0);
    click[0] = 10000;
    const SoundHandle rv = m.play(make(22050, 1, click), SoundType::kEffects);
    m.setReverb(rv, {0.5f, 1.5f, 5000, 0});
    std::vector<int16_t> tail(6000);
    m.mix(tail.data(), 3000);
    CHECK(tail[0] == 5000 && tail[2 * 300] == 0);
    int echoes = 0;
    for (size_t k = 2 * 640; k < tail.size(); k += 2)
        echoes += tail[k] != 0;
    CHECK(echoes > 100 && tail[2 * 655] != tail[2 * 655 + 1]); // left and right differ
    m.stopAll();
    // Its tail: once a short sound has ended, the reverb goes on through
    // silence for the tail's time, then the sound is done.
    std::vector<int16_t> blip(10, 0);
    blip[0] = 10000;
    const SoundHandle tb = m.play(make(22050, 1, blip), SoundType::kEffects);
    m.setReverb(tb, {0.5f, 1.5f, 5000, 0, 0.1f});
    m.mix(tail.data(), 1000);
    CHECK(m.isPlaying(tb) && tail[2 * 655] != 0);
    m.mix(tail.data(), 2000);
    CHECK(!m.isPlaying(tb));
}

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.mixer", {
                                    {"sound queue", [](const std::filesystem::path &) { testSoundQueue(); }},
                                    {"loop and rate", [](const std::filesystem::path &) { testLoopAndRate(); }},
                                    {"output stream", [](const std::filesystem::path &) { testOutputStream(); }},
                                    {"filters", [](const std::filesystem::path &) { testFilters(); }},
                                });
