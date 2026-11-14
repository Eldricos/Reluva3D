// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/formats/lexer.h"

#include "common/fs.h"

#include <cstdio>

namespace reluva::idtech3 {

std::string Entity::get(std::string_view key, std::string_view fallback) const {
    const std::string k = fs::toLower(key);
    for (const auto &[name, value] : pairs)
        if (fs::toLower(name) == k)
            return value;
    return std::string(fallback);
}

bool Entity::has(std::string_view key) const {
    const std::string k = fs::toLower(key);
    for (const auto &kv : pairs)
        if (fs::toLower(kv.first) == k)
            return true;
    return false;
}

bool Entity::vector(std::string_view key, float out[3]) const {
    if (!has(key))
        return false;
    const std::string v = get(key);
    return std::sscanf(v.c_str(), "%f %f %f", &out[0], &out[1], &out[2]) == 3;
}

Error parseEntities(std::string_view text, std::vector<Entity> &out) {
    out.clear();
    // The lump is usually NUL-terminated; ignore anything after the NUL.
    if (const size_t nul = text.find('\0'); nul != std::string_view::npos)
        text = text.substr(0, nul);
    Lexer lex(text);
    while (auto tok = lex.next()) {
        if (*tok != "{")
            return Error::fail("Entity lump line " + std::to_string(lex.line()) + ": expected '{', found '" +
                               std::string(*tok) + "'");
        Entity e;
        while (true) {
            auto key = lex.next();
            if (!key)
                return Error::fail("Entity lump ends inside an entity");
            if (*key == "}")
                break;
            auto value = lex.next(false);
            if (!value)
                return Error::fail("Entity lump line " + std::to_string(lex.line()) + ": key '" + std::string(*key) +
                                   "' has no value");
            e.pairs.emplace_back(std::string(*key), std::string(*value));
        }
        out.push_back(std::move(e));
    }
    return Error::success();
}

std::string nextEntityToken(std::string_view s, size_t &i) {
    while (true) {
        while (i < s.size() && static_cast<unsigned char>(s[i]) <= ' ' && s[i] != '\0')
            ++i;
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n')
                ++i;
            continue;
        }
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
            const size_t end = s.find("*/", i + 2);
            i = end == std::string::npos ? s.size() : end + 2;
            continue;
        }
        break;
    }
    if (i >= s.size() || s[i] == '\0')
        return {};
    std::string token;
    if (s[i] == '"') {
        ++i;
        while (i < s.size() && s[i] != '"' && s[i] != '\0')
            token += s[i++];
        if (i < s.size() && s[i] == '"')
            ++i;
        return token;
    }
    while (i < s.size() && static_cast<unsigned char>(s[i]) > ' ')
        token += s[i++];
    return token;
}

} // namespace reluva::idtech3
