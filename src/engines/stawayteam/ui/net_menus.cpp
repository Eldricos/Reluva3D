// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/net_menus.h"

#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/ui/layout.h"

#include <algorithm>
#include <cstdio>

namespace reluva::stawayteam {

namespace {

// The words the screens show (0x640e3c, 0x640e4c, 0x640e24, 0x640dfc,
// 0x640da4, 0x642368).
const char *const kAvailableGames = "Available Games";
const char *const kNameLabel = "Name: ";
const char *const kEnterName = "Please enter your name";
const char *const kNoNetwork = "No Network Detected";
const char *const kCannotJoin =
    "Cannot join selected game.  The selected game is full, already started, or has exited.";
const char *const kMissionStarting = "Mission starting in %d seconds.";
// The list of games looked for again every 20 seconds (0x51210b).
constexpr uint64_t kRefreshMs = 20000;

// Choose Netgame's buttons, in the order they're added.
enum ChooseButton { kCreate, kJoin, kRefresh, kCancel, kTcp, kIpx };
// The Net Lobby's.
enum LobbyButton { kStart, kExit, kSend };

void addButton(ButtonGroup &buttons, std::vector<Font> &fonts, const Archive &data, const TextObject *def,
               const std::string &folder, const std::string &key, const std::string &picture, int offsetX = 0,
               int offsetY = 0) {
    int x = 0;
    int y = 0;
    int textX = 0;
    int textY = 0;
    readPoint(def, key + " Pos", x, y);
    readPoint(def, key + " Text Pos", textX, textY);
    Button button(data, folder, picture, x + offsetX, y + offsetY);
    // The fonts are kept by the screen, in a vector made big enough first.
    Font &font = fonts[buttons.buttons().size()];
    (void)font.load(data, readText(def, key + " Font"));
    button.setLabel(readText(def, key + " Text"), &font, textX, textY);
    button.setSound(gameSound(data, readText(def, key + " Press Sound")));
    buttons.add(std::move(button));
}

struct Point {
    int x = 0;
    int y = 0;
};

Point point(const TextObject *def, const std::string &key) {
    Point p;
    readPoint(def, key, p.x, p.y);
    return p;
}

} // namespace

std::vector<NetMission> loadNetMissions(const Archive &data) {
    std::vector<NetMission> out;
    for (int number = 1; number <= 18; ++number) {
        Mission mission;
        if (loadMission(data, number, mission) || !mission.availableInNetGame)
            continue;
        NetMission m{number, mission.name, {}};
        std::vector<uint8_t> bytes;
        if (!mission.netLobbyScript.empty() && !data.read("Levels/" + mission.netLobbyScript + ".RDS", bytes)) {
            const RdsScript script = readRdsScript(std::string_view((const char *)bytes.data(), bytes.size()));
            if (const std::string *text = script.find(mission.netLobbyText))
                m.description = *text;
        }
        out.push_back(std::move(m));
    }
    return out;
}

ChooseNetGameScreen::ChooseNetGameScreen(const Archive &data, const TextObject &rules, NetAccess network,
                                         std::string name, int screenW, int screenH)
    : _def(menuDefinition(rules, "Choose Netgame")), _network(std::move(network)),
      _background(screenPicture(data, "Start Netgame Menu/Background")), _name(std::move(name)) {
    if (_name.size() > netplay::kMostName)
        _name.resize(netplay::kMostName);
    _x = (screenW - _background.w) / 2;
    _y = (screenH - _background.h) / 2;
    _texts = readStaticTexts(data, _def);
    // Its Menu Font, for the labels, the list and the name.
    (void)_font.load(data, "Trek Small");
    (void)_chosenFont.load(data, "Small Blue");
    _buttonFonts.resize(6);
    const std::string folder = "Start Netgame Menu";
    addButton(_buttons, _buttonFonts, data, _def, folder, "CreateGameButton", "Create");
    addButton(_buttons, _buttonFonts, data, _def, folder, "JoinGameButton", "Join");
    addButton(_buttons, _buttonFonts, data, _def, folder, "RefreshButton", "Refresh");
    addButton(_buttons, _buttonFonts, data, _def, folder, "CancelButton", "Cancel");
    addButton(_buttons, _buttonFonts, data, _def, folder, "TCPButton", "TCP");
    addButton(_buttons, _buttonFonts, data, _def, folder, "IPXButton", "IPX");
    _buttons.setOrigin(_x, _y);
    // Reluva's games go over IP alone: TCP/IP's the one chosen, IPX can't be.
    _buttons.button(kTcp).setChecked(true);
    _buttons.button(kIpx).setEnabled(false);
    std::unique_ptr<net::UdpSocket> socket = _network.open ? _network.open(0) : nullptr;
    if (socket)
        _browser = netplay::Session::browse(std::move(socket), _network.askAt);
    else
        _message = kNoNetwork;
    enableButtons();
}

const std::vector<netplay::Found> &ChooseNetGameScreen::games() const {
    static const std::vector<netplay::Found> kNone;
    return _browser ? _browser->found() : kNone;
}

void ChooseNetGameScreen::refresh(uint64_t now) {
    if (_browser)
        _browser->refresh();
    _selected = -1;
    _nextRefresh = now + kRefreshMs;
    enableButtons();
}

void ChooseNetGameScreen::enableButtons() {
    // Making a game wants a name; joining one, a name and a game chosen
    // (0x512240). Nothing while one's being joined.
    const bool network = _browser != nullptr && !_session;
    const bool named = !_name.empty();
    _buttons.button(kCreate).setEnabled(network && named);
    _buttons.button(kJoin).setEnabled(network && named && _selected >= 0 && games()[size_t(_selected)].open);
    _buttons.button(kRefresh).setEnabled(network);
}

int ChooseNetGameScreen::rowAt(int x, int y) const {
    const Point list = point(_def, "List Pos");
    const Point size = point(_def, "List Size");
    const int top = list.y + _font.height() + 2;
    const int rowH = std::max(1, _font.height() + 2);
    x -= _x;
    y -= _y;
    if (x < list.x || x >= list.x + size.x || y < top || y >= list.y + size.y)
        return -1;
    const int row = (y - top) / rowH;
    return row < int(games().size()) ? row : -1;
}

ChooseNetGameScreen::Choice ChooseNetGameScreen::handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now) {
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
        if (_session)
            _session->leave();
        return Choice::kCancel;
    }
    // The name's field has the keys: the buttons are clicked.
    if (ev.type == EventType::kTextInput) {
        if (_name.size() < netplay::kMostName)
            _name += ev.text;
        _name.resize(std::min(_name.size(), netplay::kMostName));
        enableButtons();
        return Choice::kNone;
    }
    if (ev.type == EventType::kKeyDown) {
        if (ev.key == key::kBackspace && !_name.empty())
            _name.pop_back();
        enableButtons();
        return Choice::kNone;
    }
    const int chosen = _buttons.handleEvent(ev, mixer);
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && chosen < 0) {
        const int row = rowAt(ev.mouseX, ev.mouseY);
        if (row >= 0) {
            _selected = row;
            _message.clear();
        }
        enableButtons();
    }
    if (chosen < 0 || !_buttons.buttons()[size_t(chosen)].enabled())
        return Choice::kNone;
    switch (chosen) {
    case kCancel:
        if (_session)
            _session->leave();
        return Choice::kCancel;
    case kRefresh:
        refresh(now);
        break;
    case kCreate: {
        std::unique_ptr<net::UdpSocket> socket = _network.open(netplay::kDefaultPort);
        if (!socket) {
            _message = kNoNetwork;
            break;
        }
        _session = netplay::Session::host(std::move(socket), _name);
        return Choice::kLobby;
    }
    case kJoin: {
        std::unique_ptr<net::UdpSocket> socket = _network.open(0);
        if (!socket) {
            _message = kNoNetwork;
            break;
        }
        _message.clear();
        _session = netplay::Session::join(std::move(socket), games()[size_t(_selected)].address, _name);
        enableButtons();
        break;
    }
    default:
        break;
    }
    return Choice::kNone;
}

