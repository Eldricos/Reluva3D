// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/netplay.h"

#include <algorithm>
#include <random>

namespace reluva::stawayteam::netplay {

namespace {

// Every datagram starts with these, then its type; the rest is the
// type's. Numbers are little-endian, strings a byte's length and then
// their bytes.
constexpr uint8_t kMagic[4] = {'R', 'S', 'A', 'T'};
enum Type : uint8_t {
    kAsk = 1, // is there a game?
    kHere,    // players, open, the game's name
    kJoin,    // nonce, name
    kWelcome, // nonce
    kRefuse,  // nonce
    kState,   // the lobby: see sendState()
    kChat,    // the first line's number, how many, and the lines
    kBye,     // gone
};
// The host sends the chat's last lines each time: the player takes those
// it hasn't had. A line lost is only lost if more than this many follow it
// before the next state arrives.
constexpr size_t kLinesSent = 16;

class Writer {
public:
    explicit Writer(Type type) : _data(std::begin(kMagic), std::end(kMagic)) { _data.push_back(type); }
    void u8(uint32_t v) { _data.push_back(uint8_t(v)); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i)
            _data.push_back(uint8_t(v >> (8 * i)));
    }
    void text(const std::string &s) {
        const size_t n = std::min<size_t>(s.size(), 255);
        u8(uint32_t(n));
        _data.insert(_data.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
    }
    const std::vector<uint8_t> &data() const { return _data; }

private:
    std::vector<uint8_t> _data;
};

class Reader {
public:
    explicit Reader(const std::vector<uint8_t> &data) : _data(data), _at(5) {}
    bool ok() const { return _ok; }
    uint32_t u8() {
        if (_at + 1 > _data.size()) {
            _ok = false;
            return 0;
        }
        return _data[_at++];
    }
    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= u8() << (8 * i);
        return v;
    }
    std::string text() {
        const size_t n = u8();
        if (!_ok || _at + n > _data.size()) {
            _ok = false;
            return {};
        }
        std::string s(_data.begin() + std::ptrdiff_t(_at), _data.begin() + std::ptrdiff_t(_at + n));
        _at += n;
        return s;
    }

private:
    const std::vector<uint8_t> &_data;
    size_t _at;
    bool _ok = true;
};

// The program names the game for the one who made it (0x511eb7).
std::string gameName(const std::string &host) {
    return host + "'s Game";
}

} // namespace

std::unique_ptr<Session> Session::host(std::unique_ptr<net::UdpSocket> socket, const std::string &playerName) {
    std::unique_ptr<Session> s(new Session);
    s->_socket = std::move(socket);
    s->_state = State::kLobby;
    s->_host = true;
    s->_players = {playerName};
    return s;
}

std::unique_ptr<Session> Session::join(std::unique_ptr<net::UdpSocket> socket, const net::Address &address,
                                       const std::string &playerName) {
    std::unique_ptr<Session> s(new Session);
    s->_socket = std::move(socket);
    s->_state = State::kConnecting;
    s->_peer = address;
    s->_hasPeer = true;
    s->_players = {playerName};
    s->_nonce = std::random_device{}() | 1u;
    return s;
}

std::unique_ptr<Session> Session::browse(std::unique_ptr<net::UdpSocket> socket, std::vector<net::Address> askAt) {
    std::unique_ptr<Session> s(new Session);
    s->_socket = std::move(socket);
    s->_askAt = std::move(askAt);
    s->refresh();
    return s;
}

void Session::refresh() {
    _found.clear();
    _asksLeft = 3;
    _lastSent = 0;
}

void Session::chooseMission(int number) {
    if (!_host || number == _mission)
        return;
    // A countdown running goes on, to this mission (0x514060).
    _mission = number;
    _changed = true;
}

void Session::chat(const std::string &text) {
    if (text.empty() || (_state != State::kLobby && _state != State::kStarted))
        return;
    const std::string line = text.substr(0, kMostChat);
    if (_host) {
        _log.push_back({0, line});
        _changed = true;
    } else {
        _toSend.push_back(line);
        _lastSent = 0;
    }
}

bool Session::nextChat(ChatLine &line) {
    if (_host && _linesHad < _log.size())
        _inbox.push_back(_log[_linesHad++]);
    if (_inbox.empty())
        return false;
    line = std::move(_inbox.front());
    _inbox.pop_front();
    return true;
}

void Session::start() {
    if (!_host || _mission == 0 || _state != State::kLobby || counting())
        return;
    _countdownEnd = _now + kCountdownMs;
    _changed = true;
}

void Session::leave() {
    if (_socket && _hasPeer)
        _socket->send(_peer, Writer(kBye).data());
    _socket.reset();
    _state = State::kLost;
}

void Session::sendState() {
    // Players, the mission, whether it's started and the countdown's ms
    // left (0: none), how many of the player's lines it has taken, and the
    // chat's last lines.
    Writer w(kState);
    w.u8(uint32_t(_players.size()));
    for (const std::string &name : _players)
        w.text(name);
    w.u8(uint32_t(_mission));
    w.u8(_state == State::kStarted ? 1 : 0);
    w.u32(uint32_t(countdownLeft()));
    w.u32(_peerSent);
    const size_t first = _log.size() > kLinesSent ? _log.size() - kLinesSent : 0;
    w.u32(uint32_t(first));
    w.u8(uint32_t(_log.size() - first));
    for (size_t i = first; i < _log.size(); ++i) {
        w.u8(uint32_t(_log[i].from));
        w.text(_log[i].text);
    }
    _socket->send(_peer, w.data());
}

