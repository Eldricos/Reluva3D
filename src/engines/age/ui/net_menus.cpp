// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The multiplayer menus: tune/widget.csv's Net Select Menu, Enter an
// Address, Host Options, Host Race Menu, Network Arena and their dialogs,
// on the game's jpg/ backgrounds with its texture/ buttons. Which picture
// is which control, and what it does, is Reluva's reading of the
// pictures; see docs/engines/age/games/mm2/systems/multiplayer.md.

#include "engines/age/ui/net_menus.h"

#include "graphics/font.h"
#include "engines/age/formats/race.h"
#include "engines/age/game/car.h"
#include "engines/age/ui/screen.h"

#include <algorithm>
#include <cstdio>

namespace reluva::age {

namespace {

// The dialogs' corner (tune/menu.csv: 120, 75, 400 x 330), and a message
// box's (120, 202, 400 x 76).
constexpr int kDialogX = 120;
constexpr int kDialogY = 75;
constexpr int kMessageY = 202;
// Boxes drawn on the backgrounds, not controls (measured on the jpg/
// pictures): the lobby's host settings, its "you" and its chat; the
// heading's box; the eject dialog's list.
struct Box {
    int x;
    int y;
    int w;
    int h;
};
constexpr Box kSettingsBox{38, 66, 222, 114};
constexpr Box kYouBox{276, 214, 180, 46};
constexpr Box kChatBox{278, 306, 350, 62};
constexpr Box kHeadingBox{453, 10, 164, 24};
constexpr Box kEjectList{192, 164, 256, 160};
constexpr Box kCopsPanel{267, 45, 373, 213}; // where jpg/host_cr goes
constexpr int kRow = 13;                     // a list's line
constexpr size_t kLinesKept = 50;

const char *const kTimes[] = {"Morning", "Noon", "Evening", "Night"};
const char *const kWeathers[] = {"Clear", "Cloudy", "Fog", "Rain"};

bool inside(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void fillBox(Surface &o, const Box &b, Color c, uint8_t alpha = 255) {
    for (int y = std::max(0, b.y); y < std::min(o.h, b.y + b.h); ++y)
        for (int x = std::max(0, b.x); x < std::min(o.w, b.x + b.w); ++x) {
            uint8_t *p = o.row(y) + x * 4;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = alpha;
        }
}

std::string cityPicture(const std::string &map) {
    return map == "london" ? "london" : "sf";
}

// Cops & Robbers' choices as the game words them (mmlang.dll 506-513):
// its gold masses, Weightless, Quarter Ton and Half Ton (a US ton's, in
// kg), and its time limits.
constexpr int kGoldMasses[3] = {0, 227, 454};
constexpr int kTimeLimits[4] = {5, 10, 20, 30};
int goldMassIndex(int kg) {
    int at = 0;
    for (int i = 0; i < 3; ++i)
        if (kGoldMasses[i] <= kg)
            at = i;
    return at;
}

} // namespace

NetMenus::NetMenus(System &sys, const SearchSet &files, const std::vector<CityInfo> &cities,
                   const std::vector<CarInfo> &cars, const GameStrings &strings)
    : _sys(sys), _files(files), _cities(cities), _cars(cars), _strings(strings), _pictures(files) {
    std::vector<uint8_t> data;
    if (!files.read("tune/widget.csv", data))
        _ok = !parseWidgets(std::string(data.begin(), data.end()), _widgets) && _pictures.jpg("sess_bk") &&
              !cities.empty() && !cars.empty();
    // Reluva's own: how many computer players, below Max Players and
    // drawn as it is.
    if (const Widget *max = widget("Max Players", "Host Options")) {
        Widget bots = *max;
        bots.name = kBotsField;
        bots.y += 52;
        _widgets.push_back(bots);
    }
    if (!cars.empty())
        _myCar = cars.front().base;
    // Reluva's own: the meeting server, in the panel under the connections.
    _widgets.push_back({"Net Select Menu", "MEETING SERVER", 46, 312, 214, 22, ""});
    _meetingServer = currentOptions().meetingServer;
}

NetMenus::~NetMenus() {
    leave();
}

RaceKind NetMenus::raceKind(NetMode mode) {
    return mode == NetMode::kCircuit      ? RaceKind::kCircuit
           : mode == NetMode::kCheckpoint ? RaceKind::kCheckpoint
                                          : RaceKind::kBlitz;
}

std::string NetMenus::modeName(NetMode mode) const {
    switch (mode) {
    case NetMode::kCruise:
        return _strings.get(80, "Cruise");
    case NetMode::kBlitz:
        return _strings.get(7, "Blitz Race");
    case NetMode::kCheckpoint:
        return _strings.get(6, "Checkpoint Race");
    case NetMode::kCircuit:
        return _strings.get(5, "Circuit Race");
    case NetMode::kCopsRobbers:
        return _strings.get(79, "Cops & Robbers");
    }
    return {};
}

std::string NetMenus::goldModeName(GoldMode mode) const {
    switch (mode) {
    case GoldMode::kFreeForAll:
        return _strings.get(8, "Free-For-All");
    case GoldMode::kCopsAndRobbers:
        return _strings.get(9, "Cops & Robbers");
    case GoldMode::kRobberTeams:
        return _strings.get(10, "Robbers Teams");
    }
    return {};
}

std::string NetMenus::teamName(uint8_t team) const {
    // The game's words: COPS, ROBBERS, BLUE, RED.
    const NetSettings &s = _session ? _session->settings() : _settings;
    if (s.gold == GoldMode::kCopsAndRobbers)
        return team ? _strings.get(122, "COPS") : _strings.get(123, "ROBBERS");
    return team ? _strings.get(124, "BLUE") : _strings.get(125, "RED");
}

std::string NetMenus::carFor(const NetPlayer &p) const {
    const NetSettings &s = _session ? _session->settings() : _settings;
    if (s.mode == NetMode::kCopsRobbers && s.gold == GoldMode::kCopsAndRobbers && p.team == 1)
        for (const CarInfo &c : _cars)
            if (c.base == "vpcop")
                return c.base;
    for (const CarInfo &c : _cars)
        if (c.base == p.car)
            return c.base;
    return _cars.empty() ? std::string() : _cars.front().base;
}

std::string NetMenus::goldMassName(int kg) const {
    static const char *const kNames[3] = {"Weightless", "Quarter Ton", "Half Ton"};
    const int at = goldMassIndex(kg);
    return _strings.get(uint32_t(506 + at), kNames[at]);
}

std::string NetMenus::timeLimitName(int minutes) const {
    for (int i = 0; i < 4; ++i)
        if (kTimeLimits[i] == minutes)
            return _strings.get(uint32_t(510 + i), std::to_string(minutes) + " minutes");
    return std::to_string(minutes) + " minutes";
}

std::string NetMenus::noticeText(const NetNotice &n) const {
    switch (n.kind) {
    case NetNotice::Kind::kJoined:
        return n.name + " " + _strings.get(42, "has joined");
    case NetNotice::Kind::kLeft:
        return n.name + " " + _strings.get(38, "has left the game");
    case NetNotice::Kind::kHostQuit:
        return _strings.get(40, "The Host has quit");
    case NetNotice::Kind::kEjected:
        return _strings.get(44, "You have been ejected");
    case NetNotice::Kind::kBackToLobby:
        return _strings.get(73, "**Session returning to Lobby**");
    case NetNotice::Kind::kStarted:
        return {};
    case NetNotice::Kind::kHostMoved: // the game's words for the new host's own screen
        if (_session && _session->isHost())
            return _strings.get(39, "You are now the Host");
        return n.name + " is the host now";
    case NetNotice::Kind::kSuspect:
        if (_session && _session->me() && _session->me()->name == n.name && !_session->isHost())
            return _strings.get(46, "You are a cheater");
        return n.name + " " + _strings.get(41, "is a cheater");
    case NetNotice::Kind::kRefused:
        switch (n.reason) {
        case kRefusedVersion:
            return _strings.get(83, "ERROR: Network versions do not match.");
        case kRefusedFull:
            return "The session is full";
        case kRefusedPlaying: // older hosts' word
            return "The game has started: wait for the lobby";
        case kRefusedPassword:
            return "The password is wrong";
        default:
            return "The host didn't answer";
        }
    }
    return {};
}

void NetMenus::say(const std::string &s, bool system) {
    if (s.empty())
        return;
    _lines.push_back({s, _nowMs, system});
    while (_lines.size() > kLinesKept)
        _lines.pop_front();
}

void NetMenus::message(const std::string &text, const std::string &picture) {
    _message = text;
    _messagePicture = picture;
}

void NetMenus::enter() {
    leave();
    go(Screen::kSessions);
}

void NetMenus::leave() {
    if (_meetingHost)
        _meetingHost->unpublish();
    _meetingHost = nullptr;
    if (_session)
        _session->leave();
    _session.reset();
    _meetingBrowser.reset();
    _mapper.reset(); // the router's port closed again
    _local.reset();
    _browser.reset();
    _sys.setTextInput(false);
}

void NetMenus::showLobby() {
    if (_session)
        go(Screen::kLobby);
    else
        go(Screen::kSessions);
}

void NetMenus::carChosen(const std::string &car, int paintJob) {
    _myCar = car;
    _myPaint = paintJob;
    if (_session)
        _session->setCar(car, uint8_t(paintJob));
    showLobby();
}

std::string NetMenus::menuName() const {
    switch (_screen) {
    case Screen::kSessions:
        return "Net Select Menu";
    case Screen::kAddress:
        return "Enter an Address";
    case Screen::kPassword:
        return "Enter a valid password";
    case Screen::kHostOptions:
        return "Host Options";
    case Screen::kHostRace:
        return "Host Race Menu";
    case Screen::kLobby:
        return "Network Arena";
    case Screen::kEject:
        return "eject_DLG";
    }
    return {};
}

int NetMenus::originX() const {
    return _screen == Screen::kAddress || _screen == Screen::kPassword || _screen == Screen::kHostOptions ||
                   _screen == Screen::kEject
               ? kDialogX
               : 0;
}

int NetMenus::originY() const {
    return originX() ? kDialogY : 0;
}

const Widget *NetMenus::widget(const std::string &name) const {
    return widget(name, menuName());
}

const Widget *NetMenus::widget(const std::string &name, const std::string &menu) const {
    for (const Widget &w : _widgets)
        if (w.menu == menu && w.name == name)
            return &w;
    return nullptr;
}

std::vector<NetMenus::Item> NetMenus::items() const {
    auto button = [](const std::string &w, const std::string &pic, int states, bool enabled = true) {
        Item i;
        i.widget = w;
        i.picture = pic;
        i.button = true;
        i.states = states;
        i.enabled = enabled;
        return i;
    };
    auto field = [](const std::string &w, bool typed = false) {
        Item i;
        i.widget = w;
        i.text = typed;
        return i;
    };
    const Item cancel = button("dlg_cancel", "dlg_can", 4);
    const Item done = button("dlg_done", "dlg_done", 4);
    switch (_screen) {
    case Screen::kSessions:
        // TCP/IP only: the zone, IPX, serial and modem aren't offered.
        // (The zone's picture has five states, though widget.csv says four.)
        return {button("sess_zn", "sess_zn", 5, false),
                button("sess_ipx", "sess_ipx", 5, false),
                button("sess_tcp", "sess_tcp", 5),
                button("sess_ser", "sess_ser", 5, false),
                button("sess_mod", "sess_mod", 5, false),
                field("NET NAME", true),
                field("MEETING SERVER", true),
                button("sess_hst", "sess_hst", 4),
                button("sess_jn", "sess_jn", 4),
                field("textscroll"),
                button("mnav_prev", "mnav_prv", 4)};
    case Screen::kAddress:
        return {field("TCP/IP Address", true), cancel, done};
    case Screen::kPassword:
        return {field("Password", true), cancel, done};
    case Screen::kHostOptions:
        return {field("Password", true), field("Max Players"), field(kBotsField), cancel, done};
    case Screen::kHostRace: {
        std::vector<Item> list = {button("cruise_m", "cruise_m", 5), button("blitz_m", "blitz_m", 5),
                                  button("cp_m", "cp_m", 5), button("circt_m", "circt_m", 5),
                                  button("cops_m", "cops_m", 5)};
        if (hasRace(_settings.mode))
            list.push_back(field("RACE NAME"));
        if (_settings.mode == NetMode::kCircuit)
            list.push_back(field("LAPS"));
        if (_settings.mode == NetMode::kCopsRobbers) {
            for (const char *b : {"free_cr", "cpsvr_cr", "robrs_cr", "none_cr", "time_cr", "point_cr"})
                list.push_back(button(b, b, 5));
            if (_settings.limit != GoldLimit::kNone)
                list.push_back(field("LIMIT VALUE"));
            list.push_back(field("GOLD MASS"));
        }
        for (const char *f : {"RACE LOCALE", "TIME", "WEATHER", "PEDESTRIAN DENSITY"})
            list.push_back(field(f));
        list.push_back(button("aren_strt", "host_dn", 4));
        return list;
    }
    case Screen::kLobby: {
        const bool host = _session && _session->isHost();
        const NetPlayer *me = _session ? _session->me() : nullptr;
        const NetSettings &s = _session ? _session->settings() : _settings;
        std::vector<Item> list = {field("Chat Entry", true)};
        if (s.teams()) {
            const bool copr = s.gold == GoldMode::kCopsAndRobbers;
            list.push_back(button("lobb_red", copr ? "lobb_rob" : "lobb_red", 5));
            list.push_back(button("lobb_blu", copr ? "lobb_cop" : "lobb_blu", 5));
        }
        list.push_back(button("lobb_ejt", "lobb_ejt", 4, host && _session->players().size() > 1));
        list.push_back(button("lobb_hst", "lobb_hst", 4, host));
        // A cop drives the police car: no choosing.
        const bool cop = me && s.mode == NetMode::kCopsRobbers && s.gold == GoldMode::kCopsAndRobbers && me->team == 1;
        list.push_back(button("mpst_veh", "lobb_veh", 4, !cop));
        if (host)
            list.push_back(button("lobb_rdy", "lobb_srt", 5, _session->everyoneReady()));
        else
            list.push_back(button("lobb_rdy", me && me->ready ? "lobb_rdy" : "lobb_nr", 5, me != nullptr));
        return list;
    }
    case Screen::kEject:
        return {field("ejectlist"), done};
    }
    return {};
}

bool NetMenus::typing() const {
    const auto list = items();
    return _messagePicture.empty() && _focus >= 0 && size_t(_focus) < list.size() && list[size_t(_focus)].text;
}

void NetMenus::go(Screen s) {
    _screen = s;
    _focus = 0;
    const auto list = items();
    // The first control that's there to use; on the sessions screen, Host.
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].enabled && (s != Screen::kSessions || list[i].widget == "sess_hst")) {
            _focus = int(i);
            break;
        }
    if (s == Screen::kSessions && !_browser) {
        std::unique_ptr<net::UdpSocket> socket;
        if (!_sys.openUdp(0, socket) && socket) {
            _browser = std::make_unique<NetBrowser>(std::move(socket));
            _browser->setBroadcasts(_sys.broadcastAddresses());
        } else
            message("The network is not available");
    }
    if (s == Screen::kHostRace)
        refreshRaces();
    _sys.setTextInput(typing());
}

