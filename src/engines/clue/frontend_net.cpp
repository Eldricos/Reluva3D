// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The network screens, as dialogInfo.res lays them out: Connection Options
// (dialog 9), Host or Join (10), Host Game (11), Join Game (12), the
// players' picker with its chat (13), and the set-up of a suspect (18-23).
// The Internet Game Zone is gone: its button joins a game by its address
// instead, on the Host Game window relabelled (docs/engines/clue.md).

#include "engines/clue/frontend.h"

#include <algorithm>

namespace reluva::clue {

namespace {

enum Dialog {
    kNetHostDialog = 11, kNetJoinDialog = 12, kNetLobbyDialog = 13, kSetupFirst = 18, kSessionLost = 27,
    kHostGone = 28,
};
constexpr int kReturn = 10000, kOk = 10001;
constexpr int kSeats = netplay::kSeats;
// Host Game: the controls by their place (the labels have no ids).
constexpr size_t kTitleAt = 1, kGameNameLabelAt = 4, kMaxLabelAt = 6;
const char* const kLevelName[3] = {"Novice CPU", "Normal CPU", "Expert CPU"};

} // namespace

std::unique_ptr<net::UdpSocket> FrontEnd::netSocket(uint16_t port, std::string& error) {
    std::unique_ptr<net::UdpSocket> socket;
    if (Error err = sys_.openUdp(port, socket)) {
        error = err.message;
        return nullptr;
    }
    return socket;
}

void FrontEnd::netShow(Screen s) {
    switch (s) {
    case Screen::NetHost:
    case Screen::NetAddress:
        showDialog(kNetHostDialog);
        if (s == Screen::NetHost) {
            dialog_->setText(0, netName_ + "'s game");
            for (int n = 2; n <= kSeats; ++n) {
                dialog_->setVisible(3 + 2 * (n - 2), n == netMaxHumans_);
                dialog_->setVisible(4 + 2 * (n - 2), n != netMaxHumans_);
            }
        } else {
            // Joining by address, in place of the Game Zone.
            dialog_->setTextAt(kTitleAt, "Join Game");
            dialog_->setTextAt(kGameNameLabelAt, "Please Enter the Host's Address");
            for (size_t i = kMaxLabelAt; i < kMaxLabelAt + 6; ++i)
                dialog_->setVisibleAt(i, false);
            for (int id = 3; id <= 12; ++id)
                dialog_->setVisible(id, false);
            dialog_->setText(0, netMessage_.empty() ? std::string() : netMessage_);
            netMessage_.clear();
        }
        dialog_->setText(1, netName_);
        dialog_->focus(0);
        sys_.setTextInput(true);
        break;
    case Screen::NetJoin: {
        showDialog(kNetJoinDialog);
        dialog_->setText(6, netName_);
        dialog_->focus(6);
        sys_.setTextInput(true);
        // Asking at each network the machine is on.
        std::vector<net::Address> askAt{net::Address::broadcast(netplay::kDefaultPort)};
        for (uint32_t ip : sys_.broadcastAddresses())
            askAt.push_back({ip, netplay::kDefaultPort});
        std::string error;
        auto socket = netSocket(0, error);
        net_ = socket ? netplay::Session::browse(std::move(socket), askAt) : nullptr;
        for (int slot = 1; slot <= 5; ++slot) {
            dialog_->setText(slot, slot == 1 && !net_ ? error : "");
            dialog_->setEnabled(slot, false);
        }
        break;
    }
    case Screen::NetLobby:
        showDialog(kNetLobbyDialog);
        dialog_->focus(64);
        sys_.setTextInput(true);
        refreshLobby();
        break;
    case Screen::NetSetup:
        showDialog(kSetupFirst + netSeat_);
        refreshNetSetup();
        break;
    case Screen::NetMessage:
        sys_.setTextInput(false);
        showDialog(netMessage_ == "host" ? kHostGone : kSessionLost);
        if (netMessage_ != "host")
            dialog_->setTextAt(1, netMessage_);
        break;
    default: break;
    }
}

void FrontEnd::refreshLobby() {
    if (!dialog_ || !net_)
        return;
    const netplay::Lobby& l = net_->lobby();
    for (int k = 0; k < kSeats; ++k) {
        const netplay::Seat& seat = l.seats[size_t(k)];
        const bool taken = seat.kind != netplay::Seat::None;
        dialog_->setVisible(5 + k, !taken);
        dialog_->setVisible(11 + k, taken);
        dialog_->setVisible(23 + k, taken);
        if (taken)
            dialog_->setText(23 + k, seat.kind == netplay::Seat::Human ? "is|" + seat.name
                                                                       : std::string("is|") + kLevelName[seat.level]);
    }
    const bool host = net_->isHost();
    dialog_->setVisible(65, !host);
    dialog_->setVisible(66, host && !net_->canStart());
    dialog_->setVisible(67, host && net_->canStart());
    dialog_->setEnabled(29, host && net_->canStart());
    // The chat's last three lines.
    for (int k = 0; k < 3; ++k) {
        const int from = int(netChat_.size()) - 3 + k;
        dialog_->setText(61 + k, from >= 0 ? netChat_[size_t(from)] : std::string());
    }
}

void FrontEnd::refreshNetSetup() {
    const netplay::Seat& d = netDraft_;
    dialog_->setChosen(0, d.kind == netplay::Seat::Human);
    dialog_->setChosen(1, d.kind == netplay::Seat::Computer);
    dialog_->setChosen(2, d.kind == netplay::Seat::None);
    for (int id : {3, 4, 5, 6, 7})
        dialog_->setVisible(id, d.kind == netplay::Seat::Computer);
    for (int level = 0; level < 3; ++level)
        dialog_->setChosen(5 + level, d.level == level);
    // A human here is the host's own: no name to type.
    for (int id : {8, 9, 10})
        dialog_->setVisible(id, false);
    dialog_->setEnabled(12, net_ && net_->canStart());
}

void FrontEnd::netClick(int id) {
    switch (at_) {
    case Screen::Connection:
        netInternet_ = id == 0;
        go(Screen::NetGame);
        break;
    case Screen::NetGame:
        if (id == 1)
            go(Screen::NetHost);
        else if (id == 2)
            go(netInternet_ ? Screen::NetAddress : Screen::NetJoin);
        else if (id == 0)
            go(Screen::MainMenu);
        else if (id == kReturn)
            go(Screen::Connection);
        break;
    case Screen::NetHost:
        if (id >= 3 && id <= 12) {
            netMaxHumans_ = 2 + (id - 3) / 2;
            for (int n = 2; n <= kSeats; ++n) {
                dialog_->setVisible(3 + 2 * (n - 2), n == netMaxHumans_);
                dialog_->setVisible(4 + 2 * (n - 2), n != netMaxHumans_);
            }
        } else if (id == 2) {
            if (!dialog_->text(1).empty())
                netName_ = dialog_->text(1);
            std::string error;
            auto socket = netSocket(netplay::kDefaultPort, error);
            if (!socket) {
                dialog_->setTextAt(kGameNameLabelAt, "Can't host: " + error);
                break;
            }
            net_ = netplay::Session::host(std::move(socket), dialog_->text(0), netName_, netMaxHumans_);
            netChat_ = {"Hosting on port " + std::to_string(netplay::kDefaultPort) + "."};
            net_->take(-1);
            go(Screen::NetLobby);
        } else if (id == kReturn)
            go(Screen::NetGame);
        break;
    case Screen::NetAddress:
        if (id == 2) {
            if (!dialog_->text(1).empty())
                netName_ = dialog_->text(1);
            // "address" or "address:port", by number or by name.
            const auto [hostName, port] = net::splitHostPort(dialog_->text(0), netplay::kDefaultPort);
            net::Address address;
            uint32_t ip = 0;
            if (!net::parseAddress(dialog_->text(0), netplay::kDefaultPort, address)) {
                if (hostName.empty() || !sys_.resolveHost(hostName, ip)) {
                    dialog_->setTextAt(kGameNameLabelAt, "No such address. Please Enter the Host's Address");
                    break;
                }
                address = {ip, port};
            }
            std::string error;
            auto socket = netSocket(0, error);
            if (!socket) {
                dialog_->setTextAt(kGameNameLabelAt, "Can't connect: " + error);
                break;
            }
            net_ = netplay::Session::join(std::move(socket), address, netName_);
            netChat_ = {"Connecting to " + address.toString() + " ..."};
            go(Screen::NetLobby);
        } else if (id == kReturn)
            go(Screen::NetGame);
        break;
    case Screen::NetJoin:
        if (id == 0 && net_)
            net_->refresh();
        else if (id >= 1 && id <= 5 && net_ && size_t(id - 1) < net_->found().size()) {
            if (!dialog_->text(6).empty())
                netName_ = dialog_->text(6);
            const net::Address address = net_->found()[size_t(id - 1)].address;
            std::string error;
            auto socket = netSocket(0, error);
            if (!socket)
                break;
            net_ = netplay::Session::join(std::move(socket), address, netName_);
            netChat_ = {"Connecting to " + address.toString() + " ..."};
            go(Screen::NetLobby);
        } else if (id == kReturn) {
            net_.reset();
            go(Screen::NetGame);
        }
        break;
    case Screen::NetLobby:
        if (!net_)
            break;
        if (id >= 5 && id <= 16) {
            const int seat = (id - 5) % kSeats;
            const netplay::Seat& s = net_->lobby().seats[size_t(seat)];
            if (net_->isHost() && !(s.kind == netplay::Seat::Human && s.owner != 0)) {
                // The host sets up any suspect not another player's.
                netSeat_ = seat;
                netDraft_ = s;
                go(Screen::NetSetup);
            } else if (s.kind == netplay::Seat::Human && s.owner == net_->me())
                net_->take(-1); // given back
            else if (s.kind != netplay::Seat::Human)
                net_->take(seat);
        } else if (id == 29 && net_->canStart())
            net_->start(uint32_t(sys_.getMillis() * 2654435761u));
        else if (id == 60) {
            net_->chat(dialog_->text(64));
            dialog_->setText(64, "");
        } else if (id == 30)
            netLeave();
        break;
    case Screen::NetSetup:
        if (id == 0)
            netDraft_.kind = netplay::Seat::Human;
        else if (id == 1)
            netDraft_.kind = netplay::Seat::Computer;
        else if (id == 2)
            netDraft_.kind = netplay::Seat::None;
        else if (id >= 5 && id <= 7)
            netDraft_.level = uint8_t(id - 5);
        else if (id == 11 && net_) {
            // Options: the variant.
            net_->setNoDieRoll(!net_->lobby().noDieRoll);
            netChat_.push_back(net_->lobby().noDieRoll ? "Variant: No Die Roll." : "Variant: Classic.");
        }
        if (id <= 7) {
            refreshNetSetup();
            break;
        }
        if ((id == kOk || id == 12) && net_)
            net_->setSeat(netSeat_, netDraft_.kind, netDraft_.level);
        if (id == 12 && net_ && net_->canStart())
            net_->start(uint32_t(sys_.getMillis() * 2654435761u));
        else if (id == kOk || id == kReturn)
            go(Screen::NetLobby);
        break;
    case Screen::NetMessage:
        if (id == kOk)
            go(Screen::MainMenu);
        break;
    default: break;
    }
}

void FrontEnd::netLeave() {
    if (net_)
        net_->leave();
    net_.reset();
    netChat_.clear();
    sys_.setTextInput(false);
    go(Screen::MainMenu);
}

void FrontEnd::netLost(std::string message) {
    // (by value: it may be the session's own, which goes now)
    if (play_)
        play_->stopSounds();
    play_.reset();
    net_.reset();
    netMessage_ = message;
    go(Screen::NetMessage);
}

void FrontEnd::netUpdate() {
    if (!net_)
        return;
    net_->update(now_);
    std::string line;
    while (net_->nextChat(line))
        netChat_.push_back(line);
    using State = netplay::Session::State;
    switch (net_->state()) {
    case State::Browsing:
        if (at_ == Screen::NetJoin && dialog_) {
            const auto& found = net_->found();
            for (int slot = 1; slot <= 5; ++slot) {
                const size_t i = size_t(slot - 1);
                if (i < found.size())
                    dialog_->setText(slot, found[i].gameName + " (" + std::to_string(found[i].humans) + "/" +
                                               std::to_string(found[i].maxHumans) + ")" +
                                               (found[i].started ? " playing" : ""));
                dialog_->setEnabled(slot, i < found.size() && !found[i].started);
            }
        }
        break;
    case State::Refused:
        netLost(net_->refusal());
        break;
    case State::Lost:
        netLost(net_->isHost() ? "The Game Session|Has Been Lost" : "host");
        break;
    case State::Connecting:
    case State::Lobby:
        if (at_ == Screen::NetLobby)
            refreshLobby();
        break;
    case State::Playing:
        // The game begun: in, as a game at one machine is.
        if (at_ == Screen::NetLobby || at_ == Screen::NetSetup) {
            sys_.setTextInput(false);
            play_.reset();
            go(Screen::Loading);
        }
        break;
    }
}

} // namespace reluva::clue
