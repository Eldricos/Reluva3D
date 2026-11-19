// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/net_client.h"

#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/info_string.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

constexpr int32_t kSvcNop = 1;
constexpr int32_t kSvcGamestate = 2;
constexpr int32_t kSvcConfigstring = 3;
constexpr int32_t kSvcBaseline = 4;
constexpr int32_t kSvcServerCommand = 5;
constexpr int32_t kSvcDownload = 6;
constexpr int32_t kSvcSnapshot = 7;
constexpr uint32_t kClcMove = 2;
constexpr uint32_t kClcMoveNoDelta = 3;
constexpr uint32_t kClcClientCommand = 4;
constexpr uint64_t kResendMs = 3000;    // getchallenge and connect, until answered
constexpr uint64_t kWaitingMs = 1000;   // a packet a second while waiting for the level
constexpr uint64_t kTimeoutMs = 200000; // cl_timeout
constexpr int kMaxPackets = 43;         // Elite Force's cl_maxpackets
constexpr size_t kMaxPacketCommands = 32;

} // namespace

NetClient::NetClient(std::unique_ptr<reluva::net::UdpSocket> socket, const reluva::net::Address &server,
                     std::string userinfo, uint16_t qport)
    : _socket(std::move(socket)), _server(server), _userinfo(std::move(userinfo)), _qport(qport), _chan(true, qport) {
}

NetClient::NetClient(std::vector<uint8_t> demo)
    : _qport(0), _state(State::kConnected), _chan(true, 0), _demo(true), _demoData(std::move(demo)) {
}

void NetClient::sendText(const std::string &text) {
    if (!_socket)
        return;
    std::vector<uint8_t> d(4, 0xff);
    d.insert(d.end(), text.begin(), text.end());
    _socket->send(_server, d);
}

void NetClient::frame(uint64_t now) {
    if (_demo) {
        readDemo(now);
        return;
    }
    reluva::net::Address from;
    std::vector<uint8_t> d;
    while (_socket->receive(from, d)) {
        if (from != _server)
            continue;
        if (d.size() >= 4 && d[0] == 0xff && d[1] == 0xff && d[2] == 0xff && d[3] == 0xff) {
            outOfBand(std::string(d.begin() + 4, d.end()), now);
            continue;
        }
        std::vector<uint8_t> message;
        if (_state < State::kConnected || !_chan.process(std::move(d), message))
            continue;
        _lastReceived = now;
        _incoming = _chan.incomingSequence();
        wire::MessageReader r(std::move(message));
        parseMessage(r, now);
    }
    switch (_state) {
    case State::kDisconnected:
        return;
    case State::kConnecting:
        if (!_lastSent || now - _lastSent >= kResendMs) {
            _lastSent = now;
            ++_connectPackets;
            sendText("getchallenge");
        }
        return;
    case State::kChallenging:
        if (!_lastSent || now - _lastSent >= kResendMs) {
            _lastSent = now;
            ++_connectPackets;
            std::string info = _userinfo;
            setInfoValue(info, "protocol", "24");
            setInfoValue(info, "qport", std::to_string(_qport));
            setInfoValue(info, "challenge", std::to_string(_challenge));
            sendText("connect \"" + info + "\"");
        }
        return;
    default:
        break;
    }
    if (now - _lastReceived > kTimeoutMs) {
        _state = State::kDisconnected;
        _message = "Server connection timed out.";
        return;
    }
    if (_haveSnapshot) {
        // The level's clock runs on with ours from the last setting.
        _serverTime = std::max(_serverTime, int32_t(int64_t(now) + _timeDelta));
        if (_serverTime >= _newestSnapshotTime - 5)
            _extrapolated = true;
    }
    // At cl_maxpackets while playing; 20 a second while downloading, as
    // the original's client acknowledges blocks; otherwise one a second.
    const uint64_t interval = _state == State::kActive ? 1000 / kMaxPackets : downloading() ? 50 : kWaitingMs;
    if (!_lastPacket || now - _lastPacket >= interval)
        sendPacket(now);
}

void NetClient::readDemo(uint64_t now) {
    // As the original's client plays one: the messages up to the game
    // state at once; then, once the level is loaded, one a frame until
    // the first snapshot, and after that the next whenever the time shown
    // reaches the newest snapshot.
    while (_state == State::kConnected)
        if (!readDemoMessage(now))
            return;
    if (_state != State::kActive)
        return;
    if (!_haveSnapshot) {
        readDemoMessage(now);
        return;
    }
    _serverTime = std::max(_serverTime, int32_t(int64_t(now) + _timeDelta));
    while (_state == State::kActive && _serverTime >= _newestSnapshotTime)
        if (!readDemoMessage(now))
            return;
}

