// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The multiplayer menus: tune/widget.csv's Net Select Menu, Enter an
// Address, Host Options, Host Race Menu, Network Arena and their dialogs,
// on the game's jpg/ backgrounds with its texture/ buttons. Which picture
// is which control, and what it does, is Reluva's reading of the
// pictures; see docs/engines/age-menus.md.

#include "engines/age/net_menus.h"

#include "common/font.h"
#include "engines/age/race.h"
#include "engines/age/viewers.h"

#include <algorithm>
#include <cstdio>

namespace reluva::age {

namespace {

// The dialogs' corner (tune/menu.csv: 120, 75, 400 x 330), and a message
// box's (120, 202, 400 x 76).
constexpr int kDialogX = 120, kDialogY = 75, kMessageY = 202;
// Boxes drawn on the backgrounds, not controls (measured on the jpg/
// pictures): the lobby's host settings, its "you" and its chat; the
// heading's box; the eject dialog's list.
struct Box {
    int x, y, w, h;
};
constexpr Box kSettingsBox{38, 66, 222, 114};
constexpr Box kYouBox{276, 214, 180, 46};
constexpr Box kChatBox{278, 306, 350, 62};
constexpr Box kHeadingBox{453, 10, 164, 24};
constexpr Box kEjectList{192, 164, 256, 160};
constexpr Box kCopsPanel{267, 45, 373, 213}; // where jpg/host_cr goes
constexpr int kRow = 13;                     // a list's line
constexpr size_t kLinesKept = 50;

const char* const kTimes[] = {"Morning", "Noon", "Evening", "Night"};
const char* const kWeathers[] = {"Clear", "Cloudy", "Fog", "Rain"};

bool inside(int x, int y, int bx, int by, int bw, int bh) { return x >= bx && x < bx + bw && y >= by && y < by + bh; }

void fillBox(Surface& o, const Box& b, Color c, uint8_t alpha = 255) {
    for (int y = std::max(0, b.y); y < std::min(o.h, b.y + b.h); ++y)
        for (int x = std::max(0, b.x); x < std::min(o.w, b.x + b.w); ++x) {
            uint8_t* p = o.row(y) + x * 4;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = alpha;
        }
}

std::string cityPicture(const std::string& map) { return map == "london" ? "london" : "sf"; }

} // namespace

NetMenus::NetMenus(System& sys, const SearchSet& files, const std::vector<CityInfo>& cities,
                   const std::vector<CarInfo>& cars, const GameStrings& strings)
    : sys_(sys), files_(files), cities_(cities), cars_(cars), strings_(strings), pictures_(files) {
    std::vector<uint8_t> data;
    if (!files.read("tune/widget.csv", data))
        ok_ = !parseWidgets(std::string(data.begin(), data.end()), widgets_) && pictures_.jpg("sess_bk") &&
              !cities.empty() && !cars.empty();
    // Reluva's own: how many computer players, below Max Players and
    // drawn as it is.
    if (const Widget* max = widget("Max Players", "Host Options")) {
        Widget bots = *max;
        bots.name = kBotsField;
        bots.y += 52;
        widgets_.push_back(bots);
    }
    if (!cars.empty())
        myCar_ = cars.front().base;
}

NetMenus::~NetMenus() { leave(); }

RaceKind NetMenus::raceKind(NetMode mode) {
    return mode == NetMode::Circuit      ? RaceKind::Circuit
           : mode == NetMode::Checkpoint ? RaceKind::Checkpoint
                                         : RaceKind::Blitz;
}

std::string NetMenus::modeName(NetMode mode) const {
    switch (mode) {
    case NetMode::Cruise:
        return strings_.get(80, "Cruise");
    case NetMode::Blitz:
        return strings_.get(7, "Blitz Race");
    case NetMode::Checkpoint:
        return strings_.get(6, "Checkpoint Race");
    case NetMode::Circuit:
        return strings_.get(5, "Circuit Race");
    case NetMode::CopsRobbers:
        return strings_.get(79, "Cops & Robbers");
    }
    return {};
}

std::string NetMenus::goldModeName(GoldMode mode) const {
    switch (mode) {
    case GoldMode::FreeForAll:
        return strings_.get(8, "Free-For-All");
    case GoldMode::CopsAndRobbers:
        return strings_.get(9, "Cops & Robbers");
    case GoldMode::RobberTeams:
        return strings_.get(10, "Robbers Teams");
    }
    return {};
}

std::string NetMenus::teamName(uint8_t team) const {
    // The game's words: COPS, ROBBERS, BLUE, RED.
    const NetSettings& s = session_ ? session_->settings() : settings_;
    if (s.gold == GoldMode::CopsAndRobbers)
        return team ? strings_.get(122, "COPS") : strings_.get(123, "ROBBERS");
    return team ? strings_.get(124, "BLUE") : strings_.get(125, "RED");
}

std::string NetMenus::carFor(const NetPlayer& p) const {
    const NetSettings& s = session_ ? session_->settings() : settings_;
    if (s.mode == NetMode::CopsRobbers && s.gold == GoldMode::CopsAndRobbers && p.team == 1)
        for (const CarInfo& c : cars_)
            if (c.base == "vpcop")
                return c.base;
    for (const CarInfo& c : cars_)
        if (c.base == p.car)
            return c.base;
    return cars_.empty() ? std::string() : cars_.front().base;
}

std::string NetMenus::noticeText(const NetNotice& n) const {
    switch (n.kind) {
    case NetNotice::Kind::Joined:
        return n.name + " " + strings_.get(42, "has joined");
    case NetNotice::Kind::Left:
        return n.name + " " + strings_.get(38, "has left the game");
    case NetNotice::Kind::HostQuit:
        return strings_.get(40, "The Host has quit");
    case NetNotice::Kind::Ejected:
        return strings_.get(44, "You have been ejected");
    case NetNotice::Kind::BackToLobby:
        return strings_.get(73, "**Session returning to Lobby**");
    case NetNotice::Kind::Started:
        return {};
    case NetNotice::Kind::Refused:
        switch (n.reason) {
        case kRefusedVersion:
            return strings_.get(83, "ERROR: Network versions do not match.");
        case kRefusedFull:
            return "The session is full";
        case kRefusedPlaying:
            return "The game has started: wait for the lobby";
        case kRefusedPassword:
            return "The password is wrong";
        default:
            return "The host didn't answer";
        }
    }
    return {};
}

void NetMenus::say(const std::string& s, bool system) {
    if (s.empty())
        return;
    lines_.push_back({s, nowMs_, system});
    while (lines_.size() > kLinesKept)
        lines_.pop_front();
}

void NetMenus::message(const std::string& text, const std::string& picture) {
    message_ = text;
    messagePicture_ = picture;
}

void NetMenus::enter() {
    leave();
    go(Screen::Sessions);
}

void NetMenus::leave() {
    if (session_)
        session_->leave();
    session_.reset();
    local_.reset();
    browser_.reset();
    sys_.setTextInput(false);
}

void NetMenus::showLobby() {
    if (session_)
        go(Screen::Lobby);
    else
        go(Screen::Sessions);
}

void NetMenus::carChosen(const std::string& car, int paintJob) {
    myCar_ = car;
    myPaint_ = paintJob;
    if (session_)
        session_->setCar(car, uint8_t(paintJob));
    showLobby();
}

std::string NetMenus::menuName() const {
    switch (screen_) {
    case Screen::Sessions:
        return "Net Select Menu";
    case Screen::Address:
        return "Enter an Address";
    case Screen::Password:
        return "Enter a valid password";
    case Screen::HostOptions:
        return "Host Options";
    case Screen::HostRace:
        return "Host Race Menu";
    case Screen::Lobby:
        return "Network Arena";
    case Screen::Eject:
        return "eject_DLG";
    }
    return {};
}

int NetMenus::originX() const {
    return screen_ == Screen::Address || screen_ == Screen::Password || screen_ == Screen::HostOptions ||
                   screen_ == Screen::Eject
               ? kDialogX
               : 0;
}

int NetMenus::originY() const { return originX() ? kDialogY : 0; }

const Widget* NetMenus::widget(const std::string& name) const { return widget(name, menuName()); }

const Widget* NetMenus::widget(const std::string& name, const std::string& menu) const {
    for (const Widget& w : widgets_)
        if (w.menu == menu && w.name == name)
            return &w;
    return nullptr;
}

std::vector<NetMenus::Item> NetMenus::items() const {
    auto button = [](const std::string& w, const std::string& pic, int states, bool enabled = true) {
        Item i;
        i.widget = w;
        i.picture = pic;
        i.button = true;
        i.states = states;
        i.enabled = enabled;
        return i;
    };
    auto field = [](const std::string& w, bool typed = false) {
        Item i;
        i.widget = w;
        i.text = typed;
        return i;
    };
    const Item cancel = button("dlg_cancel", "dlg_can", 4), done = button("dlg_done", "dlg_done", 4);
    switch (screen_) {
    case Screen::Sessions:
        // TCP/IP only: the zone, IPX, serial and modem are the past's.
        // (The zone's picture has five states, though widget.csv says four.)
        return {button("sess_zn", "sess_zn", 5, false),
                button("sess_ipx", "sess_ipx", 5, false),
                button("sess_tcp", "sess_tcp", 5),
                button("sess_ser", "sess_ser", 5, false),
                button("sess_mod", "sess_mod", 5, false),
                field("NET NAME", true),
                button("sess_hst", "sess_hst", 4),
                button("sess_jn", "sess_jn", 4),
                field("textscroll"),
                button("mnav_prev", "mnav_prv", 4)};
    case Screen::Address:
        return {field("TCP/IP Address", true), cancel, done};
    case Screen::Password:
        return {field("Password", true), cancel, done};
    case Screen::HostOptions:
        return {field("Password", true), field("Max Players"), field(kBotsField), cancel, done};
    case Screen::HostRace: {
        std::vector<Item> list = {button("cruise_m", "cruise_m", 5), button("blitz_m", "blitz_m", 5),
                                  button("cp_m", "cp_m", 5), button("circt_m", "circt_m", 5),
                                  button("cops_m", "cops_m", 5)};
        if (hasRace(settings_.mode))
            list.push_back(field("RACE NAME"));
        if (settings_.mode == NetMode::Circuit)
            list.push_back(field("LAPS"));
        if (settings_.mode == NetMode::CopsRobbers) {
            for (const char* b : {"free_cr", "cpsvr_cr", "robrs_cr", "none_cr", "time_cr", "point_cr"})
                list.push_back(button(b, b, 5));
            if (settings_.limit != GoldLimit::None)
                list.push_back(field("LIMIT VALUE"));
            list.push_back(field("GOLD MASS"));
        }
        for (const char* f : {"RACE LOCALE", "TIME", "WEATHER", "PEDESTRIAN DENSITY"})
            list.push_back(field(f));
        list.push_back(button("aren_strt", "host_dn", 4));
        return list;
    }
    case Screen::Lobby: {
        const bool host = session_ && session_->isHost();
        const NetPlayer* me = session_ ? session_->me() : nullptr;
        const NetSettings& s = session_ ? session_->settings() : settings_;
        std::vector<Item> list = {field("Chat Entry", true)};
        if (s.teams()) {
            const bool copr = s.gold == GoldMode::CopsAndRobbers;
            list.push_back(button("lobb_red", copr ? "lobb_rob" : "lobb_red", 5));
            list.push_back(button("lobb_blu", copr ? "lobb_cop" : "lobb_blu", 5));
        }
        list.push_back(button("lobb_ejt", "lobb_ejt", 4, host && session_->players().size() > 1));
        list.push_back(button("lobb_hst", "lobb_hst", 4, host));
        // A cop drives the police car: no choosing.
        const bool cop = me && s.mode == NetMode::CopsRobbers && s.gold == GoldMode::CopsAndRobbers && me->team == 1;
        list.push_back(button("mpst_veh", "lobb_veh", 4, !cop));
        if (host)
            list.push_back(button("lobb_rdy", "lobb_srt", 5, session_->everyoneReady()));
        else
            list.push_back(button("lobb_rdy", me && me->ready ? "lobb_rdy" : "lobb_nr", 5, me != nullptr));
        return list;
    }
    case Screen::Eject:
        return {field("ejectlist"), done};
    }
    return {};
}

bool NetMenus::typing() const {
    const auto list = items();
    return messagePicture_.empty() && focus_ >= 0 && size_t(focus_) < list.size() && list[size_t(focus_)].text;
}

void NetMenus::go(Screen s) {
    screen_ = s;
    focus_ = 0;
    const auto list = items();
    // The first control that's there to use; on the sessions screen, Host.
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].enabled && (s != Screen::Sessions || list[i].widget == "sess_hst")) {
            focus_ = int(i);
            break;
        }
    if (s == Screen::Sessions && !browser_) {
        std::unique_ptr<net::UdpSocket> socket;
        if (!sys_.openUdp(0, socket) && socket) {
            browser_ = std::make_unique<NetBrowser>(std::move(socket));
            browser_->setBroadcasts(sys_.broadcastAddresses());
        } else
            message("The network is not available");
    }
    if (s == Screen::HostRace)
        refreshRaces();
    sys_.setTextInput(typing());
}