void NetMenus::refreshRaces() {
    _raceNames.clear();
    if (_cities.empty())
        return;
    _settings.city = uint8_t(std::min<size_t>(_settings.city, _cities.size() - 1));
    if (hasRace(_settings.mode))
        _raceNames = raceNames(_files, _cities[_settings.city].map, raceKind(_settings.mode));
    _settings.race = uint8_t(std::min<size_t>(_settings.race, _raceNames.empty() ? 0 : _raceNames.size() - 1));
}

// Every car's top speed, for the host's check of the others' cars.
std::map<std::string, float> NetMenus::topSpeeds() const {
    std::map<std::string, float> speeds;
    for (const CarInfo &car : _cars) {
        CarSetup setup;
        if (!loadCarSetup(_files, car.base, setup))
            speeds[car.base] = setup.highSpeed;
    }
    return speeds;
}

void NetMenus::startHosting() {
    std::unique_ptr<net::UdpSocket> socket;
    if (Error err = _sys.openUdp(kNetPort, socket); err || !socket) {
        // No network (or the port's taken): a game on this computer alone,
        // against computer players.
        _local = net::LoopbackNetwork::create();
        socket = _local->open(kNetPort);
        message("Playing on this computer only: " + (err ? err.message : std::string("no network")));
    }
    _browser.reset();
    _meetingBrowser.reset();
    // Listed at the meeting server too, if one's given: its players reach
    // this one through it.
    _meetingHost = nullptr;
    _meetingSaid = false;
    net::Address meeting;
    if (!_local && meetingServer(meeting)) {
        auto listed = std::make_unique<net::MeetingSocket>(std::move(socket), meeting, "age");
        _meetingHost = listed.get();
        _meetingSinceMs = 0;
        socket = std::move(listed);
    }
    _session = NetSession::host(std::move(socket), _netName, _settings, _password);
    _session->setTopSpeeds(topSpeeds());
    _session->setCar(_myCar, uint8_t(_myPaint));
    // The computer players: the game's names for opponents, in cars from
    // the list.
    for (int i = 0; i < _bots; ++i) {
        const std::string car = _cars[(size_t(i) * 5 + 3) % _cars.size()].base;
        _session->addBot(_strings.get(uint32_t(13 + i), "Opponent " + std::to_string(i + 1)), car);
    }
    _lines.clear();
    _lastChatSeq = 0;
    go(Screen::kLobby);
}

