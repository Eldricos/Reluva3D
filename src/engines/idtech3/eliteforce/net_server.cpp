// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/net_server.h"

#include "engines/idtech3/eliteforce/game_info.h"

#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/client/server_browser.h"

#include "common/fs.h"

#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace reluva::idtech3::eliteforce {

namespace {

// Server message opcodes (Quake III's numbers).
constexpr uint32_t kSvcGamestate = 2;
constexpr uint32_t kSvcConfigstring = 3;
constexpr uint32_t kSvcBaseline = 4;
constexpr uint32_t kSvcServerCommand = 5;
constexpr uint32_t kSvcSnapshot = 7;
// Client message opcodes.
constexpr int32_t kClcNop = 1;
constexpr int32_t kClcMove = 2;
constexpr int32_t kClcMoveNoDelta = 3;
constexpr int32_t kClcClientCommand = 4;
constexpr int kMaxPacketCommands = 32;
constexpr int32_t kPlayersConfigstring = 544; // CS_PLAYERS: a player's info, "\n\<name>\..."
constexpr int32_t kPersistantOffset = 268;    // playerState persistant[], 1.2's layout; [0] the score
constexpr size_t kConfigstringPiece = 1000;   // longer configstrings go in pieces (bcs0, bcs1, bcs2)
constexpr uint64_t kZombieMs = 2000;
constexpr uint64_t kChallengeMs = 60000;

} // namespace

std::vector<std::string> configstringCommands(const std::map<int32_t, std::string> &before,
                                              const std::map<int32_t, std::string> &now) {
    std::vector<std::string> out;
    for (const auto &[index, text] : now) {
        const auto it = before.find(index);
        if (it != before.end() && it->second == text)
            continue;
        const std::string i = std::to_string(index);
        if (text.size() < kConfigstringPiece) {
            out.push_back("cs " + i + " \"" + text + "\"");
            continue;
        }
        for (size_t at = 0; at < text.size(); at += kConfigstringPiece) {
            const char *kind = at == 0 ? "bcs0 " : at + kConfigstringPiece >= text.size() ? "bcs2 " : "bcs1 ";
            out.push_back(kind + i + " \"" + text.substr(at, kConfigstringPiece) + "\"");
        }
    }
    for (const auto &[index, text] : before)
        if (!now.count(index))
            out.push_back("cs " + std::to_string(index) + " \"\"");
    return out;
}

NetServer::NetServer(GameHost &game, CvarStore &cvars, std::unique_ptr<reluva::net::UdpSocket> socket)
    : _game(game), _cvars(cvars), _socket(std::move(socket)) {
    _cvars.create("sv_timeout", "200", 0);
    _clients.resize(64);
    _random ^= uint32_t(reinterpret_cast<uintptr_t>(this));
}

NetServer::~NetServer() {
    // As the original when it shuts down: a last message to everyone,
    // telling them it's gone; and two heartbeats, so the masters ask, get
    // no answer and drop it.
    for (Client &c : _clients)
        if (c.state >= State::kConnected)
            finalMessage(c, "Server shut down", _lastNow);
    heartbeat(_lastNow, true);
    heartbeat(_lastNow, true);
}

void NetServer::heartbeat(uint64_t now, bool force) {
    // Only a public Internet server (dedicated 2) tells the masters it's
    // there, as the original: every 5 minutes, in Elite Force's words.
    if (_cvars.integer("dedicated") != 2 || !resolveHost || (!force && now < _nextHeartbeat))
        return;
    _nextHeartbeat = now + 300000;
    if (!_mastersResolved) {
        _mastersResolved = true;
        for (const std::string &name : masterServerNames(_cvars, masterServers())) {
            const auto [host, port] = reluva::net::splitHostPort(name, ServerBrowser::kMasterPort);
            uint32_t ip = 0;
            if (resolveHost(host, ip))
                _masters.push_back({ip, port});
            else if (print)
                print("Couldn't find the master server " + name + "\n");
        }
    }
    for (const reluva::net::Address &m : _masters) {
        sendText(m, "\\heartbeat\\" + std::to_string(port()) + "\\gamename\\STEF1\\");
        if (print)
            print("Heartbeat to the master at " + m.toString() + "\n");
    }
}

void NetServer::finalMessage(Client &c, const std::string &reason, uint64_t now) {
    // As the original: the reason as a command, and twice, a snapshot
    // with it (the client game reads commands as snapshots say they're
    // there; the retail client acts on disconnect only then).
    if (c.reliableSequence - c.reliableAcknowledge < kMaxReliable) {
        ++c.reliableSequence;
        c.reliable[size_t(c.reliableSequence & (kMaxReliable - 1))] = "disconnect " + reason;
    }
    for (int i = 0; i < 2; ++i) {
        if (c.state >= State::kPrimed) {
            sendSnapshot(c, now);
        } else {
            wire::MessageWriter w;
            writeReliable(c, w);
            transmit(c, w);
        }
    }
}

uint32_t NetServer::nextRandom() {
    _random = _random * 1103515245u + 12345u;
    return _random >> 1;
}

