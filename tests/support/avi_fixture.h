// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
namespace reluva::test::fixtures {
inline void avi32(std::vector<uint8_t>& b, size_t at, uint32_t n) { for (int i=0;i<4;++i) b[at+i]=uint8_t(n>>(i*8)); }
inline std::vector<uint8_t> aviChunk(const char* id, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(8); std::memcpy(out.data(),id,4); avi32(out,4,uint32_t(data.size()));
    out.insert(out.end(),data.begin(),data.end()); if (data.size()&1) out.push_back(0); return out;
}
inline std::vector<uint8_t> aviList(const char* id, const std::vector<std::vector<uint8_t>>& chunks) {
    std::vector<uint8_t> data(id,id+4); for (const auto& c:chunks) data.insert(data.end(),c.begin(),c.end()); return aviChunk("LIST",data);
}
inline std::vector<uint8_t> avi(int w, int h, const std::vector<std::vector<uint8_t>>& packets) {
    std::vector<uint8_t> main(56), stream(56), format(40);
    avi32(main,0,33333); avi32(main,16,uint32_t(packets.size())); avi32(main,24,1); avi32(main,32,w); avi32(main,36,h);
    std::memcpy(stream.data(),"vidsCRAM",8); avi32(stream,20,1); avi32(stream,24,30); avi32(stream,32,uint32_t(packets.size()));
    avi32(format,0,40); avi32(format,4,w); avi32(format,8,h); avi32(format,12,0x100001); std::memcpy(format.data()+16,"CRAM",4);
    auto header=aviList("hdrl",{aviChunk("avih",main),aviList("strl",{aviChunk("strh",stream),aviChunk("strf",format)})});
    std::vector<std::vector<uint8_t>> frames; for(const auto& p:packets)frames.push_back(aviChunk("00dc",p));
    std::vector<uint8_t> body{'A','V','I',' '}; body.insert(body.end(),header.begin(),header.end());
    auto movie=aviList("movi",frames); body.insert(body.end(),movie.begin(),movie.end()); return aviChunk("RIFF",body);
}
inline void aviWord(std::vector<uint8_t>& out,uint16_t n) { out.push_back(uint8_t(n));out.push_back(uint8_t(n>>8)); }
inline std::vector<uint8_t> classicAvi() {
    std::vector<std::vector<uint8_t>> packets;
    for(unsigned i=0;i<55;++i) { std::vector<uint8_t> p; aviWord(p,uint16_t(0x8000|(i%32)*0x401)); packets.push_back(p); }
    return avi(4,4,packets);
}
}