void NetMenus::joinAddress(const net::Address &address) {
    std::unique_ptr<net::UdpSocket> socket;
    if (Error err = _sys.openUdp(0, socket); err || !socket) {
        message("The network is not available");
        return;
    }
    _session = NetSession::join(std::move(socket), address, _netName, _password);
    _session->setTopSpeeds(topSpeeds());
    _session->setCar(_myCar, uint8_t(_myPaint));
    _lines.clear();
    _lastChatSeq = 0;
    go(Screen::kLobby);
}

void NetMenus::joinMeeting(uint32_t id) {
    net::Address meeting;
    std::unique_ptr<net::UdpSocket> socket;
    if (!meetingServer(meeting)) {
        message("Can't find the meeting server " + _meetingServer);
        return;
    }
    if (Error err = _sys.openUdp(0, socket); err || !socket) {
        message("The network is not available");
        return;
    }
    auto through = std::make_unique<net::MeetingSocket>(std::move(socket), meeting, "age");
    const net::Address host = through->connect(id);
    _meetingBrowser.reset();
    _session = NetSession::join(std::move(through), host, _netName, _password);
    _session->setTopSpeeds(topSpeeds());
    _session->setCar(_myCar, uint8_t(_myPaint));
    _lines.clear();
    _lastChatSeq = 0;
    go(Screen::kLobby);
}

bool NetMenus::meetingServer(net::Address &out) {
    if (_meetingServer.empty())
        return false;
    if (_meetingServer != _meetingLooked) {
        _meetingLooked = _meetingServer;
        _meetingAt = {};
        const auto [host, port] = net::splitHostPort(_meetingServer, net::kMeetingPort);
        net::Address a;
        a.port = port;
        if (net::parseAddress(_meetingServer, net::kMeetingPort, a) || _sys.resolveHost(host, a.ip))
            _meetingAt = a;
        _meetingBrowser.reset();
        // Kept for next time.
        GameOptions options = currentOptions();
        if (options.meetingServer != _meetingServer) {
            options.meetingServer = _meetingServer;
            setCurrentOptions(options);
            if (!_savePath.empty())
                options.save(_savePath);
        }
    }
    out = _meetingAt;
    return _meetingAt.port != 0;
}

std::vector<NetBrowser::Found> NetMenus::sessionList() const {
    std::vector<NetBrowser::Found> list = _browser ? _browser->sessions() : std::vector<NetBrowser::Found>{};
    if (_meetingBrowser)
        for (const net::MeetingSocket::Listed &l : _meetingBrowser->listed()) {
            NetBrowser::Found f;
            if (!NetBrowser::parseInfo(l.info, f))
                continue;
            f.meetingId = l.id;
            f.ping = 0;
            list.push_back(f);
        }
    return list;
}

