// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/cursor.h"
#include <algorithm>
namespace reluva::boggle {
ComputerCursorFrame computerCursorFrame(const AcceptedWord& word,const std::vector<ClassicPoint>& centers,
                                        ClassicPoint origin,uint64_t elapsed,uint64_t due,ClassicPoint submitPoint) {
    ComputerCursorFrame frame;
    if(!due || word.path.empty() || word.path.size()>25 || (centers.size()!=16 && centers.size()!=25))return frame;
    for(size_t cube:word.path)if(cube>=centers.size())return frame;
    const uint64_t span=std::min<uint64_t>(due,1000),start=due-span;
    if(elapsed<start)return frame;
    const uint64_t motion=std::max<uint64_t>(1,span*3/4);
    const uint64_t progress=std::min(elapsed-start,motion)*word.path.size();
    frame.active=true;frame.cubes=std::min(word.path.size(),size_t(progress/motion));
    if(frame.cubes==word.path.size()) {
        if(elapsed>=due){frame.point=submitPoint;return frame;}
        const auto from=centers[word.path.back()];
        const uint64_t travel=std::max<uint64_t>(1,span-motion);
        const uint64_t progress=std::min(elapsed-start-motion,travel);
        frame.point={int(from.x+(int64_t(submitPoint.x)-from.x)*int64_t(progress)/int64_t(travel)),
                     int(from.y+(int64_t(submitPoint.y)-from.y)*int64_t(progress)/int64_t(travel))};
        return frame;
    }
    const auto from=frame.cubes?centers[word.path[frame.cubes-1]]:origin;
    const auto to=centers[word.path[frame.cubes]];
    const int64_t fraction=int64_t(progress%motion);
    frame.point={int(from.x+(int64_t(to.x)-from.x)*fraction/int64_t(motion)),
                 int(from.y+(int64_t(to.y)-from.y)*fraction/int64_t(motion))};
    return frame;
}
void drawComputerCursor(Surface& screen,ClassicPoint point) {
    // An independently drawn native arrow. No original cursor bitmap is used.
    static constexpr const char* rows[]={"#", "##", "#.#", "#..#", "#...#", "#....#", "#.....#", "#......#",
        "#.......#", "#....#####", "#..#..#", "#.# #..#", "##   #..#", "#     ##"};
    if(screen.format!=PixelFormat::RGBA32 || point.x>=screen.w || point.y>=screen.h || point.x < -10 || point.y < -14)return;
    for(int y=0;y<14;++y)for(int x=0;rows[y][x];++x) {
        const char pixel=rows[y][x];const int px=point.x+x,py=point.y+y;
        if(pixel==' ' || px<0 || py<0 || px>=screen.w || py>=screen.h)continue;
        auto* out=screen.row(py)+px*4;out[0]=out[1]=out[2]=pixel=='#'?12:255;out[3]=255;
    }
}
} // namespace reluva::boggle