namespace {

constexpr int kDownloadWindow = 48; // blocks sent ahead of the client's acknowledgement
constexpr size_t kDownloadBlock = 1024;

// A pak's name as the original lists it: its folder and name, without .pk3.
std::string pakName(const ZipArchive &pak) {
    const std::filesystem::path p(pak.describe());
    return p.parent_path().filename().string() + "/" + p.stem().string();
}

int downloadBlocks(size_t size) {
    return int(size / kDownloadBlock + (size % kDownloadBlock ? 1 : 0)) + 1;
}

} // namespace

void NetServer::levelStarted() {
    _checksumFeed = int32_t(nextRandom());
    // The paks the level uses, as the original lists them in the system
    // info for players to check theirs against (and download what they
    // lack), by checksum and by folder and name: those the server's side
    // read from since the level started (the map, its bots' map and
    // files, the game code's reads; not files read from every pak,
    // docs/engines/idtech3/games/stvef/systems/network.md), any outside the game's own folder (a mod's),
    // and, Reluva's addition, the client game's and menus'.
    _pure = false;
    _purePaks.clear();
    if (_files) {
        std::set<const Archive *> used;
        auto note = [&](const std::string &name) {
            if (const Archive *a = _files->find(name))
                used.insert(a);
        };
        const std::string map = _cvars.string("mapname");
        note("maps/" + map + ".bsp");
        note("vm/cgame.qvm");
        note("vm/ui.qvm");
        for (const std::string &name : _game.filesRead())
            if (referencesPak(name))
                note(name);
        std::string sums;
        std::string names;
        std::string allSums;
        std::string allNames;
        _referencedNames.clear();
        for (const Archive *a : _files->archives()) {
            const auto *pak = dynamic_cast<const ZipArchive *>(a);
            if (!pak)
                continue;
            const std::string name = pakName(*pak);
            allSums += std::to_string(pakChecksum(*pak)) + " ";
            allNames += name + " ";
            std::string folder = name.substr(0, name.find('/'));
            for (char &ch : folder)
                ch = char(std::tolower(static_cast<unsigned char>(ch)));
            if (!used.count(pak) && folder == "baseef")
                continue;
            sums += std::to_string(pakChecksum(*pak)) + " ";
            names += name + " ";
            _referencedNames.push_back(name);
        }
        _cvars.create("sv_referencedPaks", "", CvarStore::kSystemInfo | CvarStore::kReadOnly);
        _cvars.create("sv_referencedPakNames", "", CvarStore::kSystemInfo | CvarStore::kReadOnly);
        _cvars.set("sv_referencedPaks", sums, true);
        _cvars.set("sv_referencedPakNames", names, true);

        // Pure (sv_pure 1): every pak the server has, which players may
        // read only from, and the checksums their answers must give. The
        // client game and menus must come from paks for that.
        const auto *cgame = dynamic_cast<const ZipArchive *>(_files->find("vm/cgame.qvm"));
        const auto *ui = dynamic_cast<const ZipArchive *>(_files->find("vm/ui.qvm"));
        if (_cvars.integer("sv_pure") != 0) {
            if (cgame && ui) {
                _pure = true;
                _pureCgame = pakChecksum(*cgame, _checksumFeed);
                _pureUi = pakChecksum(*ui, _checksumFeed);
                for (const Archive *a : _files->archives())
                    if (const auto *pak = dynamic_cast<const ZipArchive *>(a))
                        _purePaks.insert(pakChecksum(*pak, _checksumFeed));
            } else if (print) {
                print("sv_pure is set, but the client game or menus aren't in a pak: not pure\n");
            }
        }
        _cvars.create("sv_paks", "", CvarStore::kSystemInfo | CvarStore::kReadOnly);
        _cvars.create("sv_pakNames", "", CvarStore::kSystemInfo | CvarStore::kReadOnly);
        _cvars.set("sv_paks", _pure ? allSums : "", true);
        _cvars.set("sv_pakNames", _pure ? allNames : "", true);
    }
    _cvars.create("sv_allowDownload", "0", CvarStore::kServerInfo);
    // The entities as the level settled are the baselines new players'
    // snapshots are made from, as the original takes them.
    _baselines.clear();
    for (const int32_t n : _game.linked())
        if (std::vector<uint8_t> s = _game.entityState(n); !s.empty())
            _baselines[n] = std::move(s);
    _configstrings = _game.configstrings();
    _serverCommandsSeen = _game.serverCommands().size();
    _dropsSeen = _game.drops().size();
    ++_levelCount;
    _restartsSeen = _game.restarts();
    _restartedIds.clear();
    _serverId = _cvars.integer("sv_serverid");
    for (Client &c : _clients)
        if (c.state >= State::kConnected) {
            c.state = State::kConnected;
            c.gamestateMessage = -1;
            c.deltaMessage = -1;
            c.nextSnapshotTime = 0;
            c.begun = false;
            c.pureAnswered = c.pureValid = false; // answered again for this level's feed
        }
}

