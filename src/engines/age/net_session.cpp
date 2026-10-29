// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Reluva's multiplayer protocol: small datagrams, each "RM", a type and
// its fields (little-endian). Nothing is sent once and trusted to arrive:
// the lobby, the game's state and the cars are sent again and again, the
// newest replacing the last, and chat lines are sent until the host says
// it has them. See docs/engines/age-multiplayer.md.

#include "engines/age/net_session.h"

#include "common/md5.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace reluva::age {

namespace {

enum Msg : uint8_t {
    kQuery = 1, // version, the asker's time (ms)
    kInfo,      // version, name, mode, city, players, most, flags, the asker's time
    kJoin,      // version, name, password
    kWelcome,   // the player's id
    kRefuse,    // why (kRefused*)
    kStatus,    // a player's: id, round, name, car, paint, team, flags, ping, time, chat seq, chat text
    kLobby,     // the host's: the session, the players, the chat; for the player: its id, chat taken, echo
    kCar,       // a player's car
    kCars,      // the host's: the others' cars
    kGame,      // the host's: the race clock, over, the game's state
    kLeave,     // a player is going
    kKick,      // the host ejects the player
    kBye,       // the host is going: who's host now (or none)
    kChallenge, // the host's: prove the password with this
};

constexpr uint64_t kLobbyEveryMs = 250, kStatusEveryMs = 250, kGameEveryMs = 100, kCarsEveryMs = 50;
constexpr uint64_t kTimeoutMs = 5000;         // silent this long: gone
constexpr uint64_t kLoadingTimeoutMs = 60000; // while the game loads (a city takes a while)
constexpr uint64_t kJoinEveryMs = 500, kJoinTimeoutMs = 5000;
constexpr size_t kChatKept = 8;
constexpr size_t kStatesKept = 32;
constexpr float kFastest = 110;               // m/s no car goes (the fastest top out near 70)
constexpr float kAheadOfClock = 1;            // seconds a car's state may be stamped past the host's clock
constexpr int kCheater = 5;                   // states turned down before a player's called a cheater
constexpr uint32_t kChatUnknown = 0xFFFFFFFF; // a new host doesn't know what chat it has of a player

class Writer {
public:
    explicit Writer(uint8_t type) : out{'R', 'M', type} {}
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        u16(uint16_t(v));
        u16(uint16_t(v >> 16));
    }
    void f32(float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        u32(u);
    }
    void str(const std::string& s) {
        const size_t n = std::min<size_t>(s.size(), 255);
        u8(uint8_t(n));
        out.insert(out.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
    }
    void vec(const Vec3& v) {
        for (float f : v)
            f32(f);
    }
    std::vector<uint8_t> out;
};

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& d) : data(d), at(3) {}
    uint8_t u8() {
        if (at + 1 > data.size()) {
            ok = false;
            return 0;
        }
        return data[at++];
    }
    uint16_t u16() { return uint16_t(u8() | u8() << 8); }
    uint32_t u32() { return uint32_t(u16()) | uint32_t(u16()) << 16; }
    float f32() {
        const uint32_t u = u32();
        float v;
        std::memcpy(&v, &u, 4);
        return std::isfinite(v) ? v : 0;
    }
    std::string str() {
        const size_t n = u8();
        if (at + n > data.size()) {
            ok = false;
            return {};
        }
        std::string s(data.begin() + std::ptrdiff_t(at), data.begin() + std::ptrdiff_t(at + n));
        at += n;
        return s;
    }
    Vec3 vec() {
        Vec3 v;
        for (float& f : v)
            f = f32();
        return v;
    }
    const std::vector<uint8_t>& data;
    size_t at;
    bool ok = true;
};

uint8_t typeOf(const std::vector<uint8_t>& d) { return d.size() >= 3 && d[0] == 'R' && d[1] == 'M' ? d[2] : 0; }

void writeCar(Writer& w, const NetCarState& s) {
    w.f32(s.clock);
    w.vec(s.position);
    w.vec(s.velocity);
    for (float q : s.rotation)
        w.u16(uint16_t(int16_t(std::lround(std::clamp(q, -1.0f, 1.0f) * 32767))));
    w.u16(uint16_t(int16_t(std::lround(std::clamp(s.steer, -3.0f, 3.0f) * 10000))));
    for (float c : s.compression)
        w.u8(uint8_t(std::clamp(std::lround(c * 100), 0L, 255L)));
    w.u8(s.lamps);
    w.u16(s.next);
    w.f32(s.finishTime);
    w.u16(uint16_t(std::clamp(std::lround(s.rpm), 0L, 65535L)));
    w.u8(uint8_t(std::clamp(std::lround(s.throttle * 255), 0L, 255L)));
    w.u16(uint16_t(std::clamp(std::lround(s.damage * 65535), 0L, 65535L)));
    w.u8(s.dents);
    for (const Vec3& d : s.dentAt) // 5 cm steps, +-6 m
        for (float f : d)
            w.u8(uint8_t(int8_t(std::clamp(std::lround(f * 20), -127L, 127L))));
    w.u8(s.resets);
}