std::string *NetMenus::textOf(const std::string &w) {
    if (w == "NET NAME")
        return &_netName;
    if (w == "MEETING SERVER")
        return &_meetingServer;
    if (w == "TCP/IP Address")
        return &_address;
    if (w == "Password")
        return &_password;
    if (w == "Chat Entry")
        return &_chat;
    return nullptr;
}

void NetMenus::activate(const Item &item, Action &action) {
    if (!item.enabled)
        return;
    const std::string &w = item.widget;
    if (w == "sess_hst") {
        _editing = false;
        _password.clear();
        go(Screen::kHostOptions);
    } else if (w == "sess_jn") {
        const auto found = sessionList();
        if (_chosenSession < found.size()) {
            const auto &f = found[_chosenSession];
            _password.clear();
            _pendingJoin = f.address;
            _pendingMeeting = f.meetingId;
            if (f.password)
                go(Screen::kPassword);
            else if (f.meetingId)
                joinMeeting(f.meetingId);
            else
                joinAddress(f.address);
        } else
            go(Screen::kAddress);
    } else if (w == "mnav_prev") {
        leave();
        action = Action::kBack;
    } else if (w == "dlg_cancel") {
        go(_screen == Screen::kEject ? Screen::kLobby : Screen::kSessions);
    } else if (w == "dlg_done") {
        if (_screen == Screen::kAddress && _address.empty()) {
            go(Screen::kSessions); // "or leave blank to search for available session"
            return;
        }
        if (_screen == Screen::kAddress) {
            const auto [host, port] = net::splitHostPort(_address, kNetPort);
            net::Address a;
            a.port = port;
            if (!net::parseAddress(_address, kNetPort, a) && !_sys.resolveHost(host, a.ip)) {
                message("Can't find " + host);
                return;
            }
            _password.clear();
            joinAddress(a);
        } else if (_screen == Screen::kPassword) {
            if (_pendingMeeting)
                joinMeeting(_pendingMeeting);
            else
                joinAddress(_pendingJoin);
        } else if (_screen == Screen::kHostOptions) {
            _settings.maxPlayers = std::clamp<uint8_t>(_settings.maxPlayers, 2, kMaxNetPlayers);
            go(Screen::kHostRace);
        } else if (_screen == Screen::kEject && _session) {
            std::vector<uint8_t> ids;
            for (const NetPlayer &p : _session->players())
                if (p.id != _session->myId())
                    ids.push_back(p.id);
            if (_ejectChoice < ids.size())
                _session->eject(ids[_ejectChoice]);
            go(Screen::kLobby);
        }
    } else if (w == "cruise_m" || w == "blitz_m" || w == "cp_m" || w == "circt_m" || w == "cops_m") {
        _settings.mode = w == "cruise_m"  ? NetMode::kCruise
                         : w == "blitz_m" ? NetMode::kBlitz
                         : w == "cp_m"    ? NetMode::kCheckpoint
                         : w == "circt_m" ? NetMode::kCircuit
                                          : NetMode::kCopsRobbers;
        refreshRaces();
    } else if (w == "free_cr" || w == "cpsvr_cr" || w == "robrs_cr") {
        _settings.gold = w == "free_cr"    ? GoldMode::kFreeForAll
                         : w == "cpsvr_cr" ? GoldMode::kCopsAndRobbers
                                           : GoldMode::kRobberTeams;
    } else if (w == "none_cr" || w == "time_cr" || w == "point_cr") {
        _settings.limit = w == "none_cr" ? GoldLimit::kNone : w == "time_cr" ? GoldLimit::kTime : GoldLimit::kPoints;
        _settings.limitValue = uint16_t(_settings.limit == GoldLimit::kTime ? 10 : 5);
    } else if (w == "aren_strt") {
        if (_editing && _session) {
            _session->setSettings(_settings);
            go(Screen::kLobby);
        } else
            startHosting();
    } else if (w == "lobb_red" || w == "lobb_blu") {
        if (_session)
            _session->setTeam(w == "lobb_blu" ? 1 : 0);
    } else if (w == "lobb_ejt") {
        _ejectChoice = 0;
        go(Screen::kEject);
    } else if (w == "lobb_hst") {
        if (_session)
            _settings = _session->settings();
        _editing = true;
        go(Screen::kHostRace);
    } else if (w == "mpst_veh") {
        action = Action::kGarage;
    } else if (w == "lobb_rdy" && _session) {
        if (_session->isHost()) {
            if (Error err = _session->start(_nowMs))
                message(err.message);
        } else if (const NetPlayer *me = _session->me())
            _session->setReady(!me->ready);
    }
}

void NetMenus::change(const Item &item, int step) {
    auto wrap = [](int v, int n) { return n > 0 ? (v % n + n) % n : 0; };
    const std::string &w = item.widget;
    if (w == "textscroll") {
        const size_t n = sessionList().size();
        _chosenSession = size_t(wrap(int(_chosenSession) + step, int(n)));
    } else if (w == "Max Players") {
        _settings.maxPlayers = uint8_t(std::clamp(_settings.maxPlayers + step, 2, kMaxNetPlayers));
        _bots = std::min(_bots, _settings.maxPlayers - 1);
    } else if (w == kBotsField) {
        _bots = std::clamp(_bots + step, 0, _settings.maxPlayers - 1);
    } else if (w == "RACE NAME") {
        _settings.race = uint8_t(wrap(_settings.race + step, int(_raceNames.size())));
    } else if (w == "LAPS") {
        _settings.laps = uint8_t(std::clamp(_settings.laps + step, 1, 20));
    } else if (w == "LIMIT VALUE") {
        // The game's time limits (mmlang.dll: 5, 10, 20, 30 minutes); the
        // points are deliveries, 1-50.
        if (_settings.limit == GoldLimit::kTime) {
            int at = 0;
            for (int i = 0; i < 4; ++i)
                if (kTimeLimits[i] <= _settings.limitValue)
                    at = i;
            _settings.limitValue = uint16_t(kTimeLimits[std::clamp(at + step, 0, 3)]);
        } else
            _settings.limitValue = uint16_t(std::clamp(_settings.limitValue + step, 1, 50));
    } else if (w == "GOLD MASS") {
        // The game's three: Weightless, Quarter Ton, Half Ton.
        _settings.goldMass = uint16_t(kGoldMasses[std::clamp(goldMassIndex(_settings.goldMass) + step, 0, 2)]);
    } else if (w == "RACE LOCALE") {
        _settings.city = uint8_t(wrap(_settings.city + step, int(_cities.size())));
        refreshRaces();
    } else if (w == "TIME") {
        _settings.timeOfDay = uint8_t(wrap(_settings.timeOfDay + step, 4));
    } else if (w == "WEATHER") {
        _settings.weather = uint8_t(wrap(_settings.weather + step, 4));
    } else if (w == "PEDESTRIAN DENSITY") {
        _settings.peds = std::clamp(std::round(_settings.peds * 10 + float(step)) / 10, 0.0f, 1.0f);
    } else if (w == "ejectlist" && _session) {
        _ejectChoice = size_t(wrap(int(_ejectChoice) + step, int(_session->players().size()) - 1));
    }
}