void NetMenus::refreshRaces() {
    raceNames_.clear();
    if (cities_.empty())
        return;
    settings_.city = uint8_t(std::min<size_t>(settings_.city, cities_.size() - 1));
    if (hasRace(settings_.mode))
        raceNames_ = raceNames(files_, cities_[settings_.city].map, raceKind(settings_.mode));
    settings_.race = uint8_t(std::min<size_t>(settings_.race, raceNames_.empty() ? 0 : raceNames_.size() - 1));
}

void NetMenus::startHosting() {
    std::unique_ptr<net::UdpSocket> socket;
    if (Error err = sys_.openUdp(kNetPort, socket); err || !socket) {
        // No network (or the port's taken): a game on this computer alone,
        // against computer players.
        local_ = net::LoopbackNetwork::create();
        socket = local_->open(kNetPort);
        message("Playing on this computer only: " + (err ? err.message : std::string("no network")));
    }
    browser_.reset();
    session_ = NetSession::host(std::move(socket), netName_, settings_, password_);
    session_->setCar(myCar_, uint8_t(myPaint_));
    // The computer players: the game's names for opponents, in cars from
    // the list.
    for (int i = 0; i < bots_; ++i) {
        const std::string car = cars_[(size_t(i) * 5 + 3) % cars_.size()].base;
        session_->addBot(strings_.get(uint32_t(13 + i), "Opponent " + std::to_string(i + 1)), car);
    }
    lines_.clear();
    lastChatSeq_ = 0;
    go(Screen::Lobby);
}

