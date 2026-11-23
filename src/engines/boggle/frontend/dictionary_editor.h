// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/user_dictionary.h"
#include "common/events.h"
namespace reluva::boggle {
class DictionaryEditor {
public:
    Error open(const std::filesystem::path& save,const std::filesystem::path& game);
    bool event(const Event& event); // true after successful Done or Cancel
    void draw(Surface& screen) const;
private:
    UserDictionary user_;
    std::set<std::string> draft_;
    std::vector<std::string> rows_;
    size_t selected_=0,scroll_=0;
    std::string input_,error_;
    bool replace_=true,clearPrompt_=false;
    void refresh();
};
}
