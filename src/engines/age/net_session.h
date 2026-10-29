// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Multiplayer between copies of Reluva, over UDP: a host and up to seven
// players who join it, found on the local network by a broadcast or
// reached by address. The host keeps the session (its settings, the
// players, the chat, when the game starts) and passes each car's state on
// to the others. It's Reluva's own protocol: the original's (DirectPlay)
// isn't spoken. See docs/engines/age-multiplayer.md.

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
constexpr uint8_t kNetVersion = 2;
constexpr uint8_t kNoPlayer = 255;

// The Host Race Menu's events and its Cops & Robbers choices.
enum class NetMode : uint8_t { Cruise, Blitz, Checkpoint, Circuit, CopsRobbers };
enum class GoldMode : uint8_t { FreeForAll, CopsAndRobbers, RobberTeams };
enum class GoldLimit : uint8_t { None, Time, Points };

// What the host chose (the Host Race Menu and Host Options).
struct NetSettings {
    NetMode mode = NetMode::Cruise;
    uint8_t city = 0, race = 0, laps = 3;
    uint8_t timeOfDay = 1, weather = 0; // as the race files number them
    float peds = 0.5f;                  // pedestrian density, 0-1
    GoldMode gold = GoldMode::FreeForAll;
    GoldLimit limit = GoldLimit::Points;
    uint16_t limitValue = 5; // minutes, or points
    uint16_t goldMass = 454; // kg the car carrying the gold takes on (the game's Half Ton)
    uint8_t maxPlayers = kMaxNetPlayers;
    bool teams() const { return mode == NetMode::CopsRobbers && gold != GoldMode::FreeForAll; }
};

struct NetPlayer {
    uint8_t id = 0;
    std::string name;
    std::string car; // its base name ("vpbug")
    uint8_t paint = 0;
    uint8_t team = 0; // in the team modes: 0 or 1
    bool ready = false, loaded = false;
    bool bot = false;  // a computer player, driven on the host's machine
    uint16_t ping = 0; // milliseconds there and back, as it last said
    // Where the host reaches it, passed on so another can take over as
    // host (none for the host and computer players).
    net::Address address;
    bool cheater = false; // the host has turned down too much of what its machine said
};

struct NetChatLine {
    uint32_t seq = 0;
    std::string from, text;
};

// A car's state as its driver sends it, many times a second.
struct NetCarState {
    float clock = 0; // the race clock when it was so
    Vec3 position{}, velocity{};
    std::array<float, 4> rotation{0, 0, 0, 1}; // a quaternion (x, y, z, w)
    float steer = 0;                           // the front wheels' angle, radians
    std::array<float, 4> compression{};        // each wheel's, metres
    uint8_t lamps = 0;                         // kLamp* bits
    uint16_t next = 0;                         // its next checkpoint
    float finishTime = -1;                     // when it finished, if it has
    // For its sound and look on the others' machines.
    float rpm = 0, throttle = 0;
    float damage = 0;             // 0..1 of the way to wrecked
    uint8_t dents = 0;            // how many times its body's been dented (counting on, round past 255)
    std::array<Vec3, 4> dentAt{}; // the last four, the newest first, in the car's frame
    // Counted on each time it's put back (fallen out, stuck): a jump
    // then is no trick.
    uint8_t resets = 0;
};
enum : uint8_t { kLampHead = 1, kLampBrake = 2, kLampReverse = 4, kLampSiren = 8 };

// A rotation as a quaternion and back.
std::array<float, 4> quaternionOf(const Mat3& m);
Mat3 matrixOfQuaternion(const std::array<float, 4>& q);
// A car's state at `clock` from the ones received (in clock order):
// between the two either side, or carried on from the last for up to
// half a second. False if there's nothing to go on.
bool carStateAt(const std::deque<NetCarState>& states, float clock, NetCarState& out);

// Something that happened, for the menus and the game to say.
struct NetNotice {
    enum class Kind {
        Joined,      // `name` has joined
        Left,        // `name` has left the game
        HostQuit,    // the host has quit (or stopped answering)
        Ejected,     // the host ejected us
        Refused,     // the host refused us (`reason`)
        Started,     // the host started the game: load it
        BackToLobby, // the game's over: back to the lobby
        HostMoved,   // the host went: `name` is host now
        Suspect,     // the host turned down what `name`'s machine said of its car
    };
    Kind kind;
    std::string name;
    int reason = 0; // Refused: kRefused*
};
enum : int { kRefusedVersion = 1, kRefusedFull = 2, kRefusedPlaying = 3, kRefusedPassword = 4, kRefusedTimeout = 5 };