void Session::received(const net::Address &from, const std::vector<uint8_t> &data) {
    if (data.size() < 5 || !std::equal(std::begin(kMagic), std::end(kMagic), data.begin()))
        return;
    Reader r(data);
    const uint8_t type = data[4];
    const bool fromPeer = _hasPeer && from == _peer;
    if (_state == State::kBrowsing) {
        if (type != kHere)
            return;
        Found f;
        f.address = from;
        f.players = int(r.u8());
        f.open = r.u8() != 0;
        f.gameName = r.text();
        if (!r.ok())
            return;
        const auto it = std::find_if(_found.begin(), _found.end(), [&](const Found &o) { return o.address == from; });
        if (it == _found.end())
            _found.push_back(f);
        else
            *it = f;
        return;
    }
    if (_host) {
        const bool open = !_hasPeer && _state == State::kLobby && !counting();
        if (type == kAsk) {
            Writer w(kHere);
            w.u8(uint32_t(_players.size()));
            w.u8(open ? 1 : 0);
            w.text(gameName(_players[0]));
            _socket->send(from, w.data());
        } else if (type == kJoin) {
            const uint32_t nonce = r.u32();
            std::string name = r.text().substr(0, kMostName);
            if (!r.ok())
                return;
            // The same join asked again (the welcome lost) is welcomed again.
            if (!fromPeer && !open) {
                Writer w(kRefuse);
                w.u32(nonce);
                _socket->send(from, w.data());
                return;
            }
            if (!fromPeer) {
                _peer = from;
                _hasPeer = true;
                _peerSent = 0;
                _players.push_back(name.empty() ? "?" : name);
                _changed = true;
            }
            _heard = _now;
            Writer w(kWelcome);
            w.u32(nonce);
            _socket->send(from, w.data());
        } else if (fromPeer && type == kChat) {
            _heard = _now;
            const uint32_t first = r.u32();
            const uint32_t count = r.u8();
            for (uint32_t i = 0; i < count && r.ok(); ++i) {
                const std::string text = r.text().substr(0, kMostChat);
                if (r.ok() && first + i == _peerSent + 1) {
                    _log.push_back({1, text});
                    ++_peerSent;
                    _changed = true;
                }
            }
        } else if (fromPeer && type == kBye) {
            // The one who joined has gone: the game's open again, unless
            // counting, which goes on without them (0x51617a).
            _hasPeer = false;
            _players.resize(1);
            _changed = true;
        }
        return;
    }
    if (!fromPeer)
        return;
    _heard = _now;
    if (_state == State::kConnecting) {
        if ((type == kWelcome || type == kRefuse) && r.u32() == _nonce)
            _state = type == kWelcome ? State::kLobby : State::kRefused;
        return;
    }
    if (type == kBye) {
        _state = State::kLost;
        return;
    }
    if (type != kState)
        return;
    std::vector<std::string> players(r.u8());
    for (std::string &name : players)
        name = r.text();
    const int mission = int(r.u8());
    const bool started = r.u8() != 0;
    const uint32_t left = r.u32();
    const uint32_t taken = r.u32();
    const uint32_t first = r.u32();
    std::vector<ChatLine> lines(r.u8());
    for (ChatLine &line : lines) {
        line.from = int(r.u8());
        line.text = r.text();
    }
    if (!r.ok())
        return;
    _players = std::move(players);
    _me = 1;
    _mission = mission;
    if (started) {
        _countdownEnd = 0;
        _state = State::kStarted;
    } else if (_state == State::kLobby) {
        _countdownEnd = left ? _now + left : 0;
    }
    while (_taken < taken && !_toSend.empty()) {
        _toSend.pop_front();
        ++_taken;
    }
    for (size_t i = 0; i < lines.size(); ++i)
        if (first + i == _linesHad) {
            _inbox.push_back(std::move(lines[i]));
            ++_linesHad;
        }
}

void Session::update(uint64_t nowMs) {
    _now = nowMs;
    if (!_socket)
        return;
    net::Address from;
    std::vector<uint8_t> data;
    while (_socket && _socket->receive(from, data))
        received(from, data);
    if (!_socket)
        return;
    if (_state == State::kLobby && _countdownEnd && _now >= _countdownEnd) {
        _countdownEnd = 0;
        _state = State::kStarted;
    }
    if (_state == State::kBrowsing) {
        if (_asksLeft > 0 && (_lastSent == 0 || _now - _lastSent >= 500)) {
            for (const net::Address &at : _askAt)
                _socket->send(at, Writer(kAsk).data());
            _lastSent = _now;
            --_asksLeft;
        }
        return;
    }
    if (!_hasPeer)
        return;
    if (_heard == 0)
        _heard = _now;
    if (_state == State::kConnecting && _now - _heard > kJoinTimeoutMs) {
        _state = State::kRefused;
        return;
    }
    if (_now - _heard > kTimeoutMs) {
        if (_host) {
            _hasPeer = false;
            _players.resize(1);
        } else {
            _state = State::kLost;
        }
        return;
    }
    if (!_changed && _lastSent != 0 && _now - _lastSent < kSendMs)
        return;
    _changed = false;
    _lastSent = _now;
    if (_host) {
        sendState();
    } else if (_state == State::kConnecting) {
        Writer w(kJoin);
        w.u32(_nonce);
        w.text(_players[0]);
        _socket->send(_peer, w.data());
    } else {
        // The lines the host hasn't taken in yet, or none: it hears from
        // the player either way.
        Writer w(kChat);
        w.u32(_taken + 1);
        const size_t count = std::min<size_t>(_toSend.size(), 8);
        w.u8(uint32_t(count));
        for (size_t i = 0; i < count; ++i)
            w.text(_toSend[i]);
        _socket->send(_peer, w.data());
    }
}

} // namespace reluva::stawayteam::netplay