bool NetClient::readDemoMessage(uint64_t now) {
    auto word = [&](size_t at) {
        int32_t v;
        std::memcpy(&v, _demoData.data() + at, 4);
        return v;
    };
    // The end: -1 for both, or the file running out.
    if (_demoAt + 8 > _demoData.size() || word(_demoAt + 4) < 0 ||
        size_t(word(_demoAt + 4)) > _demoData.size() - _demoAt - 8 ||
        word(_demoAt + 4) > int32_t(wire::Netchan::kMaxMessage)) {
        _state = State::kDisconnected;
        return false;
    }
    _incoming = word(_demoAt);
    const auto length = size_t(word(_demoAt + 4));
    wire::MessageReader r(
        std::vector<uint8_t>(_demoData.begin() + long(_demoAt + 8), _demoData.begin() + long(_demoAt + 8 + length)));
    _demoAt += 8 + length;
    parseMessage(r, now);
    return true;
}

void NetClient::outOfBand(const std::string &text, uint64_t now) {
    if (text.rfind("print\n", 0) == 0) {
        _message = text.substr(6);
        while (!_message.empty() && _message.back() == '\n')
            _message.pop_back();
        return;
    }
    const std::vector<std::string> words = splitCommand(text.substr(0, text.find('\n')));
    if (words.empty())
        return;
    if (words[0] == "challengeResponse" && _state == State::kConnecting && words.size() >= 2) {
        _challenge = std::atoi(words[1].c_str());
        _state = State::kChallenging;
        _lastSent = 0;
        _connectPackets = 0;
    } else if (words[0] == "connectResponse" && _state == State::kChallenging) {
        _state = State::kConnected;
        _chan = wire::Netchan(true, _qport);
        _lastSent = _lastPacket = 0;
        _lastReceived = now;
        _message.clear();
    }
}

void NetClient::parseMessage(wire::MessageReader &r, uint64_t now) {
    while (!r.atEnd()) {
        const int32_t op = r.byte();
        if (op == kSvcServerCommand) {
            const int32_t sequence = r.longValue();
            const std::string text = r.string();
            if (!r.overflowed())
                serverCommand(sequence, text);
        } else if (op == kSvcGamestate) {
            parseGamestate(r);
        } else if (op == kSvcSnapshot) {
            parseSnapshot(r, now);
        } else if (op == kSvcDownload) {
            parseDownload(r, now);
        } else if (op == kSvcNop) {
            continue;
        } else {
            break; // the end (protocol 24 sends no end marker)
        }
        if (r.overflowed())
            break;
    }
}

void NetClient::serverCommand(int32_t sequence, const std::string &text) {
    if (sequence <= _serverCommandSequence)
        return;
    _serverCommandSequence = sequence;
    if (_serverCommands.size() < size_t(sequence))
        _serverCommands.resize(size_t(sequence));
    _serverCommands[size_t(sequence - 1)] = text;
    // Configstrings that changed, kept here too (the level's id in the
    // system info goes back with every packet), as the original's client
    // keeps its game state.
    const std::vector<std::string> words = splitCommand(text);
    if (words.size() >= 2 && (words[0] == "cs" || words[0] == "bcs0" || words[0] == "bcs1" || words[0] == "bcs2")) {
        const int32_t index = std::atoi(words[1].c_str());
        const std::string value = words.size() >= 3 ? words[2] : std::string();
        if (words[0] == "bcs0")
            _bigConfigstring = value;
        else if (words[0] == "bcs1")
            _bigConfigstring += value;
        else if (words[0] == "bcs2")
            _configstrings[index] = _bigConfigstring + value;
        else
            _configstrings[index] = value;
    }
    if (text.rfind("disconnect", 0) == 0) {
        // Protocol 24's reason is the rest of the line, unquoted.
        _state = State::kDisconnected;
        _message = text.size() > 11 ? text.substr(11) : "The server disconnected.";
    }
}

