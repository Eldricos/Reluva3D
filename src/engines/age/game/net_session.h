// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Multiplayer between copies of Reluva, over UDP: a host and up to seven
// players who join it, found on the local network by a broadcast or
// reached by address. The host keeps the session (its settings, the
// players, the chat, when the game starts) and passes each car's state on
// to the others. It's Reluva's own protocol: the original's (DirectPlay)
// isn't spoken. See
// docs/engines/age/games/mm2/systems/network-protocol.md.

#include "common/crypto.h"
#include "common/net.h"
#include "common/types.h"
#include "engines/age/vecmath.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

constexpr uint16_t kNetPort = 28520; // where a host listens
constexpr int kMaxNetPlayers = 8;
constexpr uint8_t kNetVersion = 3;
constexpr uint8_t kNoPlayer = 255;

/** The Host Race Menu's events. */
enum class NetMode : uint8_t { kCruise, kBlitz, kCheckpoint, kCircuit, kCopsRobbers };
/** Cops & Robbers' games: everyone for themselves, or in two teams. */
enum class GoldMode : uint8_t { kFreeForAll, kCopsAndRobbers, kRobberTeams };
/** What ends a game of Cops & Robbers: nothing, a time, or a score. */
enum class GoldLimit : uint8_t { kNone, kTime, kPoints };

/** What the host chose (the Host Race Menu and Host Options). */
struct NetSettings {
    NetMode mode = NetMode::kCruise;
    uint8_t city = 0;
    uint8_t race = 0;
    uint8_t laps = 3;
    // as the race files number them
    uint8_t timeOfDay = 1;
    uint8_t weather = 0;
    float peds = 0.5f; // pedestrian density, 0-1
    GoldMode gold = GoldMode::kFreeForAll;
    GoldLimit limit = GoldLimit::kPoints;
    uint16_t limitValue = 5; // minutes, or points
    uint16_t goldMass = 454; // kg the car carrying the gold takes on (the game's Half Ton)
    uint8_t maxPlayers = kMaxNetPlayers;
    /** Whether it's Cops & Robbers in teams. */
    bool teams() const { return mode == NetMode::kCopsRobbers && gold != GoldMode::kFreeForAll; }
};

/** A player in the session, as the lobby lists them. */
struct NetPlayer {
    uint8_t id = 0;
    std::string name;
    std::string car; // its base name ("vpbug")
    uint8_t paint = 0;
    uint8_t team = 0; // in the team modes: 0 or 1
    bool ready = false;
    bool loaded = false;
    bool bot = false;  // a computer player, driven on the host's machine
    uint16_t ping = 0; // milliseconds there and back, as it last said
    // Where the host reaches it, passed on so another can take over as
    // host (none for the host and computer players).
    net::Address address;
    bool cheater = false; // the host has turned down too much of what its machine said
};

/** A line of the lobby's and the game's chat. */
struct NetChatLine {
    uint32_t seq = 0;
    std::string from;
    std::string text;
};

/** A car's state as its driver sends it, many times a second. */
struct NetCarState {
    float clock = 0; // the race clock when it was so
    Vec3 position{};
    Vec3 velocity{};
    std::array<float, 4> rotation{0, 0, 0, 1}; // a quaternion (x, y, z, w)
    float steer = 0;                           // the front wheels' angle, radians
    std::array<float, 4> compression{};        // each wheel's, metres
    uint8_t lamps = 0;                         // kLamp* bits
    uint16_t next = 0;                         // its next checkpoint
    float finishTime = -1;                     // when it finished, if it has
    // For its sound and look on the others' machines.
    float rpm = 0;
    float throttle = 0;
    float damage = 0;             // 0..1 of the way to wrecked
    uint8_t dents = 0;            // how many times its body's been dented (counting on, round past 255)
    std::array<Vec3, 4> dentAt{}; // the last four, the newest first, in the car's frame
    // Counted on each time it's put back (fallen out, stuck): a jump
    // then is no trick.
    uint8_t resets = 0;
};
/** NetCarState::lamps: the lamps lit. */
enum : uint8_t { kLampHead = 1, kLampBrake = 2, kLampReverse = 4, kLampSiren = 8 };

/** A rotation as a quaternion (x, y, z, w; of length 1) and back. */
///@{
std::array<float, 4> quaternionOf(const Mat3 &m);
/** A unit quaternion (x, y, z, w) as a rotation matrix. */
Mat3 matrixOfQuaternion(const std::array<float, 4> &q);
///@}
/**
 * A car's state at `clock` from the ones received (in clock order):
 * between the two either side, or carried on from the last for up to
 * half a second; the first, before it. False if there's nothing to go on.
 */