void NetMenus::joinAddress(const net::Address& address) {
    std::unique_ptr<net::UdpSocket> socket;
    if (Error err = sys_.openUdp(0, socket); err || !socket) {
        message("The network is not available");
        return;
    }
    session_ = NetSession::join(std::move(socket), address, netName_, password_);
    session_->setCar(myCar_, uint8_t(myPaint_));
    lines_.clear();
    lastChatSeq_ = 0;
    go(Screen::Lobby);
}

std::string* NetMenus::textOf(const std::string& w) {
    if (w == "NET NAME")
        return &netName_;
    if (w == "TCP/IP Address")
        return &address_;
    if (w == "Password")
        return &password_;
    if (w == "Chat Entry")
        return &chat_;
    return nullptr;
}

void NetMenus::activate(const Item& item, Action& action) {
    if (!item.enabled)
        return;
    const std::string& w = item.widget;
    if (w == "sess_hst") {
        editing_ = false;
        password_.clear();
        go(Screen::HostOptions);
    } else if (w == "sess_jn") {
        const auto& found = browser_ ? browser_->sessions() : std::vector<NetBrowser::Found>{};
        if (chosenSession_ < found.size()) {
            const auto& f = found[chosenSession_];
            password_.clear();
            if (f.password) {
                pendingJoin_ = f.address;
                go(Screen::Password);
            } else
                joinAddress(f.address);
        } else
            go(Screen::Address);
    } else if (w == "mnav_prev") {
        leave();
        action = Action::Back;
    } else if (w == "dlg_cancel") {
        go(screen_ == Screen::Eject ? Screen::Lobby : Screen::Sessions);
    } else if (w == "dlg_done") {
        if (screen_ == Screen::Address) {
            const auto [host, port] = net::splitHostPort(address_, kNetPort);
            net::Address a;
            a.port = port;
            if (!net::parseAddress(address_, kNetPort, a) && !sys_.resolveHost(host, a.ip)) {
                message("Can't find " + host);
                return;
            }
            password_.clear();
            joinAddress(a);
        } else if (screen_ == Screen::Password) {
            joinAddress(pendingJoin_);
        } else if (screen_ == Screen::HostOptions) {
            settings_.maxPlayers = std::clamp<uint8_t>(settings_.maxPlayers, 2, kMaxNetPlayers);
            go(Screen::HostRace);
        } else if (screen_ == Screen::Eject && session_) {
            std::vector<uint8_t> ids;
            for (const NetPlayer& p : session_->players())
                if (p.id != session_->myId())
                    ids.push_back(p.id);
            if (ejectChoice_ < ids.size())
                session_->eject(ids[ejectChoice_]);
            go(Screen::Lobby);
        }
    } else if (w == "cruise_m" || w == "blitz_m" || w == "cp_m" || w == "circt_m" || w == "cops_m") {
        settings_.mode = w == "cruise_m"  ? NetMode::Cruise
                         : w == "blitz_m" ? NetMode::Blitz
                         : w == "cp_m"    ? NetMode::Checkpoint
                         : w == "circt_m" ? NetMode::Circuit
                                          : NetMode::CopsRobbers;
        refreshRaces();
    } else if (w == "free_cr" || w == "cpsvr_cr" || w == "robrs_cr") {
        settings_.gold = w == "free_cr"    ? GoldMode::FreeForAll
                         : w == "cpsvr_cr" ? GoldMode::CopsAndRobbers
                                           : GoldMode::RobberTeams;
    } else if (w == "none_cr" || w == "time_cr" || w == "point_cr") {
        settings_.limit = w == "none_cr" ? GoldLimit::None : w == "time_cr" ? GoldLimit::Time : GoldLimit::Points;
        settings_.limitValue = settings_.limit == GoldLimit::Time ? 10 : 5;
    } else if (w == "aren_strt") {
        if (editing_ && session_) {
            session_->setSettings(settings_);
            go(Screen::Lobby);
        } else
            startHosting();
    } else if (w == "lobb_red" || w == "lobb_blu") {
        if (session_)
            session_->setTeam(w == "lobb_blu" ? 1 : 0);
    } else if (w == "lobb_ejt") {
        ejectChoice_ = 0;
        go(Screen::Eject);
    } else if (w == "lobb_hst") {
        if (session_)
            settings_ = session_->settings();
        editing_ = true;
        go(Screen::HostRace);
    } else if (w == "mpst_veh") {
        action = Action::Garage;
    } else if (w == "lobb_rdy" && session_) {
        if (session_->isHost()) {
            if (Error err = session_->start(nowMs_))
                message(err.message);
        } else if (const NetPlayer* me = session_->me())
            session_->setReady(!me->ready);
    }
}

