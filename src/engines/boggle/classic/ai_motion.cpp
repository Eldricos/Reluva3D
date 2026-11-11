// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/ai_motion.h"
#include <algorithm>
#include <cmath>
namespace reluva::boggle {
namespace {
uint64_t travel(ClassicPoint from,ClassicPoint to,unsigned speed,unsigned scale) {
    // Measured movement advances integer screen coordinates every 50 ms.
    // Rounding each step matters: distance/speed alone arrives too early.
    const float scaledSpeed=(float(speed)/1000.f)*(float(scale)/1000.f);
    const float step=float(50)*scaledSpeed;
    uint64_t elapsed=0;
    for(;;) {
        elapsed+=50;const int64_t dx=int64_t(to.x)-from.x,dy=int64_t(to.y)-from.y;
        const double squared=double(dx*dx+dy*dy);
        if(double(step)*step>squared)return elapsed;
        const double distance=std::sqrt(squared);
        const float x=float(double(dx)/distance)*step,y=float(double(dy)/distance)*step;
        const ClassicPoint next{int(double(from.x)+x),int(double(from.y)+y)};
        if(next.x==from.x && next.y==from.y)return 0;
        from=next;
    }
}
bool valid(ClassicPoint point){return point.x>=-10000 && point.x<=10000 && point.y>=-10000 && point.y<=10000;}
}
ComputerGeometry defaultComputerGeometry(size_t side) {
    ComputerGeometry result;
    if(side!=4 && side!=5)return result;
    const int cell=256/int(side);
    for(size_t i=0;i<side*side;++i)result.centers.push_back({24+int(i%side)*cell+cell/2,56+int(i/side)*cell+cell/2});
    result.origin={152,184};result.submit={550,220};return result;
}
Error makeComputerMotion(const AcceptedWord& word,const ComputerGeometry& geometry,ClassicPoint origin,
                         unsigned cubeSpeed,unsigned submitSpeed,const std::vector<uint64_t>& holds,ComputerMotion& out) {
    out={};
    if((geometry.centers.size()!=16 && geometry.centers.size()!=25) || word.path.empty() || word.path.size()>25 ||
       holds.size()!=word.path.size() || !cubeSpeed || !submitSpeed || cubeSpeed>1000 || submitSpeed>1000 ||
       geometry.scale<100 || geometry.scale>4000 || !valid(origin) || !valid(geometry.submit))
        return Error::fail("Invalid Classic computer motion geometry");
    for(auto point:geometry.centers)if(!valid(point))return Error::fail("Invalid Classic computer cube position");
    for(size_t i=0;i<word.path.size();++i)if(word.path[i]>=geometry.centers.size() || holds[i]>1000)
        return Error::fail("Invalid Classic computer motion path");
    ComputerMotion motion;ClassicPoint from=origin;uint64_t time=0;
    for(size_t i=0;i<word.path.size();++i) {
        const auto to=geometry.centers[word.path[i]];
        const auto moving=travel(from,to,cubeSpeed,geometry.scale);
        if(!moving)return Error::fail("Classic computer movement cannot advance at this scale");
        time+=moving;motion.presses.push_back(time);
        time+=((holds[i]+99)/50)*50;motion.releases.push_back(time);from=to;
    }
    const auto moving=travel(from,geometry.submit,submitSpeed,geometry.scale);
    if(!moving)return Error::fail("Classic computer submission movement cannot advance at this scale");
    motion.duration=time+moving;
    out=std::move(motion);return Error::success();
}
} // namespace reluva::boggle