NetCarState readCar(Reader& r) {
    NetCarState s;
    s.clock = r.f32();
    s.position = r.vec();
    s.velocity = r.vec();
    float n = 0;
    for (float& q : s.rotation) {
        q = float(int16_t(r.u16())) / 32767;
        n += q * q;
    }
    if (n > 1e-6f)
        for (float& q : s.rotation)
            q /= std::sqrt(n);
    else
        s.rotation = {0, 0, 0, 1};
    s.steer = float(int16_t(r.u16())) / 10000;
    for (float& c : s.compression)
        c = float(r.u8()) / 100;
    s.lamps = r.u8();
    s.next = r.u16();
    s.finishTime = r.f32();
    s.rpm = float(r.u16());
    s.throttle = float(r.u8()) / 255;
    s.damage = float(r.u16()) / 65535;
    s.dents = r.u8();
    for (Vec3& d : s.dentAt)
        for (float& f : d)
            f = float(int8_t(r.u8())) / 20;
    s.resets = r.u8();
    return s;
}

// The proof of a password: HMAC-MD5 of the host's challenge and the
// player's name, keyed by the password.
std::array<uint8_t, 16> passwordProof(const std::string& password, const std::array<uint8_t, 16>& nonce,
                                      const std::string& name) {
    std::vector<uint8_t> message(nonce.begin(), nonce.end());
    message.insert(message.end(), name.begin(), name.begin() + std::ptrdiff_t(std::min<size_t>(name.size(), 255)));
    return hmacMd5(password.data(), password.size(), message.data(), message.size());
}

void writeSettings(Writer& w, const NetSettings& s) {
    w.u8(uint8_t(s.mode));
    w.u8(s.city);
    w.u8(s.race);
    w.u8(s.laps);
    w.u8(s.timeOfDay);
    w.u8(s.weather);
    w.f32(s.peds);
    w.u8(uint8_t(s.gold));
    w.u8(uint8_t(s.limit));
    w.u16(s.limitValue);
    w.u16(s.goldMass);
    w.u8(s.maxPlayers);
}

NetSettings readSettings(Reader& r) {
    NetSettings s;
    s.mode = NetMode(std::min<uint8_t>(r.u8(), uint8_t(NetMode::CopsRobbers)));
    s.city = r.u8();
    s.race = r.u8();
    s.laps = r.u8();
    s.timeOfDay = r.u8();
    s.weather = r.u8();
    s.peds = std::clamp(r.f32(), 0.0f, 1.0f);
    s.gold = GoldMode(std::min<uint8_t>(r.u8(), uint8_t(GoldMode::RobberTeams)));
    s.limit = GoldLimit(std::min<uint8_t>(r.u8(), uint8_t(GoldLimit::Points)));
    s.limitValue = r.u16();
    s.goldMass = r.u16();
    s.maxPlayers = std::clamp<uint8_t>(r.u8(), 1, kMaxNetPlayers);
    return s;
}

Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

} // namespace

std::array<float, 4> quaternionOf(const Mat3& m) {
    // The matrix's columns are where X, Y and Z go: element (row r,
    // column c) is the column's r'th.
    const float r00 = m.x[0], r10 = m.x[1], r20 = m.x[2];
    const float r01 = m.y[0], r11 = m.y[1], r21 = m.y[2];
    const float r02 = m.z[0], r12 = m.z[1], r22 = m.z[2];
    std::array<float, 4> q;
    const float trace = r00 + r11 + r22;
    if (trace > 0) {
        const float s = std::sqrt(trace + 1) * 2;
        q = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, s / 4};
    } else if (r00 > r11 && r00 > r22) {
        const float s = std::sqrt(1 + r00 - r11 - r22) * 2;
        q = {s / 4, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
    } else if (r11 > r22) {
        const float s = std::sqrt(1 + r11 - r00 - r22) * 2;
        q = {(r01 + r10) / s, s / 4, (r12 + r21) / s, (r02 - r20) / s};
    } else {
        const float s = std::sqrt(1 + r22 - r00 - r11) * 2;
        q = {(r02 + r20) / s, (r12 + r21) / s, s / 4, (r10 - r01) / s};
    }
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (float& v : q)
        v /= n;
    return q;
}

Mat3 matrixOfQuaternion(const std::array<float, 4>& q) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat3 m;
    m.x = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)};
    m.y = {2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)};
    m.z = {2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
    return m;
}

bool carStateAt(const std::deque<NetCarState>& states, float clock, NetCarState& out) {
    if (states.empty())
        return false;
    if (clock <= states.front().clock) {
        out = states.front();
        return true;
    }
    for (size_t i = 0; i + 1 < states.size(); ++i) {
        const NetCarState &a = states[i], &b = states[i + 1];
        if (clock > b.clock)
            continue;
        const float span = b.clock - a.clock;
        const float t = span > 1e-6f ? (clock - a.clock) / span : 1;
        out = t < 0.5f ? a : b;
        out.clock = clock;
        out.position = lerp(a.position, b.position, t);
        out.velocity = lerp(a.velocity, b.velocity, t);
        out.steer = a.steer + (b.steer - a.steer) * t;
        for (size_t k = 0; k < 4; ++k)
            out.compression[k] = a.compression[k] + (b.compression[k] - a.compression[k]) * t;
        // The shorter way round, then normalised.
        const float d = a.rotation[0] * b.rotation[0] + a.rotation[1] * b.rotation[1] + a.rotation[2] * b.rotation[2] +
                        a.rotation[3] * b.rotation[3];
        const float sign = d < 0 ? -1.0f : 1.0f;
        float n = 0;
        for (size_t k = 0; k < 4; ++k) {
            out.rotation[k] = a.rotation[k] + (sign * b.rotation[k] - a.rotation[k]) * t;
            n += out.rotation[k] * out.rotation[k];
        }
        n = std::sqrt(n);
        for (float& v : out.rotation)
            v /= n > 1e-6f ? n : 1;
        return true;
    }
    // Past the newest: carried on as it was going, a little way.
    out = states.back();
    const float ahead = std::min(clock - out.clock, 0.5f);
    out.position += out.velocity * ahead;
    out.clock = clock;
    return true;
}

