// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/frame_comparison.h"
#include "tools/tool.h"
#include <charconv>
#include <cstdio>
#include <fstream>
#include <utility>
namespace {
reluva::Error load(const char* path,reluva::image::Image& out){
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in || in.tellg()<0 || in.tellg()>32*1024*1024)return reluva::Error::fail("Cannot read frame, or encoded frame exceeds 32 MiB");
    std::vector<uint8_t> bytes(size_t(in.tellg()));in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!in)return reluva::Error::fail("Cannot read complete frame");
    return reluva::image::decode(bytes,out);
}
int compare(int argc,char**argv){
    if(argc!=4 && argc!=8){std::fprintf(stderr,"usage: reluva-tool boggle frame-compare <reference> <native> [x y width height]\n");return 2;}
    reluva::image::Image a,b;for(const auto& item:{std::pair{argv[2],&a},std::pair{argv[3],&b}})if(auto e=load(item.first,*item.second)){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;}
    int region[]={0,0,a.w,a.h};if(argc==8)for(size_t i=0;i<4;++i){const std::string_view s=argv[4+i];const auto p=std::from_chars(s.data(),s.data()+s.size(),region[i]);if(p.ec!=std::errc{} || p.ptr!=s.data()+s.size())return 2;}
    reluva::boggle::FrameComparison result;if(auto e=reluva::boggle::compareFrames(a,b,region[0],region[1],region[2],region[3],result)){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;}
    std::printf("Compared RGB pixels %llu, changed %llu, maximum delta %u\nMean absolute %.6f, RMS %.6f\n",(unsigned long long)result.pixels,(unsigned long long)result.changed,result.maxDelta,result.meanAbsolute,result.rmse);return 0;
}
const reluva::tool::Commands commands("boggle",{{"frame-compare","<reference> <native> [x y width height]","compare supplied screenshots by region; report RGB differences without exporting pixels",compare}});
}
