// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace reluva::log {

namespace {
std::atomic<int> level{0};

void print(const char* prefix, const char* fmt, va_list args) {
    char buf[2048];
    std::vsnprintf(buf, sizeof buf, fmt, args);
    std::fprintf(stderr, "%s%s\n", prefix, buf);
}
} // namespace

void setDebugLevel(int l) { level = l; }
int debugLevel() { return level; }

void debug(int l, const char* fmt, ...) {
    if (l > level)
        return;
    va_list args;
    va_start(args, fmt);
    print("", fmt, args);
    va_end(args);
}

void warning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    print("WARNING: ", fmt, args);
    va_end(args);
}

} // namespace reluva::log
