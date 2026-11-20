// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/ui/layout_screen.h"

#include <array>
#include <map>
#include <string>

// The options screen (options.wdf), opened from the main menu: the four
// volumes, tool tips, tactical combat, random events, autosave and the two
// timers. What each control is, and its defaults, are the manual's
// (chapter 13); its words lexicon.dic's; which widget is which and how it
// looks, a screenshot of the game's (docs/engines/botf/systems/screens.md).

namespace reluva::botf {

/** What the options screen sets. */
struct GameOptions {
    static constexpr int kMaxVolume = 127; // the sliders' maximum, from the layout

    /**
     * Voice, video playback, background music and sound effects, 0 (off) to
     * kMaxVolume; at first as a fresh install's Stbof.ini has them.
     */
    std::array<int, 4> volume{100, kMaxVolume, 100, 100};
    bool toolTips = true;
    bool manualCombat = true;
    bool randomEvents = true;
    bool autosave = true;
    int strategicTimer = 0; // 0 none; the other choices aren't known yet
    int tacticalTimer = 0;  // the same
};

/**
 * The options as Stbof.ini keeps them (docs/engines/botf/systems/game-files.md);
 * the defaults for what it doesn't have.
 */
GameOptions optionsFromIni(const std::map<std::string, std::string> &ini);

/** The options screen. */
class OptionsScreen : LayoutScreen {
public:
    using LayoutScreen::setSoundHandler;
    enum class Result { kOpen, kAccepted, kCancelled };

    /** `lexicon` may be empty: the words are then the English ones. */
    OptionsScreen(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    Error load(); // the two layouts
    /** Shows `options`, to be changed until Accept or Cancel. */
    void open(const GameOptions &options) { _options = options; }
    void draw(Surface &screen) const;
    /**
     * Mouse clicks on the controls (a click on a setting changes it; a click
     * or a drag on a volume sets it), and Return/Escape for Accept/Cancel.
     */
    Result handle(const Event &event);

    /** The options as they are on the screen: after Accept, the ones chosen. */
    const GameOptions &options() const { return _options; }

private:
    void click(int32_t id);
    void setVolume(int32_t slider, int x);

    GameOptions _options;
    int32_t _dragging = 0; // the volume slider the mouse went down on, or 0
};

} // namespace reluva::botf
