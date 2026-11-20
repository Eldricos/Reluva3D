// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/ui/layout_screen.h"

#include <array>
#include <string>

// The Multiplayer Setup screen (mpsetup.wdf), opened by the main menu's New
// Multiplayer Game: Host or Join, the game's and the player's names and a
// password, and the connection: Network (mpcnet.wdf), Modem (mpcmod.wdf)
// or Serial (mpcser.wdf), chosen in mpconn.wdf. What each control does is
// the manual's (chapter 12); how it looks, the layouts' and a screenshot of
// the game's (docs/engines/botf/systems/screens.md). Reluva can't connect
// to anyone yet: Connect does nothing.

namespace reluva::botf {

/** The Multiplayer Setup screen. */
class MultiplayerScreen : LayoutScreen {
public:
    using LayoutScreen::setSoundHandler;
    enum class Result { kOpen, kCancelled };
    enum class Role { kNone, kHost, kJoin };
    enum class Connection { kNone, kNetwork, kModem, kSerial };
    /**
     * The fields that take typing: the three under Cancel, in the order
     * Tab goes through them, the network's host address, and the modem's
     * address book's rows.
     */
    enum class Field { kNone, kGameName, kPlayerName, kPassword, kHostAddress, kAddress };
    static constexpr int kAddressRows = 7;

    MultiplayerScreen(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    Error load(); // the layout, its panels and the main menu
    /** Back as at first: nothing chosen, the fields kept. */
    void open();
    void draw(Surface &screen) const;
    /**
     * Clicks on the buttons and fields, typed text (kTextInput), Backspace
     * and Tab for the field being typed in, and Escape for Cancel.
     */
    Result handle(const Event &event);

    Role role() const { return _role; }
    Connection connection() const { return _connection; }
    Field field() const { return _field; }
    /** The address book's row typed in (with Field::kAddress). */
    int addressRow() const { return _addressRow; }
    /** A field's text, UTF-8; for kAddress, `row`'s. */
    const std::string &text(Field field, int row = 0) const;
    /** Whether the flashing cursor is shown just now. */
    void setCursorShown(bool shown) { _cursorShown = shown; }

private:
    void click(int32_t id);
    std::string *edited();

    Role _role = Role::kNone;
    Connection _connection = Connection::kNone;
    bool _ipx = false; // the network's protocol: TCP/IP, or IPX
    Field _field = Field::kNone;
    std::string _gameName;
    std::string _playerName;
    std::string _password;
    std::string _hostAddress;
    std::array<std::string, kAddressRows> _addresses;
    int _addressRow = 0;
    int _connectPanel = -1; // mpconn.wdf
    int _networkPanel = -1;
    int _modemPanel = -1;
    int _serialPanel = -1;
    int32_t _baud = 0; // the serial panel's choices: their buttons, or 0
    int32_t _port = 0;
    bool _cursorShown = true;
};

} // namespace reluva::botf
