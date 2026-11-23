// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/formats/definitions.h"
#include "common/events.h"
#include <optional>
namespace reluva::boggle {
class DefinitionView {
public:
    void open(std::string word,std::vector<DefinitionEntry> entries,std::string error={},const Definitions* dictionary=nullptr);
    bool event(const Event& event);
    void draw(Surface& screen) const;
private:
    struct Glyph {uint32_t cp=0;unsigned style=0;int x=0;size_t line=0;std::string reference;bool root=false;};
    struct Page {std::string word,error;std::vector<DefinitionEntry> entries;size_t selected=0,scroll=0;};
    std::string word_,error_;
    std::vector<DefinitionEntry> entries_;
    std::vector<Glyph> glyphs_;
    std::vector<Page> back_;
    const Definitions* dictionary_=nullptr;
    size_t selected_=0,scroll_=0,lines_=0;
    std::optional<size_t> referenceSelection_;
    void wrap();
    void follow(const std::string& word,bool root);
};
}
