// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/resources.h"
#include "common/md5.h"
#include "engines/boggle/formats/cursor_image.h"
#include "engines/boggle/classic/options.h"
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
int musicCheck(int argc,char**argv) {
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle music-check <game folder>\n");return 2;}
    reluva::boggle::Resources resources;
    if(auto err=resources.open(argv[2])){std::fprintf(stderr,"%s\n",err.message.c_str());return 1;}
    reluva::Md5 digest; size_t frames=0;
    auto number=[&](uint32_t n){uint8_t b[4];for(int i=0;i<4;++i)b[i]=uint8_t(n>>(i*8));digest.update(b,4);};
    for(const auto* name:reluva::boggle::classicMusicFiles) {
        reluva::audio::Sound sound;
        if(auto err=resources.sound(std::string("Waves/")+name+".wav",sound)){std::fprintf(stderr,"%s\n",err.message.c_str());return 1;}
        number(sound.rate);number(sound.channels);number(uint32_t(sound.frames()));
        for(int16_t sample:sound.samples){const uint16_t n=uint16_t(sample);uint8_t b[2]={uint8_t(n),uint8_t(n>>8)};digest.update(b,2);}
        frames+=sound.frames();
    }
    std::printf("Classic music tracks %zu, audio frames %zu\n",reluva::boggle::classicMusicFiles.size(),frames);
    std::printf("decoded music: %s\n",digest.hexDigest().c_str());return 0;
}
int cursors(int argc,char**argv) {
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle cursor-check <game folder>\n");return 2;}
    reluva::boggle::Resources resources;reluva::boggle::Language language;
    auto failed=[](const reluva::Error& e){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;};
    if(auto e=resources.open(argv[2]))return failed(e);
    if(auto e=resources.language("Language/English.dll",language))return failed(e);
    reluva::Md5 digest;size_t images=0,groups=0;
    auto number=[&](uint32_t n){uint8_t b[4];for(int i=0;i<4;++i)b[i]=uint8_t(n>>(i*8));digest.update(b,4);};
    for(const auto& member:language.resources)if(member.type.isId(1) && member.language==1033) {
        reluva::boggle::CursorImage image;if(auto e=reluva::boggle::readCursorImage(member.data,image))return failed(e);
        number(member.name.id);number(image.colors.w);number(image.colors.h);number(image.hotspotX);number(image.hotspotY);number(image.depth);
        digest.update(image.colors.rgba.data(),image.colors.rgba.size());digest.update(image.mask.data(),image.mask.size());++images;
    }
    for(const auto& group:language.resources)if(group.type.isId(12) && group.language==1033) {
        reluva::boggle::CursorImage image;if(auto e=reluva::boggle::readCursorGroup(language.resources,group.name.id,1033,image))return failed(e);
        ++groups;
    }
    if(!images || !groups)return failed(reluva::Error::fail("No installed cursor artwork"));
    std::printf("Indexed cursor images %zu, groups %zu\n",images,groups);
    std::printf("decoded cursors: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"cursor-check","<game folder>","validate indexed cursor groups and masks; report metadata only",cursors},{"presentation-check","<game folder>","decode both Classic lid clips and eight sound cues; report metadata only",check},{"music-check","<game folder>","decode the fifteen loop tracks; report metadata only",musicCheck}});
}