struct NetSession::Peer {
    net::Address address;
    uint8_t id = 0;
    uint64_t heardMs = 0;
    uint32_t chatTaken = 0; // the player's chat lines the host has
    uint32_t echo = 0;      // the time in its last status
    uint64_t echoAtMs = 0;  // and when that arrived
};

NetSession::~NetSession() = default;

std::unique_ptr<NetSession> NetSession::host(std::unique_ptr<net::UdpSocket> socket, const std::string& name,
                                             const NetSettings& settings, const std::string& password) {
    std::unique_ptr<NetSession> s(new NetSession);
    s->socket_ = std::move(socket);
    s->host_ = true;
    s->sessionName_ = name;
    s->password_ = password;
    s->settings_ = settings;
    s->myId_ = 0;
    s->hostId_ = 0;
    std::random_device random;
    for (uint8_t& b : s->secret_)
        b = uint8_t(random());
    NetPlayer me;
    me.id = 0;
    me.name = name;
    me.car = "vpbug";
    s->players_.push_back(me);
    s->wish_ = me;
    return s;
}

std::unique_ptr<NetSession> NetSession::join(std::unique_ptr<net::UdpSocket> socket, const net::Address& address,
                                             const std::string& name, const std::string& password) {
    std::unique_ptr<NetSession> s(new NetSession);
    s->socket_ = std::move(socket);
    s->hostAddress_ = address;
    s->password_ = password;
    s->wish_.name = name;
    s->wish_.car = "vpbug";
    return s;
}

const NetPlayer* NetSession::player(uint8_t id) const {
    for (const NetPlayer& p : players_)
        if (p.id == id)
            return &p;
    return nullptr;
}

NetPlayer* NetSession::mutablePlayer(uint8_t id) { return const_cast<NetPlayer*>(player(id)); }

std::vector<NetNotice> NetSession::takeNotices() {
    std::vector<NetNotice> out;
    out.swap(notices_);
    return out;
}

void NetSession::notice(NetNotice::Kind kind, const std::string& name, int reason) {
    notices_.push_back({kind, name, reason});
}

int NetSession::rejected(uint8_t id) const {
    const auto it = rejected_.find(id);
    return it == rejected_.end() ? 0 : it->second;
}

uint8_t NetSession::freeId() const {
    for (uint8_t id = 1; id < kNoPlayer; ++id)
        if (!player(id))
            return id;
    return kNoPlayer;
}

uint8_t NetSession::teamWithFewer() const {
    int counts[2] = {};
    for (const NetPlayer& p : players_)
        ++counts[p.team & 1];
    return counts[1] < counts[0] ? 1 : 0;
}

bool NetSession::silentTooLong(uint64_t heardMs, uint64_t nowMs) const {
    // Loading a city stops the game for a while: no word then means
    // nothing, nor in the first moments after.
    const uint64_t limit =
        phase_ == Phase::Loading || (phase_ == Phase::Playing && !counting()) ? kLoadingTimeoutMs : kTimeoutMs;
    return nowMs > heardMs + limit;
}

void NetSession::addChat(const std::string& from, const std::string& text) {
    chat_.push_back({++chatSeq_, from, text.substr(0, 120)});
    while (chat_.size() > kChatKept)
        chat_.pop_front();
}

void NetSession::setCar(const std::string& car, uint8_t paint) {
    wish_.car = car;
    wish_.paint = paint;
    if (host_)
        if (NetPlayer* p = mutablePlayer(myId_)) {
            p->car = car;
            p->paint = paint;
        }
}

void NetSession::setTeam(uint8_t team) {
    wish_.team = team & 1;
    teamChosen_ = true;
    if (host_)
        if (NetPlayer* p = mutablePlayer(myId_))
            p->team = team & 1;
}

void NetSession::setReady(bool ready) { wish_.ready = ready; }

void NetSession::say(const std::string& text) {
    if (text.empty())
        return;
    if (host_)
        addChat(wish_.name, text);
    else
        outbox_.push_back(text.substr(0, 120));
}

void NetSession::setSettings(const NetSettings& settings) {
    if (!host_ || phase_ != Phase::Lobby)
        return;
    settings_ = settings;
    settings_.maxPlayers = std::clamp<uint8_t>(settings_.maxPlayers, 1, kMaxNetPlayers);
}

bool NetSession::everyoneReady() const {
    for (const NetPlayer& p : players_)
        if (p.id != myId_ && !p.ready)
            return false;
    return true;
}

Error NetSession::start(uint64_t nowMs) {
    if (!host_ || phase_ != Phase::Lobby)
        return Error::fail("only the host starts, from the lobby");
    if (!everyoneReady())
        return Error::fail("not everyone is ready");
    seed_ = uint32_t(nowMs * 2654435761u) ^ (round_ << 16) ^ 0x5eed;
    phase_ = Phase::Loading;
    loadingSinceMs_ = nowMs;
    goMs_ = 0;
    for (NetPlayer& p : players_)
        p.loaded = p.bot;
    cars_.clear();
    gameState_.clear();
    over_ = false;
    haveMyCar_ = false;
    wish_.loaded = false;
    notice(NetNotice::Kind::Started);
    lastLobbyMs_ = 0; // tell them now
    return Error::success();
}

