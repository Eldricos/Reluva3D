// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/resources.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <cstdio>
namespace {
int check(int argc,char**argv){
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle presentation-check <game folder>\n");return 2;}
    reluva::boggle::Resources r;if(auto e=r.open(argv[2])){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;}
    reluva::Md5 digest;size_t frames=0,audioFrames=0;
    auto number=[&](uint32_t n){uint8_t b[4];for(int i=0;i<4;++i)b[i]=uint8_t(n>>(i*8));digest.update(b,4);};
    for(const auto* path:{"Anims/ClsO1All.avi","Anims/ClsM1All.avi"}){
        reluva::video::AviVideo1 clip;if(auto e=r.animation(path,clip)){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;}
        number(clip.width);number(clip.height);number(clip.rate);number(clip.scale);number(uint32_t(clip.frames.size()));
        for(const auto& image:clip.frames) digest.update(image.rgba.data(),image.rgba.size());
        frames+=clip.frames.size();
    }
    for(const auto* name:{"Shake","ClkDown","ClkUp","Writing","Seven","Eight","Tick","GameOver"}){
        reluva::audio::Sound sound;if(auto e=r.sound(std::string("Waves/")+name+".wav",sound)){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;}
        number(sound.rate);number(sound.channels);number(uint32_t(sound.frames()));
        for(int16_t sample:sound.samples){const uint16_t n=uint16_t(sample);uint8_t b[2]={uint8_t(n),uint8_t(n>>8)};digest.update(b,2);}audioFrames+=sound.frames();
    }
    std::printf("Classic clips 2, video frames %zu, sounds 8, audio frames %zu\n",frames,audioFrames);
    std::printf("decoded presentation: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"presentation-check","<game folder>","decode both Classic lid clips and eight sound cues; report metadata only",check}});
}