void NetMenus::change(const Item& item, int step) {
    auto wrap = [](int v, int n) { return n > 0 ? (v % n + n) % n : 0; };
    const std::string& w = item.widget;
    if (w == "textscroll") {
        const size_t n = browser_ ? browser_->sessions().size() : 0;
        chosenSession_ = size_t(wrap(int(chosenSession_) + step, int(n)));
    } else if (w == "Max Players") {
        settings_.maxPlayers = uint8_t(std::clamp(settings_.maxPlayers + step, 2, kMaxNetPlayers));
        bots_ = std::min(bots_, settings_.maxPlayers - 1);
    } else if (w == kBotsField) {
        bots_ = std::clamp(bots_ + step, 0, settings_.maxPlayers - 1);
    } else if (w == "RACE NAME") {
        settings_.race = uint8_t(wrap(settings_.race + step, int(raceNames_.size())));
    } else if (w == "LAPS") {
        settings_.laps = uint8_t(std::clamp(settings_.laps + step, 1, 20));
    } else if (w == "LIMIT VALUE") {
        const int most = settings_.limit == GoldLimit::Time ? 60 : 50;
        settings_.limitValue = uint16_t(std::clamp(settings_.limitValue + step, 1, most));
    } else if (w == "GOLD MASS") {
        settings_.goldMass = uint16_t(std::clamp(settings_.goldMass + step * 100, 0, 2000));
    } else if (w == "RACE LOCALE") {
        settings_.city = uint8_t(wrap(settings_.city + step, int(cities_.size())));
        refreshRaces();
    } else if (w == "TIME") {
        settings_.timeOfDay = uint8_t(wrap(settings_.timeOfDay + step, 4));
    } else if (w == "WEATHER") {
        settings_.weather = uint8_t(wrap(settings_.weather + step, 4));
    } else if (w == "PEDESTRIAN DENSITY") {
        settings_.peds = std::clamp(std::round(settings_.peds * 10 + float(step)) / 10, 0.0f, 1.0f);
    } else if (w == "ejectlist" && session_) {
        ejectChoice_ = size_t(wrap(int(ejectChoice_) + step, int(session_->players().size()) - 1));
    }
}

bool NetMenus::hit(const Item& item, int x, int y) const {
    if (item.widget == "ejectlist")
        return inside(x, y, kEjectList.x, kEjectList.y, kEjectList.w, kEjectList.h);
    const Widget* w = widget(item.widget);
    if (!w)
        return false;
    int width = w->w, height = w->h;
    if (item.button)
        if (const image::Image* im = pictures_.texture(item.picture)) {
            width = im->w;
            height = im->h / item.states;
        }
    return inside(x, y, w->x + originX(), w->y + originY(), width, height);
}

