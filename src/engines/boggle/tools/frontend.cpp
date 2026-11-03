// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/resources.h"
#include "engines/boggle/frontend/media.h"
#include "common/fs.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <cstdio>
namespace {
int check(int argc,char**argv) {
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle frontend-check <game folder>\n");return 2;}
    reluva::boggle::Resources resources;
    if(auto err=resources.open(argv[2])){std::fprintf(stderr,"%s\n",err.message.c_str());return 1;}
    reluva::Md5 digest;size_t images=0,pixels=0;
    if(reluva::fs::findFile(argv[2],"Bitmaps/Select1.bmp")) {
        reluva::image::Image image;
        if(auto err=resources.image("Bitmaps/Select1.bmp",image)){std::fprintf(stderr,"%s\n",err.message.c_str());return 1;}
        if(image.w!=640 || image.h!=480){std::fprintf(stderr,"Unsupported game-selection bitmap dimensions\n");return 1;}
        images=1;pixels=size_t(image.w)*image.h;digest.update(image.rgba.data(),image.rgba.size());
    }
    std::printf("Selection backgrounds %zu, pixels %zu\n",images,pixels);
    std::printf("decoded frontend: %s\n",digest.hexDigest().c_str());return 0;
}
int movies(int argc,char**argv) {
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle movie-check <game folder>\n");return 2;}
    reluva::boggle::Resources resources;
    if(auto err=resources.open(argv[2])){std::fprintf(stderr,"%s\n",err.message.c_str());return 1;}
    std::vector<std::string> paths{"Anims/INTRO.AVI"};
    for(const auto* name:reluva::boggle::menuMovieNames)paths.push_back(std::string("Anims/")+name+".avi");
    reluva::Md5 digest;size_t movies=0,frames=0,audioFrames=0;
    auto number=[&](uint32_t n){uint8_t b[4];for(int i=0;i<4;++i)b[i]=uint8_t(n>>(8*i));digest.update(b,4);};
    for(const auto& path:paths) {
        if(!reluva::fs::findFile(argv[2],path))continue;
        reluva::video::AviCinepak movie;
        if(auto err=resources.movie(path,movie)){std::fprintf(stderr,"%s\n",err.message.c_str());return 1;}
        number(movie.width);number(movie.height);number(movie.rate);number(movie.scale);number(uint32_t(movie.frames.size()));
        for(const auto& image:movie.frames)digest.update(image.rgba.data(),image.rgba.size());
        number(movie.soundtrack.rate);number(movie.soundtrack.channels);number(uint32_t(movie.soundtrack.frames()));
        for(auto sample:movie.soundtrack.samples){const uint16_t n=uint16_t(sample);uint8_t b[2]={uint8_t(n),uint8_t(n>>8)};digest.update(b,2);}
        ++movies;frames+=movie.frames.size();audioFrames+=movie.soundtrack.frames();
    }
    std::printf("Frontend movies %zu, video frames %zu, audio frames %zu\n",movies,frames,audioFrames);
    std::printf("decoded movies: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"frontend-check","<game folder>","validate the optional game-selection artwork; report metadata only",check},{"movie-check","<game folder>","decode available intro/menu movies and PCM; report metadata only",movies}});
}
