// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/frontend/media.h"
#include "common/audio/mixer.h"
#include "common/font.h"
#include "common/fs.h"
#include <algorithm>
namespace reluva::boggle {
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
    buttons_={};if(!enabled)return Error::success();
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
    const auto& movie=buttons_[index];image::blit(screen,movie.frames[movie.frameAt(elapsed,true)],x,y);return true;
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
