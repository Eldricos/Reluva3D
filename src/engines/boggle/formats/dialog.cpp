// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/dialog.h"
namespace reluva::boggle {
namespace {
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& bytes):bytes_(bytes){}
    bool good=true;
    uint8_t byte(){if(pos_>=bytes_.size()){good=false;return 0;}return bytes_[pos_++];}
    uint16_t word(){const auto a=byte(),b=byte();return uint16_t(a|uint16_t(b)<<8);}
    uint32_t integer(){const auto a=word(),b=word();return uint32_t(a)|uint32_t(b)<<16;}
    int coordinate(){const auto n=word();return n<32768?int(n):int(n)-65536;}
    DialogRect rect(){DialogRect r;r.x=coordinate();r.y=coordinate();r.w=coordinate();r.h=coordinate();return r;}
    void align(){const size_t padding=(4-pos_%4)%4;if(padding>bytes_.size()-pos_)good=false;else pos_+=padding;}
    DialogField field(){
        DialogField result;uint16_t n=word();
        if(n==0xffff){result.ordinal=true;result.id=word();return result;}
        while(good && n){if(result.text.size()>=4096){good=false;break;}result.text.push_back(char16_t(n));n=word();}
        for(size_t i=0;i<result.text.size();++i){const auto c=result.text[i];
            if(c>=0xd800 && c<=0xdbff){if(++i>=result.text.size() || result.text[i]<0xdc00 || result.text[i]>0xdfff)good=false;}
            else if(c>=0xdc00 && c<=0xdfff)good=false;
        }
        return result;
    }
    std::u16string string(){auto f=field();if(f.ordinal)good=false;return std::move(f.text);}
    std::vector<uint8_t> data(size_t count){
        if(count>bytes_.size()-pos_){good=false;return {};}
        std::vector<uint8_t> out(bytes_.begin()+pos_,bytes_.begin()+pos_+count);pos_+=count;return out;
    }
    bool finished(){if(bytes_.size()-pos_>3)return false;while(pos_<bytes_.size())if(byte())return false;return good;}
private:
    const std::vector<uint8_t>& bytes_;
    size_t pos_=0;
};
}
Error readDialog(const std::vector<uint8_t>& bytes,DialogTemplate& out){
    out={};auto fail=[] {return Error::fail("Unsupported or malformed Windows dialog template");};
    if(bytes.size()<18 || bytes.size()>1024*1024)return fail();
    DialogTemplate dialog;Reader r(bytes);
    dialog.extended=bytes[2]==0xff && bytes[3]==0xff;
    if(dialog.extended){if(r.word()!=1 || r.word()!=0xffff)return fail();dialog.helpId=r.integer();dialog.extendedStyle=r.integer();dialog.style=r.integer();}
    else {dialog.style=r.integer();dialog.extendedStyle=r.integer();}
    const size_t count=r.word();if(count>1024)return fail();dialog.rect=r.rect();
    dialog.menu=r.field();dialog.windowClass=r.field();dialog.title=r.string();dialog.hasFont=(dialog.style&0x40)!=0;
    if(dialog.hasFont){dialog.pointSize=r.word();if(dialog.extended){dialog.weight=r.word();dialog.italic=r.byte();dialog.charset=r.byte();}dialog.typeface=r.string();}
    for(size_t i=0;r.good && i<count;++i){
        r.align();DialogControl control;
        if(dialog.extended){control.helpId=r.integer();control.extendedStyle=r.integer();control.style=r.integer();}
        else {control.style=r.integer();control.extendedStyle=r.integer();}
        control.rect=r.rect();control.id=dialog.extended?r.integer():r.word();control.windowClass=r.field();control.title=r.field();
        size_t extra=r.word();
        if(extra && !dialog.extended){if(extra<2)return fail();extra-=2;}
        control.creationData=r.data(extra);dialog.controls.push_back(std::move(control));
    }
    if(!r.good || dialog.controls.size()!=count || !r.finished())return fail();
    out=std::move(dialog);return Error::success();
}
} // namespace reluva::boggle
