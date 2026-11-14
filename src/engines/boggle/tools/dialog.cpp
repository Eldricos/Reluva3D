// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/dialog.h"
#include "engines/boggle/resources.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <cstdio>
namespace {
int check(int argc,char** argv){
    using namespace reluva;using namespace reluva::boggle;
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle dialog-check <game folder>\n");return 2;}
    Resources resources;Language language;
    if(auto e=resources.open(argv[2])){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;}
    if(auto e=resources.language("Language/English.dll",language)){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;}
    Md5 digest;size_t count=0,extended=0,controls=0;
    auto number=[&](uint32_t n){uint8_t b[4];for(size_t i=0;i<4;++i)b[i]=uint8_t(n>>(i*8));digest.update(b,4);};
    auto string=[&](const std::u16string& s){number(uint32_t(s.size()));for(auto c:s)number(c);};
    auto field=[&](const DialogField& f){number(f.ordinal);number(f.id);string(f.text);};
    auto rect=[&](const DialogRect& r){for(int n:{r.x,r.y,r.w,r.h})number(uint32_t(n));};
    for(const auto& r:language.resources)if(r.type.isId(5)){
        DialogTemplate d;if(auto e=readDialog(r.data,d)){std::fprintf(stderr,"dialog %u language %u: %s\n",r.name.id,r.language,e.message.c_str());return 1;}
        ++count;extended+=d.extended;controls+=d.controls.size();number(r.name.named);number(r.name.id);string(r.name.text);number(r.language);number(d.extended);number(d.style);number(d.extendedStyle);number(d.helpId);rect(d.rect);
        field(d.menu);field(d.windowClass);string(d.title);number(d.hasFont);number(d.pointSize);number(d.weight);number(d.italic);number(d.charset);string(d.typeface);number(uint32_t(d.controls.size()));
        for(const auto& c:d.controls){number(c.id);number(c.style);number(c.extendedStyle);number(c.helpId);rect(c.rect);field(c.windowClass);field(c.title);number(uint32_t(c.creationData.size()));if(!c.creationData.empty())digest.update(c.creationData.data(),c.creationData.size());}
    }
    std::printf("Windows dialogs %zu, standard %zu, extended %zu, controls %zu\n",count,count-extended,extended,controls);
    std::printf("decoded dialogs: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"dialog-check","<game folder>","validate installed dialog templates; report counts and checksum only",check}});
}