bool NetMenus::hit(const Item &item, int x, int y) const {
    if (item.widget == "ejectlist")
        return inside(x, y, kEjectList.x, kEjectList.y, kEjectList.w, kEjectList.h);
    const Widget *w = widget(item.widget);
    if (!w)
        return false;
    int width = w->w;
    int height = w->h;
    if (item.button)
        if (const image::Image *im = _pictures.texture(item.picture)) {
            width = im->w;
            height = im->h / item.states;
        }
    return inside(x, y, w->x + originX(), w->y + originY(), width, height);
}

NetMenus::Action NetMenus::handle(const Event &ev) {
    Action action = Action::kNone;
    if (!_messagePicture.empty()) {
        // A message: any key or click puts it away.
        if (ev.type == EventType::kKeyDown || (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft)) {
            _message.clear();
            _messagePicture.clear();
        }
        _sys.setTextInput(typing());
        return action;
    }
    if (_session && _session->joining()) {
        if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
            leave();
            go(Screen::kSessions);
        }
        return action;
    }
    const auto list = items();
    if (list.empty())
        return action;
    _focus = std::clamp(_focus, 0, int(list.size()) - 1);
    const Item &item = list[size_t(_focus)];
    if (ev.type == EventType::kTextInput && item.text) {
        if (std::string *t = textOf(item.widget))
            for (const char *c = ev.text; *c; ++c)
                if (static_cast<unsigned char>(*c) >= 32 && t->size() < (item.widget == "Chat Entry" ? 100u : 31u))
                    t->push_back(*c);
        return action;
    }
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        // The Host Race Menu's arrows step their values.
        if (_screen == Screen::kHostRace) {
            static const std::pair<const char *, const char *> kArrows[] = {{"RACE UP", "RACE NAME"},
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
            for (const auto &[arrow, field] : kArrows)
                if (const Widget *w = widget(arrow))
                    if (inside(ev.mouseX, ev.mouseY, w->x, w->y, std::min(w->w, 32), std::max(w->h / 2, 12))) {
                        bool shown = false;
                        for (const Item &i : list)
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
                _focus = int(i);
                if (list[i].button)
                    activate(list[i], action);
                else if (list[i].widget == "textscroll") {
                    const Widget *w = widget("textscroll");
                    const int row = (ev.mouseY - w->y - 4) / kRow - 1;
                    if (row >= 0 && size_t(row) < sessionList().size())
                        _chosenSession = size_t(row);
                } else if (!list[i].text) {
                    const Widget *w = widget(list[i].widget);
                    change(list[i], w && ev.mouseX < w->x + originX() + w->w / 2 ? -1 : 1);
                }
                _sys.setTextInput(typing());
                return action;
            }
        return action;
    }
    if (ev.type != EventType::kKeyDown)
        return action;
    switch (ev.key) {
    case key::kUp:
    case key::kDown: {
        const int step = ev.key == key::kDown ? 1 : -1;
        for (size_t tries = 0; tries < list.size(); ++tries) {
            _focus = (_focus + step + int(list.size())) % int(list.size());
            if (list[size_t(_focus)].enabled)
                break;
        }
        break;
    }
    case key::kLeft:
    case key::kRight:
        if (!item.button && !item.text)
            change(item, ev.key == key::kRight ? 1 : -1);
        break;
    case key::kBackspace:
        if (std::string *t = item.text ? textOf(item.widget) : nullptr; t && !t->empty())
            t->pop_back();
        break;
    case key::kReturn:
    case key::kKpEnter:
        if (item.button)
            activate(item, action);
        else if (item.widget == "Chat Entry") {
            if (_session && !_chat.empty())
                _session->say(_chat);
            _chat.clear();
        } else if (item.widget == "textscroll")
            activate(list[7], action); // join the one chosen
        else if (_screen == Screen::kAddress || _screen == Screen::kPassword || _screen == Screen::kHostOptions ||
                 _screen == Screen::kEject)
            activate(list.back(), action); // done
        else if (_screen == Screen::kHostRace)
            activate(list.back(), action); // done
        break;
    case key::kEscape:
        switch (_screen) {
        case Screen::kSessions:
            leave();
            action = Action::kBack;
            break;
        case Screen::kAddress:
        case Screen::kPassword:
        case Screen::kHostOptions:
            go(Screen::kSessions);
            break;
        case Screen::kHostRace:
            go(_editing ? Screen::kLobby : Screen::kHostOptions);
            break;
        case Screen::kEject:
            go(Screen::kLobby);
            break;
        case Screen::kLobby:
            leave();
            go(Screen::kSessions);
            break;
        }
        break;
    default:
        break;
    }
    _sys.setTextInput(typing());
    return action;
}