NetMenus::Action NetMenus::handle(const Event& ev) {
    Action action = Action::None;
    if (!messagePicture_.empty()) {
        // A message: any key or click puts it away.
        if (ev.type == EventType::KeyDown || (ev.type == EventType::MouseDown && ev.button == MouseButton::Left)) {
            message_.clear();
            messagePicture_.clear();
        }
        sys_.setTextInput(typing());
        return action;
    }
    if (session_ && session_->joining()) {
        if (ev.type == EventType::KeyDown && ev.key == key::Escape) {
            leave();
            go(Screen::Sessions);
        }
        return action;
    }
    const auto list = items();
    if (list.empty())
        return action;
    focus_ = std::clamp(focus_, 0, int(list.size()) - 1);
    const Item& item = list[size_t(focus_)];
    if (ev.type == EventType::TextInput && item.text) {
        if (std::string* t = textOf(item.widget))
            for (const char* c = ev.text; *c; ++c)
                if (static_cast<unsigned char>(*c) >= 32 && t->size() < (item.widget == "Chat Entry" ? 100u : 31u))
                    t->push_back(*c);
        return action;
    }
    if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        // The Host Race Menu's arrows step their values.
        if (screen_ == Screen::HostRace) {
            static const std::pair<const char*, const char*> kArrows[] = {{"RACE UP", "RACE NAME"},
                                                                          {"RACE DOWN", "RACE NAME"},
                                                                          {"LIMIT VALUE UP", "LIMIT VALUE"},
                                                                          {"LIMIT VALUE DOWN", "LIMIT VALUE"},
                                                                          {"GOLD MASS UP", "GOLD MASS"},
                                                                          {"GOLD MASS DOWN", "GOLD MASS"},
                                                                          {"RACE LOCALE UP", "RACE LOCALE"},
                                                                          {"RACE LOCALE DOWN", "RACE LOCALE"},
                                                                          {"TIME UP", "TIME"},
                                                                          {"TIME DOWN", "TIME"},
                                                                          {"WEATHER UP", "WEATHER"},
                                                                          {"WEATHER DOWN", "WEATHER"}};
            for (const auto& [arrow, field] : kArrows)
                if (const Widget* w = widget(arrow))
                    if (inside(ev.mouseX, ev.mouseY, w->x, w->y, std::min(w->w, 32), std::max(w->h / 2, 12))) {
                        bool shown = false;
                        for (const Item& i : list)
                            shown = shown || i.widget == field;
                        if (shown) {
                            Item f;
                            f.widget = field;
                            change(f, std::string(arrow).find("DOWN") != std::string::npos ? -1 : 1);
                            return action;
                        }
                    }
        }
        for (size_t i = 0; i < list.size(); ++i)
            if (hit(list[i], ev.mouseX, ev.mouseY)) {
                focus_ = int(i);
                if (list[i].button)
                    activate(list[i], action);
                else if (list[i].widget == "textscroll") {
                    const Widget* w = widget("textscroll");
                    const int row = (ev.mouseY - w->y - 4) / kRow - 1;
                    if (row >= 0 && browser_ && size_t(row) < browser_->sessions().size())
                        chosenSession_ = size_t(row);
                } else if (!list[i].text) {
                    const Widget* w = widget(list[i].widget);
                    change(list[i], w && ev.mouseX < w->x + originX() + w->w / 2 ? -1 : 1);
                }
                sys_.setTextInput(typing());
                return action;
            }
        return action;
    }
    if (ev.type != EventType::KeyDown)
        return action;
    switch (ev.key) {
    case key::Up:
    case key::Down: {
        const int step = ev.key == key::Down ? 1 : -1;
        for (size_t tries = 0; tries < list.size(); ++tries) {
            focus_ = (focus_ + step + int(list.size())) % int(list.size());
            if (list[size_t(focus_)].enabled)
                break;
        }
        break;
    }
    case key::Left:
    case key::Right:
        if (!item.button && !item.text)
            change(item, ev.key == key::Right ? 1 : -1);
        break;
    case key::Backspace:
        if (std::string* t = item.text ? textOf(item.widget) : nullptr; t && !t->empty())
            t->pop_back();
        break;
    case key::Return:
    case key::KpEnter:
        if (item.button)
            activate(item, action);
        else if (item.widget == "Chat Entry") {
            if (session_ && !chat_.empty())
                session_->say(chat_);
            chat_.clear();
        } else if (item.widget == "textscroll")
            activate(list[7], action); // join the one chosen
        else if (screen_ == Screen::Address || screen_ == Screen::Password || screen_ == Screen::HostOptions ||
                 screen_ == Screen::Eject)
            activate(list.back(), action); // done
        else if (screen_ == Screen::HostRace)
            activate(list.back(), action); // done
        break;
    case key::Escape:
        switch (screen_) {
        case Screen::Sessions:
            leave();
            action = Action::Back;
            break;
        case Screen::Address:
        case Screen::Password:
        case Screen::HostOptions:
            go(Screen::Sessions);
            break;
        case Screen::HostRace:
            go(editing_ ? Screen::Lobby : Screen::HostOptions);
            break;
        case Screen::Eject:
            go(Screen::Lobby);
            break;
        case Screen::Lobby:
            leave();
            go(Screen::Sessions);
            break;
        }
        break;
    default:
        break;
    }
    sys_.setTextInput(typing());
    return action;
}

NetMenus::Action NetMenus::update(uint64_t nowMs) {
    nowMs_ = nowMs;
    Action action = Action::None;
    if (browser_ && !session_)
        browser_->update(nowMs);
    if (!session_)
        return action;
    session_->update(nowMs);
    for (const NetNotice& n : session_->takeNotices()) {
        say(noticeText(n), true);
        switch (n.kind) {
        case NetNotice::Kind::Started:
            action = Action::Load;
            break;
        case NetNotice::Kind::BackToLobby:
            action = Action::Lobby;
            break;
        case NetNotice::Kind::Ejected:
            message({}, "boot_dlg");
            break;
        case NetNotice::Kind::HostQuit:
        case NetNotice::Kind::Refused:
            message(noticeText(n));
            break;
        default:
            break;
        }
    }
    for (const NetChatLine& c : session_->chat())
        if (c.seq > lastChatSeq_) {
            lastChatSeq_ = c.seq;
            say(c.from + ": " + c.text, false);
        }
    if (session_->closed()) {
        session_.reset();
        go(Screen::Sessions);
        action = Action::Lobby;
    }
    return action;
}

