// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/pure.h"

#include "common/md4.h"

#include <cstdlib>
#include <set>
#include <sstream>
#include <vector>

namespace reluva::idtech3 {

int32_t pakChecksum(const ZipArchive& pak, std::optional<int32_t> feed) {
    std::vector<uint8_t> words;
    auto put = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            words.push_back(uint8_t(v >> (8 * i)));
    };
    if (feed)
        put(uint32_t(*feed));
    for (const ZipArchive::Entry& e : pak.entries())
        if (e.size > 0)
            put(e.crc);
    const auto d = md4(words.data(), words.size());
    uint32_t value = 0;
    for (int w = 0; w < 4; ++w)
        value ^= uint32_t(d[size_t(4 * w)]) | uint32_t(d[size_t(4 * w + 1)]) << 8 |
                 uint32_t(d[size_t(4 * w + 2)]) << 16 | uint32_t(d[size_t(4 * w + 3)]) << 24;
    return int32_t(value);
}

std::string pureCommand(const SearchSet& files, int32_t feed, const std::string& serverPaks) {
    const auto* cgame = dynamic_cast<const ZipArchive*>(files.find("vm/cgame.qvm"));
    const auto* ui = dynamic_cast<const ZipArchive*>(files.find("vm/ui.qvm"));
    if (!cgame || !ui)
        return {};
    std::set<int32_t> theirs;
    std::istringstream in(serverPaks);
    for (std::string word; in >> word;)
        theirs.insert(int32_t(std::strtoll(word.c_str(), nullptr, 10)));
    std::string out = "cp " + std::to_string(pakChecksum(*cgame, feed)) + " " + std::to_string(pakChecksum(*ui, feed)) +
                      " @";
    int32_t check = feed, count = 0;
    std::set<int32_t> listed;
    for (const Archive* a : files.archives()) {
        const auto* pak = dynamic_cast<const ZipArchive*>(a);
        if (!pak || (!theirs.empty() && !theirs.count(pakChecksum(*pak))))
            continue;
        const int32_t pure = pakChecksum(*pak, feed);
        if (!listed.insert(pure).second)
            continue;
        out += " " + std::to_string(pure);
        check ^= pure;
        ++count;
    }
    check ^= count;
    return out + " " + std::to_string(check);
}

} // namespace reluva::idtech3
