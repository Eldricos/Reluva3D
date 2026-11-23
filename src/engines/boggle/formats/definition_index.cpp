// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/definition_index.h"
#include "common/md5.h"
#include "engines/boggle/formats/definition_text.h"
#include <algorithm>
#include <array>
namespace reluva::boggle {
namespace {
Error bad(){return Error::fail("Invalid or unsupported definition word index");}
uint16_t be16(const std::vector<uint8_t>& b,size_t p){return uint16_t(b[p])<<8|b[p+1];}
uint32_t be32(const std::vector<uint8_t>& b,size_t p){return uint32_t(be16(b,p))<<16|be16(b,p+2);}
uint16_t le16(const std::vector<uint8_t>& b,size_t p){return uint16_t(b[p])|uint16_t(b[p+1])<<8;}
struct Row {std::string word;uint32_t id=0;uint8_t kind=0;};
std::string directoryKey(std::string word){
    word.resize(4,' ');for(auto& c:word)if(c>='A' && c<='Z')c+=32;return word;
}
void append32(std::string& bytes,uint32_t n,unsigned count){
    for(unsigned i=count;i;--i)bytes+=char(n>>((i-1)*8));
}
bool boundary(const Row& previous,std::string_view tail,const Row& first){
    if(tail.empty())return true;
    const unsigned cut=uint8_t(tail[0])&63;if(cut>previous.word.size())return false;
    const auto keep=previous.word.size()-cut;
    if(first.word.size()<keep || first.word.compare(0,keep,previous.word,0,keep))return false;
    std::string expected(1,char(first.kind|cut));expected+=first.word.substr(keep);expected.append("\7\0\20",3);
    append32(expected,uint32_t(int64_t(first.id)-previous.id),3);
    return tail.size()<expected.size() && std::string_view(expected).starts_with(tail);
}
}
Error DefinitionIndex::read(const std::vector<uint8_t>& b,uint32_t articles,size_t entryStream){
    *this={};if(b.size()<0x400 || b.size()>32*1024*1024 || !articles || articles>200000)return bad();
    const auto count=be16(b,0x316);const auto table=be32(b,0x318);
    // Independently generated entry-only fixtures and old entry-reader inputs.
    if(!count && !table)return Error::success();
    if(count!=3 || table!=0x32a || be16(b,0x314)!=1 || be32(b,table)!=0x00010001 ||
       be32(b,table+4)!=0x400 || be32(b,table+8)!=entryStream || be32(b,table+12)!=0x00020002 ||
       be32(b,table+24)!=0x00020003)return bad();
    const size_t base=be32(b,table+16),stream=be32(b,table+20),limit=be32(b,table+28);
    const size_t booleanStream=be32(b,table+32);
    if(base<entryStream || base>b.size() || b.size()-base<512 || stream<base+512 || limit<stream ||
       limit>b.size() || b.size()-limit<16 || booleanStream<limit+16 || booleanStream>b.size() ||
       be32(b,base)!=0x00020002 || be32(b,limit)!=0x00020003 ||
       !std::equal(b.begin()+base+4,b.begin()+base+14,"Index file"))return bad();
    const unsigned pages=be16(b,base+0x1e),pageBytes=be16(b,base+0x8c),maximum=be16(b,base+0x92),width=be16(b,base+0x94);
    if(pages<2 || pages>4096 || pageBytes<64 || pageBytes>4096 || (pageBytes&(pageBytes-1)) ||
       maximum<1 || maximum>128 || width<2 || width>12 || be16(b,base+0x8e)!=4 ||
       be16(b,base+0xa0)!=96 || uint64_t(pages)*pageBytes>limit-stream)return bad();
    struct Section {size_t offset=0,size=0;uint16_t stride=0,count=0;};std::map<unsigned,Section> sections;
    size_t end=0;
    for(size_t p=base+0xaa;p<base+0x14a;p+=16){
        const auto tag=be16(b,p);const uint64_t off=uint64_t(base)+be32(b,p+2);const auto size=be32(b,p+12);
        if(off<base+512 || off>stream || size>stream-off || !size || sections.contains(tag))return bad();
        sections[tag]={size_t(off),size,be16(b,p+8),be16(b,p+10)};end=std::max(end,size_t(off)+size);
    }
    for(unsigned tag:{200,210,220,230,240,250,260,270,290,300})if(!sections.contains(tag))return bad();
    if(end!=stream)return bad();
    std::vector<std::pair<size_t,size_t>> ranges;for(const auto&[tag,s]:sections)ranges.emplace_back(s.offset,s.offset+s.size);
    std::sort(ranges.begin(),ranges.end());for(size_t i=1;i<ranges.size();++i)if(ranges[i-1].second>ranges[i].first)return bad();
    const auto pool=sections[200],offsets=sections[210],lengths=sections[220],directory=sections[230];const unsigned n=1u<<width;
    if(pool.size>65536 || offsets.stride!=2 || offsets.count!=n || offsets.size!=n*2 ||
       lengths.stride!=1 || lengths.count!=n || lengths.size!=n || directory.stride!=4 ||
       directory.count!=pages || directory.size!=size_t(pages)*4)return bad();
    std::vector<std::string> tokens(n);size_t cache=0;
    for(unsigned i=0;i<n;++i){
        const auto off=le16(b,offsets.offset+i*2);const size_t len=b[lengths.offset+i];
        if(off>pool.size || len>pool.size-off || (i && !len))return bad();
        tokens[i].assign(reinterpret_cast<const char*>(b.data()+pool.offset+off),len);cache+=len;
    }
    if(!tokens[0].empty() || cache>1024*1024)return bad();
    DefinitionIndex next;next.present_=true;Row last;std::string previousTail;size_t expanded=0;
    for(unsigned page=0;page<pages;++page){
        std::string raw;const auto start=stream+size_t(page)*pageBytes;
        for(size_t bit=0;bit+width<=size_t(pageBytes)*8;bit+=width){
            unsigned symbol=0;for(unsigned k=0;k<width;++k){const auto p=bit+k;symbol=(symbol<<1)|((b[start+p/8]>>(7-p%8))&1);}
            if(raw.size()+tokens[symbol].size()>65536)return bad();
            raw+=tokens[symbol];
        }
        expanded+=raw.size();if(expanded>64*1024*1024)return bad();
        const std::string actualDirectory(reinterpret_cast<const char*>(b.data()+directory.offset+page*4),4);
        if(page==pages-1){
            std::string sentinel(1,'\0');sentinel+=last.word;sentinel+='\0';append32(sentinel,last.id,4);
            if(actualDirectory!=directoryKey(last.word) || !std::string_view(raw).starts_with(sentinel) ||
               std::any_of(raw.begin()+std::min(raw.size(),sentinel.size()),raw.end(),[](char c){return c!=0;}))return bad();
            continue;
        }
        Row row;size_t at=0;bool first=true;std::string tail;
        while(at<raw.size()){
            const auto begin=at;const auto flags=uint8_t(raw[at++]);const unsigned cut=flags&63;
            const auto stop=raw.find('\7',at);
            if(stop==raw.npos || raw.size()-stop<6){tail=raw.substr(begin);break;}
            if(cut>row.word.size() || raw[stop+1]!=0 || raw[stop+2]!=0x10 ||
               std::any_of(raw.begin()+at,raw.begin()+stop,[](unsigned char c){return c<32;}))return bad();
            row.word.resize(row.word.size()-cut);row.word.append(raw,at,stop-at);row.kind=flags&192;
            const uint32_t delta=uint32_t(uint8_t(raw[stop+3]))<<16|uint32_t(uint8_t(raw[stop+4]))<<8|uint8_t(raw[stop+5]);
            const int64_t id=int64_t(row.id)+int64_t(delta)-(delta&0x800000?0x1000000:0);
            if(id<1 || id>articles || row.word.empty() || row.word.size()>maximum || ++next.records_>500000)return bad();
            row.id=uint32_t(id);at=stop+6;
            if(first){
                if(!boundary(last,previousTail,row) || actualDirectory!=(page?directoryKey(row.word):std::string(4,' ')))return bad();
                first=false;
            }
            auto spelling=row.word;
            if(row.kind==0x80 && spelling[0]>='a' && spelling[0]<='z')spelling[0]-=32;
            if(row.kind==0xc0)for(auto& c:spelling)if(c>='a'&&c<='z')c-=32;
            std::string utf8;for(unsigned char c:spelling)utf8+=definitionUtf8(c);
            auto& refs=next.references_[utf8];
            if(std::find(refs.begin(),refs.end(),row.id)==refs.end()){
                if(refs.size()>=4096)return bad();
                refs.push_back(row.id);
            }
            if(!row.kind && row.word.size()<=64 && std::all_of(row.word.begin(),row.word.end(),[](char c){return c>='a' && c<='z';})){
                auto word=row.word;for(auto& c:word)c-=32;auto& ids=next.words_[word];
                if(std::find(ids.begin(),ids.end(),row.id)==ids.end()){
                    if(ids.size()>=64 || ++next.links_>400000)return bad();
                    ids.push_back(row.id);
                }
            }
        }
        if(first)return bad();
        if(page==pages-2){if(std::any_of(tail.begin(),tail.end(),[](char c){return c!=0;}))return bad();tail.clear();}
        last=std::move(row);previousTail=std::move(tail);
    }
    *this=std::move(next);return Error::success();
}
const std::vector<uint32_t>* DefinitionIndex::find(const std::string& key) const {
    const auto found=words_.find(key);return found==words_.end()?nullptr:&found->second;
}
const std::vector<uint32_t>* DefinitionIndex::reference(std::string_view spelling) const {
    const auto found=references_.find(std::string(spelling));return found==references_.end()?nullptr:&found->second;
}
std::string DefinitionIndex::checksum() const {
    Md5 hash;hash.update("word-index\0",11);
    for(const auto&[key,ids]:words_){
        hash.update(key.data(),key.size());hash.update("\0",1);
        for(auto id:ids){std::string bytes;append32(bytes,id,4);hash.update(bytes.data(),bytes.size());}
        const std::array<uint8_t,4> zero{};hash.update(zero.data(),zero.size());
    }
    return hash.hexDigest();
}
}