void NetClient::parseGamestate(wire::MessageReader &r) {
    _gamestateSequence = r.longValue();
    _serverCommandSequence = _gamestateSequence;
    _serverCommands.assign(size_t(std::max(0, _gamestateSequence)), std::string());
    _configstrings.clear();
    _baselines.clear();
    while (!r.atEnd()) {
        const int32_t op = r.byte();
        if (op == kSvcConfigstring) {
            const int32_t index = r.shortValue();
            _configstrings[index] = r.bigString();
        } else if (op == kSvcBaseline) {
            const auto number = int32_t(r.bits(kEntityNumberBits));
            std::vector<uint8_t> state;
            if (!readEntityDelta(r, number, {}, state))
                _baselines[number] = std::move(state);
        } else {
            break; // 0 ends the list
        }
        if (r.overflowed())
            break;
    }
    _checksumFeed = r.longValue();
    _gamestateMessage = _incoming;
    _newGamestate = true;
    _state = State::kPrimed;
    _snaps = {};
    _lastSnapMessage = -1;
    _haveSnapshot = false;
    _commands.clear();
    _commandsInLastPacket = 0;
}

void NetClient::parseSnapshot(wire::MessageReader &r, uint64_t now) {
    const int32_t acknowledged = r.longValue();
    if (acknowledged <= _reliableSequence && acknowledged > _reliableAcknowledge)
        _reliableAcknowledge = acknowledged;
    Snap snap;
    snap.message = _incoming;
    snap.serverTime = r.longValue();
    const int32_t deltaNum = r.byte();
    const int32_t flags = r.byte();
    const int32_t maskBytes = std::clamp(r.byte(), 0, 32);
    const std::vector<uint8_t> mask = r.data(size_t(maskBytes));
    std::copy(mask.begin(), mask.end(), snap.areaMask.begin());
    // A change from an older one, if it's still kept.
    const Snap *old = nullptr;
    snap.valid = true;
    if (deltaNum > 0) {
        const int32_t oldMessage = snap.message - deltaNum;
        const Snap &o = _snaps[size_t(oldMessage & 31)];
        if (o.valid && o.message == oldMessage)
            old = &o;
        else
            snap.valid = false; // read, then thrown away: the next is asked for whole
    }
    static const std::vector<uint8_t> kNone;
    readPlayerDelta(r, old ? old->playerState : kNone, snap.playerState);
    static const std::map<int32_t, std::vector<uint8_t>> kNoEntities;
    const auto &before = old ? old->entities : kNoEntities;
    auto o = before.begin();
    while (!r.overflowed()) {
        const auto number = int32_t(r.bits(kEntityNumberBits));
        if (number == kNoEntity)
            break;
        // Those before it are unchanged.
        for (; o != before.end() && o->first < number; ++o)
            snap.entities.insert(*o);
        std::vector<uint8_t> state;
        bool removed;
        if (o != before.end() && o->first == number) {
            removed = readEntityDelta(r, number, o->second, state);
            ++o;
        } else {
            const auto base = _baselines.find(number);
            removed = readEntityDelta(r, number, base != _baselines.end() ? base->second : kNone, state);
        }
        if (!removed)
            snap.entities[number] = std::move(state);
    }
    for (; o != before.end(); ++o)
        snap.entities.insert(*o);
    if (r.overflowed() || !snap.valid || downloading())
        return; // while downloading, they carry nothing (the acknowledgement)
    _snaps[size_t(snap.message & 31)] = snap;
    _lastSnapMessage = snap.message;
    GameHost::Snapshot out;
    out.serverTime = snap.serverTime;
    out.playerState = snap.playerState;
    for (const auto &[number, state] : snap.entities)
        out.entities.push_back(state);
    out.serverCommandSequence = _serverCommandSequence;
    out.messageNum = snap.message;
    out.areaMask = snap.areaMask;
    out.snapFlags = flags;
    _newSnapshots.push_back(std::move(out));
    adjustTime(snap.serverTime, now);
}

void NetClient::download(const std::string &remoteName) {
    _downloadName = remoteName;
    _downloadData.clear();
    _downloadSize = -1;
    _downloadBlock = 0;
    addClientCommand("download " + remoteName);
}

bool NetClient::takeDownload(std::string &name, std::vector<uint8_t> &data) {
    if (!_finished)
        return false;
    _finished = false;
    name = std::move(_finishedName);
    data = std::move(_finishedData);
    return true;
}

