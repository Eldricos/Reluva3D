// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/ui/animation.h"
#include "engines/stawayteam/ui/font.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * The interface around the game's view, laid out by Rules.txt's Custom
 * Rules: the team bar down the left, the button box, the info bar with the
 * minimap that pops up under the mouse, the inventory and the pause button
 * (docs/engines/stawayteam/systems/maps.md).
 */
class GameHud {
public:
    /** What an event on it asks of the game. */
    struct Result {
        bool used = false; // it was over the interface, not the map
        int select = -1;   // an officer chosen on the team bar
        bool moveCamera = false;
        bool cycleMove = false;   // the move button pressed: walk, run, crouch
        bool selectAll = false;   // the select-all button pressed
        bool toggleSound = false; // the sound button pressed: the team's noises shown or not
        bool objectives = false;  // the objectives button pressed
        bool pause = false;       // the pause button pressed: time stopped or going again
        bool cancelOrders = false;
        bool vision = false;         // the vision button pressed: its state on
        bool options = false;        // the escape button pressed
        bool toggleStun = false;     // the chosen item's panel's STUN or KILL pressed
        bool detonate = false;       // the remote mine's panel's DETONATE pressed
        bool breakMeld = false;      // the Psychic Rack's panel's BREAK CONTROL pressed
        bool centreMeld = false;     // the Psychic Rack's panel's CENTER pressed
        bool toggleCharge = false;   // the cloaking band's panel's ACTIVATE or DEACTIVATE pressed
        bool toggleOverload = false; // the phaser's panel's OVERLOAD pressed
        int item = -1;               // an item chosen on the inventory: its place in what setInventory gave
        int step = 0;                // the inventory's Next (1) or Previous (-1) button pressed
        float cameraX = 0.0f;        // where, for moveCamera: the view's middle on the map
        float cameraY = 0.0f;
    };

    /**
     * The interface for `team`, its minimap `minimap` (of Cache/Levels) for
     * a map `mapW` x `mapH`; `data` must outlive it.
     */
    GameHud(const Archive &data, const TextObject &rules, std::vector<TeamMember> team, const std::string &minimap,
            int mapW, int mapH);

    Result handleEvent(const Event &ev, uint64_t now);
    /** Another officer on the team bar, after the rest: one who's joined. */
    void addMember(const TeamMember &member);
    /**
     * Draws it, officer `selected` lit, the view's place on the minimap from
     * the camera (the view's middle) and the view's size.
     */
    void draw(Surface &screen, uint64_t now, int selected, float cameraX, float cameraY, int viewW, int viewH);

