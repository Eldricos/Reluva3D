// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/engine.h"
namespace reluva::boggle {
class ClassicGame : public Engine {
public:
    ClassicGame(System& system, const GameTarget& target, bool fromMenu = false, std::vector<std::string> players = {})
        : sys_(system), target_(target), players_(std::move(players)), fromMenu_(fromMenu) {}
    Error run() override;
    bool returnToMenu() const { return returnToMenu_; }
private:
    System& sys_;
    GameTarget target_;
    std::vector<std::string> players_;
    bool fromMenu_ = false, returnToMenu_ = false;
};
} // namespace reluva::boggle
