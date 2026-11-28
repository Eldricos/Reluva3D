// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The player's progress through the campaigns, as the game keeps it in
 * `shell.set`: for each campaign the last mission open and whether it's
 * finished, a few bytes whose use isn't known, and the player's name.
 * The game writes it in text mode, so a newline byte in it is written as
 * CR LF and read back as one. See
 * docs/engines/storm3d/games/armada/formats/shell-set.md.
 */
struct ShellProgress {
    /** The campaigns: the Federation, the Klingons, the Romulans, the Borg, the finale. */
    static constexpr size_t kCampaigns = 5;
    /** A name's room in the file, its terminating NUL included. */
    static constexpr size_t kNameBytes = 64;

    std::array<uint8_t, kCampaigns> lastMission{}; // the last open, from 0
    std::array<bool, kCampaigns> finished{};
    // The program's own bytes, kept as read: at 0, 1 and 2 of its record
    // (the install's has 1 in the third), then the campaign it was on
    // (+4) and a campaign the code sets to 4 when it opens them all (+8).
    std::array<uint8_t, 3> flags{0, 0, 1}; // a new player's finale shut (0x545ac1)
    uint8_t campaign = 0;
    uint8_t lastCampaign = 0;
    // Two names written only when a flag before them is set: by where
    // they sit beside the player's, the online service's login (a guess).
    bool hasLogin = false;
    std::string login;
    std::string loginSecond;
    std::string player;

    /**
     * Reads the file.
     * @return a failure if it's too short
     */
    Error parse(const std::vector<uint8_t> &file);
    /** The file as the game writes it. */
    std::vector<uint8_t> bytes() const;

    /**
     * Whether a campaign can be played: the first, or one after a campaign
     * finished; the finale once its lock (the third flag byte) is clear.
     */
    bool open(size_t campaign) const {
        if (campaign + 1 == kCampaigns)
            return flags[2] == 0;
        return campaign == 0 || (campaign < kCampaigns && finished[campaign - 1]);
    }
    /**
     * A mission won, as the program counts it (0x545e10): the campaign's
     * next opens if it was the last open; past its last, the campaign is
     * finished, and the Borg's finished opens the finale.
     */
    void won(size_t campaign, int mission, int missions);
};

} // namespace reluva::storm3d
