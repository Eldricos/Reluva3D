// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * A sprite table (`.spr`): named rectangles of textures, for the interface,
 * the fonts, cursors and effects, with the animations that move them, and
 * particle emitters. `sprites.spr` includes the others. See
 * docs/engines/storm3d/games/armada/formats/spr.md.
 */
class SpriteTable {
public:
    /** An animation: what it changes, over how many keyframes. */
    struct Animation {
        std::string name;
        std::string kind;          // what it changes: "offset", "width", "colour", "draw"
        int count = 0;             // its keyframes
        double duration = 0;       // in seconds; 0 for one chosen by index (a font's characters)
        std::string interpolation; // "step" or "linear"
        std::string autoLayout;    // "square", "row" or "column" when the keyframes are laid out by the game; else ""
        std::vector<std::vector<double>> keyframes; // each row's numbers, as written
    };

    /** A sprite: a rectangle of a texture, in the texture's reference size. */
    struct Sprite {
        std::string name;
        std::string texture; // a texture's name, without folder or extension: "font", "curs_24"
        double u = 0;
        double v = 0;
        double w = 0;
        double h = 0;
        int referenceWidth = 0; // the size the rectangle is measured against
        int referenceHeight = 0;
        std::string skip;                              // @skip in force: "off", or a colour key "(0,0,0)"
        std::string material;                          // @tmaterial in force: how it's drawn ("wormhole", "interface")
        std::vector<std::string> animations;           // its @anim=, in order
        std::map<std::string, std::string> parameters; // its other @name[=value]: "cursor", "origin" -> "(16,16)"
        std::string file;                              // the table it's in
    };

    /** A particle emitter: its `key=value` settings, as written. */
    struct Emitter {
        std::string name;
        std::map<std::string, std::string> settings;
    };

    /** Gives a file's text by name, or fails if there's none. */
    using Loader = std::function<Error(std::string_view name, std::string &text)>;

    /**
     * Reads a table and, through the loader, the tables it includes.
     * @return a failure if a table can't be loaded or includes nest past
     *         16 levels
     */
    Error load(std::string_view name, const Loader &loader);

    /** The sprites, in the order read. */
    const std::vector<Sprite> &sprites() const { return _sprites; }
    /** The animations, by name; a later one of a name replaces an earlier. */
    const std::map<std::string, Animation> &animations() const { return _animations; }
    /** The emitters, in the order read. */
    const std::vector<Emitter> &emitters() const { return _emitters; }
    /** The `@sprite_node` lines, as written: not read yet. */
    const std::vector<std::string> &spriteNodes() const { return _spriteNodes; }
    /** The tables read: the one loaded, then each it includes, as named. */
    const std::vector<std::string> &tables() const { return _tables; }
    /** Lines of no known form, with their file and line number. */
    const std::vector<std::string> &problems() const { return _problems; }

    /** The first sprite of that name, or nullptr. */
    const Sprite *sprite(std::string_view name) const;
    /** The animation of that name, or nullptr. */
    const Animation *animation(std::string_view name) const;

private:
    Error parse(std::string_view name, const Loader &loader, int depth);

    std::vector<Sprite> _sprites;
    std::map<std::string, Animation> _animations;
    std::vector<Emitter> _emitters;
    std::vector<std::string> _spriteNodes;
    std::vector<std::string> _tables;
    std::vector<std::string> _problems;
};

} // namespace reluva::storm3d