std::vector<std::pair<int32_t, std::string>> NetServer::players() const {
    std::vector<std::pair<int32_t, std::string>> out;
    for (const Client &c : _clients)
        if (c.state >= State::kConnected)
            out.emplace_back(c.number, c.name);
    return out;
}

void NetServer::sendText(const reluva::net::Address &to, const std::string &text) {
    std::vector<uint8_t> d(4, 0xff);
    d.insert(d.end(), text.begin(), text.end());
    _socket->send(to, d);
}

std::string NetServer::infoString(const std::string &challenge) const {
    // What the server list shows (docs/engines/idtech3/games/stvef/systems/network.md): Elite Force's
    // servers send it quoted on one line.
    const int maxClients = std::clamp(_cvars.integer("sv_maxclients"), 1, 64);
    int clients = 0;
    int humans = 0;
    for (int32_t n = 0; n < maxClients; ++n)
        if (!_game.clientSlotFree(n)) {
            ++clients;
            humans += !_game.isBot(n);
        }
    std::string info;
    setInfoValue(info, "challenge", challenge);
    // The game's name, as the GPL engine running Elite Force sends it (the
    // community's masters list servers by it).
    setInfoValue(info, "gamename", "EliteForce");
    setInfoValue(info, "protocol", "24");
    setInfoValue(info, "hostname", _cvars.string("sv_hostname"));
    setInfoValue(info, "mapname", _cvars.string("mapname"));
    setInfoValue(info, "clients", std::to_string(clients));
    setInfoValue(info, "g_humanplayers", std::to_string(humans));
    setInfoValue(info, "sv_maxclients", std::to_string(maxClients));
    setInfoValue(info, "gametype", std::to_string(_cvars.integer("g_gametype")));
    setInfoValue(info, "pure", _pure ? "1" : "0");
    return info;
}

void NetServer::noteServerId() {
    // The level's id changed by a map_restart: the old one is still this
    // level's, for packets sent before the client heard.
    const int32_t id = _cvars.integer("sv_serverid");
    if (id != _serverId) {
        _restartedIds.insert(_serverId);
        _serverId = id;
    }
}

Error NetServer::receive(uint64_t now) {
    _lastNow = now;
    noteServerId();
    // Pieces of messages that waited for the rate, then download blocks
    // as the rate allows (on the local network, a few at a time).
    for (Client &c : _clients) {
        if (c.state == State::kFree)
            continue;
        if (!c.queued.empty())
            sendQueued(c, now, false);
        for (int i = 0;
             i < (c.unlimited ? 8 : 1) && !c.download.name.empty() && c.queued.empty() && rateWait(c, now) == 0; ++i)
            sendDownload(c, now);
    }
    reluva::net::Address from;
    std::vector<uint8_t> d;
    while (_socket->receive(from, d)) {
        if (d.size() >= 4 && d[0] == 0xff && d[1] == 0xff && d[2] == 0xff && d[3] == 0xff) {
            outOfBand(from, std::string(d.begin() + 4, d.end()), now);
            continue;
        }
        uint16_t qport = 0;
        if (!wire::Netchan::clientQport(d, qport))
            continue;
        if (Client *c = clientFor(from, qport))
            if (Error err = packet(*c, d, now))
                return err;
    }
    return Error::success();
}

NetServer::Client *NetServer::clientFor(const reluva::net::Address &from, uint16_t qport) {
    for (Client &c : _clients)
        if (c.state != State::kFree && c.address.ip == from.ip && c.qport == qport) {
            c.address.port = from.port; // a router may have changed it
            return &c;
        }
    return nullptr;
}

void NetServer::outOfBand(const reluva::net::Address &from, const std::string &text, uint64_t now) {
    std::string line = text;
    if (const size_t nl = line.find('\n'); nl != std::string::npos)
        line.resize(nl);
    const std::vector<std::string> words = splitCommand(line);
    if (words.empty())
        return;
    const std::string &verb = words[0];
    // A master checking on the server after a heartbeat (the community's
    // ask with getinfo, Raven's with getstatus): it can reach it.
    if ((verb == "getinfo" || verb == "getstatus") && print &&
        std::any_of(_masters.begin(), _masters.end(), [&](const auto &m) { return m.ip == from.ip; }))
        print("The master at " + from.toString() + " asked about this server, so it can reach it\n");
    if (verb == "getinfo") {
        sendText(from, "infoResponse \"" + infoString(words.size() > 1 ? words[1] : "") + "\"");
    } else if (verb == "getstatus") {
        std::string info = _game.configstrings().count(0) ? _game.configstrings().at(0) : std::string();
        setInfoValue(info, "challenge", words.size() > 1 ? words[1] : "");
        std::string reply = "statusResponse\n" + info + "\n";
        const int maxClients = std::clamp(_cvars.integer("sv_maxclients"), 1, 64);
        for (int32_t n = 0; n < maxClients; ++n) {
            if (_game.clientSlotFree(n))
                continue;
            const auto it = _game.configstrings().find(kPlayersConfigstring + n);
            const std::string name = it != _game.configstrings().end() ? infoValue(it->second, "n") : "";
            const GameHost::Snapshot s = _game.snapshot(n);
            int32_t score = 0;
            if (s.playerState.size() >= size_t(kPersistantOffset + 4))
                std::memcpy(&score, s.playerState.data() + kPersistantOffset, 4);
            int ping = 0;
            for (const Client &c : _clients)
                if (c.state >= State::kConnected && c.number == n)
                    ping = c.ping;
            reply += std::to_string(score) + " " + std::to_string(ping) + " \"" + name + "\"\n";
        }
        sendText(from, reply);
    } else if (verb == "getchallenge") {
        Challenge &ch = _challenges[from];
        if (!ch.value || now - ch.time > kChallengeMs)
            ch = {int32_t(nextRandom() & 0x7fffffff) | 1, now};
        sendText(from, "challengeResponse " + std::to_string(ch.value));
    } else if (verb == "connect" && words.size() >= 2) {
        connect(from, words[1], now);
    }
    // "disconnect" and anything else: nothing to answer.
}

