// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/resources.h"
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
const reluva::tool::Commands commands("boggle",{{"frontend-check","<game folder>","validate the optional game-selection artwork; report metadata only",check}});
}
