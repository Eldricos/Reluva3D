// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/appearance.h"
#include "engines/boggle/frontend/media.h"
#include "engines/boggle/frontend/roster_dialog.h"
#include "engines/boggle/frontend/options_dialog.h"
#include "tests/engines/boggle/dialog_fixture.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <algorithm>
#include <limits>
using namespace reluva;using namespace reluva::boggle;
namespace {
void controls(){
    CHECK(!ui::active());ui::Appearance appearance;appearance.enable(true);
    {ui::Scope scope(appearance);CHECK(ui::active());CHECK(ui::dx(200)==300 && ui::dy(123)==200);CHECK(ui::dx(200,800)==375);
        {ui::Appearance fallback;ui::Scope nested(fallback);CHECK(!ui::active() && ui::dx(200)==400);}CHECK(ui::active());
        Surface s;s.create(640,480,PixelFormat::RGBA32);ui::fill(s,{-5,-5,10,10},{1,2,3});CHECK(s.row(0)[0]==1 && s.row(5)[0]==0);
        const auto before=s.pixels;ui::fill(s,{std::numeric_limits<int>::max(),0,100,100},{4,5,6});CHECK(before==s.pixels);
        const SelectionRect b{20,20,140,30};ui::button(s,b,"Choice",true);const auto idle=s.pixels;
        appearance.event(fixtures::mouseEvent(EventType::MouseDown,25,25));ui::button(s,b,"Choice",true);CHECK(idle!=s.pixels);
        Event lost;lost.type=EventType::FocusLost;appearance.event(lost);ui::button(s,b,"Choice",true);CHECK(idle==s.pixels);
        ui::button(s,b,"Choice",true,false);CHECK(idle!=s.pixels);
        ui::fill(s,b,{17,18,19});ui::glassButton(s,b,"");CHECK(s.row(30)[40*4]==17); // transparent interior
        CHECK(ui::caption(u"&One && Two")=="One & Two");CHECK(ui::caption(std::u16string(2049,u'A')).empty());
        const auto width=ui::measure("Alice");CHECK(width>0);CHECK(ui::position("Alice",10000,b,6,5)==5);CHECK(ui::position("Alice",-100,b,6,5)==0);
        ui::field(s,b,"Long proportional input",0,4,23,6);ui::wrapped(s,{200,20,80,100},"An independently authored wrapping check",{255,255,255});
        for(bool extended:{false,true}){
            FrontendVersionLayout version;CHECK(!version.read({{PeResourceName{5,false,{}},PeResourceName{10002,false,{}},0x0409,0,fixtures::versionDialog(extended).bytes}}));
            CHECK(version.installed() && version.label(0)=="Own choice" && version.caption()=="Own choice");
            FrontendRosterDialog roster;CHECK(!roster.read({{PeResourceName{5,false,{}},PeResourceName{10000,false,{}},0x0409,0,fixtures::rosterDialog(extended).bytes}}));CHECK(roster.installed());
            FrontendOptionsDialog options;CHECK(!options.read({{PeResourceName{5,false,{}},PeResourceName{10834,false,{}},0x0409,0,fixtures::optionsDialog(extended).bytes}}));CHECK(options.installed());
        }
    }CHECK(!ui::active());
}
void motion(){
    MenuMotion m;m.activate(100);CHECK(m.frame(0)==0 && m.playing(2) && !m.playing(0));
    m.update(190,-1);CHECK(m.frame(0)==0 && m.frame(2)==0);m.update(210,-1);CHECK(m.frame(2)==1);
    m.update(210,0);CHECK(m.frame(0)==0 && m.playing(0));m.update(390,0);CHECK(m.frame(0)==2);
    m.update(390,-1);m.update(900,-1);CHECK(m.frame(0)==2);m.update(900,0);CHECK(m.frame(0)==0);
    m.update(3600,0);CHECK(m.frame(0)==0 && m.cycle(0)==3 && m.frame(2)==20 && !m.playing(2));
    m.update(3600,0,false);m.update(9000,0,false);CHECK(m.frame(0)==0 && !m.playing(0));
    m.update(9000,0,true);m.update(9090,0);CHECK(m.frame(0)==1);
    m.update(8000,-1);CHECK(m.frame(0)==1);m.activate(0);CHECK(m.frame(2)==0 && m.cycle(0)==0);
    m.update(std::numeric_limits<uint64_t>::max(),4);CHECK(m.frame(2)==20 && m.frame(4)==0);
    m.counts({0,0,0,0,0});m.activate(0);m.update(100000,0);CHECK(m.frame(0)==0);
}
void menuAudio(const std::filesystem::path& tmp){
    fixtures::masterInstall(tmp);for(const auto* name:{"GameSel","ClsRoll","BrkRoll","BatRoll","SpcRoll","TetRoll"})fixtures::write(tmp/"Waves"/(std::string(name)+".wav"),fixtures::wave());
    Resources resources;CHECK(!resources.open(tmp));fixtures::ScriptedSystem system;system.audioSupported=true;
    auto audible=[](const auto& samples){return std::any_of(samples.begin(),samples.end(),[](auto sample){return sample!=0;});};
    {MenuAudio output(system);CHECK(!output.open(resources,tmp,false));output.enter();CHECK(!system.audioStarted);
        CHECK(!output.open(resources,tmp,true));output.enter();CHECK(system.audioCallback && audible(system.pump(1)));
        MenuMotion m;m.activate(0);m.update(0,1);output.update(m);CHECK(audible(system.pump(100)));
        m.update(10,-1,false);output.update(m);CHECK(!audible(system.pump(100))); // entry paused, rollover stopped
        m.update(10,1,true);output.update(m);CHECK(audible(system.pump(100)));
        m.activate(0);m.update(2100,-1);output.update(m);system.pump(100);
        m.update(2100,0);output.update(m);CHECK(audible(system.pump(100)));CHECK(!audible(system.pump(100))); // Classic is one-shot
        m.update(4800,0);output.update(m);CHECK(audible(system.pump(100))); // restart at video loop
        output.stop();CHECK(system.audioStopped && !system.audioCallback);output.enter();CHECK(system.audioCallback);
    }CHECK(!system.audioCallback);
    fixtures::ScriptedSystem unsupported;MenuAudio silent(unsupported);CHECK(!silent.open(resources,tmp,true));silent.enter();CHECK(!unsupported.audioCallback);
}
}
static const test::Suite suite("boggle.unit.appearance",{
    {"scoped controls, independent dialog metrics/captions, clipping and proportional fields",[](const auto&){controls();}},
    {"hover entry/reset, idle hold, Battle startup, looping, focus and host-clock limits",[](const auto&){motion();}},
    {"optional menu PCM, looping hover, focus silence, opt-out, failure and output cleanup",menuAudio}
});