ChooseNetGameScreen::Choice ChooseNetGameScreen::update(uint64_t now) {
    if (_browser) {
        if (_nextRefresh == 0)
            _nextRefresh = now + kRefreshMs;
        else if (now >= _nextRefresh)
            refresh(now);
        _browser->update(now);
        if (_selected >= int(games().size()))
            _selected = -1;
    }
    if (!_session)
        return Choice::kNone;
    _session->update(now);
    if (_session->state() == netplay::Session::State::kLobby)
        return Choice::kLobby;
    if (_session->state() != netplay::Session::State::kConnecting) {
        _session.reset();
        _message = kCannotJoin;
        enableButtons();
    }
    return Choice::kNone;
}

void ChooseNetGameScreen::draw(Surface &screen, uint64_t now) const {
    image::blit(screen, _background, _x, _y);
    for (const StaticText &t : _texts)
        t.draw(screen, _x, _y);
    _buttons.draw(screen);
    if (!_font.loaded())
        return;
    // Available Games over the list, which starts below it; Name: before
    // the field; the asking for a name centred on its place (0x512150).
    const Point list = point(_def, "List Pos");
    const Point size = point(_def, "List Size");
    _font.draw(screen, _x + list.x, _y + list.y, kAvailableGames);
    const int rowH = _font.height() + 2;
    const int top = list.y + rowH;
    const std::vector<netplay::Found> &found = games();
    for (size_t i = 0; i < found.size(); ++i) {
        const int y = top + int(i) * rowH;
        if (y + rowH > list.y + size.y)
            break;
        // The one chosen in the font the screens mark things with (inferred).
        const Font &font = int(i) == _selected && _chosenFont.loaded() ? _chosenFont : _font;
        font.draw(screen, _x + list.x, _y + y, found[i].gameName);
    }
    const Point name = point(_def, "Name Pos");
    _font.draw(screen, _x + name.x, _y + name.y, kNameLabel);
    const int fieldX = name.x + _font.width(kNameLabel) + 2;
    _font.draw(screen, _x + fieldX, _y + name.y, _name + ((now / 500) % 2 ? "_" : ""));
    const Point ask = point(_def, "EnterNameText Pos");
    const std::string &said = _message.empty() ? std::string(kEnterName) : _message;
    // A message too wide for the window broken over lines, each centred
    // on the same place (inferred).
    const int width = 2 * std::min(ask.x, _background.w - ask.x) - 8;
    _font.drawWrapped(screen, _x + ask.x, _y + ask.y, std::max(width, 0), said);
}

