// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/timers.h"

#include <cstdio>

namespace reluva::botf {

const std::vector<int> &strategicTimerChoices() {
    // 1 to 15 minutes a minute apart, then 20 and 25, then 30 to 90 ten
    // apart.
    static const std::vector<int> kChoices = [] {
        std::vector<int> v = {0};
        for (int m = 1; m <= 15; ++m)
            v.push_back(m * 60);
        v.push_back(20 * 60);
        v.push_back(25 * 60);
        for (int m = 30; m <= 90; m += 10)
            v.push_back(m * 60);
        return v;
    }();
    return kChoices;
}

const std::vector<int> &tacticalTimerChoices() {
    // 15 seconds to 5 minutes 15 seconds apart, then to 10 minutes a minute
    // apart, then to 30 five apart.
    static const std::vector<int> kChoices = [] {
        std::vector<int> v = {0};
        for (int s = 15; s <= 5 * 60; s += 15)
            v.push_back(s);
        for (int m = 6; m <= 10; ++m)
            v.push_back(m * 60);
        for (int m = 15; m <= 30; m += 5)
            v.push_back(m * 60);
        return v;
    }();
    return kChoices;
}

std::string timerText(int seconds) {
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d", seconds / 60, seconds % 60);
    return text;
}

} // namespace reluva::botf
