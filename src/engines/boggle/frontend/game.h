// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/engine.h"
namespace reluva::boggle {
class FrontendGame : public Engine {
public:
    FrontendGame(System& system, const GameTarget& target) : system_(system), target_(target) {}
    Error run() override;
private:
    System& system_;
    GameTarget target_;
};
} // namespace reluva::boggle
