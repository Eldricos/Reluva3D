// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/net.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/netplay.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/static_text.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/** A mission a network game can be played on, as the net lobby lists it. */
struct NetMission {
    int number = 0;
    std::string name;
    std::string description; // its entry of its Net Lobby Script
};

/** The missions marked Available in Net Game, in order, with their descriptions. */
std::vector<NetMission> loadNetMissions(const Archive &data);

/** How the network screens reach the network. */
struct NetAccess {
    /** A UDP socket on a port (0: any); nullptr when there's no network. */
    std::function<std::unique_ptr<net::UdpSocket>(uint16_t port)> open;
    /** Where games are looked for: the local network's broadcasts, at netplay::kDefaultPort. */
    std::vector<net::Address> askAt;
};

/**
 * Rules.txt's Choose Netgame (`CStarTrekChooseNetGameMenu`): the player's
 * name, the games found on the local network, and CREATE GAME, JOIN GAME,
 * REFRESH LIST and CANCEL, in a window in the middle of the screen
 * (docs/engines/stawayteam/systems/network.md#choose-netgame).
 */
class ChooseNetGameScreen {
public:
    enum class Choice { kNone, kCancel, kLobby };

    /**
     * The window on a screen of that size, the name typed before in it;
     * `data` must outlive it.
     */
    ChooseNetGameScreen(const Archive &data, const TextObject &rules, NetAccess network, std::string name, int screenW,
                        int screenH);

    Choice handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now);
    /** Looks for games and joins; kLobby once a game's made or joined. */
    Choice update(uint64_t now);
    void draw(Surface &screen, uint64_t now) const;

    /** The game made or joined, once kLobby's given. */
    std::unique_ptr<netplay::Session> takeSession() { return std::move(_session); }
    /** The name typed, kept for the next time (the program's Net Player Name, 0x511e91). */
    const std::string &name() const { return _name; }
    /** What it has to say: no network, or the game couldn't be joined; empty for nothing. */
    const std::string &message() const { return _message; }
    /** The games listed. */
    const std::vector<netplay::Found> &games() const;

private:
    void refresh(uint64_t now);
    void enableButtons();
    int rowAt(int x, int y) const;

    const TextObject *_def;
    NetAccess _network;
    image::Image _background;
    std::vector<StaticText> _texts;
    Font _font;
    Font _chosenFont; // the game chosen's
    std::vector<Font> _buttonFonts;
    ButtonGroup _buttons;
    int _x = 0; // its top left
    int _y = 0;
    std::string _name;
    std::string _message;
    std::unique_ptr<netplay::Session> _browser;
    std::unique_ptr<netplay::Session> _session; // being joined, or made
    int _selected = -1;
    uint64_t _nextRefresh = 0;
};

/**
 * Rules.txt's Net Lobby (`CStarTrekNetLobbyMenu`): the players, the chat
 * and what's typed for it, the missions and the one chosen's description,
 * the start's countdown, and START GAME and EXIT TO MAIN MENU
 * (docs/engines/stawayteam/systems/network.md#net-lobby).
 */
class NetLobbyScreen {
public:
    enum class Choice { kNone, kExit, kStart };

    /** The lobby of `session`, offering `missions`; `data` must outlive it. */
    NetLobbyScreen(const Archive &data, const TextObject &rules, std::vector<NetMission> missions,
                   std::unique_ptr<netplay::Session> session);

    Choice handleEvent(const Event &ev, audio::Mixer *mixer);
    /** The session's news; kStart once, as the countdown runs out, kExit if the game's gone. */
    Choice update(uint64_t now);
    void draw(Surface &screen, uint64_t now) const;

    /** A line in the chat of the screen's own, no one's. */
    void notice(std::string text) { _lines.push_back(std::move(text)); }
    /** The chat's lines, as shown. */
    const std::vector<std::string> &lines() const { return _lines; }
    netplay::Session &session() { return *_session; }

private:
    void enableButtons();
    void send();
    int missionAt(int x, int y) const;

    const TextObject *_def;
    std::vector<NetMission> _missions;
    std::unique_ptr<netplay::Session> _session;
    image::Image _background;
    image::Image _chatWindow;
    image::Image _chatInput;
    image::Image _playerList;
    image::Image _missionList;
    image::Image _missionDescription;
    std::vector<StaticText> _texts;
    Font _font;
    Font _chosenFont;
    std::vector<Font> _buttonFonts;
    ButtonGroup _buttons;
    std::vector<std::string> _lines;
    std::string _typing;
    bool _startGiven = false;
};

} // namespace reluva::stawayteam
