// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "tests/support/avi_fixture.h"
namespace reluva::test::fixtures {
inline void cv16(std::vector<uint8_t>& b,uint16_t n){b.push_back(uint8_t(n>>8));b.push_back(uint8_t(n));}
inline void cv32(std::vector<uint8_t>& b,uint32_t n){cv16(b,uint16_t(n>>16));cv16(b,uint16_t(n));}
inline std::vector<uint8_t> cvChunk(uint16_t id,std::vector<uint8_t> data) {
    if(data.size()&1)data.push_back(0x99); // arbitrary encoder alignment byte
    std::vector<uint8_t> out;cv16(out,id);cv16(out,uint16_t(data.size()+4));out.insert(out.end(),data.begin(),data.end());return out;
}
inline std::vector<uint8_t> cvStrip(int w,int h,const std::vector<std::vector<uint8_t>>& chunks,int top=0,bool inter=false) {
    std::vector<uint8_t> out;cv16(out,inter?0x1100:0x1000);cv16(out,0);cv16(out,uint16_t(top));cv16(out,0);cv16(out,uint16_t(h));cv16(out,uint16_t(w));
    for(const auto& chunk:chunks)out.insert(out.end(),chunk.begin(),chunk.end());
    out[2]=uint8_t(out.size()>>8);out[3]=uint8_t(out.size());return out;
}
inline std::vector<uint8_t> cvFrame(int w,int h,const std::vector<std::vector<uint8_t>>& strips,uint8_t flags=0) {
    std::vector<uint8_t> out{flags,0,0,0};cv16(out,uint16_t(w));cv16(out,uint16_t(h));cv16(out,uint16_t(strips.size()));
    for(const auto& strip:strips)out.insert(out.end(),strip.begin(),strip.end());
    const auto n=out.size();out[1]=uint8_t(n>>16);out[2]=uint8_t(n>>8);out[3]=uint8_t(n);return out;
}
inline std::vector<uint8_t> cinepakAvi(int w,int h,const std::vector<std::vector<uint8_t>>& packets,
                                      const std::vector<uint8_t>& pcm={},int bits=8,int channels=1) {
    std::vector<uint8_t> main(56),video(56),format(40);
    avi32(main,0,100000);avi32(main,16,uint32_t(packets.size()));avi32(main,24,pcm.empty()?1:2);avi32(main,32,w);avi32(main,36,h);
    std::memcpy(video.data(),"vidscvid",8);avi32(video,20,1);avi32(video,24,10);avi32(video,32,uint32_t(packets.size()));
    avi32(format,0,40);avi32(format,4,w);avi32(format,8,h);avi32(format,12,0x180001);std::memcpy(format.data()+16,"cvid",4);
    std::vector<std::vector<uint8_t>> streams{aviChunk("avih",main),aviList("strl",{aviChunk("strh",video),aviChunk("strf",format)})};
    if(!pcm.empty()) {
        std::vector<uint8_t> audio(56),wave(16);std::memcpy(audio.data(),"auds",4);avi32(audio,20,1);avi32(audio,24,8000);avi32(audio,32,uint32_t(pcm.size()/(channels*bits/8)+3));
        wave[0]=1;wave[2]=uint8_t(channels);avi32(wave,4,8000);avi32(wave,8,8000*channels*bits/8);wave[12]=uint8_t(channels*bits/8);wave[14]=uint8_t(bits);
        streams.push_back(aviList("strl",{aviChunk("strh",audio),aviChunk("strf",wave)}));
    }
    std::vector<std::vector<uint8_t>> movie;
    for(const auto& p:packets)movie.push_back(aviChunk("00dc",p));
    if(!pcm.empty())movie.push_back(aviChunk("01wb",pcm));
    auto hdrl=aviList("hdrl",streams),movi=aviList("movi",movie);
    std::vector<uint8_t> body{'A','V','I',' '};body.insert(body.end(),hdrl.begin(),hdrl.end());body.insert(body.end(),movi.begin(),movi.end());return aviChunk("RIFF",body);
}
inline std::vector<uint8_t> grayMovie(int w=4,int h=4,bool audio=true) {
    std::vector<std::vector<uint8_t>> packets;
    for(uint8_t value:{uint8_t(30),uint8_t(90)}) {
        std::vector<uint8_t> refs(size_t(w/4)*size_t(h/4),0);
        packets.push_back(cvFrame(w,h,{cvStrip(w,h,{cvChunk(0x2600,{value,value,value,value}),cvChunk(0x3200,refs)})}));
    }
    return cinepakAvi(w,h,packets,audio ? std::vector<uint8_t>{0,128,255} : std::vector<uint8_t>{});
}
}