void NetMenus::text(Surface& o, const std::string& name, const std::string& s, Color c) const {
    if (const Widget* w = widget(name))
        font::drawText(o, w->x + originX() + 6,
                       w->y + originY() + std::max(0, (std::min(w->h, 24) - font::kLineHeight) / 2), s, c);
}

void NetMenus::drawDialog(Surface& o, const std::string& picture, const std::string& title) const {
    if (const image::Image* im = pictures_.jpg(picture))
        drawPicture(o, *im, kDialogX, kDialogY);
    else {
        // No picture of its own (the address box's isn't shipped): a
        // panel in the dialogs' colours.
        fillBox(o, {kDialogX, kDialogY, 400, 330}, {30, 10, 70});
        fillBox(o, {kDialogX, kDialogY, 400, 3}, {120, 90, 230});
        font::drawText(o, kDialogX + 400 - 12 - int(title.size()) * font::kAdvance * 2, kDialogY + 16, title,
                       {255, 255, 255}, 2);
    }
}

void NetMenus::drawHostRace(Surface& o) const {
    const std::string city = cities_.empty() ? "sf" : cityPicture(cities_[settings_.city].map);
    // The race's map, else the city's.
    std::string map = city == "london" ? "mapr_lon" : "mapr_sf";
    if (hasRace(settings_.mode)) {
        const RaceKind k = raceKind(settings_.mode);
        map = city + "_map" +
              (k == RaceKind::Blitz     ? "blitz"
               : k == RaceKind::Circuit ? "circuit"
                                        : "race") +
              std::to_string(settings_.race);
    }
    if (const image::Image* m = pictures_.jpg(map))
        if (const Widget* w = widget("race map"))
            drawPicture(o, *m, w->x, w->y, w->w, w->h);
    if (hasRace(settings_.mode))
        text(o, "RACE NAME",
             raceNames_.empty() ? "" : raceNames_[std::min<size_t>(settings_.race, raceNames_.size() - 1)]);
    if (settings_.mode == NetMode::Circuit)
        text(o, "LAPS", std::to_string(settings_.laps));
    if (settings_.mode == NetMode::CopsRobbers) {
        if (settings_.limit != GoldLimit::None)
            text(o, "LIMIT VALUE",
                 std::to_string(settings_.limitValue) + (settings_.limit == GoldLimit::Time ? " min" : " pts"));
        text(o, "GOLD MASS", std::to_string(settings_.goldMass) + " kg");
    }
    text(o, "RACE LOCALE", cities_.empty() ? "" : cities_[settings_.city].name);
    text(o, "TIME", kTimes[settings_.timeOfDay & 3]);
    text(o, "WEATHER", kWeathers[settings_.weather & 3]);
    if (const Widget* w = widget("PEDESTRIAN DENSITY")) {
        // Over the background's grid of cells.
        const Box b{w->x + 8, w->y + 2, int(float(w->w - 26) * settings_.peds), w->h - 10};
        fillBox(o, b, {255, 200, 40});
    }
    static const char* const kDesc[] = {"race_hrm", "race_hbt", "race_hcp", "race_hct", "race_cop"};
    if (const image::Image* d = pictures_.jpg(kDesc[int(settings_.mode)]))
        drawPicture(o, *d, 40, 396);
}

