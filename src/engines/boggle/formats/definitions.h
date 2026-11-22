// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <array>
#include <map>
#include <string_view>
namespace reluva::boggle {
enum class DefinitionMatch { Headword, Variant, Subentry, Inflection, RootDerivative };
struct DefinitionEntry {uint32_t id=0;std::string headword,text;DefinitionMatch match=DefinitionMatch::Headword;};
// Independent reader for the supplied INSO entry-file component. ASCII
// lexical fields only; no inferred stemming, suffix rules or original DLLs.
// lookup folds ASCII input as Boggle's wrapper does; stored fields must match
// that lowercase spelling. entry still exposes all validated article IDs.
class Definitions {
public:
    Error read(const std::vector<uint8_t>& bytes);
    Error entry(uint32_t id,DefinitionEntry& out) const;
    Error lookup(std::string_view word,std::vector<DefinitionEntry>& out) const;
    size_t entries() const {return starts_.empty()?0:starts_.size()-1;}
    size_t headwords() const {return indexes_[0].size();}
    size_t variants() const {return indexes_[1].size();}
    size_t subentries() const {return indexes_[2].size();}
    size_t inflections() const {return indexes_[3].size();}
    size_t derivatives() const {return indexes_[4].size();}
    std::string indexChecksum() const;
private:
    std::vector<uint8_t> bytes_;
    std::vector<std::string> tokens_;
    std::vector<uint64_t> starts_;
    std::array<std::map<std::string,std::vector<uint32_t>>,5> indexes_;
    size_t stream_=0;
    unsigned width_=0,end_=0;
    Error markup(uint64_t& symbol,std::string& out) const;
};
}