void NetSession::backToLobby() {
    if (!host_ || phase_ == Phase::Lobby)
        return;
    phase_ = Phase::Lobby;
    ++round_;
    for (NetPlayer& p : players_)
        p.ready = p.loaded = p.bot;
    wish_.ready = wish_.loaded = false;
    goMs_ = 0;
    cars_.clear();
    gameState_.clear();
    over_ = false;
    haveMyCar_ = false;
    notice(NetNotice::Kind::BackToLobby);
    lastLobbyMs_ = 0;
}

void NetSession::setLoaded() {
    wish_.loaded = true;
    if (host_)
        if (NetPlayer* p = mutablePlayer(myId_))
            p->loaded = true;
}

float NetSession::clock(uint64_t nowMs) const {
    if (host_)
        return goMs_ ? float((double(nowMs) - double(goMs_)) / 1000) : 0;
    return clockKnown_ ? float(double(nowMs) / 1000 + clockOffset_) : 0;
}

void NetSession::sendCar(const NetCarState& state) {
    myCar_ = state;
    haveMyCar_ = true;
}

void NetSession::setGameState(const std::vector<uint8_t>& state, bool over) {
    if (!host_)
        return;
    gameState_ = state;
    over_ = over;
}

void NetSession::storeCar(uint8_t id, const NetCarState& s) {
    auto& q = cars_[id];
    if (!q.empty() && s.clock <= q.back().clock) {
        // Late: in its place, unless it's one already there.
        for (const NetCarState& o : q)
            if (o.clock == s.clock)
                return;
        q.insert(
            std::upper_bound(q.begin(), q.end(), s.clock, [](float c, const NetCarState& o) { return c < o.clock; }),
            s);
    } else
        q.push_back(s);
    while (q.size() > kStatesKept || (q.size() > 2 && q.front().clock < q.back().clock - 2))
        q.pop_front();
}

void NetSession::dropPlayer(uint8_t id, bool notify) {
    const NetPlayer* p = player(id);
    if (!p)
        return;
    if (notify)
        notice(NetNotice::Kind::Left, p->name);
    players_.erase(std::remove_if(players_.begin(), players_.end(), [id](const NetPlayer& q) { return q.id == id; }),
                   players_.end());
    peers_.erase(std::remove_if(peers_.begin(), peers_.end(), [id](const auto& q) { return q->id == id; }),
                 peers_.end());
    cars_.erase(id);
}

uint8_t NetSession::addBot(const std::string& name, const std::string& car) {
    const uint8_t id = freeId();
    if (!host_ || phase_ != Phase::Lobby || players_.size() >= settings_.maxPlayers || id == kNoPlayer)
        return kNoPlayer;
    NetPlayer p;
    p.id = id;
    p.name = name;
    p.car = car;
    p.team = teamWithFewer();
    p.ready = p.loaded = p.bot = true;
    players_.push_back(p);
    lastLobbyMs_ = 0;
    return id;
}

void NetSession::removeBots() {
    std::vector<uint8_t> bots;
    for (const NetPlayer& p : players_)
        if (p.bot)
            bots.push_back(p.id);
    for (uint8_t id : bots)
        dropPlayer(id, false);
}

void NetSession::sendBotCar(uint8_t id, const NetCarState& state) {
    if (const NetPlayer* p = player(id); host_ && p && p->bot)
        storeCar(id, state);
}

void NetSession::eject(uint8_t id) {
    if (!host_ || id == myId_)
        return;
    for (const auto& peer : peers_)
        if (peer->id == id)
            for (int i = 0; i < 2; ++i)
                socket_->send(peer->address, Writer(kKick).out);
    dropPlayer(id, true);
}

void NetSession::leave() {
    if (closed_)
        return;
    if (host_) {
        // The player who's been in longest (the lowest id) takes over, if
        // two are left to play on (computer players go with the host).
        uint8_t successor = kNoPlayer;
        int left = 0;
        for (const NetPlayer& p : players_)
            if (p.id != myId_ && !p.bot && p.address.port) {
                ++left;
                successor = std::min(successor, p.id);
            }
        Writer w(kBye);
        w.u8(left >= 2 ? successor : kNoPlayer);
        for (const auto& peer : peers_)
            for (int i = 0; i < 3; ++i)
                socket_->send(peer->address, w.out);
    } else if (myId_ != kNoPlayer) {
        Writer w(kLeave);
        w.u8(myId_);
        for (int i = 0; i < 3; ++i)
            socket_->send(hostAddress_, w.out);
    }
    closed_ = true;
}