bool carStateAt(const std::deque<NetCarState> &states, float clock, NetCarState &out);

/** Something that happened, for the menus and the game to say. */
struct NetNotice {
    /** What happened. */
    enum class Kind {
        kJoined,      // `name` has joined
        kLeft,        // `name` has left the game
        kHostQuit,    // the host has quit (or stopped answering)
        kEjected,     // the host ejected us
        kRefused,     // the host refused us (`reason`)
        kStarted,     // the host started the game: load it
        kBackToLobby, // the game's over: back to the lobby
        kHostMoved,   // the host went: `name` is host now
        kSuspect,     // the host turned down what `name`'s machine said of its car
    };
    Kind kind;
    std::string name;
    int reason = 0; // Refused: kRefused*
};
/**
 * Why a player wasn't let in (NetNotice::reason): another version, the
 * session full, the game going (only older hosts say so), the wrong
 * password, or no answer.
 */
enum : int { kRefusedVersion = 1, kRefusedFull = 2, kRefusedPlaying = 3, kRefusedPassword = 4, kRefusedTimeout = 5 };

/** A session as the host or a player sees it. */
class NetSession {
public:
    /** Where the session is: in the lobby, loading the game, or playing it. */
    enum class Phase : uint8_t { kLobby, kLoading, kPlaying };

    /** Hosts a session on `socket` (bound to the port players reach); the host is player 0. */
    static std::unique_ptr<NetSession> host(std::unique_ptr<net::UdpSocket> socket, const std::string &name,
                                            const NetSettings &settings, const std::string &password = {});
    /**
     * Joins the host at `address`; until it answers, joining() is true.
     * Five seconds without a welcome closes it, refused with kRefusedTimeout.
     */
    static std::unique_ptr<NetSession> join(std::unique_ptr<net::UdpSocket> socket, const net::Address &address,
                                            const std::string &name, const std::string &password = {});
    ~NetSession();

    /** Reads what's arrived and sends what's due; call every frame. */
    void update(uint64_t nowMs);
    /**
     * Says goodbye. The host hands the session to the player who's been
     * in it longest, if there are two players left to play on.
     */
    void leave();

    /** Whether we're the host, and the host's player id. */
    ///@{
    bool isHost() const { return _host; }
    /** The host's player id. */
    uint8_t hostId() const { return _hostId; }
    ///@}
    /** Whether we've asked to join and the host hasn't let us in yet. */
    bool joining() const { return !_host && _myId == kNoPlayer && !_closed; }
    /** Whether it's over for us: left, refused, ejected or the host gone. */
    bool closed() const { return _closed; }
    /** Our player id (kNoPlayer while joining), and where the session is. */
    ///@{
    uint8_t myId() const { return _myId; }
    /** Where the session is, as the host last said. */
    Phase phase() const { return _phase; }
    ///@}
    /** Each start begins a new round; the game it starts goes by its seed. */
    ///@{
    uint32_t round() const { return _round; }
    /** The host's pick at each start, from which every machine's game goes the same way. */
    uint32_t seed() const { return _seed; }
    ///@}
    /** What the host chose, and the players, as the host last said. */
    ///@{
    const NetSettings &settings() const { return _settings; }
    /** Everyone in the session, the host and computer players too. */
    const std::vector<NetPlayer> &players() const { return _players; }
    ///@}
    /** A player by id, or ourselves; nullptr if there's no such player. */
    ///@{
    const NetPlayer *player(uint8_t id) const;
    /** Our own player, or nullptr until the host has let us in. */
    const NetPlayer *me() const { return player(_myId); }
    ///@}
    /** The last eight chat lines, oldest first. */
    const std::deque<NetChatLine> &chat() const { return _chat; }
    /** What's happened since last asked, oldest first. */
    std::vector<NetNotice> takeNotices();
    /** The session's name: the host's. */
    const std::string &sessionName() const { return _sessionName; }

    /** The lobby. Any player: their car, team, readiness and chat. */
    ///@{
    void setCar(const std::string &car, uint8_t paint);
    /** Picks our side in the team modes (0 or 1); once picked, the host no longer evens the sides with us. */
    void setTeam(uint8_t team);
    /** Says whether we're ready; the host doesn't wait for itself. */
    void setReady(bool ready);
    /** Adds a line to the chat, cut at 120 characters; an empty one is dropped. */
    void say(const std::string &text);
    ///@}
    /**
     * The host: the settings (in the lobby only), ejecting a player,
     * starting and ending.
     */
    ///@{
    void setSettings(const NetSettings &settings);
    /** The host throws player `id` out; not itself. */
    void eject(uint8_t id);
    /**
     * The host adds a computer player: always ready, sides evened up like
     * anyone's.
     * @return Its id; kNoPlayer if the session's full, or not in the lobby, or we're not the host.
     */
    uint8_t addBot(const std::string &name, const std::string &car);
    /** Takes out every computer player, without a notice. */
    void removeBots();
    /** Whether every player but the host is ready. */
    bool everyoneReady() const;
    /**
     * The host starts the game: everyone goes to loading it.
     * @return A failure if we're not the host in the lobby, or not everyone's ready.
     */
    Error start(uint64_t nowMs);
    /** The host ends the game and takes everyone back to the lobby, none of them ready. */
    void backToLobby();
    ///@}