// A session as the host or a player sees it.
class NetSession {
public:
    enum class Phase : uint8_t { Lobby, Loading, Playing };

    // Hosts a session on `socket` (bound to the port players reach).
    static std::unique_ptr<NetSession> host(std::unique_ptr<net::UdpSocket> socket, const std::string& name,
                                            const NetSettings& settings, const std::string& password = {});
    // Joins the host at `address`; until it answers, joining() is true.
    static std::unique_ptr<NetSession> join(std::unique_ptr<net::UdpSocket> socket, const net::Address& address,
                                            const std::string& name, const std::string& password = {});
    ~NetSession();

    // Reads what's arrived and sends what's due; call every frame.
    void update(uint64_t nowMs);
    // Says goodbye. The host hands the session to the player who's been
    // in it longest, if there are two players left to play on.
    void leave();

    bool isHost() const { return host_; }
    uint8_t hostId() const { return hostId_; }
    bool joining() const { return !host_ && myId_ == kNoPlayer && !closed_; }
    bool closed() const { return closed_; } // left, refused, ejected or the host gone
    uint8_t myId() const { return myId_; }
    Phase phase() const { return phase_; }
    // Each start begins a new round; the game it starts goes by its seed.
    uint32_t round() const { return round_; }
    uint32_t seed() const { return seed_; }
    const NetSettings& settings() const { return settings_; }
    const std::vector<NetPlayer>& players() const { return players_; }
    const NetPlayer* player(uint8_t id) const;
    const NetPlayer* me() const { return player(myId_); }
    const std::deque<NetChatLine>& chat() const { return chat_; }
    std::vector<NetNotice> takeNotices();
    const std::string& sessionName() const { return sessionName_; }

    // The lobby. Any player: their car, team, readiness and chat.
    void setCar(const std::string& car, uint8_t paint);
    void setTeam(uint8_t team);
    void setReady(bool ready);
    void say(const std::string& text);
    // The host: the settings, ejecting a player, starting and ending.
    void setSettings(const NetSettings& settings);
    void eject(uint8_t id);
    // The host's computer players: always ready, sides evened up like
    // anyone's; kNoPlayer if the session's full.
    uint8_t addBot(const std::string& name, const std::string& car);
    void removeBots();
    bool everyoneReady() const; // every player but the host
    Error start(uint64_t nowMs);
    void backToLobby();

    // The game. Each says when it's loaded; the host then counts down
    // three seconds for everyone: clock() is the race clock (negative in
    // the countdown) or 0 until then.
    void setLoaded();
    bool counting() const { return goMs_ != 0 || clockKnown_; }
    float clock(uint64_t nowMs) const;
    // Our car, sent on to the others.
    void sendCar(const NetCarState& state);
    // The host: a computer player's car, sent on as anyone's.
    void sendBotCar(uint8_t id, const NetCarState& state);
    // The others' cars, by player, oldest first.
    const std::map<uint8_t, std::deque<NetCarState>>& cars() const { return cars_; }
    // The host's checks: how many of a player's car states it turned down
    // (moving faster than a car can, or stamped ahead of the clock).
    int rejected(uint8_t id) const;
    // The host's word on the game (the gold, the scores; the race over),
    // passed to everyone as it is.
    void setGameState(const std::vector<uint8_t>& state, bool over);
    const std::vector<uint8_t>& gameState() const { return gameState_; }
    bool over() const { return over_; }

private:
    NetSession() = default;
    struct Peer; // a player's address and when it was last heard (host)