void NetSession::update(uint64_t nowMs) {
    nowMs_ = nowMs;
    if (closed_)
        return;
    receive(nowMs);
    if (closed_)
        return;
    if (host_) {
        for (size_t i = 0; i < peers_.size();)
            if (silentTooLong(peers_[i]->heardMs, nowMs))
                dropPlayer(peers_[i]->id, true);
            else
                ++i;
        if (NetPlayer* me = mutablePlayer(myId_)) {
            me->name = wish_.name;
            me->ready = true; // the host starts when it likes
        }
        if (phase_ == Phase::Loading) {
            bool all = true;
            for (const NetPlayer& p : players_)
                all = all && p.loaded;
            if (all || nowMs > loadingSinceMs_ + kLoadingTimeoutMs) {
                phase_ = Phase::Playing;
                goMs_ = nowMs + 3000; // the countdown
            }
        }
        if (nowMs >= lastLobbyMs_ + kLobbyEveryMs) {
            lastLobbyMs_ = nowMs;
            sendLobby(nowMs);
        }
        if (phase_ == Phase::Playing && nowMs >= lastGameMs_ + kGameEveryMs) {
            lastGameMs_ = nowMs;
            sendGame(nowMs);
        }
        if (phase_ == Phase::Playing && nowMs >= lastCarsMs_ + kCarsEveryMs) {
            lastCarsMs_ = nowMs;
            relayCars();
        }
        return;
    }
    // A player: joining, or in the session.
    if (myId_ == kNoPlayer) {
        if (!joinStartMs_)
            joinStartMs_ = nowMs;
        if (nowMs > joinStartMs_ + kJoinTimeoutMs) {
            closed_ = true;
            notice(NetNotice::Kind::Refused, {}, kRefusedTimeout);
            return;
        }
        if (!joinSentMs_ || nowMs >= joinSentMs_ + kJoinEveryMs) {
            joinSentMs_ = nowMs;
            Writer w(kJoin);
            w.u8(kNetVersion);
            w.str(wish_.name);
            w.u8(haveProof_ ? 16 : 0); // the password proved, once the host's asked
            if (haveProof_)
                for (uint8_t b : proof_)
                    w.u8(b);
            socket_->send(hostAddress_, w.out);
        }
        return;
    }
    if (silentTooLong(lastHeardMs_, nowMs)) {
        hostGone(nowMs, kNoPlayer); // gone without a word: someone takes over, if they can
        if (closed_ || host_)
            return;
    }
    if (nowMs >= lastStatusMs_ + kStatusEveryMs) {
        lastStatusMs_ = nowMs;
        sendStatus(nowMs);
    }
    if (phase_ == Phase::Playing && haveMyCar_ && nowMs >= lastCarsMs_ + kCarsEveryMs) {
        lastCarsMs_ = nowMs;
        Writer w(kCar);
        writeCar(w, myCar_);
        socket_->send(hostAddress_, w.out);
    }
}

void NetSession::receive(uint64_t nowMs) {
    net::Address from;
    std::vector<uint8_t> data;
    for (int n = 0; n < 512 && socket_->receive(from, data); ++n) {
        if (host_)
            handleHost(from, data, nowMs);
        else if (from == hostAddress_)
            handlePlayer(from, data, nowMs);
        if (closed_)
            return;
    }
}

void NetSession::handleHost(const net::Address& from, const std::vector<uint8_t>& data, uint64_t nowMs) {
    Reader r(data);
    Peer* peer = nullptr;
    for (const auto& p : peers_)
        if (p->address == from)
            peer = p.get();
    switch (typeOf(data)) {
    case kQuery: {
        const uint8_t version = r.u8();
        const uint32_t time = r.u32();
        if (!r.ok)
            return;
        Writer w(kInfo);
        w.u8(kNetVersion);
        w.str(sessionName_);
        w.u8(uint8_t(settings_.mode));
        w.u8(settings_.city);
        w.u8(uint8_t(players_.size()));
        w.u8(settings_.maxPlayers);
        w.u8(uint8_t((phase_ != Phase::Lobby ? 1 : 0) | (password_.empty() ? 0 : 2)));
        w.u32(time);
        (void)version;
        socket_->send(from, w.out);
        return;
    }
    case kJoin: {
        const uint8_t version = r.u8();
        const std::string name = r.str();
        const uint8_t proofSize = r.u8();
        std::array<uint8_t, 16> proof{};
        for (size_t i = 0; i < proofSize && i < proof.size(); ++i)
            proof[i] = r.u8();
        if (!r.ok)
            return;
        if (peer) { // asked again: the welcome was lost
            Writer w(kWelcome);
            w.u8(peer->id);
            socket_->send(from, w.out);
            return;
        }
        // A password isn't sent: the host asks the player to prove it
        // (HMAC-MD5 of a challenge it hasn't used before, and the name).
        int refused = version != kNetVersion                    ? kRefusedVersion
                      : players_.size() >= settings_.maxPlayers ? kRefusedFull
                                                                : 0;
        if (!refused && !password_.empty()) {
            const auto it = challenges_.find(from);
            if (it == challenges_.end() || proofSize != 16) {
                Challenge c;
                Md5 h;
                ++nonceCount_;
                h.update(secret_.data(), secret_.size());
                h.update(&nonceCount_, sizeof nonceCount_);
                h.update(&nowMs, sizeof nowMs);
                c.nonce = h.digest();
                c.sentMs = nowMs;
                challenges_[from] = c;
                Writer w(kChallenge);
                for (uint8_t b : c.nonce)
                    w.u8(b);
                socket_->send(from, w.out);
                return;
            }
            const bool right = passwordProof(password_, it->second.nonce, name) == proof;
            challenges_.erase(it);
            if (!right)
                refused = kRefusedPassword;
        }
        const uint8_t id = freeId();
        if (!refused && id == kNoPlayer)
            refused = kRefusedFull;
        if (refused) {
            Writer w(kRefuse);
            w.u8(uint8_t(refused));
            socket_->send(from, w.out);
            return;
        }
        auto p = std::make_unique<Peer>();
        p->address = from;
        p->id = id;
        p->heardMs = nowMs;
        peers_.push_back(std::move(p));
        NetPlayer player;
        player.id = id;
        player.name = name.empty() ? "Player" : name;
        player.car = "vpbug";
        player.team = teamWithFewer();
        player.address = from;
        // Joining a game already going: loaded in when its machine's ready.
        players_.push_back(player);
        notice(NetNotice::Kind::Joined, player.name);
        Writer w(kWelcome);
        w.u8(id);
        socket_->send(from, w.out);
        lastLobbyMs_ = 0;
        return;
    }
    default:
        break;
    }
    if (!peer)
        return;
    peer->heardMs = nowMs;
    NetPlayer* p = mutablePlayer(peer->id);
    if (!p)
        return;
    switch (typeOf(data)) {
    case kStatus: {
        r.u8(); // the id: the address says who
        const uint32_t round = r.u32();
        const std::string name = r.str(), car = r.str();
        const uint8_t paint = r.u8(), team = r.u8(), flags = r.u8();
        const uint16_t ping = r.u16();
        const uint32_t time = r.u32(), chatSeq = r.u32();
        const std::string text = r.str();
        if (!r.ok)
            return;
        if (!name.empty())
            p->name = name;
        if (!car.empty())
            p->car = car;
        p->paint = paint;
        if (flags & 4) // the player's chosen; else the host's choice stands
            p->team = team & 1;
        p->ping = ping;
        if (round == round_) {
            p->ready = (flags & 1) != 0;
            if (phase_ != Phase::Lobby)
                p->loaded = (flags & 2) != 0;
        }
        peer->echo = time;
        peer->echoAtMs = nowMs;
        if (peer->chatTaken == kChatUnknown) // a new host: from here on
            peer->chatTaken = chatSeq ? chatSeq - 1 : 0;
        if (chatSeq == peer->chatTaken + 1 && !text.empty()) {
            addChat(p->name, text);
            peer->chatTaken = chatSeq;
        }
        return;
    }
    case kCar: {
        const NetCarState s = readCar(r);
        if (r.ok && phase_ == Phase::Playing && plausible(peer->id, s))
            storeCar(peer->id, s);
        return;
    }
    case kLeave:
        dropPlayer(peer->id, true);
        return;
    default:
        return;
    }
}

