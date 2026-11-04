// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace reluva::boggle {
struct WordHistoryView {size_t begin=0,end=0,maximum=0;};
// Scroll is the number of rows back from the newest view, rather than an
// iterator into a list that dictionary review can change.
inline WordHistoryView wordHistoryView(size_t count,size_t visible,size_t scroll) {
    if(!visible)return {count,count,0};
    const size_t maximum=count>visible?count-visible:0;
    const size_t begin=maximum-std::min(scroll,maximum);
    return {begin,begin+std::min(visible,count-begin),maximum};
}
inline size_t scrollWordHistory(size_t scroll,size_t count,size_t visible,int64_t rows) {
    const size_t maximum=wordHistoryView(count,visible,0).maximum;
    scroll=std::min(scroll,maximum);
    if(rows>=0)return scroll+size_t(std::min<uint64_t>(uint64_t(rows),maximum-scroll));
    const uint64_t magnitude=uint64_t(-(rows+1))+1;
    return scroll-size_t(std::min<uint64_t>(magnitude,scroll));
}
struct HistoryButton {
    int x,y,w,h;
    bool contains(int px,int py) const {return px>=x && py>=y && px<x+w && py<y+h;}
};
inline constexpr HistoryButton historyOlder{768,448,24,22},historyNewer{768,472,24,22};
} // namespace reluva::boggle
