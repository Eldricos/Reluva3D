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
        audio.countdown(9000,0);CHECK(sys.pump(32)[0]==7000);
        audio.countdown(9000,1);CHECK(sys.pump(32)[0]==7000);
        audio.countdown(9000,0);CHECK(silent(sys.pump(32)));
        audio.countdown(9000,1);CHECK(silent(sys.pump(32)));
        audio.countdown(0,1);CHECK(sys.pump(32)[0]==8000);
        audio.countdown(0,1);CHECK(silent(sys.pump(32)));
        audio.countdown(0,4);CHECK(silent(sys.pump(32)));
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
void music(const std::filesystem::path& tmp) {
    // Invented stereo-output expectations from a two-sample mono waveform.
    for (size_t i=0;i<classicMusicFiles.size();++i) {
        auto data = fixtures::wave();
        test::fixtures::put16(data,44,uint16_t(100+i));
        test::fixtures::put16(data,46,uint16_t(900+i));
        fixtures::write(tmp / "Waves" / (std::string(classicMusicFiles[i])+".wav"),data);
    }
    Resources r; CHECK(!r.open(tmp));
    for (int i=0;i<int(classicMusicFiles.size());++i) {
        fixtures::ScriptedSystem sys; sys.audioSupported=true;
        {
            ClassicAudio audio(sys); CHECK(!audio.open(r,false,i));
            CHECK(silent(sys.pump(3))); // Ready screen does not start playback.
            audio.beginRound(); CHECK(sys.pump(1)[0]==100+i);
            audio.recorded(7); audio.countdown(0); // FX disabled, music remains.
            audio.beginRound(); // Replay must preserve the music cursor.
            const auto next = sys.pump(1)[0]; CHECK(next > 100+i);
            CHECK(!silent(sys.pump(1000))); // loops beyond the original two samples
        }
        CHECK(sys.audioStopped && !sys.audioCallback);
    }
    fixtures::ScriptedSystem a,b; a.audioSupported=b.audioSupported=true;
    { ClassicAudio first(a),second(b);
      CHECK(!first.open(r,false,musicRandom,123)); CHECK(!second.open(r,false,musicRandom,123));
      first.beginRound(); second.beginRound(); CHECK(a.pump(200)==b.pump(200)); }
    fixtures::ScriptedSystem sys;
    { ClassicAudio audio(sys); CHECK(bool(audio.open(r,false,15))); }
    fixtures::write(tmp / "Waves/Clasical.wav","broken");
    { ClassicAudio audio(sys); CHECK(bool(audio.open(r,false,0))); }
    { ClassicAudio audio(sys); CHECK(!audio.open(r,false)); }
    CHECK(!sys.audioStarted);
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
    {"music catalog, independent mute, looping and replay continuity",[](const auto& tmp){music(tmp);}},
    {"opening/closing frame bounds and transition deadlines",[](const auto&){transitions();}},
});
