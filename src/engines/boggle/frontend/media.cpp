// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/media.h"
#include "common/audio/mixer.h"
#include "common/font.h"
#include "common/fs.h"
#include <algorithm>
namespace reluva::boggle {
void MenuMotion::activate(uint64_t now){frames_={};cycles_={};carry_={};last_=now;hovered_=-1;intro_=counts_[2]>1;focused_=true;}
void MenuMotion::update(uint64_t now,int hovered,bool focused){
    const auto elapsed=now>=last_?now-last_:0;last_=now;
    constexpr uint64_t intervals[]={90,100,100,100,66};
    for(size_t i=0;i<5;++i)if(playing(i) && counts_[i]){
        // Bound arithmetic even for an arbitrarily large host-clock jump.
        const auto steps=elapsed/intervals[i]+(elapsed%intervals[i]+carry_[i])/intervals[i];
        carry_[i]=(elapsed%intervals[i]+carry_[i])%intervals[i];
        if(i==2 && intro_){
            const auto end=std::min<size_t>(20,counts_[i]-1);
            if(steps>=end-std::min(frames_[i],end)){frames_[i]=end;intro_=false;carry_[i]=0;}
            else frames_[i]+=size_t(steps);
        }else{
            cycles_[i]+=steps/counts_[i]+(frames_[i]+steps%counts_[i])/counts_[i];
            frames_[i]=(frames_[i]+steps%counts_[i])%counts_[i];
        }
    }
    if(hovered<0 || hovered>=5)hovered=-1;
    if(hovered!=hovered_ && hovered>=0 && !(hovered==2 && intro_)){
        frames_[size_t(hovered)]=0;carry_[size_t(hovered)]=0;++cycles_[size_t(hovered)];
    }
    hovered_=hovered;focused_=focused;
}
Error MenuAudio::open(const Resources& resources,const std::filesystem::path& root,bool enabled){
    stop();sounds_={};if(!enabled)return Error::success();
    const char* names[]={"GameSel","ClsRoll","BrkRoll","BatRoll","SpcRoll","TetRoll"};
    for(size_t i=0;i<6;++i){const auto path=std::string("Waves/")+names[i]+".wav";
        if(!fs::findFile(root,path))continue;
        auto sound=std::make_shared<audio::Sound>();if(auto error=resources.sound(path,*sound))return error;sounds_[i]=std::move(sound);
    }return Error::success();
}
void MenuAudio::enter(){
    stop();if(std::none_of(sounds_.begin(),sounds_.end(),[](const auto& s){return bool(s);}))return;
    active_=!audio::startMixerOutput(system_,mixer_);if(!active_){system_.stopAudio();return;}
    if(sounds_[0])entry_=mixer_.play(sounds_[0],audio::SoundType::Effects);
}
void MenuAudio::stop(){if(active_)system_.stopAudio();active_=false;mixer_.stopAll();handles_={};entry_=0;playing_={};cycles_={};}
void MenuAudio::update(const MenuMotion& motion){
    if(!active_)return;
    mixer_.setPaused(entry_,!motion.focused());
    for(size_t i=0;i<5;++i){const bool play=motion.playing(i);
        if(!play || (!playing_[i] || cycles_[i]!=motion.cycle(i))){
            mixer_.stop(handles_[i]);handles_[i]=0;
            if(play && sounds_[i+1])handles_[i]=mixer_.play(sounds_[i+1],audio::SoundType::Effects,audio::kMaxVolume,0,i==1||i==3||i==4);
        }
        playing_[i]=play;cycles_[i]=motion.cycle(i);
    }
}
void FrontendMedia::activate(uint64_t now){
    std::array<size_t,5> counts{};for(size_t i=0;i<5;++i)counts[i]=buttons_[i].frames.size();
    motion_.counts(counts);motion_.activate(now);controlled_=true;
}
namespace {
class MovieOutput {
public:
    explicit MovieOutput(System& system):system_(system),mixer_(system.audioRate()){}
    ~MovieOutput(){if(active_)system_.stopAudio();}
    void start(const audio::Sound& sound) {
        if(sound.samples.empty())return;
        active_=!audio::startMixerOutput(system_,mixer_);
        if(!active_){system_.stopAudio();return;}
        mixer_.play(std::make_shared<audio::Sound>(sound),audio::SoundType::Music);
    }
private:
    System& system_;audio::Mixer mixer_;bool active_=false;
};
}
Error FrontendMedia::open(const Resources& resources,const std::filesystem::path& root,bool enabled) {
    buttons_={};motion_={};controlled_=false;if(!enabled)return Error::success();
    for(size_t i=0;i<menuMovieNames.size();++i) {
        const auto path=std::string("Anims/")+menuMovieNames[i]+".avi";
        if(!fs::findFile(root,path))continue;
        if(auto err=resources.movie(path,buttons_[i]))return err;
        const auto& movie=buttons_[i];
        if(movie.width!=100 || movie.height!=100 || movie.frames.size()>120 || !movie.soundtrack.samples.empty())
            return Error::fail("Unsupported game-selection button movie");
    }
    return Error::success();
}
bool FrontendMedia::button(Surface& screen,size_t index,int x,int y,uint64_t elapsed) const {
    if(index>=buttons_.size() || buttons_[index].frames.empty())return false;
    const auto& movie=buttons_[index];image::blit(screen,movie.frames[controlled_?motion_.frame(index):movie.frameAt(elapsed,true)],x,y);return true;
}
bool FrontendMedia::buttonFit(Surface& screen,size_t index,SelectionRect bounds,uint64_t elapsed) const {
    if(index>=buttons_.size() || buttons_[index].frames.empty())return false;
    if(bounds.w<=0 || bounds.h<=0 || screen.format!=PixelFormat::RGBA32 || screen.w<=0 || screen.h<=0 ||
       screen.w>8192 || screen.h>8192 || screen.pitch<screen.w*4 || screen.pixels.size()<size_t(screen.pitch)*screen.h)return false;
    const auto& movie=buttons_[index];const auto& frame=movie.frames[controlled_?motion_.frame(index):movie.frameAt(elapsed,true)];
    // The supported clips are square. Fit without painting outside the target,
    // including when a caller supplies a partly off-screen rectangle.
    const int side=std::min({bounds.w,bounds.h,8192});const int64_t left=int64_t(bounds.x)+(int64_t(bounds.w)-side)/2,top=int64_t(bounds.y)+(int64_t(bounds.h)-side)/2;
    for(int y=int(std::clamp<int64_t>(top,0,screen.h));y<std::min<int64_t>(screen.h,top+side);++y)
        for(int x=int(std::clamp<int64_t>(left,0,screen.w));x<std::min<int64_t>(screen.w,left+side);++x){
            const auto* src=&frame.rgba[(size_t((y-top)*frame.h/side)*frame.w+size_t((x-left)*frame.w/side))*4];
            auto* dst=screen.row(y)+x*4;std::copy_n(src,4,dst);
        }
    return true;
}
namespace {
enum class MovieExit { Completed, Skipped, Quit };
Error playIntroClip(System& system,const Resources& resources,const char* path,int width,int height,MovieExit& result) {
    result=MovieExit::Completed;
    video::AviCinepak movie;
    if(auto err=resources.movie(path,movie))return err;
    if(movie.width!=width || movie.height!=height)return Error::fail("Unsupported intro dimensions");
    Surface screen;screen.create(640,480,PixelFormat::RGBA32);
    MovieOutput output(system);output.start(movie.soundtrack);
    const auto started=system.getMillis();
    for(;;) {
        const auto now=system.getMillis(),elapsed=now>=started ? now-started : 0;
        bool done=elapsed>=movie.durationMillis();Event event;
        // Consume the batch so skipping cannot also select a game.
        while(system.pollEvent(event)) {
            if(event.type==EventType::Quit || event.type==EventType::ReturnToLauncher){result=MovieExit::Quit;return Error::success();}
            if(event.type==EventType::KeyDown && !event.repeat && event.key==key::Escape){result=MovieExit::Skipped;done=true;}
        }
        if(done)return Error::success();
        std::fill(screen.pixels.begin(),screen.pixels.end(),0);
        for(size_t i=3;i<screen.pixels.size();i+=4)screen.pixels[i]=255;
        const auto& frame=movie.frames[movie.frameAt(elapsed)];
        const int left=(640-width*2)/2,top=(480-height*2)/2;
        for(int y=0;y<height*2;++y)for(int x=0;x<width*2;++x)
            std::copy_n(frame.pixel(x/2,y/2),4,screen.row(y+top)+(x+left)*4);
        font::drawText(screen,20,456,"ESC: SKIP",{230,230,245});
        system.updateScreen(screen);system.delayMillis(10);
    }
}
MovieExit introGap(System& system) {
    Surface screen;screen.create(640,480,PixelFormat::RGBA32);
    for(size_t i=3;i<screen.pixels.size();i+=4)screen.pixels[i]=255;
    font::drawText(screen,20,456,"ESC: SKIP",{230,230,245});
    const auto started=system.getMillis();
    for(;;) {
        const auto now=system.getMillis();bool skipped=false;Event event;
        while(system.pollEvent(event)) {
            if(event.type==EventType::Quit || event.type==EventType::ReturnToLauncher)return MovieExit::Quit;
            if(event.type==EventType::KeyDown && !event.repeat && event.key==key::Escape)skipped=true;
        }
        if(skipped)return MovieExit::Skipped;
        if(now>=started && now-started>=1000)return MovieExit::Completed;
        system.updateScreen(screen);system.delayMillis(10);
    }
}
}
Error FrontendMedia::intro(System& system,const Resources& resources,const std::filesystem::path& root,bool& quit) {
    quit=false;
    const bool hasPublisher=bool(fs::findFile(root,"Anims/Hasbro.avi"));
    const bool hasGame=bool(fs::findFile(root,"Anims/INTRO.AVI"));
    MovieExit result=MovieExit::Completed;
    if(hasPublisher) {
        if(auto err=playIntroClip(system,resources,"Anims/Hasbro.avi",320,240,result))return err;
        if(result==MovieExit::Completed && hasGame)result=introGap(system);
    }
    if(result==MovieExit::Completed && hasGame) {
        if(auto err=playIntroClip(system,resources,"Anims/INTRO.AVI",300,180,result))return err;
    }
    quit=result==MovieExit::Quit;return Error::success();
}
} // namespace reluva::boggle
