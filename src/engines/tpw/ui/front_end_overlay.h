// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/events.h"
#include "common/types.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/ui_model.h"

#include <array>
#include <cstdint>

namespace reluva::tpw {

/**
 * What the front end shows over its scene until the Personnel File opens:
 * the advisor's portrait, top right, while he speaks, and a button, bottom
 * right, that skips the camera's path and then goes on to the Personnel
 * File. See docs/engines/tpw/games/simcoaster/systems/front-end.md#the-portrait-and-the-button.
 */
class FrontEndOverlay {
public:
    /** The button's two faces: ▷▷| while the camera moves, |▷ once it's over the globe. */
    enum class Button { kSkip, kPlay };

    /**
     * When the camera has come down over the globe and the button turns to
     * play; skipping jumps the front end's clock to here.
     */
    static constexpr uint32_t kGlobeMillis = 36500;
    /** When the Personnel File opens, if the player hasn't asked for it sooner. */
    static constexpr uint32_t kPersonnelFileMillis = 54000;

    /**
     * One of the advisor's lines: its number in the speech category
     * (global/Speech), when he says it and when his portrait shows, in
     * milliseconds from the start of the camera's path.
     */
    struct AdvisorLine {
        uint32_t speech;
        uint32_t says;
        uint32_t shownFrom;
        uint32_t shownTo;
    };
    /** The advisor's three lines, in order. */
    static const std::array<AdvisorLine, 3> &advisorLines();

    /** Reads the models: the portrait, its frame and the button's two faces. */
    Error load(const SearchSet &files);

    /**
     * Whether the advisor is shown at a time, in milliseconds from the
     * start of the camera's path.
     */
    static bool advisorShown(uint32_t ms);
    /** The button's face at a time, in milliseconds from the start of the camera's path. */
    static Button buttonAt(uint32_t ms);
    /** Where the button is, at 640 × 480. */
    static ScreenRect buttonRect();

    /**
     * Follows the mouse over the button, to light it.
     * @return True if the event presses the button: a click on it, or any
     *     key.
     */
    bool handle(const Event &ev);

    /** Draws the overlay over an RGBA32 surface of 640 × 480 at a time from the start of the camera's path. */
    void draw(Surface &screen, uint32_t ms) const;

private:
    LoadedModel _frame;
    LoadedModel _portraits;
    LoadedModel _skip;
    LoadedModel _play;
    bool _lit = false; // the mouse over the button
};

} // namespace reluva::tpw
