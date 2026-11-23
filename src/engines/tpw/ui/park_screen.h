// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/ui/world_view.h"
#include "graphics/soft3d/rasterizer.h"

namespace reluva::tpw {

/**
 * The park as the player sees it: its world drawn through a camera that
 * pans, turns and zooms over it, its fixed items marked, and the HUD's
 * money and date. See docs/engines/tpw/games/simcoaster/systems/park.md.
 */
class ParkScreen {
public:
    /** Where the camera looks from: at `target`, from `distance` units, turned and tilted (radians). */
    struct Camera {
        gfx::Vec3 target{};
        float yaw = 0;
        float pitch = 0;
        float distance = 0;
    };

    /** Reads the park's world, the HUD's fonts and the months' names. Fails without the world. */
    Error load(const SearchSet &files, const Park &park);

    /** The camera a new park starts with: over its entrance, looking in. */
    Camera startCamera(const Park &park) const;

    /** Draws the park through `camera` over the whole of an RGBA32 surface. */
    void draw(Surface &screen, soft3d::Rasterizer &rasterizer, const Park &park, const Camera &camera) const;

    /** The date as the HUD shows it: "14.Jan.00". */
    std::string dateText(uint32_t day) const;

private:
    LoadedWorld _world;
    CharacterSet _characters;
    Font _cashFont;
    Font _dateFont;
    StringTable _months;
    bool _hud = false; // the fonts and months read
};

/**
 * Plays the park until the player leaves it (Esc) or the window is
 * closed: its clock runs, the arrow keys move the camera over it, Q and E
 * turn it, Page Up and Page Down (or the wheel) zoom, Home goes back to
 * the start.
 * @return False if the window was closed (or the launcher asked for).
 */
bool playPark(System &sys, ParkScreen &screen, Park &park);

} // namespace reluva::tpw