void NetMenus::drawLobby(Surface& o) const {
    if (!session_)
        return;
    const NetSettings& s = session_->settings();
    const std::string city = s.city < cities_.size() ? cities_[s.city].name : "";
    // The host's settings.
    std::vector<std::string> lines = {session_->sessionName(), modeName(s.mode)};
    if (hasRace(s.mode)) {
        const auto names = s.city < cities_.size() ? raceNames(files_, cities_[s.city].map, raceKind(s.mode))
                                                   : std::vector<std::string>{};
        lines.push_back(s.race < names.size() ? names[s.race] : "");
        if (s.mode == NetMode::Circuit)
            lines.push_back(std::to_string(s.laps) + " laps");
    }
    if (s.mode == NetMode::CopsRobbers) {
        lines.push_back(goldModeName(s.gold));
        lines.push_back(s.limit == GoldLimit::None   ? std::string("No limit")
                        : s.limit == GoldLimit::Time ? std::to_string(s.limitValue) + " minutes"
                                                     : std::to_string(s.limitValue) + " points");
        lines.push_back(strings_.get(335, "Gold Mass") + ": " + std::to_string(s.goldMass) + " kg");
    }
    lines.push_back(strings_.get(87, "Locale:") + " " + city);
    lines.push_back(std::string(kTimes[s.timeOfDay & 3]) + ", " + kWeathers[s.weather & 3]);
    for (size_t i = 0; i < lines.size() && i < 8; ++i)
        font::drawText(o, kSettingsBox.x + 4, kSettingsBox.y + 2 + int(i) * kRow, lines[i],
                       i == 0 ? Color{255, 230, 140} : Color{220, 220, 255});
    // The players.
    if (const Widget* w = widget("compscroll")) {
        int y = w->y + 4;
        font::drawText(o, w->x + 6, y,
                       s.teams() ? "Player          Car                 Team      Ready  Ping"
                                 : "Player          Car                           Ready  Ping",
                       {150, 200, 255});
        y += kRow + 2;
        for (const NetPlayer& p : session_->players()) {
            std::string car = carFor(p);
            for (const CarInfo& c : cars_)
                if (c.base == car)
                    car = c.description;
            // The host marked with a star; it's ready when it starts.
            const std::string name = p.name + (p.id == 0 ? "*" : ""), ready = p.ready || p.id == 0 ? "yes" : "-";
            char row[160];
            if (s.teams())
                std::snprintf(row, sizeof row, "%-15.15s %-19.19s %-9.9s %-6s %d", name.c_str(), car.c_str(),
                              teamName(p.team).c_str(), ready.c_str(), int(p.ping));
            else
                std::snprintf(row, sizeof row, "%-15.15s %-29.29s %-6s %d", name.c_str(), car.c_str(), ready.c_str(),
                              int(p.ping));
            font::drawText(o, w->x + 6, y, row, p.id == session_->myId() ? Color{255, 255, 120} : Color{230, 230, 230});
            y += kRow;
        }
    }
    // You.
    if (const NetPlayer* me = session_->me()) {
        std::string car = carFor(*me);
        for (const CarInfo& c : cars_)
            if (c.base == car)
                car = c.description;
        font::drawText(o, kYouBox.x, kYouBox.y + 2, me->name, {255, 255, 120});
        font::drawText(o, kYouBox.x, kYouBox.y + 2 + kRow, car, {230, 230, 230});
        if (s.teams())
            font::drawText(o, kYouBox.x, kYouBox.y + 2 + 2 * kRow, teamName(me->team), {255, 200, 120});
    }
    if (s.teams())
        if (const image::Image* label = pictures_.jpg("lobb_tem"))
            drawPicture(o, *label, 474, 190);
    // The chat (and who's come and gone).
    const int shown = kChatBox.h / kRow;
    int y = kChatBox.y;
    for (size_t i = lines_.size() > size_t(shown) ? lines_.size() - size_t(shown) : 0; i < lines_.size(); ++i) {
        font::drawText(o, kChatBox.x, y, lines_[i].text.substr(0, 58),
                       lines_[i].system ? Color{255, 200, 120} : Color{230, 230, 230});
        y += kRow;
    }
    // The map: the race's, else the city's.
    const std::string pic = s.city < cities_.size() ? cityPicture(cities_[s.city].map) : "sf";
    std::string map = pic == "london" ? "mapr_lon" : "mapr_sf";
    if (hasRace(s.mode)) {
        const RaceKind k = raceKind(s.mode);
        map = pic + "_map" +
              (k == RaceKind::Blitz     ? "blitz"
               : k == RaceKind::Circuit ? "circuit"
                                        : "race") +
              std::to_string(s.race);
    }
    if (const image::Image* m = pictures_.jpg(map))
        if (const Widget* w = widget("race map"))
            drawPicture(o, *m, w->x, w->y, w->w, w->h);
    // The description: the host settings button's, else the event's.
    const auto list = items();
    const std::string focused = size_t(focus_) < list.size() ? list[size_t(focus_)].widget : "";
    static const char* const kDesc[] = {"race_hrm", "race_hbt", "race_hcp", "race_hct", "race_cop"};
    if (const image::Image* d = pictures_.jpg(focused == "lobb_hst" ? "lobb_set" : kDesc[int(s.mode)]))
        drawPicture(o, *d, 40, 396);
    // Waiting for the players to be ready (the game's words).
    if (session_->isHost()) {
        int waiting = 0;
        for (const NetPlayer& p : session_->players())
            waiting += p.id != session_->myId() && !p.ready ? 1 : 0;
        if (waiting > 0)
            font::drawText(o, kHeadingBox.x + 6, kHeadingBox.y + 6,
                           strings_.get(uint32_t(30 + std::min(waiting, 7)), "Waiting for players"), {255, 120, 120});
    } else if (!session_->joining()) {
        font::drawText(o, kHeadingBox.x + 6, kHeadingBox.y + 6, strings_.get(432, "waiting for host to start..."),
                       {150, 200, 255});
    }
}

