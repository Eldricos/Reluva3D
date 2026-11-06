// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/turn_clock.h"
#include <algorithm>
#include <limits>
namespace reluva::boggle {
namespace {
uint64_t add(uint64_t a,uint64_t b) {
    const auto limit=std::numeric_limits<uint64_t>::max();return b>limit-a?limit:a+b;
}
}
Error PlayerClocks::reset(size_t count) {
    spent_.clear();active_.reset();since_=last_=0;
    if(!count || count>4)return Error::fail("Classic turn clocks need one to four players");
    spent_.assign(count,0);return Error::success();
}
uint64_t PlayerClocks::observe(uint64_t now) const {last_=std::max(last_,now);return last_;}
uint64_t PlayerClocks::elapsed(size_t player,uint64_t now) const {
    if(player>=spent_.size())return 0;
    const auto time=observe(now);
    return active_ && *active_==player?add(spent_[player],time-since_):spent_[player];
}
void PlayerClocks::stop(uint64_t now) {
    const auto time=observe(now);
    if(active_){spent_[*active_]=add(spent_[*active_],time-since_);active_.reset();}
}
Error PlayerClocks::activate(size_t player,uint64_t now) {
    if(player>=spent_.size())return Error::fail("Invalid Classic clock player");
    stop(now);active_=player;since_=observe(now);return Error::success();
}
}