    void receive(uint64_t nowMs);
    void handleHost(const net::Address& from, const std::vector<uint8_t>& data, uint64_t nowMs);
    void handlePlayer(const net::Address& from, const std::vector<uint8_t>& data, uint64_t nowMs);
    void sendLobby(uint64_t nowMs);
    void sendStatus(uint64_t nowMs);
    void sendGame(uint64_t nowMs);
    void relayCars();
    void dropPlayer(uint8_t id, bool notify);
    void storeCar(uint8_t id, const NetCarState& s);
    bool plausible(uint8_t id, const NetCarState& s); // the host's check of a player's car
    void hostGone(uint64_t nowMs, uint8_t successor); // the host left or went quiet
    void becomeHost(uint64_t nowMs);
    void notice(NetNotice::Kind kind, const std::string& name = {}, int reason = 0);
    void addChat(const std::string& from, const std::string& text);
    NetPlayer* mutablePlayer(uint8_t id);
    uint8_t freeId() const;
    uint8_t teamWithFewer() const;
    bool silentTooLong(uint64_t heardMs, uint64_t nowMs) const;

    std::unique_ptr<net::UdpSocket> socket_;
    bool host_ = false, closed_ = false;
    uint8_t hostId_ = 0;
    net::Address hostAddress_;
    std::string sessionName_, password_;
    uint8_t myId_ = kNoPlayer;
    Phase phase_ = Phase::Lobby;
    uint32_t round_ = 1, seed_ = 0;
    NetSettings settings_;
    std::vector<NetPlayer> players_;
    std::deque<NetChatLine> chat_;
    uint32_t chatSeq_ = 0;
    std::vector<NetNotice> notices_;
    std::map<uint8_t, std::deque<NetCarState>> cars_;
    NetCarState myCar_;
    bool haveMyCar_ = false;
    std::vector<uint8_t> gameState_;
    bool over_ = false;

    // The host's.
    std::vector<std::unique_ptr<Peer>> peers_;
    struct Challenge {
        std::array<uint8_t, 16> nonce{};
        uint64_t sentMs = 0;
    };
    std::map<net::Address, Challenge> challenges_; // asked to prove the password
    uint32_t nonceCount_ = 0;
    std::array<uint8_t, 16> secret_{}; // the host's own, for its challenges
    std::map<uint8_t, int> rejected_;
    uint64_t loadingSinceMs_ = 0;
    uint64_t goMs_ = 0; // when the race clock reaches 0

    // A player's.
    NetPlayer wish_; // our name, car, team and readiness, as we last asked
    uint64_t lastHeardMs_ = 0, joinSentMs_ = 0, joinStartMs_ = 0;
    std::array<uint8_t, 16> proof_{}; // the password, proved for the host's challenge
    bool haveProof_ = false;
    std::deque<std::string> outbox_; // chat not yet taken by the host
    uint32_t outSeq_ = 0;            // the sequence number of outbox_.front()
    bool teamChosen_ = false;        // else the team's the host's choice
    uint32_t lastPhaseRound_ = 0;
    uint64_t echoMs_ = 0; // the host's echo of our last status's time
    uint16_t rtt_ = 0;
    float clockOffset_ = 0; // the host's clock minus ours, in seconds
    bool clockKnown_ = false;

    uint64_t nowMs_ = 0, lastLobbyMs_ = 0, lastStatusMs_ = 0, lastGameMs_ = 0, lastCarsMs_ = 0;
};

// Sessions found on the local network (a broadcast to kNetPort), or at
// an address asked after.
class NetBrowser {
public:
    struct Found {
        net::Address address;
        std::string name;
        NetMode mode = NetMode::Cruise;
        uint8_t city = 0, players = 0, maxPlayers = 0;
        bool playing = false, password = false, sameVersion = true;
        uint16_t ping = 0;
        uint64_t heardMs = 0;
    };
    explicit NetBrowser(std::unique_ptr<net::UdpSocket> socket) : socket_(std::move(socket)) {}
    // Where else to broadcast (System::broadcastAddresses).
    void setBroadcasts(std::vector<uint32_t> addresses) { broadcasts_ = std::move(addresses); }
    // Asks again every second; forgets what's stopped answering.
    void update(uint64_t nowMs);
    void ask(const net::Address& address) { asked_.push_back(address); }
    const std::vector<Found>& sessions() const { return found_; }

private:
    std::unique_ptr<net::UdpSocket> socket_;
    std::vector<uint32_t> broadcasts_;
    std::vector<net::Address> asked_;
    std::vector<Found> found_;
    uint64_t lastAskMs_ = 0;
};

} // namespace reluva::age