void NetSession::sendLobby(uint64_t nowMs) {
    for (const auto& peer : peers_) {
        Writer w(kLobby);
        w.u8(uint8_t(phase_));
        w.u32(round_);
        w.u32(seed_);
        w.str(sessionName_);
        writeSettings(w, settings_);
        w.u8(peer->id);
        w.u8(uint8_t(players_.size()));
        for (const NetPlayer& p : players_) {
            w.u8(p.id);
            w.str(p.name);
            w.str(p.car);
            w.u8(p.paint);
            w.u8(p.team);
            w.u8(uint8_t((p.ready ? 1 : 0) | (p.loaded ? 2 : 0) | (p.bot ? 4 : 0) | (p.cheater ? 8 : 0)));
            w.u16(p.ping);
            w.u32(p.address.ip);
            w.u16(p.address.port);
        }
        w.u8(uint8_t(chat_.size()));
        for (const NetChatLine& c : chat_) {
            w.u32(c.seq);
            w.str(c.from);
            w.str(c.text);
        }
        w.u32(peer->chatTaken);
        w.u32(peer->echo);
        w.u16(uint16_t(std::min<uint64_t>(nowMs - peer->echoAtMs, 65535))); // how long it was held
        socket_->send(peer->address, w.out);
    }
}

void NetSession::sendGame(uint64_t nowMs) {
    Writer w(kGame);
    w.f32(clock(nowMs));
    w.u8(over_ ? 1 : 0);
    w.u16(uint16_t(std::min<size_t>(gameState_.size(), 1200)));
    w.out.insert(w.out.end(), gameState_.begin(),
                 gameState_.begin() + std::ptrdiff_t(std::min<size_t>(gameState_.size(), 1200)));
    for (const auto& peer : peers_)
        socket_->send(peer->address, w.out);
}

void NetSession::relayCars() {
    // Each player gets the newest state of every other car.
    for (const auto& peer : peers_) {
        Writer w(kCars);
        std::vector<std::pair<uint8_t, const NetCarState*>> list;
        if (haveMyCar_)
            list.push_back({myId_, &myCar_});
        for (const auto& [id, states] : cars_)
            if (id != peer->id && id != myId_ && !states.empty())
                list.push_back({id, &states.back()});
        w.u8(uint8_t(list.size()));
        for (const auto& [id, s] : list) {
            w.u8(id);
            writeCar(w, *s);
        }
        socket_->send(peer->address, w.out);
    }
}

void NetSession::sendStatus(uint64_t nowMs) {
    Writer w(kStatus);
    w.u8(myId_);
    w.u32(round_);
    w.str(wish_.name);
    w.str(wish_.car);
    w.u8(wish_.paint);
    w.u8(wish_.team);
    w.u8(uint8_t((wish_.ready ? 1 : 0) | (wish_.loaded ? 2 : 0) | (teamChosen_ ? 4 : 0)));
    w.u16(rtt_);
    w.u32(uint32_t(nowMs));
    w.u32(outbox_.empty() ? 0 : outSeq_ + 1);
    w.str(outbox_.empty() ? std::string() : outbox_.front());
    socket_->send(hostAddress_, w.out);
}