NetLobbyScreen::NetLobbyScreen(const Archive &data, const TextObject &rules, std::vector<NetMission> missions,
                               std::unique_ptr<netplay::Session> session)
    : _def(menuDefinition(rules, "Net Lobby")), _missions(std::move(missions)), _session(std::move(session)) {
    const std::string folder = "Net Lobby";
    _background = screenPicture(data, folder + "/Background");
    _chatWindow = screenPicture(data, folder + "/Chat Window Background");
    _chatInput = screenPicture(data, folder + "/Chat Input Background");
    _playerList = screenPicture(data, folder + "/Player List Background");
    _missionList = screenPicture(data, folder + "/Mission List Window");
    _missionDescription = screenPicture(data, folder + "/Mission Description Window");
    _texts = readStaticTexts(data, _def);
    // Its Menu Font for the lists and the chat (0x514f4a); the chosen
    // mission in Small Blue (inferred: the list's own marking isn't traced).
    (void)_font.load(data, "Trek Small");
    (void)_chosenFont.load(data, "Small Blue");
    _buttonFonts.resize(3);
    addButton(_buttons, _buttonFonts, data, _def, folder, "StartButton", "Start");
    addButton(_buttons, _buttonFonts, data, _def, folder, "ExitButton", "Exit");
    // SEND is placed on the chat's field.
    const Point input = point(_def, "ChatInput Pos");
    addButton(_buttons, _buttonFonts, data, _def, folder, "SendChatButton", "Send", input.x, input.y);
    enableButtons();
}

void NetLobbyScreen::enableButtons() {
    // Only the host has START (0x5155de), on once a mission's chosen and
    // not counting (0x5162b2).
    const bool canStart = _session->isHost() && _session->mission() != 0 && !_session->counting() &&
                          _session->state() == netplay::Session::State::kLobby;
    _buttons.button(kStart).setEnabled(canStart);
    _buttons.button(kSend).setEnabled(!_typing.empty());
}

void NetLobbyScreen::send() {
    if (_typing.empty())
        return;
    _session->chat(_typing);
    _typing.clear();
    enableButtons();
}

int NetLobbyScreen::missionAt(int x, int y) const {
    const Point list = point(_def, "MissionList Pos");
    const Point margin = point(_def, "ChatMargin Size");
    const int rowH = std::max(1, _font.height() + 2);
    const int top = list.y + margin.y;
    if (x < list.x || x >= list.x + _missionList.w || y < top)
        return -1;
    const int row = (y - top) / rowH;
    return row < int(_missions.size()) ? row : -1;
}

NetLobbyScreen::Choice NetLobbyScreen::handleEvent(const Event &ev, audio::Mixer *mixer) {
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
        _session->leave();
        return Choice::kExit;
    }
    // The chat's field has the keys; Enter sends what's typed.
    if (ev.type == EventType::kTextInput) {
        if (_typing.size() < netplay::kMostChat)
            _typing += ev.text;
        enableButtons();
        return Choice::kNone;
    }
    if (ev.type == EventType::kKeyDown) {
        if (ev.key == key::kReturn || ev.key == key::kKpEnter)
            send();
        else if (ev.key == key::kBackspace && !_typing.empty())
            _typing.pop_back();
        enableButtons();
        return Choice::kNone;
    }
    const int chosen = _buttons.handleEvent(ev, mixer);
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && chosen < 0 && _session->isHost()) {
        const int row = missionAt(ev.mouseX, ev.mouseY);
        if (row >= 0)
            _session->chooseMission(_missions[size_t(row)].number);
        enableButtons();
    }
    if (chosen < 0 || !_buttons.buttons()[size_t(chosen)].enabled())
        return Choice::kNone;
    if (chosen == kExit) {
        _session->leave();
        return Choice::kExit;
    }
    if (chosen == kSend)
        send();
    else if (chosen == kStart)
        _session->start();
    enableButtons();
    return Choice::kNone;
}

