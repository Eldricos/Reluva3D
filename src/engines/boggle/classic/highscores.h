// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/options.h"
#include <filesystem>
#include <string_view>
namespace reluva::boggle {
struct HighScore {
    ClassicVariant variant=ClassicVariant::Original;
    uint64_t duration=180000;
    int score=0;
    std::string name;
    bool operator==(const HighScore&) const = default;
};
// Native personal bests: ten names across Classic. Original score files
// are neither read nor modified. A failed write leaves the old table intact.
class HighScores {
public:
    Error load(const std::filesystem::path& savePath,const std::filesystem::path& gamePath);
    Error record(ClassicVariant variant,uint64_t duration,int score,std::string_view name);
    std::vector<HighScore> table() const;
    bool persistent() const {return !file_.empty();}
private:
    bool loaded_=false;
    std::filesystem::path file_;
    std::vector<HighScore> rows_;
};
} // namespace reluva::boggle
