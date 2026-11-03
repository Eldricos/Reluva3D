// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "common/video/avi_video1.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <span>

namespace reluva::video {
namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t* p) { return uint32_t(u16(p)) | uint32_t(u16(p+2)) << 16; }
bool tag(const uint8_t* p, const char* s) { return std::memcmp(p,s,4) == 0; }
struct Parser {
    const std::vector<uint8_t>& bytes;
    std::span<const uint8_t> main, stream, format;
    std::vector<std::span<const uint8_t>> packets;
    bool haveStreamList = false;
    // Context: 0 root, 1 hdrl, 2 strl, 3 movi (including rec lists).
    bool chunks(size_t at, size_t end, int context, int depth = 0) {
        if (depth > 8) return false;
        while (at < end) {
            if (end - at < 8) return false;
            const auto* p = bytes.data() + at;
            const size_t size = u32(p+4), begin = at+8;
            if (size > end-begin || (size & 1) > end-begin-size) return false;
            std::span<const uint8_t> data(bytes.data()+begin,size);
            if (tag(p,"LIST")) {
                if (size < 4) return false;
                int next = -1;
                if (context == 0 && tag(data.data(),"hdrl")) next = 1;
                if (context == 1 && tag(data.data(),"strl")) {
                    if (haveStreamList) return false;
                    haveStreamList = true; next = 2;
                }
                if (context == 0 && tag(data.data(),"movi")) next = 3;
                if (context == 3 && tag(data.data(),"rec ")) next = 3;
                if (next >= 0 && !chunks(begin+4,begin+size,next,depth+1)) return false;
            } else if (context == 1 && tag(p,"avih")) {
                if (!main.empty() || size < 56) return false;
                main = data;
            } else if (context == 2 && tag(p,"strh")) {
                if (!stream.empty() || size < 56) return false;
                stream = data;
            } else if (context == 2 && tag(p,"strf")) {
                if (!format.empty() || size < 40) return false;
                format = data;
            } else if (context == 3 && (tag(p,"00dc") || tag(p,"00db"))) {
                if (packets.size() >= 512) return false;
                packets.push_back(data);
            } else if (context == 3 && (p[2] == 'w' || p[2] == 'd' || tag(p,"00pc"))) return false;
            at = begin + size + (size & 1);
        }
        return at == end;
    }
};
bool frame(std::span<const uint8_t> data, int w, int h, std::vector<uint16_t>& pixels, bool indexed) {
    size_t at = 0, block = 0; const size_t blocks = size_t(w/4) * size_t(h/4);
    auto word = [&](uint16_t& n) { if (data.size()-at < 2) return false; n = u16(data.data()+at); at += 2; return true; };
    while (block < blocks) {
        uint16_t code; if (!word(code)) return false;
        const unsigned b = code >> 8;
        if ((b & 0xfc) == 0x84) {
            const size_t skip = code & 0x3ff;
            if (!skip || skip > blocks-block) return false;
            block += skip; continue;
        }
        std::array<uint16_t,8> colors{};
        bool eight = false;
        if (indexed) {
            eight = b >= 0x90;
            if (b < 0x80 || eight) {
                const size_t count = eight ? 8 : 2;
                if (data.size()-at < count) return false;
                for (size_t i=0;i<count;++i) colors[i]=data[at++];
            } else colors.fill(code & 0xff);
        } else if (b < 0x80) {
            if (!word(colors[0]) || !word(colors[1])) return false;
            eight = (colors[0] & 0x8000) != 0;
            if (eight) for (size_t i = 2; i < 8; ++i) if (!word(colors[i])) return false;
        } else colors.fill(code & 0x7fff);
        const size_t x = (block % size_t(w/4)) * 4, y = (block / size_t(w/4)) * 4;
        for (size_t dy=0;dy<4;++dy) for (size_t dx=0;dx<4;++dx) {
            size_t index = 0;
            if (b < 0x80 || (indexed && eight)) index = (eight ? ((dy/2)*2+dx/2)*2 : 0) + (((code >> (dy*4+dx)) & 1) ^ 1);
            pixels[(size_t(h)-1-y-dy)*size_t(w)+x+dx] = colors[index] & 0x7fff;
        }
        ++block;
    }
    // VfW packets may end with a two-byte zero marker.
    return at == data.size() || (data.size()-at == 2 && u16(data.data()+at) == 0);
}
}
Error AviVideo1::read(const std::vector<uint8_t>& bytes) {
    *this = {};
    const auto invalid = [] { return Error::fail("Unsupported or malformed AVI: expected bounded single-stream 8/16-bit Microsoft Video 1"); };
    if (bytes.size() < 12 || bytes.size() > 64*1024*1024 || !tag(bytes.data(),"RIFF") ||
        !tag(bytes.data()+8,"AVI ") || u32(bytes.data()+4) != bytes.size()-8) return invalid();
    Parser p{bytes, {}, {}, {}, {}, false};
    if (!p.chunks(12,bytes.size(),0) || p.main.empty() || p.stream.empty() || p.format.empty() || p.packets.empty()) return invalid();
    const auto* a=p.main.data(); const auto* s=p.stream.data(); const auto* f=p.format.data();
    const uint32_t w=u32(f+4), h=u32(f+8), rate=u32(s+24), scale=u32(s+20);
    if (u32(a+24) != 1 || u32(a+32) != w || u32(a+36) != h || u32(s+28) != 0 || !tag(s,"vids") || (!tag(s+4,"CRAM") && !tag(s+4,"MSVC")) ||
        u32(f) < 40 || u32(f) > p.format.size() || u16(f+12) != 1 || (u16(f+14) != 16 && u16(f+14) != 8) ||
        (!tag(f+16,"CRAM") && !tag(f+16,"MSVC")) || !w || !h || w>2048 || h>2048 || w%4 || h%4 ||
        !rate || !scale || rate>1000000 || scale>1000000 || u32(a+16)!=p.packets.size() || u32(s+32)!=p.packets.size() ||
        uint64_t(w)*h*4*p.packets.size()>128*1024*1024) return invalid();
    const bool indexed=u16(f+14)==8;
    std::array<std::array<uint8_t,3>,256> palette{};
    const uint32_t colors=u32(f+32) ? u32(f+32) : 256;
    if (indexed) {
        if (colors>256 || p.format.size()-u32(f)<size_t(colors)*4) return invalid();
        for (size_t i=0;i<colors;++i) {
            const auto* color=f+u32(f)+i*4;
            palette[i]={color[2],color[1],color[0]};
        }
    }
    AviVideo1 next; next.width=int(w); next.height=int(h); next.rate=rate; next.scale=scale;
    std::vector<uint16_t> pixels(size_t(w)*h);
    for (const auto packet : p.packets) {
        if (!frame(packet,int(w),int(h),pixels,indexed)) return invalid();
        image::Image img; img.w=int(w); img.h=int(h); img.rgba.resize(size_t(w)*h*4);
        for (size_t i=0;i<pixels.size();++i) {
            auto* out=img.rgba.data()+i*4; const uint16_t n=pixels[i];
            if (indexed) {
                if (n>=colors) return invalid();
                std::copy(palette[n].begin(),palette[n].end(),out);
            } else for (int c=0;c<3;++c) { const unsigned v=(n>>((2-c)*5))&31; out[c]=uint8_t((v<<3)|(v>>2)); }
            out[3]=255;
        }
        next.frames.push_back(std::move(img));
    }
    *this=std::move(next); return Error::success();
}
} // namespace reluva::video
