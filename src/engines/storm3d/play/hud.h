// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/gui_config.h"
#include "engines/storm3d/formats/sprite_table.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/scene/sprite_font.h"
#include "image/image.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The interface in play, as the game draws it over the map: its panels
 * (the minimap, the speed rail, the ship's display, the cinematic view,
 * the control panel) and the dilithium, crew and officers along the top,
 * laid out by the race's `misc/gui_<race>.cfg` from its sprites
 * (`Sprites/gui_<race>.spr`, `gui_global.spr`). See
 * docs/engines/storm3d/games/armada/systems/play.md.
 */
class Hud {
public:
    /** What the counters say. */
    struct Resources {
        int dilithium = 0;
        int crew = 0;
        int officers = 0;
        int maxOfficers = 0;
    };
    /** The panels drawn, in the game's order, by their names in the layout ("minimap"). */
    static const std::vector<const char *> &panels();
    /** Whether a point of the screen is on one of the panels (where clicks don't reach the map). */
    bool covers(int x, int y) const;
    /**
     * Draws a unit's health bar from (left, top), `width` across and 4
     * high: the info panel's large_shield_bar sprite in dark grey, then in
     * green as far as `health` (0 to 1), as the game's draws do.
     */
    void drawHealthBar(Surface &screen, int left, int top, int width, float health);
    /** A counter's words: the number, and "used/most" for the officers. */
    static std::string counterText(const Resources &r, int counter);

    /**
     * Reads the layout, sprites and textures for a race ("fed", "kli",
     * "rom", "bor": the layout files' names).
     * @return a failure if the layout or a sprite table can't be read
     */
    Error load(const GameFiles &files, const std::string &race);
    /**
     * Draws it onto `screen` (an RGBA overlay, left clear where the map
     * shows through): the minimap's cells `seen` (128 × 128, row 0 the
     * map's far edge; empty: all seen); `counters`: the numbers written
     * (a campaign's aren't known yet).
     */
    void draw(Surface &screen, const Resources &resources, const std::vector<bool> &seen, bool counters = true);
    /**
     * Draws the game's cursor with its tip at (x, y), `seconds` into its
     * animation: the arrow, or over a unit the select cursor (`cursor.spr`'s
     * c_arrow and c_select), black left out.
     */
    void drawCursor(Surface &screen, int x, int y, double seconds, bool overUnit);

private:
    // A sprite drawn into a rectangle of the screen: its texture's
    // texels from the sprite's place, two to a pixel at the interface's
    // sprites' size, one point each, where the texel isn't clear; `tint`
    // times its colour.
    void drawSprite(Surface &screen, const std::string &sprite, int x, int y, int w, int h,
                    const std::array<uint8_t, 3> &tint = {255, 255, 255});
    void drawPanel(Surface &screen, const std::string &name);
    const image::Image *texture(const std::string &name);
    const SpriteTable::Sprite *sprite(const std::string &name) const;

    const GameFiles *_files = nullptr;
    GuiConfig _layout;
    std::vector<SpriteTable> _tables; // the race's, then gui_global.spr
    SpriteFont _font;
    std::map<std::string, image::Image> _textures; // by lower-case name; empty if not there
};

} // namespace reluva::storm3d
