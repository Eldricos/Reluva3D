// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game's multiplayer menus, redrawn as the others are (front_end.h):
// the sessions found and the net name, joining by address, the host's
// options and race settings, and the lobby with its players, teams, chat
// and readiness. They run a NetSession (net_session.h). See
// docs/engines/age-menus.md and docs/engines/age-multiplayer.md.

#include "common/system.h"
#include "engines/age/ui/front_end.h"
#include "common/portmap.h"
#include "engines/age/game/net_session.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

class NetMenus {
public:
    enum class Screen { kSessions, kAddress, kPassword, kHostOptions, kHostRace, kLobby, kEject };
    enum class Action {
        kNone,
        kBack,   // out to the main menu
        kGarage, // the player wants to choose a car
        kLoad,   // the host started: load the game
        kLobby,  // the game's over: back in the lobby
    };

    NetMenus(System &sys, const SearchSet &files, const std::vector<CityInfo> &cities, const std::vector<CarInfo> &cars,
             const GameStrings &strings);
    ~NetMenus();
    bool loaded() const { return _ok; }

    // From the main menu: the sessions on the local network.
    void enter();
    Action handle(const Event &ev);
    // The session's news (call every frame, in the menus or the game).
    Action update(uint64_t nowMs);
    void draw(Surface &overlay, uint64_t nowMs);

    Screen screen() const { return _screen; }
    NetSession *session() { return _session.get(); }
    // Back from the garage, with the car chosen, or from the game.
    void carChosen(const std::string &car, int paintJob);
    void showLobby();
    void leave(); // out of the session
    // What's been said and what's happened (joined, left), newest last,
    // for the game's screen.
    struct Line {
        std::string text;
        uint64_t atMs = 0;
        bool system = false; // the game's words, not a player's
    };
    const std::deque<Line> &lines() const { return _lines; }
    // A notice to show, from the game's strings where it has one.
    std::string noticeText(const NetNotice &n) const;
    // The race of the session's settings: its kind (Cruise and Cops &
    // Robbers have none).
    static bool hasRace(NetMode mode) {
        return mode == NetMode::kBlitz || mode == NetMode::kCheckpoint || mode == NetMode::kCircuit;
    }
    static RaceKind raceKind(NetMode mode);
    std::string modeName(NetMode mode) const;
    std::string goldMassName(int kg) const;
    std::string timeLimitName(int minutes) const;
    std::string goldModeName(GoldMode mode) const;
    std::string teamName(uint8_t team) const; // in the session's mode
    // The car a player drives: its own, but the police car for a cop.
    std::string carFor(const NetPlayer &p) const;

    // Typing goes to the focused field; the race screen lets Esc through.
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
    std::string _netName = "Player", _address, _password, _chat;
    NetSettings _settings; // the host's, as edited
    bool _editing = false; // the host's settings from the lobby (else hosting anew)
    std::vector<std::string> _raceNames;
    size_t _ejectChoice = 0;
    net::Address _pendingJoin; // a session that wants a password first
    uint32_t _lastChatSeq = 0;
    std::string _message, _messagePicture; // a dialog over the screen, until a key
    std::deque<Line> _lines;
    uint64_t _nowMs = 0;
    std::string _myCar = "vpbug";
    int _bots = 0;                                // computer players to host with
    std::shared_ptr<net::LoopbackNetwork> _local; // hosting with no network: this computer only
    static constexpr const char *kBotsField = "Computer Players";
    int _myPaint = 0;
};

} // namespace reluva::age