NetMenus::Action NetMenus::update(uint64_t nowMs) {
    _nowMs = nowMs;
    Action action = Action::kNone;
    if (_browser && !_session)
        _browser->update(nowMs);
    // The meeting server's sessions, asked for every two seconds while
    // choosing (not while its address is being typed).
    const auto list = items();
    const bool typingServer =
        _focus >= 0 && size_t(_focus) < list.size() && list[size_t(_focus)].widget == "MEETING SERVER";
    if (!_session && _screen == Screen::kSessions && !typingServer) {
        net::Address meeting;
        if (!meetingServer(meeting)) {
            _meetingBrowser.reset();
        } else {
            if (!_meetingBrowser) {
                std::unique_ptr<net::UdpSocket> socket;
                if (!_sys.openUdp(0, socket) && socket)
                    _meetingBrowser = std::make_unique<net::MeetingSocket>(std::move(socket), meeting, "age");
                _meetingAskedMs = 0;
            }
            if (_meetingBrowser && (!_meetingAskedMs || nowMs >= _meetingAskedMs + 2000)) {
                _meetingAskedMs = nowMs;
                _meetingBrowser->requestList();
            }
            net::Address from;
            std::vector<uint8_t> data;
            while (_meetingBrowser && _meetingBrowser->receive(from, data)) {}
        }
    }
    if (!_session)
        return action;
    _session->update(nowMs);
    // Listed at the meeting server while hosting, with how the session
    // stands; and whether it answered, said once.
    if (_meetingHost && _session->isHost()) {
        _meetingHost->publish(_session->sessionName(), _session->info());
        if (!_meetingSaid && _meetingHost->published()) {
            _meetingSaid = true;
            say("Listed at the meeting server " + _meetingServer + ": players there can join", true);
        } else if (!_meetingSaid && !_meetingHost->heardFromServerMs()) {
            if (!_meetingSinceMs)
                _meetingSinceMs = nowMs;
            else if (nowMs > _meetingSinceMs + 6000) {
                _meetingSaid = true;
                say("The meeting server " + _meetingServer + " isn't answering", true);
            }
        }
    }
    // Hosting on the network (from the start, or taken over): the router
    // asked to pass the port on, for players on the Internet.
    if (_session->isHost() && !_local && !_mapper) {
        _mapper = std::make_unique<net::PortMapper>(net::PortMapper::through(_sys), kNetPort);
        _mapperSaid = false;
    }
    if (_mapper && _mapper->done() && !_mapperSaid) {
        _mapperSaid = true;
        const net::PortMapping m = _mapper->result();
        if (m.way == net::PortMapping::Way::kNone)
            say(m.why + ": for players on the Internet, pass UDP port " + std::to_string(kNetPort) +
                    " on to this computer by hand",
                true);
        else
            say(std::string("The router passes the port on (") +
                    (m.way == net::PortMapping::Way::kUpnp ? "UPnP" : "NAT-PMP") + "): players on the Internet join " +
                    (m.externalIp ? net::Address{m.externalIp, m.externalPort}.toString()
                                  : "this computer's Internet address"),
                true);
    }
    for (const NetNotice &n : _session->takeNotices()) {
        say(noticeText(n), true);
        switch (n.kind) {
        case NetNotice::Kind::kStarted:
            action = Action::kLoad;
            break;
        case NetNotice::Kind::kBackToLobby:
            action = Action::kLobby;
            break;
        case NetNotice::Kind::kEjected:
            message({}, "boot_dlg");
            break;
        case NetNotice::Kind::kRefused:
            if (n.reason == kRefusedPassword) {
                message({}, "badp_dlg"); // the game's "Incorrect password"
                break;
            }
            message(noticeText(n));
            break;
        case NetNotice::Kind::kHostQuit:
            message(noticeText(n));
            break;
        default:
            break;
        }
    }
    for (const NetChatLine &c : _session->chat())
        if (c.seq > _lastChatSeq) {
            _lastChatSeq = c.seq;
            say(c.from + ": " + c.text, false);
        }
    if (_session->closed()) {
        _session.reset();
        _mapper.reset();
        go(Screen::kSessions);
        action = Action::kLobby;
    }
    return action;
}

void NetMenus::text(Surface &o, const std::string &name, const std::string &s, Color c) const {
    if (const Widget *w = widget(name))
        font::drawText(o, w->x + originX() + 6,
                       w->y + originY() + std::max(0, (std::min(w->h, 24) - font::kLineHeight) / 2), s, c);
}