    /** How the chosen officer moves, as the move button shows it: 0 walk, 1 run, 2 crouch. */
    void setMove(int move) { _move = move; }
    /** Which officers are chosen, their slots lit. */
    void setChosen(std::vector<bool> chosen) { _chosen = std::move(chosen); }
    /** An item the inventory shows: its definition, and its uses left (-1 as many as wanted). */
    struct Held {
        std::string name;
        int count = -1;
    };
    /**
     * One of the inventory bar's places, an item type's: the item it shows,
     * its place in the items (-1 none), how many of the officer's are of
     * that type, and those items, as the full inventory shows them.
     */
    struct Place {
        int item = -1;
        int count = 0;
        std::vector<int> items;
    };
    /** The chosen officer's items, the bar's places, and the item they've chosen (-1 none). */
    void setInventory(std::vector<Held> items, std::vector<Place> places, int chosen) {
        _held = std::move(items);
        _places = std::move(places);
        _heldChosen = chosen;
    }
    /**
     * Whether the full inventory's open: a right-click on the bar opens it,
     * and closes it.
     */
    bool fullInventory() const { return _full; }
    /** Whether the chosen item's name shows over the bar, as it's just been chosen. */
    void setItemTitle(bool shown) { _itemTitle = shown; }
    /** Something marked on the minimap, where it is on the map. */
    struct Mark {
        enum Kind { kOfficer, kEnemy, kInterest };
        float x = 0.0f;
        float y = 0.0f;
        Kind kind = kOfficer;
    };
    void setMarks(std::vector<Mark> marks) { _marks = std::move(marks); }
    /** The info bar's open button pressed: held up, or let down again. */
    void toggleInfoBar() { _pinned = !_pinned; }
    /** Whether the mouse was over the interface, as the last event left it. */
    bool mouseOver() const { return _mouseOver; }
    /** Whether the sound button shows the team's noises. */
    void setListening(bool on) { _listening = on; }
    /**
     * The chosen item's panel on the info bar: whether it's set to stun,
     * and what a tricorder's said, `typed` characters of it shown.
     */
    void setItemPanel(bool stun, std::string scan, size_t typed) {
        _stun = stun;
        _scan = std::move(scan);
        _scanTyped = typed;
    }
    /** What the chosen officer's weapons' damage is multiplied by: more while they're enhanced. */
    void setEnhancement(float factor) { _enhancement = factor; }
    /** How long till the chosen item goes off, overloaded, in seconds; below 0 it isn't. */
    void setOverload(float secondsLeft) { _overload = secondsLeft; }
    /** A charged item's charge (0-100), and whether it's on. */
    void setCharge(float energy, bool on) {
        _energy = energy;
        _chargeOn = on;
    }
    /** Whether time's stopped, the pause button held down. */
    void setPaused(bool paused) { _paused = paused; }
    /** The vision button's state: 0 off, 1 choosing an enemy, 2 showing its sight. */
    void setVision(int state) { _vision = state; }
    /** Each officer's health, as a part of their whole (0-1). */
    void setHealth(std::vector<float> health) { _health = std::move(health); }
    /** Which officers are on the team, on the bar in order; others left off it. */
    void setOnTeam(std::vector<bool> onTeam) { _onTeam = std::move(onTeam); }

private:
    // The officer in the team bar's slot `slot`, from the top; -1 for none.
    int officerInSlot(int slot) const;
    // The info bar's top on the screen at `now`: rising as it opens.
    int infoTop(uint64_t now);
    bool overInfoBar(int x, int y, uint64_t now);
    const image::Image &picture(const std::string &name);
    // The chosen officer's chosen item, and its panel drawn at (x, y).
    const ItemDefinition *chosenItem() const;
    void drawItemPanel(Surface &screen, int x, int y, uint64_t now);
    // The minimap's marks, its top left corner at (left, top).
    void drawMarks(Surface &screen, int left, int top, uint64_t now);
    const Animation &sequence(const std::string &name);
    const Font &panelFont(const std::string &name);

    const Archive &_data;
    const TextObject *_custom;
    std::vector<TeamMember> _team;
    std::vector<image::Image> _heads;
    std::map<std::string, ItemDefinition> _items;
    std::map<std::string, image::Image> _pictures; // of Interface/Screens
    std::map<std::string, image::Image> _icons;    // the items', by name
    image::Image _minimap;
    int _mapW = 0;
    int _mapH = 0;
    Font _font;
    std::map<std::string, Font> _fieldFonts;
    // The info bar: how open it is (0 to 1) and since when it's moving.
    float _open = 0.0f;
    bool _opening = false;
    uint64_t _last = 0;
    int _move = 0;
    std::vector<bool> _chosen;
    std::vector<float> _health;
    std::vector<bool> _onTeam;
    bool _listening = false;
    bool _paused = false;
    int _vision = 0;
    bool _stun = false;
    float _enhancement = 1.0f;
    float _overload = -1.0f; // seconds till the chosen item goes off; below 0 not overloaded
    float _energy = 100.0f;
    bool _chargeOn = false;
    std::string _scan;
    size_t _scanTyped = 0;
    std::map<std::string, Animation> _sequences; // the panels', by name
    std::map<std::string, Font> _panelFonts;
    // The info bar's corner on the screen, the chosen item's panel's
    // buttons placed from it.
    int _barX = 0;
    int _barY = 0;
    std::vector<Held> _held;
    std::vector<Place> _places;
    int _heldChosen = -1;
    bool _itemTitle = false;
    std::vector<Mark> _marks;
    // The points of interest's squares pulse: their size's phase, 0 to 4.
    float _pulse = 0.0f;
    uint64_t _pulseLast = 0;
    bool _pinned = false; // the info bar held up by its open button
    bool _mouseOver = false;
    // Where each item shown is on the screen, and its place in _held.
    struct Slot {
        int x;
        int y;
        int item;
    };
    std::vector<Slot> _slots;
    // The full inventory: open, its items as drawn, and when the mouse left
    // it and the bar (0: it hasn't).
    bool _full = false;
    std::vector<Slot> _fullSlots;
    uint64_t _fullLeft = 0;
    int _mouseX = -1;
    int _mouseY = -1;
};

} // namespace reluva::stawayteam
