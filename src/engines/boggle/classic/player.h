// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <filesystem>
#include <string_view>
namespace reluva::boggle {
// Native human name history, independent of original player/registry data.
bool playerName(std::string_view input,std::string& normalized);
class PlayerProfile {
public:
    static constexpr size_t maximumNames=32;
    Error load(const std::filesystem::path& savePath,const std::filesystem::path& gamePath);
    Error rename(std::string_view name);
    const std::string& name() const {return name_;}
    const std::vector<std::string>& history() const {return history_;}
    bool persistent() const {return !file_.empty();}
private:
    bool loaded_=false;
    std::string name_="PLAYER 1";
    std::vector<std::string> history_{"PLAYER 1"};
    std::filesystem::path file_;
};
} // namespace reluva::boggle
