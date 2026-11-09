// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/gpu_compute.h"

#include <mutex>

namespace reluva::gfx {

namespace {
std::mutex& factoryMutex() {
    static std::mutex m;
    return m;
}
ComputeFactory& factory() {
    static ComputeFactory f;
    return f;
}
} // namespace

void setComputeFactory(ComputeFactory f) {
    std::lock_guard<std::mutex> lock(factoryMutex());
    factory() = std::move(f);
}

std::unique_ptr<Compute> createCompute(std::string& error) {
    ComputeFactory f;
    {
        std::lock_guard<std::mutex> lock(factoryMutex());
        f = factory();
    }
    if (!f) {
        error = "no GPU compute in this build";
        return nullptr;
    }
    return f(error);
}

} // namespace reluva::gfx
