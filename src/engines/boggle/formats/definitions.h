// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <map>
#include <string_view>
namespace reluva::boggle {
struct DefinitionEntry {uint32_t id=0;std::string headword,text;};
// Independent reader for the supplied INSO entry-file component. Exact ASCII
// headword lookup only; no original DLLs, morphology or phonetic-font programs.
class Definitions {
public:
    Error read(const std::vector<uint8_t>& bytes);
    Error entry(uint32_t id,DefinitionEntry& out) const;
    Error lookup(std::string_view word,std::vector<DefinitionEntry>& out) const;
    size_t entries() const {return starts_.empty()?0:starts_.size()-1;}
    size_t headwords() const {return index_.size();}
private:
    std::vector<uint8_t> bytes_;
    std::vector<std::string> tokens_;
    std::vector<uint64_t> starts_;
    std::map<std::string,std::vector<uint32_t>> index_;
    size_t stream_=0;
    unsigned width_=0,end_=0;
    Error markup(uint64_t& symbol,std::string& out) const;
};
}
