// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/tune.h"

#include "common/fs.h"

#include <cstdlib>

namespace reluva::age {

const TuneNode* TuneNode::child(std::string_view n) const {
    const std::string key = fs::toLower(n);
    for (const TuneNode& c : children)
        if (fs::toLower(c.name) == key)
            return &c;
    return nullptr;
}

float TuneNode::number(std::string_view n, size_t i, float fallback) const {
    const TuneNode* c = child(n);
    if (!c || i >= c->values.size())
        return fallback;
    char* end = nullptr;
    const float v = std::strtof(c->values[i].c_str(), &end);
    return end == c->values[i].c_str() ? fallback : v;
}

Error parseTune(std::string_view text, TuneNode& root) {
    root = TuneNode{};
    std::vector<TuneNode*> open{&root};
    size_t lineNo = 0;
    while (!text.empty()) {
        const size_t eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
        ++lineNo;
        std::vector<std::string> words;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
                ++i;
            const size_t start = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
                ++i;
            if (i > start)
                words.emplace_back(line.substr(start, i - start));
        }
        if (words.empty() || (lineNo == 1 && words[0] == "type:"))
            continue;
        if (words.size() == 1 && words[0] == "}") {
            if (open.size() == 1)
                return Error::fail("line " + std::to_string(lineNo) + ": } without a block");
            open.pop_back();
            continue;
        }
        TuneNode node;
        node.name = words[0];
        const bool block = words.back() == "{";
        node.values.assign(words.begin() + 1, words.end() - (block ? 1 : 0));
        open.back()->children.push_back(std::move(node));
        if (block)
            open.push_back(&open.back()->children.back());
    }
    if (open.size() != 1)
        return Error::fail("a block isn't closed");
    return Error::success();
}

} // namespace reluva::age
