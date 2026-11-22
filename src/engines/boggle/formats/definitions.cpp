// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/definitions.h"
#include "common/md5.h"
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
namespace reluva::boggle {
namespace {
Error bad(){return Error::fail("Invalid or unsupported definition archive");}
uint16_t be16(const std::vector<uint8_t>& b,size_t p){return uint16_t(b[p])<<8|b[p+1];}
uint32_t be32(const std::vector<uint8_t>& b,size_t p){return uint32_t(be16(b,p))<<16|be16(b,p+2);}
uint16_t le16(const std::vector<uint8_t>& b,size_t p){return uint16_t(b[p])|uint16_t(b[p+1])<<8;}
uint32_t le32(const std::vector<uint8_t>& b,size_t p){return uint32_t(le16(b,p))|uint32_t(le16(b,p+2))<<16;}
std::string headword(std::string_view text){
    const auto start=text.find("<WD>");if(start==text.npos)return {};
    text.remove_prefix(start+4);text=text.substr(0,text.find('<'));
    while(!text.empty() && (text.back()==' ' || uint8_t(text.back())==255))text.remove_suffix(1);
    return std::string(text);
}
std::string key(std::string_view word){
    if(word.size()>128)return {};
    std::string result;
    for(unsigned char c:word){if(c==0xb7)continue;if(c>='a'&&c<='z')c-=32;if(c<'A'||c>'Z')return {};result+=char(c);}
    return result.size()<=64?result:std::string{};
}
// Only the literal INF fields within a complete INFL group describe stored
// inflections. Do not treat prose, cross-references or arbitrary tags as keys.
std::vector<std::string> storedForms(std::string_view text){
    std::vector<std::string> forms;
    size_t at=0;
    while((at=text.find("<INFL>",at))!=text.npos){
        at+=6;const auto end=text.find("</INFL>",at);if(end==text.npos)break;
        const auto group=text.substr(at,end-at);size_t p=0;
        while((p=group.find("<INF>",p))!=group.npos){
            p+=5;auto value=group.substr(p);value=value.substr(0,value.find('<'));
            while(!value.empty() && (value.back()==' ' || uint8_t(value.back())==255))value.remove_suffix(1);
            const auto normalized=key(value);
            if(!normalized.empty())forms.push_back(normalized);
        }
        at=end+7;
    }
    std::sort(forms.begin(),forms.end());forms.erase(std::unique(forms.begin(),forms.end()),forms.end());return forms;
}
std::string plain(std::string_view markup){
    std::string result;bool spacing=false;
    auto space=[&](bool line){if(!result.empty() && result.back()!='\n'){if(line){if(result.back()==' ')result.pop_back();result+='\n';}else if(result.back()!=' ')result+=' ';}};
    for(size_t p=0;p<markup.size();){
        const unsigned char c=markup[p++];
        if(c=='<'){
            const auto end=markup.find('>',p);if(end==markup.npos){result+='?';break;}
            const auto tag=markup.substr(p,end-p);const auto name=tag.substr(0,tag.find(' '));
            if(name=="PR" || name=="POS" || name=="DEF" || name=="ETY" || name=="ETYS" || name=="SYN" || name=="INFL" || name=="IDM" || name=="IDMSEG")space(true);
            else if(name!="EM1" && name!="/EM1" && name!="EMON" && name!="EMOFF")space(false);
            p=end+1;continue;
        }
        if(c=='\\'){
            const auto end=markup.find('/',p);if(end!=markup.npos && end-p<=32){
                const auto escape=markup.substr(p,end-p);
                if(escape=="emdash")result+=" -- ";else if(escape=="endash")result+='-';
                else if(escape=="schwa")result+="[schwa]";
                else {result+='[';result+=escape;result+=']';}
                p=end+1;continue;
            }
        }
        if(c==255 || c=='\n' || c=='\r'){space(true);spacing=false;continue;}
        if(c==' ' || c=='\t'){if(!spacing)space(false);spacing=true;continue;}
        spacing=false;
        if(c>=32 && c<127)result+=char(c);
        else if(c==0xb7)result+='-';
        else if(c==0x91 || c==0x92)result+='\'';
        else if(c==0x93 || c==0x94)result+='"';
        else if(c>=128)result+='?';
    }
    while(!result.empty() && (result.back()==' ' || result.back()=='\n'))result.pop_back();
    return result;
}
}
Error Definitions::markup(uint64_t& at,std::string& out) const {
    out.clear();const uint64_t bits=uint64_t(bytes_.size()-stream_)*8;
    while(at<bits/width_){
        const uint64_t bit=at++*width_;unsigned symbol=0;
        for(unsigned i=0;i<width_;++i){const auto b=bit+i;symbol=(symbol<<1)|((bytes_[stream_+size_t(b/8)]>>(7-b%8))&1);}
        if(symbol==end_)return Error::success();
        if(symbol>=tokens_.size() || out.size()+tokens_[symbol].size()>1024*1024)return bad();
        out+=tokens_[symbol];
    }
    out.clear();return bad();
}
Error Definitions::read(const std::vector<uint8_t>& b){
    *this={};if(b.size()<0x600 || b.size()>32*1024*1024 || be32(b,0x400)!=0x00010001 ||
       !std::equal(b.begin()+0x404,b.begin()+0x40e,"Entry file"))return bad();
    const auto count=be32(b,0x468);const auto pageSize=be16(b,0x472),width=be16(b,0x476);
    if(!count || count>200000 || pageSize<2 || pageSize>256 || (pageSize&(pageSize-1)) || width<2 || width>15)return bad();
    struct Section {size_t offset=0,size=0;uint16_t stride=0,count=0;};std::map<unsigned,Section> sections;
    size_t stream=0;
    for(size_t p=0x490;p<0x500;p+=16){
        const auto tag=be16(b,p);const uint64_t off=uint64_t(0x400)+be32(b,p+2);const auto size=be32(b,p+12);
        if(off<0x600 || off>b.size() || size>b.size()-off || sections.contains(tag))return bad();
        sections[tag]={size_t(off),size,be16(b,p+8),be16(b,p+10)};stream=std::max(stream,size_t(off)+size);
    }
    for(unsigned tag:{10,20,30,40,60,80,90})if(!sections.contains(tag))return bad();
    std::vector<std::pair<size_t,size_t>> ranges;for(const auto& [tag,s]:sections)ranges.emplace_back(s.offset,s.offset+s.size);
    std::sort(ranges.begin(),ranges.end());for(size_t i=1;i<ranges.size();++i)if(ranges[i-1].second>ranges[i].first)return bad();
    const auto directory=sections[10],left=sections[20],right=sections[30],pool=sections[60];const unsigned n=1u<<width;
    if(directory.stride!=4 || directory.count!=(count+pageSize)/pageSize || directory.size!=size_t(directory.count)*4 ||
       left.stride!=2 || right.stride!=2 || left.count!=n || right.count!=n || left.size!=n*2 || right.size!=n*2 || stream>=b.size())return bad();
    const unsigned pairs=le16(b,left.offset);if(pairs>=n)return bad();const unsigned direct=n-pairs;
    Definitions next;next.width_=width;next.stream_=stream;next.tokens_.resize(n);
    std::vector<uint8_t> state(n);size_t total=0;unsigned ends=0;
    std::function<bool(unsigned,unsigned)> expand=[&](unsigned i,unsigned depth){
        if(i>=n || depth>64 || state[i]==1)return false;
        if(state[i]==2)return true;
        state[i]=1;
        const auto a=le16(b,left.offset+i*2),c=le16(b,right.offset+i*2);
        if(i<direct){if(a>pool.size || c>pool.size-a)return false;next.tokens_[i].assign(reinterpret_cast<const char*>(b.data()+pool.offset+a),c);}
        else {if(!expand(a,depth+1)||!expand(c,depth+1))return false;next.tokens_[i]=next.tokens_[a]+next.tokens_[c];}
        if(next.tokens_[i].size()>65536 || total+next.tokens_[i].size()>16*1024*1024)return false;
        total+=next.tokens_[i].size();state[i]=2;
        if(i<direct && next.tokens_[i]==std::string(4,char(254))){next.end_=i;++ends;}return true;
    };
    for(unsigned i=0;i<n;++i)if(!expand(i,0))return bad();
    if(ends!=1)return bad();
    next.bytes_=b;uint64_t symbol=0;size_t expanded=0,formLinks=0;std::string text;
    for(uint32_t id=0;id<=count;++id){
        if(id==0 && le32(b,directory.offset)!=symbol)return bad();
        if(id && id%pageSize==0 && le32(b,directory.offset+(id/pageSize)*4)!=symbol-1)return bad();
        next.starts_.push_back(symbol);if(auto e=next.markup(symbol,text))return e;
        expanded+=text.size();if(expanded>256*1024*1024)return bad();
        if(id==0 && !text.empty())return bad();
        const auto normalized=key(headword(text));if(!normalized.empty()){
            auto& matches=next.index_[normalized];if(matches.size()>=64)return bad();matches.push_back(id);
        }
        for(const auto& form:storedForms(text)){
            auto& matches=next.inflections_[form];
            if(matches.size()>=64 || ++formLinks>400000)return bad();
            matches.push_back(id);
        }
    }
    // Bound the combined view as well as each individual index. Exact matches
    // precede stored forms, and the same article is never displayed twice.
    for(const auto& [word,ids]:next.inflections_){
        const auto exact=next.index_.find(word);size_t count=ids.size();
        if(exact!=next.index_.end())for(auto id:exact->second)if(std::find(ids.begin(),ids.end(),id)==ids.end())++count;
        if(count>64)return bad();
    }
    *this=std::move(next);return Error::success();
}
Error Definitions::entry(uint32_t id,DefinitionEntry& out) const {
    out={};if(id==0 || id>=starts_.size())return Error::fail("Definition entry is out of range");
    uint64_t at=starts_[id];std::string text;if(auto e=markup(at,text))return e;
    out.id=id;out.headword=plain(headword(text));out.text=plain(text);return Error::success();
}
Error Definitions::lookup(std::string_view word,std::vector<DefinitionEntry>& out) const {
    out.clear();const auto normalized=key(word);if(normalized.empty())return Error::fail("Definition lookup needs an ASCII English word");
    std::vector<DefinitionEntry> result;
    for(const auto* index:{&index_,&inflections_}){
        const auto found=index->find(normalized);if(found==index->end())continue;
        for(auto id:found->second){
            if(std::any_of(result.begin(),result.end(),[&](const auto& e){return e.id==id;}))continue;
            DefinitionEntry e;if(auto error=entry(id,e))return error;e.inflected=index==&inflections_;result.push_back(std::move(e));
        }
    }
    out=std::move(result);return Error::success();
}
std::string Definitions::indexChecksum() const {
    Md5 hash;
    for(const auto* index:{&index_,&inflections_}){
        hash.update(index==&index_?"headwords":"inflections",index==&index_?9:11);hash.update("\0",1);
        for(const auto& [word,ids]:*index){
            hash.update(word.data(),word.size());hash.update("\0",1);
            for(auto id:ids){const std::array<uint8_t,4> bytes={uint8_t(id>>24),uint8_t(id>>16),uint8_t(id>>8),uint8_t(id)};hash.update(bytes.data(),bytes.size());}
            const std::array<uint8_t,4> end{};hash.update(end.data(),end.size());
        }
    }
    return hash.hexDigest();
}
}