void NetSession::handlePlayer(const net::Address& from, const std::vector<uint8_t>& data, uint64_t nowMs) {
    (void)from;
    Reader r(data);
    const uint8_t type = typeOf(data);
    if (myId_ == kNoPlayer) {
        if (type == kWelcome) {
            myId_ = r.u8();
            lastHeardMs_ = nowMs;
            lastStatusMs_ = 0;
        } else if (type == kChallenge) {
            std::array<uint8_t, 16> nonce{};
            if (data.size() < r.at + nonce.size())
                return;
            std::copy_n(data.begin() + std::ptrdiff_t(r.at), nonce.size(), nonce.begin());
            proof_ = passwordProof(password_, nonce, wish_.name.substr(0, 255));
            haveProof_ = true;
            joinSentMs_ = 0; // answered at once
        } else if (type == kRefuse) {
            closed_ = true;
            notice(NetNotice::Kind::Refused, {}, r.u8());
        }
        return;
    }
    lastHeardMs_ = nowMs;
    switch (type) {
    case kLobby: {
        const Phase phase = Phase(std::min<uint8_t>(r.u8(), uint8_t(Phase::Playing)));
        const uint32_t round = r.u32(), seed = r.u32();
        const std::string name = r.str();
        const NetSettings settings = readSettings(r);
        const uint8_t you = r.u8();
        std::vector<NetPlayer> players(r.u8());
        for (NetPlayer& p : players) {
            p.id = r.u8();
            p.name = r.str();
            p.car = r.str();
            p.paint = r.u8();
            p.team = r.u8() & 1;
            const uint8_t flags = r.u8();
            p.ready = (flags & 1) != 0;
            p.loaded = (flags & 2) != 0;
            p.bot = (flags & 4) != 0;
            p.cheater = (flags & 8) != 0;
            p.ping = r.u16();
            p.address.ip = r.u32();
            p.address.port = r.u16();
        }
        std::vector<NetChatLine> lines(r.u8());
        for (NetChatLine& c : lines) {
            c.seq = r.u32();
            c.from = r.str();
            c.text = r.str();
        }
        const uint32_t taken = r.u32(), echo = r.u32();
        const uint16_t held = r.u16();
        if (!r.ok || you != myId_)
            return;
        // Who's come and gone.
        for (const NetPlayer& p : players)
            if (p.id != myId_ && !player(p.id) && !players_.empty())
                notice(NetNotice::Kind::Joined, p.name);
        for (const NetPlayer& p : players_) {
            bool still = false;
            for (const NetPlayer& q : players)
                still = still || q.id == p.id;
            if (!still && p.id != myId_)
                notice(NetNotice::Kind::Left, p.name);
        }
        for (const NetPlayer& p : players) // newly called a cheater
            if (const NetPlayer* was = player(p.id); p.cheater && was && !was->cheater)
                notice(NetNotice::Kind::Suspect, p.name);
        players_ = std::move(players);
        if (const NetPlayer* me = player(myId_); me && !teamChosen_)
            wish_.team = me->team; // the host put us on a side
        sessionName_ = name;
        settings_ = settings;
        // A new round: ready again from scratch. A new phase: the game
        // started, or it's over.
        if (round != round_) {
            round_ = round;
            wish_.ready = wish_.loaded = false;
        }
        if (phase != phase_) {
            if (phase == Phase::Loading || (phase == Phase::Playing && phase_ == Phase::Lobby)) {
                cars_.clear();
                gameState_.clear();
                over_ = false;
                clockKnown_ = false;
                haveMyCar_ = false;
                wish_.loaded = false;
                notice(NetNotice::Kind::Started);
            } else if (phase == Phase::Lobby) {
                cars_.clear();
                clockKnown_ = false;
                haveMyCar_ = false;
                notice(NetNotice::Kind::BackToLobby);
            }
            phase_ = phase;
        }
        seed_ = seed;
        for (const NetChatLine& c : lines)
            if (c.seq > chatSeq_) {
                chat_.push_back(c);
                chatSeq_ = c.seq;
            }
        while (chat_.size() > kChatKept)
            chat_.pop_front();
        while (!outbox_.empty() && taken > outSeq_) {
            outbox_.pop_front();
            ++outSeq_;
        }
        if (echo) {
            const uint32_t back = uint32_t(nowMs) - echo;
            if (back < 10000 && back >= held)
                rtt_ = uint16_t(back - held);
        }
        return;
    }
    case kGame: {
        const float hostClock = r.f32();
        const bool over = r.u8() != 0;
        const size_t n = r.u16();
        if (!r.ok || r.at + n > data.size() || phase_ != Phase::Playing)
            return;
        gameState_.assign(data.begin() + std::ptrdiff_t(r.at), data.begin() + std::ptrdiff_t(r.at + n));
        over_ = over;
        // The host's clock, half the way there and back since.
        const float offset = hostClock + float(rtt_) / 2000 - float(double(nowMs) / 1000);
        if (!clockKnown_ || std::fabs(offset - clockOffset_) > 0.5f)
            clockOffset_ = offset;
        else
            clockOffset_ += (offset - clockOffset_) * 0.1f;
        clockKnown_ = true;
        return;
    }
    case kCars: {
        const size_t n = r.u8();
        for (size_t i = 0; i < n && r.ok; ++i) {
            const uint8_t id = r.u8();
            const NetCarState s = readCar(r);
            if (r.ok && id != myId_ && phase_ == Phase::Playing)
                storeCar(id, s);
        }
        return;
    }
    case kKick:
        closed_ = true;
        notice(NetNotice::Kind::Ejected);
        return;
    case kBye: {
        const uint8_t successor = r.u8();
        hostGone(nowMs, r.ok ? successor : kNoPlayer);
        if (!closed_ && !host_ && r.ok && successor == kNoPlayer) { // the host said nobody's to go on
            closed_ = true;
            notice(NetNotice::Kind::HostQuit);
        }
        return;
    }
    default:
        return;
    }
}

