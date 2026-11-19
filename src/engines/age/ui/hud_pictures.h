// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "image/image.h"
#include "common/types.h"

#include <map>
#include <string>

namespace reluva::age {

class CarSim;

/**
 * The game's HUD pictures (texture/<name>.tga, made for a 640 x 480 screen),
 * where the game puts them (measured on its own screens, see
 * docs/engines/age/games/mm2/systems/hud.md): bottom left the speed (DIGITAC_*), the
 * gear (DIGITAC_GEAR_*), the revs (SPEED_TICKS) and the damage meter
 * (DAMAGE under DAMAGE_LABLE); the clock's digits (DIGI_*) top middle.
 */
class HudPictures {
public:
    /** False if they aren't there. */
    bool load(const SearchSet &files);
    /** Whether the last load() found the pictures. */
    bool loaded() const { return _ok; }

    /**
     * The speed, gear, revs and damage (0..1), bottom left of a 640 x 480
     * overlay.
     */
    void gauges(Surface &overlay, const CarSim &sim, float damage) const;
    /** Minutes, seconds and hundredths ("01:02:19"), centred on `centre`. */
    void clock(Surface &overlay, float seconds, int centre, int y) const;
    /**
     * The CD player (CD_BG, 179 x 100) with its top left at (x, y): the
     * play or stop button lit, `pressed` (-1 previous, 1 next, 0 none) lit
     * too, `text` in its display. False without the pictures.
     */
    bool cdPlayer(Surface &overlay, int x, int y, bool playing, int pressed, const std::string &text) const;
    /**
     * A place (1-8; 9: the gold's $) from opp_icon, `size` across centred
     * on (x, y), as over an opponent; false without the picture.
     */
    bool place(Surface &overlay, int place, int x, int y, int size) const;

private:
    const image::Image *get(const std::string &name) const;
    // Drawn over what's there, `w` x `h` (scaled to it), or only the left
    // `share` of it; `from` (0 or more): its columns from that one, unscaled.
    void blit(Surface &overlay, const image::Image &im, int x, int y, int w, int h, float share = 1,
              int from = -1) const;

    std::map<std::string, image::Image> _images;
    image::Image _places;                    // opp_icon
    std::map<std::string, image::Image> _cd; // CD_BG and its buttons
    bool _ok = false;
};

} // namespace reluva::age
