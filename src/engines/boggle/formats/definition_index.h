// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <map>
#include <string_view>
namespace reluva::boggle {
// The archive's authored word-to-article index, including implicit forms.
// No original search programs or suffix-generation rules are executed.
class DefinitionIndex {
public:
    Error read(const std::vector<uint8_t>& bytes,uint32_t articles,size_t entryStream);
    bool present() const {return present_;}
    size_t words() const {return words_.size();}
    size_t records() const {return records_;}
    size_t links() const {return links_;}
    // Keys are uppercase ASCII, already normalized by Definitions::lookup.
    const std::vector<uint32_t>* find(const std::string& key) const;
    std::string checksum() const;
private:
    std::map<std::string,std::vector<uint32_t>> words_;
    size_t records_=0,links_=0;
    bool present_=false;
};
}
