// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/layout.h"
#include "common/md5.h"
#include "common/fs.h"
#include "tools/tool.h"
#include <cstdio>
namespace {
int fonts(int argc,char** argv) {
    using namespace reluva;
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle font-check <game folder>\n");return 2;}
    boggle::Resources resources;
    if(auto error=resources.open(argv[2])){std::fprintf(stderr,"%s\n",error.message.c_str());return 1;}
    Md5 digest;size_t count=0;int advance=0;
    if(fs::findFile(argv[2],"Fonts/KPSI.TTF")) {
        font::TrueTypeAscii font;
        if(auto error=resources.font("Fonts/KPSI.TTF",18,font)){std::fprintf(stderr,"%s\n",error.message.c_str());return 1;}
        for(char c=32;c<=126;++c) {
            const std::string text(1,c);const int width=font.measure(text);advance+=width;
            const uint8_t n[4]={uint8_t(width),uint8_t(width>>8),0,0};digest.update(n,4);
            Surface screen;screen.create(64,64,PixelFormat::RGBA32);
            font.draw(screen,12,40,text,{240,220,180},52);digest.update(screen.pixels.data(),screen.pixels.size());
        }
        count=1;
    }
    std::printf("Classic handwriting fonts %zu, ASCII glyphs %zu, advance pixels %d\n",count,count*95,advance);
    std::printf("decoded font: %s\n",digest.hexDigest().c_str());return 0;
}
int check(int argc,char** argv) {
    using namespace reluva;
    using namespace reluva::boggle;
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle artwork-check <game folder>\n");return 2;}
    Resources resources;
    auto failed=[](const Error& error){std::fprintf(stderr,"%s\n",error.message.c_str());return 1;};
    if(auto error=resources.open(argv[2]))return failed(error);
    Md5 digest;size_t variants=0,frames=0;
    for(auto variant:{ClassicVariant::Original,ClassicVariant::Master}) {
        ClassicOptions options;options.variant=variant;ClassicArtwork artwork;
        if(auto error=artwork.open(resources,argv[2],options,{}))return failed(error);
        if(!artwork.active())continue;
        DiceSet dice;Board board;Dictionary dictionary;image::Image letters,qu;
        if(auto error=resources.diceSet(options.dice(),dice))return failed(error);
        if(auto error=Board::generate(dice,0x1997,board))return failed(error);
        if(auto error=resources.image("Textures/Letters.bmp",letters))return failed(error);
        if(auto error=resources.image("Textures/LettersQu.bmp",qu))return failed(error);
        if(letters.w!=512 || letters.h!=512 || qu.w!=512 || qu.h!=512)return failed(Error::fail("Unsupported Classic letter atlas"));
        if(auto error=resources.dictionary("Dicts/English.dic",dictionary))return failed(error);
        ClassicRound round(dictionary);if(auto error=round.board(std::move(board)))return failed(error);
        if(auto error=round.start(0))return failed(error);
        Surface screen;screen.create(800,600,PixelFormat::RGBA32);
        for(uint64_t now:{uint64_t(0),uint64_t(90000),uint64_t(180000)}) {
            round.tick(now);artwork.draw(screen,round,now,180000,letters,qu,{},0);
            digest.update(screen.pixels.data(),screen.pixels.size());++frames;
        }
        options.autoOrient=false;
        if(auto error=artwork.open(resources,argv[2],options,{}))return failed(error);
        if(auto error=round.start(0))return failed(error);
        artwork.draw(screen,round,0,180000,letters,qu,{},0);
        digest.update(screen.pixels.data(),screen.pixels.size());++frames;
        ++variants;
    }
    std::printf("Classic artwork variants %zu, rendered states %zu\n",variants,frames);
    std::printf("decoded artwork: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"artwork-check","<game folder>","validate and render Classic artwork; report metadata only",check},{"font-check","<game folder>","validate and rasterize optional ASCII handwriting; report metadata only",fonts}});
}
