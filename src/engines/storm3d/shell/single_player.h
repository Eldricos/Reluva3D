// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/label_map.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/shell/panel_movie.h"
#include "engines/storm3d/shell/shell_draw.h"
#include "image/image.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/** What the player chose on the single-player screen. */
enum class SingleChoice { kNone, kPreviousMenu, kLoadGame, kStartMission };

/**
 * The single-player screen, as the game draws it at 640 x 480
 * (`bitmaps/single/singlebk.bmp`): a panel for each race's campaign
 * playing its movie, the races not yet open saying which campaign comes
 * first, and over it, for a race chosen, the mission panel listing its
 * missions open so far. See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class SinglePlayerScreen {
public:
    /** A race's campaign: its movie (animations/<movie>01.bik, 02 under the pointer), its panel's place. */
    struct Race {
        const char *movie;
        const char *name; // as label.map's single_player section names its campaign ("federation")
        int x;
        int y;
    };
    /** The four campaigns, in the order they open. */
    static const std::vector<Race> &races();
    /** The race whose panel is at a point; -1 if none. */
    static int raceAt(int x, int y);
    /**
     * Each campaign's missions as `mshell.set` lists them: lines of map
     * names, six to a campaign ("none" for none), the races in races()'
     * order, then the finale.
     */
    static std::vector<std::vector<std::string>> campaignsOf(std::string_view mshell);

    /**
     * Reads its pictures, movies, text and campaigns, starting the movies
     * at `millis`.
     * @return a failure if a picture or the text isn't there
     */
    Error load(const GameFiles &files, uint64_t millis);
    /**
     * How far the player has come: campaigns finished, and missions open
     * in the next. A new player has finished none and has one open.
     */
    void setProgress(int campaignsDone, int missionsOpen) {
        _campaignsDone = campaignsDone;
        _missionsOpen = missionsOpen;
    }

    /** @return whether the pointer came onto something (the game plays a sound) */
    bool pointerAt(int x, int y, uint64_t millis);
    void press(int x, int y);
    /** Let go at (x, y): what the player chose, if anything. */
    SingleChoice release(int x, int y, uint64_t millis);
    /** The mission chosen to start: its map ("fed1.bzn"). */
    const std::string &mission() const { return _mission; }

    void draw(Surface &screen, uint64_t millis);

private:
    // What's under the pointer: a race, a button, a mission's bar.
    struct Target {
        enum Kind { kNone, kRace, kPrevious, kLoad, kMission, kOk, kCancel } kind = kNone;
        int index = 0;
        bool operator==(const Target &o) const { return kind == o.kind && index == o.index; }
    };
    Target targetAt(int x, int y) const;
    bool open(int race) const { return race <= _campaignsDone; }
    std::vector<std::string> openMissions(int race) const;
    int barY(int i) const;

    image::Image _background;
    image::Image _title;
    image::Image _panel;
    image::Image _heading;
    ShellButton _previous;
    ShellButton _load;
    ShellButton _ok;
    ShellButton _cancel;
    std::array<image::Image, 3> _bar; // a mission's: at rest, under the pointer, chosen
    std::vector<PanelMovie> _rest;    // as races()
    std::vector<PanelMovie> _lit;
    LabelMap _text;
    ShellFont _font;
    std::vector<std::vector<std::string>> _campaigns;
    int _campaignsDone = 0;
    int _missionsOpen = 1;
    Target _hover;
    Target _pressed;
    int _panelRace = -1; // the mission panel's race; -1 while it's closed
    int _chosen = 0;     // the mission chosen in it
    std::string _mission;
};

} // namespace reluva::storm3d