void NetServer::connect(const reluva::net::Address &from, const std::string &userinfo, uint64_t now) {
    const std::string protocol = infoValue(userinfo, "protocol");
    if (protocol != "24") {
        sendText(from, "print\nServer uses protocol version 24 (yours is " + protocol + ").\n");
        return;
    }
    // Clients on the same machine needn't have asked for a challenge.
    if (!from.isLoopback()) {
        const auto it = _challenges.find(from);
        if (it == _challenges.end() || std::atoi(infoValue(userinfo, "challenge").c_str()) != it->second.value) {
            sendText(from, "print\nNo or bad challenge for your address.\n");
            return;
        }
    }
    const auto qport = uint16_t(std::atoi(infoValue(userinfo, "qport").c_str()));
    // A player connecting again (the same address and qport) keeps their slot.
    int32_t number = -1;
    Client *slot = nullptr;
    for (Client &c : _clients)
        if (c.state >= State::kConnected && c.address.ip == from.ip && c.qport == qport) {
            if (now - c.lastReceived < 3000 && c.state == State::kConnected) {
                sendText(from, "connectResponse");
                return; // its own repeat
            }
            number = c.number;
            _game.disconnectClient(number);
            slot = &c;
        }
    const int maxClients = std::clamp(_cvars.integer("sv_maxclients"), 1, 64);
    if (number < 0)
        for (int32_t n = 0; n < maxClients && number < 0; ++n) {
            if (_reserved.count(n) || !_game.clientSlotFree(n))
                continue;
            bool ours = false;
            for (const Client &c : _clients)
                ours = ours || (c.state != State::kFree && c.number == n);
            if (!ours)
                number = n;
        }
    if (number < 0) {
        sendText(from, "print\nServer is full.\n");
        return;
    }
    if (!slot)
        for (Client &c : _clients)
            if (c.state == State::kFree) {
                slot = &c;
                break;
            }
    if (!slot) {
        sendText(from, "print\nServer is full.\n");
        return;
    }
    std::string info = userinfo;
    setInfoValue(info, "ip", from.toString());
    std::string refused;
    if (Error err = _game.connectClient(number, info, refused); err || !refused.empty()) {
        sendText(from, "print\n" + (err ? err.message : refused) + "\n");
        return;
    }
    *slot = Client{};
    slot->state = State::kConnected;
    slot->number = number;
    slot->address = from;
    slot->qport = qport;
    slot->userinfo = info;
    slot->lastReceived = now;
    applyUserinfo(*slot);
    sendText(from, "connectResponse");
    if (print)
        print(slot->name + " connected from " + from.toString() + "\n");
}

void NetServer::applyUserinfo(Client &c) {
    c.name = infoValue(c.userinfo, "name");
    // The rate: players on the local network aren't held to one unless the
    // server is a public one (dedicated 2) or sv_lanForceRate is 0; others
    // to theirs, 1000 to 90000 bytes a second, 3000 if they give none.
    _cvars.create("sv_lanForceRate", "1", CvarStore::kArchive);
    c.unlimited = c.address.isLan() && _cvars.integer("dedicated") != 2 && _cvars.integer("sv_lanForceRate") == 1;
    const std::string rate = infoValue(c.userinfo, "rate");
    c.rate = rate.empty() ? 3000 : std::clamp(std::atoi(rate.c_str()), 1000, 90000);
    const int fps = std::max(1, _cvars.integer("sv_fps") ? _cvars.integer("sv_fps") : 20);
    int snaps = std::atoi(infoValue(c.userinfo, "snaps").c_str());
    snaps = snaps <= 0 ? fps : std::clamp(snaps, 1, fps);
    c.snapMs = 1000 / snaps;
}

