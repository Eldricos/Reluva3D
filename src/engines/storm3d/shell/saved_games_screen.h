// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/label_map.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/shell/settings_screen.h"
#include "engines/storm3d/shell/shell_draw.h"
#include "image/image.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/** What the saved games' screen was left with. */
enum class SavedGamesChoice { kNone, kPrevious, kLoad, kSave, kDelete };

/**
 * The saved games' screen, as the program's Load Game dialog (from the
 * single-player screen and from play, 0x53b8b0) and its Save Game one
 * (from play, 0x53eac0) draw it: `bitmaps\misc\LoadBG.bmp`, a row for each
 * saved game showing its description, the scroll bar, and Previous Menu,
 * Load or Save Game and Delete Game on the bottom bar; saving, the
 * description's box under the rows and the overwrite question. See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class SavedGamesScreen {
public:
    /** A saved game listed: its file's name in `save\` and its description. */
    struct Entry {
        std::string file;
        std::string description;
    };
    /** The rows' place: from (100, 61), 394 x 32 each, 11 of them, 10 saving. */
    static constexpr int kRowX = 100;
    static constexpr int kRowY = 61;
    static constexpr int kRowWidth = 394;
    static constexpr int kRowHeight = 32;
    static constexpr size_t kDescriptionLength = 40; // the box's EM_LIMITTEXT

    /**
     * Reads its pictures and text, for loading or for saving; saving, the
     * description starts as the mission's name, `map`'s in label.map.
     * @return a failure if one isn't there
     */
    Error load(const GameFiles &files, bool saving, const std::string &map = {});
    /** The saved games to list, in the folder's order. */
    void setEntries(std::vector<Entry> entries);
    const std::vector<Entry> &entries() const { return _entries; }
    /** The row chosen, as an index into entries(); -1 for none. */
    int chosen() const { return _chosen; }
    /** The description typed, saving. */
    const std::string &description() const { return _description; }

    /** @return whether the pointer came onto a button (the game plays a sound) */
    bool pointerAt(int x, int y);
    void press(int x, int y);
    /** Let go at (x, y): what to do. */
    SavedGamesChoice release(int x, int y);
    /** Typing in the description's box, and its Backspace. */
    void type(std::string_view text);
    void erase();

    void draw(Surface &screen);

private:
    enum Target { kNone, kPrevious, kAct, kDelete, kUp, kDown, kRow, kYes, kNo };
    struct Hit {
        Target target = kNone;
        int row = 0;
        bool operator==(const Hit &o) const { return target == o.target && row == o.row; }
    };
    Hit hitAt(int x, int y) const;
    size_t rows() const { return _saving ? 10 : 11; }

    bool _saving = false;
    image::Image _background;
    ShellButton _up;
    ShellButton _down;
    image::Image _thumb;
    std::array<image::Image, 3> _row; // mpsstart1 to 3: at rest, lit, chosen
    image::Image _box;                // the description's
    NavigationBar _bar;
    LabelMap _text;
    ShellFont _font;
    std::vector<Entry> _entries;
    size_t _first = 0; // the entry in the top row
    int _chosen = -1;
    std::string _description;
    Hit _hover;
    Hit _pressed;
    // The overwrite question's box: its picture, Yes and No.
    bool _asking = false;
    image::Image _askBox;
    ShellButton _yes;
    ShellButton _no;
};

} // namespace reluva::storm3d
