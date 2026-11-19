// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/debug.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <set>

namespace reluva::stgenerations {

namespace {

std::mutex channelsMutex;
std::set<std::string>& channels() {
    static std::set<std::string> c;
    return c;
}

} // namespace

void enableDebugChannel(const std::string& name) {
    std::lock_guard<std::mutex> lock(channelsMutex);
    channels().insert(name);
}

bool isDebugChannelEnabled(const char* name) {
    std::lock_guard<std::mutex> lock(channelsMutex);
    return channels().count(name) || channels().count("all");
}

void debugC(int level, const char* channel, const char* fmt, ...) {
    if (level > log::debugLevel() || !isDebugChannelEnabled(channel))
        return;
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    log::debug(level, "%s", buffer);
}

} // namespace reluva::stgenerations
