// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/bzn.h"
#include "engines/storm3d/formats/label_map.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/shell/settings_screen.h"
#include "engines/storm3d/shell/shell_draw.h"
#include "image/image.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace reluva::storm3d {

/** What the player chose on the Game Setup screen. */
enum class SetupChoice { kNone, kPreviousMenu, kMainMenu, kLaunch };

/**
 * The Game Setup screen Instant Action opens, as the game draws it at
 * 640 x 480 over `bitmaps/multiset/mpsBG.bmp`: eight players' slots
 * (who, their side and team), the colours, the messages, the map, and
 * the game's options, with LAUNCH. See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class GameSetupScreen {
public:
    /** Who a slot is: its words' key in label.map's multiplayer_setup ("empty", "computer_medium"), or the player. */
    enum class Who { kEmpty, kPlayer, kComputerEasy, kComputerMedium, kComputerHard };
    /** A player's slot. */
    struct Slot {
        Who who = Who::kEmpty;
        int side = 0;   // into sides(): Random, then the races
        int team = 0;   // 0 for Team 1
        int colour = 0; // into teamcolor.odf's first nine: 0 none, 1 white, 2 red...
    };
    /** An option's control: its look, its place, its values' keys in label.map. */
    struct Option {
        enum Look { kList111, kToggle111, kList128, kToggle128, kList58 } look;
        int x;
        int y;
        // Each value's words; "name|value" for a list that says both
        // ("Dilithium: 3000").
        std::vector<const char *> values;
        int initial;
    };
    /** The options down the right and below the map, in the order drawn. */
    static const std::vector<Option> &options();
    /** The sides a slot can take, as label.map's keys: random first. */
    static const std::vector<const char *> &sides();
    /** An option's control's width, by its look. */
    static int widthOf(Option::Look look);
    /** Whether an option is a list, which opens upwards while pressed, rather than a toggle. */
    static bool isList(const Option &option);
    /** The control under a point: a slot's part (0 who, 1 side, 2 team) as 100 x slot + part, an option as 1000 + its index, the bottom bar's as 2000 to 2002; -1 if none. */
    static int targetAt(int x, int y);
    /** A colour's name in teamcolor.odf, by its swatch ("white" for 1). */
    static const char *colourName(int colour);
    /**
     * A slot's race's definition ("fed"): its side's, or for Random one of
     * the four in turn by the slot, until launch rolls it (randomSide).
     */
    static std::string raceOf(const Slot &slot, size_t index);
    /**
     * The side, into sides(), a Random slot becomes at launch, as the game
     * rolls it (0x43e8ad): `roll` 0 to 3, its Federation, Borg, Romulan
     * and Klingon.
     */
    static int randomSide(int roll);
    /**
     * Start positions for `players` in turn at launch, as the game deals
     * them (0x43e7ee): each, by `below(n)` (0 to n - 1), one of `starts`
     * not yet taken.
     */
    static std::vector<int> placeAtRandom(std::vector<int> starts, size_t players,
                                          const std::function<int(int)> &below);
    /** The map's start positions: the n of its Camera_<n> paths, 1 to 8. */
    static std::vector<int> startsOf(const BznMap &map);
    /** The map chosen's start positions. */
    const std::vector<int> &starts() const { return _starts; }
    /** Whether players are placed at random (the option says "Random Placement", as it starts). */
    bool randomPlacement() const;
    /** The tech level chosen, 0 (Standard Tech) to 6: which of techlvl.odf's trees every player builds by. */
    int techLevel() const;
    /**
     * Whether it's Teams, as it starts ("Teams", label.map's
     * alliances_teams_off), rather than Alliances: with Teams every
     * player's Allied Victory is on, and can't be turned off *(inferred
     * from 0x4a01e2)*.
     */
    bool teams() const;
    /** The starting dilithium, crew and most officers chosen (their options' numbers). */
    int dilithium() const { return amount(4); }
    int crew() const { return amount(5); }
    int officers() const { return amount(6); }
    /** Whether the shroud is on (its option says "Shroud On"). */
    bool shroud() const;
    /** Whether the fog is on (its option says "Fog On"). */
    bool fog() const;
    /** How many players a map takes: its `Camera_<n>` paths. */
    static int playersOf(const BznMap &map) { return int(startsOf(map).size()); }

    /**
     * Reads its pictures, text and the map, `name` the player's.
     * @return a failure if one isn't there
     */
    Error load(const GameFiles &files, const std::string &name);
    /** The slots, as the game starts them: the player, a computer at medium, the rest empty. */
    const std::array<Slot, 8> &slots() const { return _slots; }
    /** The map chosen ("4first.bzn"). */
    const std::string &map() const { return _map; }
    /** An option's value, into its values. */
    int option(size_t i) const { return _values[i]; }

    /** @return whether the pointer came onto a control (the game plays a sound) */
    bool pointerAt(int x, int y);
    void press(int x, int y);
    /** Let go at (x, y): a control changed, or what was chosen. */
    SetupChoice release(int x, int y);

    void draw(Surface &screen);

    /** Who a slot is, in words: the player's name, "Computer Medium"... */
    std::string whoText(const Slot &slot) const;
    /** A slot's side, in words ("Federation"): raceOf's for Random. */
    std::string sideText(const Slot &slot, size_t index) const;

private:
    int amount(size_t option) const; // the number an option's value says
    std::vector<int> _starts;

    image::Image _background;
    std::array<std::array<image::Image, 3>, 3> _slotLooks;   // who, side, team: at rest, lit, pressed
    std::array<std::array<image::Image, 3>, 5> _optionLooks; // as Option::Look
    ShellButton _up;
    ShellButton _down;
    image::Image _thumb;
    NavigationBar _bar;
    LabelMap _text;
    ShellFont _font;
    std::vector<std::array<uint8_t, 3>> _colours; // teamcolor.odf's first nine
    std::array<Slot, 8> _slots{};
    std::vector<int> _values; // each option's
    std::string _map;
    int _players = 0;
    std::string _name;
    int _hover = -1;
    int _pressed = -1;
    int _open = -1; // the option whose list is open, while the button is held
    // An open list's item under a point: its value; -1 if none.
    int itemAt(int x, int y) const;
};

} // namespace reluva::storm3d
