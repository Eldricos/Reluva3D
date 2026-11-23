// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/sprite_table.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/page.h"
#include "image/image.h"

#include <map>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The sprite tables' sprites: each on its texture, outlined, and enlarged
 * beside it; a font's sprite also writes a line of text with its
 * characters.
 *   Left/Right: previous/next sprite   PageUp/PageDown: 10 at a time   Home/End: first/last
 *   Up/Down: its animation's previous/next keyframe   T: the next table's first sprite
 */
class SpritePage : public Page {
public:
    /** Reads every sprite table; `files`, `sys` and `screen` must outlive it. */
    SpritePage(System &sys, Surface &screen, const GameFiles &files);

    const char *name() const override { return "Sprites"; }
    size_t count() const override { return _list.size(); }
    void activate() override { show(); }
    void handleKey(uint32_t key) override;

    /**
     * Where a sprite's frame is on its texture, in the texture's pixels:
     * its rectangle moved by its offset animation's keyframe (or to the
     * frame's cell, for keyframes laid out by `@auto`), and as wide as its
     * width animation's keyframe says, if it has those.
     * @param frame the keyframe, from 0
     * @param texW, texH the texture's size, which the reference size is scaled to
     */
    static void frameRect(const SpriteTable::Sprite &sprite, const SpriteTable::Animation *offset,
                          const SpriteTable::Animation *width, size_t frame, int texW, int texH, int &x, int &y, int &w,
                          int &h);

private:
    struct Entry {
        size_t table;  // index into _tables
        size_t sprite; // index into its sprites()
    };
    const SpriteTable::Animation *findAnimation(const std::string &name) const;
    const image::Image *texture(const std::string &name);
    void show();

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    std::vector<SpriteTable> _tables;
    std::vector<std::string> _tableNames;
    std::vector<Entry> _list;
    std::map<std::string, image::Image> _textures; // by name, as read; empty when not found
    size_t _current = 0;
    size_t _frame = 0;
};

} // namespace reluva::storm3d
