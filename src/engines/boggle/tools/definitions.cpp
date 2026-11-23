// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/resources.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <cstdio>
namespace {
int check(int argc,char**argv){
    if(argc!=3 && argc!=4){std::fprintf(stderr,"usage: reluva-tool boggle definition-check <folder> [ASCII headword]\n");return 2;}
    reluva::boggle::Resources resources;reluva::boggle::Definitions definitions;
    if(auto e=resources.open(argv[2])){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;}
    if(auto e=resources.definitions(definitions)){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;}
    reluva::Md5 digest,formatted;size_t bytes=0,count=0,richBytes=0,unsupported=0;
    auto add=[&](const reluva::boggle::DefinitionEntry& e){digest.update(e.headword.data(),e.headword.size());digest.update("\0",1);digest.update(e.text.data(),e.text.size());digest.update("\0",1);bytes+=e.text.size();richBytes+=e.formatted.plain().size();unsupported+=e.formatted.unsupported.size();++count;
        for(const auto& span:e.formatted.spans){formatted.update(span.text.data(),span.text.size());formatted.update("\0",1);const uint8_t attrs[]={uint8_t(span.style),uint8_t(span.root)};formatted.update(attrs,sizeof(attrs));formatted.update(span.reference.data(),span.reference.size());formatted.update("\0",1);}
        formatted.update("\0",1);};
    if(argc==4){std::vector<reluva::boggle::DefinitionEntry> matches;if(auto e=definitions.lookup(argv[3],matches)){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;}for(const auto& e:matches)add(e);}
    else for(uint32_t i=1;i<=definitions.entries();++i){reluva::boggle::DefinitionEntry e;if(auto error=definitions.entry(i,e)){std::fprintf(stderr,"%s\n",error.message.c_str());return 1;}add(e);}
    std::printf("Formatted UTF-8 bytes %zu, unsupported symbol names %zu\n",richBytes,unsupported);
    if(unsupported){std::fprintf(stderr,"Definition archive contains unsupported symbol names\n");return 1;}
    std::printf("formatted definitions: %s\n",formatted.hexDigest().c_str());
    const auto index=definitions.indexChecksum();digest.update(index.data(),index.size());
    std::printf("Authored word keys %zu, links %zu, word-index records %zu\n",definitions.indexedWords(),definitions.indexedLinks(),definitions.wordRecords());
    std::printf("Definition articles %zu, ASCII headword keys %zu, variant keys %zu, subentry keys %zu, stored-form keys %zu, derivative keys %zu, checked %zu, text bytes %zu\n",definitions.entries(),definitions.headwords(),definitions.variants(),definitions.subentries(),definitions.inflections(),definitions.derivatives(),count,bytes);
    std::printf("decoded definitions: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"definition-check","<folder> [ASCII headword]","validate the definition archive; report counts/checksum without exporting entries",check}});
}