void NetMenus::draw(Surface& o, uint64_t nowMs) {
    nowMs_ = nowMs;
    std::fill(o.pixels.begin(), o.pixels.end(), 0);
    // The screen under a dialog.
    const Screen under = screen_ == Screen::Address || screen_ == Screen::Password || screen_ == Screen::HostOptions
                             ? Screen::Sessions
                         : screen_ == Screen::Eject ? Screen::Lobby
                                                    : screen_;
    const bool host = session_ && session_->isHost();
    const char* background = under == Screen::Sessions   ? "sess_bk"
                             : under == Screen::HostRace ? "host_bk"
                             : host                      ? "lobbh_bk"
                                                         : "lobbj_bk";
    if (const image::Image* bg = pictures_.jpg(background))
        drawPicture(o, *bg, 0, 0, o.w, o.h);
    // Cops & Robbers' choices, on their own panel, under their buttons.
    if (under == Screen::HostRace && settings_.mode == NetMode::CopsRobbers)
        if (const image::Image* panel = pictures_.jpg("host_cr"))
            drawPicture(o, *panel, kCopsPanel.x, kCopsPanel.y);

    const Screen was = screen_;
    const int keepFocus = focus_;
    if (under != screen_) {
        // Draw the screen beneath as it is, without a focus.
        screen_ = under;
        focus_ = -1;
    }
    for (int pass = 0; pass < (under != was ? 2 : 1); ++pass) {
        if (pass == 1) {
            screen_ = was;
            focus_ = keepFocus;
            if (was == Screen::Eject)
                drawDialog(o, "ejct_dlg", "EJECT PLAYER");
            else if (was == Screen::HostOptions)
                drawDialog(o, "host_dlg", "HOST OPTIONS");
            else if (was == Screen::Password)
                drawDialog(o, "pass_dlg", "PASSWORD");
            else
                drawDialog(o, "", "ENTER AN ADDRESS");
        }
        const auto list = items();
        for (size_t i = 0; i < list.size(); ++i) {
            const Item& item = list[i];
            const Widget* w = widget(item.widget);
            if (!w)
                continue;
            const bool focused = int(i) == focus_;
            if (item.button) {
                const image::Image* im = pictures_.texture(item.picture);
                if (!im)
                    continue;
                bool chosen = item.widget == "sess_tcp";
                if (screen_ == Screen::HostRace) {
                    static const char* const kModes[] = {"cruise_m", "blitz_m", "cp_m", "circt_m", "cops_m"};
                    static const char* const kGold[] = {"free_cr", "cpsvr_cr", "robrs_cr"};
                    static const char* const kLimits[] = {"none_cr", "time_cr", "point_cr"};
                    chosen = item.widget == kModes[int(settings_.mode)] || item.widget == kGold[int(settings_.gold)] ||
                             item.widget == kLimits[int(settings_.limit)];
                }
                if (screen_ == Screen::Lobby && session_ && session_->me())
                    chosen = (item.widget == "lobb_red" && session_->me()->team == 0) ||
                             (item.widget == "lobb_blu" && session_->me()->team == 1);
                const int state = !item.enabled                            ? item.states - 1
                                  : item.states == 5 && chosen && !focused ? 2
                                  : focused                                ? 1
                                                                           : 0;
                drawButtonState(o, *im, w->x + originX(), w->y + originY(), item.states, state);
            } else if (focused) {
                font::drawText(o, w->x + originX() - 10, w->y + originY() + 4, ">", {255, 255, 120});
            }
        }
        // Each screen's words.
        switch (screen_) {
        case Screen::Sessions: {
            text(o, "NET NAME", netName_ + (focus_ == 5 && (nowMs / 400) % 2 ? "_" : ""));
            font::drawText(o, kHeadingBox.x + 6, kHeadingBox.y + 6, "TCP/IP", {255, 230, 140});
            if (const Widget* w = widget("textscroll")) {
                const auto& found = browser_ ? browser_->sessions() : std::vector<NetBrowser::Found>{};
                int y = w->y + 4;
                font::drawText(o, w->x + 6, y, "Session          Players  Event           Ping", {150, 200, 255});
                y += kRow;
                if (found.empty())
                    font::drawText(o, w->x + 6, y + 4, "None on this network yet. JOIN to give an address.",
                                   {200, 200, 200});
                for (size_t i = 0; i < found.size() && i < 8; ++i) {
                    const auto& f = found[i];
                    char row[160];
                    std::snprintf(row, sizeof row, "%-16.16s %d/%d      %-15.15s %d%s%s", f.name.c_str(), f.players,
                                  f.maxPlayers, modeName(f.mode).c_str(), f.ping, f.playing ? " (playing)" : "",
                                  f.password       ? " (password)"
                                  : !f.sameVersion ? " (other version)"
                                                   : "");
                    font::drawText(o, w->x + 6, y, row,
                                   i == chosenSession_ ? Color{255, 255, 120} : Color{230, 230, 230});
                    y += kRow;
                }
            }
            if (const image::Image* d = pictures_.jpg("mpst_tcp"))
                drawPicture(o, *d, 40, 396);
            break;
        }
        case Screen::Address:
            font::drawText(o, kDialogX + 72, kDialogY + 72,
                           "HOST NAME OR ADDRESS (port " + std::to_string(kNetPort) + ")", {220, 220, 255});
            fillBox(o, {kDialogX + 72, kDialogY + 90, 203, 22}, {0, 0, 20});
            text(o, "TCP/IP Address", address_ + ((nowMs / 400) % 2 && focus_ == 0 ? "_" : ""));
            break;
        case Screen::Password:
        case Screen::HostOptions: {
            std::string stars(password_.size(), '*');
            text(o, "Password", stars + ((nowMs / 400) % 2 && focus_ == 0 ? "_" : ""));
            if (screen_ == Screen::HostOptions) {
                text(o, "Max Players", std::to_string(settings_.maxPlayers));
                if (const Widget* w = widget(kBotsField)) {
                    // Its label and box, as the dialog draws Max Players'.
                    const std::string label = "COMPUTER PLAYERS";
                    font::drawText(o, kDialogX + w->x - 12 - int(label.size()) * font::kAdvance, kDialogY + w->y + 10,
                                   label, {230, 230, 255});
                    fillBox(o, {kDialogX + w->x, kDialogY + w->y, w->w, w->h - 8}, {0, 0, 20});
                    text(o, kBotsField, std::to_string(bots_));
                }
            }
            break;
        }
        case Screen::HostRace:
            drawHostRace(o);
            break;
        case Screen::Lobby:
            drawLobby(o);
            text(o, "Chat Entry", chat_ + ((nowMs / 400) % 2 && focus_ == 0 ? "_" : ""), {255, 255, 255});
            break;
        case Screen::Eject:
            if (session_) {
                int y = kEjectList.y;
                size_t k = 0;
                for (const NetPlayer& p : session_->players()) {
                    if (p.id == session_->myId())
                        continue;
                    font::drawText(o, kEjectList.x, y, p.name,
                                   k == ejectChoice_ ? Color{255, 255, 120} : Color{230, 230, 230});
                    y += kRow;
                    ++k;
                }
            }
            break;
        }
    }
    screen_ = was;
    focus_ = keepFocus;

    // Joining: "Retrieving game settings..."
    if (session_ && session_->joining())
        if (const image::Image* d = pictures_.jpg("rtrv_dlg"))
            drawPicture(o, *d, kDialogX, kMessageY);
    if (!messagePicture_.empty()) {
        if (const image::Image* d = pictures_.jpg(messagePicture_))
            drawPicture(o, *d, kDialogX, kMessageY);
        if (!message_.empty())
            font::drawText(o, kDialogX + 80, kMessageY + 30, message_.substr(0, 38), {255, 255, 255});
    }
    font::drawText(o, 4, o.h - 12, "Up/Down, Left/Right, Enter, Esc; type in the boxes", {200, 200, 200});
}

} // namespace reluva::age
