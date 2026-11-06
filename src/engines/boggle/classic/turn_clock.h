// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <optional>
namespace reluva::boggle {
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
