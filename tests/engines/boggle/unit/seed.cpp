// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/seed.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/frontend/game.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <set>
using namespace reluva;
using namespace reluva::boggle;
namespace {
void settings() {
    ClassicOptions options;CHECK(!readClassicOptions({},options));CHECK(!options.seed);
    for(const auto* text:{"0","6551","4294967295"}) {
        CHECK(!readClassicOptions({{"classic_seed",text}},options));
        CHECK(options.seed && std::to_string(*options.seed)==text);
    }
    for(const auto* bad:{"","-1","+1","01","0x1997"," 1","1 ","1x","4294967296","999999999999999999999"}) {
        CHECK(bool(readClassicOptions({{"classic_seed",bad}},options)));CHECK(!options.seed);
    }
    // This checks the process sequence's no-reuse property, not a statistical
    // assumption that random boards can never coincide.
    std::set<uint32_t> seen;
    for(size_t i=0;i<4096;++i)CHECK(seen.insert(freshClassicSeed()).second);
}
void install(const std::filesystem::path& root) {
    fixtures::artworkInstall(root);
    for(bool master:{false,true}) {
        std::string text;
        for(size_t i=0;i<(master?25u:16u);++i) {
            for(size_t face=0;face<6;++face){text+=char('A'+(i*6+face)%26);text+=face==5?'\n':',';}
        }
        fixtures::write(root / "Dicts" / (master?"EngMas.dst":"EngOrg.dst"),text);
    }
    // Generated coordinate colors make each alphabet-atlas cell distinguishable;
    // no letters, sampled retail pixels or retail dice are embedded here.
    auto pixels=fixtures::atlas();
    for(size_t y=0;y<512;++y)for(size_t x=0;x<512;++x) {
        const size_t at=54+(y*512+x)*3;
        pixels[at]=uint8_t(x/2);pixels[at+1]=uint8_t(y/2);pixels[at+2]=uint8_t((x+y)/4);
    }
    fixtures::write(root / "Textures/Letters.bmp",pixels);fixtures::write(root / "Textures/LettersQu.bmp",pixels);
}
GameTarget target(const std::filesystem::path& root,bool master,bool art) {
    GameTarget t;t.path=root;t.settings={{"classic_variant",master?"master":"original"},{"classic_artwork",art?"1":"0"},
        {"classic_animations","0"},{"classic_sound","0"},{"classic_seconds","0"},{"classic_auto_orient","0"},{"frontend_movies","0"}};
    return t;
}
std::vector<std::string> rounds(const GameTarget& t) {
    fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
    sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
        if(f==0 || f==2 || f==5)return {keyEvent(key::Return)};
        if(f==1 || f==3)return {keyEvent(key::F1)};
        if(f==4)return {keyEvent(key::F1+1)};
        if(f==6)return {keyEvent(key::Escape)};
        return {};
    };
    ClassicGame game(sys,t);CHECK(!game.run());CHECK(!sys.textEnabled && sys.hashes.size()==6);
    if(sys.hashes.size()==6)CHECK(sys.hashes[0]==sys.hashes[2]);
    return sys.hashes;
}
void lifecycle(const std::filesystem::path& tmp) {
    install(tmp / "game");
    for(bool master:{false,true})for(bool art:{false,true}) {
        auto t=target(tmp / "game",master,art);
        for(uint32_t seed:{uint32_t(6551),UINT32_MAX}) {
            t.settings["classic_seed"]=std::to_string(seed);
            const auto first=rounds(t),repeat=rounds(t);CHECK(first==repeat);
            t.settings["classic_seed"]=std::to_string(uint32_t(seed+1));
            const auto next=rounds(t);
            if(first.size()==6 && next.size()==6) {
                CHECK(first[5]==next[0]); // New Board advances the controlled seed
                CHECK(first[0]!=next[0]); // these invented boards distinguish the seeds
            }
        }
        t.settings.erase("classic_seed");rounds(t); // normal play also preserves replay
    }
}
void menu(const std::filesystem::path& tmp) {
    install(tmp / "game");
    for(bool fixed:{false,true}) {
        auto t=target(tmp / "game",false,true);if(fixed)t.settings["classic_seed"]="6551";
        fixtures::ScriptedSystem sys;sys.clock=[](size_t){return uint64_t(0);};
        sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0 || f==1 || f==2 || f==5 || f==6)return {keyEvent(key::Return)};
            if(f==3 || f==8)return {keyEvent(key::F1+2)};
            if(f==4)return {keyEvent('c')};
            if(f==9)return {keyEvent('l')};
            return {};
        };
        FrontendGame game(sys,t);CHECK(!game.run());CHECK(sys.hashes.size()==9 && !sys.textEnabled);
        if(fixed && sys.hashes.size()==9)CHECK(sys.hashes[2]==sys.hashes[6]);
        t.settings["classic_seed"]="invalid";fixtures::ScriptedSystem invalid;
        FrontendGame bad(invalid,t);CHECK(bool(bad.run()) && invalid.frame==0 && invalid.w==0);
    }
}
}
static const test::Suite suite("boggle.unit.seed",{
    {"fresh process sequence and strict optional replay seed",[](const auto&){settings();}},
    {"both variants/renderers: seeded restart, replay, next board and uint32 wrap",[](const auto& tmp){lifecycle(tmp);}},
    {"menu re-entry with fresh or fixed seeds and invalid override before graphics",[](const auto& tmp){menu(tmp);}},
});
