// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// DirectPlay's sessions between copies of Reluva, over UDP: what the
// DirectPlay stand-in (dplay.cpp) needs of a network, with no emulator in
// it. Reluva's own protocol, not DirectPlay's: the host keeps the session
// and passes on what its members do, and when it goes, one of the others
// takes over if the session allows (docs/common/x86/dplay.md).

#include "common/meeting.h"
#include "common/net.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace reluva::x86::dp4 {
class ReliableLink;
struct Player;
} // namespace reluva::x86::dp4

namespace reluva::x86::dplay {

/** A session as EnumSessions shows it: DPSESSIONDESC2's fields. */
struct SessionInfo {
    std::string guid; // its guidInstance, "{...}"
    std::string application;
    uint32_t flags = 0;
    uint32_t maxPlayers = 0; // 0: as many as come
    uint32_t players = 0;
    std::string name;
    std::string password;
    std::array<uint32_t, 4> user{};
};

/** A player or a group in a session. */
struct Member {
    bool group = false;
    std::string shortName;
    std::string longName;
    std::vector<uint8_t> data;
    uint32_t flags = 0;
    std::set<uint32_t> members; // a group's players
    uint32_t node = 0;          // the endpoint it belongs to; the host's is 1
};

/** One of DirectPlay's system messages, before it's laid out for the program. */
struct Event {
    /**
     * A player or group made or gone, a player added to a group or taken
     * out, the session lost, or this endpoint made its host.
     */
    enum Kind { kCreated, kDestroyed, kAdded, kRemoved, kSessionLost, kHost };
    Kind kind = kCreated;
    uint32_t id = 0;      // the player or group made or gone; the group joined or left
    uint32_t player = 0;  // the player joining or leaving a group
    Member member;        // the one made or gone
    uint32_t players = 0; // made: the session's players now
};

/** A message waiting to be received: a program's (from a player) or a system message (from 0). */
struct Received {
    uint32_t from = 0;
    uint32_t to = 0;
    std::vector<uint8_t> data;
    std::optional<Event> event;
};

/** What an endpoint needs of the network: its sockets and where hosts are. */
struct Transport {
    /** A socket on `port` (0: any); nullptr if it can't be had. */
    std::function<std::unique_ptr<net::UdpSocket>(uint16_t port)> open;
    /** Where a search goes with no address given: each network's broadcast address. */
    std::vector<uint32_t> broadcast{0xffffffffu};
    /** Where hosts listen, when the program doesn't say. */
    uint16_t port = 47624;
    /** Milliseconds from any fixed time; a steady clock if not set. */
    std::function<uint64_t()> clock;
    /** How long a peer may stay silent before it's taken as gone. */
    uint32_t timeoutMs = 10000;
    /** Whether an endpoint has a thread of its own; without, update() must be called. */
    bool thread = true;
    /**
     * A meeting server (net::MeetingSocket) that lists hosted sessions and
     * introduces joiners to their hosts across routers; ip 0 for none.
     */
    net::Address meeting;
    /**
     * Asks the router to pass a port on to this machine, for as long as
     * what it gives back is kept: called when hosting on that port.
     */
    std::function<std::shared_ptr<void>(uint16_t port)> mapPort;
    /**
     * The system's TCP, for DirectPlay 4's own protocol ([MC-DPL4CS],
     * [MC-DPL4R]; dplay4_wire.h, dplay4_reliable.h): a search also asks
     * for the original program's games (DirectPlay on Windows), which can
     * be joined, and a host answers the original's searches and takes it
     * in. A session is of one protocol or the other, never both. None,
     * Reluva's own protocol only.
     */
    std::shared_ptr<net::Sockets> tcp;
};

/**
 * One DirectPlay object's end of the network: it finds sessions, hosts
 * one or joins one, and keeps its own copy of the session's players and
 * groups, which the host keeps in step. A thread of its own sends and
 * receives; every call may come from any thread.
 */
class Endpoint {
public:
    /** How a call went. */
    enum class Status {
        kOk,
        kConnecting,
        kNoSession,
        kRefused,
        kNotInSession,
        kInvalidMember,
        kNoSocket,
    };

    /** On `transport`; in no session until it hosts or joins one. */
    explicit Endpoint(Transport transport);
    /** Leaves its session and stops its thread. */
    ~Endpoint();
    Endpoint(const Endpoint &) = delete;
    Endpoint &operator=(const Endpoint &) = delete;

