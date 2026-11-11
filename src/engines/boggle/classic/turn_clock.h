// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <optional>
namespace reluva::boggle {
// A monotonic gameplay clock. Inactivity freezes transitions and every player's
// budget together; duplicate focus notifications do not change elapsed time.
class ClassicClock {
public:
    uint64_t now(uint64_t wall);
    void focus(bool active,uint64_t wall) { now(wall);paused_=!active; }
    bool paused() const { return paused_; }
private:
    bool initialized_=false,paused_=false;
    uint64_t wall_=0,time_=0;
};
// Active time only, independent of animation, handoff and other players' time.
// Invalid/unconfigured elapsed queries return zero. Backward clock readings
// cannot refund time; elapsed totals saturate instead of wrapping.
class PlayerClocks {
public:
    Error reset(size_t count);
    Error activate(size_t player,uint64_t now);
    void stop(uint64_t now);
    uint64_t elapsed(size_t player,uint64_t now) const;
    std::optional<size_t> active() const {return active_;}
private:
    uint64_t observe(uint64_t now) const;
    std::vector<uint64_t> spent_;
    std::optional<size_t> active_;
    uint64_t since_=0;
    mutable uint64_t last_=0;
};
}