Error NetServer::packet(Client &c, const std::vector<uint8_t> &datagram, uint64_t now) {
    std::vector<uint8_t> message;
    if (c.state == State::kZombie || !c.chan.process(datagram, message))
        return Error::success();
    c.lastReceived = now;
    wire::MessageReader r(std::move(message));
    const int32_t serverId = r.longValue();
    c.messageAcknowledge = r.longValue();
    const int32_t reliableAcknowledge = r.longValue();
    if (r.overflowed())
        return Error::success();
    if (reliableAcknowledge <= c.reliableSequence && reliableAcknowledge >= c.reliableSequence - kMaxReliable)
        c.reliableAcknowledge = std::max(c.reliableAcknowledge, reliableAcknowledge);
    const Frame &acked = c.frames[size_t(c.messageAcknowledge & (kPacketBackup - 1))];
    if (acked.message == c.messageAcknowledge && acked.sentMs)
        c.ping = int(now - acked.sentMs);
    // Commands from before the level it has: the game state again once
    // it has had a message since the last one.
    if (serverId != _cvars.integer("sv_serverid")) {
        // From before a map_restart of this level: ignored, as the
        // original does, until the client hears of the new id.
        if (_restartedIds.count(serverId))
            return Error::success();
        if (c.messageAcknowledge > c.gamestateMessage)
            sendGamestate(c, now);
        return Error::success();
    }
    while (!r.atEnd()) {
        const int32_t op = r.byte();
        if (op == kClcClientCommand) {
            const int32_t sequence = r.longValue();
            const std::string text = r.string();
            if (r.overflowed() || sequence <= c.lastClientCommand)
                continue;
            if (sequence > c.lastClientCommand + 1) {
                drop(c, "Lost reliable commands", now);
                return Error::success();
            }
            c.lastClientCommand = sequence;
            if (Error err = clientCommand(c, text))
                return err;
            if (c.state == State::kZombie)
                return Error::success();
        } else if (op == kClcMove || op == kClcMoveNoDelta) {
            const int count = r.byte();
            if (count < 1 || count > kMaxPacketCommands)
                break;
            std::vector<GameHost::UserCommand> commands;
            GameHost::UserCommand previous;
            for (int i = 0; i < count; ++i)
                commands.push_back(previous = readUserCommand(r, previous));
            if (r.overflowed())
                break;
            c.deltaMessage = op == kClcMove ? c.messageAcknowledge : -1;
            // Pure: no moves before the player's paks are checked; one
            // already in the level gets the game state again (and answers).
            if (_pure && !c.pureAnswered) {
                if (c.state == State::kActive)
                    sendGamestate(c, now);
                break;
            }
            if (c.state == State::kPrimed) {
                // Its first move puts the player in the level.
                c.state = State::kActive;
                c.deltaMessage = -1;
                c.lastCommand = GameHost::UserCommand{};
                if (!c.begun)
                    if (Error err = _game.beginClient(c.number))
                        return err;
                c.begun = true;
            }
            if (_pure && !c.pureValid) {
                drop(c, "Cannot validate pure client!", now);
                return Error::success();
            }
            if (c.state != State::kActive)
                break;
            for (const GameHost::UserCommand &cmd : commands) {
                if (cmd.serverTime <= c.lastCommand.serverTime)
                    continue;
                c.lastCommand = cmd;
                if (Error err = _game.clientThink(c.number, cmd))
                    return err;
            }
            break;
        } else if (op == kClcNop) {
            continue;
        } else {
            break; // the end (kClcEof isn't sent in protocol 24)
        }
    }
    return Error::success();
}

Error NetServer::clientCommand(Client &c, const std::string &text) {
    // The engine's own, then the game's.
    const std::vector<std::string> words = splitCommand(text);
    if (words.empty())
        return Error::success();
    const std::string &verb = words[0];
    if (verb == "userinfo") {
        c.userinfo = words.size() > 1 ? words[1] : std::string();
        setInfoValue(c.userinfo, "ip", c.address.toString());
        applyUserinfo(c);
        return _game.userinfoChanged(c.number, c.userinfo);
    }
    if (verb == "disconnect") {
        drop(c, "disconnected", 0);
        return Error::success();
    }
    // Downloads, as the original's server: a pak asked for (sent from the
    // next frame on), the client's acknowledgement of each block, stopping,
    // and done (the game state again, with any players who joined meanwhile).
    if (verb == "download") {
        c.download = Client::Download{};
        c.download.name = words.size() > 1 ? words[1] : std::string();
        return Error::success();
    }
    if (verb == "nextdl") {
        const int block = words.size() > 1 ? std::atoi(words[1].c_str()) : -1;
        if (c.download.name.empty())
            return Error::success();
        if (block != c.download.clientBlock) {
            drop(c, "broken download", _lastNow);
            return Error::success();
        }
        if (block + 1 >= downloadBlocks(c.download.data.size())) {
            if (print)
                print(c.name + " downloaded " + c.download.name + "\n");
            c.download = Client::Download{}; // the empty last block: done
        } else {
            ++c.download.clientBlock;
            c.download.sendTime = _lastNow;
        }
        return Error::success();
    }
    if (verb == "stopdl") {
        c.download = Client::Download{};
        return Error::success();
    }
    if (verb == "donedl") {
        if (c.state != State::kActive)
            sendGamestate(c, _lastNow);
        return Error::success();
    }
    // The pure check (cp): on a pure server, a wrong answer drops the
    // player (moves wait for one). vdr: the player's paks changed (its
    // video restarted); it answers again.
    if (verb == "cp") {
        if (!_pure)
            return Error::success();
        c.pureAnswered = true;
        c.pureValid = pureAnswerValid(words, _pureCgame, _pureUi, _purePaks, _checksumFeed);
        if (!c.pureValid)
            drop(c, "Unpure client detected. Invalid .PK3 files referenced!", _lastNow);
        return Error::success();
    }
    if (verb == "vdr") {
        c.pureAnswered = c.pureValid = false;
        return Error::success();
    }
    if (c.state != State::kActive)
        return Error::success();
    return _game.clientCommand(c.number, text);
}