    /**
     * Asks for sessions of `application` (any, for GUID_NULL): at `where`,
     * or at each broadcast address when its ip is 0, on its port or the
     * transport's. Answers come into found().
     */
    void search(const std::string &application, const net::Address &where);
    /** The sessions that have answered a search in the last few seconds. */
    std::vector<SessionInfo> found();
    /** How many answers have come in all, to wait for the next. */
    uint64_t answers();

    /**
     * Hosts a session described by `info` (its guid made here and written
     * back), listening on `port` (0: the transport's).
     */
    Status host(SessionInfo &info, uint16_t port);
    /**
     * Joins the session found with `guid`: kConnecting until the host
     * answers, then kOk, or kRefused (full, closed or the wrong password);
     * asked again, says how it's going.
     */
    Status join(const std::string &guid, const std::string &password);
    /** Leaves the session, telling the others. */
    void leave();
    /** Whether it hosts its session. */
    bool hosting();
    /** The port its socket is on; 0 without one. */
    uint16_t port();
    /** Its session, if it's in one. */
    std::optional<SessionInfo> session();

    /** Makes a player or group of its own: its id. */
    Status create(const Member &member, uint32_t &id);
    /** Destroys one of its session's players or groups. */
    Status destroy(uint32_t id);
    /** Adds a player to a group, or takes it out. */
    Status membership(uint32_t group, uint32_t player, bool add);
    /** The session's players and groups. */
    std::map<uint32_t, Member> members();
    /**
     * Sends `data` from player `from` to a player, a group's players or
     * everyone (0), never back to `from`; `guaranteed` arrives, in order.
     */
    Status send(uint32_t from, uint32_t to, const std::vector<uint8_t> &data, bool guaranteed);
    /** Queues a message for its own players, as if it had come. */
    void post(Received message);
    /**
     * Takes (or with `peek` reads) the first waiting message that `match`
     * says yes to.
     */
    std::optional<Received> receive(const std::function<bool(const Received &)> &match, bool peek);
    /** The messages waiting that `match` says yes to: how many, and their bytes. */
    void waiting(const std::function<bool(const Received &)> &match, uint32_t &count, uint32_t &bytes);

    /** Sends, resends and receives what's due: its thread's work, for tests to call. */
    void update();

private:
    struct Link;
    struct Found {
        SessionInfo info;
        net::Address address; // none yet for one the meeting server listed
        uint64_t heardMs = 0;
        uint32_t listed = 0;   // its id at the meeting server, if it's there
        bool original = false; // the original program's, found by DirectPlay 4's own protocol
    };
    // A TCP connection of DirectPlay 4's: one to another machine's port,
    // or one it made to ours. An answer to a search is closed once sent.
    struct Stream {
        int socket = -1;
        net::Address peer;
        std::vector<uint8_t> pending; // to send, once connected
        std::vector<uint8_t> received;
        bool connecting = false;
        bool outgoing = false;
        bool closeWhenSent = false;
        uint64_t sinceMs = 0;
    };
    // Another machine in a session of DirectPlay 4's protocol: its system
    // player, where it takes TCP and UDP, and the reliable protocol's link
    // to it when the session uses that.
    struct Peer {
        uint32_t system = 0;
        net::Address stream;
        net::Address datagram;
        std::shared_ptr<dp4::ReliableLink> link;
        uint64_t heardMs = 0;
        uint64_t pingedMs = 0;
    };
    // At a host, a machine joining by DirectPlay 4: where its answer goes,
    // and the others still to say they've been told of it.
    struct Arriving {
        net::Address stream;
        std::set<uint32_t> awaiting;
        uint64_t sinceMs = 0;
    };
    enum class Step { kNone, kAskedId, kForwarded };
    enum class Role { kNone, kHost, kJoining, kJoined, kLost };

