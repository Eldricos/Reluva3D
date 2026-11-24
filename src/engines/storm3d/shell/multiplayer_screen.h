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

/** What the player chose on the Multiplayer Connection screen. */
enum class ConnectionChoice { kNone, kPreviousMenu, kInternet, kLan, kInstantAction };

/**
 * The Multiplayer Connection screen, as the game draws it at 640 x 480
 * over the Options screen's background (`bitmaps/wonlogin/wlsBG.bmp`):
 * a box for the player's name (`wlstext.bmp`), three buttons for the
 * ways to play, and the bottom bar. See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class MultiplayerScreen {
public:
    /** A way to play's button: what it chooses, its words' key in label.map's multiplayer_connection, its top. */
    struct Button {
        ConnectionChoice choice;
        const char *key;
        int y;
    };
    /** The buttons, top to bottom. */
    static const std::vector<Button> &buttons();
    /** The choice under a point: kNone if none. */
    static ConnectionChoice choiceAt(int x, int y);
    /** The longest name the box takes. */
    static constexpr size_t kNameLength = 31;
    /** A name after typing `text` (printable ASCII kept, the rest left out), up to kNameLength. */
    static std::string typed(std::string_view name, std::string_view text);

    /**
     * Reads its pictures and text.
     * @return a failure if one isn't there
     */
    Error load(const GameFiles &files);
    /** The player's name, as typed in the box. */
    const std::string &name() const { return _name; }
    void setName(const std::string &name) { _name = name; }
    /** Text typed: added to the name. */
    void type(std::string_view text) { _name = typed(_name, text); }
    /** Backspace: the name's last letter taken off. */
    void erase() {
        if (!_name.empty())
            _name.pop_back();
    }

    /** @return whether the pointer came onto a button (the game plays a sound) */
    bool pointerAt(int x, int y);
    void press(int x, int y);
    /** Let go at (x, y): the choice if it's on the button pressed. */
    ConnectionChoice release(int x, int y);

    void draw(Surface &screen);

private:
    image::Image _background;
    image::Image _box;
    std::array<ShellButton, 3> _ways; // as buttons()
    NavigationBar _bar;
    LabelMap _text;
    ShellFont _font;
    std::string _name;
    ConnectionChoice _hover = ConnectionChoice::kNone;
    ConnectionChoice _pressed = ConnectionChoice::kNone;
};

} // namespace reluva::storm3d
