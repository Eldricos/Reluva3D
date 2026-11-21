// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A meeting server for network play: two players who can't reach each
// other (both behind routers that pass nothing on) each reach it, it
// introduces them so they can try to reach each other directly (UDP hole
// punching), and passes their datagrams on where that fails. It only
// passes on what the games send, which they may encrypt end to end. See
// docs/common/meeting-server.md.

#include "common/net.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::net {

constexpr uint16_t kMeetingPort = 28530; // where a meeting server listens unless told otherwise

/**
 * The server: sessions published by their hosts, listed by game, and the
 * connections it introduces and relays. Driven by update(), on one thread.
 */
class MeetingServer {
public:
    /** A connection between a session's host and one who joined it. */
    struct Connection {
        uint32_t id = 0;
        uint32_t session = 0;
        Address host; // as the server sees each: its router's side
        Address joiner;
        uint64_t heardMs = 0;
        uint64_t relayed = 0; // datagrams passed on
    };
    /** A session as its host published it. */
    struct Session {
        uint32_t id = 0;
        std::string game; // the engine's or game's id: lists are per game
        std::string name;
        std::vector<uint8_t> info; // the game's own description, passed on as it is
        Address host;
        uint64_t heardMs = 0;
    };

    /** Serves on `socket`, which it owns. */
    explicit MeetingServer(std::unique_ptr<UdpSocket> socket);
    /** Answers what's arrived and forgets what's gone quiet. */
    void update(uint64_t nowMs);
    /** The sessions listed, by id. */
    const std::map<uint32_t, Session> &sessions() const { return _sessions; }
    /** The connections it has introduced and still keeps, by id. */
    const std::map<uint32_t, Connection> &connections() const { return _connections; }

private:
    void handle(const Address &from, const std::vector<uint8_t> &data, uint64_t nowMs);
    void expire(uint64_t nowMs);

    std::unique_ptr<UdpSocket> _socket;
    std::map<uint32_t, Session> _sessions;
    std::map<uint32_t, Connection> _connections;
    uint32_t _nextSession = 1;
    uint32_t _nextConnection = 1;
};

/**
 * A UDP socket that also reaches through a meeting server. Datagrams to
 * any address go out on the socket it wraps as they would; each
 * connection the server introduces gets an address of its own (see
 * isMeetingAddress()), and datagrams to it go straight to the other side
 * once the two have reached each other, else through the server. What
 * arrives from a connection comes in as from its address, whichever way it
 * came, so a game's own protocol needn't know.
 */
class MeetingSocket : public UdpSocket {
public:
    /** A session the server lists. */
    struct Listed {
        uint32_t id = 0;
        std::string name;
        std::vector<uint8_t> info;
    };
    /** A connection, as this side sees it. */
    struct Link {
        Address address;     // its meeting address, as the game uses it
        Address peer;        // the other side's, as the server saw it
        bool direct = false; // reached each other: no more through the server
        bool known = false;  // the server has said where the other side is
    };

    /**
     * Wraps `inner` (owned) for the meeting server at `server`, for the
     * game `game` (its sessions are listed apart from other games').
     */
    MeetingSocket(std::unique_ptr<UdpSocket> inner, const Address &server, std::string game);
    /** Closes the socket it wraps; a session published stays listed until the server forgets it. */
    ~MeetingSocket() override;

    /** Milliseconds from any fixed time; a steady clock unless set (tests). */
    void setClock(std::function<uint64_t()> clock) { _clock = std::move(clock); }

    /**
     * Lists the session at the server, and keeps it listed (and the way
     * to the server open) while called again every few seconds; `info` is
     * the game's own description of it, at most 1,000 bytes.
     */
    void publish(const std::string &name, const std::vector<uint8_t> &info);
    /** Takes the session off the server's list. */
    void unpublish();
    /** Whether the server has listed the session (publish() answered). */
    bool published() const { return _published; }

    /** Asks the server for the game's sessions; listed() has them once it answers. */
    void requestList();
    /** The sessions in the server's last answer to requestList(). */
    const std::vector<Listed> &listed() const { return _listed; }
    /** When the server last answered anything, 0 if never. */
    uint64_t heardFromServerMs() const { return _heardMs; }

    /**
     * Asks to be introduced to the host of session `id`.
     * @return The address to send the session's datagrams to: they wait
     * at the server until the host's side is known.
     */
    Address connect(uint32_t id);
    /** The connections, by meeting address. */
    const std::map<Address, Link> &links() const { return _links; }

    /** The wrapped socket's port. */
    uint16_t port() const override { return _inner->port(); }
    /**
     * Sends as the wrapped socket does, or, to a connection's address,
     * straight to the other side or through the server.
     * @return False for a connection the server hasn't introduced yet.
     */
    bool send(const Address &to, const std::vector<uint8_t> &data) override;
    /**
     * The next datagram for the game, a connection's coming as from its
     * address; the meeting server's own are dealt with on the way.
     */
    bool receive(Address &from, std::vector<uint8_t> &data) override;

private:
    struct State; // a connection's state beyond what links() shows
    uint64_t now() const;
    void tick();
    void control(const std::vector<uint8_t> &data);

    std::unique_ptr<UdpSocket> _inner;
    Address _server;
    std::string _game;
    std::function<uint64_t()> _clock;
    std::string _name;
    std::vector<uint8_t> _info;
    bool _publishing = false;
    bool _published = false;
    uint64_t _publishedMs = 0;
    uint64_t _heardMs = 0;
    std::vector<Listed> _listed;
    std::map<Address, Link> _links;
    std::map<uint32_t, std::unique_ptr<State>> _states; // by connection id
    std::vector<uint32_t> _waitingFor;                  // sessions asked to connect to, not yet answered
    uint64_t _lastTickMs = 0;
    uint64_t _connectSentMs = 0;
};

/** Whether `a` is a connection's meeting address: only a MeetingSocket reaches it. */
bool isMeetingAddress(const Address &a);

} // namespace reluva::net
