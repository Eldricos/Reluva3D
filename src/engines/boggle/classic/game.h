// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/engine.h"
namespace reluva::boggle {
class ClassicGame : public Engine {
public:
    ClassicGame(System& system, const GameTarget& target) : sys_(system), target_(target) {}
    Error run() override;
private:
    System& sys_;
    GameTarget target_;
};
} // namespace reluva::boggle
