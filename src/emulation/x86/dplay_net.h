// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/net.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::x86::dplay {

/** Where a session is hosted and looked for, UDP and TCP, unless told otherwise. */
constexpr uint16_t kPort = 28541;

/** A session as its host describes it (DPSESSIONDESC2's values). */
struct SessionInfo {
    std::string instance; // GUIDs as ComHost::guid writes them
    std::string application;
    std::string name;
    std::string password; // the host's alone: others learn only whether there is one
    bool hasPassword = false;
    uint32_t flags = 0;
    uint32_t maxPlayers = 0;
    uint32_t currentPlayers = 0;
    uint32_t user[4] = {};
};

/** A player as every program in the session sees it. */
struct PlayerInfo {
    uint32_t id = 0;
    uint32_t flags = 0;
    std::string shortName;
    std::string longName;
    std::vector<uint8_t> data; // what every player sees (DPSET_REMOTE)
};

/** A session found by search(): where its host is, and what it says of it. */
struct FoundSession {
    net::Address host; // its TCP port
    SessionInfo info;
    std::chrono::steady_clock::time_point heard;
};

/** What the session's other programs did, as poll() reports it. */
struct Event {
    /**
     * What happened: a player joined, left, or changed its data or name; a
     * message; the session's settings; the session lost; or this program
     * made its host, the host gone.
     */
    enum Kind { kPlayerAdded, kPlayerRemoved, kPlayerData, kMessage, kSession, kSessionLost, kPlayerName, kBecameHost };
    Kind kind = kMessage;
    PlayerInfo player; // added; removed and data: its id (and data)
    uint32_t from = 0; // a message's
    uint32_t to = 0;   // 0: every player but its sender
    std::vector<uint8_t> bytes;
    SessionInfo session;
};

/**
 * One DirectPlay object's network, between copies of Reluva: Reluva's own
 * protocol, not Microsoft's (docs/common/x86/dplay.md). Sessions are
 * found by UDP (a question broadcast, or sent to a host, on kPort) and
 * played over TCP to the host, which passes on what its players' programs
 * send each other. Nothing waits but find() and join(), which wait up to
 * the time they're given.
 */
class Node {
public:
    /**
     * A node on `sockets`: questions go to each address in `broadcast` and
     * to the machine itself; sessions are hosted and looked for on `port`.
     */
    Node(std::shared_ptr<net::Sockets> sockets, std::vector<uint32_t> broadcast, uint16_t port = kPort);
    ~Node();

    /**
     * Hosts `session`: listens on the port.
     * @return False if the port is taken, or there's no network.
     */
    bool host(const SessionInfo &session);
    /**
     * Asks for sessions of `application` ("" or GUID_NULL: any), at `to`
     * (a name or a.b.c.d; "" the local network), unless asked within the
     * last second; with `waitMs`, waits that long for answers.
     */
    void find(const std::string &to, const std::string &application, int waitMs);
    /** The sessions heard of in the last five seconds. */
    std::vector<FoundSession> found();
    /**
     * Joins a session find() found, by its instance: connects and waits up
     * to `waitMs` to be let in.
     * @return 0 when let in: the session and its players (`players`), and
     *         the first ID for this program's players (`firstId`); else
     *         DirectPlay's error (no session, refused, wrong password...).
     */
    uint32_t join(const std::string &instance, const std::string &password, int waitMs, SessionInfo &session,
                  std::vector<PlayerInfo> &players, uint32_t &firstId);
    /** Whether it hosts a session or is in one. */
    bool open() const { return _hosting || _hostConnection >= 0; }

    /** Tells the session's other programs of a player of this one's: made, gone, its data changed. */
    ///@{
    void addPlayer(const PlayerInfo &p);
    void removePlayer(uint32_t id);
    void setPlayerData(uint32_t id, const std::vector<uint8_t> &data);
    /** One of this program's players renamed, for the other programs. */
    void setPlayerName(uint32_t id, const std::string &shortName, const std::string &longName);
    ///@}
    /** A message from a player of this program's to player `to` (0: every one), for the others' players. */
    void send(uint32_t from, uint32_t to, const std::vector<uint8_t> &bytes);
    /** The host's new description of its session, for the others. */
    void setSession(const SessionInfo &session);

    /** Reads what's come (and answers questions, as a host), and gives what happened. */
    std::vector<Event> poll();

    /**
     * The frames and datagrams' layout, for tests: a TCP frame's kind,
     * after its size.
     */
    enum Frame : uint8_t { kJoin = 1, kWelcome, kAdd, kRemove, kData, kMessage, kSession, kName, kNext, kRejoin };

private:
    struct Connection {
        int socket = -1;
        std::vector<uint8_t> in;
        std::vector<uint8_t> out;
        bool joined = false;
        uint32_t firstId = 0; // the IDs its players have, as the host gave it
        net::Address address; // its program's, as the host accepted it
    };
    void frame(Connection &to, Frame kind, const std::vector<uint8_t> &payload);
    void flush(Connection &c);
    // Reads what's come on `c`: false if it's closed.
    bool read(Connection &c);
    // The next whole frame in `c.in`, taken out; false if there's none.
    static bool nextFrame(Connection &c, Frame &kind, std::vector<uint8_t> &payload);
    void handle(int index, Frame kind, const std::vector<uint8_t> &payload, std::vector<Event> &events);
    void dropConnection(int index, std::vector<Event> &events);
    void answerQuestions();
    void readAnswers();
    // To every joined connection but `except` (-1: all).
    void forward(Frame kind, const std::vector<uint8_t> &payload, int except);
    int ownerOf(uint32_t id) const;
    // The host: who takes over if it goes, for every program.
    void announceSuccessor();
    // A joining program, its host gone: the session goes on, the successor
    // hosting it, or it's lost.
    void migrate(std::vector<Event> &events);

    std::shared_ptr<net::Sockets> _net;
    std::vector<uint32_t> _broadcast;
    uint16_t _port;
    int _udp = -1;
    int _listen = -1;
    bool _hosting = false;
    SessionInfo _session;
    std::map<int, Connection> _connections; // by index; the host's own is -1
    int _nextConnection = 0;
    int _hostConnection = -1;                // a joining program's connection to its host
    std::map<uint32_t, int> _owners;         // remote players' connections (host) ...
    std::map<uint32_t, PlayerInfo> _players; // every player (a joining program's too, to host if it must)
    uint32_t _firstId = 0;                   // a joining program's first ID for its players
    net::Address _successor;                 // the program that takes over if the host goes
    uint32_t _successorFirstId = 0;          // its first ID; 0 for none
    uint32_t _nextBlock = 1;
    std::map<std::string, FoundSession> _found;
    std::chrono::steady_clock::time_point _asked;
    bool _askedOnce = false;
};

} // namespace reluva::x86::dplay
