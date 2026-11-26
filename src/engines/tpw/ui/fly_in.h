// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/tpw/ui/model_view.h"
#include "graphics/soft3d/view3d.h"

#include <cstdint>
#include <string>

namespace reluva::tpw {

/**
 * The fly-in: the camera flown along the road into a new park, to its
 * gates, by the world's camera path (`obstacles/Camera/camera.MD2`): its
 * camera dummy carried along the path's spline, turned as its animation
 * turns it. See docs/engines/tpw/games/simcoaster/systems/fly-in.md.
 */
class FlyIn {
public:
    /**
     * A frame's time: 500 frames from the tunnel at 1:34.5 in the
     * recording to the camera's stop at about 1:47.5 (inferred).
     */
    static constexpr uint32_t kFrameMillis = 26;
    /** Reads the world's camera path; fails without it, its spline, its dummy or its animation. */
    Error load(const SearchSet &files, const std::string &worldFolder);
    /** The flight's length, in frames, kFrameMillis each. */
    float frames() const;
    /** The camera at `frame`, for a screen of w × h. */
    soft3d::OrbitCamera camera(float frame, int w, int h) const;
    /** Which way the camera faces at `frame`, in radians about the vertical, as ParkScreen::Camera::yaw counts it. */
    float yaw(float frame) const;

private:
    // Where the dummy is along the spline at `frame`, in the model's space.
    gfx::Vec3 along(float frame) const;
    // The point u stretches between knots along the spline.
    gfx::Vec3 at(float u) const;

    LoadedModel _path;
    int _animation = -1;
    size_t _dummy = 0;  // the camera, in the model's nodes
    size_t _spline = 0; // the spline's node
};

class ParkScreen;
class Park;

/**
 * Plays the fly-in over a new park, its objects' scripts running, then
 * holds on the gates as the recording does before the briefing; a click
 * or a key ends it (guessed).
 * @return False if the window was closed (or the launcher asked for).
 */
bool playFlyIn(System &sys, const FlyIn &flyIn, ParkScreen &screen, Park &park);

} // namespace reluva::tpw
