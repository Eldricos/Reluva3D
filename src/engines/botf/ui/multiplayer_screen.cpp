// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/multiplayer_screen.h"

#include <set>

namespace reluva::botf {

namespace {

// mpsetup.wdf's widgets, matched to the manual's controls by their
// pictures' names (m_host, m_join, m_gmnm, m_plynm, m_pswrd ...).
constexpr int32_t kHost = 108;
constexpr int32_t kJoin = 109;
constexpr int32_t kCancel = 114;
constexpr int32_t kGameNameField = 702; // pictures, clicked to type in
constexpr int32_t kPlayerNameField = 703;
constexpr int32_t kPasswordField = 701;
constexpr int32_t kGameNameText = 112;
constexpr int32_t kPlayerNameText = 113;
constexpr int32_t kPasswordText = 700;
// The empty pictures the panels go in: the connection's type beside Host
// and Join, the connection's settings to the right (each the size of its
// panel).
constexpr int32_t kConnectPlace = 399;
constexpr int32_t kSettingsPlace = 200;
// mpconn.wdf
constexpr int32_t kNetwork = 193;
constexpr int32_t kModem = 194;
constexpr int32_t kSerial = 196;
// mpcnet.wdf
constexpr int32_t kTcpIp = 202;
constexpr int32_t kIpx = 203;
constexpr int32_t kHostAddressField = 207; // a button, clicked to type in
constexpr int32_t kHostAddressText = 714;
constexpr int32_t kNetworkConnect = 210;
// mpcmod.wdf and mpcser.wdf's Connect, and the serial panel's baud rates
// (14,400 to 115,200) and ports (COM1-COM4).
constexpr int32_t kModemConnect = 491;
constexpr int32_t kSerialConnect = 451;
const std::set<int32_t> kBauds = {442, 443, 444, 445, 446};
const std::set<int32_t> kPorts = {447, 448, 449, 450};

// The manual: "a password, no longer than 13 characters". The names' only
// limit known is their field's width.
constexpr size_t kMaxPassword = 13;
constexpr size_t kMaxName = 64;

// The fields' text: Load Game's colour, taken for the same game's fields
// (not seen with text yet).
constexpr Color kTextColor{0, 166, 156};

} // namespace

Error MultiplayerScreen::load() {
    if (Error err = loadLayouts("mpsetup.wdf", {kHost, kJoin, kCancel, kGameNameField, kPlayerNameField, kPasswordField,
                                                kGameNameText, kPlayerNameText, kPasswordText}))
        return err;
    if (Error err = addPanel("mpconn.wdf", kConnectPlace, {kNetwork, kModem, kSerial}, _connectPanel))
        return err;
    if (Error err =
            addPanel("mpcnet.wdf", kSettingsPlace, {kTcpIp, kIpx, kHostAddressField, kNetworkConnect}, _networkPanel))
        return err;
    if (Error err = addPanel("mpcmod.wdf", kSettingsPlace, {kModemConnect}, _modemPanel))
        return err;
    return addPanel("mpcser.wdf", kSettingsPlace, {kSerialConnect}, _serialPanel);
}

void MultiplayerScreen::open() {
    _role = Role::kNone;
    _connection = Connection::kNone;
    _field = Field::kNone;
    _pressed = 0;
    for (int panel : {_connectPanel, _networkPanel, _modemPanel, _serialPanel})
        showPanel(panel, false);
}

const std::string &MultiplayerScreen::text(Field field) const {
    static const std::string kEmpty;
    switch (field) {
    case Field::kGameName:
        return _gameName;
    case Field::kPlayerName:
        return _playerName;
    case Field::kPassword:
        return _password;
    case Field::kHostAddress:
        return _hostAddress;
    default:
        return kEmpty;
    }
}

std::string *MultiplayerScreen::edited() {
    switch (_field) {
    case Field::kGameName:
        return &_gameName;
    case Field::kPlayerName:
        return &_playerName;
    case Field::kPassword:
        return &_password;
    case Field::kHostAddress:
        return &_hostAddress;
    default:
        return nullptr;
    }
}

void MultiplayerScreen::click(int32_t id) {
    // Host or Join, then the connection's type: each shows what comes next
    // (inferred: the panels' places are empty on the screen at first).
    if (id == kHost || id == kJoin) {
        _role = id == kHost ? Role::kHost : Role::kJoin;
        showPanel(_connectPanel, true);
    } else if (id == kNetwork || id == kModem || id == kSerial) {
        _connection = id == kNetwork ? Connection::kNetwork : id == kModem ? Connection::kModem : Connection::kSerial;
        showPanel(_networkPanel, id == kNetwork);
        showPanel(_modemPanel, id == kModem);
        showPanel(_serialPanel, id == kSerial);
        if (_field == Field::kHostAddress && id != kNetwork)
            _field = Field::kNone;
    } else if (id == kTcpIp || id == kIpx) {
        _ipx = id == kIpx;
    } else if (id == kHostAddressField) {
        _field = Field::kHostAddress;
    } else if (kBauds.count(id)) {
        _baud = id;
    } else if (kPorts.count(id)) {
        _port = id;
    }
    // Connect: there's no one to connect to yet.
}

MultiplayerScreen::Result MultiplayerScreen::handle(const Event &e) {
    if (e.type == EventType::kKeyDown && e.key == key::kEscape)
        return Result::kCancelled;
    if (e.type == EventType::kKeyDown && e.key == key::kTab) {
        // The manual: "You can move from field to field by clicking on a new
        // field or pressing your Tab key", among the three under Cancel.
        _field = _field == Field::kGameName     ? Field::kPlayerName
                 : _field == Field::kPlayerName ? Field::kPassword
                                                : Field::kGameName;
        return Result::kOpen;
    }
    if (std::string *t = edited()) {
        const int32_t box = _field == Field::kGameName     ? kGameNameText
                            : _field == Field::kPlayerName ? kPlayerNameText
                            : _field == Field::kPassword   ? kPasswordText
                                                           : kHostAddressText;
        if (e.type == EventType::kTextInput) {
            typeInto(*t, box, e.text, _field == Field::kPassword ? kMaxPassword : kMaxName);
            return Result::kOpen;
        }
        if (e.type == EventType::kKeyDown && e.key == key::kBackspace) {
            eraseLast(*t);
            return Result::kOpen;
        }
    }
    if (e.button != MouseButton::kLeft)
        return Result::kOpen;
    if (e.type == EventType::kMouseDown) {
        _pressed = buttonAt(e.mouseX, e.mouseY);
        const std::pair<int32_t, Field> fields[] = {{kGameNameField, Field::kGameName},
                                                    {kPlayerNameField, Field::kPlayerName},
                                                    {kPasswordField, Field::kPassword}};
        for (const auto &[id, field] : fields)
            if (const Placed *p = find(id); p && p->contains(e.mouseX, e.mouseY))
                _field = field;
    } else if (e.type == EventType::kMouseUp) {
        // A button acts when the mouse comes up on it, as Windows buttons do.
        const int32_t id = _pressed && buttonAt(e.mouseX, e.mouseY) == _pressed ? _pressed : 0;
        _pressed = 0;
        if (id == kCancel)
            return Result::kCancelled;
        if (id)
            click(id);
    }
    return Result::kOpen;
}

void MultiplayerScreen::draw(Surface &screen) const {
    // The choices made show their buttons' second pictures, as kind 2
    // buttons do on the empire selection screen.
    std::set<int32_t> lit;
    if (_role != Role::kNone)
        lit.insert(_role == Role::kHost ? kHost : kJoin);
    if (_connection != Connection::kNone)
        lit.insert(_connection == Connection::kNetwork ? kNetwork
                   : _connection == Connection::kModem ? kModem
                                                       : kSerial);
    if (_connection == Connection::kNetwork)
        lit.insert(_ipx ? kIpx : kTcpIp);
    if (_connection == Connection::kNetwork && _field == Field::kHostAddress)
        lit.insert(kHostAddressField);
    if (_connection == Connection::kSerial) {
        if (_baud)
            lit.insert(_baud);
        if (_port)
            lit.insert(_port);
    }
    drawLayouts(screen, lit);
    const std::pair<int32_t, Field> boxes[] = {{kGameNameText, Field::kGameName},
                                               {kPlayerNameText, Field::kPlayerName},
                                               {kPasswordText, Field::kPassword},
                                               {kHostAddressText, Field::kHostAddress}};
    for (const auto &[box, field] : boxes) {
        if (field == Field::kHostAddress && !panelShown(_networkPanel))
            continue;
        drawText(screen, box, text(field), kTextColor);
        if (field == _field && _cursorShown)
            drawCursor(screen, box, text(field));
    }
}

} // namespace reluva::botf