    uint64_t now() const;
    void openSearchSocket(const std::string &application);
    std::unique_ptr<net::UdpSocket> openSocket(uint16_t port, const std::string &application);
    void meetingWork();
    void datagram(const net::Address &from, const std::vector<uint8_t> &data, bool listener);
    void message(Link &link, const std::vector<uint8_t> &data);
    void hostMessage(Link &link, const std::vector<uint8_t> &data);
    void joinerMessage(const std::vector<uint8_t> &data);
    void sendReliable(Link &link, const std::vector<uint8_t> &message);
    void sendDatagram(const net::Address &to, const std::vector<uint8_t> &data);
    void toOthers(const std::vector<uint8_t> &message, uint32_t exceptNode);
    void dropNode(uint32_t node);
    void lost();
    void hostGone();
    void rejoin(const net::Address &host);
    void tellNode(uint32_t node, const net::Address &address, uint32_t exceptNode);
    void apply(const std::vector<uint8_t> &message, bool tell);
    void deliver(uint32_t from, uint32_t to, const std::vector<uint8_t> &data, bool guaranteed, uint32_t exceptNode);
    void sendMessage(Link &link, const std::vector<uint8_t> &message, bool guaranteed);
    void searchOriginal(const std::string &application, const net::Address &where);
    bool originalDatagram(const net::Address &from, const std::vector<uint8_t> &data);
    void originalWork();
    void closeOriginal();
    bool openOriginalListen();
    bool openDatagramSocket();
    void streamTo(const net::Address &to, const std::vector<uint8_t> &message);
    void flushStreams();
    void originalMessage(const std::vector<uint8_t> &message, uint32_t fromIp, Peer *peer);
    void originalDatagramFrom(const net::Address &from, const std::vector<uint8_t> &data);
    void originalDelivered(Peer &peer, uint16_t from, uint16_t to, const std::vector<uint8_t> &data);
    Peer *peerAt(const net::Address &datagram);
    Peer &addPeer(const dp4::Player &system, uint32_t fromIp, uint16_t streamPort);
    void dropPeer(uint32_t system);
    void toPeer(Peer &peer, const std::vector<uint8_t> &message, bool guaranteed);
    void flushLink(Peer &peer);
    uint16_t indexOf(uint32_t id) const;
    uint32_t idAt(uint16_t index) const;
    uint32_t newId();
    uint32_t systemOf(uint32_t node) const;
    dp4::Player record(uint32_t id, const Member &member) const;
    dp4::Player ourSystem(bool nameServer) const;
    void welcome(uint32_t system);
    void originalWait(std::unique_lock<std::mutex> &lock, const std::function<bool()> &done);
    Status createOriginal(std::unique_lock<std::mutex> &lock, const Member &member, uint32_t &id);
    void leaveOriginal();
    void originalUpdate();
    void closeLinks();
    SessionInfo current() const;

    Transport _transport;
    std::mutex _mutex;
    std::unique_ptr<net::UdpSocket> _socket;
    net::MeetingSocket *_meeting = nullptr; // _socket, when it's one
    // A host that took over: on the port the old one listened on, if it
    // could be had, answering searches there.
    std::unique_ptr<net::UdpSocket> _listen;
    // DirectPlay 4's own protocol, with Transport::tcp: where a searcher
    // takes the original's answers, a host's socket on its port for the
    // original's searches, and the connections under way.
    int _originalListen = -1;
    uint16_t _originalPort = 0;
    std::unique_ptr<net::UdpSocket> _originalEnum;
    std::vector<Stream> _streams;
    uint32_t _idSeed = 0;
    // A session of DirectPlay 4's protocol: its machines by their system
    // players, this one's system player and the host's, the UDP socket
    // they're reached on, and, joining, how far it's got.
    bool _original = false;
    std::map<uint32_t, Peer> _peers;
    uint32_t _system = 0;
    uint32_t _hostSystem = 0;
    std::unique_ptr<net::UdpSocket> _datagrams;
    Step _step = Step::kNone;
    uint64_t _stepMs = 0;
    net::Address _hostStream;
    std::string _password;
    std::map<uint32_t, Arriving> _arriving;
    std::set<uint32_t> _reserved;     // a host's ids given out for machines still joining
    std::optional<uint32_t> _granted; // a player's id the host has given; 0 when it refused
    bool _asking = false;
    uint16_t _uniqueness = 0;
    std::shared_ptr<void> _mapping; // the router's port, while hosting
    uint64_t _publishedMs = 0;      // when a host last published, or a seeker last asked for the list
    uint64_t _listHeardMs = 0;
    Role _role = Role::kNone;
    SessionInfo _session;
    std::map<uint32_t, Member> _members;
    uint32_t _node = 0;
    uint32_t _hostNode = 0;
    // The other endpoints' addresses, as the host sees them: where to go
    // when one of them takes over.
    std::map<uint32_t, net::Address> _nodes;
    // A new host's: the endpoints it waits for, and since when.
    std::map<uint32_t, uint64_t> _awaiting;
    // A joiner's, its new host's address not reachable: since when it's
    // looked for the session at the meeting server instead; 0 if not.
    uint64_t _seekingMs = 0;
    uint32_t _nextId = 1;
    uint32_t _nextNode = 2;
    std::map<std::string, Found> _found;
    uint64_t _answers = 0;
    std::vector<std::unique_ptr<Link>> _links; // a joiner's one, to its host; a host's, to each joiner
    std::deque<Received> _queue;
    bool _refused = false;
    bool _stopping = false;
    std::thread _thread;
};

} // namespace reluva::x86::dplay
