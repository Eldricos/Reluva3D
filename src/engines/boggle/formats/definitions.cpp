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
std::string headword(std::string_view text,std::string_view marker="<WD>"){
    const auto start=text.find(marker);if(start==text.npos)return {};
    text.remove_prefix(start+marker.size());text=text.substr(0,text.find('<'));
    while(!text.empty() && (text.back()==' ' || uint8_t(text.back())==255))text.remove_suffix(1);
    return std::string(text);
}
std::string key(std::string_view word){
    if(word.size()>128)return {};
    std::string result;
    for(unsigned char c:word){if(c==0xb7)continue;if(c>='a'&&c<='z')c-=32;if(c<'A'||c>'Z')return {};result+=char(c);}
    return result.size()<=64?result:std::string{};
}
std::string fieldKey(std::string_view value,bool stress){
    while(!value.empty() && (value.front()==' ' || uint8_t(value.front())==255))value.remove_prefix(1);
    while(!value.empty() && (value.back()==' ' || uint8_t(value.back())==255))value.remove_suffix(1);
    // Boggle lowercases the query before the reference library's case-sensitive
    // lookup. Match each stored field's spelling, not its parent article's case.
    if(std::any_of(value.begin(),value.end(),[](unsigned char c){return c>='A' && c<='Z';}))return {};
    if(!stress)return key(value);
    std::string letters;
    for(size_t p=0;p<value.size();){
        if(value.substr(p).starts_with("\\bprime/") || value.substr(p).starts_with("\\lprime/"))p+=8;
        else {letters+=value[p++];if(letters.size()>128)return {};}
    }
    return key(letters);
}
void unique(std::vector<std::string>& keys){
    std::sort(keys.begin(),keys.end());keys.erase(std::unique(keys.begin(),keys.end()),keys.end());
}
std::vector<std::string> fields(std::string_view text,std::string_view marker,bool stress=false,bool skipFirst=false){
    std::vector<std::string> keys;size_t p=0;
    while((p=text.find(marker,p))!=text.npos){
        p+=marker.size();if(skipFirst){skipFirst=false;continue;}
        auto value=text.substr(p);value=value.substr(0,value.find('<'));
        const auto normalized=fieldKey(value,stress);if(!normalized.empty())keys.push_back(normalized);
    }
    unique(keys);return keys;
}
std::vector<std::string> alternateFields(std::string_view text){
    std::string outside;size_t at=0;
    while(true){
        const auto group=text.find("<INFL>",at);
        if(group==text.npos){outside+=text.substr(at);break;}
        outside+=text.substr(at,group-at);outside+="<>";
        const auto end=text.find("</INFL>",group+6);if(end==text.npos)break;
        at=end+7;
    }
    return fields(outside,"<WD>",false,true);
}
// Only the literal INF fields within a complete INFL group describe stored
// inflections. Do not treat prose, cross-references or arbitrary tags as keys.
std::vector<std::string> storedForms(std::string_view text){
    std::vector<std::string> forms;
    size_t at=0;
    while((at=text.find("<INFL>",at))!=text.npos){
        at+=6;const auto end=text.find("</INFL>",at);if(end==text.npos)break;
        for(const auto marker:{"<INF>","<WD>"}){
            const auto group=fields(text.substr(at,end-at),marker);forms.insert(forms.end(),group.begin(),group.end());
        }
        at=end+7;
    }
    unique(forms);return forms;
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
    next.bytes_=b;uint64_t symbol=0;size_t expanded=0,indexLinks=0;std::string text;
    for(uint32_t id=0;id<=count;++id){
        if(id==0 && le32(b,directory.offset)!=symbol)return bad();
        if(id && id%pageSize==0 && le32(b,directory.offset+(id/pageSize)*4)!=symbol-1)return bad();
        next.starts_.push_back(symbol);if(auto e=next.markup(symbol,text))return e;
        expanded+=text.size();if(expanded>256*1024*1024)return bad();
        if(id==0 && !text.empty())return bad();
        const bool root=text.starts_with("<IWD>");next.roots_.push_back(root);
        next.primary_.push_back(root?std::string{}:key(headword(text)));
        DefinitionText name;
        if(auto e=definitionText(headword(text,root?"<IWD>":"<WD>"),name))return e;
        auto spelling=name.plain();
        for(size_t p=0;(p=spelling.find("·",p))!=spelling.npos;)spelling.erase(p,2);
        if(!spelling.empty())(root?next.rootNames_:next.references_)[spelling].push_back(id);
        const std::array<std::vector<std::string>,5> keys={
            std::vector<std::string>{fieldKey(headword(text),false)},alternateFields(text),
            fields(text,"<RUN>",true),storedForms(text),text.starts_with("<IWD>")?fields(text,"<DRV>"):std::vector<std::string>{}};
        for(size_t i=0;i<keys.size();++i)for(const auto& word:keys[i])if(!word.empty()){
            auto& matches=next.indexes_[i][word];
            if(matches.size()>=64 || ++indexLinks>400000)return bad();
            matches.push_back(id);
        }
    }
    // Bound the combined view as well as each individual index. Exact matches
    // precede variants/subentries, inflections and roots. Deduplicate articles.
    for(const auto& index:next.indexes_)for(const auto& [word,ids]:index){
        std::vector<uint32_t> combined;
        for(const auto& other:next.indexes_)if(const auto match=other.find(word);match!=other.end())for(auto id:match->second){
            if(std::find(combined.begin(),combined.end(),id)==combined.end())combined.push_back(id);
            if(combined.size()>64)return bad();
        }
    }
    if(auto error=next.authored_.read(b,count,stream))return error;
    *this=std::move(next);return Error::success();
}
Error Definitions::entry(uint32_t id,DefinitionEntry& out) const {
    out={};if(id==0 || id>=starts_.size())return Error::fail("Definition entry is out of range");
    uint64_t at=starts_[id];std::string text;if(auto e=markup(at,text))return e;
    out.id=id;out.headword=plain(headword(text,text.starts_with("<IWD>")?"<IWD>":"<WD>"));out.text=plain(text);
    if(auto error=definitionText(text,out.formatted))return error;
    // KW also labels etymological fragments and aliases with no article.
    // Keep their printed text, but offer navigation only to validated targets.
    for(auto& span:out.formatted.spans)if(!span.reference.empty()&&!hasReference(span.reference,span.root)){
        span.reference.clear();span.root=false;
    }
    return Error::success();
}
Error Definitions::lookup(std::string_view word,std::vector<DefinitionEntry>& out) const {
    out.clear();const auto normalized=key(word);if(normalized.empty())return Error::fail("Definition lookup needs an ASCII English word");
    std::vector<DefinitionEntry> result;
    if(authored_.present()){
        const auto* ids=authored_.find(normalized);if(!ids)return Error::success();
        for(auto id:*ids){
            DefinitionEntry e;if(auto error=entry(id,e))return error;
            e.match=roots_[id]?DefinitionMatch::RootDerivative:DefinitionMatch::RelatedForm;
            for(size_t i=0;i<indexes_.size();++i){
                const auto found=indexes_[i].find(normalized);
                if(found!=indexes_[i].end() && std::find(found->second.begin(),found->second.end(),id)!=found->second.end()){e.match=DefinitionMatch(i);break;}
            }
            result.push_back(std::move(e));
        }
        // The displayed primary headword (case folded), rather than alternate
        // WD fields embedded in a sense or inflection, determines first rank.
        // Keep the authored article order for every other match.
        std::stable_sort(result.begin(),result.end(),[&](const auto& a,const auto& b){
            return (primary_[a.id]==normalized)>(primary_[b.id]==normalized);
        });
        out=std::move(result);return Error::success();
    }
    for(size_t i=0;i<indexes_.size();++i){
        const auto found=indexes_[i].find(normalized);if(found==indexes_[i].end())continue;
        for(auto id:found->second){
            if(std::any_of(result.begin(),result.end(),[&](const auto& e){return e.id==id;}))continue;
            DefinitionEntry e;if(auto error=entry(id,e))return error;e.match=DefinitionMatch(i);result.push_back(std::move(e));
        }
    }
    out=std::move(result);return Error::success();
}
bool Definitions::hasReference(std::string_view spelling,bool root) const {
    if(spelling.empty()||spelling.size()>128)return false;
    if(root)return rootNames_.contains(std::string(spelling));
    if(authored_.reference(spelling)||references_.contains(std::string(spelling)))return true;
    const auto normalized=key(spelling);if(normalized.empty())return false;
    if(authored_.present())return authored_.find(normalized)!=nullptr;
    return std::any_of(indexes_.begin(),indexes_.end(),[&](const auto& index){return index.contains(normalized);});
}
Error Definitions::reference(std::string_view spelling,bool root,std::vector<DefinitionEntry>& out) const {
    out.clear();if(spelling.empty()||spelling.size()>128)return Error::fail("Invalid definition reference");
    const auto& names=root?rootNames_:references_;
    const auto named=names.find(std::string(spelling));
    const auto* ids=root?(named==names.end()?nullptr:&named->second):authored_.reference(spelling);
    if(!ids && named!=names.end())ids=&named->second;
    if(!ids && !root)return lookup(spelling,out);
    if(!ids)return Error::success();
    for(auto id:*ids){DefinitionEntry e;if(auto error=entry(id,e))return error;e.match=root?DefinitionMatch::RootDerivative:DefinitionMatch::Headword;out.push_back(std::move(e));}
    return Error::success();
}
std::string Definitions::indexChecksum() const {
    Md5 hash;
    constexpr std::array<std::string_view,5> names={"headwords","variants","subentries","inflections","derivatives"};
    for(size_t i=0;i<indexes_.size();++i){
        hash.update(names[i].data(),names[i].size());hash.update("\0",1);
        for(const auto& [word,ids]:indexes_[i]){
            hash.update(word.data(),word.size());hash.update("\0",1);
            for(auto id:ids){const std::array<uint8_t,4> bytes={uint8_t(id>>24),uint8_t(id>>16),uint8_t(id>>8),uint8_t(id)};hash.update(bytes.data(),bytes.size());}
            const std::array<uint8_t,4> end{};hash.update(end.data(),end.size());
        }
    }
    if(authored_.present()){const auto sum=authored_.checksum();hash.update(sum.data(),sum.size());}
    return hash.hexDigest();
}
}
