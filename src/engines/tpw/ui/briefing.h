// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/ui_model.h"

#include <functional>
#include <string>

namespace reluva::tpw {

/**
 * The mission's briefing: the "Current Objective" window over the park as
 * it opens, the president's letter (Operation Expand!'s for the first
 * mission) or, turned over, the objective's requirement and how far the
 * park is with it. See docs/engines/tpw/games/simcoaster/systems/briefing.md.
 */
class Briefing {
public:
    enum class Control {
        kNone,
        kObjectivesTab,
        kChallengesTab,
        kTurn, // "Go to Details", "Go to Briefing"
        kProceed,
        kCancel, // the red cross
        kClose,  // the window's own cross, top right
    };

    /** What the window shows. */
    struct View {
        bool details = false; // the requirement and status, not the letter
        Control hovered = Control::kNone;
    };

    /** Reads the window's models, fonts and strings. */
    Error load(const SearchSet &files);

    /**
     * Draws the window for a park's mission over an RGBA32 surface of 640
     * × 480; the letter is to `player`.
     */
    void draw(Surface &screen, const Park &park, const std::string &player, const View &view) const;

    /** The control at a point of the screen; kNone for none. */
    static Control controlAt(int x, int y);
    /** Where a control is, at 640 × 480. */
    static ScreenRect placeOf(Control control);

    /** The letter's lines for a park's mission: From, To, Subject, then the letter. */
    std::vector<std::string> letter(const Park &park, const std::string &player) const;
    /** The details' lines: the requirement, then the status. */
    std::vector<std::string> details(const Park &park) const;
    /** What the letter says with no objective here: that it's done and where the next is, or all are. */
    std::string noObjective(const Park &park) const;

private:
    void drawText(Surface &screen, const Font &font, int x, int y, const std::string &text, Color c) const;
    void drawCentred(Surface &screen, const Font &font, const ScreenRect &in, int y, const std::string &text,
                     Color c) const;

    LoadedModel _window;
    LoadedModel _panel;
    LoadedModel _frame;
    LoadedModel _pictures;
    LoadedModel _portraits;
    LoadedModel _tab;
    LoadedModel _bar;
    LoadedModel _button;
    LoadedModel _cancel;
    LoadedModel _help;
    LoadedModel _close;
    CharacterSet _characters;
    Font _title;
    Font _name;
    Font _small;
    Font _label;
    StringTable _ui;
    StringTable _operations;
    StringTable _missions;
    StringTable _statistics;
    StringTable _tags;
    StringTable _themes;
    StringTable _abilities;
    StringTable _staffTypes;
    StringTable _ranks;
};

/**
 * Shows the briefing over the park until the player proceeds or closes
 * it, the park's clock stopped meanwhile; `backdrop` draws the park.
 * @return False if the window was closed (or the launcher asked for).
 */
bool showBriefing(System &sys, const Briefing &briefing, Park &park, const std::string &player,
                  const std::function<void(Surface &)> &backdrop);

} // namespace reluva::tpw
