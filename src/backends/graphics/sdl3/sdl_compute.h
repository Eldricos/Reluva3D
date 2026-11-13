// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/gpu_compute.h"

#include <memory>
#include <string>

namespace reluva {

// gfx::Compute on SDL's GPU API (Vulkan, Metal), on a GPU device of its own
// (no window needed, so reluva-tool can use it too).
std::unique_ptr<gfx::Compute> makeSdlCompute(std::string& error);

} // namespace reluva
