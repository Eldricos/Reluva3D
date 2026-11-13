// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The network screens, as dialogInfo.res lays them out: Connection Options
// (dialog 9), Host or Join (10), Host Game (11), Join Game (12), the
// players' picker with its chat (13), and the set-up of a suspect (18-23).
// The Internet Game Zone is gone: its button joins a game by its address
// instead, on the Host Game window relabelled (docs/engines/clue/systems/network.md).

#include "engines/clue/ui/frontend.h"

#include <algorithm>
#include <utility>

namespace reluva::clue {

namespace {

enum Dialog {
    kNetHostDialog = 11,
    kNetJoinDialog = 12,
    kNetLobbyDialog = 13,
    kSetupFirst = 18,
    kSessionLost = 27,
    kHostGone = 28,
};
constexpr int kReturn = 10000;
constexpr int kOk = 10001;
constexpr int kSeats = netplay::kSeats;
// Host Game: the controls by their place (the labels have no ids).
constexpr size_t kTitleAt = 1;
constexpr size_t kGameNameLabelAt = 4;
constexpr size_t kMaxLabelAt = 6;
const char *const kLevelName[3] = {"Novice CPU", "Normal CPU", "Expert CPU"};

} // namespace

std::unique_ptr<net::UdpSocket> FrontEnd::netSocket(uint16_t port, std::string &error) {
    std::unique_ptr<net::UdpSocket> socket;
    if (Error err = _sys.openUdp(port, socket)) {
        error = err.message;
        return nullptr;
    }
    return socket;
}

void FrontEnd::netShow(Screen s) {
    switch (s) {
    case Screen::kNetHost:
    case Screen::kNetAddress:
        showDialog(kNetHostDialog);
        if (s == Screen::kNetHost) {
            _dialog->setText(0, _netName + "'s game");
            for (int n = 2; n <= kSeats; ++n) {
                _dialog->setVisible(3 + 2 * (n - 2), n == _netMaxHumans);
                _dialog->setVisible(4 + 2 * (n - 2), n != _netMaxHumans);
            }
        } else {
            // Joining by address, in place of the Game Zone.
            _dialog->setTextAt(kTitleAt, "Join Game");
            _dialog->setTextAt(kGameNameLabelAt, "Please Enter the Host's Address");
            for (size_t i = kMaxLabelAt; i < kMaxLabelAt + 6; ++i)
                _dialog->setVisibleAt(i, false);
            for (int id = 3; id <= 12; ++id)
                _dialog->setVisible(id, false);
            // The row of boxes behind them is in the window's picture:
            // covered as it's drawn (FrontEnd::update).
            _dialog->setText(0, _netMessage.empty() ? std::string() : _netMessage);
            _netMessage.clear();
        }
        _dialog->setText(1, _netName);
        _dialog->focus(0);
        _sys.setTextInput(true);
        break;
    case Screen::kNetJoin: {
        showDialog(kNetJoinDialog);
        _dialog->setText(6, _netName);
        _dialog->focus(6);
        _sys.setTextInput(true);
        // Asking at each network the machine is on.
        std::vector<net::Address> askAt{net::Address::broadcast(netplay::kDefaultPort)};
        for (uint32_t ip : _sys.broadcastAddresses())
            askAt.push_back({ip, netplay::kDefaultPort});
        std::string error;
        auto socket = netSocket(0, error);
        _net = socket ? netplay::Session::browse(std::move(socket), askAt) : nullptr;
        for (int slot = 1; slot <= 5; ++slot) {
            _dialog->setText(slot, slot == 1 && !_net ? error : "");
            _dialog->setEnabled(slot, false);
        }
        break;
    }
    case Screen::kNetLobby:
        showDialog(kNetLobbyDialog);
        _dialog->focus(64);
        _sys.setTextInput(true);
        refreshLobby();
        break;
    case Screen::kNetSetup:
        showDialog(kSetupFirst + _netSeat);
        refreshNetSetup();
        break;
    case Screen::kNetMessage:
        _sys.setTextInput(false);
        showDialog(_netMessage == "host" ? kHostGone : kSessionLost);
        if (_netMessage != "host")
            _dialog->setTextAt(1, _netMessage);
        break;
    default:
        break;
    }
}

void FrontEnd::refreshLobby() {
    if (!_dialog || !_net)
        return;
    const netplay::Lobby &l = _net->lobby();
    for (int k = 0; k < kSeats; ++k) {
        const netplay::Seat &seat = l.seats[size_t(k)];
        const bool taken = seat.kind != netplay::Seat::kNone;
        _dialog->setVisible(5 + k, !taken);
        _dialog->setVisible(11 + k, taken);
        _dialog->setVisible(23 + k, taken);
        if (taken)
            _dialog->setText(23 + k, seat.kind == netplay::Seat::kHuman ? "is|" + seat.name
                                                                        : std::string("is|") + kLevelName[seat.level]);
    }
    const bool host = _net->isHost();
    _dialog->setVisible(65, !host);
    _dialog->setVisible(66, host && !_net->canStart());
    _dialog->setVisible(67, host && _net->canStart());
    _dialog->setEnabled(29, host && _net->canStart());
    // The chat's last three lines.
    for (int k = 0; k < 3; ++k) {
        const int from = int(_netChat.size()) - 3 + k;
        _dialog->setText(61 + k, from >= 0 ? _netChat[size_t(from)] : std::string());
    }
}

void FrontEnd::refreshNetSetup() {
    const netplay::Seat &d = _netDraft;
    _dialog->setChosen(0, d.kind == netplay::Seat::kHuman);
    _dialog->setChosen(1, d.kind == netplay::Seat::kComputer);
    _dialog->setChosen(2, d.kind == netplay::Seat::kNone);
    for (int id : {3, 4, 5, 6, 7})
        _dialog->setVisible(id, d.kind == netplay::Seat::kComputer);
    for (int level = 0; level < 3; ++level)
        _dialog->setChosen(5 + level, d.level == level);
    // A human here is the host's own: no name to type.
    for (int id : {8, 9, 10})
        _dialog->setVisible(id, false);
    _dialog->setEnabled(12, _net && _net->canStart());
}

void FrontEnd::netClick(int id) {
    switch (_at) {
    case Screen::kConnection:
        _netInternet = id == 0;
        go(Screen::kNetGame);
        break;
    case Screen::kNetGame:
        if (id == 1)
            go(Screen::kNetHost);
        else if (id == 2)
            go(_netInternet ? Screen::kNetAddress : Screen::kNetJoin);
        else if (id == 0)
            go(Screen::kMainMenu);
        else if (id == kReturn)
            go(Screen::kConnection);
        break;
    case Screen::kNetHost:
        if (id >= 3 && id <= 12) {
            _netMaxHumans = 2 + (id - 3) / 2;
            for (int n = 2; n <= kSeats; ++n) {
                _dialog->setVisible(3 + 2 * (n - 2), n == _netMaxHumans);
                _dialog->setVisible(4 + 2 * (n - 2), n != _netMaxHumans);
            }
        } else if (id == 2) {
            if (!_dialog->text(1).empty())
                _netName = _dialog->text(1);
            std::string error;
            auto socket = netSocket(netplay::kDefaultPort, error);
            if (!socket) {
                _dialog->setTextAt(kGameNameLabelAt, "Can't host: " + error);
                break;
            }
            _net = netplay::Session::host(std::move(socket), _dialog->text(0), _netName, _netMaxHumans);
            _netChat = {"Hosting on port " + std::to_string(netplay::kDefaultPort) + "."};
            _net->take(-1);
            go(Screen::kNetLobby);
        } else if (id == kReturn)
            go(Screen::kNetGame);
        break;
    case Screen::kNetAddress:
        if (id == 2) {
            if (!_dialog->text(1).empty())
                _netName = _dialog->text(1);
            // "address" or "address:port", by number or by name.
            const auto [hostName, port] = net::splitHostPort(_dialog->text(0), netplay::kDefaultPort);
            net::Address address;
            uint32_t ip = 0;
            if (!net::parseAddress(_dialog->text(0), netplay::kDefaultPort, address)) {
                if (hostName.empty() || !_sys.resolveHost(hostName, ip)) {
                    _dialog->setTextAt(kGameNameLabelAt, "No such address. Please Enter the Host's Address");
                    break;
                }
                address = {ip, port};
            }
            std::string error;
            auto socket = netSocket(0, error);
            if (!socket) {
                _dialog->setTextAt(kGameNameLabelAt, "Can't connect: " + error);
                break;
            }
            _net = netplay::Session::join(std::move(socket), address, _netName);
            _netChat = {"Connecting to " + address.toString() + " ..."};
            go(Screen::kNetLobby);
        } else if (id == kReturn)
            go(Screen::kNetGame);
        break;
    case Screen::kNetJoin:
        if (id == 0 && _net)
            _net->refresh();
        else if (id >= 1 && id <= 5 && _net && size_t(id - 1) < _net->found().size()) {
            if (!_dialog->text(6).empty())
                _netName = _dialog->text(6);
            const net::Address address = _net->found()[size_t(id - 1)].address;
            std::string error;
            auto socket = netSocket(0, error);
            if (!socket)
                break;
            _net = netplay::Session::join(std::move(socket), address, _netName);
            _netChat = {"Connecting to " + address.toString() + " ..."};
            go(Screen::kNetLobby);
        } else if (id == kReturn) {
            _net.reset();
            go(Screen::kNetGame);
        }
        break;
    case Screen::kNetLobby:
        if (!_net)
            break;
        if (id >= 5 && id <= 16) {
            const int seat = (id - 5) % kSeats;
            const netplay::Seat &s = _net->lobby().seats[size_t(seat)];
            if (_net->isHost() && !(s.kind == netplay::Seat::kHuman && s.owner != 0)) {
                // The host sets up any suspect not another player's.
                _netSeat = seat;
                _netDraft = s;
                go(Screen::kNetSetup);
            } else if (s.kind == netplay::Seat::kHuman && s.owner == _net->me())
                _net->take(-1); // given back
            else if (s.kind != netplay::Seat::kHuman)
                _net->take(seat);
        } else if (id == 29 && _net->canStart())
            _net->start(uint32_t(_sys.getMillis() * 2654435761u));
        else if (id == 60) {
            _net->chat(_dialog->text(64));
            _dialog->setText(64, "");
        } else if (id == 30)
            netLeave();
        break;
    case Screen::kNetSetup:
        if (id == 0)
            _netDraft.kind = netplay::Seat::kHuman;
        else if (id == 1)
            _netDraft.kind = netplay::Seat::kComputer;
        else if (id == 2)
            _netDraft.kind = netplay::Seat::kNone;
        else if (id >= 5 && id <= 7)
            _netDraft.level = uint8_t(id - 5);
        else if (id == 11 && _net) {
            // Options: the variant.
            _net->setNoDieRoll(!_net->lobby().noDieRoll);
            _netChat.push_back(_net->lobby().noDieRoll ? "Variant: No Die Roll." : "Variant: Classic.");
        }
        if (id <= 7) {
            refreshNetSetup();
            break;
        }
        if ((id == kOk || id == 12) && _net)
            _net->setSeat(_netSeat, _netDraft.kind, _netDraft.level);
        if (id == 12 && _net && _net->canStart())
            _net->start(uint32_t(_sys.getMillis() * 2654435761u));
        else if (id == kOk || id == kReturn)
            go(Screen::kNetLobby);
        break;
    case Screen::kNetMessage:
        if (id == kOk)
            go(Screen::kMainMenu);
        break;
    default:
        break;
    }
}

void FrontEnd::netLeave() {
    if (_net)
        _net->leave();
    _net.reset();
    _netChat.clear();
    _sys.setTextInput(false);
    go(Screen::kMainMenu);
}

void FrontEnd::netLost(std::string message) {
    // (by value: it may be the session's own, which goes now)
    if (_play)
        _play->stopSounds();
    _play.reset();
    _net.reset();
    _netMessage = std::move(message);
    go(Screen::kNetMessage);
}

void FrontEnd::netUpdate() {
    if (!_net)
        return;
    _net->update(_now);
    std::string line;
    while (_net->nextChat(line))
        _netChat.push_back(line);
    using State = netplay::Session::State;
    switch (_net->state()) {
    case State::kBrowsing:
        if (_at == Screen::kNetJoin && _dialog) {
            const auto &found = _net->found();
            for (int slot = 1; slot <= 5; ++slot) {
                const size_t i = size_t(slot - 1);
                if (i < found.size())
                    _dialog->setText(slot, found[i].gameName + " (" + std::to_string(found[i].humans) + "/" +
                                               std::to_string(found[i].maxHumans) + ")" +
                                               (found[i].started ? " playing" : ""));
                _dialog->setEnabled(slot, i < found.size() && !found[i].started);
            }
        }
        break;
    case State::kRefused:
        netLost(_net->refusal());
        break;
    case State::kLost:
        netLost(_net->isHost() ? "The Game Session|Has Been Lost" : "host");
        break;
    case State::kConnecting:
    case State::kLobby:
        if (_at == Screen::kNetLobby)
            refreshLobby();
        break;
    case State::kPlaying:
        // The game begun: in, as a game at one machine is.
        if (_at == Screen::kNetLobby || _at == Screen::kNetSetup) {
            _sys.setTextInput(false);
            _play.reset();
            go(Screen::kLoading);
        }
        break;
    }
}

} // namespace reluva::clue
