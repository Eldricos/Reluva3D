// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/shell_progress.h"

#include <algorithm>

namespace reluva::storm3d {

namespace {

// The record's parts in the order written (0x545b90): the missions, the
// finished flags, five single bytes, the login flag; then the two login
// names if it's set, and the player's name.
constexpr size_t kFixed = ShellProgress::kCampaigns * 2 + 6;

std::string nameAt(const std::vector<uint8_t> &d, size_t at) {
    std::string s;
    for (size_t i = at; i < at + ShellProgress::kNameBytes && d[i]; ++i)
        s.push_back(char(d[i]));
    return s;
}

void putName(std::vector<uint8_t> &out, const std::string &name) {
    const size_t n = std::min(name.size(), ShellProgress::kNameBytes - 1);
    out.insert(out.end(), name.begin(), name.begin() + long(n));
    out.insert(out.end(), ShellProgress::kNameBytes - n, 0);
}

} // namespace

Error ShellProgress::parse(const std::vector<uint8_t> &file) {
    // Read in text mode, as the game reads it: CR LF is one newline.
    std::vector<uint8_t> d;
    for (size_t i = 0; i < file.size(); ++i)
        if (!(file[i] == '\r' && i + 1 < file.size() && file[i + 1] == '\n'))
            d.push_back(file[i]);
    if (d.size() < kFixed)
        return Error::fail("shell.set: too short");
    size_t at = 0;
    for (uint8_t &m : lastMission)
        m = d[at++];
    for (bool &f : finished)
        f = d[at++] != 0;
    for (uint8_t &f : flags)
        f = d[at++];
    campaign = d[at++];
    lastCampaign = d[at++];
    hasLogin = d[at++] != 0;
    if (d.size() < at + kNameBytes * (hasLogin ? 3 : 1))
        return Error::fail("shell.set: too short");
    if (hasLogin) {
        login = nameAt(d, at);
        loginSecond = nameAt(d, at + kNameBytes);
        at += 2 * kNameBytes;
    }
    player = nameAt(d, at);
    return Error::success();
}

bool ShellProgress::won(size_t campaign, int mission, int missions) {
    if (campaign >= kCampaigns || mission != lastMission[campaign])
        return false;
    if (lastMission[campaign] + 1 < missions) {
        ++lastMission[campaign];
        return true;
    }
    finished[campaign] = true;
    if (finished[3])
        flags[2] = 0;
    // The first campaign not finished (0x545e39).
    lastCampaign = 0;
    while (lastCampaign < kCampaigns - 1 && finished[lastCampaign])
        ++lastCampaign;
    return false;
}

std::vector<uint8_t> ShellProgress::bytes() const {
    std::vector<uint8_t> d;
    d.insert(d.end(), lastMission.begin(), lastMission.end());
    for (bool f : finished)
        d.push_back(f ? 1 : 0);
    d.insert(d.end(), flags.begin(), flags.end());
    d.push_back(campaign);
    d.push_back(lastCampaign);
    d.push_back(hasLogin ? 1 : 0);
    if (hasLogin) {
        putName(d, login);
        putName(d, loginSecond);
    }
    putName(d, player);
    // Written in text mode: a newline goes out as CR LF.
    std::vector<uint8_t> out;
    for (uint8_t b : d) {
        if (b == '\n')
            out.push_back('\r');
        out.push_back(b);
    }
    return out;
}

} // namespace reluva::storm3d