NetLobbyScreen::Choice NetLobbyScreen::update(uint64_t now) {
    _session->update(now);
    netplay::ChatLine line;
    while (_session->nextChat(line)) {
        const std::vector<std::string> &players = _session->players();
        const std::string who = line.from >= 0 && line.from < int(players.size()) ? players[size_t(line.from)] : "?";
        // The host puts who said it before what they said (0x5c1080).
        _lines.push_back(who + ": " + line.text);
    }
    enableButtons();
    const netplay::Session::State state = _session->state();
    if (state == netplay::Session::State::kLost || state == netplay::Session::State::kRefused)
        return Choice::kExit;
    if (state == netplay::Session::State::kStarted && !_startGiven) {
        _startGiven = true;
        return Choice::kStart;
    }
    return Choice::kNone;
}

void NetLobbyScreen::draw(Surface &screen, uint64_t now) const {
    image::blit(screen, _background, 0, 0);
    const Point chat = point(_def, "Chat Pos");
    const Point margin = point(_def, "ChatMargin Size");
    const Point players = point(_def, "PlayerList Pos");
    const Point playerName = point(_def, "PlayerName Pos");
    const Point missions = point(_def, "MissionList Pos");
    const Point description = point(_def, "MissionDesc Pos");
    const Point descriptionText = point(_def, "MissionDescText Pos");
    const Point input = point(_def, "ChatInput Pos");
    const Point field = point(_def, "ChatFieldInput Pos");
    const Point countdown = point(_def, "Countdown Pos");
    image::blit(screen, _chatWindow, chat.x, chat.y);
    image::blit(screen, _playerList, players.x, players.y);
    image::blit(screen, _missionList, missions.x, missions.y);
    image::blit(screen, _missionDescription, description.x, description.y);
    image::blit(screen, _chatInput, input.x, input.y);
    for (const StaticText &t : _texts)
        t.draw(screen, 0, 0);
    _buttons.draw(screen);
    if (!_font.loaded())
        return;
    const int rowH = _font.height() + 2;
    // The chat: its last lines that fit, broken to its width, inside its margin.
    const int chatW = _chatWindow.w - 2 * margin.x;
    const int chatRows = std::max(0, (_chatWindow.h - 2 * margin.y) / std::max(1, rowH));
    std::vector<std::string> shown;
    for (auto it = _lines.rbegin(); it != _lines.rend() && int(shown.size()) < chatRows; ++it) {
        const std::vector<std::string> broken = _font.wrap(*it, chatW);
        for (auto b = broken.rbegin(); b != broken.rend() && int(shown.size()) < chatRows; ++b)
            shown.push_back(*b);
    }
    for (size_t i = 0; i < shown.size(); ++i)
        _font.draw(screen, chat.x + margin.x, chat.y + margin.y + int(shown.size() - 1 - i) * rowH, shown[i]);
    const std::vector<std::string> &names = _session->players();
    for (size_t i = 0; i < names.size(); ++i)
        _font.draw(screen, players.x + playerName.x, players.y + playerName.y + int(i) * rowH, names[i]);
    // The missions, the one chosen in its own font, and its description.
    for (size_t i = 0; i < _missions.size(); ++i) {
        const int y = missions.y + margin.y + int(i) * rowH;
        if (y + rowH > missions.y + _missionList.h)
            break;
        const bool chosen = _missions[i].number == _session->mission();
        const Font &font = chosen && _chosenFont.loaded() ? _chosenFont : _font;
        font.draw(screen, missions.x + margin.x, y, _missions[i].name);
        if (chosen)
            _font.drawWrapped(screen, description.x + descriptionText.x, description.y + descriptionText.y,
                              _missionDescription.w - 2 * descriptionText.x, _missions[i].description,
                              Font::Align::kLeft);
    }
    // A line longer than the field shows its end, up to SEND (inferred).
    std::string typing = _typing + ((now / 500) % 2 ? "_" : "");
    const int fieldW = point(_def, "SendChatButton Pos").x - field.x;
    size_t from = 0;
    while (from < typing.size() && _font.width(typing.substr(from)) > fieldW)
        ++from;
    _font.draw(screen, input.x + field.x, input.y + field.y, typing.substr(from));
    if (_session->counting()) {
        char text[64];
        // Whole seconds, rounded up, as the program counts (0x5160a7).
        std::snprintf(text, sizeof text, kMissionStarting, int((_session->countdownLeft() + 999) / 1000));
        _font.draw(screen, countdown.x - _font.width(text) / 2, countdown.y, text);
    }
}

} // namespace reluva::stawayteam
