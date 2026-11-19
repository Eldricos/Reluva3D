// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game's multiplayer menus, redrawn as the others are (front_end.h):
// the sessions found and the net name, joining by address, the host's
// options and race settings, and the lobby with its players, teams, chat
// and readiness. They run a NetSession (net_session.h). See
// docs/engines/age/games/mm2/systems/multiplayer.md and docs/engines/age/games/mm2/systems/multiplayer.md.

#include "common/system.h"
#include "engines/age/ui/front_end.h"
#include "common/portmap.h"
#include "engines/age/game/net_session.h"

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

/**
 * The multiplayer menus and the session they run, from the sessions found
 * to the lobby; it stays alive through the game, for the session's news.
 */
class NetMenus {
public:
    /** The screen shown; the address, password, host options and eject screens are dialogs over another. */
    enum class Screen { kSessions, kAddress, kPassword, kHostOptions, kHostRace, kLobby, kEject };
    /** What the menus want the game to do next. */
    enum class Action {
        kNone,
        kBack,   // out to the main menu
        kGarage, // the player wants to choose a car
        kLoad,   // the host started: load the game
        kLobby,  // the game's over (back in the lobby), or the session closed (back to the sessions)
    };

    /** Reads the menus' layout (tune/widget.csv) and pictures; loaded() says whether it all came. */
    NetMenus(System &sys, const SearchSet &files, const std::vector<CityInfo> &cities, const std::vector<CarInfo> &cars,
             const GameStrings &strings);
    /** Leaves the session, if there is one. */
    ~NetMenus();
    /** Whether the layout, the background and at least one city and car were found. */
    bool loaded() const { return _ok; }

    /** From the main menu: the sessions on the local network, leaving any session we were in. */
    void enter();
    /** Takes a key, click or typing; a message on screen takes the first key or click to put it away. */
    Action handle(const Event &ev);
    /** The session's news (call every frame, in the menus or the game). */
    Action update(uint64_t nowMs);
    /** Draws the screen, and any dialog over it, into `overlay`. */
    void draw(Surface &overlay, uint64_t nowMs);

    /** The screen shown. */
    Screen screen() const { return _screen; }
    /** The session we're in, or nullptr; owned by the menus. */
    NetSession *session() { return _session.get(); }
    /** Back from the garage, with the car chosen, or from the game. */
    void carChosen(const std::string &car, int paintJob);
    /** Goes to the lobby, or to the sessions found if we're in no session. */
    void showLobby();
    /** Out of the session, closing what hosting opened. */
    void leave();
    /** A line of what's been said and what's happened, for the game's screen. */
    struct Line {
        std::string text;
        uint64_t atMs = 0;
        bool system = false; // the game's words, not a player's
    };
    /** What's been said and what's happened (joined, left): the last 50, newest last. */
    const std::deque<Line> &lines() const { return _lines; }
    /** A notice to show, from the game's strings where it has one; empty for a start. */
    std::string noticeText(const NetNotice &n) const;
    /** Whether the mode is a race, with a race of the city's to choose (Cruise and Cops & Robbers aren't). */
    static bool hasRace(NetMode mode) {
        return mode == NetMode::kBlitz || mode == NetMode::kCheckpoint || mode == NetMode::kCircuit;
    }
    /** The kind of race the mode's races are; Blitz for a mode with none. */
    static RaceKind raceKind(NetMode mode);
    /** The mode's name, in the game's words. */
    std::string modeName(NetMode mode) const;
    /** The gold's mass in the game's words: the heaviest of Weightless, Quarter and Half Ton not over `kg`. */
    std::string goldMassName(int kg) const;
    /** A time limit in the game's words, or "N minutes" for one it has no words for. */
    std::string timeLimitName(int minutes) const;
    /** The Cops & Robbers choice's name, in the game's words. */
    std::string goldModeName(GoldMode mode) const;
    /** A side's name in the session's mode: robbers and cops (0, 1), or red and blue. */
    std::string teamName(uint8_t team) const;
    /** The car a player drives: its own, but the police car for a cop. */
    std::string carFor(const NetPlayer &p) const;

    /** Typing goes to the focused field; the race screen lets Esc through. */
    bool typing() const;

private:
    struct Item {
        std::string widget;  // widget.csv's name
        std::string picture; // the button's texture (else the widget's name)
        bool button = false; // else a field
        bool enabled = true;
        bool text = false; // a field typed in
        int states = 4;    // the picture's
    };
    std::vector<Item> items() const;
    const Widget *widget(const std::string &name) const;
    const Widget *widget(const std::string &name, const std::string &menu) const;
    std::string menuName() const;
    int originX() const; // dialogs' widgets are from their corner
    int originY() const;
    void go(Screen s);
    void activate(const Item &item, Action &action);
    void change(const Item &item, int step);
    std::string *textOf(const std::string &widget);
    bool hit(const Item &item, int x, int y) const;
    void text(Surface &o, const std::string &widget, const std::string &s, Color c = {255, 230, 140}) const;
    void say(const std::string &s, bool system);
    void startHosting();
    std::map<std::string, float> topSpeeds() const;
    void joinAddress(const net::Address &address);
    void refreshRaces();
    void message(const std::string &text, const std::string &picture = "msg_dlg");
    void drawDialog(Surface &o, const std::string &picture, const std::string &title) const;
    void drawLobby(Surface &o) const;
    void drawHostRace(Surface &o) const;

    System &_sys;
    const SearchSet &_files;
    const std::vector<CityInfo> &_cities;
    const std::vector<CarInfo> &_cars;
    const GameStrings &_strings;
    mutable MenuPictures _pictures;
    std::vector<Widget> _widgets;
    bool _ok = false;

    Screen _screen = Screen::kSessions;
    int _focus = 0;
    std::unique_ptr<NetSession> _session;
    // Hosting on the network: the router asked to pass the port on, and
    // whether what came of it has been said.
    std::unique_ptr<net::PortMapper> _mapper;
    bool _mapperSaid = false;
    std::unique_ptr<NetBrowser> _browser;
    size_t _chosenSession = 0;
    std::string _netName = "Player";
    std::string _address;
    std::string _password;
    std::string _chat;
    NetSettings _settings; // the host's, as edited
    bool _editing = false; // the host's settings from the lobby (else hosting anew)
    std::vector<std::string> _raceNames;
    size_t _ejectChoice = 0;
    net::Address _pendingJoin; // a session that wants a password first
    uint32_t _lastChatSeq = 0;
    // a dialog over the screen, until a key
    std::string _message;
    std::string _messagePicture;
    std::deque<Line> _lines;
    uint64_t _nowMs = 0;
    std::string _myCar = "vpbug";
    int _bots = 0;                                // computer players to host with
    std::shared_ptr<net::LoopbackNetwork> _local; // hosting with no network: this computer only
    static constexpr const char *kBotsField = "Computer Players";
    int _myPaint = 0;
};

} // namespace reluva::age
