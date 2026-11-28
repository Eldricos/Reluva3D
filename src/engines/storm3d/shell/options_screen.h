// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/label_map.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/shell/settings_screen.h"
#include "engines/storm3d/shell/shell_draw.h"
#include "image/image.h"

#include <vector>

namespace reluva::storm3d {

/** What the player chose on the Options screen. */
enum class OptionsChoice {
    kNone,
    kPreviousMenu,
    kGraphics,
    kSound,
    kGame,
    kSurrender,
    kAbort,
    kReturn,
    kRestate,
    kSaveGame,
    kLoadGame
};
/**
 * Which of the screen's menus: the front end's; a skirmish's; a
 * campaign mission's, with Save Game but during a cut scene (0x5375fb).
 */
enum class OptionsMenu : uint8_t { kFrontEnd, kSkirmish, kCampaign, kCampaignCutScene };

/**
 * The front end's Options screen, as the game draws it at 640 x 480: the
 * background with its title bar and panel (`bitmaps/wonlogin/wlsBG.bmp`),
 * a button for each of the three settings screens, and along the bottom
 * Previous Menu beside two empty plates. In a game (its MENU button) it
 * also has Surrender above them and Abort Mission and Return to Game
 * below, and the bottom's plates are all empty; Abort Mission asks first,
 * in a box over it (`bitmaps/misc/DBsmall2BG.bmp`), and is chosen by its
 * Yes. See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class OptionsScreen {
public:
    /** A settings screen's button: what it chooses, its words' key in label.map's escape_menu, its top. */
    struct Button {
        OptionsChoice choice;
        const char *key;
        int y;
    };
    /** The buttons, top to bottom, of a menu. */
    static const std::vector<Button> &buttons(OptionsMenu menu);
    /** The choice under a point, the buttons' sizes as the install's: kNone if none. */
    static OptionsChoice choiceAt(int x, int y, OptionsMenu menu);
    /** Whether Abort Mission's box is up, asking. */
    bool asking() const { return _asking; }

    /**
     * Reads its pictures and text.
     * @return a failure if one isn't there
     */
    Error load(const GameFiles &files, OptionsMenu menu = OptionsMenu::kFrontEnd);

    /** @return whether the pointer came onto a button (the game plays a sound) */
    bool pointerAt(int x, int y);
    void press(int x, int y);
    /** Let go at (x, y): the choice if it's on the button pressed. */
    OptionsChoice release(int x, int y);

    void draw(Surface &screen);

private:
    image::Image _background;
    OptionsMenu _menu = OptionsMenu::kFrontEnd;
    bool _inGame = false;
    std::vector<ShellButton> _settings; // as buttons()
    NavigationBar _bar;
    LabelMap _text;
    ShellFont _font;
    OptionsChoice _hover = OptionsChoice::kNone;
    OptionsChoice _pressed = OptionsChoice::kNone;
    // Abort Mission's box: its picture, Yes and No, which is under the
    // pointer and pressed (0 none, 1 Yes, 2 No).
    bool _asking = false;
    image::Image _box;
    ShellButton _yes;
    ShellButton _no;
    int _askHover = 0;
    int _askPressed = 0;
    int askAt(int x, int y) const;
};

} // namespace reluva::storm3d