    /**
     * Says we've loaded the game. Once everyone has (or a minute's gone),
     * the host counts down three seconds for everyone.
     */
    ///@{
    void setLoaded();
    /** Whether the countdown has begun, so clock() means something. */
    bool counting() const { return _goMs != 0 || _clockKnown; }
    /** The race clock in seconds: negative in the countdown, 0 until it's begun. */
    float clock(uint64_t nowMs) const;
    ///@}
    /** Our car, sent on to the others. */
    void sendCar(const NetCarState &state);
    /** The host: a computer player's car, sent on as anyone's. */
    void sendBotCar(uint8_t id, const NetCarState &state);
    /** The others' cars (computer players' too), by player, oldest first: the last two seconds at most. */
    const std::map<uint8_t, std::deque<NetCarState>> &cars() const { return _cars; }
    /**
     * Each car's top speed (m/s, by base name), so the host can hold a
     * player's car to its own and not only to what no car does. Given to
     * every session, as any player may take over as host.
     */
    void setTopSpeeds(std::map<std::string, float> speeds) { _topSpeeds = std::move(speeds); }
    /**
     * The fastest a player's car may be said to go, in m/s: its top speed with room for a slope or a knock,
     * and never past 110 (also what a car with no top speed given gets).
     */
    float speedLimit(uint8_t id) const;
    /**
     * The host's checks: how many of a player's car states it turned down
     * (moving faster than a car can, or stamped ahead of the clock).
     */
    int rejected(uint8_t id) const;
    /**
     * The host's word on the game (the gold, the scores; the race over),
     * passed to everyone as it is. Ignored but on the host.
     */
    ///@{
    void setGameState(const std::vector<uint8_t> &state, bool over);
    /**
     * The host's description of the session, as it answers a search on
     * the local network: what a meeting server lists for it too.
     * @param time The asker's time, sent back for its ping (0 for a listing).
     */
    std::vector<uint8_t> info(uint32_t time = 0) const;
    /** The host's word on the game, as it last gave it; empty until it has. */
    const std::vector<uint8_t> &gameState() const { return _gameState; }
    /** Whether the host has said the game's over. */
    bool over() const { return _over; }
    ///@}

private:
    NetSession() = default;
    struct Peer; // a player's address and when it was last heard (host)
    // The keys between us and another, and the counts taken from it.
    struct Link {
        crypto::Key with{}; // its public key, as they were made from
        crypto::Key send{};
        crypto::Key recv{};
        uint64_t newest = 0; // the highest count opened
        uint64_t seen = 0;   // bit n: newest - n was opened
        bool ready = false;
    };

    void receive(uint64_t nowMs);
    void sendWelcome(const Peer &peer);
    Link *link(uint8_t id);
    /** Sent sealed to player `toId` (or as it is, if it's sent so), else not at all. */
    void sendTo(const net::Address &to, uint8_t toId, const std::vector<uint8_t> &message);
    bool unseal(const std::vector<uint8_t> &data, std::vector<uint8_t> &plain, uint8_t &sender);
    void handleHost(const net::Address &from, const std::vector<uint8_t> &data, uint64_t nowMs);
    void handlePlayer(const net::Address &from, const std::vector<uint8_t> &data, uint64_t nowMs);
    void sendLobby(uint64_t nowMs);
    void sendStatus(uint64_t nowMs);
    void sendGame(uint64_t nowMs);
    void relayCars();
    void dropPlayer(uint8_t id, bool notify);
    void storeCar(uint8_t id, const NetCarState &s);
    bool plausible(uint8_t id, const NetCarState &s); // the host's check of a player's car
    void hostGone(uint64_t nowMs, uint8_t successor); // the host left or went quiet
    void becomeHost(uint64_t nowMs);
    void notice(NetNotice::Kind kind, const std::string &name = {}, int reason = 0);
    void addChat(const std::string &from, const std::string &text);
    NetPlayer *mutablePlayer(uint8_t id);
    uint8_t freeId() const;
    uint8_t teamWithFewer() const;
    bool silentTooLong(uint64_t heardMs, uint64_t nowMs) const;

