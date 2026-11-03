// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/presentation.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <algorithm>
using namespace reluva;
using namespace reluva::boggle;
namespace {
bool silent(const std::vector<int16_t>& pcm) { return std::all_of(pcm.begin(),pcm.end(),[](auto n){return n==0;}); }
void sounds(const std::filesystem::path& tmp) {
    fixtures::presentationInstall(tmp / "install"); Resources r; CHECK(!r.open(tmp / "install"));
    fixtures::ScriptedSystem sys; sys.audioSupported=true;
    {
        ClassicAudio audio(sys); CHECK(!audio.open(r,true)); CHECK(sys.audioStarted);
        audio.recorded(6); CHECK(sys.pump(32)[0]==4000);
        audio.recorded(7); CHECK(sys.pump(32)[0]==5000);
        audio.recorded(8); CHECK(sys.pump(32)[0]==6000);
        audio.recorded(12); CHECK(sys.pump(32)[0]==6000);
        audio.recorded(3); audio.recorded(3); CHECK(sys.pump(32)[0]==4000);
        audio.beginRound(); CHECK(!silent(sys.pump(100))); audio.stopShake(); CHECK(silent(sys.pump(32)));
        audio.countdown(11000); CHECK(silent(sys.pump(32)));
        audio.countdown(10000); CHECK(sys.pump(32)[0]==7000);
        audio.countdown(10999); CHECK(silent(sys.pump(32)));
        audio.countdown(9000); CHECK(sys.pump(32)[0]==7000);
        audio.countdown(1000); CHECK(sys.pump(32)[0]==7000);
        audio.countdown(999); CHECK(sys.pump(32)[0]==8000);
        audio.countdown(0); CHECK(silent(sys.pump(32)));
        audio.beginRound(false); auto pcm=sys.pump(100); CHECK(pcm[0]==1000 && pcm.back()==0);
        audio.countdown(0); CHECK(sys.pump(32)[0]==8000);
    }
    CHECK(sys.audioStopped && !sys.audioCallback);
    fixtures::ScriptedSystem unavailable;
    { ClassicAudio audio(unavailable); CHECK(!audio.open(r,true)); audio.recorded(3); CHECK(silent(unavailable.pump(32))); }
    CHECK(!unavailable.audioStarted);
    Resources empty; fixtures::ScriptedSystem muted;
    { ClassicAudio audio(muted); CHECK(!audio.open(empty,false)); }
    CHECK(!muted.audioStarted && !muted.audioStopped);
    fixtures::write(tmp / "install/Waves/Writing.wav", "malformed");
    { ClassicAudio audio(muted); CHECK(bool(audio.open(r,true))); }
    CHECK(!muted.audioStarted);
}
void transitions() {
    ClassicTransition t; CHECK(t.type()==Transition::None); CHECK(!t.finish(1000));
    t.begin(Transition::Opening,1000); CHECK(t.frame(999)==0 && t.frame(1099)==0 && t.frame(1100)==1);
    CHECK(t.frame(5399)==43 && !t.finish(5399)); CHECK(t.finish(5400) && t.type()==Transition::None);
    t.begin(Transition::Closing,6000); CHECK(t.frame(5999)==44 && t.frame(6099)==44);
    CHECK(t.frame(7099)==54 && !t.finish(7099)); CHECK(t.finish(7100));
    t.begin(Transition::Opening,0); CHECK(t.frame(UINT64_MAX)==43); CHECK(t.finish(UINT64_MAX));
}
}
static const test::Suite suite("boggle.unit.presentation", {
    {"cue selection, countdown deduplication, looping and audio lifetime",[](const auto& tmp){sounds(tmp);}},
    {"opening/closing frame bounds and transition deadlines",[](const auto&){transitions();}},
});
