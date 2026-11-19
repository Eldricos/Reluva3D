// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "image/image.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// A game's own window over its scenes, which the other game hasn't (And
// Then There Were None's journal): the shared scene page updates and
// draws it, gives it clicks first and asks it of verbs it doesn't know.

namespace reluva::awe {

struct GameState;
class GameFonts;

/** What a tool's hand wants of a game's own window (GameOverlay::hand). */
enum class OverlayWant {
    kNothing, ///< nothing in hand, the window shut, so the scene can be clicked
    kHold,    ///< the item in hand, the window shut
    kCombine, ///< the items put together by its own button
    kExamine, ///< the items examined by its own button
};

/** A tool's next input to the window: wait a frame, a click, the click that does it, or nothing more needed. */
struct OverlayHand {
    enum class Kind { kWait, kClick, kDone, kReady } kind = Kind::kWait;
    int x = 0;
    int y = 0;
};

/** What a game's own window needs from the scene page around it. */
struct OverlayHost {
    GameState &state;
    const Archive &files;
    /** A picture of the game's, loaded once; an empty image if it hasn't one. */
    std::function<const image::Image &(const std::string &file)> picture;
    /** A sound of the game's played once (its path, "sound\interface\click1.wav"). */
    std::function<void(const std::string &file)> sound;
    /** interface.txt's text for a tag. */
    std::function<std::string(const std::string &tag)> text;
    /** A script run on the main runner as the inventory runs one: the player stopped, what ran dropped. */
    std::function<void(const std::string &script)> run;
};

/** A game's own window over its scenes. */
class GameOverlay {
public:
    virtual ~GameOverlay() = default;
    /** Moves it on by `seconds`. */
    virtual void update(float seconds) = 0;
    /** Draws it over `dst` (the game's frame), when the interface is shown. */
    virtual void draw(Surface &dst, const GameFonts &fonts) = 0;
    /** A click at (x, y): true if it took it. */
    virtual bool click(int x, int y) = 0;
    /** The mouse moved to (x, y). */
    virtual void mouseMove(int x, int y) {
        (void)x;
        (void)y;
    }
    /** A right click, when the player can act: true if it took it. */
    virtual bool rightClick() { return false; }
    /** A key (its character, lower case, or reluva's key code): true if it took it. */
    virtual bool key(uint32_t code) {
        (void)code;
        return false;
    }
    /** A script verb of its own (`words[0]` lower case): true if it's one, done. */
    virtual bool verb(const std::vector<std::string> &words) = 0;

    // For the tools that play the game (awe game-run -solve): what its
    // inventory does with items, without the mouse.

    /** The items put together (`parts`), or one taken apart: false if it can't be tried. */
    virtual bool combine(const std::vector<std::string> &parts) {
        (void)parts;
        return false;
    }
    /** The groups of items in the bag that examining would do something with. */
    virtual std::vector<std::vector<std::string>> examinable() const { return {}; }
    /** Those items examined: false if it can't be tried. */
    virtual bool examine(const std::vector<std::string> &items) {
        (void)items;
        return false;
    }
    /** Whether a click at (x, y) would be its, not the scene's (for the tools' choice of where to click). */
    virtual bool covers(int x, int y) const {
        (void)x;
        (void)y;
        return false;
    }
    /**
     * Whatever it shows that waits for a click (a close-up) put away.
     * @param click as a click puts it away (what it says then said), not as
     *        opening the menu does
     */
    virtual void dismiss(bool click) { (void)click; }
    /** An act is changing: whatever slides is put back at once. */
    virtual void actChanging() {}
    /** The next input a player's mouse would make there towards `want` (awe game-check's hand). */
    virtual OverlayHand hand(OverlayWant want, const std::vector<std::string> &items) {
        (void)want;
        (void)items;
        return {OverlayHand::Kind::kReady};
    }
};

} // namespace reluva::awe
