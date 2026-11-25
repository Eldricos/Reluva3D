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
    /** What the ship's display, the control panel and the rail show of a unit selected. */
    struct Unit {
        std::string raceIcon; // a sprite: "federation_icon"
        std::string className;
        std::string name;
        int crew = 0;
        int maxCrew = 0;
        int officers = 0;  // what it costs; none: no line for them
        float shields = 1; // 0 to 1
        float construction = 0;
        std::string wireframe;         // its texture: "FWbase"
        std::array<bool, 5> systems{}; // as info_system's icons: shields, engines, weapons, life support, sensors
        /** An order's button: its place (controlButton1 to 12), its icon and border sprites, whether the unit can. */
        struct Button {
            int slot = 1;
            std::string sprite;
            std::string border;
            bool enabled = true;
        };
        std::vector<Button> controls;
        std::vector<std::string> specials; // its special weapons' buttons' sprites: "b_gtracbm"
    };
    /** Whether the interface's sprite tables have a sprite of this name. */
    bool hasSprite(const std::string &name) const;
    /** Draws a selected unit's display, orders and special weapons over the panels. */
    void drawUnit(Surface &screen, const Unit &unit);
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
    // `additive`: added on, as the game's systems' icons and buttons'
    // borders are; `part`: a bar's share shown from its left.
    void drawSprite(Surface &screen, const std::string &sprite, int x, int y, int w, int h,
                    const std::array<uint8_t, 3> &tint = {255, 255, 255}, bool additive = false, float part = 1);
    static void drawTexels(Surface &screen, const image::Image &tex, double u, double v, double tw, double th, int x,
                           int y, int w, int h, const std::array<uint8_t, 3> &tint, bool additive);
    void drawPanel(Surface &screen, const std::string &name);
    void drawText(Surface &screen, const std::array<int, 4> &area, const std::string &text,
                  const std::array<uint8_t, 3> &tint);
    const image::Image *texture(const std::string &name);
    const SpriteTable::Sprite *sprite(const std::string &name) const;

    const GameFiles *_files = nullptr;
    GuiConfig _layout;
    std::vector<SpriteTable> _tables; // the race's, then gui_global.spr
    SpriteFont _font;
    std::map<std::string, image::Image> _textures; // by lower-case name; empty if not there
    std::map<std::string, std::string> _words;     // Dynamic_Localized_Strings.h's, by key
    std::string word(const std::string &key, const std::string &fallback) const;
};

} // namespace reluva::storm3d