void NetServer::addReliable(Client &c, const std::string &text) {
    if (c.reliableSequence - c.reliableAcknowledge >= kMaxReliable) {
        drop(c, "Server command overflow", 0);
        return;
    }
    ++c.reliableSequence;
    c.reliable[size_t(c.reliableSequence & (kMaxReliable - 1))] = text;
}

void NetServer::writeReliable(Client &c, wire::MessageWriter &w) {
    for (int32_t i = c.reliableAcknowledge + 1; i <= c.reliableSequence; ++i) {
        w.byte(kSvcServerCommand);
        w.longValue(i);
        w.string(c.reliable[size_t(i & (kMaxReliable - 1))]);
    }
}

void NetServer::transmit(Client &c, const wire::MessageWriter &w) {
    // The first datagram now unless others wait; a message in pieces sends
    // the rest one at a time as the client's rate allows (sendQueued).
    for (std::vector<uint8_t> &d : c.chan.transmit(w.bytes()))
        c.queued.push_back(std::move(d));
    sendQueued(c, _lastNow, true);
}

void NetServer::sendQueued(Client &c, uint64_t now, bool first) {
    while (!c.queued.empty() && (first || rateWait(c, now) == 0)) {
        _socket->send(c.address, c.queued.front());
        c.lastSentSize = c.queued.front().size();
        c.lastSentMs = now;
        c.queued.pop_front();
        first = false;
        if (!c.unlimited)
            break; // the next when the rate allows
    }
}

int NetServer::rateWait(const Client &c, uint64_t now) const {
    // As the original: the time the last datagram (and 28 bytes of UDP
    // and IP) takes at the client's rate, less what has passed since.
    if (c.unlimited)
        return 0;
    int rate = c.rate;
    if (const int most = _cvars.integer("sv_maxRate"))
        rate = std::min(rate, std::max(most, 1000));
    if (const int least = _cvars.integer("sv_minRate"))
        rate = std::max(rate, std::max(least, 1000));
    const auto ms = int((c.lastSentSize + 28) * 1000 / size_t(std::max(rate, 1)));
    const auto passed = int(now - c.lastSentMs);
    return passed > ms ? 0 : ms - passed;
}

void NetServer::sendGamestate(Client &c, uint64_t now) {
    c.state = State::kPrimed;
    c.gamestateMessage = c.chan.outgoingSequence() + 1;
    c.gamestateMs = now;
    c.deltaMessage = -1;
    wire::MessageWriter w;
    writeReliable(c, w);
    w.byte(kSvcGamestate);
    w.longValue(c.reliableSequence);
    for (const auto &[index, text] : _game.configstrings()) {
        if (text.empty())
            continue;
        w.byte(kSvcConfigstring);
        w.shortValue(index);
        w.bigString(text);
    }
    const std::vector<uint8_t> none;
    for (const auto &[number, state] : _baselines) {
        w.byte(kSvcBaseline);
        writeEntityDelta(w, none, state, true);
    }
    w.byte(0); // the end of the list
    w.longValue(_checksumFeed);
    transmit(c, w);
}