bool NetSession::plausible(uint8_t id, const NetCarState& s) {
    // What a player's machine says of its car, as the host checks it: not
    // stamped ahead of the race clock, and not further from where it last
    // was than a car can go in the time, unless it's been put back since.
    bool ok = s.clock <= clock(nowMs_) + kAheadOfClock && length(s.velocity) < kFastest;
    const auto it = cars_.find(id);
    if (ok && it != cars_.end() && !it->second.empty()) {
        const NetCarState& last = it->second.back();
        const float dt = s.clock - last.clock;
        if (dt > 0 && s.resets == last.resets && length(s.position - last.position) > 2 + kFastest * dt)
            ok = false;
    }
    if (ok)
        return true;
    if (++rejected_[id] == kCheater)
        if (NetPlayer* p = mutablePlayer(id)) {
            p->cheater = true;
            notice(NetNotice::Kind::Suspect, p->name);
        }
    return false;
}

void NetSession::hostGone(uint64_t nowMs, uint8_t successor) {
    // The players left (not the host, nor its computer players) play on
    // if there are two of them, under the one the host named, else the
    // one who's been in longest; each knows the others' addresses from
    // the lobby.
    std::vector<uint8_t> left;
    for (const NetPlayer& p : players_)
        if (p.id != hostId_ && !p.bot && (p.address.port || p.id == myId_))
            left.push_back(p.id);
    if (left.size() < 2) {
        closed_ = true;
        notice(NetNotice::Kind::HostQuit);
        return;
    }
    const uint8_t next = std::find(left.begin(), left.end(), successor) != left.end()
                             ? successor
                             : *std::min_element(left.begin(), left.end());
    const uint8_t oldHost = hostId_;
    std::vector<uint8_t> gone;
    for (const NetPlayer& p : players_)
        if (p.id == oldHost || p.bot)
            gone.push_back(p.id);
    for (uint8_t id : gone) {
        players_.erase(
            std::remove_if(players_.begin(), players_.end(), [id](const NetPlayer& q) { return q.id == id; }),
            players_.end());
        cars_.erase(id);
    }
    hostId_ = next;
    const NetPlayer* now = player(next);
    notice(NetNotice::Kind::HostMoved, now ? now->name : std::string());
    if (next == myId_) {
        becomeHost(nowMs);
        return;
    }
    hostAddress_ = now->address;
    lastHeardMs_ = nowMs;
}

void NetSession::becomeHost(uint64_t nowMs) {
    // Our clock (the old host's, as we kept it) goes on as the race clock.
    const float raceClock = clock(nowMs);
    const bool knew = clockKnown_;
    host_ = true;
    hostId_ = myId_;
    peers_.clear();
    challenges_.clear();
    std::random_device random;
    for (uint8_t& b : secret_)
        b = uint8_t(random());
    for (NetPlayer& p : players_) {
        if (p.id == myId_) {
            p.address = {};
            p.name = wish_.name;
            p.car = wish_.car;
            p.paint = wish_.paint;
            p.loaded = wish_.loaded;
            continue;
        }
        auto peer = std::make_unique<Peer>();
        peer->address = p.address;
        peer->id = p.id;
        peer->heardMs = nowMs;
        peer->chatTaken = kChatUnknown;
        peers_.push_back(std::move(peer));
    }
    for (const std::string& line : outbox_) // what we'd said that the old host hadn't taken
        addChat(wish_.name, line);
    outbox_.clear();
    goMs_ = 0;
    if (phase_ == Phase::Playing)
        goMs_ = knew ? uint64_t(std::max<int64_t>(1, int64_t(nowMs) - int64_t(std::llround(raceClock * 1000))))
                     : nowMs + 3000; // it hadn't started: the countdown again
    else if (phase_ == Phase::Loading)
        loadingSinceMs_ = nowMs;
    lastLobbyMs_ = lastGameMs_ = lastCarsMs_ = 0;
}

void NetBrowser::update(uint64_t nowMs) {
    net::Address from;
    std::vector<uint8_t> data;
    for (int n = 0; n < 256 && socket_->receive(from, data); ++n) {
        if (typeOf(data) != kInfo)
            continue;
        Reader r(data);
        Found f;
        f.address = from;
        f.sameVersion = r.u8() == kNetVersion;
        f.name = r.str();
        f.mode = NetMode(std::min<uint8_t>(r.u8(), uint8_t(NetMode::CopsRobbers)));
        f.city = r.u8();
        f.players = r.u8();
        f.maxPlayers = r.u8();
        const uint8_t flags = r.u8();
        const uint32_t time = r.u32();
        if (!r.ok)
            continue;
        f.playing = (flags & 1) != 0;
        f.password = (flags & 2) != 0;
        f.ping = uint16_t(std::min<uint32_t>(uint32_t(nowMs) - time, 65535));
        f.heardMs = nowMs;
        auto it = std::find_if(found_.begin(), found_.end(), [&](const Found& o) { return o.address == from; });
        if (it != found_.end())
            *it = f;
        else
            found_.push_back(f);
    }
    found_.erase(
        std::remove_if(found_.begin(), found_.end(), [nowMs](const Found& f) { return nowMs > f.heardMs + 4000; }),
        found_.end());
    if (lastAskMs_ && nowMs < lastAskMs_ + 1000)
        return;
    lastAskMs_ = nowMs;
    Writer w(kQuery);
    w.u8(kNetVersion);
    w.u32(uint32_t(nowMs));
    socket_->send(net::Address::broadcast(kNetPort), w.out);
    for (uint32_t b : broadcasts_)
        socket_->send({b, kNetPort}, w.out);
    for (const net::Address& a : asked_)
        socket_->send(a, w.out);
}

} // namespace reluva::age
