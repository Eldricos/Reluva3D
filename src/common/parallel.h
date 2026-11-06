// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace reluva {

// f(0) .. f(n - 1), spread over the machine's cores (the calling thread
// one of them); back once all are done. The calls must touch nothing in
// common but what they only read.
template <class F>
void parallelFor(size_t n, F&& f) {
    const size_t workers = std::min<size_t>(n, std::max(1u, std::thread::hardware_concurrency()));
    if (workers <= 1) {
        for (size_t i = 0; i < n; i++)
            f(i);
        return;
    }
    std::atomic<size_t> next{0};
    auto run = [&] {
        for (size_t i; (i = next.fetch_add(1)) < n;)
            f(i);
    };
    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (size_t w = 1; w < workers; w++)
        threads.emplace_back(run);
    run();
    for (std::thread& t : threads)
        t.join();
}

} // namespace reluva