void NetClient::parseDownload(wire::MessageReader &r, uint64_t now) {
    // A block: its number, for block 0 the file's size (-1: refused), its
    // length (0: the end) and its bytes; acknowledged in order (nextdl).
    const auto block = int32_t(uint16_t(r.shortValue()));
    if (block == 0 && _downloadBlock == 0) {
        _downloadSize = r.longValue();
        if (_downloadSize < 0) {
            _state = State::kDisconnected;
            _message = "Couldn't download " + _downloadName + ": the server won't send it";
            _downloadName.clear();
            return;
        }
    }
    const int32_t length = r.shortValue();
    if (length < 0 || length > 16384)
        return;
    std::vector<uint8_t> data;
    if (length > 0)
        data = r.data(size_t(length));
    if (_downloadName.empty() || block != (_downloadBlock & 0xffff))
        return; // not asked for, or out of order: sent again
    _downloadData.insert(_downloadData.end(), data.begin(), data.end());
    addClientCommand("nextdl " + std::to_string(_downloadBlock));
    ++_downloadBlock;
    if (length == 0) {
        // The end: acknowledged at once, twice, so the server stops.
        _finishedName = std::move(_downloadName);
        _finishedData = std::move(_downloadData);
        _downloadName.clear();
        _downloadData.clear();
        _finished = true;
        sendPacket(now);
        sendPacket(now);
    }
}

void NetClient::adjustTime(int32_t snapshotTime, uint64_t now) {
    // As the original's client: the level's time is ours plus a
    // difference, reset when it's far out, halved towards the newest
    // snapshot when it's somewhat out, and otherwise nudged a little
    // each snapshot, back when the time had run past the newest one and
    // on otherwise, so it stays just behind the newest.
    _newestSnapshotTime = snapshotTime;
    const int64_t newDelta = int64_t(snapshotTime) - int64_t(now);
    if (!_haveSnapshot) {
        _haveSnapshot = true;
        _timeDelta = newDelta;
        _serverTime = snapshotTime;
        return;
    }
    if (_demo)
        return; // a demo's clock keeps the first setting, as the original's
    const int64_t off = newDelta > _timeDelta ? newDelta - _timeDelta : _timeDelta - newDelta;
    if (off > 500) {
        _timeDelta = newDelta;
        _serverTime = snapshotTime;
    } else if (off > 100) {
        _timeDelta = (_timeDelta + newDelta) / 2;
    } else if (_extrapolated) {
        _extrapolated = false;
        _timeDelta -= 2;
    } else {
        _timeDelta += 1;
    }
}

void NetClient::addCommand(const GameHost::UserCommand &cmd) {
    if (_state < State::kPrimed)
        return;
    _commands.push_back(cmd);
    while (_commands.size() > kMaxPacketCommands) {
        _commands.pop_front();
        if (_commandsInLastPacket)
            --_commandsInLastPacket;
    }
}

void NetClient::addClientCommand(const std::string &text) {
    if (_reliableSequence - _reliableAcknowledge >= int32_t(_reliable.size()))
        return; // too many waiting
    ++_reliableSequence;
    _reliable[size_t(_reliableSequence & 63)] = text;
}

void NetClient::setUserinfo(const std::string &userinfo) {
    _userinfo = userinfo;
    if (_state >= State::kConnected)
        addClientCommand("userinfo \"" + userinfo + "\"");
}

void NetClient::disconnect() {
    if (_state >= State::kConnected) {
        addClientCommand("disconnect");
        for (int i = 0; i < 3; ++i)
            sendPacket(_lastPacket + 1);
    }
    _state = State::kDisconnected;
}

void NetClient::sendPacket(uint64_t now) {
    _lastPacket = now;
    if (!_socket)
        return;
    wire::MessageWriter w;
    const auto it = _configstrings.find(1);
    w.longValue(it != _configstrings.end() ? std::atoi(infoValue(it->second, "sv_serverid").c_str()) : 0);
    w.longValue(_chan.incomingSequence());
    w.longValue(_serverCommandSequence);
    for (int32_t i = _reliableAcknowledge + 1; i <= _reliableSequence; ++i) {
        w.byte(kClcClientCommand);
        w.longValue(i);
        w.string(_reliable[size_t(i & 63)]);
    }
    if (!_commands.empty() && _state >= State::kPrimed) {
        // A change from the last snapshot if this message is one we
        // could take as one; otherwise the next is asked for whole.
        const bool delta = _lastSnapMessage >= 0 && _lastSnapMessage == _chan.incomingSequence();
        w.byte(delta ? kClcMove : kClcMoveNoDelta);
        w.byte(uint32_t(_commands.size()));
        GameHost::UserCommand previous;
        for (const GameHost::UserCommand &cmd : _commands) {
            writeUserCommand(w, previous, cmd);
            previous = cmd;
        }
        // Each command goes out twice (in this packet and the next).
        for (size_t i = 0; i < _commandsInLastPacket && !_commands.empty(); ++i)
            _commands.pop_front();
        _commandsInLastPacket = _commands.size();
    }
    for (const std::vector<uint8_t> &d : _chan.transmit(w.bytes()))
        _socket->send(_server, d);
}

} // namespace reluva::idtech3::eliteforce
