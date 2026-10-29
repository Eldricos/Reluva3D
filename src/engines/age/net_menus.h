// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game's multiplayer menus, redrawn as the others are (front_end.h):
// the sessions found and the net name, joining by address, the host's
// options and race settings, and the lobby with its players, teams, chat
// and readiness. They run a NetSession (net_session.h). See
// docs/engines/age-menus.md and docs/engines/age-multiplayer.md.

#include "common/system.h"
#include "engines/age/front_end.h"
#include "engines/age/net_session.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

class NetMenus {
public:
    enum class Screen { Sessions, Address, Password, HostOptions, HostRace, Lobby, Eject };
    enum class Action {
        None,
        Back,   // out to the main menu
        Garage, // the player wants to choose a car
        Load,   // the host started: load the game
        Lobby,  // the game's over: back in the lobby
    };

    NetMenus(System& sys, const SearchSet& files, const std::vector<CityInfo>& cities, const std::vector<CarInfo>& cars,
             const GameStrings& strings);
    ~NetMenus();
    bool loaded() const { return ok_; }

    // From the main menu: the sessions on the local network.
    void enter();
    Action handle(const Event& ev);
    // The session's news (call every frame, in the menus or the game).
    Action update(uint64_t nowMs);
    void draw(Surface& overlay, uint64_t nowMs);

    Screen screen() const { return screen_; }
    NetSession* session() { return session_.get(); }
    // Back from the garage, with the car chosen, or from the game.
    void carChosen(const std::string& car, int paintJob);
    void showLobby();
    void leave(); // out of the session
    // What's been said and what's happened (joined, left), newest last,
    // for the game's screen.
    struct Line {
        std::string text;
        uint64_t atMs = 0;
        bool system = false; // the game's words, not a player's
    };
    const std::deque<Line>& lines() const { return lines_; }
    // A notice to show, from the game's strings where it has one.
    std::string noticeText(const NetNotice& n) const;
    // The race of the session's settings: its kind (Cruise and Cops &
    // Robbers have none).
    static bool hasRace(NetMode mode) {
        return mode == NetMode::Blitz || mode == NetMode::Checkpoint || mode == NetMode::Circuit;
    }
    static RaceKind raceKind(NetMode mode);
    std::string modeName(NetMode mode) const;
    std::string goldModeName(GoldMode mode) const;
    std::string teamName(uint8_t team) const; // in the session's mode
    // The car a player drives: its own, but the police car for a cop.
    std::string carFor(const NetPlayer& p) const;

    // Typing goes to the focused field; the Race page lets Esc through.
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
    const Widget* widget(const std::string& name) const;
    const Widget* widget(const std::string& name, const std::string& menu) const;
    std::string menuName() const;
    int originX() const; // dialogs' widgets are from their corner
    int originY() const;
    void go(Screen s);
    void activate(const Item& item, Action& action);
    void change(const Item& item, int step);
    std::string* textOf(const std::string& widget);
    bool hit(const Item& item, int x, int y) const;
    void text(Surface& o, const std::string& widget, const std::string& s, Color c = {255, 230, 140}) const;
    void say(const std::string& s, bool system);
    void startHosting();
    void joinAddress(const net::Address& address);
    void refreshRaces();
    void message(const std::string& text, const std::string& picture = "msg_dlg");
    void drawDialog(Surface& o, const std::string& picture, const std::string& title) const;
    void drawLobby(Surface& o) const;
    void drawHostRace(Surface& o) const;

    System& sys_;
    const SearchSet& files_;
    const std::vector<CityInfo>& cities_;
    const std::vector<CarInfo>& cars_;
    const GameStrings& strings_;
    mutable MenuPictures pictures_;
    std::vector<Widget> widgets_;
    bool ok_ = false;

    Screen screen_ = Screen::Sessions;
    int focus_ = 0;
    std::unique_ptr<NetSession> session_;
    std::unique_ptr<NetBrowser> browser_;
    size_t chosenSession_ = 0;
    std::string netName_ = "Player", address_, password_, chat_;
    NetSettings settings_; // the host's, as edited
    bool editing_ = false; // the host's settings from the lobby (else hosting anew)
    std::vector<std::string> raceNames_;
    size_t ejectChoice_ = 0;
    net::Address pendingJoin_; // a session that wants a password first
    uint32_t lastChatSeq_ = 0;
    std::string message_, messagePicture_; // a dialog over the screen, until a key
    std::deque<Line> lines_;
    uint64_t nowMs_ = 0;
    std::string myCar_ = "vpbug";
    int bots_ = 0;                                // computer players to host with
    std::shared_ptr<net::LoopbackNetwork> local_; // hosting with no network: this computer only
    static constexpr const char* kBotsField = "Computer Players";
    int myPaint_ = 0;
};

} // namespace reluva::age
