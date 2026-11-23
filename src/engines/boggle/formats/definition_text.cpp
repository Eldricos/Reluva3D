// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/definition_text.h"
#include <algorithm>
#include <map>
namespace reluva::boggle {
std::string definitionUtf8(uint32_t c){
    std::string s;
    if(c<128)s+=char(c);
    else if(c<2048){s+=char(0xc0|(c>>6));s+=char(0x80|(c&63));}
    else if(c<65536){s+=char(0xe0|(c>>12));s+=char(0x80|((c>>6)&63));s+=char(0x80|(c&63));}
    else {s+=char(0xf0|(c>>18));s+=char(0x80|((c>>12)&63));s+=char(0x80|((c>>6)&63));s+=char(0x80|(c&63));}
    return s;
}
uint32_t definitionCodepoint(std::string_view s,size_t& at){
    if(at>=s.size())return 0;
    const unsigned c=uint8_t(s[at++]);if(c<128)return c;
    unsigned n=0,cp=0,min=0;
    if(c>=0xc2&&c<=0xdf){n=1;cp=c&31;min=128;}
    else if(c>=0xe0&&c<=0xef){n=2;cp=c&15;min=2048;}
    else if(c>=0xf0&&c<=0xf4){n=3;cp=c&7;min=65536;}
    else return 0xfffd;
    if(n>s.size()-at)return 0xfffd;
    for(unsigned i=0;i<n;++i){const unsigned b=uint8_t(s[at]);if((b&0xc0)!=0x80)return 0xfffd;++at;cp=(cp<<6)|(b&63);}
    return cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)?0xfffd:cp;
}
std::string definitionSymbol(std::string_view name){
    static const std::map<std::string,std::string,std::less<>> fixed={
        {"bprime","′"},{"lprime","′"},{"schwa","ə"},{"schwaacute","ə́"},
        {"bdquote","“"},{"edquote","”"},{"bsquote","‘"},{"esquote","’"},
        {"emdash","—"},{"endash","–"},{"deg","°"},{"degree","°"},{"minute","′"},{"sec","″"},{"secmark","″"},
        {"phonth","ṯẖ"},{"phonkh","ḵẖ"},{"phonn","N"},{"phonnj","ny"},{"phond","ð"},{"phonp","þ"},{"thorn","þ"},
        {"ae","æ"},{"aelong","ǣ"},{"oelong","œ̄"},{"oolong","o͞o"},{"oobreve","o͝o"},
        {"irevc","ɔ"},{"irevclong","ɔ̄"},{"irevcgrave","ɔ̀"},
        {"babreve","̮"},{"balong","̱"},
        {"rough","ʽ"},{"smooth","ʼ"},{"plus","+"},{"+","+"},{"minus","−"},{"times","×"},{"botx","×"},
        {"eq","="},{"equiv","≡"},{"sim","∼"},{"plusmn","±"},{"div","÷"},{"lt","<"},{"gt",">"},
        {"root","√"},{"int","∫"},{"function","ƒ"},{"cent","¢"},{"rx","℞"},{"dram","ʒ"},
        {"handr","☞"},{"dagger","†"},{"ddagger","‡"},{"para","¶"},{"flat","♭"},{"natural","♮"},{"sharp","♯"},
        {"bond1","—"},{"bond2","="},{"barule","——"},{"vrule","|"},{"barl","l̄"},{"bari","ī"},{"dbar","đ"},
        {"caret","^"},{"nodoti","ı"},{"ques","¿"},{"b;a","ᵇ∕ₐ"},{"2.3","²∕₃"}
    };
    if(const auto f=fixed.find(name);f!=fixed.end())return f->second;
    const auto colon=name.find(':');if(colon!=name.npos && colon && colon+1<name.size())return std::string(name.substr(0,colon))+"⁄"+std::string(name.substr(colon+1));
    static const std::map<std::string,std::string,std::less<>> accents={
        {"acute","́"},{"grave","̀"},{"breve","̆"},{"long","̄"},{"icflex","̂"},{"cflex","̂"},{"cedil","̧"},
        {"dier","̈"},{"tilde","̃"},{"angst","̊"},{"slash","̸"},{"dot","̇"},{"endot","·"},{"hook","̨"},
        {"lowdot","̣"},{"lowangst","̥"},{"lowibreve","̯"},{"lowangstacute","̥́"},{"longtilde","̄̃"},
        {"longacute","̄́"},{"acutedot","́̇"},{"tildemac","̃̄"},{"bremac","̆̄"},{"acucflex","́̂"},{"cflexlowdot","̣̂"}
    };
    if(const auto a=accents.find(name);a!=accents.end())return "◌"+a->second;
    if(name.size()>1){if(const auto a=accents.find(name.substr(1));a!=accents.end())return std::string(name.substr(0,1))+a->second;}
    if(name.starts_with("gr")){
        auto rest=name.substr(2);bool upper=!rest.empty()&&rest.front()>='A'&&rest.front()<='Z';std::string lower(rest);
        for(auto& c:lower)if(c>='A'&&c<='Z')c+=32;
        static const std::map<std::string,uint32_t,std::less<>> greek={{"alpha",0x3b1},{"beta",0x3b2},{"chi",0x3c7},{"delta",0x3b4},{"epsilon",0x3b5},{"eta",0x3b7},{"iota",0x3b9},{"lambda",0x3bb},{"omicron",0x3bf},{"pi",0x3c0},{"rho",0x3c1},{"sigma",0x3c3},{"sigma1",0x3c3},{"sigma2",0x3c2},{"tau",0x3c4},{"theta",0x3b8},{"upsilon",0x3c5}};
        std::string mark;for(auto suffix:{"acute","grave"})if(lower.ends_with(suffix)){mark=accents.at(suffix);lower.resize(lower.size()-std::string_view(suffix).size());break;}
        if(const auto g=greek.find(lower);g!=greek.end())return definitionUtf8(g->second-(upper?32:0))+mark;
    }
    // Inverted breve is a separate diacritic used in the stored transliterations.
    if(name.size()>1&&name.substr(1)=="ibreve")return std::string(name.substr(0,1))+"̑";
    return {};
}
std::string DefinitionText::plain() const {std::string s;for(const auto& span:spans)s+=span.text;return s;}
Error definitionText(std::string_view markup,DefinitionText& out){
    out={};if(markup.size()>1024*1024)return Error::fail("Definition exceeds text limits");
    DefinitionText next;unsigned style=0,emphasis=0,sense=0,subsense=0;bool pronunciation=false,root=false;std::string reference;
    auto emit=[&](std::string text){if(text.empty())return;if(next.spans.empty()||next.spans.back().style!=(style|emphasis)||next.spans.back().reference!=reference||next.spans.back().root!=root)next.spans.push_back({{},style|emphasis,reference,root});next.spans.back().text+=text;};
    auto space=[&](bool line){if(next.spans.empty())return;const auto c=next.spans.back().text.back();if(c=='\n')return;if(line)emit("\n");else if(c!=' ')emit(" ");};
    for(size_t p=0;p<markup.size();){const unsigned char c=markup[p++];
        if(c=='<'){
            const auto end=markup.find('>',p);if(end==markup.npos)return Error::fail("Unterminated definition tag");
            auto tag=markup.substr(p,end-p);tag=tag.substr(0,tag.find(' '));p=end+1;
            if(tag=="EMON"){space(false);style|=DefinitionItalic;continue;}
            if(tag=="EM1"||tag=="EM3"){emphasis|=DefinitionItalic;continue;}
            if(tag=="EM2"||tag=="EM4"||tag=="EM5"){emphasis|=DefinitionBold;continue;}
            if(tag.starts_with("/EM")||tag=="EMOFF"){emphasis=0;continue;}
            if(tag=="UP"){emphasis|=DefinitionSuper;continue;}if(tag=="/UP"){emphasis&=~DefinitionSuper;continue;}
            if(tag=="UB"){emphasis|=DefinitionSub;continue;}if(tag=="/UB"){emphasis&=~DefinitionSub;continue;}
            reference.clear();root=false;style=0;
            emphasis&=~(DefinitionSuper|DefinitionSub);
            if(tag=="WD"||tag=="IWD"||tag=="RUN"||tag=="IDM"||tag=="NAME"||tag=="PH")style=DefinitionBold;
            if(tag=="POS"||tag=="SPOS"||tag=="LAB"||tag=="LANG"||tag=="LFORM"||tag=="ILL"||tag=="TITLE")style=DefinitionItalic;
            if(tag=="KW"||tag=="IEKW"||tag=="HWREF"||tag=="XREF"||tag=="DRV"){
                auto value=markup.substr(p,markup.find('<',p)-p);while(!value.empty()&&(value.back()==' '||uint8_t(value.back())==255))value.remove_suffix(1);
                DefinitionText label;
                if(!definitionText(value,label))reference=label.plain();
                for(size_t at=0;(at=reference.find("·",at))!=reference.npos;)reference.erase(at,2);
                while(!reference.empty()&&reference.front()==' ')reference.erase(reference.begin());
                if(reference.size()>128)reference.clear();
                root=tag=="IEKW";style=DefinitionBold;
            }
            if(tag=="PSEG"){
                if(pronunciation){emit(")");pronunciation=false;}
                sense=subsense=0;
            }
            if(tag=="PRON"){emit("(");pronunciation=true;}
            if(tag=="DS"){space(true);emit(std::to_string(++sense)+". ");subsense=0;}
            if(tag=="SDS"){space(false);emit(std::string(1,char('a'+std::min(subsense++,25u)))+". ");}
            const bool line=tag=="PR"||(tag=="DEF"&&!sense)||tag=="DEFN"||tag=="ETYSEG"||tag=="ETYS"||tag=="RUNSEG"||tag=="USAGE"||tag=="USEN"||tag=="SYNTX"||tag=="ANTTX"||tag=="IDMSEG"||tag=="DLIST";
            // Wrapper tags delimit fields but carry no printable content.
            if(line)space(true);
            else if(tag!="TX"&&tag!="DEFTX"&&tag!="PRBODY"&&tag!="PRON"&&tag!="ITX"&&tag!="/HWREF"&&tag!="/XREF")space(false);
            continue;
        }
        if(c=='\\'){
            const auto end=markup.find('/',p);if(end==markup.npos||end-p>32)return Error::fail("Invalid definition symbol");
            const auto name=markup.substr(p,end-p);auto symbol=definitionSymbol(name);
            if(symbol.empty()){if(std::find(next.unsupported.begin(),next.unsupported.end(),name)==next.unsupported.end())next.unsupported.emplace_back(name);symbol="["+std::string(name)+"]";}
            const auto previous=emphasis;if(name=="bprime")emphasis|=DefinitionBold;
            emit(symbol);emphasis=previous;p=end+1;continue;
        }
        if(c==255||c=='\n'||c=='\r'){if(pronunciation){emit(")");pronunciation=false;}space(true);continue;}
        if(c==' '||c=='\t'){space(false);continue;}
        if(c>=32)emit(definitionUtf8(c));
        if(next.spans.size()>32768)return Error::fail("Definition exceeds span limits");
    }
    while(!next.spans.empty()){auto& s=next.spans.back().text;while(!s.empty()&&(s.back()==' '||s.back()=='\n'))s.pop_back();if(!s.empty())break;next.spans.pop_back();}
    out=std::move(next);return Error::success();
}
}