void NetServer::sendSnapshot(Client &c, uint64_t now) {
    const int32_t sequence = c.chan.outgoingSequence() + 1;
    Frame &frame = c.frames[size_t(sequence & (kPacketBackup - 1))];
    const GameHost::Snapshot s = _game.snapshot(c.number);
    frame = Frame{};
    frame.message = sequence;
    frame.sentMs = now;
    frame.playerState = s.playerState;
    frame.serverTime = s.serverTime;
    for (const std::vector<uint8_t> &e : s.entities) {
        int32_t number = 0;
        std::memcpy(&number, e.data(), 4);
        frame.entities[number] = e;
    }
    // A change from the snapshot it last had, if it's still kept.
    const Frame *old = nullptr;
    int32_t deltaNum = 0;
    if (c.deltaMessage > 0 && sequence - c.deltaMessage < kPacketBackup - 3) {
        const Frame &f = c.frames[size_t(c.deltaMessage & (kPacketBackup - 1))];
        if (f.message == c.deltaMessage) {
            old = &f;
            deltaNum = sequence - c.deltaMessage;
        }
    }
    wire::MessageWriter w;
    writeReliable(c, w);
    w.byte(kSvcSnapshot);
    w.longValue(c.lastClientCommand);
    w.longValue(s.serverTime);
    w.byte(uint32_t(deltaNum));
    // Snap flags: 1 the last was held back by the rate; 2 not active (still
    // loading); 4 changes with each level.
    w.byte((c.rateDelayed ? 1u : 0u) | (c.state != State::kActive ? 2u : 0u) | (_levelCount & 1 ? 4u : 0u));
    const int maskBytes = std::min(32, (_game.areaCount() + 7) / 8);
    w.byte(uint32_t(maskBytes));
    w.data(s.areaMask.data(), size_t(maskBytes));
    static const std::vector<uint8_t> kNone;
    writePlayerDelta(w, old ? old->playerState : kNone, frame.playerState);
    // The entities: the old and new lists merged by number.
    static const std::map<int32_t, std::vector<uint8_t>> kNoEntities;
    const auto &before = old ? old->entities : kNoEntities;
    auto o = before.begin();
    auto n = frame.entities.begin();
    while (o != before.end() || n != frame.entities.end()) {
        if (n != frame.entities.end() && (o == before.end() || n->first < o->first)) {
            const auto base = _baselines.find(n->first);
            writeEntityDelta(w, base != _baselines.end() ? base->second : kNone, n->second, true);
            ++n;
        } else if (o != before.end() && (n == frame.entities.end() || o->first < n->first)) {
            writeEntityRemoved(w, o->first);
            ++o;
        } else {
            writeEntityDelta(w, o->second, n->second, false);
            ++o;
            ++n;
        }
    }
    w.bits(uint32_t(kNoEntity), kEntityNumberBits);
    transmit(c, w);
}

void NetServer::writeDummySnapshot(Client &c, wire::MessageWriter &w) {
    // What goes before each download block (protocol 24): a snapshot with
    // nothing in it, for the reliable acknowledgement.
    w.byte(kSvcSnapshot);
    w.longValue(c.lastClientCommand);
    w.longValue(_game.levelTime());
    w.byte(0); // not a change from an older one
    w.byte((c.rateDelayed ? 1u : 0u) | (c.state != State::kActive ? 2u : 0u) | (_levelCount & 1 ? 4u : 0u));
    w.byte(0); // no area mask
    static const std::vector<uint8_t> kNone;
    writePlayerDelta(w, kNone, kNone);
    w.bits(uint32_t(kNoEntity), kEntityNumberBits);
}

bool NetServer::downloadAllowed(const std::string &name, std::string &why) const {
    // As the original: only with sv_allowDownload, only paks the level
    // uses, and never Raven's own (pak0 to pak3 in baseEF).
    std::string base = name;
    if (base.size() > 4 && fs::toLower(base.substr(base.size() - 4)) == ".pk3")
        base.resize(base.size() - 4);
    else
        base.clear();
    bool referenced = false;
    for (const std::string &r : _referencedNames)
        referenced = referenced || fs::toLower(r) == fs::toLower(base);
    const std::string lower = fs::toLower(base);
    const bool raven = lower.size() == 10 && lower.rfind("baseef/pak", 0) == 0 && lower[9] >= '0' && lower[9] <= '3';
    if (!(_cvars.integer("sv_allowDownload") & 1))
        why = "download disabled";
    else if (!referenced)
        why = "not referenced";
    else if (raven)
        why = "a Raven pak";
    else
        return true;
    return false;
}

void NetServer::sendDownload(Client &c, uint64_t now) {
    Client::Download &d = c.download;
    if (!d.opened) {
        d.opened = true;
        std::string why;
        Error err = Error::success();
        bool ok = downloadAllowed(d.name, why);
        if (ok) {
            // The pak's file itself, from where the level's files are.
            ok = false;
            if (_files)
                for (const Archive *a : _files->archives())
                    if (const auto *pak = dynamic_cast<const ZipArchive *>(a))
                        if (fs::toLower(pakName(*pak) + ".pk3") == fs::toLower(d.name)) {
                            const std::filesystem::path path(pak->describe());
                            std::error_code ec;
                            const auto size = std::filesystem::file_size(path, ec);
                            std::ifstream in(path, std::ios::binary);
                            d.data.resize(size_t(ec ? 0 : size));
                            ok =
                                !ec && in.read(reinterpret_cast<char *>(d.data.data()), std::streamsize(d.data.size()));
                        }
            if (!ok)
                why = "not found";
        }
        if (print)
            print(c.name + (ok ? " downloading " : " can't download ") + d.name + (ok ? "" : " (" + why + ")") + "\n");
        if (!ok) {
            // Block 0 with the size -1: refused (protocol 24 gives no reason).
            wire::MessageWriter w;
            writeDummySnapshot(c, w);
            w.byte(6);
            w.shortValue(0);
            w.longValue(-1);
            transmit(c, w);
            d = Client::Download{};
            return;
        }
    }
    const int total = downloadBlocks(d.data.size());
    const int window = std::min(d.clientBlock + kDownloadWindow, total);
    if (d.xmitBlock >= window) {
        // The whole window is out: again from the client's, after a second.
        if (now - d.sendTime <= 1000)
            return;
        d.xmitBlock = d.clientBlock;
    }
    const size_t at = size_t(d.xmitBlock) * kDownloadBlock;
    const size_t length = at < d.data.size() ? std::min(kDownloadBlock, d.data.size() - at) : 0;
    wire::MessageWriter w;
    writeDummySnapshot(c, w);
    w.byte(6);
    w.shortValue(d.xmitBlock);
    if (d.xmitBlock == 0)
        w.longValue(int32_t(d.data.size()));
    w.shortValue(int32_t(length));
    if (length)
        w.data(d.data.data() + at, length);
    transmit(c, w);
    ++d.xmitBlock;
    d.sendTime = now;
}

