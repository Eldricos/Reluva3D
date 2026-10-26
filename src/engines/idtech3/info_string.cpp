// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/info_string.h"

#include "common/fs.h"

namespace reluva::idtech3 {

std::vector<std::pair<std::string, std::string>> infoPairs(const std::string& info) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t i = 0;
    while (i < info.size()) {
        if (info[i] == '\\')
            ++i;
        const size_t keyEnd = info.find('\\', i);
        if (keyEnd == std::string::npos)
            break;
        const size_t valueEnd = info.find('\\', keyEnd + 1);
        out.emplace_back(info.substr(i, keyEnd - i),
                         info.substr(keyEnd + 1, valueEnd == std::string::npos ? std::string::npos
                                                                                : valueEnd - keyEnd - 1));
        if (valueEnd == std::string::npos)
            break;
        i = valueEnd;
    }
    return out;
}

std::string infoValue(const std::string& info, const std::string& key) {
    const std::string k = fs::toLower(key);
    for (const auto& [name, value] : infoPairs(info))
        if (fs::toLower(name) == k)
            return value;
    return {};
}

void setInfoValue(std::string& info, const std::string& key, const std::string& value) {
    auto clean = [](std::string s) {
        std::string out;
        for (char c : s)
            if (c != '\\' && c != '"' && c != ';')
                out.push_back(c);
        return out;
    };
    const std::string k = clean(key), v = clean(value), lower = fs::toLower(k);
    std::string rebuilt;
    for (const auto& [name, old] : infoPairs(info))
        if (fs::toLower(name) != lower)
            rebuilt += "\\" + name + "\\" + old;
    if (!v.empty())
        rebuilt += "\\" + k + "\\" + v;
    info = std::move(rebuilt);
}

} // namespace reluva::idtech3
