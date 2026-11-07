// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/pure.h"

#include "common/md4.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
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

bool pureAnswerValid(const std::vector<std::string>& words, int32_t cgame, int32_t ui,
                     const std::set<int32_t>& serverPaks, int32_t feed) {
    size_t at = 1;
    if (words.size() > 4 && words[3] != "@" && words[4] == "@")
        at = 2; // a level id first
    // cgame ui @ [paks...] count; six words at least, as the original
    // counts them (so a pak, without a level id)
    if (words.size() < 6 || words.size() < at + 4 || words[0] != "cp" || words[at + 2] != "@")
        return false;
    auto number = [](const std::string& w, int32_t& out) {
        char* end = nullptr;
        const long long v = std::strtoll(w.c_str(), &end, 10);
        out = int32_t(v);
        return !w.empty() && end && *end == 0;
    };
    int32_t theirCgame = 0, theirUi = 0;
    if (!number(words[at], theirCgame) || !number(words[at + 1], theirUi) || theirCgame != cgame || theirUi != ui)
        return false;
    std::set<int32_t> paks;
    int32_t check = feed;
    for (size_t i = at + 3; i + 1 < words.size(); ++i) {
        int32_t sum = 0;
        if (!number(words[i], sum) || !paks.insert(sum).second || !serverPaks.count(sum))
            return false;
        check ^= sum;
    }
    int32_t encoded = 0;
    if (!number(words.back(), encoded))
        return false;
    check ^= int32_t(paks.size());
    return check == encoded;
}

bool referencesPak(const std::string& name) {
    std::string lower = name;
    for (char& ch : lower)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    for (const char* ext : {".shader", ".txt", ".cfg", ".config", ".bot", ".arena", ".menu"}) {
        const size_t n = std::strlen(ext);
        if (lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0)
            return false;
    }
    return lower != "vm/qagame.qvm" && lower.find("levelshots") == std::string::npos;
}

} // namespace reluva::idtech3