void NetServer::drop(Client &c, const std::string &reason, uint64_t now) {
    if (c.state <= State::kZombie)
        return;
    // Its last message, while it's still in the game, then out.
    finalMessage(c, reason, now);
    _game.disconnectClient(c.number);
    if (print)
        print(c.name + " " + reason + "\n");
    c.state = State::kZombie;
    c.zombieSince = now;
}

Error NetServer::frame(uint64_t now) {
    _lastNow = now;
    // Players the game dropped (kicked, a bad password...).
    const auto &drops = _game.drops();
    for (; _dropsSeen < drops.size(); ++_dropsSeen)
        for (Client &c : _clients)
            if (c.state >= State::kConnected && c.number == drops[_dropsSeen].first) {
                // As the original's engine when the game drops a player:
                // told why, and the game told they've gone (ClientDisconnect).
                drop(c, drops[_dropsSeen].second, now);
            }
    // Configstrings that changed, to those that have had a game state.
    const std::vector<std::string> changes = configstringCommands(_configstrings, _game.configstrings());
    _configstrings = _game.configstrings();
    for (Client &c : _clients)
        if (c.state >= State::kPrimed)
            for (const std::string &change : changes)
                addReliable(c, change);
    // The game's commands to players.
    const auto &commands = _game.serverCommands();
    for (; _serverCommandsSeen < commands.size(); ++_serverCommandsSeen) {
        const auto &[to, text] = commands[_serverCommandsSeen];
        for (Client &c : _clients)
            if (c.state >= State::kPrimed && (to == -1 || to == c.number))
                addReliable(c, text);
    }
    // A map_restart: everyone is told, and is back in the level (the game
    // has put them there), as the original's server does.
    if (_game.restarts() != _restartsSeen) {
        _restartsSeen = _game.restarts();
        noteServerId();
        ++_levelCount; // the snapshots' flag 4
        for (Client &c : _clients) {
            if (c.state < State::kConnected)
                continue;
            addReliable(c, "map_restart");
            c.begun = true;
            c.deltaMessage = -1;
            if (c.state == State::kPrimed)
                c.state = State::kActive;
        }
    }
    // The masters are told at once when the server empties, gets its first
    // player or fills up, and every 5 minutes besides.
    {
        const int maxClients = std::clamp(_cvars.integer("sv_maxclients"), 1, 64);
        int players = 0;
        for (int32_t n = 0; n < maxClients; ++n)
            players += !_game.clientSlotFree(n);
        const bool changed = _lastPlayerCount >= 0 && players != _lastPlayerCount &&
                             (players == 0 || _lastPlayerCount == 0 || players == maxClients);
        heartbeat(now, _lastPlayerCount < 0 || changed);
        _lastPlayerCount = players;
    }
    const uint64_t timeoutMs = uint64_t(std::max(1, _cvars.integer("sv_timeout"))) * 1000;
    const int32_t levelTime = _game.levelTime();
    for (Client &c : _clients) {
        if (c.state == State::kZombie) {
            if (now - c.zombieSince > kZombieMs)
                c.state = State::kFree;
            continue;
        }
        if (c.state == State::kFree)
            continue;
        if (now - c.lastReceived > timeoutMs) {
            drop(c, "timed out", now);
            continue;
        }
        // As the original: snapshots to those loading the level too
        // (flagged not active), whose clocks they set.
        if (c.state >= State::kPrimed && levelTime >= c.nextSnapshotTime && c.download.name.empty()) {
            // Not while a message's pieces still wait, or before the rate
            // allows: skipped, the next flagged as delayed.
            if (!c.queued.empty() || rateWait(c, now) > 0) {
                c.rateDelayed = true;
            } else {
                c.nextSnapshotTime = levelTime + c.snapMs;
                sendSnapshot(c, now);
                c.rateDelayed = false;
            }
        } else if (c.state == State::kConnected && now - c.gamestateMs >= 1000) {
            // Something now and then while it loads (its commands, if
            // any), so a lost game state is noticed: its acknowledgement
            // passes it.
            c.gamestateMs = now;
            wire::MessageWriter w;
            writeReliable(c, w);
            transmit(c, w);
        }
    }
    return Error::success();
}

} // namespace reluva::idtech3::eliteforce