    std::unique_ptr<net::UdpSocket> _socket;
    bool _host = false;
    bool _closed = false;
    uint8_t _hostId = 0;
    net::Address _hostAddress;
    std::string _sessionName;
    std::string _password;
    uint8_t _myId = kNoPlayer;
    Phase _phase = Phase::kLobby;
    uint32_t _round = 1;
    uint32_t _seed = 0;
    NetSettings _settings;
    std::vector<NetPlayer> _players;
    std::deque<NetChatLine> _chat;
    uint32_t _chatSeq = 0;
    std::vector<NetNotice> _notices;
    std::map<uint8_t, std::deque<NetCarState>> _cars;
    NetCarState _myCar;
    bool _haveMyCar = false;
    std::vector<uint8_t> _gameState;
    bool _over = false;

    // The host's.
    std::vector<std::unique_ptr<Peer>> _peers;
    struct Challenge {
        std::array<uint8_t, 16> nonce{};
        uint64_t sentMs = 0;
    };
    std::map<net::Address, Challenge> _challenges; // asked to prove the password
    uint32_t _nonceCount = 0;
    std::array<uint8_t, 16> _secret{}; // the host's own, for its challenges
    std::map<uint8_t, int> _rejected;
    std::map<std::string, float> _topSpeeds;
    crypto::Key _secretKey{}; // this session's, made at its start
    crypto::Key _publicKey{};
    std::map<uint8_t, crypto::Key> _publics; // every player's public key, by id
    std::map<uint8_t, Link> _links;
    uint64_t _sent = 0; // sealed datagrams sent, for each one's nonce
    uint64_t _loadingSinceMs = 0;
    uint64_t _goMs = 0; // when the race clock reaches 0

    // A player's.
    NetPlayer _wish; // our name, car, team and readiness, as we last asked
    uint64_t _lastHeardMs = 0;
    uint64_t _joinSentMs = 0;
    uint64_t _joinStartMs = 0;
    std::array<uint8_t, 16> _proof{}; // the password, proved for the host's challenge
    bool _haveProof = false;
    std::array<uint8_t, 16> _challengeNonce{}; // the host's challenge, for its proof back
    bool _haveChallenge = false;
    std::deque<std::string> _outbox; // chat not yet taken by the host
    uint32_t _outSeq = 0;            // the chat lines the host has taken: _outbox.front() is the next
    bool _teamChosen = false;        // else the team's the host's choice
    uint32_t _lastPhaseRound = 0;
    uint64_t _echoMs = 0; // the host's echo of our last status's time
    uint16_t _rtt = 0;
    float _clockOffset = 0; // the host's clock minus ours, in seconds
    bool _clockKnown = false;

    uint64_t _nowMs = 0;
    uint64_t _lastLobbyMs = 0;
    uint64_t _lastStatusMs = 0;
    uint64_t _lastGameMs = 0;
    uint64_t _lastCarsMs = 0;
};

/**
 * Sessions found on the local network (a broadcast to kNetPort), or at
 * an address asked after.
 */
class NetBrowser {
public:
    /** A session that answered. */
    struct Found {
        net::Address address;
        std::string name;
        NetMode mode = NetMode::kCruise;
        uint8_t city = 0;
        uint8_t players = 0;
        uint8_t maxPlayers = 0;
        bool playing = false;
        bool password = false;
        bool sameVersion = true;
        uint16_t ping = 0;
        uint64_t heardMs = 0;
        uint32_t time = 0;      // the asker's time it sent back
        uint32_t meetingId = 0; // listed by a meeting server: its session there (0: found on the network)
    };
    /** Reads a host's description (Info) into `f`; false if it isn't one. */
    static bool parseInfo(const std::vector<uint8_t> &data, Found &f);
    /** Asks and listens on `socket`, which it keeps. */
    explicit NetBrowser(std::unique_ptr<net::UdpSocket> socket) : _socket(std::move(socket)) {}
    /** Where else to broadcast (System::broadcastAddresses). */
    void setBroadcasts(std::vector<uint32_t> addresses) { _broadcasts = std::move(addresses); }
    /** Asks again every second; forgets what's stopped answering (for four seconds). */
    void update(uint64_t nowMs);
    /** Asks after a session at `address` too, every second from then on. */
    void ask(const net::Address &address) { _asked.push_back(address); }
    /** The sessions that have answered, in the order they first did. */
    const std::vector<Found> &sessions() const { return _found; }

private:
    std::unique_ptr<net::UdpSocket> _socket;
    std::vector<uint32_t> _broadcasts;
    std::vector<net::Address> _asked;
    std::vector<Found> _found;
    uint64_t _lastAskMs = 0;
};

} // namespace reluva::age
