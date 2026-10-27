// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/soft3d/rasterizer.h"
#include "common/soft3d/view3d.h"

namespace reluva::newworlds {

// The software 3D helpers (common/soft3d), as this engine's viewers use them.
using soft3d::clipNear;
using soft3d::cross;
using soft3d::dot;
using soft3d::normalize;
using soft3d::OrbitCamera;
using soft3d::Rasterizer;
using soft3d::ScreenVertex;
using soft3d::Vec;
using soft3d::ViewVertex;

} // namespace reluva::newworlds
