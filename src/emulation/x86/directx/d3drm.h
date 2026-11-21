// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/directx/directx.h"
#include "emulation/x86/win32.h"

#include <cstddef>
#include <memory>

namespace reluva::x86 {

/**
 * d3drm.dll: Direct3D Retained Mode, up to DirectX 5, for the programs
 * that use it, written from Microsoft's DirectX 5 documentation
 * (docs/common/x86/d3drm.md). Its objects (frames, meshes, lights,
 * textures, viewports, devices) are kept as the program builds them; a
 * device is made on DirectDraw's surfaces and Direct3D's devices
 * (`directX`'s), which draw what it shows.
 */
class Direct3DRM {
public:
    /**
     * Adds Direct3DRMCreate and D3DRMCreateColorRGBA to `process`. What
     * they make points into this object, so it has to outlive the
     * program's use of it, as `directX` does.
     */
    Direct3DRM(Win32Process &process, DirectX &directX);
    ~Direct3DRM();

    /** Retained Mode objects made so far, of any kind. */
    size_t objectsMade() const;

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
