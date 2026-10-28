// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/image.h"
#include "common/types.h"

#include <map>
#include <string>

namespace reluva::age {

class CarSim;

// The game's HUD pictures (texture/*.tga, made for a 640 x 480 screen):
// the speedometer's panel (SPEED) with its digits (DIGITAC_*_HALF), gear
// (DIGITAC_GEAR_*), revs (TACOMETER TICKS_HALF) and MPH; the damage meter
// (DAMAGE, DAMAGE_LABLE); and the clock's digits (DIGI_*). Where they go is
// Reluva's layout: the game's isn't in its files. See
// docs/engines/age-driving.md.
class HudPictures {
public:
    // False if they aren't there.
    bool load(const SearchSet& files);
    bool loaded() const { return ok_; }

    // The panel with its top left at (x, y): 202 x 110.
    void speedometer(Surface& overlay, const CarSim& sim, int x, int y) const;
    // The meter, `width` across with its label above, its bottom left at
    // (x, y); `damage` 0..1.
    void damage(Surface& overlay, float damage, int x, int y, int width) const;
    // Minutes, seconds and tenths, ending at `right`.
    void clock(Surface& overlay, float seconds, int right, int y) const;

private:
    const image::Image* get(const std::string& name) const;
    // Drawn over what's there, `w` x `h` (scaled to it), or only the left
    // `share` of it.
    void blit(Surface& overlay, const image::Image& im, int x, int y, int w, int h, float share = 1) const;

    std::map<std::string, image::Image> images_;
    bool ok_ = false;
};

} // namespace reluva::age