void NetMenus::drawDialog(Surface &o, const std::string &picture, const std::string &title) const {
    if (const image::Image *im = _pictures.jpg(picture))
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

void NetMenus::drawHostRace(Surface &o) const {
    const std::string city = _cities.empty() ? "sf" : cityPicture(_cities[_settings.city].map);
    // The race's map, else the city's.
    std::string map = city == "london" ? "mapr_lon" : "mapr_sf";
    if (hasRace(_settings.mode)) {
        const RaceKind k = raceKind(_settings.mode);
        map = city + "_map" +
              (k == RaceKind::kBlitz     ? "blitz"
               : k == RaceKind::kCircuit ? "circuit"
                                         : "race") +
              std::to_string(_settings.race);
    }
    if (const image::Image *m = _pictures.jpg(map))
        if (const Widget *w = widget("race map"))
            drawPicture(o, *m, w->x, w->y, w->w, w->h);
    if (hasRace(_settings.mode))
        text(o, "RACE NAME",
             _raceNames.empty() ? "" : _raceNames[std::min<size_t>(_settings.race, _raceNames.size() - 1)]);
    if (_settings.mode == NetMode::kCircuit)
        text(o, "LAPS", std::to_string(_settings.laps));
    if (_settings.mode == NetMode::kCopsRobbers) {
        if (_settings.limit != GoldLimit::kNone)
            text(o, "LIMIT VALUE",
                 _settings.limit == GoldLimit::kTime ? timeLimitName(_settings.limitValue)
                                                     : std::to_string(_settings.limitValue) + " pts");
        text(o, "GOLD MASS", goldMassName(_settings.goldMass));
    }
    text(o, "RACE LOCALE", _cities.empty() ? "" : _cities[_settings.city].name);
    text(o, "TIME", kTimes[_settings.timeOfDay & 3]);
    text(o, "WEATHER", kWeathers[_settings.weather & 3]);
    if (const Widget *w = widget("PEDESTRIAN DENSITY")) {
        // Over the background's grid of cells.
        const Box b{w->x + 8, w->y + 2, int(float(w->w - 26) * _settings.peds), w->h - 10};
        fillBox(o, b, {255, 200, 40});
    }
    static const char *const kDesc[] = {"race_hrm", "race_hbt", "race_hcp", "race_hct", "race_cop"};
    if (const image::Image *d = _pictures.jpg(kDesc[int(_settings.mode)]))
        drawPicture(o, *d, 40, 396);
}

void NetMenus::drawLobby(Surface &o) const {
    if (!_session)
        return;
    const NetSettings &s = _session->settings();
    const std::string city = s.city < _cities.size() ? _cities[s.city].name : "";
    // The host's settings.
    std::vector<std::string> lines = {_session->sessionName(), modeName(s.mode)};
    if (hasRace(s.mode)) {
        const auto names = s.city < _cities.size() ? raceNames(_files, _cities[s.city].map, raceKind(s.mode))
                                                   : std::vector<std::string>{};
        lines.push_back(s.race < names.size() ? names[s.race] : "");
        if (s.mode == NetMode::kCircuit)
            lines.push_back(std::to_string(s.laps) + " laps");
    }
    if (s.mode == NetMode::kCopsRobbers) {
        lines.push_back(goldModeName(s.gold));
        lines.push_back(s.limit == GoldLimit::kNone   ? std::string("No limit")
                        : s.limit == GoldLimit::kTime ? timeLimitName(s.limitValue)
                                                      : std::to_string(s.limitValue) + " points");
        lines.push_back(_strings.get(335, "Gold Mass") + ": " + goldMassName(s.goldMass));
    }
    lines.push_back(_strings.get(87, "Locale:") + " " + city);
    lines.push_back(std::string(kTimes[s.timeOfDay & 3]) + ", " + kWeathers[s.weather & 3]);
    for (size_t i = 0; i < lines.size() && i < 8; ++i)
        font::drawText(o, kSettingsBox.x + 4, kSettingsBox.y + 2 + int(i) * kRow, lines[i],
                       i == 0 ? Color{255, 230, 140} : Color{220, 220, 255});
    // The players.
    if (const Widget *w = widget("compscroll")) {
        int y = w->y + 4;
        font::drawText(o, w->x + 6, y,
                       s.teams() ? "Player          Car                 Team      Ready  Ping"
                                 : "Player          Car                           Ready  Ping",
                       {150, 200, 255});
        y += kRow + 2;
        for (const NetPlayer &p : _session->players()) {
            std::string car = carFor(p);
            for (const CarInfo &c : _cars)
                if (c.base == car)
                    car = c.description;
            // The host marked with a star; it's ready when it starts.
            const std::string name = p.name + (p.id == 0 ? "*" : "");
            const std::string ready = p.ready || p.id == 0 ? "yes" : "-";
            char row[160];
            if (s.teams())
                std::snprintf(row, sizeof row, "%-15.15s %-19.19s %-9.9s %-6s %d", name.c_str(), car.c_str(),
                              teamName(p.team).c_str(), ready.c_str(), int(p.ping));
            else
                std::snprintf(row, sizeof row, "%-15.15s %-29.29s %-6s %d", name.c_str(), car.c_str(), ready.c_str(),
                              int(p.ping));
            font::drawText(o, w->x + 6, y, row, p.id == _session->myId() ? Color{255, 255, 120} : Color{230, 230, 230});
            y += kRow;
        }
    }
    // You.
    if (const NetPlayer *me = _session->me()) {
        std::string car = carFor(*me);
        for (const CarInfo &c : _cars)
            if (c.base == car)
                car = c.description;
        font::drawText(o, kYouBox.x, kYouBox.y + 2, me->name, {255, 255, 120});
        font::drawText(o, kYouBox.x, kYouBox.y + 2 + kRow, car, {230, 230, 230});
        if (s.teams())
            font::drawText(o, kYouBox.x, kYouBox.y + 2 + 2 * kRow, teamName(me->team), {255, 200, 120});
    }
    if (s.teams())
        if (const image::Image *label = _pictures.jpg("lobb_tem"))
            drawPicture(o, *label, 474, 190);
    // The chat (and who's come and gone).
    const int shown = kChatBox.h / kRow;
    int y = kChatBox.y;
    for (size_t i = _lines.size() > size_t(shown) ? _lines.size() - size_t(shown) : 0; i < _lines.size(); ++i) {
        font::drawText(o, kChatBox.x, y, _lines[i].text.substr(0, 58),
                       _lines[i].system ? Color{255, 200, 120} : Color{230, 230, 230});
        y += kRow;
    }
    // The map: the race's, else the city's.
    const std::string pic = s.city < _cities.size() ? cityPicture(_cities[s.city].map) : "sf";
    std::string map = pic == "london" ? "mapr_lon" : "mapr_sf";
    if (hasRace(s.mode)) {
        const RaceKind k = raceKind(s.mode);
        map = pic + "_map" +
              (k == RaceKind::kBlitz     ? "blitz"
               : k == RaceKind::kCircuit ? "circuit"
                                         : "race") +
              std::to_string(s.race);
    }
    if (const image::Image *m = _pictures.jpg(map))
        if (const Widget *w = widget("race map"))
            drawPicture(o, *m, w->x, w->y, w->w, w->h);
    // The description: the host settings button's, else the event's.
    const auto list = items();
    const std::string focused = size_t(_focus) < list.size() ? list[size_t(_focus)].widget : "";
    static const char *const kDesc[] = {"race_hrm", "race_hbt", "race_hcp", "race_hct", "race_cop"};
    if (const image::Image *d = _pictures.jpg(focused == "lobb_hst" ? "lobb_set" : kDesc[int(s.mode)]))
        drawPicture(o, *d, 40, 396);
    // Waiting for the players to be ready (the game's words).
    if (_session->isHost()) {
        int waiting = 0;
        for (const NetPlayer &p : _session->players())
            waiting += p.id != _session->myId() && !p.ready ? 1 : 0;
        if (waiting > 0)
            font::drawText(o, kHeadingBox.x + 6, kHeadingBox.y + 6,
                           _strings.get(uint32_t(30 + std::min(waiting, 7)), "Waiting for players"), {255, 120, 120});
    } else if (!_session->joining()) {
        font::drawText(o, kHeadingBox.x + 6, kHeadingBox.y + 6, _strings.get(432, "waiting for host to start..."),
                       {150, 200, 255});
    }
}

void NetMenus::draw(Surface &o, uint64_t nowMs) {
    _nowMs = nowMs;
    std::fill(o.pixels.begin(), o.pixels.end(), 0);
    // The screen under a dialog.
    const Screen under = _screen == Screen::kAddress || _screen == Screen::kPassword || _screen == Screen::kHostOptions
                             ? Screen::kSessions
                         : _screen == Screen::kEject ? Screen::kLobby
                                                     : _screen;
    const bool host = _session && _session->isHost();
    const char *background = under == Screen::kSessions   ? "sess_bk"
                             : under == Screen::kHostRace ? "host_bk"
                             : host                       ? "lobbh_bk"
                                                          : "lobbj_bk";
    if (const image::Image *bg = _pictures.jpg(background))
        drawPicture(o, *bg, 0, 0, o.w, o.h);
    // Cops & Robbers' choices, on their own panel, under their buttons.
    if (under == Screen::kHostRace && _settings.mode == NetMode::kCopsRobbers)
        if (const image::Image *panel = _pictures.jpg("host_cr"))
            drawPicture(o, *panel, kCopsPanel.x, kCopsPanel.y);

    const Screen was = _screen;
    const int keepFocus = _focus;
    if (under != _screen) {
        // Draw the screen beneath as it is, without a focus.
        _screen = under;
        _focus = -1;
    }
    for (int pass = 0; pass < (under != was ? 2 : 1); ++pass) {
        if (pass == 1) {
            _screen = was;
            _focus = keepFocus;
            if (was == Screen::kEject)
                drawDialog(o, "ejct_dlg", "EJECT PLAYER");
            else if (was == Screen::kHostOptions)
                drawDialog(o, "host_dlg", "HOST OPTIONS");
            else if (was == Screen::kPassword)
                drawDialog(o, "pass_dlg", "PASSWORD");
            else
                drawDialog(o, "tcp_dlg", "ENTER AN ADDRESS");
        }
        const auto list = items();
        for (size_t i = 0; i < list.size(); ++i) {
            const Item &item = list[i];
            const Widget *w = widget(item.widget);
            if (!w)
                continue;
            const bool focused = int(i) == _focus;
            if (item.button) {
                const image::Image *im = _pictures.texture(item.picture);
                if (!im)
                    continue;
                bool chosen = item.widget == "sess_tcp";
                if (_screen == Screen::kHostRace) {
                    static const char *const kModes[] = {"cruise_m", "blitz_m", "cp_m", "circt_m", "cops_m"};
                    static const char *const kGold[] = {"free_cr", "cpsvr_cr", "robrs_cr"};
                    static const char *const kLimits[] = {"none_cr", "time_cr", "point_cr"};
                    chosen = item.widget == kModes[int(_settings.mode)] || item.widget == kGold[int(_settings.gold)] ||
                             item.widget == kLimits[int(_settings.limit)];
                }
                if (_screen == Screen::kLobby && _session && _session->me())
                    chosen = (item.widget == "lobb_red" && _session->me()->team == 0) ||
                             (item.widget == "lobb_blu" && _session->me()->team == 1);
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
        switch (_screen) {
        case Screen::kSessions: {
            text(o, "NET NAME", _netName + (_focus == 5 && (nowMs / 400) % 2 ? "_" : ""));
            font::drawText(o, kHeadingBox.x + 6, kHeadingBox.y + 6, "TCP/IP", {255, 230, 140});
            if (const Widget *w = widget("textscroll")) {
                const auto found = sessionList();
                int y = w->y + 4;
                font::drawText(o, w->x + 6, y, "Session          Players  Event           Ping", {150, 200, 255});
                y += kRow;
                if (found.empty())
                    font::drawText(o, w->x + 6, y + 4, "None on this network yet. JOIN to give an address.",
                                   {200, 200, 200});
                for (size_t i = 0; i < found.size() && i < 8; ++i) {
                    const auto &f = found[i];
                    char row[160];
                    // A meeting server's sessions have no ping: what it is
                    // depends on which way the datagrams end up going.
                    const std::string ping = f.meetingId ? "via server" : std::to_string(f.ping);
                    std::snprintf(row, sizeof row, "%-16.16s %d/%d      %-15.15s %s%s%s", f.name.c_str(), f.players,
                                  f.maxPlayers, modeName(f.mode).c_str(), ping.c_str(), f.playing ? " (playing)" : "",
                                  f.password       ? " (password)"
                                  : !f.sameVersion ? " (other version)"
                                                   : "");
                    font::drawText(o, w->x + 6, y, row,
                                   i == _chosenSession ? Color{255, 255, 120} : Color{230, 230, 230});
                    y += kRow;
                }
            }
            // The meeting server: Reluva's field, in the Net Name box's
            // colours, with a word on what it's for and how it answers.
            if (const Widget *w = widget("MEETING SERVER")) {
                const bool focused = _focus == 6;
                font::drawText(o, w->x, w->y - 16, "MEETING SERVER", {255, 255, 255});
                for (int y = w->y; y < w->y + w->h; ++y)
                    for (int x = w->x; x < w->x + w->w; ++x) {
                        const bool edge = x == w->x || y == w->y || x == w->x + w->w - 1 || y == w->y + w->h - 1;
                        uint8_t *p = &o.pixels[size_t(y) * size_t(o.pitch) + size_t(x) * 4];
                        p[0] = edge ? (focused ? 255 : 82) : 8;
                        p[1] = edge ? (focused ? 255 : 80) : 2;
                        p[2] = edge ? (focused ? 120 : 163) : 46;
                        p[3] = 255;
                    }
                text(o, "MEETING SERVER", _meetingServer + (focused && (nowMs / 400) % 2 ? "_" : ""));
                std::string state = "When players can't reach each other";
                if (!_meetingServer.empty() && !focused) {
                    if (!_meetingAt.port)
                        state = "Can't find that server";
                    else if (!_meetingBrowser || !_meetingBrowser->heardFromServerMs())
                        state = "Asking the server...";
                    else
                        state = std::to_string(_meetingBrowser->listed().size()) + " session(s) there";
                }
                font::drawText(o, w->x, w->y + w->h + 5, state, {20, 0, 64});
            }
            if (const image::Image *d = _pictures.jpg("mpst_tcp"))
                drawPicture(o, *d, 40, 396);
            break;
        }
        case Screen::kAddress:
            // The game's own words are on jpg/tcp_dlg; the port's Reluva's.
            font::drawText(o, kDialogX + 72, kDialogY + 118, "Port " + std::to_string(kNetPort) + " unless given",
                           {200, 200, 255});
            text(o, "TCP/IP Address", _address + ((nowMs / 400) % 2 && _focus == 0 ? "_" : ""));
            break;
        case Screen::kPassword:
        case Screen::kHostOptions: {
            std::string stars(_password.size(), '*');
            text(o, "Password", stars + ((nowMs / 400) % 2 && _focus == 0 ? "_" : ""));
            if (_screen == Screen::kHostOptions) {
                text(o, "Max Players", std::to_string(_settings.maxPlayers));
                if (const Widget *w = widget(kBotsField)) {
                    // Its label and box, as the dialog draws Max Players'.
                    const std::string label = "COMPUTER PLAYERS";
                    font::drawText(o, kDialogX + w->x - 12 - int(label.size()) * font::kAdvance, kDialogY + w->y + 10,
                                   label, {230, 230, 255});
                    fillBox(o, {kDialogX + w->x, kDialogY + w->y, w->w, w->h - 8}, {0, 0, 20});
                    text(o, kBotsField, std::to_string(_bots));
                }
            }
            break;
        }
        case Screen::kHostRace:
            drawHostRace(o);
            break;
        case Screen::kLobby:
            drawLobby(o);
            text(o, "Chat Entry", _chat + ((nowMs / 400) % 2 && _focus == 0 ? "_" : ""), {255, 255, 255});
            break;
        case Screen::kEject:
            if (_session) {
                int y = kEjectList.y;
                size_t k = 0;
                for (const NetPlayer &p : _session->players()) {
                    if (p.id == _session->myId())
                        continue;
                    font::drawText(o, kEjectList.x, y, p.name,
                                   k == _ejectChoice ? Color{255, 255, 120} : Color{230, 230, 230});
                    y += kRow;
                    ++k;
                }
            }
            break;
        }
    }
    _screen = was;
    _focus = keepFocus;

    // Joining: "Retrieving game settings..."
    if (_session && _session->joining())
        if (const image::Image *d = _pictures.jpg("rtrv_dlg"))
            drawPicture(o, *d, kDialogX, kMessageY);
    if (!_messagePicture.empty()) {
        if (const image::Image *d = _pictures.jpg(_messagePicture))
            drawPicture(o, *d, kDialogX, kMessageY);
        if (!_message.empty())
            font::drawText(o, kDialogX + 80, kMessageY + 30, _message.substr(0, 38), {255, 255, 255});
    }
    font::drawText(o, 4, o.h - 12, "Up/Down, Left/Right, Enter, Esc; type in the boxes", {200, 200, 200});
}

} // namespace reluva::age
