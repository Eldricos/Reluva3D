// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/seed.h"
#include <atomic>
#include <chrono>
#include <random>
#include <exception>
namespace reluva::boggle {
namespace {
uint32_t initialSeed() {
    const auto ticks=uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    uint32_t seed=uint32_t(ticks)^uint32_t(ticks>>32);
    // Entropy may be unavailable on a platform. A clock-seeded sequence still
    // provides ordinary play without making game startup depend on that service.
    try {std::random_device source;seed^=uint32_t(source());}
    catch(const std::exception&) {}
    return seed;
}
}
uint32_t freshClassicSeed() {
    static std::atomic<uint32_t> next{initialSeed()};
    // An odd increment visits every uint32 value before wrapping. Keep the
    // sequence independent of timer, input, audio and menu frame counts.
    return next.fetch_add(0x9e3779b9u,std::memory_order_relaxed);
}
}
